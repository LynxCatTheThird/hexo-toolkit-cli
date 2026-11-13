#pragma once

#include <chrono>            // std::chrono
#include <filesystem>        // std::filesystem
#include <format>            // std::format
#include <fstream>           // std::ifstream
#include <optional>          // std::optional
#include <string>            // std::string
#include <string_view>       // std::string_view
#include <thread>            // std::this_thread
#include <vector>            // std::vector

#include "logs.hpp"

#include <fcntl.h>       // open, O_RDONLY
#include <signal.h>      // kill
#include <sys/wait.h>    // WIFEXITED, WEXITSTATUS
#include <unistd.h>  // close()

#include <cerrno>  // errno, EINTR

inline volatile sig_atomic_t foregroundChildProcessGroupId = -1;
inline volatile sig_atomic_t foregroundTerminalSignal = 0;

inline void forwardTerminalSignalToChildProcessGroup(int signalNumber) {
    if (signalNumber != SIGTSTP) foregroundTerminalSignal = signalNumber;
    const sig_atomic_t processGroupId = foregroundChildProcessGroupId;
    if (processGroupId > 0) kill(-static_cast<pid_t>(processGroupId), signalNumber);
}

inline bool dryRunEnabled = false;
inline std::filesystem::path commandLogFile;

inline std::string quoteShellArgument(std::string_view argument) {
    std::string quoted = "'";
    for (char const character : argument) {
        if (character == '\'')
            quoted += "'\\''";
        else
            quoted.push_back(character);
    }
    quoted.push_back('\'');
    return quoted;
}

inline std::string joinShellArguments(const std::vector<std::string> &arguments) {
    std::string result;
    for (const auto &argument : arguments) {
        result.push_back(' ');
        result += quoteShellArgument(argument);
    }
    return result;
}

class ManagedProcess {
    int outputRead = -1;
    pid_t terminalOwner = -1;
    pid_t pid = -1;
    pid_t processGroupId = -1;
    int terminalSignal = 0;
    // 保存工具进程原来的信号处理方式，命令结束后恢复。
    struct sigaction oldSigInt{};
    struct sigaction oldSigQuit{};
    struct sigaction oldSigTerm{};
    struct sigaction oldSigTstp{};
    bool signalsSaved = false;
    bool finished = false;
    int exitCode = -1;

   public:
    // 每条命令由一个局部对象持有进程和管道。
    ManagedProcess() = default;
    ManagedProcess(const ManagedProcess &) = delete;
    ManagedProcess &operator=(const ManagedProcess &) = delete;

    ~ManagedProcess() {
        terminate();
        if (outputRead >= 0) close(outputRead);
    }

    bool wasInterruptedByTerminalSignal() const noexcept {
        return terminalSignal == SIGINT || terminalSignal == SIGQUIT;
    }

    void captureTerminalSignal() noexcept {
        if (foregroundChildProcessGroupId == processGroupId) {
            terminalSignal = static_cast<int>(foregroundTerminalSignal);
        }
    }

    void restoreSignals() noexcept {
        if (terminalOwner > 0) {
            if (tcgetpgrp(STDIN_FILENO) == processGroupId) setTerminalForeground(terminalOwner);
            terminalOwner = -1;
        }
        if (signalsSaved) {
            sigaction(SIGINT, &oldSigInt, nullptr);
            sigaction(SIGQUIT, &oldSigQuit, nullptr);
            sigaction(SIGTERM, &oldSigTerm, nullptr);
            sigaction(SIGTSTP, &oldSigTstp, nullptr);
            if (foregroundChildProcessGroupId == processGroupId) {
                foregroundChildProcessGroupId = -1;
                foregroundTerminalSignal = 0;
            }
            signalsSaved = false;
        }
    }

    // 切换终端前台进程组时，临时忽略 SIGTTOU。
    static void setTerminalForeground(pid_t group) noexcept {
        struct sigaction ignored{}, previous{};
        ignored.sa_handler = SIG_IGN;
        sigemptyset(&ignored.sa_mask);
        sigaction(SIGTTOU, &ignored, &previous);
        tcsetpgrp(STDIN_FILENO, group);
        sigaction(SIGTTOU, &previous, nullptr);
    }

    // 每轮读取一块输出，让退出状态和信号得到及时处理。
    std::string readOutput() {
        char buffer[16384];
        if (outputRead < 0) return {};
        const auto count = read(outputRead, buffer, sizeof(buffer));
        return count > 0 ? std::string(buffer, static_cast<size_t>(count)) : std::string{};
    }

