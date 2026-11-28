#include <fo4ide/process/ProcessRunner.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <reproc++/reproc.hpp>
#include <reproc++/run.hpp>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <signal.h>
#endif

namespace fo4ide::process {
namespace {

constexpr std::size_t kMaxCapturedBytes = 1024 * 1024;

#if !defined(_WIN32)
bool endsWithExe(const std::filesystem::path& path) {
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return extension == ".exe";
}
#endif

constexpr std::size_t kMaxStreamLineBytes = 64 * 1024;
constexpr auto kPipeGraceAfterExit = std::chrono::seconds(3);
constexpr auto kPollSlice = std::chrono::milliseconds(200);

#if defined(_WIN32)
// Invariant: reproc's exit pipe is inherited by grandchildren, so its wait() cannot see the child exit while one is alive; the process handle can.
class ExitWatch {
public:
    explicit ExitWatch(int pid)
        : handle_(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid))) {}
    ~ExitWatch() { if (handle_) CloseHandle(handle_); }
    ExitWatch(const ExitWatch&) = delete;
    ExitWatch& operator=(const ExitWatch&) = delete;
    bool usable() const { return handle_ != nullptr; }
    std::optional<int> exitCode() const {
        DWORD code = 0;
        if (!handle_ || WaitForSingleObject(handle_, 0) != WAIT_OBJECT_0 || !GetExitCodeProcess(handle_, &code)) return std::nullopt;
        return static_cast<int>(code);
    }
private:
    HANDLE handle_;
};
#endif

[[maybe_unused]] inline int pidOf(int pid) { return pid; }

[[maybe_unused]] inline int pidOf(const std::pair<int, std::error_code>& pid) { return pid.first; }

}

std::string Output::combined() const {
    std::string text;
    if (stdErr.empty()) text = stdOut;
    else if (stdOut.empty()) text = stdErr;
    else if (stdOut.ends_with('\n')) text = stdOut + stdErr;
    else text = stdOut + "\n" + stdErr;
    if (truncated) {
        text += "\n[output truncated: " + std::to_string(droppedBytes) + " bytes not captured]";
    }
    return text;
}

Result<Output> ReprocRunner::run(const Request& request) {
    if (request.executable.empty()) {
        return std::unexpected(Error{"ProcessStartFailed", "Executable path is empty"});
    }

    auto executable = request.executable;
    auto childArguments = request.arguments;
    auto environment = request.environment;

#if !defined(_WIN32)
    if (endsWithExe(executable)) {
        const char* configured = std::getenv("FO4RE_WINE");
        const bool explicitlyDisabled = configured && *configured == '\0';
        if (!explicitlyDisabled) {
            std::filesystem::path wine = "wine";
            if (configured) {
                std::string value(configured);
                if (!value.empty() && std::any_of(value.begin(), value.end(), [](unsigned char c) { return !std::isspace(c); })) {
                    wine = value;
                }
            }
            childArguments.insert(childArguments.begin(), executable.string());
            executable = std::move(wine);
            environment.insert_or_assign("DOTNET_ROOT", "");
        }
    }
#endif

    std::vector<std::string> arguments;
    arguments.reserve(childArguments.size() + 1);
    arguments.push_back(executable.string());
    arguments.insert(arguments.end(), childArguments.begin(), childArguments.end());

    reproc::options options;
    const auto workingDirectory = request.workingDirectory.string();
    if (!workingDirectory.empty()) options.working_directory = workingDirectory.c_str();
    if (!environment.empty()) options.env.extra = environment;

    // reproc's default stderr redirect is the parent process, even when an
    // stderr sink is passed to reproc::run. Explicit pipes are required for
    // both streams so callers receive complete child output.
    options.redirect.out.type = reproc::redirect::pipe;
    options.redirect.err.type = reproc::redirect::pipe;

    const auto timeoutCount = request.timeout.count() > 0
        ? static_cast<int>(request.timeout.count())
        : 120000;
    const reproc::milliseconds timeout(timeoutCount);
    options.deadline = timeout;
    options.stop = {
        {reproc::stop::wait, reproc::milliseconds(0)},
        {reproc::stop::terminate, reproc::milliseconds(2000)},
        {reproc::stop::kill, reproc::milliseconds(2000)},
    };

    std::string out;
    std::string err;
    std::size_t droppedBytes = 0;
    const auto boundedSink = [&droppedBytes](std::string& target) {
        return [&target, &droppedBytes](reproc::stream, const std::uint8_t* buffer, size_t size) {
            const auto room = kMaxCapturedBytes > target.size() ? kMaxCapturedBytes - target.size() : 0;
            const auto keep = std::min(room, size);
            if (keep != 0) target.append(reinterpret_cast<const char*>(buffer), keep);
            droppedBytes += size - keep;
            return std::error_code{};
        };
    };
    const auto [status, ec] = reproc::run(arguments, options, boundedSink(out), boundedSink(err));

    if (ec && ec != std::errc::timed_out) return std::unexpected(Error{"ProcessFailed", ec.message()});
    return Output{true, ec == std::errc::timed_out, status, std::move(out), std::move(err),
                  droppedBytes != 0, droppedBytes};
}

