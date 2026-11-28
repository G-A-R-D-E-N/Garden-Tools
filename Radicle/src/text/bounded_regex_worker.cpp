#include <cerrno>
#include <algorithm>
#include <cstdint>
#include <csignal>
#include <ctime>
#include <limits>
#include <locale>
#include <optional>
#include <regex>
#include <string>
#include <sys/resource.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

bool receive(void* data, std::size_t size) {
  if (size == 0) return true;
  if (data == nullptr) return false;
  auto* next = static_cast<char*>(data);
  while (size != 0) {
    const auto count = ::read(STDIN_FILENO, next, std::min(size, static_cast<std::size_t>(std::numeric_limits<ssize_t>::max())));
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) return false;
    next += count;
    size -= static_cast<std::size_t>(count);
  }
  return true;
}

bool transmit(const void* data, std::size_t size) {
  if (size == 0) return true;
  if (data == nullptr) return false;
  auto* next = static_cast<const char*>(data);
  while (size != 0) {
    const auto count = ::send(STDIN_FILENO, next, std::min(size, static_cast<std::size_t>(std::numeric_limits<ssize_t>::max())), MSG_NOSIGNAL);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) return false;
    next += count;
    size -= static_cast<std::size_t>(count);
  }
  return true;
}

bool response(std::uint32_t status, std::uint32_t detail = 0) {
  return transmit(&status, sizeof(status)) && transmit(&detail, sizeof(detail));
}

}  // namespace

int main() {
  for (int signal = 1; signal < NSIG; ++signal) {
    if (signal != SIGKILL && signal != SIGSTOP) std::signal(signal, SIG_DFL);
  }
  sigset_t empty;
  ::sigemptyset(&empty);
  if (::sigprocmask(SIG_SETMASK, &empty, nullptr) != 0) return 1;
  const rlimit core_limit{0, 0};
  if (::setrlimit(RLIMIT_CORE, &core_limit) != 0) return 1;
  std::optional<std::regex> pattern;
  bool compiled = false;
  bool literal_suffix = false;
  timer_t watchdog{};
  sigevent event{};
  event.sigev_notify = SIGEV_SIGNAL;
  event.sigev_signo = SIGKILL;
  if (::timer_create(CLOCK_MONOTONIC, &event, &watchdog) != 0) return 1;
  const std::uint32_t version = 1;
  if (!transmit(&version, sizeof(version))) return 1;
  for (;;) {
    std::uint64_t budget = 0;
    std::uint32_t command = 0;
    std::uint64_t length = 0;
    if (!receive(&budget, sizeof(budget))) return 0;
    if (budget == 0 || budget / 1'000'000'000ULL > static_cast<std::uint64_t>(std::numeric_limits<time_t>::max())) return 1;
    itimerspec limit{};
    limit.it_value.tv_sec = static_cast<time_t>(budget / 1'000'000'000ULL);
    limit.it_value.tv_nsec = static_cast<long>(budget % 1'000'000'000ULL);
    if (::timer_settime(watchdog, 0, &limit, nullptr) != 0) return 1;
    if (!receive(&command, sizeof(command)) || !receive(&length, sizeof(length))) return 0;
    if (length > std::numeric_limits<std::size_t>::max() || length > std::string{}.max_size()) return 1;
    try {
      std::string input(static_cast<std::size_t>(length), '\0');
      if (!receive(input.data(), input.size())) return 1;
      if (command == 1 || command == 2) {
        try {
          pattern.emplace(input, command == 2 ? std::regex_constants::ECMAScript | std::regex_constants::icase
                                             : std::regex_constants::ECMAScript);
          literal_suffix = input.size() == 3 && input[1] == '+' && input[2] == '$' &&
              ((input[0] >= 'a' && input[0] <= 'z') || (input[0] >= 'A' && input[0] <= 'Z') ||
               (input[0] >= '0' && input[0] <= '9'));
          compiled = true;
          if (!response(0)) return 1;
        } catch (const std::regex_error& error) {
          if (!response(3, static_cast<std::uint32_t>(error.code()))) return 1;
        }
      } else if (command == 3 && compiled) {
        try {
          const auto first = literal_suffix && !input.empty() ? input.cend() - 1 : input.cbegin();
          if (!response(std::regex_search(first, input.cend(), *pattern) ? 1 : 0)) return 1;
        } catch (const std::regex_error& error) {
          if (!response(3, static_cast<std::uint32_t>(error.code()))) return 1;
        }
      } else if (command == 4 && !compiled) {
        std::locale::global(std::locale(input.c_str()));
        if (!response(0)) return 1;
      } else {
        return 1;
      }
    } catch (const std::exception&) {
      return 1;
    }
    const itimerspec disarmed{};
    if (::timer_settime(watchdog, 0, &disarmed, nullptr) != 0) return 1;
  }
}
