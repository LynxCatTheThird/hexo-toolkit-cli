#pragma once

#include <cstdio>       // stderr, fflush
#include <string>       // std::string
#include <utility>      // std::forward

#include "spdlog/sinks/stdout_color_sinks.h"  // IWYU pragma: keep //颜色定义
#include "spdlog/spdlog.h"                    // spdlog

inline void initLogger() {
    auto const sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
    sink->set_color(spdlog::level::debug, sink->dark);
    sink->set_color(spdlog::level::trace, sink->dark);
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
