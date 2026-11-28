#include "fo4/archive/path_filter.hpp"
#include "fo4/text/bounded_regex.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cerrno>
#include <string>

#ifdef __linux__
#include "../src/text/bounded_regex_runtime.hpp"
#include <csignal>
#include <cstdlib>
#include <locale>
#include <dlfcn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#endif

namespace {

using fo4::archive::PathFilter;
using fo4::text::BoundedRegex;
using fo4::text::RegexTimeout;
using std::chrono::milliseconds;

// Constraint: the constructor starts the worker inside the same timeout, so ASan builds need more than 50 ms for it.
#if defined(__SANITIZE_ADDRESS__)
#define RADICLE_TEST_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define RADICLE_TEST_ASAN 1
#endif
#endif

#ifdef RADICLE_TEST_ASAN
constexpr milliseconds k_hostile_deadline{2000};
#else
constexpr milliseconds k_hostile_deadline{50};
#endif

TEST(BoundedRegex, AnswersLikeStdRegexSearchWhenItFinishesInTime) {
  const BoundedRegex regex(R"(gun\d+\.nif)");
  EXPECT_TRUE(regex.search("Meshes\\GUN01.NIF"));
  EXPECT_FALSE(regex.search("Meshes\\gun.nif"));
  EXPECT_THROW(BoundedRegex("(unclosed"), std::regex_error);
}

TEST(BoundedRegex, MatchesLongOrdinaryInputWithinItsDeadline) {
  const BoundedRegex regex("a+$", false, std::chrono::seconds(5));
  const std::string ordinary(1'000'000, 'a');
  EXPECT_TRUE(regex.search(ordinary));
}

TEST(BoundedRegex, SearchesPastEmbeddedNulBytes) {
  const BoundedRegex regex("b", false);
  const std::string text("a\0b", 3);
  EXPECT_TRUE(regex.search(text));
}

TEST(BoundedRegex, SupportsEcmaScriptLookaheadAndBackreferences) {
  EXPECT_TRUE(BoundedRegex(R"((foo)(?=bar))", false).search("foobar"));
  EXPECT_TRUE(BoundedRegex(R"((ab)\1)", false).search("abab"));
  EXPECT_FALSE(BoundedRegex(R"((ab)\1)", false).search("abac"));
}

TEST(BoundedRegex, LiteralSuffixMatchesTheEngineAtEndAnchorsAndCaseBoundaries) {
  for (const bool insensitive : {false, true}) {
    for (const char* expression : {"a+$", "Z+$", "3+$"}) {
      const BoundedRegex bounded(expression, insensitive, std::chrono::seconds(5));
      const std::regex reference(expression, insensitive ? std::regex_constants::ECMAScript | std::regex_constants::icase
                                                       : std::regex_constants::ECMAScript);
      for (const std::string& text : {std::string{}, std::string("aa"), std::string("AA"), std::string("zz"),
                                     std::string("333"), std::string("aa\n"), std::string("aa\r"), std::string("aa\r\n"),
                                     std::string("aa\xE2\x80\xA8"), std::string("aa\xE2\x80\xA9"),
                                     std::string("a\0a", 3), std::string("a\0", 2)}) {
        EXPECT_EQ(bounded.search(text), std::regex_search(text, reference)) << expression;
      }
    }
  }
}

TEST(BoundedRegex, StopsACatastrophicPatternAtTheDeadline) {
  const BoundedRegex regex("(a+)+$", true, k_hostile_deadline);
  const std::string hostile = std::string(40, 'a') + "!";
  const auto started = std::chrono::steady_clock::now();
  EXPECT_THROW(regex.search(hostile), RegexTimeout);
  EXPECT_LT(std::chrono::steady_clock::now() - started, k_hostile_deadline + std::chrono::seconds(5));
}

TEST(BoundedRegex, RemainsUsableAfterAPathologicalTimeout) {
  const BoundedRegex regex("^(a|aa)+$", true, std::chrono::seconds(10));
  const std::string hostile = std::string(30000, 'a') + "!";
  EXPECT_THROW(regex.search(hostile), RegexTimeout);
  EXPECT_TRUE(regex.search("aa"));
}

TEST(BoundedRegex, TheTimeoutMessageNamesTheCause) {
  try {
    throw RegexTimeout();
  } catch (const RegexTimeout& timeout) {
    const std::string what = timeout.what();
    EXPECT_NE(what.find("too long"), std::string::npos);
    EXPECT_NE(what.find("stopped"), std::string::npos);
  }
}

#ifdef __linux__
TEST(BoundedRegex, InheritedPatternIsRejectedWithoutTouchingTheParentsConnection) {
  GTEST_FLAG_SET(death_test_style, "threadsafe");
  EXPECT_EXIT({
    const BoundedRegex regex("a", false, std::chrono::seconds(5));
    const pid_t child = ::fork();
    if (child < 0) _exit(2);
    if (child == 0) {
      ::alarm(2);
      try { regex.search("a"); } catch (const std::runtime_error&) { _exit(0); }
      _exit(3);
    }
    int status = 0;
    if (::waitpid(child, &status, 0) != child || !WIFEXITED(status) || WEXITSTATUS(status) != 0) _exit(4);
    _exit(regex.search("a") && !regex.search("b") ? 0 : 5);
  }, ::testing::ExitedWithCode(0), "");
}

TEST(BoundedRegex, EmptyViewsHaveTheEnginesEmptyInputSemantics) {
  EXPECT_TRUE(BoundedRegex("^$", false).search(std::string_view{}));
  EXPECT_FALSE(BoundedRegex("a+$", false).search(std::string_view{}));
}

TEST(BoundedRegex, NamedEnvironmentLocaleMatchesTheInProcessEngine) {
  GTEST_FLAG_SET(death_test_style, "threadsafe");
  EXPECT_EXIT({
    std::locale::global(std::locale(""));
    const BoundedRegex regex("[[:alpha:]]+$", true, std::chrono::seconds(5));
    const std::regex reference("[[:alpha:]]+$", std::regex_constants::ECMAScript | std::regex_constants::icase);
    const std::string text("A\xC3\xA9");
    _exit(regex.search(text) == std::regex_search(text, reference) ? 0 : 2);
  }, ::testing::ExitedWithCode(0), "");
}

TEST(BoundedRegex, InvalidRuntimeTokensAreSafeDuringInitialization) {
  GTEST_FLAG_SET(death_test_style, "threadsafe");
  EXPECT_EXIT({
    void* module = ::dlopen(RADICLE_REGEX_RUNTIME_TEST_PATH, RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);
    if (module == nullptr) _exit(2);
    const auto entry = reinterpret_cast<RadicleRegexRuntimeEntry>(::dlsym(module, "radicle_regex_runtime_v1"));
    if (entry == nullptr) _exit(3);
    const auto* api = entry();
    std::thread invalid([api] {
      for (int i = 0; i != 10000; ++i) {
        int signal = 0;
        if (api->status(0, &signal) != -1) _exit(4);
        api->release(0);
      }
    });
    const BoundedRegex regex("a", false, std::chrono::seconds(5));
    invalid.join();
    _exit(regex.search("a") ? 0 : 5);
  }, ::testing::ExitedWithCode(0), "");
}

TEST(BoundedRegex, RuntimeCapacityFailsClosedAndReleasedSlotsBecomeReusable) {
  GTEST_FLAG_SET(death_test_style, "threadsafe");
  EXPECT_EXIT({
    void* module = ::dlopen(RADICLE_REGEX_RUNTIME_TEST_PATH, RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);
    if (module == nullptr) _exit(2);
    const auto entry = reinterpret_cast<RadicleRegexRuntimeEntry>(::dlsym(module, "radicle_regex_runtime_v1"));
    if (entry == nullptr) _exit(3);
    const auto* api = entry();
    int sockets[2];
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets) != 0) _exit(4);
    std::uint64_t tokens[64]{};
    for (auto& token : tokens) {
      if (api->spawn("/bin/true", sockets[1], &token) != 0) _exit(5);
    }
    std::uint64_t extra = 0;
    if (api->spawn("/bin/true", sockets[1], &extra) != EAGAIN) _exit(6);
    for (const auto token : tokens) api->release(token);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (api->spawn("/bin/true", sockets[1], &extra) == EAGAIN) {
      if (std::chrono::steady_clock::now() >= deadline) _exit(7);
      std::this_thread::sleep_for(milliseconds(10));
    }
    if (extra == 0) _exit(8);
    api->release(extra);
    _exit(0);
  }, ::testing::ExitedWithCode(0), "");
}

