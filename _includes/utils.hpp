#pragma once

#include <chrono>       // std::chrono
#include <filesystem>   // std::filesystem
#include <fstream>      // std::ifstream
#include <format>       // std::format
#include <optional>     // std::optional
#include <string>       // std::string
#include <string_view>  // std::string_view
#include <utility>      // std::swap
#include <thread>       // std::this_thread

#if defined(_WIN32)
#include <winsock2.h>  // Windows Sockets API
#include <windows.h>   // CreateProcessA
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
#include <signal.h>      // kill
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

    const std::string quotedDependency = std::format("\"{}\"", dependencies);
    if (fileContent.find(quotedDependency) != std::string_view::npos) return true;

    const std::string scopeDependency = std::format("/{}@", dependencies);
    if (fileContent.find(scopeDependency) != std::string_view::npos) return true;

    const std::string pnpmDependency = std::format("/{}:", dependencies);
    return fileContent.find(pnpmDependency) != std::string_view::npos;
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

struct ManagedProcess {
#if defined(_WIN32)
    HANDLE processHandle = nullptr;
    HANDLE threadHandle = nullptr;
#else
    pid_t pid = -1;
#endif
    bool finished = false;
    int exitCode = -1;

    ManagedProcess() = default;
    ManagedProcess(const ManagedProcess &) = delete;
    ManagedProcess &operator=(const ManagedProcess &) = delete;

    // 进程句柄禁止拷贝，只允许移动转交所有权。
    ManagedProcess(ManagedProcess &&other) noexcept { swap(other); }
    ManagedProcess &operator=(ManagedProcess &&other) noexcept {
        if (this == &other) return *this;
        cleanup();
        swap(other);
        return *this;
    }

    ~ManagedProcess() { cleanup(); }

    void swap(ManagedProcess &other) noexcept {
#if defined(_WIN32)
        std::swap(processHandle, other.processHandle);
        std::swap(threadHandle, other.threadHandle);
#else
        std::swap(pid, other.pid);
#endif
        std::swap(finished, other.finished);
        std::swap(exitCode, other.exitCode);
    }

    void cleanup() noexcept {
        if (finished) return;
        terminate();
    }

    static std::optional<ManagedProcess> start(const std::string &command) {
        ManagedProcess process;
#if defined(_WIN32)
        // Windows 直接创建子进程，避免经过 shell 的额外状态层。
        STARTUPINFOA startupInfo{};
        startupInfo.cb = sizeof(startupInfo);
        std::string mutableCommand = command;
        PROCESS_INFORMATION processInformation{};
        if (!CreateProcessA(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                            &startupInfo, &processInformation)) {
            return std::nullopt;
        }
        process.processHandle = processInformation.hProcess;
        process.threadHandle = processInformation.hThread;
#else
        // Unix 下仍使用 /bin/sh -c 执行命令，保持与原有命令字符串兼容。
        pid_t childPid = fork();
        if (childPid < 0) return std::nullopt;
        if (childPid == 0) {
            execl("/bin/sh", "sh", "-c", command.c_str(), static_cast<char *>(nullptr));
            _exit(127);
        }
        process.pid = childPid;
#endif
        return process;
    }

    std::optional<int> pollExitCode() {
        if (finished) return exitCode;
#if defined(_WIN32)
        // 非阻塞检查一次进程状态，避免把等待逻辑锁死在系统调用里。
        DWORD status = WaitForSingleObject(processHandle, 0);
        if (status == WAIT_TIMEOUT) return std::nullopt;
        if (status != WAIT_OBJECT_0) {
            finished = true;
            exitCode = -1;
        } else {
            DWORD code = 0;
            GetExitCodeProcess(processHandle, &code);
            finished = true;
            exitCode = static_cast<int>(code);
        }
        CloseHandle(threadHandle);
        CloseHandle(processHandle);
        threadHandle = nullptr;
        processHandle = nullptr;
        return exitCode;
#else
        // WNOHANG 用于轮询子进程，而不是一直阻塞主线程。
        int status = 0;
        pid_t result = waitpid(pid, &status, WNOHANG);
        if (result == 0) return std::nullopt;
        if (result < 0) {
            finished = true;
            exitCode = -1;
            pid = -1;
            return exitCode;
        }
        finished = true;
        exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        pid = -1;
        return exitCode;
#endif
    }

    bool terminate() {
        if (finished) return true;
#if defined(_WIN32)
        // Windows 直接结束子进程，并回收句柄。
        if (processHandle == nullptr) return true;
        TerminateProcess(processHandle, 1);
        WaitForSingleObject(processHandle, INFINITE);
        DWORD code = 1;
        GetExitCodeProcess(processHandle, &code);
        exitCode = static_cast<int>(code);
        finished = true;
        CloseHandle(threadHandle);
        CloseHandle(processHandle);
        threadHandle = nullptr;
        processHandle = nullptr;
        return true;
#else
        // 先温和退出，超时后再强制杀掉，减少残留僵尸进程的概率。
        if (pid <= 0) return true;
        kill(pid, SIGTERM);
        for (int i = 0; i < 50; ++i) {
            if (auto code = pollExitCode(); code.has_value()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        kill(pid, SIGKILL);
        int status = 0;
        waitpid(pid, &status, 0);
        finished = true;
        exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        pid = -1;
        return true;
#endif
    }
};

// 函数用途：跨平台执行系统命令并返回真实的退出码
// 参数：
//   command: 要执行的命令行指令
// 返回值：命令执行完毕后的实际退出状态码
inline int runCommand(const std::string &command) {
    auto process = ManagedProcess::start(command);
    if (!process) return -1;

    while (true) {
        if (auto exitCode = process->pollExitCode()) {
            return *exitCode;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
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
