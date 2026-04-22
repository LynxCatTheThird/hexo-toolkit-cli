#pragma once

#include <filesystem>     // std::filesystem
#include <fstream>        // std::ifstream
#include <optional>       // std::optional, std::nullopt
#include <string>         // std::string
#include <string_view>    // std::string_view
#include <unordered_map>  // std::unordered_map
#include <vector>         // std::vector, std::pair

#include "ryml.hpp"      // IWYU pragma: keep // rapidyaml 核心
#include "ryml_std.hpp"  // IWYU pragma: keep // rapidyaml std::string 支持
#include "spdlog/spdlog.h"

#if defined(_WIN32)
#include <windows.h>  // Windows API
#elif defined(__linux__)
#include <linux/limits.h>  // PATH_MAX
#include <unistd.h>        // POSIX API
#endif

// 透明哈希，允许 unordered_map 使用 string_view 直接查找，避免构造临时 std::string
struct StringHash {
    using is_transparent = void;
    size_t operator()(std::string_view sv) const noexcept { return std::hash<std::string_view>{}(sv); }
};

// 获取当前可执行文件所在目录
inline std::filesystem::path getExecutableDir() {
#if defined(_WIN32)
    wchar_t buffer[MAX_PATH + 1];
    DWORD len = GetModuleFileNameW(NULL, buffer, MAX_PATH);
    buffer[len] = L'\0';
    return std::filesystem::path(buffer).parent_path();
#elif defined(__linux__)
    char buffer[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (len != -1) {
        buffer[len] = '\0';
        return std::filesystem::path(buffer).parent_path();
    }
    return std::filesystem::current_path();
#else
    return std::filesystem::current_path();
#endif
}


class Node {
   public:
    std::unordered_map<std::string, std::string, StringHash, std::equal_to<>> scalars;
    std::vector<std::pair<std::string, std::string>> additionalTools;

    // 传入 string_view 避免拷贝，内部按行切割
    bool parse(std::string_view content) {
        try {
            // 使用 rapidyaml 直接解析内容，零拷贝或最小拷贝
            ryml::Tree tree = ryml::parse_in_arena(ryml::csubstr(content.data(), content.size()));

            if (tree.empty() || !tree.rootref().is_map()) {
                return true;
            }

            for (auto child : tree.rootref().children()) {
                if (child.has_key()) {
                    std::string key;
                    child >> ryml::key(key);

                    // 记录键，用于下方 has() 判断覆盖情况
                    scalars[key] = "";

                    // 解析 - [k, v] 格式的数组
                    if (child.is_seq() && key == "additionalTools") {
                        for (auto item : child.children()) {
                            if (item.is_seq() && item.num_children() >= 2) {
                                std::string k, v;
                                item[0] >> k;
                                item[1] >> v;
                                additionalTools.emplace_back(k, v);
                            }
                        }
                    }
                    // 解析 key: value 格式
                    else if (child.has_val()) {
                        std::string val;
                        child >> val;
                        scalars[key] = val;
                    }
                }
            }
        } catch (...) {
            // rapidyaml 在语法错误时可能会抛出异常或触发断言
            return false;
        }
        return true;
    }

    // 安全检查
    std::optional<std::string> get_opt(std::string_view key) const {
        if (auto it = scalars.find(key); it != scalars.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    // 检查某个键是否存在于配置文件中（用于检测空列表等覆盖情况）
    bool has(std::string_view key) const { return scalars.find(key) != scalars.end(); }

    // 兼容原有代码的旧接口
    std::string get(const std::string &key, const std::string &def = "") const {
        auto val = get_opt(key);
        return val.has_value() ? *val : def;
    }

    double getDouble(const std::string &key, double def) const {
        auto val = get_opt(key);
        if (!val.has_value() || val->empty()) return def;
        try {
            return std::stod(*val);
        } catch (...) {
            return def;
        }
    }

    short getShort(const std::string &key, short def) const {
        auto val = get_opt(key);
        if (!val.has_value() || val->empty()) return def;
        try {
            return static_cast<short>(std::stoi(*val));
        } catch (...) {
            return def;
        }
    }
};



// 包管理器信息表，用于表驱动检测
struct PackageManagerInfo {
    std::string_view lockFile;
    std::string_view name;
    std::string_view commandPrefix;
};

inline constexpr PackageManagerInfo PM_TABLE[] = {
    {"package-lock.json", "npm", "npx "},
    {"yarn.lock", "yarn", "yarn run "},
    {"pnpm-lock.yaml", "pnpm", "pnpm exec "},
};

struct Config {
    double similarityThreshold = 0.85;
    std::string dependenciesSearchingFile = "package.json";
    std::vector<std::pair<std::string, std::string>> additionalTools = {
        {"swpp", "hexo swpp"}, {"gulp", "gulp zip"}, {"algolia", "hexo algolia"}};

    bool shouldAutoDetectDependencies = true;  // 自动检测功能开关

    // 包管理器信息
    std::string packageManager = "npm";
    std::string packageManagerCommand = "npx ";

    void logPathInfo(const std::filesystem::path &configPath) {
        spdlog::debug("配置文件路径: {}", configPath.string());
        spdlog::debug("可执行文件目录: {}", getExecutableDir().string());
        spdlog::debug("当前工作目录: {}", std::filesystem::current_path().string());
    }

    // 检测使用的包管理器，并自动设置依赖搜索文件
    void detectPackageManager() {
        for (const auto &pm : PM_TABLE) {
            if (std::filesystem::exists(pm.lockFile)) {
                packageManager = pm.name;
                packageManagerCommand = pm.commandPrefix;
                spdlog::debug("检测到包管理器: {}", packageManager);
                if (shouldAutoDetectDependencies) {
                    dependenciesSearchingFile = std::string(pm.lockFile);
                }
                spdlog::debug("依赖搜索文件设置为: {}", dependenciesSearchingFile);
                return;
            }
        }
        spdlog::debug("未检测到特定包管理器，默认使用: {}", packageManager);
        if (shouldAutoDetectDependencies) {
            dependenciesSearchingFile = "package-lock.json";
        }
        spdlog::debug("依赖搜索文件设置为: {}", dependenciesSearchingFile);
    }

    void loadFromYamlIfExists(const std::string &filename = "config.yaml") {
        std::filesystem::path configPath = getExecutableDir() / filename;

        if (!std::filesystem::exists(configPath)) {
            spdlog::warn("配置文件 {} 未找到，使用默认配置", configPath.string());
            return;
        }

        // 使用二进制模式打开以获得更快的读取速度
        std::ifstream fin(configPath, std::ios::in | std::ios::binary);
        if (!fin) {
            spdlog::warn("配置文件 {} 无法读取，使用默认值", configPath.string());
            return;
        }

        // 一次性读取文件内容，比 istreambuf_iterator 逐字符读取快得多
        fin.seekg(0, std::ios::end);
        std::streamsize size = fin.tellg();
        std::string content;
        if (size > 0) {
            content.resize(static_cast<size_t>(size));
            fin.seekg(0, std::ios::beg);
            fin.read(content.data(), size);
        }

        Node node;
        if (!node.parse(content)) {
            spdlog::error("YAML 配置文件 {} 解析失败", configPath.string());
            return;
        }

        // 读取相似度阈值，并进行合理性检查
        double temp_threshold = node.getDouble("similarityThreshold", similarityThreshold);
        if (temp_threshold > 0.0 && temp_threshold < 1.0) {
            similarityThreshold = temp_threshold;
        } else {
            spdlog::error("非法相似度阈值，保留默认值 {}", similarityThreshold);
        }

        // 使用 has 方法读取可能的依赖文件配置，支持覆盖默认值并控制检测开关
        if (node.has("dependenciesSearchingFile")) {
            dependenciesSearchingFile = node.get("dependenciesSearchingFile");
            shouldAutoDetectDependencies = false;  // 一旦 yaml 中明确规定了路径，关闭自动检测
            spdlog::info("正在使用自定义依赖文件: {}", dependenciesSearchingFile);
        }

        // 读取附属工具列表，只要键存在或列表不为空，即接管配置
        if (node.has("additionalTools") || !node.additionalTools.empty()) {
            additionalTools.clear();  // 清空默认工具列表

            if (!node.additionalTools.empty()) {
                spdlog::info("加载了 {} 个扩展工具", node.additionalTools.size());
                additionalTools = std::move(node.additionalTools);  // 移动语义优化
            } else {
                spdlog::info("检测到空的 additionalTools 配置，已清空内置扩展工具");
            }
        }

        spdlog::debug("成功加载配置 {}", configPath.string());
    }
};

inline Config config;