#pragma once
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "logs.hpp"
#include "ryml.hpp"
#include "ryml_std.hpp"

// 空文件或打开失败返回空字符串，读取中途失败抛出异常。
inline std::string readFileContents(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file) {
        logTrace("无法打开文件：{}", path.string());
        return {};
    }
    file.seekg(0, std::ios::end);
    std::streamsize const size = file.tellg();
    if (size <= 0) return {};
    std::string content(static_cast<size_t>(size), '\0');
    file.seekg(0, std::ios::beg);
    file.read(content.data(), size);
    if (!file) throw std::runtime_error("读取文件失败：" + path.string());
    return content;
}

// 在 manifest 的依赖字段中精确匹配包名。
inline bool isDependenciesPresent(ryml::ConstNodeRef root, std::string_view dependency) {
    if (!root.is_map()) return false;
    for (const char *sectionName : {"dependencies", "devDependencies", "optionalDependencies", "peerDependencies"}) {
        if (root.has_child(sectionName) && root[sectionName].is_map() &&
            root[sectionName].has_child(ryml::csubstr(dependency.data(), dependency.size()))) {
            return true;
        }
    }
    return false;
}
