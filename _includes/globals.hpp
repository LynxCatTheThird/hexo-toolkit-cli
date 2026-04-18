#pragma once
#include <string>  // std::string

// 全局变量，存储包管理器的基本命令，如 "npm"、"yarn"、"pnpm" 等
inline std::string packageManager = "";
// 全局变量，存储包管理器命令前缀，如 "npx "、"yarn run "、"pnpm exec " 等
inline std::string packageManagerCommand = "";
