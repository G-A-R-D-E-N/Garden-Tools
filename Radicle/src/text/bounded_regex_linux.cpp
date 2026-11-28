#include "fo4/text/bounded_regex.hpp"
#include "bounded_regex_runtime.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <climits>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <dlfcn.h>
#include <fcntl.h>
#include <filesystem>
#include <locale>
#include <limits>
#include <mutex>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

namespace fo4::text {
namespace {

int module_anchor = 0;

std::filesystem::path worker_path() {
    if (const char* override_path = std::getenv("RADICLE_REGEX_WORKER")) {
        const std::filesystem::path candidate(override_path);
        if (!candidate.is_absolute() || ::access(candidate.c_str(), X_OK) != 0) {
            throw std::runtime_error("RADICLE_REGEX_WORKER must name an executable by absolute path");
        }
        return candidate;
    }
    std::filesystem::path executable;
    char buffer[4096];
    const auto length = ::readlink("/proc/self/exe", buffer, sizeof(buffer));
    if (length > 0 && static_cast<std::size_t>(length) < sizeof(buffer)) {
        executable = std::filesystem::path(std::string(buffer, static_cast<std::size_t>(length))).parent_path();
    }
    Dl_info info{};
    std::filesystem::path module;
    if (::dladdr(&module_anchor, &info) != 0 && info.dli_fname != nullptr) {
        module = std::filesystem::absolute(info.dli_fname).parent_path();
    }
    for (const auto& base : {executable, module, module / ".." / "bin",
                             std::filesystem::path(RADICLE_REGEX_WORKER_INSTALL_PATH).parent_path()}) {
        if (!base.is_absolute()) continue;
        auto candidate = base / "radicle-regex-worker";
        if (::access(candidate.c_str(), X_OK) == 0) return candidate;
    }
    throw std::runtime_error("Radicle regex worker is missing; install it beside the executable or set RADICLE_REGEX_WORKER to its absolute path");
}

}  // namespace

RegexTimeout::RegexTimeout()
    : std::runtime_error("The regex filter took too long to evaluate and was stopped. "
                         "Use a simpler expression or substring/wildcard mode.") {}

struct BoundedRegex::Impl {
    using Clock = std::chrono::steady_clock;
    using Deadline = Clock::time_point;

    const Deadline initialized_at = Clock::now();
    const pid_t owner = ::getpid();
    std::string expression;
    std::string locale_name;
    bool insensitive;
    std::chrono::milliseconds timeout;
    std::timed_mutex mutex;
    void* runtime_module = nullptr;
    const RadicleRegexRuntimeV1* runtime = nullptr;
    std::uint64_t child = 0;
    int socket = -1;
    bool worker_ready = false;

    Impl(std::string_view value, bool ignore_case, std::chrono::milliseconds limit)
        : expression(value), insensitive(ignore_case), timeout(limit) {
        try {
            const auto deadline = initialized_at + timeout;
            if (Clock::now() >= deadline) throw RegexTimeout();
            locale_name = std::locale().name();
            start(deadline);
        } catch (...) {
            stop();
            if (runtime_module != nullptr) ::dlclose(runtime_module);
            throw;
        }
    }

    ~Impl() {
        stop();
        if (runtime_module != nullptr) ::dlclose(runtime_module);
    }

    void stop() noexcept {
        if (socket >= 0) {
            ::close(socket);
            socket = -1;
        }
        if (child != 0) {
            runtime->release(child);
            child = 0;
        }
        worker_ready = false;
    }

    [[noreturn]] void disconnected() {
        const bool evaluating = worker_ready;
        stop();
        if (evaluating) throw RegexTimeout();
        throw std::runtime_error("Radicle regex worker exited unexpectedly");
    }

    void ready(short events, Deadline deadline) {
        for (;;) {
            const auto now = Clock::now();
            if (now >= deadline) { stop(); throw RegexTimeout(); }
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
            const int wait_ms = static_cast<int>(std::clamp<std::int64_t>(remaining.count() + 1, 1, INT_MAX));
            pollfd descriptor{socket, events, 0};
            const auto result = ::poll(&descriptor, 1, wait_ms);
            if (result < 0 && errno == EINTR) continue;
            if (result < 0) { stop(); throw std::runtime_error("Cannot poll Radicle regex worker"); }
            if (result == 0) continue;
            if (descriptor.revents & events) return;
            disconnected();
        }
    }

    void transfer(void* data, std::size_t size, bool writing, Deadline deadline) {
        if (size == 0) return;
        if (data == nullptr) { stop(); throw std::invalid_argument("Null regex payload"); }
        auto* next = static_cast<char*>(data);
        while (size != 0) {
            ready(writing ? POLLOUT : POLLIN, deadline);
            const auto chunk = std::min(size, static_cast<std::size_t>(std::numeric_limits<ssize_t>::max()));
            const auto count = writing ? ::send(socket, next, chunk, MSG_NOSIGNAL) : ::recv(socket, next, chunk, 0);
            if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
            if (count <= 0) disconnected();
            next += count;
            size -= static_cast<std::size_t>(count);
        }
    }

