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
  GIT_TAG        v0.16.0
  GIT_SHALLOW    ON
)
set(RYML_DEFAULT_CALLBACK_USES_EXCEPTIONS ON CACHE BOOL "Report YAML errors as exceptions" FORCE)
FetchContent_MakeAvailable(rapidyaml)

add_executable(${PROJECT_NAME} main.cpp)
target_compile_definitions(${PROJECT_NAME} PRIVATE HEXO_TOOL_VERSION="${PROJECT_VERSION}")
target_include_directories(${PROJECT_NAME} PRIVATE _includes)
function(hexo_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow)
    endif()
endfunction()
hexo_enable_warnings(${PROJECT_NAME})

# 链接库
target_link_libraries(${PROJECT_NAME} PRIVATE 
    spdlog::spdlog 
    ryml
)

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(HexoToolTests tests/core_tests.cpp)
    target_include_directories(HexoToolTests PRIVATE _includes)
    target_link_libraries(HexoToolTests PRIVATE spdlog::spdlog ryml)
    hexo_enable_warnings(HexoToolTests)
    if(WIN32)
        target_link_libraries(HexoToolTests PRIVATE ws2_32)
    endif()

    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME workflows COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/workflow_tests.py" $<TARGET_FILE:${PROJECT_NAME}>)
    set_tests_properties(workflows PROPERTIES TIMEOUT 120)
    add_test(NAME core COMMAND HexoToolTests)
    add_test(NAME cli_help COMMAND $<TARGET_FILE:${PROJECT_NAME}> help)
    set_tests_properties(cli_help PROPERTIES PASS_REGULAR_EXPRESSION "deploy    生成并部署（一条龙）")
    add_test(NAME cli_version COMMAND $<TARGET_FILE:${PROJECT_NAME}> --version)
    add_test(NAME cli_no_arguments COMMAND $<TARGET_FILE:${PROJECT_NAME}>)
    add_test(NAME cli_rejects_extra_arguments COMMAND $<TARGET_FILE:${PROJECT_NAME}> help unexpected)
    set_tests_properties(cli_rejects_extra_arguments PROPERTIES WILL_FAIL TRUE)
    add_test(NAME cli_rejects_non_hexo_project COMMAND $<TARGET_FILE:${PROJECT_NAME}> build --dry-run)
    set_tests_properties(cli_rejects_non_hexo_project PROPERTIES WILL_FAIL TRUE)

    set(HEXO_TEST_PROJECT "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/hexo-project")
    add_test(NAME cli_build_dry_run
             COMMAND $<TARGET_FILE:${PROJECT_NAME}> build --dry-run --project "${HEXO_TEST_PROJECT}" -- --config
                     "site config.yml")
    set_tests_properties(cli_build_dry_run PROPERTIES
                         PASS_REGULAR_EXPRESSION "hexo generate"
                         FAIL_REGULAR_EXPRESSION "hexo deploy|配置文件|未安装")

    add_test(NAME cli_deploy_dry_run
             COMMAND $<TARGET_FILE:${PROJECT_NAME}> deploy --dry-run --project "${HEXO_TEST_PROJECT}")
    set_tests_properties(cli_deploy_dry_run PROPERTIES
                         PASS_REGULAR_EXPRESSION "预演完成，未执行命令"
                         FAIL_REGULAR_EXPRESSION "配置文件|未安装")

    add_test(NAME cli_verbose_dry_run
             COMMAND $<TARGET_FILE:${PROJECT_NAME}> deploy -v --dry-run --project "${HEXO_TEST_PROJECT}")
    set_tests_properties(cli_verbose_dry_run PROPERTIES
                         PASS_REGULAR_EXPRESSION "配置 默认配置 · npm · 2 个附属工具"
                         FAIL_REGULAR_EXPRESSION "前缀匹配|模糊匹配|未匹配|2>&1")

    add_test(NAME cli_trace_dry_run
             COMMAND $<TARGET_FILE:${PROJECT_NAME}> deploy -vv --dry-run --project "${HEXO_TEST_PROJECT}")
    set_tests_properties(cli_trace_dry_run PROPERTIES
                         PASS_REGULAR_EXPRESSION "命令匹配：deploy"
                         FAIL_REGULAR_EXPRESSION "2>&1")

    add_test(NAME cli_packages_alias COMMAND $<TARGET_FILE:${PROJECT_NAME}> pkg --dry-run --project "${HEXO_TEST_PROJECT}")
    set_tests_properties(cli_packages_alias PROPERTIES PASS_REGULAR_EXPRESSION "npm-check-updates")
    add_test(NAME cli_rejects_unknown_option COMMAND $<TARGET_FILE:${PROJECT_NAME}> --hel)
    set_tests_properties(cli_rejects_unknown_option PROPERTIES WILL_FAIL TRUE)

    if(WIN32)
        add_test(NAME cli_rejects_unsafe_windows_arguments
                 COMMAND $<TARGET_FILE:${PROJECT_NAME}> build --dry-run --project "${HEXO_TEST_PROJECT}" -- "&")
        set_tests_properties(cli_rejects_unsafe_windows_arguments PROPERTIES WILL_FAIL TRUE)
    endif()
endif()
