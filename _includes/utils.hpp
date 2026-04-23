#pragma once

#include <cstdlib>      // std::system
#include <filesystem>   // std::filesystem
#include <fstream>      // std::ifstream
#include <string>       // std::string
#include <string_view>  // std::string_view

#if defined(_WIN32)
#include <winsock2.h>  // Windows Sockets API
#pragma comment(lib, "ws2_32.lib")

// RAII 包装：确保 Winsock 在程序生命周期内只初始化一次
// 避免 isPortOpen 在轮询循环中反复调用 WSAStartup/WSACleanup 的开销
namespace detail {
struct WinsockInit {
    WSADATA data{};
    WinsockInit() { WSAStartup(MAKEWORD(2, 2), &data); }
    ~WinsockInit() { WSACleanup(); }
};
}  // namespace detail

#else
#include <netinet/in.h>  // AF_INET, INADDR_LOOPBACK
#include <sys/wait.h>    // WIFEXITED, WEXITSTATUS
#include <unistd.h>      // close()
#endif

// 根据操作系统的不同指向不同的空设备路径
// 仅重定向 stdout，stderr 保留输出以便调试
#if defined(_WIN32)
inline constexpr std::string_view DEVICE_NULL = " > NUL";
#else
inline constexpr std::string_view DEVICE_NULL = " > /dev/null";
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

// 函数用途：判断给定端口是否已被占用（即能够成功连接）
// 参数：port - 待判断的端口号
// 返回值：若端口已被占用（有进程在监听）则返回 true，否则返回 false
inline bool isPortInUse(int port) {
#if defined(_WIN32)
    // 保证局部 static 的初始化是线程安全的，Winsock 只会初始化一次
    static detail::WinsockInit winsockInitialization;
    SOCKET socketDescriptor = socket(AF_INET, SOCK_STREAM, 0);
    if (socketDescriptor == INVALID_SOCKET) return false;
#else
    int socketDescriptor = socket(AF_INET, SOCK_STREAM, 0);  // 创建一个套接字
    if (socketDescriptor < 0) return false;                  // 若创建套接字失败返回端口未打开
#endif

    struct sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;                      // 地址族为IPv4
    serverAddress.sin_port = htons(port);                    // 设置端口号
    serverAddress.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // 设置为本地回环地址

    bool isOpen = false;  // 初始化端口状态为未打开
    if (connect(socketDescriptor, (struct sockaddr *)&serverAddress, sizeof(serverAddress)) == 0) {
        isOpen = true;  // 连接成功，端口被占用
    }

#if defined(_WIN32)
    closesocket(socketDescriptor);  // 关闭套接字（WSACleanup 由 ~WinsockInit 负责）
#else
    close(socketDescriptor);  // 关闭套接字
#endif

    return isOpen;
}

// 函数用途：跨平台执行系统命令并返回真实的退出码
// 参数：
//   command: 要执行的命令行指令
// 返回值：命令执行完毕后的实际退出状态码
inline int runCommand(const std::string &command) {
    int status = std::system(command.c_str());
#if defined(_WIN32)
    return status;
#else
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

// 函数用途：一次性读取整个文件内容
// 参数：
//   path: 要读取的目标文件路径
// 返回值：包含文件所有内容的 std::string，如果读取失败则返回空字符串
inline std::string readFileContents(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file) return {};
    file.seekg(0, std::ios::end);
    std::streamsize size = file.tellg();
    if (size <= 0) return {};
    std::string content(static_cast<size_t>(size), '\0');
    file.seekg(0, std::ios::beg);
    file.read(content.data(), size);
    return content;
}
