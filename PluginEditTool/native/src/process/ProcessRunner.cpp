#include <fo4ide/process/ProcessRunner.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <system_error>
#include <utility>
#include <vector>

#include <reproc++/run.hpp>

namespace fo4ide::process {
namespace {

#if !defined(_WIN32)
bool endsWithExe(const std::filesystem::path& path) {
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return extension == ".exe";
}
#endif

}

std::string Output::combined() const {
    if (stdErr.empty()) return stdOut;
    if (stdOut.empty()) return stdErr;
    if (stdOut.ends_with('\n')) return stdOut + stdErr;
    return stdOut + "\n" + stdErr;
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
    reproc::sink::string outSink(out);
    reproc::sink::string errSink(err);
    const auto [status, ec] = reproc::run(arguments, options, outSink, errSink);

    if (ec) {
        const bool timedOut = ec == std::errc::timed_out;
        if (timedOut) return Output{true, true, status, std::move(out), std::move(err)};
        return std::unexpected(Error{"ProcessFailed", ec.message()});
    }

    return Output{true, false, status, std::move(out), std::move(err)};
}

}
