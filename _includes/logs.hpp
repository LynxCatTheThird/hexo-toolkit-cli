#pragma once

#include <chrono>       // std::chrono::milliseconds
#include <cstdio>       // stderr, fflush
#include <format>       // std::format
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

// 函数用途：RAII 计时器，离开作用域时自动打印总用时
class ScopedTimer {
    std::string label;
    std::chrono::high_resolution_clock::time_point startTime;

   public:
    explicit ScopedTimer(std::string_view labelText)
        : label(labelText), startTime(std::chrono::high_resolution_clock::now()) {}
    ~ScopedTimer() {
        auto elapsed = std::chrono::high_resolution_clock::now() - startTime;
        double seconds = std::chrono::duration<double>(elapsed).count();
        spdlog::debug("{}用时: {} 秒", label, formatDuration(seconds, 3));
    }
};

// 函数用途：输出等待转圈动画，直到条件满足为止
// 参数：
//   label                —— 显示在转圈动画前的标签文字
//   predicate            —— 一个返回 bool 的函数对象；返回 true 表示结束等待
//   intervalMilliseconds —— 每次轮询的时间间隔（毫秒），默认为 50ms
// 返回值：无
template <std::invocable Predicate>
inline void waitWithSpinner(std::string_view label, Predicate &&predicate, int intervalMilliseconds = 50) {
    // 转圈动画帧序列：
    //   Windows  → ASCII（兼容老式 CMD/PowerShell）
    //   其他平台 → Braille 点阵（与 npm/yarn/pnpm 风格一致）
#if defined(_WIN32)
    static constexpr std::string_view spinnerFrames[] = {"|", "/", "-", "\\"};
#else
    static constexpr std::string_view spinnerFrames[] = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
#endif
    static constexpr size_t frameCount = std::size(spinnerFrames);
    size_t count = 0;

    // 当 predicate() 返回 false 时持续等待
    while (!predicate()) {
        // 每隔若干次（可调）输出一个 spinner 字符，避免频繁刷新
        if (count % 10 == 0) {
            int spinnerIndex = (count / 10) % static_cast<int>(frameCount);
            fmt::print(stderr, fmt::fg(fmt::terminal_color::yellow) | fmt::emphasis::bold, "\r[W] {} {}", label,
                       spinnerFrames[spinnerIndex]);
            fflush(stderr);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(intervalMilliseconds));
        ++count;
    }
    // 覆盖清除行
    fmt::print(stderr, "\r{}\r", std::string(label.size() + 6, ' '));
    fflush(stderr);
}
