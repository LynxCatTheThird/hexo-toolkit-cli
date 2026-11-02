#pragma once

#include <chrono>  // std::chrono::high_resolution_clock
#include <string>  // std::string

#include "configs.hpp"  // 配置文件解析
#include "deploy.hpp"
#include "logs.hpp"     // 日志
#include "network.hpp"  // 工具函数
#include "process.hpp"

// 函数用途：清理 Hexo 产生的缓存文件
inline int hexoClean(const Config &config, std::string_view extraArguments = {}) {
    logStep("清理缓存");
    int returnCode = runCommand(std::format("{}hexo clean{}", config.packageManagerCommand, extraArguments));
    if (returnCode != 0) logError("hexo clean 失败（退出码 {}）", returnCode);
    return returnCode;
}

// Server output itself reports readiness and the actual address. Never infer it from another listener.
inline int hexoServer(const Config &config, const std::vector<std::string> &arguments = {}) {
    std::string portArgument;
    bool customPort = false;
    for (const auto &argument : arguments) {
        if (argument == "--port" || argument == "-p" || argument.starts_with("--port=") ||
            (argument.starts_with("-p") && argument.size() > 2))
            customPort = true;
    }
    if (!customPort) {
        int port = 4000;
        while (port <= 4100 && isPortInUse(static_cast<std::uint16_t>(port))) ++port;
        if (port > 4100) {
            logError("无可用端口（4000–4100 均已被占用）");
            return 1;
        }
        portArgument = std::format(" --port {}", port);
    }
    logStep("启动本地服务器");
    return runCommand(
        std::format("{}hexo server{}{}", config.packageManagerCommand, portArgument, joinShellArguments(arguments)),
        true, true);
}

// Forward shared context only; generation/deployment-specific flags do not belong to plugins.
inline std::string sharedHexoArguments(const std::vector<std::string> &arguments) {
    std::vector<std::string> shared;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const auto &argument = arguments[index];
        if (argument == "--") break;
        if (argument == "--config" || argument == "--cwd") {
            shared.push_back(argument);
            ++index;
            if (index == arguments.size() || arguments[index].empty() || arguments[index].starts_with('-'))
                throw std::runtime_error(argument + " 缺少参数");
            shared.push_back(arguments[index]);
        } else if (argument.starts_with("--config=") || argument.starts_with("--cwd=") || argument == "--safe" ||
                   argument.starts_with("--safe=")) {
            shared.push_back(argument);
        }
    }
    return joinShellArguments(shared);
}

inline bool isHexoToolCommand(std::string_view command) {
    const auto start = command.find_first_not_of(" \t");
    if (start == std::string_view::npos) return false;
    command.remove_prefix(start);
    return command.substr(0, command.find_first_of(" \t")) == "hexo";
}

// 函数用途：生成 Hexo 静态文件及附属产物
inline int hexoGenerate(const Config &config, const std::vector<std::string> &arguments = {}) {
    const auto extraArguments = joinShellArguments(arguments);
    const auto sharedArguments = sharedHexoArguments(arguments);
    ryml::Tree dependencyTree;
    if (!config.additionalTools.empty()) {
        const auto dependencyContent = readFileContents(config.dependenciesSearchingFile);
        if (dependencyContent.empty()) {
            logError("无法读取依赖文件 '{}'", config.dependenciesSearchingFile);
            return 1;
        }
        dependencyTree = ryml::parse_in_arena(ryml::to_csubstr(dependencyContent));
        if (!dependencyTree.rootref().is_map()) throw std::runtime_error("依赖文件必须是 manifest 对象");
    }
    if (int const cleanExitCode = hexoClean(config, extraArguments); cleanExitCode != 0) {
        logError("初始清理失败");
        return cleanExitCode;
    }
    logStep("生成静态文件");
    {
        ScopedTimer const generateTimer("生成静态文件");
        int generateExitCode =
            runCommand(std::format("{}hexo generate{}", config.packageManagerCommand, extraArguments));
        if (generateExitCode != 0) {
            logError("hexo generate 失败（退出码 {}）", generateExitCode);
            return generateExitCode;
        }
    }

    // 如果配置了附属工具，则执行它们
    if (config.additionalTools.empty()) {
        logTrace("未配置附属工具");
    } else {
        ScopedTimer const additionalTimer("附属工具");

        for (const auto &[keyword, command] : config.additionalTools) {
            if (isDependenciesPresent(dependencyTree.rootref(), keyword)) {
                logStep("运行 {}", keyword);
                auto toolCommand = command;
                if (isHexoToolCommand(toolCommand))
                    toolCommand.insert(toolCommand.find_first_not_of(" \t") + 4, sharedArguments);
                int toolExitCode = runCommand(std::format("{}{}", config.packageManagerCommand, toolCommand));
                if (toolExitCode != 0) {
                    logError("{} 执行失败（退出码 {}）", keyword, toolExitCode);
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
inline int hexoBuild(const Config &config, const std::vector<std::string> &arguments = {}) {
    ScopedTimer const totalTimer("本次操作执行总");
    return hexoGenerate(config, arguments);
}

// 函数用途：执行 clean → generate → 附属工具 → deploy → clean 一条龙流程
inline int hexoDeploy(const Config &config, const std::vector<std::string> &arguments = {}) {
    ScopedTimer const totalTimer("本次操作执行总");
    if (int const generateExitCode = hexoGenerate(config, arguments); generateExitCode != 0) return generateExitCode;

    logStep("部署静态文件");
    const auto extraArguments = joinShellArguments(arguments);
    const auto displayCommand = std::format("{}hexo deploy{}", config.packageManagerCommand, extraArguments);
    int deployExitCode = runCommand(checkedDeployCommand(config, extraArguments), true, false, displayCommand);
    if (deployExitCode != 0) {
        logError("hexo deploy 失败（退出码 {}）", deployExitCode);
        return deployExitCode;
    }
    int const finalCleanExitCode = hexoClean(config, extraArguments);
    if (finalCleanExitCode != 0) logError("部署已完成，但最终清理失败");
    return finalCleanExitCode;
}