TEST(BoundedRegex, RuntimeReapsAnAbandonedWorkerAfterItsCallerClosesTheModule) {
  GTEST_FLAG_SET(death_test_style, "threadsafe");
  EXPECT_EXIT({
    void* module = ::dlopen(RADICLE_REGEX_RUNTIME_TEST_PATH, RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);
    if (module == nullptr) _exit(2);
    const auto entry = reinterpret_cast<RadicleRegexRuntimeEntry>(::dlsym(module, "radicle_regex_runtime_v1"));
    if (entry == nullptr) _exit(3);
    const auto* api = entry();
    int sockets[2];
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets) != 0) _exit(4);
    std::uint64_t token = 0;
    if (api->spawn(RADICLE_REGEX_WORKER_TEST_PATH, sockets[1], &token) != 0) _exit(5);
    ::close(sockets[1]);
    api->release(token);
    ::close(sockets[0]);
    ::dlclose(module);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    int signal = 0;
    while (api->status(token, &signal) != -1) {
      if (std::chrono::steady_clock::now() >= deadline) _exit(6);
      std::this_thread::sleep_for(milliseconds(10));
    }
    _exit(0);
  }, ::testing::ExitedWithCode(0), "");
}

TEST(BoundedRegex, HostIgnoringChildExitsDoesNotBreakTimeoutAndReuse) {
  GTEST_FLAG_SET(death_test_style, "threadsafe");
  EXPECT_EXIT({
    std::signal(SIGCHLD, SIG_IGN);
    const BoundedRegex regex("(a+)+$", false, k_hostile_deadline);
    try {
      regex.search(std::string(40, 'a') + "!");
      _exit(2);
    } catch (const RegexTimeout&) {}
    _exit(regex.search("aa") ? 0 : 3);
  }, ::testing::ExitedWithCode(0), "");
}

