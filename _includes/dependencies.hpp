#pragma once
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "logs.hpp"
#include "ryml.hpp"
#include "ryml_std.hpp"

// 函数用途：一次性读取整个文件内容
// 参数：
//   path: 要读取的目标文件路径
// 返回值：包含文件所有内容的 std::string，如果读取失败则返回空字符串
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

// 函数用途：检查缓存的文件内容中是否存在特定依赖项
// 参数：
//   fileContent: 文件的完整内容字符串
//   dependencies: 要查找的依赖项
// 返回值：若依赖声明中存在指定的包名则返回 true，否则返回 false
inline bool isDependenciesPresent(std::string_view fileContent, std::string_view dependencies,
                                  const std::filesystem::path &dependencyFile = "package.json") {
    if (fileContent.empty()) return false;

    // Parse direct dependency fields only. Custom paths use the same manifest schema.
    static_cast<void>(dependencyFile);
    try {
        ryml::Tree tree = ryml::parse_in_arena(ryml::csubstr(fileContent.data(), fileContent.size()));
        ryml::NodeRef root = tree.rootref();
        if (!root.is_map()) return false;
        const std::string dependencyName(dependencies);
        for (const char *sectionName :
             {"dependencies", "devDependencies", "optionalDependencies", "peerDependencies"}) {
            if (root.has_child(sectionName) && root[sectionName].is_map() &&
                root[sectionName].has_child(ryml::to_csubstr(dependencyName))) {
                return true;
            }
        }
        return false;
    } catch (const std::exception &) {
        return false;
    }
}
