#pragma once

#include <string>  // std::string

#include "configs.hpp"  // 配置文件解析
#include "logs.hpp"     // 日志
#include "network.hpp"  // 工具函数
#include "process.hpp"

// 清理 Hexo 缓存和生成文件。
inline int hexoClean(const Config &config, std::string_view extraArguments = {}) {
    logStep("清理缓存");
    int returnCode = runCommand(std::format("{}hexo clean{}", config.packageManagerCommand, extraArguments));
    if (returnCode != 0) logError("hexo clean 失败（退出码 {}）", returnCode);
    return returnCode;
}

// 自动选择可用端口，启动状态和访问地址由 Hexo 输出。
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

// 清理和附属 Hexo 命令共享配置选项，其余参数交给 generate/deploy。
inline std::string sharedHexoArguments(const std::vector<std::string> &arguments) {
    std::vector<std::string> shared;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const auto &argument = arguments[index];
        if (argument == "--") break;
        if (argument == "--config") {
            shared.push_back(argument);
            ++index;
            if (index == arguments.size() || arguments[index].empty() || arguments[index].starts_with('-'))
                throw std::runtime_error(argument + " 缺少参数");
            shared.push_back(arguments[index]);
        } else if (argument.starts_with("--config=") || argument == "--safe" || argument.starts_with("--safe=")) {
            shared.push_back(argument);
        }
    }
    return joinShellArguments(shared);
}

// 在首次清理前检查参数，保证 build/deploy 按固定顺序执行。
inline void validateWorkflowArguments(const std::vector<std::string> &arguments) {
    for (const auto &argument : arguments) {
        if (argument == "--") break;
        const std::string_view option = std::string_view(argument).substr(0, argument.find('='));
        if (option == "--deploy" || option == "--generate" || option == "--watch" || option == "--help" ||
            option == "-d" || option == "-g" || option == "-w" || option == "-h" || option == "--d" ||
            option == "--g" || option == "--w" || option == "--h")
            throw std::runtime_error("build/deploy 不接受改变流程的 Hexo 参数：" + argument +
                                     "；请使用对应命令，帮助请使用 -- 之前的 --help");
        // 组合短选项可能包含 -d/-w，因此要求分别书写。
        if (argument.starts_with('-') && !argument.starts_with("--") && argument.size() > 2)
            throw std::runtime_error("build/deploy 的短选项请分开书写，参数值用空格分隔：" + argument);
    }
}

inline bool isHexoToolCommand(std::string_view command) {
    const auto start = command.find_first_not_of(" \t");
    if (start == std::string_view::npos) return false;
    command.remove_prefix(start);
    return command.substr(0, command.find_first_of(" \t")) == "hexo";
}

// clean → generate → 在 manifest 中声明的附属工具。
inline int hexoBuild(const Config &config, const std::vector<std::string> &arguments = {}) {
    validateWorkflowArguments(arguments);
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
    if (int const cleanExitCode = hexoClean(config, sharedArguments); cleanExitCode != 0) {
        logError("初始清理失败");
        return cleanExitCode;
    }
    logStep("生成静态文件");
    const int generateExitCode =
        runCommand(std::format("{}hexo generate{}", config.packageManagerCommand, extraArguments));
    if (generateExitCode != 0) {
        logError("hexo generate 失败（退出码 {}）", generateExitCode);
        return generateExitCode;
    }

    for (const auto &[keyword, command] : config.additionalTools) {
        if (!isDependenciesPresent(dependencyTree.rootref(), keyword)) {
            logTrace("未安装 {}，跳过", keyword);
            continue;
        }
        logStep("运行 {}", keyword);
        auto toolCommand = command;
        if (isHexoToolCommand(toolCommand))
            toolCommand.insert(toolCommand.find_first_not_of(" \t") + 4, sharedArguments);
        const int toolExitCode = runCommand(std::format("{}{}", config.packageManagerCommand, toolCommand));
        if (toolExitCode != 0) {
            logError("{} 执行失败（退出码 {}）", keyword, toolExitCode);
            return toolExitCode;
        }
    }

    return 0;
}

// 复用构建流程，部署配置和插件执行交给项目自身的 Hexo。
inline int hexoDeploy(const Config &config, const std::vector<std::string> &arguments = {}) {
    if (int const buildExitCode = hexoBuild(config, arguments); buildExitCode != 0) return buildExitCode;

    logStep("部署静态文件");
    const auto extraArguments = joinShellArguments(arguments);
    const int deployExitCode =
        runCommand(std::format("{}hexo deploy{}", config.packageManagerCommand, extraArguments), true);
    if (deployExitCode != 0) {
        logError("hexo deploy 失败（退出码 {}）", deployExitCode);
        return deployExitCode;
    }
    // 空部署配置或缺失插件也可能返回 0，这里统一按退出码决定清理。
    const int finalCleanExitCode = hexoClean(config, sharedHexoArguments(arguments));
    if (finalCleanExitCode != 0) logError("部署已完成，但最终清理失败");
    return finalCleanExitCode;
}
