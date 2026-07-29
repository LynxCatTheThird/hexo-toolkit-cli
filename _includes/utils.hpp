#pragma once

#include <cctype>            // std::tolower
#include <chrono>            // std::chrono
#include <filesystem>        // std::filesystem
#include <format>            // std::format
#include <fstream>           // std::ifstream
#include <initializer_list>  // std::initializer_list
#include <optional>          // std::optional
#include <string>            // std::string
#include <string_view>       // std::string_view
#include <thread>            // std::this_thread
#include <utility>           // std::swap
#include <vector>            // std::vector

#include "logs.hpp"

#if defined(_WIN32)
#include <windows.h>   // CreateProcessA
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
#include <fcntl.h>       // open, O_RDONLY
#include <netinet/in.h>  // AF_INET, INADDR_LOOPBACK
#include <signal.h>      // kill
#include <sys/wait.h>    // WIFEXITED, WEXITSTATUS
#include <unistd.h>      // close()

#include <cerrno>  // errno, EINTR
#endif

inline bool dryRunEnabled = false;
inline bool traceOutputEnabled = false;
inline std::filesystem::path commandLogFile;

inline std::string_view commandOutputRedirect() {
#if defined(_WIN32)
    return traceOutputEnabled ? " > NUL" : " > NUL 2> NUL";
#else
    return traceOutputEnabled ? " > /dev/null" : " > /dev/null 2>&1";
#endif
}

inline std::string failureTraceHint() { return traceOutputEnabled ? std::string{} : "。使用 -vv 查看原始输出"; }

inline std::string commandForLog(std::string_view command) {
    const std::string_view redirect = commandOutputRedirect();
    if (!traceOutputEnabled && command.ends_with(redirect)) {
        command.remove_suffix(redirect.size());
    }
    return std::string(command);
}

inline std::string quoteShellArgument(std::string_view argument) {
#if defined(_WIN32)
    std::string quoted = "\"";
    size_t backslashCount = 0;
    for (char character : argument) {
        if (character == '\\') {
            ++backslashCount;
            continue;
        }
        if (character == '"') {
            quoted.append(backslashCount * 2 + 1, '\\');
            quoted.push_back('"');
            backslashCount = 0;
            continue;
        }
        quoted.append(backslashCount, '\\');
        backslashCount = 0;
        quoted.push_back(character);
    }
    quoted.append(backslashCount * 2, '\\');
    quoted.push_back('"');
    return quoted;
#else
    std::string quoted = "'";
    for (char character : argument) {
        if (character == '\'')
            quoted += "'\\''";
        else
            quoted.push_back(character);
    }
    quoted.push_back('\'');
    return quoted;
#endif
}

inline std::string joinShellArguments(const std::vector<std::string> &arguments) {
    std::string result;
    for (const auto &argument : arguments) {
        result.push_back(' ');
        result += quoteShellArgument(argument);
    }
    return result;
}

inline std::string analyzeCommandOutput(std::string_view output) {
    auto containsAny = [output](std::initializer_list<std::string_view> terms) {
        for (auto term : terms) {
            if (output.find(term) != std::string_view::npos) return true;
        }
        return false;
    };
    if (containsAny({"EACCES", "permission denied", "权限不足"})) return "权限不足，检查目录权限或以正确用户运行";
    if (containsAny({"ENOENT", "not found", "找不到", "不存在"}))
        return "找不到命令或文件，检查依赖是否安装及 PATH 配置";
    if (containsAny({"ECONNREFUSED", "ETIMEDOUT", "network", "网络", "registry"})) return "网络或 registry 连接失败";
    if (containsAny({"Cannot find module", "MODULE_NOT_FOUND", "模块"})) return "Node.js 模块缺失，尝试重新安装依赖";
    if (containsAny({"YAMLException", "Invalid config", "配置文件"})) return "配置文件格式或内容可能有误";
    if (containsAny({"authentication", "401", "403", "unauthorized", "认证"}))
        return "认证失败，检查 token、账号或仓库权限";
    return {};
}

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
    HANDLE jobHandle = nullptr;
#else
    pid_t pid = -1;
    pid_t processGroupId = -1;
    // 与 std::system() 行为一致：子进程运行期间父进程忽略 SIGINT/SIGQUIT，
    // 让 Ctrl+C 仅由子进程处理，父进程在子进程退出后正常清理终端状态。
    struct sigaction oldSigInt{};
    struct sigaction oldSigQuit{};
    bool signalsSaved = false;
