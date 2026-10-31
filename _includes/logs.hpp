#pragma once

#include <chrono>       // std::chrono::milliseconds
#include <cstdio>       // stderr, fflush
#include <format>       // std::format
#include <string>       // std::string
#include <string_view>  // std::string_view
#include <utility>      // std::forward

#include "spdlog/sinks/stdout_color_sinks.h"  // IWYU pragma: keep //颜色定义
#include "spdlog/spdlog.h"                    // spdlog

inline void initLogger() {
    auto const sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
#if !defined(_WIN32)
    sink->set_color(spdlog::level::debug, sink->dark);
    sink->set_color(spdlog::level::trace, sink->dark);
#endif
    auto const logger = std::make_shared<spdlog::logger>("console", sink);
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

// 函数用途：RAII 计时器，离开作用域时自动打印总用时
class ScopedTimer {
    std::string label;
    std::chrono::steady_clock::time_point startTime;

   public:
    explicit ScopedTimer(std::string_view labelText) : label(labelText), startTime(std::chrono::steady_clock::now()) {}
    ~ScopedTimer() {
        auto const elapsed = std::chrono::steady_clock::now() - startTime;
        double seconds = std::chrono::duration<double>(elapsed).count();
        logTrace("{}用时 {:.3f} 秒", label, seconds);
    }

    // RAII 对象不允许拷贝或移动，防止意外复制导致重复计时打印
    ScopedTimer(const ScopedTimer &) = delete;
    ScopedTimer &operator=(const ScopedTimer &) = delete;
    ScopedTimer(ScopedTimer &&) = delete;
    ScopedTimer &operator=(ScopedTimer &&) = delete;
};
