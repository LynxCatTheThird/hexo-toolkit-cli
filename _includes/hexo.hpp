#pragma once

#include <chrono>  // std::chrono::high_resolution_clock
#include <future>  // std::future, std::async
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
inline void hexoServer() {
    ScopedTimer totalTimer("本次操作执行总");
    hexoClean();
    for (int portNumber = 4000; portNumber <= 65535; portNumber++) {
        std::string command =
            std::format("{}hexo server --port {}{}", config.packageManagerCommand, portNumber, DEVICE_NULL);
        if (!isPortInUse(portNumber)) {
            spdlog::info("正在尝试于 {} 端口启动 Hexo 本地预览服务器... ", portNumber);
            std::future<int> resultFuture = std::async(std::launch::async, [&command] { return runCommand(command); });

            {
                ScopedTimer startupTimer("Hexo 本地预览服务器启动");
                waitWithSpinner("等待 Hexo 本地预览服务器启动...", [&]() {
                    if (resultFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                        return true;  // 服务器进程在端口开放前结束，可能是启动失败，直接结束等待
                    }
                    return isPortInUse(portNumber);
                });
            }

            // 检查服务器进程是否意外结束
            if (resultFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                int exitCode = resultFuture.get();
                spdlog::error("Hexo 服务器启动失败或意外退出，退出码: {}", exitCode);
                // 注意：std::async 返回的 future 析构时会阻塞等待线程结束（标准保证）
                break;
            }

            // 服务器成功启动，输出信息
            spdlog::info("您现在可以访问 http://localhost:{} 预览效果了", portNumber);

            // 阻塞，等待用户自己关掉服务器进程
            int exitCode = resultFuture.get();
            spdlog::info("Hexo 服务器已正常关闭，退出码: {}", exitCode);
            break;
        } else {
            spdlog::error("{} 端口已被占用，尝试使用下一个端口...", portNumber);
        }
    }
}

// 函数用途：部署 Hexo 静态文件
inline void hexoBuild() {
    ScopedTimer totalTimer("本次操作执行总");
    hexoClean();
    spdlog::info("生成静态文件...");
    {
        ScopedTimer generateTimer("生成静态文件");
        int generateExitCode = runCommand(std::format("{}hexo generate{}", config.packageManagerCommand, DEVICE_NULL));
        if (generateExitCode != 0) {
            spdlog::error("hexo generate 失败，退出码: {}，中止后续部署操作", generateExitCode);
            return;
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
    } else {
        hexoClean();
    }
}
