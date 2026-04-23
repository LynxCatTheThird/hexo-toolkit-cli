#include <cstdlib>   // std::system
#include <iostream>  // std::cout
#include <string>    // std::string

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
    double simScore = getJaroWinklerSimilarity(expectedOrder, input);
    if (simScore >= config.similarityThreshold) {
        spdlog::debug("模糊匹配成功，识别意图为 {}", expectedOrder);
        return true;
    } else {
        spdlog::debug("判断意图是否为 {} ... 失败", expectedOrder);
        return false;
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
int main(int argc, char *argv[]) {
    initLogger();
    config.loadFromYamlIfExists();
    config.detectPackageManager();

    if (argc == 1) {
        spdlog::error("请输入参数");
        utilHelper();
        return 1;
    } else if (isOrder("build", argv[1]) || isOrder("deploy", argv[1])) {
        hexoBuild();
    } else if (isOrder("server", argv[1])) {
        hexoServer();
    } else if (isOrder("theme", argv[1])) {
        int submoduleExitCode = std::system("git submodule update --remote --merge");
        if (submoduleExitCode != 0) spdlog::error("git submodule 更新失败，退出码: {}", submoduleExitCode);
    } else if (isOrder("packages", argv[1])) {
        spdlog::debug("检测到包管理器: {}", config.packageManager);
        for (const auto &pm : PM_TABLE) {
            if (config.packageManager == pm.name) {
                int pmExitCode = std::system(std::string(pm.upgradeCommand).c_str());
                if (pmExitCode != 0) spdlog::error("包管理器 {} 更新失败，退出码: {}", pm.name, pmExitCode);
                break;
            }
        }
    } else {
        spdlog::error("无效的参数：所有判断都失败了，无法判断命令意图");
        spdlog::error("您确定 {} 是正确的命令吗？", std::string(argv[1]));
        utilHelper();
        return 1;
    }
    return 0;
}