#endif
    bool finished = false;
    int exitCode = -1;

    // pid 初始为 -1，terminate() 会提前检查 pid <= 0 而跳过，移动后的空对象不会误杀进程
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
        std::swap(jobHandle, other.jobHandle);
#else
        std::swap(pid, other.pid);
        std::swap(processGroupId, other.processGroupId);
        std::swap(oldSigInt, other.oldSigInt);
        std::swap(oldSigQuit, other.oldSigQuit);
        std::swap(signalsSaved, other.signalsSaved);
#endif
        std::swap(finished, other.finished);
        std::swap(exitCode, other.exitCode);
    }

    void restoreSignals() noexcept {
#if !defined(_WIN32)
        if (signalsSaved) {
            sigaction(SIGINT, &oldSigInt, nullptr);
            sigaction(SIGQUIT, &oldSigQuit, nullptr);
            signalsSaved = false;
        }
#endif
    }

    void cleanup() noexcept {
#if defined(_WIN32)
        if (finished) return;
#else
        if (finished && processGroupId <= 0) return;
#endif
        terminate();
    }

    static std::optional<ManagedProcess> start(const std::string &command) {
        ManagedProcess process;
#if defined(_WIN32)
        // 现有命令包含重定向和 &&，需要由 cmd.exe 解释。Job Object 确保子孙进程一并回收。
        STARTUPINFOA startupInfo{};
        startupInfo.cb = sizeof(startupInfo);
        std::string mutableCommand = std::format("cmd.exe /D /S /C \"{}\"", command);
        PROCESS_INFORMATION processInformation{};
        if (!CreateProcessA(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE,
                            CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &startupInfo, &processInformation)) {
            return std::nullopt;
        }

        HANDLE job = CreateJobObjectA(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobInformation{};
        jobInformation.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (job == nullptr ||
            !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &jobInformation, sizeof(jobInformation)) ||
            !AssignProcessToJobObject(job, processInformation.hProcess) ||
            ResumeThread(processInformation.hThread) == static_cast<DWORD>(-1)) {
            TerminateProcess(processInformation.hProcess, 1);
            CloseHandle(processInformation.hThread);
            CloseHandle(processInformation.hProcess);
            if (job != nullptr) CloseHandle(job);
            return std::nullopt;
        }
        process.processHandle = processInformation.hProcess;
        process.threadHandle = processInformation.hThread;
        process.jobHandle = job;
#else
        // Unix 下仍使用 /bin/sh -c 执行命令，保持与原有命令字符串兼容。
        pid_t childPid = fork();
        if (childPid < 0) return std::nullopt;
        if (childPid == 0) {
            // 独立进程组允许父进程在超时或退出时终止整棵命令进程树。
            setpgid(0, 0);
            // 将子进程 stdin 重定向到 /dev/null，避免 Node.js 工具链的 readline 交互冲突。
            int devNull = open("/dev/null", O_RDONLY);
            if (devNull >= 0) {
                if (devNull != STDIN_FILENO) {
                    dup2(devNull, STDIN_FILENO);
                    close(devNull);
                }
            }
            // 标记为 CI 环境，使 pnpm/npm 等跳过 ConfirmPrompt 而非因 EOF 报错退出。
            setenv("CI", "true", 0);
            execl("/bin/sh", "sh", "-c", command.c_str(), static_cast<char *>(nullptr));
            _exit(127);
        }
        process.pid = childPid;
        process.processGroupId = childPid;
        // 与子进程中的 setpgid 配合消除调度竞态；子进程已 exec 时的失败可忽略。
        setpgid(childPid, childPid);

        // 父进程：保存并忽略 SIGINT/SIGQUIT，复刻 std::system() 的 POSIX 行为。
        struct sigaction ignoreAction{};
        ignoreAction.sa_handler = SIG_IGN;
        sigemptyset(&ignoreAction.sa_mask);
        sigaction(SIGINT, &ignoreAction, &process.oldSigInt);
        sigaction(SIGQUIT, &ignoreAction, &process.oldSigQuit);
        process.signalsSaved = true;
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
        CloseHandle(jobHandle);
        threadHandle = nullptr;
        processHandle = nullptr;
        jobHandle = nullptr;
        return exitCode;
#else
        // WNOHANG 用于轮询子进程，而不是一直阻塞主线程。
        int status = 0;
        pid_t result = waitpid(pid, &status, WNOHANG);
        if (result == 0) return std::nullopt;
        if (result < 0) {
            if (errno == EINTR) return std::nullopt;
            finished = true;
            exitCode = -1;
            pid = -1;
            restoreSignals();
            return exitCode;
        }
        finished = true;
        exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        pid = -1;
        if (kill(-processGroupId, 0) < 0 && errno == ESRCH) processGroupId = -1;
        restoreSignals();
        return exitCode;
#endif
    }

    bool terminate() {
#if defined(_WIN32)
        if (finished) return true;
        // Job Object 会结束 cmd.exe 以及它启动的 Node/Hexo 子孙进程。
        if (processHandle == nullptr) return true;
        if (jobHandle != nullptr)
            TerminateJobObject(jobHandle, 1);
        else
            TerminateProcess(processHandle, 1);
        WaitForSingleObject(processHandle, INFINITE);
        DWORD code = 1;
        GetExitCodeProcess(processHandle, &code);
        exitCode = static_cast<int>(code);
        finished = true;
        CloseHandle(threadHandle);
        CloseHandle(processHandle);
        if (jobHandle != nullptr) CloseHandle(jobHandle);
        threadHandle = nullptr;
        processHandle = nullptr;
        jobHandle = nullptr;
        return true;
#else
        // 先温和退出，超时后再强制杀掉，减少残留僵尸进程的概率。
        if (processGroupId <= 0) return true;
        const pid_t groupId = processGroupId;
        kill(-groupId, SIGTERM);
        for (int i = 0; i < 50; ++i) {
            if (!finished) pollExitCode();
            if (kill(-groupId, 0) < 0 && errno == ESRCH) {
                processGroupId = -1;
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        kill(-groupId, SIGKILL);
        if (pid > 0) {
            int status = 0;
            pid_t waitResult;
            do {
                waitResult = waitpid(pid, &status, 0);
            } while (waitResult < 0 && errno == EINTR);
            if (waitResult > 0) exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            pid = -1;
        }
        finished = true;
        processGroupId = -1;
        restoreSignals();
        return true;
#endif
    }
};

// 函数用途：跨平台执行系统命令并返回真实的退出码
// 参数：
//   command: 要执行的命令行指令
// 返回值：命令执行完毕后的实际退出状态码
inline std::string readFileContents(const std::filesystem::path &path);

inline int runCommand(const std::string &command) {
    if (dryRunEnabled) {
        spdlog::info("  $ {}", commandForLog(command));
        return 0;
    }
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto capturePath = std::filesystem::temp_directory_path() / std::format("hexo-toolkit-command-{}.log", stamp);
    std::string executableCommand = command;
    const std::string_view redirect = commandOutputRedirect();
    if (executableCommand.ends_with(redirect)) executableCommand.resize(executableCommand.size() - redirect.size());
    executableCommand = "( " + executableCommand + " ) > " + quoteShellArgument(capturePath.string()) + " 2>&1";
    logDetail("$ {}", commandForLog(command));
    auto process = ManagedProcess::start(executableCommand);
    if (!process) return -1;

    int result = -1;
    while (true) {
        if (auto exitCode = process->pollExitCode()) {
            result = *exitCode;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    std::string output = readFileContents(capturePath);
    if (!commandLogFile.empty()) {
        std::ifstream source(capturePath, std::ios::in | std::ios::binary);
        std::ofstream destination(commandLogFile, std::ios::out | std::ios::app | std::ios::binary);
        if (source && destination) {
            destination << "\n===== " << commandForLog(command) << " =====\n";
            destination << source.rdbuf();
        } else {
            spdlog::error("无法写入日志文件：{}", commandLogFile.string());
        }
    }
    std::error_code removeError;
    std::filesystem::remove(capturePath, removeError);
    if (result != 0 && !output.empty()) {
        if (auto diagnosis = analyzeCommandOutput(output); !diagnosis.empty()) {
            logError("可能原因：{}", diagnosis);
        } else {
            logError("子进程输出：\n{}", output.substr(output.size() > 8000 ? output.size() - 8000 : 0));
        }
    }
    return result;
}

// 函数用途：一次性读取整个文件内容
// 参数：
//   path: 要读取的目标文件路径
// 返回值：包含文件所有内容的 std::string，如果读取失败则返回空字符串
inline std::string readFileContents(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file) {
        logTrace("无法打开文件：{}", path.string());
        return {};
    }
    file.seekg(0, std::ios::end);
    std::streamsize size = file.tellg();
    if (size <= 0) return {};
    std::string content(static_cast<size_t>(size), '\0');
    file.seekg(0, std::ios::beg);
    file.read(content.data(), size);
    return content;
}
