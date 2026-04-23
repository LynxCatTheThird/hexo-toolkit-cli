#pragma once

#include <chrono>       // std::chrono::milliseconds
#include <cstdio>       // stderr, fflush
#include <format>       // std::format
#include <functional>   // std::function
#include <string>       // std::string
#include <string_view>  // std::string_view
#include <thread>       // std::this_thread::sleep_for

#include "spdlog/fmt/bundled/color.h"         // 修复系统未安装 fmt 时的编译问题
#include "spdlog/sinks/stdout_color_sinks.h"  // IWYU pragma: keep //颜色定义
#include "spdlog/spdlog.h"                    // spdlog

inline void initLogger() {
    auto logger = spdlog::stdout_color_mt("console");
    spdlog::set_default_logger(logger);
    spdlog::set_pattern("%^[%L] %v%$");  // 彩色等级 + 消息
    spdlog::set_level(spdlog::level::info);
}

// 函数用途：格式化执行用时
// 参数：
//   seconds: 秒数
//   precision: 精度
// 返回值：格式化后的时间字符串
inline std::string formatDuration(double seconds, int precision = 2) {
    return std::format("{:.{}f}", seconds, precision);
}

// 函数用途：输出等待转圈动画，直到条件满足为止
// 参数：
//   label       —— 显示在转圈动画前的标签文字
//   predicate   —— 一个返回 bool 的函数对象；返回 true 表示结束等待
//   interval_ms —— 每次轮询的时间间隔（毫秒），默认为 100ms
// 返回值：无
inline void waitWithSpinner(std::string_view label, std::function<bool()> predicate, int interval_ms = 100) {
    // 定义转圈动画的字符序列
    static const char spinner[] = "|/-\\";
    int count = 0;

    // 当 predicate() 返回 false 时持续等待
    while (!predicate()) {
        // 每隔若干次（可调）输出一个 spinner 字符，避免频繁刷新
        if (count % 10 == 0) {
            int spinnerIndex = (count / 10) % (sizeof(spinner) - 1);
            // 使用 fmt 库做到跨平台安全输出，且自动兼容老旧 Windows CMD
            fmt::print(stderr, fmt::fg(fmt::terminal_color::yellow) | fmt::emphasis::bold, "\r[W] {} {}", label,
                       spinner[spinnerIndex]);
            fflush(stderr);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
        ++count;
    }
    // 覆盖清除行
    fmt::print(stderr, "\r{}\r", std::string(label.size() + 6, ' '));
    fflush(stderr);
}
