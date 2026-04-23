#include <initializer_list>  //std::initializer_list
#include <iostream>          // std::cout
#include <string>            // std::string

#include "algorithms.hpp"  // 核心算法
#include "configs.hpp"     // 配置文件解析
#include "hexo.hpp"        // Hexo 主体业务
#include "logs.hpp"        // 日志

// 函数用途：判断命令意图
// 参数：
//   expectedOrder: 传入预期的命令
//   input: 传入argv[1]
// 返回值：若匹配成功则返回 true，否则返回 false
bool isOrder(std::string_view expectedOrder, std::string_view input) {
    if (isPrefixOf(input, expectedOrder)) {
        spdlog::debug("子串方法判断成功，识别意图为 {}", expectedOrder);
        return true;
    }
    double similarityScore = getJaroWinklerSimilarity(expectedOrder, input);
    if (similarityScore >= config.similarityThreshold) {
        spdlog::debug("模糊匹配成功，识别意图为 {}", expectedOrder);
        return true;
    } else {
        spdlog::debug("判断意图是否为 {} ... 失败", expectedOrder);
        return false;
    }
}

// 函数用途：判断命令意图（支持多个候选）
bool isOrderAny(std::initializer_list<std::string_view> expectedOrders, std::string_view input) {
    for (auto order : expectedOrders) {
        if (isPrefixOf(input, order)) {
            spdlog::debug("子串方法判断成功，识别意图为 {}", order);
            return true;
        }
    }
    for (auto order : expectedOrders) {
        double similarityScore = getJaroWinklerSimilarity(order, input);
        if (similarityScore >= config.similarityThreshold) {
            spdlog::debug("模糊匹配成功，识别意图为 {}", order);
            return true;
        }
    }
    return false;
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
int main(int argc, char *argv[]) {
    initLogger();

    std::string_view commandString = "";

    if (argc >= 2) {
        std::string_view arg1(argv[1]);
        std::string_view arg2 = argc >= 3 ? std::string_view(argv[2]) : "";

        // 判断是否为开启 debug 的标识符（支持模糊匹配）
        auto isDebugFlag = [](std::string_view s) {
            return isOrderAny({"--debug", "-debug", "--verbose", "-verbose", "-v"}, s);
        };

        if (isDebugFlag(arg1)) {
            spdlog::set_level(spdlog::level::debug);
            commandString = arg2;
        } else if (!arg2.empty() && isDebugFlag(arg2)) {
            spdlog::set_level(spdlog::level::debug);
            commandString = arg1;
        } else {
            commandString = arg1;
        }
    }

    config.loadFromYamlIfExists();
    config.detectPackageManager();

    if (commandString.empty()) {
        spdlog::error("请输入参数");
        utilHelper();
        return 1;
    }

    if (isOrderAny({"build", "deploy"}, commandString)) {
        hexoBuild();
    } else if (isOrder("server", commandString)) {
        hexoServer();
    } else if (isOrder("theme", commandString)) {
        int submoduleExitCode = runCommand("git submodule update --remote --merge");
        if (submoduleExitCode != 0) spdlog::error("git submodule 更新失败，退出码: {}", submoduleExitCode);
    } else if (isOrder("packages", commandString)) {
        spdlog::debug("检测到包管理器: {}", config.packageManager);
        int packageManagerExitCode = runCommand(config.upgradeCommand);
        if (packageManagerExitCode != 0)
            spdlog::error("包管理器 {} 更新失败，退出码: {}", config.packageManager, packageManagerExitCode);
    } else {
        spdlog::error("无效的参数：所有判断都失败了，无法判断命令意图");
        spdlog::error("您确定 {} 是正确的命令吗？", std::string(commandString));
        utilHelper();
        return 1;
    }
    return 0;
}
