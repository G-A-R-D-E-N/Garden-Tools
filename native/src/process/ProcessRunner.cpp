#include <fo4ide/process/ProcessRunner.hpp>

#include <system_error>
#include <utility>
#include <vector>

#include <reproc++/run.hpp>

namespace fo4ide::process {

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

    std::vector<std::string> arguments;
    arguments.reserve(request.arguments.size() + 1);
    arguments.push_back(request.executable.string());
    arguments.insert(arguments.end(), request.arguments.begin(), request.arguments.end());

    reproc::options options;
    const auto workingDirectory = request.workingDirectory.string();
    if (!workingDirectory.empty()) options.working_directory = workingDirectory.c_str();
    if (!request.environment.empty()) options.env.extra = request.environment;

    const auto timeoutCount = request.timeout.count() > 0
        ? static_cast<int>(request.timeout.count())
        : 120000;
    const reproc::milliseconds timeout(timeoutCount);
    options.stop = {
        {reproc::stop::wait, timeout},
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
