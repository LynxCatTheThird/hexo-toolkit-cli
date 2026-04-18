cmake_minimum_required(VERSION 3.24)

project(HexoTool VERSION 1.0 LANGUAGES CXX)

# 全局设置
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 依赖拉取
include(FetchContent)

# 拉取 spdlog
FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG        v1.17.0
  GIT_SHALLOW    ON
)
FetchContent_MakeAvailable(spdlog)

# 拉取 rapidyaml
FetchContent_Declare(
  rapidyaml
  GIT_REPOSITORY https://github.com/biojppm/rapidyaml
  GIT_TAG        v0.11.1
  GIT_SHALLOW    ON
)
FetchContent_MakeAvailable(rapidyaml)

# 目标定义
add_executable(${PROJECT_NAME} main.cpp)

# 包含目录
target_include_directories(${PROJECT_NAME} PRIVATE _includes)

# 编译选项与优化
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic -march=native) # 严谨的警告级别，并开启原生架构优化
    target_link_options(${PROJECT_NAME} PRIVATE -Wl,--gc-sections) # 消除未使用的代码段
elseif(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /Gy /Gw) # 独立打包
    target_link_options(${PROJECT_NAME} PRIVATE /OPT:REF /OPT:ICF) # 去除重复或未引用代码
endif()

# 开启 LTO
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported)
if(ipo_supported)
    set_target_properties(${PROJECT_NAME} PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

# 链接库
target_link_libraries(${PROJECT_NAME} PRIVATE 
    spdlog::spdlog 
    ryml
)

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32)
endif()