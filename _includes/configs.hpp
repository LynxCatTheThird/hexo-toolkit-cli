#pragma once

#include <charconv>     // std::from_chars
#include <filesystem>   // std::filesystem
#include <string>       // std::string
#include <string_view>  // std::string_view
#include <vector>       // std::vector, std::pair

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

// 获取当前可执行文件所在目录
inline std::filesystem::path getExecutableDir() {
    static const std::filesystem::path exeDir = []() -> std::filesystem::path {
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
    }();
    return exeDir;
}

// 包管理器信息表，用于表驱动检测
struct PackageManagerInfo {
    std::string_view lockFile;
    std::string_view name;
    std::string_view commandPrefix;
    std::string_view upgradeCommand;  // 升级项目依赖的完整命令
};

inline constexpr PackageManagerInfo PM_TABLE[] = {
    {"package-lock.json", "npm", "npx ", "npx npm-check-updates -u && npm install"},
    {"yarn.lock", "yarn", "yarn run ", "yarn upgrade --latest"},
    {"pnpm-lock.yaml", "pnpm", "pnpm exec ", "pnpm update --latest"},
};

template <typename Number>
inline bool parseNumberFully(std::string_view text, Number &value) {
    auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    return ec == std::errc{} && ptr == text.data() + text.size();
}

struct Config {
    double similarityThreshold = 0.85;
    int serverStartupTimeoutSeconds = 30;
    std::string dependenciesSearchingFile = "package.json";
    std::vector<std::pair<std::string, std::string>> additionalTools = {{"hexo-swpp", "hexo swpp"},
                                                                        {"hexo-algolia", "hexo algolia"}};

    bool shouldAutoDetectDependencies = true;  // 自动检测功能开关

    // 包管理器信息
    std::string packageManager = "npm";
    std::string packageManagerCommand = "npx ";
    std::string upgradeCommand = "npx npm-check-updates -u && npm install";

    void logPathInfo(const std::filesystem::path &configPath) {
        logTrace("配置文件：{}", configPath.string());
        logTrace("可执行文件目录：{}", getExecutableDir().string());
        logTrace("当前工作目录：{}", std::filesystem::current_path().string());
    }

    // 检测使用的包管理器，并自动设置依赖搜索文件
    void detectPackageManager() {
        for (const auto &packageManagerInfo : PM_TABLE) {
            if (std::filesystem::exists(packageManagerInfo.lockFile)) {
                packageManager = packageManagerInfo.name;
                packageManagerCommand = packageManagerInfo.commandPrefix;
                upgradeCommand = packageManagerInfo.upgradeCommand;
                logTrace("检测到包管理器：{}", packageManager);
                if (shouldAutoDetectDependencies) {
                    dependenciesSearchingFile = packageManagerInfo.lockFile;
                }
                logTrace("依赖搜索文件：{}", dependenciesSearchingFile);
                return;
            }
        }
        logTrace("未检测到 lock 文件，使用 {}", packageManager);
        if (shouldAutoDetectDependencies) {
            dependenciesSearchingFile = "package.json";
        }
        logTrace("依赖搜索文件：{}", dependenciesSearchingFile);
    }

    void loadFromYamlIfExists(const std::string &filename = "config.yaml") {
        std::filesystem::path configPath = getExecutableDir() / filename;

        if (!std::filesystem::exists(configPath)) {
            logTrace("配置文件不存在，使用默认配置：{}", configPath.string());
            return;
        }

        // 使用二进制模式打开以获得更快的读取速度
        std::string content = readFileContents(configPath);
        if (content.empty()) {
            spdlog::warn("配置文件 {} 无法读取或为空，使用默认值", configPath.string());
            return;
        }

        try {
            ryml::Tree tree = ryml::parse_in_arena(ryml::csubstr(content.data(), content.size()));
            if (tree.empty() || !tree.rootref().is_map()) {
                return;
            }

            ryml::NodeRef root = tree.rootref();

            // 读取相似度阈值，并进行合理性检查
            if (root.has_child("similarityThreshold") && root["similarityThreshold"].has_val()) {
                std::string valStr;
                root["similarityThreshold"] >> valStr;
                double temporaryThreshold{};
                if (parseNumberFully(valStr, temporaryThreshold) && temporaryThreshold > 0.0 &&
                    temporaryThreshold <= 1.0) {
                    similarityThreshold = temporaryThreshold;
                } else {
                    spdlog::error("非法相似度阈值，保留默认值 {}", similarityThreshold);
                }
            }

            // 使用 has_child 方法读取可能的依赖文件配置，支持覆盖默认值并控制检测开关
            if (root.has_child("dependenciesSearchingFile") && root["dependenciesSearchingFile"].has_val()) {
                std::string file;
                root["dependenciesSearchingFile"] >> file;
                dependenciesSearchingFile = std::move(file);
                shouldAutoDetectDependencies = false;  // 一旦 yaml 中明确规定了路径，关闭自动检测
                logTrace("使用自定义依赖文件：{}", dependenciesSearchingFile);
            }

            // 允许用户在配置中收紧或放宽 server 启动阶段的等待上限。
            if (root.has_child("serverStartupTimeoutSeconds") && root["serverStartupTimeoutSeconds"].has_val()) {
                std::string timeoutText;
                root["serverStartupTimeoutSeconds"] >> timeoutText;
                int timeoutSeconds{};
                if (parseNumberFully(timeoutText, timeoutSeconds) && timeoutSeconds > 0) {
                    serverStartupTimeoutSeconds = timeoutSeconds;
                } else {
                    spdlog::error("非法服务器启动超时，保留默认值 {} 秒", serverStartupTimeoutSeconds);
                }
            }

            // 只要 YAML 中声明了 additionalTools 键（哪怕是空列表），即接管默认配置
            if (root.has_child("additionalTools")) {
                ryml::NodeRef tools = root["additionalTools"];
                if (tools.is_seq()) {
                    additionalTools.clear();
                    for (ryml::NodeRef item : tools.children()) {
                        if (item.is_seq() && item.num_children() >= 2 && item[0].has_val() && item[1].has_val()) {
                            std::string itemKey, itemValue;
                            item[0] >> itemKey;
                            item[1] >> itemValue;
                            additionalTools.emplace_back(std::move(itemKey), std::move(itemValue));
                        }
                    }
                    if (!additionalTools.empty()) {
                        logTrace("加载了 {} 个附属工具", additionalTools.size());
                    } else {
                        logTrace("additionalTools 为空，已清空内置附属工具");
                    }
                }
            }

            logTrace("已加载配置：{}", configPath.string());

        } catch (const std::exception &exception) {
            spdlog::error("YAML 配置文件 {} 解析失败: {}", configPath.string(), exception.what());
        } catch (...) {
            spdlog::error("YAML 配置文件 {} 解析遇到未知异常", configPath.string());
        }
    }
};

inline Config config;
