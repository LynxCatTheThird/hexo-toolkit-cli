#include <cstdio>   // std::puts
#include <cstdlib>  // std::getenv
#include <exception>
#include <filesystem>  // std::filesystem
#include <string>      // std::string
#include <string_view>
#include <system_error>
#include <vector>  // std::vector

#include "commands.hpp"
#include "configs.hpp"  // 配置文件解析
#include "hexo.hpp"     // Hexo 主体业务
#include "logs.hpp"     // 日志
#include "process.hpp"
#include "spdlog/common.h"
#include "spdlog/spdlog.h"

#ifndef HEXO_TOOL_VERSION
#define HEXO_TOOL_VERSION "unknown"
#endif

namespace {

// 函数用途：显示帮助信息
void utilHelper() {
    std::puts(R"(HexoTool )" HEXO_TOOL_VERSION R"(
构建、预览和部署 Hexo 博客。

用法
  HexoTool <命令> [选项] [-- Hexo 参数]

命令
  deploy    生成并部署（一条龙）
  build     只生成本地静态文件
  server    启动本地预览
  clean     清理缓存和生成文件
  packages  更新 Node.js 依赖
  theme     更新主题子模块

选项
  --project <路径>  指定 Hexo 项目目录
  --dry-run         预览命令，不实际执行
  --quiet           只显示警告和错误
  --no-color        禁用彩色输出
  --log-file <路径> 保存外部命令原始输出
  -v, --verbose     显示项目和外部命令
  -vv, --trace      显示内部诊断
  --version         显示版本
  -h, --help        显示帮助

示例
  HexoTool deploy
  HexoTool server --project ~/blog -- --draft)");
}

bool isHexoProject(const std::filesystem::path &directory) {
    return std::filesystem::is_regular_file(directory / "_config.yml") &&
           std::filesystem::is_regular_file(directory / "package.json");
}

// 函数用途：主函数
// 参数：
//   argc: 命令行参数个数
//   argv: 命令行参数数组
// 返回值：0 - 成功，1 - 失败
int run(int argc, char *argv[]) {
    initLogger();
    Config config;

    if (const char *env_p = std::getenv("HEXO_TOOLKIT_DEBUG"); env_p != nullptr) {
        spdlog::set_level(spdlog::level::debug);
    }
    if (const char *env_p = std::getenv("HEXO_TOOLKIT_TRACE"); env_p != nullptr) {
        spdlog::set_level(spdlog::level::trace);
    }

    std::string_view commandString;
    std::filesystem::path projectPath;
    std::vector<std::string> passThroughArguments;
    bool readingPassThroughArguments = false;
    bool helpRequested = false;
    bool versionRequested = false;

    // debug 标志应精确匹配，避免模糊匹配误触（如 --decog 触发 debug 模式）
    auto const isDebugFlag = [](std::string_view argument) {
        return argument == "--debug" || argument == "-debug" || argument == "--verbose" || argument == "-verbose" ||
               argument == "-v";
    };
    auto const isTraceFlag = [](std::string_view argument) { return argument == "-vv" || argument == "--trace"; };

    for (int index = 1; index < argc; ++index) {
        std::string_view argument(argv[index]);
        if (readingPassThroughArguments) {
            passThroughArguments.emplace_back(argument);
        } else if (argument == "--") {
            readingPassThroughArguments = true;
        } else if (argument == "--help" || argument == "-h" || argument == "-help") {
            helpRequested = true;
        } else if (argument == "--version" || argument == "-V") {
            versionRequested = true;
        } else if (isTraceFlag(argument)) {
            spdlog::set_level(spdlog::level::trace);
        } else if (isDebugFlag(argument)) {
            if (spdlog::get_level() > spdlog::level::debug) spdlog::set_level(spdlog::level::debug);
        } else if (argument == "--quiet") {
            spdlog::set_level(spdlog::level::warn);
        } else if (argument == "--no-color") {
            disableLoggerColor();
        } else if (argument == "--log-file") {
            ++index;
            if (index >= argc || std::string_view(argv[index]).empty() ||
                std::string_view(argv[index]).starts_with('-')) {
                logError("--log-file 缺少路径参数");
                return 1;
            }
            commandLogFile = argv[index];
        } else if (argument.starts_with("--log-file=")) {
            commandLogFile = argument.substr(std::string_view("--log-file=").size());
            if (commandLogFile.empty()) {
                logError("--log-file 缺少路径参数");
                return 1;
            }
        } else if (argument == "--dry-run") {
            dryRunEnabled = true;
        } else if (argument == "--project") {
            ++index;
            if (index >= argc || std::string_view(argv[index]).empty() ||
                std::string_view(argv[index]).starts_with('-')) {
                logError("--project 缺少路径参数");
                return 1;
            }
            projectPath = argv[index];
        } else if (argument.starts_with("--project=")) {
            projectPath = argument.substr(std::string_view("--project=").size());
            if (projectPath.empty()) {
                logError("--project 缺少路径参数");
                return 1;
            }
        } else if (argument.starts_with('-')) {
            logError("未知选项 {}", argument);
            return 1;
        } else if (commandString.empty()) {
            commandString = argument;
        } else {
            logError("多余参数 '{}'。Hexo 参数请放在 -- 之后。", argument);
            return 1;
        }
    }

    if (versionRequested || commandString == "version") {
        std::puts("HexoTool " HEXO_TOOL_VERSION);
        return 0;
    }
    const auto command = resolveCommand(commandString);
    if (helpRequested || (commandString.empty() && passThroughArguments.empty()) || command == CommandIntent::help) {
        utilHelper();
        return 0;
    }
    if (command == CommandIntent::invalid) {
        logError("未知命令 '{}'。运行 'HexoTool help' 查看可用命令。", commandString);
        config.loadFromYamlIfExists();
        if (auto suggestion = suggestCommand(commandString, config.similarityThreshold); !suggestion.empty())
            logError("是否想输入 '{}'？", suggestion);
        return 1;
    }
    // Relative log paths belong to the invoking directory, independently of --project.
    if (!commandLogFile.empty()) commandLogFile = std::filesystem::absolute(commandLogFile);

    if (!projectPath.empty()) {
        std::error_code pathError;
        std::filesystem::current_path(projectPath, pathError);
        if (pathError) {
            logError("无法打开项目目录 '{}': {}", projectPath.string(), pathError.message());
            return 1;
        }
    }

    const auto workingDirectory = std::filesystem::current_path();
    if (!isHexoProject(workingDirectory)) {
        logError("'{}' 不是 Hexo 项目：缺少 _config.yml 或 package.json。", workingDirectory.string());
        return 1;
    }

    config.loadFromYamlIfExists();
    config.detectPackageManager();

    const auto configPath = getExecutableDir() / "config.yaml";
    logDetail("项目 {}", workingDirectory.string());
    logDetail("配置 {} · {} · {} 个附属工具", std::filesystem::exists(configPath) ? configPath.string() : "默认配置",
              config.packageManager, config.additionalTools.size());

    if (!passThroughArguments.empty() && command != CommandIntent::build && command != CommandIntent::deploy &&
        command != CommandIntent::clean && command != CommandIntent::server) {
        logError("{} 不接受 Hexo 参数。", commandString);
        return 1;
    }
    if (hasUnsafeShellArguments(passThroughArguments)) {
        logError("Windows 下的 Hexo 参数不能包含 cmd.exe 特殊字符（\" % & | < > ( ) ^ !）。");
        return 1;
    }

    const std::string extraArguments = joinShellArguments(passThroughArguments);
    int exitCode = 0;
    switch (command) {
        case CommandIntent::build:
            exitCode = hexoBuild(config, passThroughArguments);
            break;
        case CommandIntent::deploy:
            exitCode = hexoDeploy(config, passThroughArguments);
            break;
        case CommandIntent::clean:
            exitCode = hexoClean(config, extraArguments);
            break;
        case CommandIntent::server:
            exitCode = hexoServer(config, passThroughArguments);
            break;
        case CommandIntent::theme:
            logStep("更新主题子模块");
            exitCode = runCommand("git submodule update --remote --merge", true);
            if (exitCode != 0) logError("主题更新失败（退出码 {}）", exitCode);
            break;
        case CommandIntent::packages:
            logStep("使用 {} 更新依赖", config.packageManager);
            exitCode = runCommand(config.upgradeCommand, true);
            if (exitCode != 0) logError("依赖更新失败（退出码 {}）", exitCode);
            break;
        case CommandIntent::help:
        case CommandIntent::invalid:
            return 1;
    }
    if (exitCode == 0) {
        if (dryRunEnabled) {
            logSuccess("预演完成，未执行命令");
        } else {
            switch (command) {
                case CommandIntent::build:
                    logSuccess("构建完成");
                    break;
                case CommandIntent::deploy:
                    logSuccess("部署完成");
                    break;
                case CommandIntent::clean:
                    logSuccess("清理完成");
                    break;
                case CommandIntent::server:
                    logSuccess("服务器已停止");
                    break;
                case CommandIntent::theme:
                    logSuccess("主题已更新");
                    break;
                case CommandIntent::packages:
                    logSuccess("依赖已更新");
                    break;
                case CommandIntent::help:
                case CommandIntent::invalid:
                    break;
            }
        }
    }
    return exitCode;
}

}  // namespace

int main(int argc, char *argv[]) {
    try {
        return run(argc, argv);
    } catch (const std::exception &exception) {
        logError("操作失败：{}", exception.what());
        return 1;
    }
}