    std::uint32_t request(std::uint32_t command, std::string_view input, Deadline deadline) {
        const auto remaining = std::chrono::duration_cast<std::chrono::nanoseconds>(deadline - Clock::now());
        if (remaining.count() <= 0) { stop(); throw RegexTimeout(); }
        auto budget = static_cast<std::uint64_t>(remaining.count());
        auto length = static_cast<std::uint64_t>(input.size());
        transfer(&budget, sizeof(budget), true, deadline);
        transfer(&command, sizeof(command), true, deadline);
        transfer(&length, sizeof(length), true, deadline);
        transfer(const_cast<char*>(input.data()), input.size(), true, deadline);
        std::uint32_t status = 0;
        std::uint32_t detail = 0;
        transfer(&status, sizeof(status), false, deadline);
        transfer(&detail, sizeof(detail), false, deadline);
        if (status == 3) {
            const auto code = static_cast<std::regex_constants::error_type>(detail);
            if (code == std::regex_constants::error_stack || code == std::regex_constants::error_complexity) {
                throw RegexTimeout();
            }
            throw std::regex_error(code);
        }
        if (status > 1) { stop(); throw std::runtime_error("Invalid Radicle regex worker response"); }
        return status;
    }

    void start(Deadline deadline) {
        if (Clock::now() >= deadline) throw RegexTimeout();
        if (locale_name == "*") throw std::runtime_error("Radicle isolated regex requires a named locale");
        const auto path = worker_path().string();
        if (runtime_module == nullptr) {
            const auto location = std::filesystem::path(path).parent_path() / "libradicle-regex-runtime.so";
            runtime_module = ::dlopen(location.c_str(), RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);
            if (runtime_module == nullptr) throw std::runtime_error("Radicle regex runtime is missing beside its worker");
            auto entry = reinterpret_cast<RadicleRegexRuntimeEntry>(::dlsym(runtime_module, "radicle_regex_runtime_v1"));
            if (entry == nullptr || (runtime = entry()) == nullptr || runtime->version != 1) {
                throw std::runtime_error("Incompatible Radicle regex runtime");
            }
        }
        int pair[2] = {-1, -1};
        if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair) != 0) {
            throw std::runtime_error("Cannot create Radicle regex worker connection");
        }
        const int parent = ::fcntl(pair[0], F_DUPFD_CLOEXEC, 3);
        const int worker = ::fcntl(pair[1], F_DUPFD_CLOEXEC, 3);
        ::close(pair[0]);
        ::close(pair[1]);
        if (parent < 0 || worker < 0) {
            if (parent >= 0) ::close(parent);
            if (worker >= 0) ::close(worker);
            throw std::runtime_error("Cannot duplicate Radicle regex worker connection");
        }
        const int spawned = runtime->spawn(path.c_str(), worker, &child);
        ::close(worker);
        if (spawned != 0) {
            ::close(parent);
            throw std::runtime_error("Cannot start Radicle regex worker: Linux pidfd support or cleanup capacity is unavailable (" + std::to_string(spawned) + ")");
        }
        socket = parent;
        const int flags = ::fcntl(socket, F_GETFL, 0);
        if (flags < 0 || ::fcntl(socket, F_SETFL, flags | O_NONBLOCK) < 0) {
            stop();
            throw std::runtime_error("Cannot configure Radicle regex worker connection");
        }
        try {
            std::uint32_t handshake = 0;
            transfer(&handshake, sizeof(handshake), false, deadline);
            if (handshake != 1) throw std::runtime_error("Incompatible Radicle regex worker");
            worker_ready = true;
            request(4, locale_name, deadline);
            request(insensitive ? 2U : 1U, expression, deadline);
        } catch (...) {
            stop();
            throw;
        }
    }

    bool search(std::string_view text) {
        if (owner != ::getpid()) throw std::runtime_error("An inherited Radicle regex cannot be used after fork; exec first");
        const auto deadline = Clock::now() + timeout;
        std::unique_lock lock(mutex, std::defer_lock);
        if (!lock.try_lock_until(deadline)) throw RegexTimeout();
        if (child == 0) start(deadline);
        return request(3, text, deadline) == 1;
    }
};

BoundedRegex::BoundedRegex(std::string_view pattern, bool case_insensitive, std::chrono::milliseconds timeout)
    : pattern_(std::make_shared<Impl>(pattern, case_insensitive, timeout)), timeout_(timeout) {}

bool BoundedRegex::search(std::string_view text) const {
    return pattern_->search(text);
}

}  // namespace fo4::text
