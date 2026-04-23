#pragma once

#include <algorithm>    // std::max, std::min
#include <cstdint>      // uint32_t
#include <string_view>  // std::string_view
#include <type_traits>  // std::is_constant_evaluated

#include "spdlog/spdlog.h"  // spdlog::warn

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
    constexpr size_t MAX_LEN = 32;
    if (string1.size() > MAX_LEN || string2.size() > MAX_LEN) {
        if (!std::is_constant_evaluated()) {
            spdlog::warn("输入字符串超过最大长度限制 ({})，跳过相似度计算", MAX_LEN);
        }
        return 0.0;
    }

    int length1 = static_cast<int>(string1.length());
    int length2 = static_cast<int>(string2.length());

    if (length1 == 0 && length2 == 0) return 1.0;
    if (length1 == 0 || length2 == 0) return 0.0;
    if (string1 == string2) return 1.0;

    // 匹配窗口大小（保证非负，避免单字符串时 /2-1 = -1 的边界问题）
    int match_distance = std::max(0, std::max(length1, length2) / 2 - 1);

    // 记录被匹配过的字符位置 (使用 uint32_t 位图代替 bool 数组，极大提升性能)
    uint32_t string1_matches = 0;
    uint32_t string2_matches = 0;

    int matches = 0;
    for (int i = 0; i < length1; i++) {
        int start = std::max(0, i - match_distance);
        int end = std::min(i + match_distance + 1, length2);
        for (int j = start; j < end; j++) {
            if (string2_matches & (1U << j)) continue;
            if (string1[i] != string2[j]) continue;
            string1_matches |= (1U << i);
            string2_matches |= (1U << j);
            matches++;
            break;
        }
    }

    if (matches == 0) return 0.0;

    // 计算换位次数 (Transpositions)
    int transpositions = 0;
    int matchIndex = 0;
    for (int i = 0; i < length1; i++) {
        if (!(string1_matches & (1U << i))) continue;
        while (!(string2_matches & (1U << matchIndex))) matchIndex++;
        if (string1[i] != string2[matchIndex]) transpositions++;
        matchIndex++;
    }
    transpositions /= 2;

    // 计算 Jaro 相似度
    double jaro = ((double)matches / length1 + (double)matches / length2 + (double)(matches - transpositions) / matches) / 3.0;

    // Winkler 前缀奖励 (最多奖励前 4 个字符)
    int prefix = 0;
    int max_prefix = std::min({length1, length2, 4});
    for (int i = 0; i < max_prefix; i++) {
        if (string1[i] == string2[i])
            prefix++;
        else
            break;
    }

    // 0.1 是标准的 Winkler 缩放因子
    return jaro + prefix * 0.1 * (1.0 - jaro);
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
inline constexpr bool isPrefixOf(std::string_view input, std::string_view command) { return command.starts_with(input); }