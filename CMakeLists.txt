cmake_minimum_required(VERSION 3.24)

project(HexoTool VERSION 1.0 LANGUAGES CXX)

# 全局设置
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN YES)

# 编译缓存选项
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()

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

# 拉取 mimalloc 并替换默认分配器
FetchContent_Declare(
    mimalloc
    GIT_REPOSITORY https://github.com/microsoft/mimalloc.git
    GIT_TAG        v2.1.2
    GIT_SHALLOW    ON
)
# 关闭 mimalloc 自身的测试和没必要的构建目标
set(MI_BUILD_TESTS OFF CACHE BOOL "Disable mimalloc tests" FORCE)
set(MI_BUILD_OBJECT OFF CACHE BOOL "Disable mimalloc object build" FORCE)

set(MI_OVERRIDE ON CACHE BOOL "Override standard allocations" FORCE)
set(MI_BUILD_SHARED OFF CACHE BOOL "Build shared library" FORCE)
FetchContent_MakeAvailable(mimalloc)

# 目标定义与 Unity Build 加速编译
add_executable(${PROJECT_NAME} main.cpp)
set_target_properties(${PROJECT_NAME} PROPERTIES UNITY_BUILD ON)

target_precompile_headers(${PROJECT_NAME} PRIVATE 
    <spdlog/spdlog.h>
)

target_include_directories(${PROJECT_NAME} PRIVATE _includes)

# PGO 编译开关
# 用法: cmake -B build -DENABLE_PGO_GEN=ON (生成插桩文件) -> 运行程序 -> cmake -B build -DENABLE_PGO_USE=ON (使用配置优化)
option(ENABLE_PGO_GEN "Enable PGO generation" OFF)
option(ENABLE_PGO_USE "Enable PGO usage" OFF)

# 编译选项与优化
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic -march=native -O3 -fstrict-aliasing)
    target_link_options(${PROJECT_NAME} PRIVATE -Wl,--gc-sections -s)
    
    # 启用 mold 或 lld 链接器
    find_program(MOLD_LINKER mold)
    find_program(LLD_LINKER lld)
    if(MOLD_LINKER)
        target_link_options(${PROJECT_NAME} PRIVATE -fuse-ld=mold)
    elseif(LLD_LINKER)
        target_link_options(${PROJECT_NAME} PRIVATE -fuse-ld=lld)
    endif()

    # 处理 PGO 参数
    if(ENABLE_PGO_GEN)
        target_compile_options(${PROJECT_NAME} PRIVATE -fprofile-generate)
        target_link_options(${PROJECT_NAME} PRIVATE -fprofile-generate)
    elseif(ENABLE_PGO_USE)
        target_compile_options(${PROJECT_NAME} PRIVATE -fprofile-use -fprofile-correction)
        target_link_options(${PROJECT_NAME} PRIVATE -fprofile-use)
    endif()

elseif(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /Gy /Gw /O2)
    target_link_options(${PROJECT_NAME} PRIVATE /OPT:REF /OPT:ICF /DEBUG:NONE)
endif()

# 开启 LTO (插桩收集 PGO 数据时关闭 LTO 避免冲突)
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported)
if(ipo_supported AND NOT ENABLE_PGO_GEN)
    set_target_properties(${PROJECT_NAME} PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

# 链接库
target_link_libraries(${PROJECT_NAME} PRIVATE 
    spdlog::spdlog 
    ryml
    mimalloc-static
)

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32)
endif()

# UPX 产物压缩
# find_program(UPX_PROGRAM upx)
# if(UPX_PROGRAM)
#     add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
#         COMMAND ${UPX_PROGRAM} --best --lzma $<TARGET_FILE:${PROJECT_NAME}>
#         COMMENT "Compressing binary with UPX"
#     )
# endif()