    bool start(const std::string &command) {
        // 使用 /bin/sh -c 执行命令，兼容配置中的 shell 命令。
        int outputPipe[2];
        if (pipe(outputPipe) < 0) return false;
        outputRead = outputPipe[0];
        fcntl(outputRead, F_SETFL, O_NONBLOCK);
        fcntl(outputRead, F_SETFD, FD_CLOEXEC);
        sigset_t terminalSignals{};
        sigset_t oldSignalMask{};
        sigemptyset(&terminalSignals);
        sigaddset(&terminalSignals, SIGINT);
        sigaddset(&terminalSignals, SIGQUIT);
        sigaddset(&terminalSignals, SIGTERM);
        sigaddset(&terminalSignals, SIGTSTP);
        // fork 与信号处理器安装期间，临时阻塞终端信号。
        sigprocmask(SIG_BLOCK, &terminalSignals, &oldSignalMask);
        const bool ownsTerminal = isatty(STDIN_FILENO) && tcgetpgrp(STDIN_FILENO) == getpgrp();
        pid_t const childPid = fork();
        if (childPid < 0) {
            close(outputPipe[1]);
            sigprocmask(SIG_SETMASK, &oldSignalMask, nullptr);
            return false;
        }
        if (childPid == 0) {
            // 独立进程组用于向整组子进程转发信号。
            setpgid(0, 0);
            close(outputPipe[0]);
            dup2(outputPipe[1], STDOUT_FILENO);
            dup2(outputPipe[1], STDERR_FILENO);
            if (outputPipe[1] != STDOUT_FILENO && outputPipe[1] != STDERR_FILENO) close(outputPipe[1]);
            if (ownsTerminal) setTerminalForeground(getpgrp());
            sigprocmask(SIG_SETMASK, &oldSignalMask, nullptr);
            execl("/bin/sh", "sh", "-c", command.c_str(), static_cast<char *>(nullptr));
            _exit(127);
        }
        close(outputPipe[1]);
        pid = childPid;
        processGroupId = childPid;
        // 父子两侧都设置进程组，覆盖 fork 后的调度顺序。
        setpgid(childPid, childPid);
        if (ownsTerminal) {
            terminalOwner = getpgrp();
            setTerminalForeground(childPid);
        }

        // 转发发给工具的信号；终端信号直接送往前台子进程组。
        struct sigaction forwardAction{};
        forwardAction.sa_handler = forwardTerminalSignalToChildProcessGroup;
        sigemptyset(&forwardAction.sa_mask);
        forwardAction.sa_flags = SA_RESTART;
        foregroundChildProcessGroupId = processGroupId;
        foregroundTerminalSignal = 0;
        sigaction(SIGINT, &forwardAction, &oldSigInt);
        sigaction(SIGQUIT, &forwardAction, &oldSigQuit);
        sigaction(SIGTERM, &forwardAction, &oldSigTerm);
        sigaction(SIGTSTP, &forwardAction, &oldSigTstp);
        signalsSaved = true;
        sigprocmask(SIG_SETMASK, &oldSignalMask, nullptr);
        return true;
    }

    std::optional<int> pollExitCode(bool suspendWithChild = true) {
        if (finished) return exitCode;
        // fg 恢复后，将终端前台重新交给子进程组。
        if (terminalOwner > 0 && tcgetpgrp(STDIN_FILENO) == terminalOwner) setTerminalForeground(processGroupId);
        // 轮询退出和暂停状态，配合输出读取。
        int status = 0;
        pid_t const result = waitpid(pid, &status, WNOHANG | WUNTRACED);
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
        if (WIFSTOPPED(status)) {
            if (suspendWithChild && foregroundTerminalSignal != SIGTERM) {
                // shell 跟踪工具进程，因此子进程暂停时工具也一同暂停。
                kill(-processGroupId, SIGSTOP);
                if (terminalOwner > 0 && tcgetpgrp(STDIN_FILENO) == processGroupId)
                    setTerminalForeground(terminalOwner);
                raise(SIGSTOP);
                // fg 恢复时接管终端，bg 恢复时保持后台运行。
                if (terminalOwner > 0 && tcgetpgrp(STDIN_FILENO) == terminalOwner)
                    setTerminalForeground(processGroupId);
            }
            kill(-processGroupId, SIGCONT);
            return std::nullopt;
        }
        finished = true;
        exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : (WIFSIGNALED(status) ? 128 + WTERMSIG(status) : -1);
        pid = -1;
        captureTerminalSignal();
        restoreSignals();
        if (kill(-processGroupId, 0) < 0 && errno == ESRCH) processGroupId = -1;
        return exitCode;
    }

    void terminate() {
        // 先发送 SIGTERM，等待最多 5 秒后用 SIGKILL 结束进程组。
        if (processGroupId <= 0) return;
        const pid_t groupId = processGroupId;
        kill(-groupId, SIGTERM);
        kill(-groupId, SIGCONT);
        for (int i = 0; i < 50; ++i) {
            if (!finished) pollExitCode(false);
            if (kill(-groupId, 0) < 0 && errno == ESRCH) {
                processGroupId = -1;
                return;
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
    }
};

// 流式写入日志，内存中保留约 8 KB 的输出尾部供失败时展示。
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
    ManagedProcess process;
    if (!process.start(command)) {
        logError("无法启动命令：{}", command);
        return 1;
    }
    const bool showOutput = isatty(STDIN_FILENO) != 0 || (liveOutput && spdlog::should_log(spdlog::level::info)) ||
                            spdlog::should_log(spdlog::level::debug);
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
        auto chunk = process.readOutput();
        if (!chunk.empty()) consume(chunk);
        if (auto code = process.pollExitCode()) {
            result = *code;
            // 主进程退出后，限量读取管道中的剩余输出。
            for (int drains = 0; drains < 64 && !(chunk = process.readOutput()).empty(); ++drains) consume(chunk);
            break;
        }
        if (chunk.empty()) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    // server 将终端中断视为正常停止。
    // TODO: 构建/部署遇到中断时，应停止后续步骤，即使子进程返回 0。
    if (interruptIsSuccess && (process.wasInterruptedByTerminalSignal() || result == 130 || result == 131)) result = 0;
    if (result != 0 && !showOutput && !tail.empty()) logError("子进程输出：\n{}", tail);
    return result == 0 && logFailed ? 1 : result;
}
