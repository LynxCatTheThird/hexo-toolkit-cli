#pragma once

#include <chrono>   // std::chrono::high_resolution_clock
#include <cstdlib>  // std::system
#include <future>   // std::future, std::async
#include <string>   // std::string

#include "configs.hpp"  // 配置文件解析
#include "globals.hpp"  // 全局变量与状态
#include "logs.hpp"     // 日志
#include "utils.hpp"    //工具函数

// 函数用途：清理 Hexo 产生的缓存文件
inline void hexoClean() {
    spdlog::info("清理 Hexo 缓存文件...");
    std::system(std::format("{}hexo clean{}", packageManagerCommand, DEV_NULL).c_str());
}

// 函数用途：启动 Hexo 本地预览服务器
inline void hexoServer() {
    auto totalStart = std::chrono::high_resolution_clock::now();  // 开始记录总执行时间
    hexoClean();
    for (int portNum = 4000; portNum <= 65535; portNum++) {
        std::string portStr = std::to_string(portNum);
        std::string command = std::format("{}hexo server --port {}{}", packageManagerCommand, portStr, DEV_NULL);
        if (!isPortOpen(portNum)) {
            spdlog::info("正在尝试于 {} 端口启动 Hexo 本地预览服务器... ", portStr);
            auto serverStart = std::chrono::high_resolution_clock::now();  // 开始记录 hexo server 启动时间
            std::future<int> resultFuture = std::async(std::launch::async, executeCommand, command);

            waitWithSpinner("等待 Hexo 本地预览服务器启动...", [&]() {
                if (resultFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                    return true;  // 服务器进程在端口开放前结束，可能是启动失败，直接结束等待
                }
                return isPortOpen(portNum);
            });

            // 检查服务器进程是否意外结束
            if (resultFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                int exitCode = resultFuture.get();
                spdlog::error("Hexo 服务器启动失败或意外退出，退出码: {}", exitCode);
                break;
            }

            // 服务器成功启动，记录时间并输出信息
            auto serverEnd = std::chrono::high_resolution_clock::now();             // 结束记录 hexo server 启动时间
            std::chrono::duration<double> serverElapsed = serverEnd - serverStart;  // 计算 hexo server 启动时间
            spdlog::debug("Hexo 本地预览服务器启动用时: {} 秒", formatDuration(serverElapsed.count(), 3));
            spdlog::info("您现在可以访问 http://localhost:{} 预览效果了", portStr);

            // 阻塞，等待用户自己关掉服务器进程
            int exitCode = resultFuture.get();
            spdlog::info("Hexo 服务器已正常关闭，退出码: {}", exitCode);
            break;
        } else {
            spdlog::error("{} 端口已被占用，尝试使用下一个端口...", portStr);
        }
    }
    auto totalEnd = std::chrono::high_resolution_clock::now();           // 结束记录总执行时间
    std::chrono::duration<double> totalElapsed = totalEnd - totalStart;  // 计算总执行时间
    spdlog::info("本次操作执行总用时: {} 秒", formatDuration(totalElapsed.count(), 3));
}

// 函数用途：部署 Hexo 静态文件
inline void hexoBuild() {
    auto totalStart = std::chrono::high_resolution_clock::now();  // 开始记录总执行时间
    hexoClean();
    spdlog::info("生成静态文件...");
    auto generateStart = std::chrono::high_resolution_clock::now();  // 开始记录 Generate 时间
    std::system(std::format("{}hexo generate{}", packageManagerCommand, DEV_NULL).c_str());
    auto generateEnd = std::chrono::high_resolution_clock::now();  // 结束记录 Generate 时间

    // 如果配置了附属工具，则执行它们
    if (config.additionalTools.empty()) {
        spdlog::info("未配置附属工具，跳过执行");
    } else {
        spdlog::info("执行附属命令...");
        auto additionalStart = std::chrono::high_resolution_clock::now();
        for (const auto &[keyword, command] : config.additionalTools) {
            if (isDependenciesPresent(config.dependenciesSearchingFile, keyword)) {
                spdlog::info("正在执行 {} ...", keyword);
                std::system(std::format("{}{}{}", packageManagerCommand, command, DEV_NULL).c_str());
            } else {
                spdlog::warn("本地项目中未安装 {} 或检索出错，跳过执行", keyword);
            }
        }
        auto additionalEnd = std::chrono::high_resolution_clock::now();

        std::chrono::duration<double> additionalElapsed = additionalEnd - additionalStart;
        spdlog::info("附属工具执行完毕，用时: {} 秒", formatDuration(additionalElapsed.count(), 3));
    }

    spdlog::info("部署静态文件...");
    std::system(std::format("{}hexo d{}", packageManagerCommand, DEV_NULL).c_str());
    hexoClean();
    auto totalEnd = std::chrono::high_resolution_clock::now();                    // 结束记录总执行时间
    std::chrono::duration<double> generateElapsed = generateEnd - generateStart;  // 计算 Generate 时间
    spdlog::debug("生成静态文件用时: {} 秒", formatDuration(generateElapsed.count(), 3));
    std::chrono::duration<double> totalElapsed = totalEnd - totalStart;  // 计算总执行时间
    spdlog::debug("本次操作执行总用时: {} 秒", formatDuration(totalElapsed.count(), 3));
}
