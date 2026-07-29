#pragma once

#include <chrono>       // std::chrono::milliseconds
#include <concepts>     // std::invocable
#include <cstdio>       // stderr, fflush
#include <format>       // std::format
#include <string>       // std::string
#include <string_view>  // std::string_view
#include <thread>       // std::this_thread::sleep_for
#include <utility>      // std::forward

#include "spdlog/fmt/bundled/color.h"         // 修复系统未安装 fmt 时的编译问题
#include "spdlog/sinks/stdout_color_sinks.h"  // IWYU pragma: keep //颜色定义
#include "spdlog/spdlog.h"                    // spdlog

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

inline bool isInteractiveTerminal() {
#if defined(_WIN32)
    return _isatty(_fileno(stderr)) != 0;
#else
    return isatty(fileno(stderr)) != 0;
#endif
}

inline void initLogger() {
    auto sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
#if defined(_WIN32)
    sink->set_color(spdlog::level::debug, FOREGROUND_INTENSITY);
    sink->set_color(spdlog::level::trace, FOREGROUND_INTENSITY);
#else
    sink->set_color(spdlog::level::debug, sink->dark);
    sink->set_color(spdlog::level::trace, sink->dark);
#endif
    auto logger = std::make_shared<spdlog::logger>("console", sink);
    spdlog::set_default_logger(logger);
    spdlog::set_pattern("%^%v%$");
    spdlog::set_level(spdlog::level::info);
}

inline void disableLoggerColor() { spdlog::set_pattern("%v"); }

template <typename... Arguments>
inline void logStep(spdlog::format_string_t<Arguments...> format, Arguments &&...arguments) {
    spdlog::info("→ {}", fmt::format(format, std::forward<Arguments>(arguments)...));
}

template <typename... Arguments>
inline void logSuccess(spdlog::format_string_t<Arguments...> format, Arguments &&...arguments) {
    spdlog::info("✓ {}", fmt::format(format, std::forward<Arguments>(arguments)...));
}

template <typename... Arguments>
inline void logError(spdlog::format_string_t<Arguments...> format, Arguments &&...arguments) {
    spdlog::error("✗ {}", fmt::format(format, std::forward<Arguments>(arguments)...));
}

template <typename... Arguments>
inline void logDetail(spdlog::format_string_t<Arguments...> format, Arguments &&...arguments) {
    spdlog::debug("  {}", fmt::format(format, std::forward<Arguments>(arguments)...));
}

template <typename... Arguments>
inline void logTrace(spdlog::format_string_t<Arguments...> format, Arguments &&...arguments) {
    spdlog::trace("    {}", fmt::format(format, std::forward<Arguments>(arguments)...));
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
        logTrace("{}用时 {:.3f} 秒", label, seconds);
    }

    // RAII 对象不允许拷贝或移动，防止意外复制导致重复计时打印
    ScopedTimer(const ScopedTimer &) = delete;
    ScopedTimer &operator=(const ScopedTimer &) = delete;
    ScopedTimer(ScopedTimer &&) = delete;
    ScopedTimer &operator=(ScopedTimer &&) = delete;
};

// 函数用途：输出等待转圈动画，直到条件满足为止
// 参数：
//   label                —— 显示在转圈动画前的标签文字
//   predicate            —— 一个返回 bool 的函数对象；返回 true 表示结束等待
//   intervalMilliseconds —— 每次轮询的时间间隔（毫秒），默认为 50ms
// 返回值：无
template <std::invocable Predicate>
inline void waitWithSpinner(std::string_view label, Predicate &&predicate, int intervalMilliseconds = 50) {
    if (!isInteractiveTerminal()) {
        while (!predicate()) std::this_thread::sleep_for(std::chrono::milliseconds(intervalMilliseconds));
        return;
    }
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
            size_t spinnerIndex = (count / 10) % frameCount;
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
