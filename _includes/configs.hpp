#pragma once

#include <string>           // std::string
#include <string_view>      // std::string_view
#include <unordered_map>    // std::unordered_map
#include <vector>           // std::vector, std::pair
#include <filesystem>       // std::filesystem
#include <fstream>          // std::ifstream
#include <optional>         // std::optional, std::nullopt

#if defined(_WIN32)
#include <windows.h>        // Windows API
#elif defined(__linux__)
#include <unistd.h>         // POSIX API
#include <linux/limits.h>   // PATH_MAX
#endif

#include <logs.hpp>

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

namespace SimpleYAML {

    class Node {
    public:
        std::unordered_map<std::string, std::string> scalars;
        std::vector<std::pair<std::string, std::string>> additionalTools;

        // 传入 string_view 避免拷贝，内部按行切割
        bool parse(std::string_view content) {
            size_t start = 0;
            while (start < content.size()) {
                // 手动寻找换行符，比 std::getline(stringstream) 性能更高，无内存分配
                size_t end = content.find('\n', start);
                if (end == std::string_view::npos) end = content.size();

                std::string_view line = content.substr(start, end - start);
                start = end + 1; // 移动到下一行

                // 剔除注释
                if (auto hash_pos = line.find('#'); hash_pos != std::string_view::npos) {
                    line = line.substr(0, hash_pos);
                }

                line = trim(line);
                if (line.empty()) continue;

                // 解析 - [k, v] 格式的数组
                if (line.front() == '-') {
                    auto pos1 = line.find('[');
                    auto pos2 = line.find(']');
                    if (pos1 != std::string_view::npos && pos2 != std::string_view::npos && pos2 > pos1) {
                        auto pairStr = line.substr(pos1 + 1, pos2 - pos1 - 1);
                        auto comma = pairStr.find(',');
                        if (comma != std::string_view::npos) {
                            // 使用 string_view 裁剪并去引号
                            std::string_view k = stripQuotes(trim(pairStr.substr(0, comma)));
                            std::string_view v = stripQuotes(trim(pairStr.substr(comma + 1)));
                            // 仅在此处存入 vector 时发生拷贝
                            additionalTools.emplace_back(std::string(k), std::string(v));
                        }
                    }
                }
                // 解析 key: value 格式
                else {
                    auto colon = line.find(':');
                    if (colon != std::string_view::npos) {
                        std::string_view key_view = trim(line.substr(0, colon));
                        std::string_view val_view = stripQuotes(trim(line.substr(colon + 1)));
                        if (!key_view.empty()) {
                            scalars[std::string(key_view)] = std::string(val_view);
                        }
                    }
                }
            }
            return true;
        }

        // 安全检查
        std::optional<std::string> get_opt(std::string_view key) const {
            if (auto it = scalars.find(std::string(key)); it != scalars.end()) {
                return it->second;
            }
            return std::nullopt;
        }

        // 兼容你原有代码的旧接口
        std::string get(const std::string& key, const std::string& def = "") const {
            auto val = get_opt(key);
            return val.has_value() ? *val : def;
        }

        double getDouble(const std::string& key, double def) const {
            auto val = get_opt(key);
            if (!val.has_value() || val->empty()) return def;
            try { return std::stod(*val); }
            catch (...) { return def; }
        }

        short getShort(const std::string& key, short def) const {
            auto val = get_opt(key);
            if (!val.has_value() || val->empty()) return def;
            try { return static_cast<short>(std::stoi(*val)); }
            catch (...) { return def; }
        }

    private:
        static constexpr std::string_view trim(std::string_view s) {
            size_t start = s.find_first_not_of(" \t\r\n");
            if (start == std::string_view::npos) return {};
            s.remove_prefix(start);
            size_t end = s.find_last_not_of(" \t\r\n");
            if (end != std::string_view::npos) s.remove_suffix(s.size() - end - 1);
            return s;
        }

        static constexpr std::string_view stripQuotes(std::string_view s) {
            if (s.size() >= 2) {
                if ((s.starts_with('"') && s.ends_with('"')) ||
                    (s.starts_with('\'') && s.ends_with('\''))) {
                    s.remove_prefix(1);
                    s.remove_suffix(1);
                }
            }
            return s;
        }
    };

} // namespace SimpleYAML


struct Config {
    double similarityThreshold = 0.85;
    std::string dependenciesSearchingFile = "package.json";
    std::vector<std::pair<std::string, std::string>> additionalTools = {
        {"swpp", "hexo swpp"},
        {"gulp", "gulp zip"},
        {"algolia", "hexo algolia"}
    };

    void logPathInfo(const std::filesystem::path& configPath) {
        spdlog::debug("配置文件路径: {}", configPath.string());
        spdlog::debug("可执行文件目录: {}", getExecutableDir().string());
        spdlog::debug("当前工作目录: {}", std::filesystem::current_path().string());
    }

    void loadFromYamlIfExists(const std::string& filename = "config.yaml") {
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

        // 一次性把文件读入 string，比使用 stringstream 快得多
        std::string content((std::istreambuf_iterator<char>(fin)), std::istreambuf_iterator<char>());

        SimpleYAML::Node node;
        if (!node.parse(content)) {
            spdlog::error("YAML 配置文件 {} 解析失败", configPath.string());
            return;
        }

        // 使用兼容的接口恢复逻辑
        double temp_threshold = node.getDouble("similarityThreshold", similarityThreshold);
        if (temp_threshold > 0.0 && temp_threshold < 1.0) {
            similarityThreshold = temp_threshold;
        } else {
            spdlog::error("非法相似度阈值，保留默认值 {}", similarityThreshold);
        }

        // 读取可能的 package.json 配置
        dependenciesSearchingFile = node.get("dependenciesSearchingFile", dependenciesSearchingFile);

        if (!node.additionalTools.empty()) {
            spdlog::info("加载了 {} 个扩展工具", node.additionalTools.size());
            additionalTools = std::move(node.additionalTools); // 移动语义优化
        }
        spdlog::info("成功加载配置 {}", configPath.string());
    }
};

inline Config config;