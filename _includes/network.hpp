#pragma once
#include <cstdint>

#include "process.hpp"

// 函数用途：判断给定端口是否已被占用（尝试绑定 IPv4 通配地址）
// 参数：port - 待判断的端口号
// 返回值：端口无法绑定时返回 true
inline bool isPortInUse(std::uint16_t port) {
#if defined(_WIN32)
    // 保证局部 static 的初始化是线程安全的，Winsock 只会初始化一次
    static detail::WinsockInit winsockInitialization;
    SOCKET socketDescriptor = socket(AF_INET, SOCK_STREAM, 0);
    if (socketDescriptor == INVALID_SOCKET) throw std::runtime_error("无法创建端口检测 socket");
#else
    int const socketDescriptor = socket(AF_INET, SOCK_STREAM, 0);  // 创建一个套接字
    if (socketDescriptor < 0) throw std::runtime_error("无法创建端口检测 socket");
#endif

    struct sockaddr_in serverAddress{};
    serverAddress.sin_family = AF_INET;                 // 地址族为IPv4
    serverAddress.sin_port = htons(port);               // 设置端口号
    serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);  // 使用通配地址

    const bool isOpen =
        bind(socketDescriptor, reinterpret_cast<sockaddr *>(&serverAddress), sizeof(serverAddress)) != 0;

#if defined(_WIN32)
    closesocket(socketDescriptor);  // 关闭套接字（WSACleanup 由 ~WinsockInit 负责）
#else
    close(socketDescriptor);  // 关闭套接字
#endif

    return isOpen;
}
