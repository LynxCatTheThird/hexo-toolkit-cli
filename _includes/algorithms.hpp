#pragma once

#include <algorithm>    // std::max, std::min
#include <cstdint>      // uint32_t
#include <string_view>  // std::string_view
#include <type_traits>  // std::is_constant_evaluated

#include "spdlog/spdlog.h"  // spdlog

// 函数用途：计算两个字符串之间的 Jaro-Winkler 相似度
// 参数：
//   string1: 字符串1
//   string2: 字符串2
// 返回值：返回 0.0 到 1.0 之间的相似度，1.0 表示完全匹配
inline constexpr double getJaroWinklerSimilarity(std::string_view string1, std::string_view string2) {
    // 对于 CLI 命令行工具，输入参数的长度通常极短
    // 使用 std::vector 会强制在堆上动态分配内存，开销远大于计算本身
    // 这里可以设定一个合理的上限（32），在这个范围内使用位运算，速度极快
    // 此检查优先于一切后续比较，避免在超长输入上浪费算力
    constexpr size_t MAXIMUM_LENGTH = 32;
    if (string1.size() > MAXIMUM_LENGTH || string2.size() > MAXIMUM_LENGTH) {
        if (!std::is_constant_evaluated()) {
            spdlog::warn("输入字符串超过最大长度限制 ({})，跳过相似度计算", MAXIMUM_LENGTH);
        }
        return 0.0;
    }

    int lengthFirst = static_cast<int>(string1.length());
    int lengthSecond = static_cast<int>(string2.length());

    if (lengthFirst == 0 && lengthSecond == 0) return 1.0;
    if (lengthFirst == 0 || lengthSecond == 0) return 0.0;
    if (string1 == string2) return 1.0;

    // 匹配窗口大小（保证非负，避免单字符串时 /2-1 = -1 的边界问题）
    int matchDistance = std::max(0, std::max(lengthFirst, lengthSecond) / 2 - 1);

    // 记录被匹配过的字符位置 (使用 uint32_t 位图代替 bool 数组，极大提升性能)
    uint32_t string1Matches = 0;
    uint32_t string2Matches = 0;

    int matches = 0;
    for (int index1 = 0; index1 < lengthFirst; index1++) {
        int start = std::max(0, index1 - matchDistance);
        int end = std::min(index1 + matchDistance + 1, lengthSecond);
        for (int index2 = start; index2 < end; index2++) {
            if (string2Matches & (1U << index2)) continue;
            if (string1[index1] != string2[index2]) continue;
            string1Matches |= (1U << index1);
            string2Matches |= (1U << index2);
            matches++;
            break;
        }
    }

    if (matches == 0) return 0.0;

    // 计算换位次数 (Transpositions)
    int transpositionCount = 0;
    int matchIndex = 0;
    for (int index1 = 0; index1 < lengthFirst; index1++) {
        if (!(string1Matches & (1U << index1))) continue;
        while (!(string2Matches & (1U << matchIndex))) matchIndex++;
        if (string1[index1] != string2[matchIndex]) transpositionCount++;
        matchIndex++;
    }
    transpositionCount /= 2;

    // 计算 Jaro 相似度
    double jaroScore = (static_cast<double>(matches) / lengthFirst + static_cast<double>(matches) / lengthSecond +
                        static_cast<double>(matches - transpositionCount) / matches) /
                       3.0;

    // Winkler 前缀奖励 (最多奖励前 4 个字符)
    int prefixLength = 0;
    int maxPrefixLength = std::min({lengthFirst, lengthSecond, 4});
    for (int index1 = 0; index1 < maxPrefixLength; index1++) {
        if (string1[index1] == string2[index1])
            prefixLength++;
        else
            break;
    }

    // 0.1 是标准的 Winkler 缩放因子
    return jaroScore + prefixLength * 0.1 * (1.0 - jaroScore);
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
    return command.starts_with(input);
}
