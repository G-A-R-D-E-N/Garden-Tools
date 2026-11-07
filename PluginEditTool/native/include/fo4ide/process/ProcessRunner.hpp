#pragma once

#include <chrono>
#include <filesystem>
#include <map>
#include <string>
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

}
