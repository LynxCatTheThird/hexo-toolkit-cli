#pragma once

#include <filesystem>     // std::filesystem
#include <optional>       // std::optional, std::nullopt
#include <string>         // std::string
#include <string_view>    // std::string_view
#include <unordered_map>  // std::unordered_map
#include <vector>         // std::vector, std::pair

#include "ryml.hpp"         // IWYU pragma: keep // rapidyaml 核心
#include "ryml_std.hpp"     // IWYU pragma: keep // rapidyaml std::string 支持
#include "spdlog/spdlog.h"  // spdlog::warn
#include "utils.hpp"        // 工具函数

#if defined(_WIN32)
#include <windows.h>  // Windows API
#elif defined(__linux__)
#include <linux/limits.h>  // PATH_MAX
#include <unistd.h>        // POSIX API
#endif

// 透明哈希，允许 unordered_map 使用 string_view 直接查找，避免构造临时 std::string
struct StringHash {
    using is_transparent = void;
    size_t operator()(std::string_view stringView) const noexcept { return std::hash<std::string_view>{}(stringView); }
};

// 获取当前可执行文件所在目录
inline std::filesystem::path getExecutableDir() {
#if defined(_WIN32)
    wchar_t buffer[MAX_PATH + 1];
    DWORD length = GetModuleFileNameW(NULL, buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return std::filesystem::current_path();
    }
    buffer[length] = L'\0';
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

                    // 解析 - [itemKey, itemValue] 格式的数组，用于额外工具列表
                    if (child.is_seq() && key == "additionalTools") {
                        for (auto item : child.children()) {
                            if (item.is_seq() && item.num_children() >= 2) {
                                std::string itemKey, itemValue;
                                item[0] >> itemKey;
                                item[1] >> itemValue;
                                additionalTools.emplace_back(itemKey, itemValue);
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
        } catch (const std::exception &exception) {
            spdlog::error("YAML 解析异常: {}", exception.what());
            return false;
        } catch (...) {
            // rapidyaml 在语法错误时可能会抛出异常或触发断言
            spdlog::error("YAML 解析遇到未知异常");
            return false;
        }
        return true;
    }

    // 安全检查，尝试获取配置项的值，如果不存在则返回 std::nullopt
    std::optional<std::string> getOptional(std::string_view key) const {
        if (auto it = scalars.find(key); it != scalars.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    // 检查某个键是否存在于配置文件中（用于检测空列表等覆盖情况）
    bool has(std::string_view key) const { return scalars.find(key) != scalars.end(); }

    // 获取字符串配置项，若不存在则返回 defaultValue
    std::string get(std::string_view key, std::string_view defaultValue = "") const {
        auto value = getOptional(key);
        if (value.has_value()) return *value;
        return std::string(defaultValue);
    }

    // 获取双精度浮点数配置项
    double getDouble(std::string_view key, double defaultValue) const {
        auto value = getOptional(key);
        if (!value.has_value() || value->empty()) return defaultValue;
        try {
            return std::stod(*value);
        } catch (const std::exception &exception) {
            spdlog::warn("配置项 '{}' 转换为 double 异常: {}", key, exception.what());
            return defaultValue;
        } catch (...) {
            return defaultValue;
        }
    }

    // 获取短整数配置项
    short getShort(std::string_view key, short defaultValue) const {
        auto value = getOptional(key);
        if (!value.has_value() || value->empty()) return defaultValue;
        try {
            int integerValue = std::stoi(*value);
            if (integerValue < std::numeric_limits<short>::min() || integerValue > std::numeric_limits<short>::max()) {
                spdlog::warn("配置项 '{}' 的值 {} 超出 short 范围，保留默认值 {}", key, integerValue, defaultValue);
                return defaultValue;
            }
            return static_cast<short>(integerValue);
        } catch (const std::exception &exception) {
            spdlog::warn("配置项 '{}' 转换为 short 异常: {}", key, exception.what());
            return defaultValue;
        } catch (...) {
            return defaultValue;
        }
    }
};

// 包管理器信息表，用于表驱动检测
struct PackageManagerInfo {
    std::string_view lockFile;
    std::string_view name;
    std::string_view commandPrefix;
    std::string_view upgradeCommand;  // 升级项目依赖的完整命令
};

inline constexpr PackageManagerInfo PM_TABLE[] = {
    {"package-lock.json", "npm", "npx ", "npx rimraf node_modules package-lock.json && ncu -u && npm install"},
    {"yarn.lock", "yarn", "yarn run ", "yarn upgrade --latest"},
    {"pnpm-lock.yaml", "pnpm", "pnpm exec ", "pnpm update --latest"},
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
    std::string upgradeCommand = "npx rimraf node_modules package-lock.json && ncu -u && npm install";

    void logPathInfo(const std::filesystem::path &configPath) {
        spdlog::debug("配置文件路径: {}", configPath.string());
        spdlog::debug("可执行文件目录: {}", getExecutableDir().string());
        spdlog::debug("当前工作目录: {}", std::filesystem::current_path().string());
    }

    // 检测使用的包管理器，并自动设置依赖搜索文件
    void detectPackageManager() {
        for (const auto &packageManagerInfo : PM_TABLE) {
            if (std::filesystem::exists(packageManagerInfo.lockFile)) {
                packageManager = packageManagerInfo.name;
                packageManagerCommand = packageManagerInfo.commandPrefix;
                upgradeCommand = std::string(packageManagerInfo.upgradeCommand);
                spdlog::debug("检测到包管理器: {}", packageManager);
                if (shouldAutoDetectDependencies) {
                    dependenciesSearchingFile = std::string(packageManagerInfo.lockFile);
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
        std::string content = readFileContents(configPath);
        if (content.empty()) {
            spdlog::warn("配置文件 {} 无法读取或为空，使用默认值", configPath.string());
            return;
        }

        Node node;
        if (!node.parse(content)) {
            spdlog::error("YAML 配置文件 {} 解析失败", configPath.string());
            return;
        }

        // 读取相似度阈值，并进行合理性检查
        double temporaryThreshold = node.getDouble("similarityThreshold", similarityThreshold);
        if (temporaryThreshold > 0.0 && temporaryThreshold <= 1.0) {
            similarityThreshold = temporaryThreshold;
        } else {
            spdlog::error("非法相似度阈值，保留默认值 {}", similarityThreshold);
        }

        // 使用 has 方法读取可能的依赖文件配置，支持覆盖默认值并控制检测开关
        if (node.has("dependenciesSearchingFile")) {
            dependenciesSearchingFile = node.get("dependenciesSearchingFile");
            shouldAutoDetectDependencies = false;  // 一旦 yaml 中明确规定了路径，关闭自动检测
            spdlog::info("正在使用自定义依赖文件: {}", dependenciesSearchingFile);
        }

        // 只要 YAML 中声明了 additionalTools 键（哪怕是空列表），即接管默认配置
        if (node.has("additionalTools")) {
            additionalTools.clear();
            if (!node.additionalTools.empty()) {
                spdlog::info("加载了 {} 个扩展工具", node.additionalTools.size());
                additionalTools = std::move(node.additionalTools);
            } else {
                spdlog::info("检测到空的 additionalTools 配置，已清空内置扩展工具");
            }
        }

        spdlog::debug("成功加载配置 {}", configPath.string());
    }
};

inline Config config;
