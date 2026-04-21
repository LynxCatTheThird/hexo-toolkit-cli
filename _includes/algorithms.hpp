#pragma once

#include <algorithm>    // std::max, std::min
#include <string_view>  // std::string_view

// 函数用途：计算两个字符串之间的 Jaro-Winkler 相似度
// 参数：
//   s1: 字符串1
//   s2: 字符串2
// 返回值：返回 0.0 到 1.0 之间的相似度，1.0 表示完全匹配
inline double getJaroWinklerSimilarity(std::string_view s1, std::string_view s2) {
    int len1 = s1.length();
    int len2 = s2.length();

    if (len1 == 0 && len2 == 0) return 1.0;
    if (len1 == 0 || len2 == 0) return 0.0;
    if (s1 == s2) return 1.0;

    // 匹配窗口大小
    int match_distance = std::max(len1, len2) / 2 - 1;

    // 对于 CLI 命令行工具，输入参数的长度通常极短
    // 使用 std::vector 会强制在堆上动态分配内存，开销远大于计算本身
    // 我们设定一个合理的上限（例如 32），在这个范围内使用栈内存，速度极快
    const int MAX_LEN = 32;
    if (len1 > MAX_LEN || len2 > MAX_LEN) return 0.0;  // 如果输入了离谱的超长字符串，直接判定为不匹配

    // 记录被匹配过的字符位置 (栈上分配，无 new/malloc 开销)
    bool s1_matches[MAX_LEN] = {false};
    bool s2_matches[MAX_LEN] = {false};

    int matches = 0;
    for (int i = 0; i < len1; i++) {
        int start = std::max(0, i - match_distance);
        int end = std::min(i + match_distance + 1, len2);
        for (int j = start; j < end; j++) {
            if (s2_matches[j]) continue;
            if (s1[i] != s2[j]) continue;
            s1_matches[i] = true;
            s2_matches[j] = true;
            matches++;
            break;
        }
    }

    if (matches == 0) return 0.0;

    // 计算换位次数 (Transpositions)
    int t = 0;
    int k = 0;
    for (int i = 0; i < len1; i++) {
        if (!s1_matches[i]) continue;
        while (!s2_matches[k]) k++;
        if (s1[i] != s2[k]) t++;
        k++;
    }
    t /= 2;

    // 计算 Jaro 相似度
    double jaro = ((double)matches / len1 + (double)matches / len2 + (double)(matches - t) / matches) / 3.0;

    // Winkler 前缀奖励 (最多奖励前 4 个字符)
    int prefix = 0;
    for (int i = 0; i < std::min(std::min(len1, len2), 4); i++) {
        if (s1[i] == s2[i])
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
inline bool isPrefixOf(std::string_view input, std::string_view command) { return command.starts_with(input); }