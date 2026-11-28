#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <fo4ide/core/Result.hpp>

namespace fo4ide::process {

struct Request {
    std::filesystem::path executable;
    std::vector<std::string> arguments;
    std::filesystem::path workingDirectory;
    std::map<std::string, std::string> environment;
    std::chrono::milliseconds timeout{120000};
};

struct Output {
    bool started{false};
    bool timedOut{false};
    int exitCode{-1};
    std::string stdOut;
    std::string stdErr;
    bool truncated{false};
    std::size_t droppedBytes{0};

    std::string combined() const;
};

class Runner {
public:
    virtual ~Runner() = default;
    virtual Result<Output> run(const Request& request) = 0;
};

class ReprocRunner final : public Runner {
public:
    Result<Output> run(const Request& request) override;
};

// Invariant: the sink runs on the calling thread, once per stdout or stderr line, without the line break.
using LineSink = std::function<void(bool isStderr, std::string_view line)>;

// Constraint: pid is published right after the child starts and cancel is read after that, so a cancel
// raised before the child exists still ends it. Requires no timeout: the caller ends the run with killTree.
Result<int> runStreaming(const Request& request, const LineSink& onLine, std::atomic<int>& pid,
                         const std::atomic<bool>& cancel);

// Invariant: ends the process and every child it started, so no grandchild keeps a lock the run held.
void killTree(int pid);

}
