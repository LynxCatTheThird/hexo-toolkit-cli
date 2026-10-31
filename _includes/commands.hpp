#pragma once

#include <array>
#include <string_view>

#include "algorithms.hpp"
#include "logs.hpp"

enum class CommandIntent { help, build, deploy, clean, server, theme, packages, invalid };
struct CommandName {
    std::string_view name;
    CommandIntent intent;
};
inline constexpr std::array commandNames = {
    CommandName{"help", CommandIntent::help},        CommandName{"build", CommandIntent::build},
    CommandName{"deploy", CommandIntent::deploy},    CommandName{"clean", CommandIntent::clean},
    CommandName{"server", CommandIntent::server},    CommandName{"theme", CommandIntent::theme},
    CommandName{"packages", CommandIntent::packages}};

inline CommandIntent resolveCommand(std::string_view input) {
    if (input == "srv") return CommandIntent::server;
    if (input == "pkg") return CommandIntent::packages;
    CommandIntent match = CommandIntent::invalid;
    for (auto [name, intent] : commandNames) {
        if (input == name) {
            logTrace("命令匹配：{}", name);
            return intent;
        }
        if (isPrefixOf(input, name)) {
            if (match != CommandIntent::invalid) return CommandIntent::invalid;
            match = intent;
            logTrace("前缀匹配：{}", name);
        }
    }
    return match;
}

inline std::string_view suggestCommand(std::string_view input, double threshold = 0.85) {
    std::string_view suggestion;
    for (auto [name, intent] : commandNames) {
        static_cast<void>(intent);
        const double score = getJaroWinklerSimilarity(input, name);
        if (score > threshold) {
            threshold = score;
            suggestion = name;
        }
    }
    return suggestion;
}
