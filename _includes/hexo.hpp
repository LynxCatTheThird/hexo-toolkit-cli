#pragma once

#include <chrono>  // std::chrono::high_resolution_clock
#include <string>  // std::string

#include "configs.hpp"  // 配置文件解析
#include "logs.hpp"     // 日志
#include "utils.hpp"    // 工具函数

// 函数用途：清理 Hexo 产生的缓存文件
inline void hexoClean() {
    spdlog::info("清理 Hexo 缓存文件...");
    int returnCode = runCommand(std::format("{}hexo clean{}", config.packageManagerCommand, DEVICE_NULL));
    if (returnCode != 0) spdlog::warn("hexo clean 异常退出，退出码: {}", returnCode);
}

// 函数用途：启动 Hexo 本地预览服务器
inline int hexoServer() {
    ScopedTimer totalTimer("本次操作执行总");
    hexoClean();
    for (int portNumber = 4000; portNumber <= 4100; portNumber++) {
        std::string command =
            std::format("{}hexo server --port {}{}", config.packageManagerCommand, portNumber, DEVICE_NULL);
        if (!isPortInUse(portNumber)) {
            spdlog::info("正在尝试于 {} 端口启动 Hexo 本地预览服务器... ", portNumber);
            auto process = ManagedProcess::start(command);
            if (!process) {
                spdlog::error("Hexo 服务器启动失败，无法创建子进程");
                return 1;
            }

            {
                ScopedTimer startupTimer("Hexo 本地预览服务器启动");
                // 启动阶段只等到“端口开放”或“进程退出”，再加一层超时兜底。
                const auto startupDeadline =
                    std::chrono::steady_clock::now() + std::chrono::seconds(config.serverStartupTimeoutSeconds);
                bool startupTimedOut = false;
                waitWithSpinner("等待 Hexo 本地预览服务器启动...", [&]() {
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
                    spdlog::error("Hexo 服务器启动失败或意外退出，退出码: {}", *exitCode);
                    return *exitCode;
                }
                if (startupTimedOut && !isPortInUse(portNumber)) {
                    spdlog::error("Hexo 服务器启动超过 {} 秒仍未监听端口 {}", config.serverStartupTimeoutSeconds,
                                  portNumber);
                    process->terminate();
                    return 1;
                }
                if (startupTimedOut) {
                    spdlog::warn("Hexo 服务器启动较慢，已超过 {} 秒", config.serverStartupTimeoutSeconds);
                }
            }

            // 服务器成功启动，输出信息
            spdlog::info("您现在可以访问 http://localhost:{} 预览效果了", portNumber);

            // 服务器运行期间持续轮询进程状态，退出后再返回退出码。
            waitWithSpinner("Hexo 服务器运行中，等待退出...", [&]() { return process->pollExitCode().has_value(); });
            int exitCode = process->pollExitCode().value_or(-1);
            spdlog::info("Hexo 服务器已正常关闭，退出码: {}", exitCode);
            return exitCode;
        } else {
            spdlog::error("{} 端口已被占用，尝试使用下一个端口...", portNumber);
        }
    }
    return 1;
}

// 函数用途：部署 Hexo 静态文件
inline int hexoBuild() {
    ScopedTimer totalTimer("本次操作执行总");
    hexoClean();
    spdlog::info("生成静态文件...");
    {
        ScopedTimer generateTimer("生成静态文件");
        int generateExitCode = runCommand(std::format("{}hexo generate{}", config.packageManagerCommand, DEVICE_NULL));
        if (generateExitCode != 0) {
            spdlog::error("hexo generate 失败，退出码: {}，中止后续部署操作", generateExitCode);
            return generateExitCode;
        }
    }

    // 如果配置了附属工具，则执行它们
    if (config.additionalTools.empty()) {
        spdlog::info("未配置附属工具，跳过执行");
    } else {
        spdlog::info("执行附属命令...");
        ScopedTimer additionalTimer("附属工具");

        // 一次性读取并缓存依赖文件内容，避免在循环中重复读取磁盘
        std::string dependencyContent = readFileContents(config.dependenciesSearchingFile);
        if (dependencyContent.empty()) {
            spdlog::warn("无法打开或读取依赖文件 {}", config.dependenciesSearchingFile);
        }

        for (const auto &[keyword, command] : config.additionalTools) {
            if (isDependenciesPresent(dependencyContent, keyword)) {
                spdlog::info("正在执行 {} ...", keyword);
                int toolExitCode =
                    runCommand(std::format("{}{}{}", config.packageManagerCommand, command, DEVICE_NULL));
                if (toolExitCode != 0) spdlog::warn("附属工具 {} 执行异常，退出码: {}", keyword, toolExitCode);
            } else {
                spdlog::warn("本地项目中未安装 {} 或检索出错，跳过执行", keyword);
            }
        }
    }

    spdlog::info("部署静态文件...");
    int deployExitCode = runCommand(std::format("{}hexo d{}", config.packageManagerCommand, DEVICE_NULL));
    if (deployExitCode != 0) {
        spdlog::error("hexo deploy 失败，退出码: {}", deployExitCode);
        return deployExitCode;
    } else {
        hexoClean();
    }
    return 0;
}
