#pragma once
#include <cstdint>
#include <stdexcept>

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

// 尝试绑定 IPv4 通配地址，绑定失败表示端口不可用。
inline bool isPortInUse(std::uint16_t port) {
    int const socketDescriptor = socket(AF_INET, SOCK_STREAM, 0);
    if (socketDescriptor < 0) throw std::runtime_error("无法创建端口检测 socket");

    struct sockaddr_in serverAddress{};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(port);
    serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);

    const bool isOpen =
        bind(socketDescriptor, reinterpret_cast<sockaddr *>(&serverAddress), sizeof(serverAddress)) != 0;

    close(socketDescriptor);

    return isOpen;
}
