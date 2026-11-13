#pragma once

#include <charconv>     // std::from_chars
#include <filesystem>   // std::filesystem
#include <initializer_list>
#include <string>       // std::string
#include <string_view>  // std::string_view
#include <system_error>
#include <utility>
#include <vector>       // std::vector, std::pair

#include "dependencies.hpp"
#include "ryml.hpp"         // IWYU pragma: keep // rapidyaml 核心
#include "ryml_std.hpp"     // IWYU pragma: keep // rapidyaml std::string 支持

// 通过 /proc 定位可执行文件，读取失败时沿用当前工作目录。
inline std::filesystem::path getExecutableDir() {
    std::error_code error;
    const auto executable = std::filesystem::read_symlink("/proc/self/exe", error);
    return error ? std::filesystem::current_path() : executable.parent_path();
}

template <typename Number>
inline bool parseNumberFully(std::string_view text, Number &value) {
    auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    return ec == std::errc{} && ptr == text.data() + text.size();
}

struct Config {
    double similarityThreshold = 0.85;
    std::string dependenciesSearchingFile = "package.json";
    std::vector<std::pair<std::string, std::string>> additionalTools = {{"hexo-swpp", "hexo swpp"},
                                                                        {"hexo-algolia", "hexo algolia"}};
    std::string packageManager = "npm";
    std::string packageManagerCommand = "npx --no-install ";
    std::string upgradeCommand = "npx --yes npm-check-updates -u && npm install";

    // 优先采用 manifest 声明；锁文件对应多个包管理器时要求明确声明。
    void detectPackageManager() {
        const auto manifest = readFileContents("package.json");
        auto tree = ryml::parse_in_arena(ryml::to_csubstr(manifest));
        auto root = tree.rootref();
        if (!root.is_map()) throw std::runtime_error("package.json 必须是对象");
        std::string declared;
        if (root.has_child("packageManager")) {
            if (!root["packageManager"].has_val()) throw std::runtime_error("packageManager 必须是字符串");
            root["packageManager"].load(&declared);
        }
        std::string selected;
        if (!declared.empty()) {
            selected = declared.substr(0, declared.find('@'));
            if (selected != "npm" && selected != "yarn" && selected != "pnpm")
                throw std::runtime_error("不支持的 packageManager：" + declared);
        } else {
            for (const auto &[file, name] :
                 std::initializer_list<std::pair<const char *, const char *>>{{"package-lock.json", "npm"},
                                                                              {"npm-shrinkwrap.json", "npm"},
                                                                              {"yarn.lock", "yarn"},
                                                                              {"pnpm-lock.yaml", "pnpm"}}) {
                if (!std::filesystem::exists(file)) continue;
                if (!selected.empty() && selected != name)
                    throw std::runtime_error("存在不同包管理器的 lock 文件，请在 package.json 中声明 packageManager");
                selected = name;
            }
        }
        packageManager = selected.empty() ? "npm" : selected;
        if (packageManager == "npm") {
            packageManagerCommand = "npx --no-install ";
            upgradeCommand = "npx --yes npm-check-updates -u && npm install";
        } else if (packageManager == "pnpm") {
            packageManagerCommand = "pnpm exec ";
            upgradeCommand = "pnpm update --latest";
        } else {
            packageManagerCommand = "yarn run ";
            const auto at = declared.find('@');
            const bool modern = at != std::string::npos
                                    ? !std::string_view(declared).substr(at + 1).starts_with("1.")
                                    : readFileContents("yarn.lock").find("__metadata:") != std::string::npos;
            upgradeCommand = modern ? "yarn up \"*\" \"@*/*\"" : "yarn upgrade --latest";
        }
        logTrace("检测到包管理器：{}", packageManager);
    }

    void loadFromYaml(std::string_view content) {
        auto tree = ryml::parse_in_arena(ryml::csubstr(content.data(), content.size()));
        auto root = tree.rootref();
        if (!root.is_map()) throw std::runtime_error("配置必须是 YAML 映射");
        Config next = *this;
        if (root.has_child("similarityThreshold")) {
            std::string value;
            if (!root["similarityThreshold"].has_val()) throw std::runtime_error("相似度阈值必须是数值");
            root["similarityThreshold"].load(&value);
            if (!parseNumberFully(value, next.similarityThreshold) ||
                !(next.similarityThreshold > 0.0 && next.similarityThreshold <= 1.0))
                throw std::runtime_error("相似度阈值必须在 (0, 1] 范围内");
        }
        if (root.has_child("dependenciesSearchingFile")) {
            auto const node = root["dependenciesSearchingFile"];
            if (!node.has_val()) throw std::runtime_error("依赖文件路径必须是字符串");
            node.load(&next.dependenciesSearchingFile);
            if (next.dependenciesSearchingFile.empty()) throw std::runtime_error("依赖文件路径不能为空");
        }
        if (root.has_child("additionalTools")) {
            auto tools = root["additionalTools"];
            if (!tools.is_seq()) throw std::runtime_error("additionalTools 必须是列表");
            next.additionalTools.clear();
            for (auto item : tools.children()) {
                if (!item.is_seq() || item.num_children() != 2 || !item[0].has_val() || !item[1].has_val())
                    throw std::runtime_error("每个附属工具必须是 [包名, 命令]");
                std::string name, command;
                item[0].load(&name);
                item[1].load(&command);
                if (name.empty() || command.empty()) throw std::runtime_error("附属工具包名和命令不能为空");
                next.additionalTools.emplace_back(std::move(name), std::move(command));
            }
        }
        *this = std::move(next);
    }

    void loadFromYamlIfExists(const std::string &filename = "config.yaml") {
        const auto path = getExecutableDir() / filename;
        if (!std::filesystem::exists(path)) return;
        const auto content = readFileContents(path);
        if (content.empty()) throw std::runtime_error("配置文件为空或无法读取：" + path.string());
        loadFromYaml(content);
    }
};
