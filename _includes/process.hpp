#pragma once

#include <chrono>            // std::chrono
#include <exception>         // std::exception
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
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>  // Windows Sockets API
#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#endif

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
#include <termios.h>
#include <unistd.h>  // close()

#include <cerrno>  // errno, EINTR
#endif

#if defined(_WIN32)
inline volatile LONG foregroundConsoleInterrupted = 0;
inline BOOL WINAPI handleConsoleInterrupt(DWORD event) {
    if (event != CTRL_C_EVENT && event != CTRL_BREAK_EVENT) return FALSE;
    InterlockedExchange(&foregroundConsoleInterrupted, 1);
    return TRUE;
}
#else
inline volatile sig_atomic_t foregroundChildProcessGroupId = -1;
inline volatile sig_atomic_t foregroundTerminalSignal = 0;

inline void forwardTerminalSignalToChildProcessGroup(int signalNumber) {
    foregroundTerminalSignal = signalNumber;
    const sig_atomic_t processGroupId = foregroundChildProcessGroupId;
    if (processGroupId > 0) kill(-static_cast<pid_t>(processGroupId), signalNumber);
}
#endif

inline bool dryRunEnabled = false;
inline std::filesystem::path commandLogFile;

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
    for (char const character : argument) {
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

inline bool hasUnsafeShellArguments(const std::vector<std::string> &arguments) {
#if defined(_WIN32)
    for (const auto &argument : arguments) {
        if (argument.find_first_of("\"%&|<>()^!\r\n") != std::string::npos) return true;
    }
#else
    static_cast<void>(arguments);
#endif
    return false;
}

inline std::string analyzeCommandOutput(std::string_view output) {
    auto const containsAny = [output](std::initializer_list<std::string_view> terms) {
        for (auto const term : terms) {
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

class ManagedProcess {
#if defined(_WIN32)
    HANDLE processHandle = nullptr;
    HANDLE threadHandle = nullptr;
    HANDLE jobHandle = nullptr;
    HANDLE outputRead = nullptr;
    bool consoleHandlerInstalled = false;
    bool consoleInterrupted = false;
#else
    int outputRead = -1;
    pid_t terminalOwner = -1;
    pid_t pid = -1;
    pid_t processGroupId = -1;
    int terminalSignal = 0;
    // 子进程使用独立进程组，父进程临时接管终端信号并转发给整个子进程树。
    struct sigaction oldSigInt{};
    struct sigaction oldSigQuit{};
    struct sigaction oldSigTerm{};
    bool signalsSaved = false;
#endif
    bool finished = false;
    int exitCode = -1;

   public:
    // pid 初始为 -1，terminate() 会提前检查 pid <= 0 而跳过，移动后的空对象不会误杀进程
    ManagedProcess() = default;
    ManagedProcess(const ManagedProcess &) = delete;
    ManagedProcess &operator=(const ManagedProcess &) = delete;

    // 进程句柄禁止拷贝，只允许移动转交所有权。
    ManagedProcess(ManagedProcess &&other) noexcept { swap(other); }
    ManagedProcess &operator=(ManagedProcess &&other) noexcept {
        if (this == &other) return *this;
        cleanup();
#if defined(_WIN32)
        if (outputRead) CloseHandle(outputRead);
        outputRead = nullptr;
#else
        if (outputRead >= 0) close(outputRead);
        outputRead = -1;
#endif
        swap(other);
        return *this;
    }

    ~ManagedProcess() {
        cleanup();
#if defined(_WIN32)
        if (outputRead) CloseHandle(outputRead);
#else
        if (outputRead >= 0) close(outputRead);
#endif
    }

    void swap(ManagedProcess &other) noexcept {
#if defined(_WIN32)
        std::swap(processHandle, other.processHandle);
        std::swap(threadHandle, other.threadHandle);
        std::swap(jobHandle, other.jobHandle);
        std::swap(consoleHandlerInstalled, other.consoleHandlerInstalled);
        std::swap(consoleInterrupted, other.consoleInterrupted);
        std::swap(outputRead, other.outputRead);
#else
        std::swap(outputRead, other.outputRead);
        std::swap(terminalOwner, other.terminalOwner);
        std::swap(pid, other.pid);
        std::swap(processGroupId, other.processGroupId);
        std::swap(terminalSignal, other.terminalSignal);
        std::swap(oldSigInt, other.oldSigInt);
        std::swap(oldSigQuit, other.oldSigQuit);
        std::swap(oldSigTerm, other.oldSigTerm);
        std::swap(signalsSaved, other.signalsSaved);
#endif
        std::swap(finished, other.finished);
        std::swap(exitCode, other.exitCode);
    }

    bool wasInterruptedByTerminalSignal() const noexcept {
#if defined(_WIN32)
        return consoleInterrupted;
#else
        return terminalSignal == SIGINT || terminalSignal == SIGQUIT;
#endif
    }

    void captureTerminalSignal() noexcept {
#if !defined(_WIN32)
        if (foregroundChildProcessGroupId == processGroupId) {
            terminalSignal = static_cast<int>(foregroundTerminalSignal);
        }
#endif
    }

    void restoreSignals() noexcept {
#if defined(_WIN32)
        if (consoleHandlerInstalled) {
            consoleInterrupted = InterlockedExchange(&foregroundConsoleInterrupted, 0) != 0;
            SetConsoleCtrlHandler(handleConsoleInterrupt, FALSE);
            consoleHandlerInstalled = false;
        }
#else
        if (terminalOwner > 0) {
            setTerminalForeground(terminalOwner);
            terminalOwner = -1;
        }
        if (signalsSaved) {
            sigaction(SIGINT, &oldSigInt, nullptr);
            sigaction(SIGQUIT, &oldSigQuit, nullptr);
            sigaction(SIGTERM, &oldSigTerm, nullptr);
            if (foregroundChildProcessGroupId == processGroupId) {
                foregroundChildProcessGroupId = -1;
                foregroundTerminalSignal = 0;
            }
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

    // The parent temporarily yields the terminal so interactive child tools can read stdin.
#if !defined(_WIN32)
    static void setTerminalForeground(pid_t group) noexcept {
        struct sigaction ignored{}, previous{};
        ignored.sa_handler = SIG_IGN;
        sigemptyset(&ignored.sa_mask);
        sigaction(SIGTTOU, &ignored, &previous);
        tcsetpgrp(STDIN_FILENO, group);
        sigaction(SIGTTOU, &previous, nullptr);
    }
#endif

    // Read at most one chunk per iteration so a busy child cannot starve signal/exit handling.
    std::string readOutput() {
        char buffer[16384];
#if defined(_WIN32)
        if (!outputRead) return {};
        DWORD available = 0, count = 0;
        if (!PeekNamedPipe(outputRead, nullptr, 0, nullptr, &available, nullptr) || available == 0) return {};
        if (!ReadFile(outputRead, buffer, sizeof(buffer), &count, nullptr)) return {};
        return std::string(buffer, count);
#else
        if (outputRead < 0) return {};
        const auto count = read(outputRead, buffer, sizeof(buffer));
        return count > 0 ? std::string(buffer, static_cast<size_t>(count)) : std::string{};
#endif
    }

    static std::optional<ManagedProcess> start(const std::string &command, bool captureOutput = false) {
        ManagedProcess process;
#if defined(_WIN32)
        // 现有命令包含重定向和 &&，需要由 cmd.exe 解释。Job Object 确保子孙进程一并回收。
        STARTUPINFOA startupInfo{};
        startupInfo.cb = sizeof(startupInfo);
        HANDLE outputWrite = nullptr;
        HANDLE inputCopy = nullptr;
        if (captureOutput) {
            SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
            if (!CreatePipe(&process.outputRead, &outputWrite, &attributes, 0)) return std::nullopt;
            SetHandleInformation(process.outputRead, HANDLE_FLAG_INHERIT, 0);
            const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
            if (input != nullptr && input != INVALID_HANDLE_VALUE)
                DuplicateHandle(GetCurrentProcess(), input, GetCurrentProcess(), &inputCopy, 0, TRUE,
                                DUPLICATE_SAME_ACCESS);
            startupInfo.dwFlags |= STARTF_USESTDHANDLES;
            startupInfo.hStdOutput = outputWrite;
            startupInfo.hStdError = outputWrite;
            startupInfo.hStdInput = inputCopy;
        }
        std::string mutableCommand = std::format("cmd.exe /D /S /C \"{}\"", command);
        PROCESS_INFORMATION processInformation{};
        const BOOL created = CreateProcessA(nullptr, mutableCommand.data(), nullptr, nullptr, captureOutput,
                                            CREATE_SUSPENDED, nullptr, nullptr, &startupInfo, &processInformation);
        if (outputWrite) CloseHandle(outputWrite);
        if (inputCopy) CloseHandle(inputCopy);
        if (!created) return std::nullopt;

        InterlockedExchange(&foregroundConsoleInterrupted, 0);
        process.consoleHandlerInstalled = SetConsoleCtrlHandler(handleConsoleInterrupt, TRUE) != FALSE;
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
            process.restoreSignals();
            return std::nullopt;
        }
        process.processHandle = processInformation.hProcess;
        process.threadHandle = processInformation.hThread;
        process.jobHandle = job;
#else
        // Unix 下仍使用 /bin/sh -c 执行命令，保持与原有命令字符串兼容。
        int outputPipe[2] = {-1, -1};
        if (captureOutput) {
            if (pipe(outputPipe) < 0) return std::nullopt;
            process.outputRead = outputPipe[0];
            fcntl(outputPipe[0], F_SETFL, O_NONBLOCK);
            fcntl(outputPipe[0], F_SETFD, FD_CLOEXEC);
        }
        sigset_t terminalSignals{};
        sigset_t oldSignalMask{};
        sigemptyset(&terminalSignals);
        sigaddset(&terminalSignals, SIGINT);
        sigaddset(&terminalSignals, SIGQUIT);
        sigaddset(&terminalSignals, SIGTERM);
        // 阻塞安装窗口内的终端信号，避免 Ctrl+C 落在 fork() 与转发处理器安装之间。
        sigprocmask(SIG_BLOCK, &terminalSignals, &oldSignalMask);
        const bool ownsTerminal = isatty(STDIN_FILENO) && tcgetpgrp(STDIN_FILENO) == getpgrp();
        pid_t const childPid = fork();
        if (childPid < 0) {
            if (outputPipe[1] >= 0) close(outputPipe[1]);
            sigprocmask(SIG_SETMASK, &oldSignalMask, nullptr);
            return std::nullopt;
        }
        if (childPid == 0) {
            // 独立进程组允许父进程在超时或退出时终止整棵命令进程树。
            setpgid(0, 0);
            if (captureOutput) {
                close(outputPipe[0]);
                dup2(outputPipe[1], STDOUT_FILENO);
                dup2(outputPipe[1], STDERR_FILENO);
                if (outputPipe[1] != STDOUT_FILENO && outputPipe[1] != STDERR_FILENO) close(outputPipe[1]);
            }
            if (ownsTerminal) setTerminalForeground(getpgrp());
            sigprocmask(SIG_SETMASK, &oldSignalMask, nullptr);
            execl("/bin/sh", "sh", "-c", command.c_str(), static_cast<char *>(nullptr));
            _exit(127);
        }
        if (outputPipe[1] >= 0) close(outputPipe[1]);
        process.pid = childPid;
        process.processGroupId = childPid;
        // 与子进程中的 setpgid 配合消除调度竞态；子进程已 exec 时的失败可忽略。
        setpgid(childPid, childPid);
        if (ownsTerminal) {
            process.terminalOwner = getpgrp();
            setTerminalForeground(childPid);
        }

        // Forward signals sent to the wrapper; terminal-generated signals go directly to the foreground group.
        struct sigaction forwardAction{};
        forwardAction.sa_handler = forwardTerminalSignalToChildProcessGroup;
        sigemptyset(&forwardAction.sa_mask);
        forwardAction.sa_flags = SA_RESTART;
        foregroundChildProcessGroupId = process.processGroupId;
        foregroundTerminalSignal = 0;
        sigaction(SIGINT, &forwardAction, &process.oldSigInt);
        sigaction(SIGQUIT, &forwardAction, &process.oldSigQuit);
        sigaction(SIGTERM, &forwardAction, &process.oldSigTerm);
        process.signalsSaved = true;
        sigprocmask(SIG_SETMASK, &oldSignalMask, nullptr);
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
        restoreSignals();
        return exitCode;
#else
        // WNOHANG 用于轮询子进程，而不是一直阻塞主线程。
        int status = 0;
        pid_t const result = waitpid(pid, &status, WNOHANG);
        if (result == 0) return std::nullopt;
        if (result < 0) {
            if (errno == EINTR) return std::nullopt;
            finished = true;
            exitCode = -1;
            pid = -1;
            captureTerminalSignal();
            restoreSignals();
            return exitCode;
        }
        finished = true;
        exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : (WIFSIGNALED(status) ? 128 + WTERMSIG(status) : -1);
        pid = -1;
        captureTerminalSignal();
        restoreSignals();
        if (kill(-processGroupId, 0) < 0 && errno == ESRCH) processGroupId = -1;
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
        restoreSignals();
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
            if (waitResult > 0) {
                exitCode =
                    WIFEXITED(status) ? WEXITSTATUS(status) : (WIFSIGNALED(status) ? 128 + WTERMSIG(status) : -1);
            }
            pid = -1;
        }
        finished = true;
        restoreSignals();
        processGroupId = -1;
        return true;
#endif
    }
};

// Commands share one bounded output path: stream to the requested log and keep only an error tail.
inline int runCommand(const std::string &command, bool liveOutput = false, bool interruptIsSuccess = false) {
    if (dryRunEnabled) {
        spdlog::info("  $ {}", command);
        return 0;
    }
    std::ofstream log;
    if (!commandLogFile.empty()) {
        log.open(commandLogFile, std::ios::out | std::ios::app | std::ios::binary);
        if (!log) {
            logError("无法写入日志文件：{}", commandLogFile.string());
            return 1;
        }
        log << "\n===== " << command << " =====\n";
        log.flush();
        if (!log) {
            logError("无法写入日志文件：{}", commandLogFile.string());
            return 1;
        }
    }
    logDetail("$ {}", command);
    auto process = ManagedProcess::start(command, true);
    if (!process) {
        logError("无法启动命令：{}", command);
        return 1;
    }
    const bool showOutput =
        (liveOutput && spdlog::should_log(spdlog::level::info)) || spdlog::should_log(spdlog::level::debug);
    std::string tail;
    bool logFailed = false;
    auto const consume = [&](const std::string &chunk) {
        if (log.is_open() && !logFailed) {
            log.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
            log.flush();
            if (!log) {
                logFailed = true;
                logError("写入日志失败：{}", commandLogFile.string());
            }
        }
        if (showOutput) {
            std::fwrite(chunk.data(), 1, chunk.size(), stderr);
            std::fflush(stderr);
        }
        tail += chunk;
        if (tail.size() > 8000) tail.erase(0, tail.size() - 8000);
    };
    int result;
    for (;;) {
        auto chunk = process->readOutput();
        if (!chunk.empty()) consume(chunk);
        if (auto code = process->pollExitCode()) {
            result = *code;
            // The leader has exited; drain already buffered output without waiting for detached descendants.
            for (int drains = 0; drains < 64 && !(chunk = process->readOutput()).empty(); ++drains) consume(chunk);
            break;
        }
        if (chunk.empty()) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (interruptIsSuccess && (process->wasInterruptedByTerminalSignal() || result == 130 || result == 131)) result = 0;
    if (result != 0 && !tail.empty()) {
        if (!showOutput) logError("子进程输出：\n{}", tail);
        if (auto diagnosis = analyzeCommandOutput(tail); !diagnosis.empty()) logError("可能原因：{}", diagnosis);
    }
    return result == 0 && logFailed ? 1 : result;
}
