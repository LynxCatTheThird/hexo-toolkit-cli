#if !defined(_WIN32)
#include <signal.h>
#include <unistd.h>
#endif

#include <chrono>
#include <cmath>
#include <csignal>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string_view>
#include <system_error>
#include <thread>

#include "algorithms.hpp"
#include "commands.hpp"
#include "configs.hpp"
#include "dependencies.hpp"
#include "logs.hpp"
#include "process.hpp"

namespace {
int failures = 0;

void check(bool condition, std::string_view message) {
    if (condition) return;
    std::cerr << "FAILED: " << message << '\n';
    ++failures;
}
}  // namespace

static int runTests() {
    check(isPrefixOf("bui", "build"), "command prefixes are accepted");
    check(!isPrefixOf("builds", "build"), "longer command is not a prefix");
    check(getJaroWinklerSimilarity("build", "build") == 1.0, "identical commands have full similarity");
    check(getJaroWinklerSimilarity("build", "server") < 0.85, "unrelated commands stay below threshold");
    check(std::abs(getJaroWinklerSimilarity("MARTHA", "MARHTA") - 0.9611111111111111) < 1e-12,
          "standard transposition example has the expected similarity");
    check(std::abs(getJaroWinklerSimilarity("abcdef", "bcadef") - 11.0 / 12.0) < 1e-12,
          "odd mismatches preserve fractional transpositions");

    check(isDependenciesPresent(R"({"dependencies":{"hexo-swpp":"1.0.0"}})", "hexo-swpp"),
          "package.json dependency is detected");
    check(isDependenciesPresent(R"({"devDependencies":{"hexo-swpp":"1.0.0"}})", "hexo-swpp"),
          "package.json development dependency is detected");
    check(!isDependenciesPresent(R"({"scripts":{"hexo-swpp":"hexo swpp"}})", "hexo-swpp"),
          "package.json script names are not treated as dependencies");
    check(!isDependenciesPresent(R"({"dependencies":{"hexo-swpp-extra":"1.0.0"}})", "hexo-swpp"),
          "dependency names are matched exactly");
    check(!isDependenciesPresent("  /hexo-swpp@1.0.0:", "hexo-swpp", "pnpm-lock.yaml"),
          "lock entries do not count as direct dependencies");

    check(!hasUnsafeShellArguments({"--config", "site config.yml"}), "ordinary pass-through arguments are accepted");
#if defined(_WIN32)
    check(hasUnsafeShellArguments({"\" & whoami"}), "cmd metacharacters are rejected in pass-through arguments");
#endif

    disableLoggerColor();

    check(!isDependenciesPresent("[]", "hexo-swpp"), "non-object package manifests have no dependencies");
    check(!isDependenciesPresent("{broken", "hexo-swpp"), "invalid package manifests do not abort");
    const auto originalDirectory = std::filesystem::current_path();
    const auto detectionDirectory =
        std::filesystem::temp_directory_path() /
        std::format("hexo-toolkit-detection-{}", std::chrono::steady_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(detectionDirectory);
    std::ofstream(detectionDirectory / "package.json") << "{}";
    std::ofstream(detectionDirectory / "pnpm-lock.yaml") << "lockfileVersion: 9\n";
    std::filesystem::current_path(detectionDirectory);
    Config detectedConfig;
    detectedConfig.detectPackageManager();
    check(detectedConfig.packageManager == "pnpm", "pnpm lock file selects pnpm");
    std::filesystem::current_path(originalDirectory);
    std::filesystem::remove_all(detectionDirectory);
    check(detectedConfig.dependenciesSearchingFile == "package.json",
          "package manager detection keeps direct dependencies as the plugin source");

    check(resolveCommand("dep") == CommandIntent::deploy, "explicit prefixes remain supported");
    check(resolveCommand("deply") == CommandIntent::invalid, "typos cannot execute deployment");
    check(suggestCommand("deply") == "deploy", "typos suggest the intended command");
    check(resolveCommand("") == CommandIntent::invalid, "empty input is not a prefix");
    check(getJaroWinklerSimilarity(std::string(80, 'a'), std::string(80, 'a')) == 1.0,
          "similarity supports strings longer than a bitmask");
    Config yamlConfig;
    yamlConfig.loadFromYaml("additionalTools: []");
    check(yamlConfig.additionalTools.empty(), "empty additional tools disables plugins");
    bool rejected = false;
    try {
        yamlConfig.loadFromYaml("similarityThreshold: 0.9\nadditionalTools: wrong");
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected && yamlConfig.similarityThreshold == 0.85, "invalid configuration is rejected atomically");

    double threshold = 0.0;
    int timeout = 0;
    check(parseNumberFully("0.85", threshold) && threshold == 0.85, "valid floating-point config is parsed");
    check(!parseNumberFully("0.85invalid", threshold), "floating-point config rejects trailing characters");
    check(parseNumberFully("30", timeout) && timeout == 30, "valid integer config is parsed");
    check(!parseNumberFully("30s", timeout), "integer config rejects trailing characters");

#if defined(_WIN32)
    check(joinShellArguments({"--config", "site config.yml"}) == " \"--config\" \"site config.yml\"",
          "Windows pass-through arguments are quoted");
#else
    check(joinShellArguments({"--config", "site config.yml", "a'b"}) == " '--config' 'site config.yml' 'a'\\''b'",
          "POSIX pass-through arguments are quoted");
#endif

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
    std::string const capturedText((std::istreambuf_iterator<char>(captured)), std::istreambuf_iterator<char>());
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

    auto signalProcess = ManagedProcess::start("sleep 5");
    check(signalProcess.has_value(), "signal forwarding test starts");
    if (signalProcess) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        kill(getpid(), SIGINT);
        std::optional<int> signalExitCode;
        for (int attempts = 0; attempts < 50 && !signalExitCode.has_value(); ++attempts) {
            signalExitCode = signalProcess->pollExitCode();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        check(signalExitCode == 128 + SIGINT, "terminal signals are forwarded with the conventional exit code");
        check(signalProcess->wasInterruptedByTerminalSignal(), "forwarded terminal signals are recorded");
    }
#endif

    if (failures != 0) std::cerr << failures << " test(s) failed\n";
    return failures == 0 ? 0 : 1;
}

int main() {
    try {
        return runTests();
    } catch (const std::exception &exception) {
        std::cerr << "Unexpected test exception: " << exception.what() << '\n';
        return 1;
    }
}
