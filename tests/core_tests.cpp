#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <string_view>
#include <thread>

#include "algorithms.hpp"
#include "configs.hpp"
#include "utils.hpp"

namespace {
int failures = 0;

void check(bool condition, std::string_view message) {
    if (condition) return;
    std::cerr << "FAILED: " << message << '\n';
    ++failures;
}
}  // namespace

int main() {
    check(isPrefixOf("bui", "build"), "command prefixes are accepted");
    check(!isPrefixOf("builds", "build"), "longer command is not a prefix");
    check(getJaroWinklerSimilarity("build", "build") == 1.0, "identical commands have full similarity");
    check(getJaroWinklerSimilarity("build", "server") < 0.85, "unrelated commands stay below threshold");

    check(isDependenciesPresent(R"({"dependencies":{"hexo-swpp":"1.0.0"}})", "hexo-swpp"),
          "package.json dependency is detected");
    check(!isDependenciesPresent(R"({"dependencies":{"hexo-swpp-extra":"1.0.0"}})", "hexo-swpp"),
          "dependency names are matched exactly");

    double threshold = 0.0;
    int timeout = 0;
    check(parseNumberFully("0.85", threshold) && threshold == 0.85, "valid floating-point config is parsed");
    check(!parseNumberFully("0.85invalid", threshold), "floating-point config rejects trailing characters");
    check(parseNumberFully("30", timeout) && timeout == 30, "valid integer config is parsed");
    check(!parseNumberFully("30s", timeout), "integer config rejects trailing characters");

#if defined(_WIN32)
    check(joinShellArguments({"--config", "site config.yml"}) == " \"--config\" \"site config.yml\"",
          "Windows pass-through arguments are quoted");
    traceOutputEnabled = false;
    check(commandOutputRedirect() == " > NUL 2> NUL", "Windows normal mode hides child stdout and stderr");
    check(commandForLog("git status > NUL 2> NUL") == "git status", "Windows log output hides quiet redirect");
    traceOutputEnabled = true;
    check(commandOutputRedirect() == " > NUL", "Windows trace mode keeps child stderr visible");
    check(commandForLog("git status > NUL") == "git status > NUL", "Windows trace log shows raw redirect");
#else
    check(joinShellArguments({"--config", "site config.yml", "a'b"}) == " '--config' 'site config.yml' 'a'\\''b'",
          "POSIX pass-through arguments are quoted");
    traceOutputEnabled = false;
    check(commandOutputRedirect() == " > /dev/null 2>&1", "POSIX normal mode hides child stdout and stderr");
    check(commandForLog("git status > /dev/null 2>&1") == "git status", "POSIX log output hides quiet redirect");
    traceOutputEnabled = true;
    check(commandOutputRedirect() == " > /dev/null", "POSIX trace mode keeps child stderr visible");
    check(commandForLog("git status > /dev/null") == "git status > /dev/null", "POSIX trace log shows raw redirect");
#endif
    traceOutputEnabled = false;

#if defined(_WIN32)
    check(runCommand("exit /B 7") == 7, "Windows command exit code is propagated");
#else
    check(runCommand("exit 7") == 7, "POSIX command exit code is propagated");
    check(analyzeCommandOutput("npm ERR! EACCES permission denied") == "权限不足，检查目录权限或以正确用户运行",
          "common command failures are diagnosed");

    const auto logPath =
        std::filesystem::temp_directory_path() /
        std::format("hexo-toolkit-log-test-{}.log", std::chrono::steady_clock::now().time_since_epoch().count());
    commandLogFile = logPath;
    check(runCommand("printf 'stdout failure\\n'; printf 'stderr failure\\n' >&2; exit 9") == 9,
          "command output is captured on failure");
    check(runCommand("printf 'successful output\\n'") == 0, "successful command output is captured silently");
    std::ifstream captured(logPath);
    std::string capturedText((std::istreambuf_iterator<char>(captured)), std::istreambuf_iterator<char>());
    check(capturedText.find("stdout failure") != std::string::npos &&
              capturedText.find("stderr failure") != std::string::npos,
          "command log file keeps stdout and stderr");
    check(capturedText.find("successful output") != std::string::npos, "command log appends output across commands");
    commandLogFile.clear();
    std::error_code logRemoveError;
    std::filesystem::remove(logPath, logRemoveError);

    const auto marker =
        std::filesystem::temp_directory_path() /
        std::format("hexo-toolkit-process-test-{}", std::chrono::steady_clock::now().time_since_epoch().count());
    auto process = ManagedProcess::start(std::format("(sleep 1; touch '{}') & wait", marker.string()));
    check(process.has_value(), "process tree test starts");
    if (process) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        process->terminate();
        std::this_thread::sleep_for(std::chrono::milliseconds(1100));
        check(!std::filesystem::exists(marker), "terminating a command also terminates descendants");
    }
    std::error_code removeError;
    std::filesystem::remove(marker, removeError);
#endif

    if (failures != 0) std::cerr << failures << " test(s) failed\n";
    return failures == 0 ? 0 : 1;
}