TEST(BoundedRegex, MissingWorkerFailsClosedBeforeEvaluatingRegex) {
  GTEST_FLAG_SET(death_test_style, "threadsafe");
  EXPECT_EXIT({
    ::setenv("RADICLE_REGEX_WORKER", "/radicle-missing-regex-worker", 1);
    try {
      const BoundedRegex regex("a");
      _exit(2);
    } catch (const std::runtime_error&) { _exit(0); }
  }, ::testing::ExitedWithCode(0), "");
}

TEST(BoundedRegex, CompilationHasADeadlineBeforeAnySearch) {
  EXPECT_THROW(BoundedRegex("a", false, milliseconds(0)), RegexTimeout);
}
#endif

TEST(PathFilter, SimpleWildcardAndRegexModesMatchWholePathsAsDocumented) {
  EXPECT_TRUE(PathFilter("GUN", "simple").matches("Meshes\\gun01.nif"));
  EXPECT_TRUE(PathFilter("Meshes\\*.nif", "wildcard").matches("meshes\\a.nif"));
  EXPECT_FALSE(PathFilter("Meshes\\*.nif", "wildcard").matches("Meshes_a.nif"));
  EXPECT_FALSE(PathFilter("Meshes\\*.nif", "wildcard").matches("Textures\\Meshes\\a.nif"));
  EXPECT_FALSE(PathFilter("*.nif", "wildcard").matches("a.nif.txt"));
  EXPECT_TRUE(PathFilter("a.b", "wildcard").matches("a.b"));
  EXPECT_FALSE(PathFilter("a.b", "wildcard").matches("axb"));
  EXPECT_TRUE(PathFilter("", "regex").matches("anything"));
  EXPECT_THROW(PathFilter("x", "glob"), std::invalid_argument);
}

TEST(PathFilter, ARegexFilterThatRunsPastItsDeadlineThrowsRegexTimeout) {
  const PathFilter filter("(a+)+$", "regex", k_hostile_deadline);
  EXPECT_THROW(filter.matches(std::string(40, 'a') + "!"), RegexTimeout);
}

}  // namespace
