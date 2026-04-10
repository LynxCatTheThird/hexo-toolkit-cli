#include <iostream>     // std::cout
#include <string>       // std::string
#include <string_view>  // std::string_view
#include <filesystem>   // std::filesystem::exists
#include <cstdlib>      // std::system

#include <globals.hpp>    // 全局变量与状态
#include <configs.hpp>    // 配置文件解析
#include <utils.hpp>      // 辅助函数
#include <algorithms.hpp> // 核心算法
#include <hexo.hpp>       // Hexo 主体业务

// 函数用途：判断命令意图
// 参数：
//   expectedOrder: 传入预期的命令
//   input: 传入argv[1]
// 返回值：若匹配成功则返回 true，否则返回 false
bool isOrder(std::string_view expectedOrder, std::string_view input) {
    if (isPrefixOf(expectedOrder, input) || isPrefixOf(input, expectedOrder)) {
        spdlog::debug("子串方法判断成功，识别意图为 {}", expectedOrder);
        return true;
    }
    double simScore = getJaroWinklerSimilarity(expectedOrder, input);
    if (simScore >= config.similarityThreshold) {
        spdlog::debug("模糊匹配成功，识别意图为 {}", expectedOrder);
        return true;
    } else {
        spdlog::debug("判断意图是否为 {} ... 失败", expectedOrder);
        return false;
    }
}

// 函数用途：检测使用的包管理器
// 返回值：无，函数内部会设置全局变量 pm 和 pmCommand，并输出检测结果
void detectPackageManager() {
    if (std::filesystem::exists("package-lock.json")) {
        pm = "npm";
        pmCommand = "npx ";
        spdlog::debug("检测到包管理器: {}", pm);
        return;
    } else if (std::filesystem::exists("yarn.lock")) {
        pm = "yarn";
        pmCommand = "yarn run ";
        spdlog::debug("检测到包管理器: {}", pm);
        return;
    } else if (std::filesystem::exists("pnpm-lock.yaml")) {
        pm = "pnpm";
        pmCommand = "pnpm exec ";
        spdlog::debug("检测到包管理器: {}", pm);
        return;
    } else {
        pm = "npm";
        pmCommand = "npx ";
        spdlog::debug("未检测到特定包管理器，默认使用: {}", pm);
        return;
    }
}

// 函数用途：显示帮助信息
void utilHelper() {
    std::cout << R"(Hexo 辅助工具
1. 部署静态文件：build、deploy
2. 启动本地预览服务器：server
3. 更新依赖包：packages
4. 更新子模块（更新主题）：theme
5. 显示帮助信息：help
命令有一定鲁棒性，欢迎翻阅源代码查看具体实现
)";
}

// 函数用途：主函数
// 参数：
//   argc: 命令行参数个数
//   argv: 命令行参数数组
// 返回值：0 - 成功，1 - 失败
int main(int argc, char* argv[ ]) {
    initLogger();
    config.loadFromYamlIfExists();
    detectPackageManager();

    // 根据检测到的包管理器自动设置依赖搜索文件
    if (config.shouldAutoDetectDependencies) { // 如果未在配置文件中指定，则自动设置
        if (pm == "npm") config.dependenciesSearchingFile = "package-lock.json";
        else if (pm == "yarn") config.dependenciesSearchingFile = "yarn.lock";
        else if (pm == "pnpm") config.dependenciesSearchingFile = "pnpm-lock.yaml";
        else config.dependenciesSearchingFile = "package.json";
    }
    spdlog::debug("依赖搜索文件设置为: {}", config.dependenciesSearchingFile);

    if (argc == 1) {
        spdlog::error("请输入参数");
        utilHelper();
        return 1;
    } else if (isOrder("build", argv[1]) || isOrder("deploy", argv[1])) {
        hexoBuild();
    } else if (isOrder("server", argv[1])) {
        hexoServer();
    } else if (isOrder("theme", argv[1])) {
        std::system("git submodule update --remote --merge");
    } else if (isOrder("packages", argv[1])) {
        std::string command;
        if (pm == "yarn") {
            command = "yarn upgrade --latest";
        } else if (pm == "pnpm") {
            command = "pnpm update --latest";
        } else {
            command = "npx rimraf node_modules package-lock.json && ncu -u && npm install"; // 默认
        }
        spdlog::debug("检测到包管理器: {}", pm);
        std::system(command.c_str());
    } else {
        spdlog::error("无效的参数：所有判断都失败了，无法判断命令意图");
        spdlog::error("您确定 {} 是正确的命令吗？", std::string(argv[1]));
        utilHelper();
        return 1;
    }
    return 0;
}
