#include "bounded_regex_runtime.hpp"

#include <cerrno>
#include <atomic>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <linux/close_range.h>
#include <linux/sched.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char** environ;

namespace {

constexpr std::size_t capacity = 64;
enum class State { free, spawning, active, exited, abandoned };
struct Slot {
    State state;
    std::uint64_t token;
    int pidfd;
    int signal;
};
Slot slots[capacity]{};
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_once_t once = PTHREAD_ONCE_INIT;
std::atomic<pid_t> owner{0};
static_assert(std::atomic<pid_t>::is_always_lock_free);
int initialization_error = 0;
std::uint64_t next_token = 1;

void collect(Slot& slot) {
    if (slot.state != State::active && slot.state != State::abandoned) return;
    siginfo_t info{};
    const int result = ::waitid(P_PIDFD, static_cast<id_t>(slot.pidfd), &info, WEXITED | WNOHANG);
    if (result == 0 && info.si_pid == 0) return;
    if (result < 0 && errno != ECHILD) return;
    slot.signal = result == 0 && (info.si_code == CLD_KILLED || info.si_code == CLD_DUMPED)
        ? info.si_status : 0;
    ::close(slot.pidfd);
    slot.pidfd = -1;
    slot.state = slot.state == State::abandoned ? State::free : State::exited;
}

void* reap(void*) {
    for (;;) {
        ::pthread_mutex_lock(&mutex);
        for (auto& slot : slots) collect(slot);
        ::pthread_mutex_unlock(&mutex);
        const timespec interval{0, 10'000'000};
        ::nanosleep(&interval, nullptr);
    }
    return nullptr;
}

void initialize() {
    owner = ::getpid();
#if defined(SYS_pidfd_send_signal)
    siginfo_t info{};
    constexpr int invalid_fd = std::numeric_limits<int>::max();
    if (::waitid(P_PIDFD, static_cast<id_t>(invalid_fd), &info, WEXITED | WNOHANG) != -1 || errno != EBADF ||
        ::syscall(SYS_pidfd_send_signal, invalid_fd, 0, nullptr, 0) != -1 || errno != EBADF) {
        initialization_error = ENOTSUP;
        return;
    }
#else
    initialization_error = ENOSYS;
    return;
#endif
    pthread_attr_t attributes;
    initialization_error = ::pthread_attr_init(&attributes);
    if (initialization_error != 0) return;
    initialization_error = ::pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
    if (initialization_error == 0) {
        pthread_t thread;
        initialization_error = ::pthread_create(&thread, &attributes, reap, nullptr);
    }
    ::pthread_attr_destroy(&attributes);
}

int spawn_worker(const char* path, int connection, std::uint64_t* token) {
    if (path == nullptr || token == nullptr || connection < 3) return EINVAL;
    ::pthread_once(&once, initialize);
    if (owner != ::getpid()) return ENOTSUP;
    if (initialization_error != 0) return initialization_error;
#if !defined(SYS_clone3) || !defined(SYS_pidfd_send_signal) || !defined(SYS_close_range)
    return ENOSYS;
#else
    Slot* selected = nullptr;
    ::pthread_mutex_lock(&mutex);
    for (auto& slot : slots) {
        collect(slot);
        if (selected == nullptr && slot.state == State::free) selected = &slot;
    }
    if (selected == nullptr || next_token == std::numeric_limits<std::uint64_t>::max()) {
        ::pthread_mutex_unlock(&mutex);
        return EAGAIN;
    }
    selected->state = State::spawning;
    selected->token = next_token++;
    selected->signal = 0;
    ::pthread_mutex_unlock(&mutex);

    sigset_t blocked;
    sigset_t previous;
    ::sigfillset(&blocked);
    const int mask_error = ::pthread_sigmask(SIG_SETMASK, &blocked, &previous);
    int pidfd = -1;
    clone_args arguments{};
    arguments.flags = CLONE_PIDFD;
    arguments.pidfd = reinterpret_cast<std::uintptr_t>(&pidfd);
    arguments.exit_signal = SIGCHLD;
    char* argv[] = {const_cast<char*>(path), nullptr};
    const long child = mask_error == 0 ? ::syscall(SYS_clone3, &arguments, sizeof(arguments)) : -1;
    const int error = mask_error != 0 ? mask_error : errno;
    if (child == 0) {
        if (::dup2(connection, STDIN_FILENO) < 0) ::_exit(126);
        if (::syscall(SYS_close_range, 3U, ~0U, CLOSE_RANGE_CLOEXEC) < 0) ::_exit(126);
        ::close(STDOUT_FILENO);
        ::close(STDERR_FILENO);
        ::execve(path, argv, environ);
        ::_exit(126);
    }
    if (mask_error == 0) ::pthread_sigmask(SIG_SETMASK, &previous, nullptr);
    ::pthread_mutex_lock(&mutex);
    if (child < 0) {
        selected->state = State::free;
    } else {
        selected->pidfd = pidfd;
        selected->state = State::active;
        *token = selected->token;
    }
    ::pthread_mutex_unlock(&mutex);
    return child < 0 ? error : 0;
#endif
}

int status(std::uint64_t token, int* signal) {
    if (owner != ::getpid() || signal == nullptr) return -1;
    int result = -1;
    ::pthread_mutex_lock(&mutex);
    for (auto& slot : slots) {
        if (slot.token != token || slot.state == State::free) continue;
        collect(slot);
        result = slot.state == State::exited ? 1 : 0;
        *signal = slot.signal;
        break;
    }
    ::pthread_mutex_unlock(&mutex);
    return result;
}

void release(std::uint64_t token) {
    if (owner != ::getpid()) return;
    ::pthread_mutex_lock(&mutex);
    for (auto& slot : slots) {
        if (slot.token != token || slot.state == State::free) continue;
        collect(slot);
        if (slot.state == State::active) {
#ifdef SYS_pidfd_send_signal
            ::syscall(SYS_pidfd_send_signal, slot.pidfd, SIGKILL, nullptr, 0);
#endif
            slot.state = State::abandoned;
        } else if (slot.state == State::exited) {
            slot.state = State::free;
        }
        break;
    }
    ::pthread_mutex_unlock(&mutex);
}

const RadicleRegexRuntimeV1 api{1, spawn_worker, status, release};

}

extern "C" const RadicleRegexRuntimeV1* radicle_regex_runtime_v1() {
    return &api;
}