Result<int> runStreaming(const Request& request, const LineSink& onLine, std::atomic<int>& pid,
                         const std::atomic<bool>& cancel) {
    if (request.executable.empty()) {
        return std::unexpected(Error{"ProcessStartFailed", "Executable path is empty"});
    }

    std::vector<std::string> arguments;
    arguments.reserve(request.arguments.size() + 1);
    arguments.push_back(request.executable.string());
    arguments.insert(arguments.end(), request.arguments.begin(), request.arguments.end());

    reproc::options options;
    const auto workingDirectory = request.workingDirectory.string();
    if (!workingDirectory.empty()) options.working_directory = workingDirectory.c_str();
    if (!request.environment.empty()) options.env.extra = request.environment;
    options.redirect.out.type = reproc::redirect::pipe;
    options.redirect.err.type = reproc::redirect::pipe;
    options.nonblocking = true;
    options.stop = {{reproc::stop::wait, reproc::milliseconds(0)}};

    reproc::process process;
    if (const auto started = process.start(arguments, options)) {
        return std::unexpected(Error{"ProcessStartFailed", started.message()});
    }
    (void)process.close(reproc::stream::in);
    pid.store(pidOf(process.pid()));
#if defined(_WIN32)
    const ExitWatch watch(pid.load());
#endif
    if (cancel.load()) killTree(pid.load());

    std::string partial[2];
    const auto sink = [&](reproc::stream stream, const std::uint8_t* buffer, std::size_t size) {
        auto& pending = partial[stream == reproc::stream::err ? 1 : 0];
        pending.append(reinterpret_cast<const char*>(buffer), size);
        std::size_t start = 0;
        for (auto end = pending.find('\n'); end != std::string::npos; end = pending.find('\n', start)) {
            std::string_view line(pending.data() + start, end - start);
            if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
            onLine(stream == reproc::stream::err, line);
            start = end + 1;
        }
        pending.erase(0, start);
        if (pending.size() > kMaxStreamLineBytes) {
            onLine(stream == reproc::stream::err, pending);
            pending.clear();
        }
        return std::error_code{};
    };

    // Invariant: the loop ends at EOF of both pipes or kPipeGraceAfterExit after the child exited, so a grandchild that kept the pipes open cannot hang the caller.
    bool pipeOpen[2] = {true, true};
    std::optional<std::chrono::steady_clock::time_point> exitedAt;
    const auto pump = [&](reproc::stream stream) {
        const int slot = stream == reproc::stream::err ? 1 : 0;
        std::uint8_t buffer[4096];
        while (pipeOpen[slot]) {
            const auto [size, ec] = process.read(stream, buffer, sizeof buffer);
            if (ec == std::errc::broken_pipe) pipeOpen[slot] = false;
            if (ec) return;
            sink(stream, buffer, size);
        }
    };
    while (pipeOpen[0] || pipeOpen[1]) {
        if (process.poll(reproc::event::out | reproc::event::err, kPollSlice).second) break;
        pump(reproc::stream::out);
        pump(reproc::stream::err);
        if (!exitedAt) {
#if defined(_WIN32)
            const bool exited = watch.usable() ? watch.exitCode().has_value() : !process.wait(reproc::milliseconds(0)).second;
#else
            const bool exited = !process.wait(reproc::milliseconds(0)).second;
#endif
            if (exited) exitedAt = std::chrono::steady_clock::now();
        } else if (std::chrono::steady_clock::now() - *exitedAt > kPipeGraceAfterExit) {
            break;
        }
    }
    for (int i = 0; i < 2; ++i) {
        if (!partial[i].empty()) onLine(i == 1, partial[i]);
    }

#if defined(_WIN32)
    if (const auto code = watch.exitCode()) {
        pid.store(0);
        return *code;
    }
#endif
    const auto [status, ec] = process.stop({
        {reproc::stop::wait, reproc::milliseconds(5000)},
        {reproc::stop::terminate, reproc::milliseconds(2000)},
        {reproc::stop::kill, reproc::milliseconds(2000)},
    });
    pid.store(0);
    if (ec) return std::unexpected(Error{"ProcessFailed", ec.message()});
    return status;
}

void killTree(int pid) {
    if (pid <= 0) return;
#if defined(_WIN32)
    ReprocRunner runner;
    Request request;
    request.executable = "taskkill";
    request.arguments = {"/PID", std::to_string(pid), "/T", "/F"};
    request.timeout = std::chrono::milliseconds(10000);
    (void)runner.run(request);
#else
    ::kill(-pid, SIGKILL);
    ::kill(pid, SIGKILL);
#endif
}

}
