#pragma once
#include <string>  // std::string

// 使用 inline 关键字可以在头文件中定义全局变量，避免 ODR（单一定义规则）违规
inline std::string pm = "";         // 全局变量，存储包管理器的基本命令，如 "npm"、"yarn"、"pnpm" 等
inline std::string pmCommand = "";  // 全局变量，存储包管理器命令前缀，如 "npx "、"yarn run "、"pnpm exec " 等
