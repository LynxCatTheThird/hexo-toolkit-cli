#pragma once

#include <algorithm>  // std::max, std::min
#include <cstddef>
#include <string_view>  // std::string_view
#include <vector>

// 函数用途：计算两个字符串之间的 Jaro-Winkler 相似度
// 参数：
//   string1: 字符串1
//   string2: 字符串2
// 返回值：返回 0.0 到 1.0 之间的相似度，1.0 表示完全匹配
inline double getJaroWinklerSimilarity(std::string_view string1, std::string_view string2) {
    const auto lengthFirst = string1.length();
    const auto lengthSecond = string2.length();

    if (lengthFirst == 0 && lengthSecond == 0) return 1.0;
    if (lengthFirst == 0 || lengthSecond == 0) return 0.0;
    if (string1 == string2) return 1.0;

    // 匹配窗口大小（保证非负，避免单字符串时 /2-1 = -1 的边界问题）
    const auto halfLength = std::max(lengthFirst, lengthSecond) / 2;
    const auto matchDistance = halfLength > 0 ? halfLength - 1 : 0;

    std::vector<bool> string1Matches(lengthFirst, false);
    std::vector<bool> string2Matches(lengthSecond, false);

    std::size_t matches = 0;
    for (std::size_t index1 = 0; index1 < lengthFirst; index1++) {
        const auto start = index1 > matchDistance ? index1 - matchDistance : 0;
        const auto end = std::min(index1 + matchDistance + 1, lengthSecond);
        for (std::size_t index2 = start; index2 < end; index2++) {
            if (string2Matches[index2]) continue;
            if (string1[index1] != string2[index2]) continue;
            string1Matches[index1] = true;
            string2Matches[index2] = true;
            matches++;
            break;
        }
    }

    if (matches == 0) return 0.0;

    // 计算换位次数 (Transpositions)
    std::size_t transpositionCount = 0;
    std::size_t matchIndex = 0;
    for (std::size_t index1 = 0; index1 < lengthFirst; index1++) {
        if (!string1Matches[index1]) continue;
        while (!string2Matches[matchIndex]) matchIndex++;
        if (string1[index1] != string2[matchIndex]) transpositionCount++;
        matchIndex++;
    }
    const double transpositions = static_cast<double>(transpositionCount) / 2.0;

    // 计算 Jaro 相似度
    double const jaroScore = (static_cast<double>(matches) / static_cast<double>(lengthFirst) +
                              static_cast<double>(matches) / static_cast<double>(lengthSecond) +
                              (static_cast<double>(matches) - transpositions) / static_cast<double>(matches)) /
                             3.0;

    // Winkler 前缀奖励 (最多奖励前 4 个字符)
    std::size_t prefixLength = 0;
    const auto maxPrefixLength = std::min({lengthFirst, lengthSecond, std::size_t{4}});
    for (std::size_t index1 = 0; index1 < maxPrefixLength; index1++) {
        if (string1[index1] == string2[index1])
            prefixLength++;
        else
            break;
    }

    // 0.1 是标准的 Winkler 缩放因子
    if (jaroScore < 0.7) return jaroScore;
    return jaroScore + static_cast<double>(prefixLength) * 0.1 * (1.0 - jaroScore);
}

// 函数用途：判断用户输入是否为指令的合法缩写（头部子串）
// 参数：
//   input: 用户输入的字符串（如 "bu"）
//   command: 预设的完整指令字符串（如 "build"）
// 返回值：若 input 是 command 的开头部分则返回 true
// 举例：
//   isPrefixOf("bu", "build") -> true
//   isPrefixOf("build", "build") -> true
//   isPrefixOf("builds", "build") -> false
inline constexpr bool isPrefixOf(std::string_view input, std::string_view command) {
    return !input.empty() && command.starts_with(input);
}
