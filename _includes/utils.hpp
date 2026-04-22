#pragma once

#include <cstdlib>      // std::system
#include <string>       // std::string
#include <string_view>  // std::string_view

#if defined(_WIN32)
#include <winsock2.h>  // Windows Sockets API
#pragma comment(lib, "ws2_32.lib")
#else
#include <netinet/in.h>  // AF_INET, INADDR_LOOPBACK
#include <unistd.h>      // close()
#endif

// 根据操作系统的不同指向不同的空设备路径
// 仅重定向 stdout，stderr 保留输出以便调试
#if defined(_WIN32)
inline constexpr std::string_view DEV_NULL = " > NUL";
#else
inline constexpr std::string_view DEV_NULL = " > /dev/null";
#endif

// 函数用途：检查缓存的文件内容中是否存在特定依赖项
// 参数：
//   fileContent: 文件的完整内容字符串
//   dependencies: 要查找的依赖项
// 返回值：若字符串中存在指定的依赖项则返回 true，否则返回 false
inline bool isDependenciesPresent(std::string_view fileContent, std::string_view dependencies) {
    if (fileContent.empty()) return false;
    return fileContent.find(dependencies) != std::string_view::npos;
}

// 函数用途：判断给定端口是否处于打开状态
// 参数：port - 待判断的端口号
// 返回值：若端口打开则返回true，否则返回false
inline bool isPortOpen(int port) {
#if defined(_WIN32)
    WSADATA wsaData;
    // Windows 下必须先初始化网络库
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return false;
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {  // 若创建套接字失败返回端口未打开
        WSACleanup();
        return false;
    }
#else
    int sock = socket(AF_INET, SOCK_STREAM, 0);  // 创建一个套接字
    if (sock < 0) return false;                  // 若创建套接字失败返回端口未打开
#endif

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;                      // 地址族为IPv4
    addr.sin_port = htons(port);                    // 设置端口号
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // 设置为本地回环地址

    bool isOpen = false;  // 初始化端口状态为未打开
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
        isOpen = true;  // 连接成功，端口被占用
    }

#if defined(_WIN32)
    closesocket(sock);  // 关闭套接字
    WSACleanup();
#else
    close(sock);  // 关闭套接字
#endif

    return isOpen;
}

// 用于异步执行系统命令
// 输入：const std::string& command - 要执行的命令
// 输出：int - 命令执行的返回值
inline int executeCommand(const std::string &command) {
    // 调用系统命令执行函数执行给定的命令
    return std::system(command.c_str());
}
