#pragma once

#include <chrono>  // std::chrono::high_resolution_clock
#include <string>  // std::string

#include "configs.hpp"  // 配置文件解析
#include "logs.hpp"     // 日志
#include "utils.hpp"    // 工具函数

// 函数用途：清理 Hexo 产生的缓存文件
inline int hexoClean(std::string_view extraArguments = {}) {
    logStep("清理缓存");
    int returnCode = runCommand(
        std::format("{}hexo clean{}{}", config.packageManagerCommand, extraArguments, commandOutputRedirect()));
    if (returnCode != 0) logError("hexo clean 失败（退出码 {}）{}", returnCode, failureTraceHint());
    return returnCode;
}

// 函数用途：启动 Hexo 本地预览服务器
inline int hexoServer(std::string_view extraArguments = {}) {
    ScopedTimer totalTimer("本次操作执行总");
    if (int cleanExitCode = hexoClean(); cleanExitCode != 0) return cleanExitCode;
    for (int portNumber = 4000; portNumber <= 4100; portNumber++) {
        std::string command = std::format("{}hexo server --port {}{}{}", config.packageManagerCommand, portNumber,
                                          extraArguments, commandOutputRedirect());
        if (!isPortInUse(portNumber)) {
            logStep("启动本地服务器（端口 {}）", portNumber);
            if (dryRunEnabled) {
                spdlog::info("  $ {}", commandForLog(command));
                return 0;
            }
            logDetail("$ {}", commandForLog(command));
            auto process = ManagedProcess::start(command);
            if (!process) {
                logError("无法启动 Hexo 服务器进程");
                return 1;
            }

            {
                ScopedTimer startupTimer("本地服务器启动");
                // 启动阶段只等到“端口开放”或“进程退出”，再加一层超时兜底。
                const auto startupDeadline =
                    std::chrono::steady_clock::now() + std::chrono::seconds(config.serverStartupTimeoutSeconds);
                bool startupTimedOut = false;
                waitWithSpinner("正在启动", [&]() {
                    if (process->pollExitCode().has_value()) {
                        return true;
                    }
                    if (std::chrono::steady_clock::now() >= startupDeadline) {
                        startupTimedOut = true;
                        return true;
                    }
                    return isPortInUse(portNumber);
                });
                if (auto exitCode = process->pollExitCode()) {
                    logError("服务器启动失败（退出码 {}）{}", *exitCode, failureTraceHint());
                    return *exitCode;
                }
                if (startupTimedOut && !isPortInUse(portNumber)) {
                    logError("启动超时：{} 秒内未监听端口 {}", config.serverStartupTimeoutSeconds, portNumber);
                    process->terminate();
                    return 1;
                }
                if (startupTimedOut) {
                    spdlog::warn("服务器启动较慢，已等待 {} 秒", config.serverStartupTimeoutSeconds);
                }
            }

            logSuccess("http://localhost:{}", portNumber);

            // 服务器运行期间持续轮询进程状态，退出后再返回退出码。
            waitWithSpinner("服务器运行中", [&]() { return process->pollExitCode().has_value(); });
            int exitCode = process->pollExitCode().value_or(-1);
            if (exitCode != 0) logError("服务器已停止（退出码 {}）", exitCode);
            return exitCode;
        } else {
            logTrace("端口 {} 已占用", portNumber);
        }
    }
    return 1;
}

// 函数用途：生成 Hexo 静态文件及附属产物
inline int hexoGenerate(std::string_view extraArguments = {}) {
    if (int cleanExitCode = hexoClean(); cleanExitCode != 0) {
        logError("初始清理失败");
        return cleanExitCode;
    }
    logStep("生成静态文件");
    {
        ScopedTimer generateTimer("生成静态文件");
        int generateExitCode = runCommand(
            std::format("{}hexo generate{}{}", config.packageManagerCommand, extraArguments, commandOutputRedirect()));
        if (generateExitCode != 0) {
            logError("hexo generate 失败（退出码 {}）{}", generateExitCode, failureTraceHint());
            return generateExitCode;
        }
    }

    // 如果配置了附属工具，则执行它们
    if (config.additionalTools.empty()) {
        logTrace("未配置附属工具");
    } else {
        ScopedTimer additionalTimer("附属工具");

        // 一次性读取并缓存依赖文件内容，避免在循环中重复读取磁盘
        std::string dependencyContent = readFileContents(config.dependenciesSearchingFile);
        if (dependencyContent.empty()) {
            logError("无法读取依赖文件 '{}'", config.dependenciesSearchingFile);
            return 1;
        }

        for (const auto &[keyword, command] : config.additionalTools) {
            if (isDependenciesPresent(dependencyContent, keyword)) {
                logStep("运行 {}", keyword);
                int toolExitCode =
                    runCommand(std::format("{}{}{}", config.packageManagerCommand, command, commandOutputRedirect()));
                if (toolExitCode != 0) {
                    logError("{} 执行失败（退出码 {}）{}", keyword, toolExitCode, failureTraceHint());
                    return toolExitCode;
                }
            } else {
                logTrace("未安装 {}，跳过", keyword);
            }
        }
    }

    return 0;
}

// 函数用途：只生成本地静态文件，不执行部署
inline int hexoBuild(std::string_view extraArguments = {}) {
    ScopedTimer totalTimer("本次操作执行总");
    return hexoGenerate(extraArguments);
}

// 函数用途：执行 clean → generate → 附属工具 → deploy → clean 一条龙流程
inline int hexoDeploy(std::string_view extraArguments = {}) {
    ScopedTimer totalTimer("本次操作执行总");
    if (int generateExitCode = hexoGenerate(extraArguments); generateExitCode != 0) return generateExitCode;

    logStep("部署静态文件");
    int deployExitCode = runCommand(
        std::format("{}hexo deploy{}{}", config.packageManagerCommand, extraArguments, commandOutputRedirect()));
    if (deployExitCode != 0) {
        logError("hexo deploy 失败（退出码 {}）{}", deployExitCode, failureTraceHint());
        return deployExitCode;
    }
    int finalCleanExitCode = hexoClean();
    if (finalCleanExitCode != 0) logError("部署已完成，但最终清理失败");
    return finalCleanExitCode;
}
