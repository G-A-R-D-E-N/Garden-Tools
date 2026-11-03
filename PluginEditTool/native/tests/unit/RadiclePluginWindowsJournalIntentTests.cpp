#include <chrono>
#include <algorithm>
#include <fo4/io/sha256.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include "../../src/plugin/RadiclePluginWindowsJournalDirectory.hpp"
#include "../../src/plugin/RadiclePluginWindowsJournalNative.hpp"
#include "../../src/plugin/RadiclePluginWindowsJournalIntent.hpp"
#include "../../src/plugin/RadiclePluginJournalDestination.hpp"
#include <nlohmann/json.hpp>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>
#include <winternl.h>
#include <sddl.h>
#include <aclapi.h>
#include <userenv.h>

namespace {
struct Handle {
    HANDLE value{INVALID_HANDLE_VALUE};
    ~Handle() { if (value != INVALID_HANDLE_VALUE && value != nullptr) ::CloseHandle(value); }
};
struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() { std::error_code ec; std::filesystem::remove_all(root, ec); }
};
struct PrivateRoot {
    std::filesystem::path path;
    Handle handle;
    explicit PrivateRoot(const std::filesystem::path& parent, bool readable = false) : path(parent / "journal") {
        Handle token;
        REQUIRE(::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token.value));
        DWORD length = 0;
        ::GetTokenInformation(token.value, TokenUser, nullptr, 0, &length);
        std::vector<std::uintptr_t> bytes((length + sizeof(std::uintptr_t) - 1) / sizeof(std::uintptr_t));
        REQUIRE(::GetTokenInformation(token.value, TokenUser, bytes.data(), length, &length));
        wchar_t* sid = nullptr;
        REQUIRE(::ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(bytes.data())->User.Sid, &sid));
        const auto sddl = std::wstring(L"O:") + sid + L"D:P(A;OICI;FA;;;" + sid + L")" +
            (readable ? L"(A;OICI;GR;;;WD)" : L"");
        ::LocalFree(sid);
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        REQUIRE(::ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr));
        SECURITY_ATTRIBUTES attributes{sizeof(attributes), descriptor, FALSE};
        const auto created = ::CreateDirectoryW(path.c_str(), &attributes);
        ::LocalFree(descriptor);
        REQUIRE(created);
        handle.value = ::CreateFileW(path.c_str(), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | READ_CONTROL | SYNCHRONIZE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        REQUIRE(handle.value != INVALID_HANDLE_VALUE);
    }
};

std::span<const std::byte> payload(std::string_view text) {
    return std::as_bytes(std::span(text.data(), text.size()));
}
}
#endif
#if defined(_WIN32) && defined(FO4IDE_TEST_HOOKS)
namespace {
const fo4ide::plugin::detail::WindowsJournalDirectory* replacementDirectory = nullptr;
struct WorkerEnvironmentReset {
    static std::optional<std::wstring> read(const wchar_t* name) {
        ::SetLastError(ERROR_SUCCESS);
        const auto length = ::GetEnvironmentVariableW(name, nullptr, 0);
        if (!length) {
            if (::GetLastError() == ERROR_ENVVAR_NOT_FOUND) return std::nullopt;
            FAIL("worker probe environment must be absent or nonempty");
        }
        std::wstring value(length, L'\0');
        const auto copied = ::GetEnvironmentVariableW(name, value.data(), length);
        REQUIRE(copied < length);
        value.resize(copied);
        return value;
    }
    std::optional<std::wstring> root = read(L"FO4IDE_REPLACEMENT_PROBE_ROOT");
    std::optional<std::wstring> step = read(L"FO4IDE_REPLACEMENT_PROBE_STEP");
    ~WorkerEnvironmentReset() {
        ::SetEnvironmentVariableW(L"FO4IDE_REPLACEMENT_PROBE_ROOT", root ? root->c_str() : nullptr);
        ::SetEnvironmentVariableW(L"FO4IDE_REPLACEMENT_PROBE_STEP", step ? step->c_str() : nullptr);
    }
};
DWORD runReplacementWorker(std::wstring_view test, const std::filesystem::path& root, int phase) {
    std::array<wchar_t, 32768> executable{};
    const auto length = ::GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    REQUIRE(length > 0);
    REQUIRE(length < executable.size());
    WorkerEnvironmentReset environmentReset;
    REQUIRE(::SetEnvironmentVariableW(L"FO4IDE_REPLACEMENT_PROBE_ROOT", root.c_str()));
    REQUIRE(::SetEnvironmentVariableW(L"FO4IDE_REPLACEMENT_PROBE_STEP", std::to_wstring(phase).c_str()));
    auto command = std::wstring(L"\"") + executable.data() + L"\" \"" + std::wstring(test) + L"\" --reporter compact";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    REQUIRE(::CreateProcessW(executable.data(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
        nullptr, nullptr, &startup, &process));
    Handle processHandle{process.hProcess}, threadHandle{process.hThread};
    const auto wait = ::WaitForSingleObject(processHandle.value, 10000);
    if (wait != WAIT_OBJECT_0) {
        ::TerminateProcess(processHandle.value, 78);
        ::WaitForSingleObject(processHandle.value, 5000);
    }
    REQUIRE(wait == WAIT_OBJECT_0);
    DWORD exitCode = 0;
    REQUIRE(::GetExitCodeProcess(processHandle.value, &exitCode));
    ::SetEnvironmentVariableW(L"FO4IDE_REPLACEMENT_PROBE_ROOT", nullptr);
    ::SetEnvironmentVariableW(L"FO4IDE_REPLACEMENT_PROBE_STEP", nullptr);
    return exitCode;
}
}
#endif
#ifdef _WIN32
namespace {
fo4ide::plugin::detail::WindowsJournalReplacementRequest replacementRequest() {
    return {"0123456789abcdef0123456789abcdef", 0, "destination", "stage", "backup"};
}
fo4ide::plugin::detail::WindowsJournalReplacementRequest absentReplacementRequest() {
    return {"fedcba9876543210fedcba9876543210", 0, "destination", "stage", "backup"};
}
#ifdef FO4IDE_TEST_HOOKS
fo4ide::plugin::detail::WindowsJournalReplacementExpectation expectation(
    const fo4ide::plugin::detail::WindowsJournalDirectory& journal, bool original = true) {
    const auto originalBytes = payload("original");
    const auto finalBytes = payload(original ? "updated" : "created");
    return {journal.directoryIdentity(), original, original ? 8u : 0u,
        original ? fo4::io::sha256_hex(std::vector<std::byte>(originalBytes.begin(), originalBytes.end())) : "",
        7, fo4::io::sha256_hex(std::vector<std::byte>(finalBytes.begin(), finalBytes.end()))};
}
using Inventory = std::vector<std::pair<std::string, fo4ide::plugin::detail::WindowsJournalFileSnapshot>>;
Inventory inventory(const fo4ide::plugin::detail::WindowsJournalDirectory& journal) {
    auto names = journal.listFiles();
    std::sort(names.begin(), names.end());
    Inventory result;
    for (const auto& name : names) result.emplace_back(name, journal.snapshotFile(name, 16384));
    return result;
}
void requireUnchanged(const fo4ide::plugin::detail::WindowsJournalDirectory& journal, const Inventory& before) {
    const auto after = inventory(journal);
    REQUIRE(after.size() == before.size());
    for (std::size_t i = 0; i < before.size(); ++i) {
        CHECK(after[i].first == before[i].first);
        CHECK(after[i].second.identity == before[i].second.identity);
        CHECK(after[i].second.bytes == before[i].second.bytes);
    }
}
bool secondRecoveryBlocked = false;
void attemptSecondRecovery(fo4ide::plugin::detail::WindowsJournalIntentStep step) {
    if (step != fo4ide::plugin::detail::WindowsJournalIntentStep::intentWritten) return;
    std::thread other([] {
        try { replacementDirectory->resumeReplacement(replacementRequest()); }
        catch (const std::system_error& error) { secondRecoveryBlocked = error.code().value() == ERROR_LOCK_VIOLATION; }
    });
    other.join();
}
struct IntentHookReset {
    ~IntentHookReset() { fo4ide::plugin::detail::setWindowsJournalIntentHook(nullptr); }
};
#endif
}
#endif

// Detects: persisted intent must resume replacement without losing the backup or publishing the wrong bytes.
TEST_CASE("Windows persisted replacement resumes idempotently from validated intent") {
#if defined(_WIN32) && defined(FO4IDE_TEST_HOOKS)
    using namespace fo4ide::plugin::detail;
    const auto root = std::filesystem::current_path() / ".probe" / ("intent_basic_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Cleanup cleanup{root};
    std::filesystem::create_directories(root);
    PrivateRoot privateRoot(root);
    WindowsJournalDirectory journal(privateRoot.handle.value);
    journal.writeFile("destination", payload("original"));
    journal.writeFile("stage", payload("updated"));
    const auto request = replacementRequest();
    replacementDirectory = &journal;
    secondRecoveryBlocked = false;
    IntentHookReset reset;
    setWindowsJournalIntentHook(attemptSecondRecovery);
    journal.persistReplacementIntent(request, journal.snapshotFile("destination", 8), journal.snapshotFile("stage", 7));
    setWindowsJournalIntentHook(nullptr);
    CHECK(secondRecoveryBlocked);
    journal.resumeReplacement(request);
    CHECK(journal.readFile("destination", 7) == std::vector<std::byte>(payload("updated").begin(), payload("updated").end()));
    CHECK(journal.readFile("backup", 8) == std::vector<std::byte>(payload("original").begin(), payload("original").end()));
    CHECK_THROWS(journal.readFile("stage", 7));
    const auto firstFiles = journal.listFiles();
    CHECK_NOTHROW(journal.resumeReplacement(request));
    CHECK(journal.listFiles() == firstFiles);
#endif
}

// Detects: a legacy present-original intent must remain resumable after schema extension.
TEST_CASE("Windows persisted replacement resumes legacy present intent without originalExists") {
#if defined(_WIN32) && defined(FO4IDE_TEST_HOOKS)
    using namespace fo4ide::plugin::detail;
    const auto root = std::filesystem::current_path() / ".probe" / ("intent_legacy_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Cleanup cleanup{root};
    std::filesystem::create_directories(root);
    PrivateRoot privateRoot(root);
    WindowsJournalDirectory journal(privateRoot.handle.value);
    journal.writeFile("destination", payload("original"));
    journal.writeFile("stage", payload("updated"));
    const auto request = replacementRequest();
    const auto name = "wr-0123456789abcdef0123456789abcdef-0.intent";
    journal.persistReplacementIntent(request, journal.snapshotFile("destination", 8), journal.snapshotFile("stage", 7));
    auto document = nlohmann::json::parse(journal.readFile(name, 16384));
    document.erase("originalExists");
    journal.removeFile(name);
    journal.writeFile(name, payload(document.dump()));
    CHECK_NOTHROW(journal.resumeReplacement(request, expectation(journal)));
    CHECK(journal.readFile("destination", 7) == std::vector<std::byte>(payload("updated").begin(), payload("updated").end()));
    CHECK(journal.readFile("backup", 8) == std::vector<std::byte>(payload("original").begin(), payload("original").end()));
#endif
}

// Detects: invalid intent and occupied backups must leave original and staged files intact.
TEST_CASE("Windows persisted replacement refuses invalid intent and backup conflict") {
#if defined(_WIN32) && defined(FO4IDE_TEST_HOOKS)
    using namespace fo4ide::plugin::detail;
    const auto root = std::filesystem::current_path() / ".probe" / ("intent_conflict_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Cleanup cleanup{root};
    std::filesystem::create_directories(root);
    PrivateRoot privateRoot(root);
    WindowsJournalDirectory journal(privateRoot.handle.value);
    journal.writeFile("destination", payload("original"));
    journal.writeFile("stage", payload("updated"));
    const auto request = replacementRequest();
    journal.persistReplacementIntent(request, journal.snapshotFile("destination", 8), journal.snapshotFile("stage", 7));
    SECTION("truncated record") {
        journal.removeFile("wr-0123456789abcdef0123456789abcdef-0.intent");
        journal.writeFile("wr-0123456789abcdef0123456789abcdef-0.intent", payload("{"));
    }
    SECTION("occupied backup") { journal.writeFile("backup", payload("newcomer")); }
    SECTION("false original flag conflicts with original evidence") {
        const auto name = "wr-0123456789abcdef0123456789abcdef-0.intent";
        const auto stored = journal.readFile(name, 16384);
        auto document = nlohmann::json::parse(stored);
        document["originalExists"] = false;
        journal.removeFile(name);
        journal.writeFile(name, payload(document.dump()));
    }
    SECTION("wrong held directory identity") {
        const auto name = "wr-0123456789abcdef0123456789abcdef-0.intent";
        const auto stored = journal.readFile(name, 16384);
        auto document = nlohmann::json::parse(stored);
        document["directory"]["file"] = std::string(32, '0');
        journal.removeFile(name);
        journal.writeFile(name, payload(document.dump()));
    }
    SECTION("oversized content evidence") {
        const auto name = "wr-0123456789abcdef0123456789abcdef-0.intent";
        const auto stored = journal.readFile(name, 16384);
        auto document = nlohmann::json::parse(stored);
        document["original"]["size"] = kWindowsReplacementMaxFileBytes + 1;
        journal.removeFile(name);
        journal.writeFile(name, payload(document.dump()));
    }
    SECTION("duplicate fields are rejected") {
        const auto name = "wr-0123456789abcdef0123456789abcdef-0.intent";
        const auto stored = journal.readFile(name, 16384);
        std::string encoded(reinterpret_cast<const char*>(stored.data()), stored.size());
        encoded.pop_back();
        encoded += ",\"version\":1}";
        journal.removeFile(name);
        journal.writeFile(name, payload(encoded));
    }
    SECTION("stale original hash") {
        const auto name = "wr-0123456789abcdef0123456789abcdef-0.intent";
        const auto stored = journal.readFile(name, 16384);
        auto document = nlohmann::json::parse(stored);
        document["original"]["sha256"] = std::string(64, '0');
        journal.removeFile(name);
        journal.writeFile(name, payload(document.dump()));
    }
    SECTION("premature completion marker") {
        journal.writeFile("wr-0123456789abcdef0123456789abcdef-0.done", payload("completed"));
    }
    SECTION("independent destination disagreement") {
        auto other = request;
        other.destination = "other";
        CHECK_THROWS(journal.resumeReplacement(other));
        return;
    }
    CHECK_THROWS(journal.resumeReplacement(request));
    CHECK(journal.readFile("destination", 8) == std::vector<std::byte>(payload("original").begin(), payload("original").end()));
    CHECK(journal.readFile("stage", 7) == std::vector<std::byte>(payload("updated").begin(), payload("updated").end()));
#endif
}

#if defined(_WIN32) && defined(FO4IDE_TEST_HOOKS)
namespace {
int intentCrashPhase() {
    wchar_t value[16]{};
    const auto length = ::GetEnvironmentVariableW(L"FO4IDE_REPLACEMENT_PROBE_STEP", value, 16);
    if (length == 0 || length >= 16) throw std::runtime_error("invalid intent worker phase");
    return std::stoi(value);
}
void terminateIntentWorker(fo4ide::plugin::detail::WindowsJournalIntentStep step) {
    constexpr int phases[]{0, 1, 5, 6};
    if (intentCrashPhase() == phases[static_cast<unsigned>(step)]) ::TerminateProcess(::GetCurrentProcess(), 77);
}
void terminateIntentRenameWorker(fo4ide::plugin::detail::WindowsJournalReplacementStep step) {
    if (intentCrashPhase() == static_cast<int>(step) + 2) ::TerminateProcess(::GetCurrentProcess(), 77);
}
}
#endif

// Detects: fresh-process recovery must stop at the injected publication boundary.
TEST_CASE("Windows persisted intent worker", "[.intent-worker]") {
#if defined(_WIN32) && defined(FO4IDE_TEST_HOOKS)
    using namespace fo4ide::plugin::detail;
    std::array<wchar_t, 32768> path{};
    const auto length = ::GetEnvironmentVariableW(L"FO4IDE_REPLACEMENT_PROBE_ROOT", path.data(), static_cast<DWORD>(path.size()));
    REQUIRE(length > 0);
    REQUIRE(length < path.size());
    Handle authorized{::CreateFileW(path.data(), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | READ_CONTROL | SYNCHRONIZE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    REQUIRE(authorized.value != INVALID_HANDLE_VALUE);
    WindowsJournalDirectory journal(authorized.value);
    const auto request = replacementRequest();
    if (intentCrashPhase() == -1) {
        CHECK_NOTHROW(journal.resumeReplacement(request, expectation(journal, request.transaction == replacementRequest().transaction)));
        return;
    }
    setWindowsJournalIntentHook(terminateIntentWorker);
    setWindowsJournalReplacementHook(terminateIntentRenameWorker);
    journal.persistReplacementIntent(request, journal.snapshotFile("destination", 8), journal.snapshotFile("stage", 7));
    journal.resumeReplacement(request);
    FAIL("intent worker did not terminate at requested boundary");
#endif
}

// Detects: each interrupted publication boundary must roll forward safely after restart.
TEST_CASE("Windows persisted replacement recovers in a fresh process after every publication boundary") {
#if defined(_WIN32) && defined(FO4IDE_TEST_HOOKS)
    using namespace fo4ide::plugin::detail;
    for (int phase = 0; phase < 7; ++phase) {
        CAPTURE(phase);
        const auto root = std::filesystem::current_path() / ".probe" / ("intent_restart_" + std::to_string(phase) + "_" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        Cleanup cleanup{root};
        std::filesystem::create_directories(root);
        PrivateRoot privateRoot(root);
        WindowsJournalDirectory journal(privateRoot.handle.value);
        journal.writeFile("destination", payload("original"));
        journal.writeFile("stage", payload("updated"));
        const auto original = journal.snapshotFile("destination", 8);
        const auto replacement = journal.snapshotFile("stage", 7);
        REQUIRE(runReplacementWorker(L"Windows persisted intent worker", privateRoot.path, phase) == 77);
        journal.removeFile(".windows-replacement.lock");
        const auto beforeInspection = inventory(journal);
        const auto expectedState = phase < 3 ? WindowsJournalReplacementState::prepared :
            phase == 3 ? WindowsJournalReplacementState::backedUp : WindowsJournalReplacementState::applied;
        CHECK(journal.inspectReplacement(replacementRequest(), expectation(journal)) == expectedState);
        requireUnchanged(journal, beforeInspection);
        REQUIRE(runReplacementWorker(L"Windows persisted intent worker", privateRoot.path, -1) == 0);
        CHECK(journal.snapshotFile("destination", 7).identity == replacement.identity);
        CHECK(journal.readFile("destination", 7) == replacement.bytes);
        CHECK(journal.snapshotFile("backup", 8).identity == original.identity);
        CHECK(journal.readFile("backup", 8) == original.bytes);
        CHECK_THROWS(journal.readFile("stage", 7));
        CHECK_NOTHROW(journal.readFile("wr-0123456789abcdef0123456789abcdef-0.intent", 16384));
        CHECK_NOTHROW(journal.readFile("wr-0123456789abcdef0123456789abcdef-0.done", 16384));
        const auto completedFiles = journal.listFiles();
        CHECK_NOTHROW(journal.resumeReplacement(replacementRequest()));
        CHECK(journal.listFiles() == completedFiles);
    }
#endif
}

// Detects: conflicting newcomers and backups must survive recovery without overwrite.
TEST_CASE("Windows persisted replacement preserves newcomers and backup conflicts after restart") {
#if defined(_WIN32) && defined(FO4IDE_TEST_HOOKS)
    using namespace fo4ide::plugin::detail;
    const auto root = std::filesystem::current_path() / ".probe" / ("intent_restart_conflict_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Cleanup cleanup{root};
    std::filesystem::create_directories(root);
    PrivateRoot privateRoot(root);
    WindowsJournalDirectory journal(privateRoot.handle.value);
    journal.writeFile("destination", payload("original"));
    journal.writeFile("stage", payload("updated"));
    bool stageExpected = true;
    SECTION("newcomer after original was backed up") {
        REQUIRE(runReplacementWorker(L"Windows persisted intent worker", privateRoot.path, 3) == 77);
        journal.writeFile("destination", payload("newcomer"));
        CHECK_THROWS(journal.resumeReplacement(replacementRequest()));
        CHECK(journal.readFile("destination", 8) == std::vector<std::byte>(payload("newcomer").begin(), payload("newcomer").end()));
        CHECK(journal.readFile("backup", 8) == std::vector<std::byte>(payload("original").begin(), payload("original").end()));
    }
    SECTION("backup collision before original was moved") {
        REQUIRE(runReplacementWorker(L"Windows persisted intent worker", privateRoot.path, 1) == 77);
        journal.writeFile("backup", payload("newcomer"));
        CHECK_THROWS(journal.resumeReplacement(replacementRequest()));
        CHECK(journal.readFile("destination", 8) == std::vector<std::byte>(payload("original").begin(), payload("original").end()));
        CHECK(journal.readFile("backup", 8) == std::vector<std::byte>(payload("newcomer").begin(), payload("newcomer").end()));
    }
    SECTION("truncated completion stage preserves applied output and original") {
        REQUIRE(runReplacementWorker(L"Windows persisted intent worker", privateRoot.path, 5) == 77);
        journal.removeFile("wr-0123456789abcdef0123456789abcdef-0.done.stage");
        journal.writeFile("wr-0123456789abcdef0123456789abcdef-0.done.stage", payload("{"));
        CHECK_THROWS(journal.resumeReplacement(replacementRequest()));
        CHECK(journal.readFile("destination", 7) == std::vector<std::byte>(payload("updated").begin(), payload("updated").end()));
        CHECK(journal.readFile("backup", 8) == std::vector<std::byte>(payload("original").begin(), payload("original").end()));
        stageExpected = false;
    }
    if (stageExpected) CHECK(journal.readFile("stage", 7) == std::vector<std::byte>(payload("updated").begin(), payload("updated").end()));
    else CHECK_THROWS(journal.readFile("stage", 7));
    CHECK_THROWS(journal.readFile("wr-0123456789abcdef0123456789abcdef-0.done", 16384));
#endif
}

#if defined(_WIN32) && defined(FO4IDE_TEST_HOOKS)
namespace {
void terminateAbsentIntentWorker(fo4ide::plugin::detail::WindowsJournalIntentStep step) {
    if (step == fo4ide::plugin::detail::WindowsJournalIntentStep::replacementPublished) {
        if (intentCrashPhase() == 4) ::TerminateProcess(::GetCurrentProcess(), 77);
        return;
    }
    constexpr int phases[]{0, 1, 5, 6};
    if (intentCrashPhase() == phases[static_cast<unsigned>(step)]) ::TerminateProcess(::GetCurrentProcess(), 77);
}
}
#endif

// Detects: the absent-original child worker must stop at the injected boundary.
TEST_CASE("Windows absent-original worker", "[.intent-absent-worker]") {
#if defined(_WIN32) && defined(FO4IDE_TEST_HOOKS)
    using namespace fo4ide::plugin::detail;
    std::array<wchar_t, 32768> path{};
    const auto length = ::GetEnvironmentVariableW(L"FO4IDE_REPLACEMENT_PROBE_ROOT", path.data(), static_cast<DWORD>(path.size()));
    REQUIRE(length > 0);
    REQUIRE(length < path.size());
    Handle authorized{::CreateFileW(path.data(), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | READ_CONTROL | SYNCHRONIZE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    REQUIRE(authorized.value != INVALID_HANDLE_VALUE);
    WindowsJournalDirectory journal(authorized.value);
    const auto request = absentReplacementRequest();
    if (intentCrashPhase() == -1) {
        CHECK_NOTHROW(journal.resumeReplacement(request, expectation(journal, request.transaction == replacementRequest().transaction)));
        return;
    }
    setWindowsJournalIntentHook(terminateAbsentIntentWorker);
    journal.persistReplacementIntent(request, std::nullopt, journal.snapshotFile("stage", 7));
    journal.resumeReplacement(request);
    FAIL("absent-original worker did not terminate at requested boundary");
#endif
}

// Detects: every absent-original publication boundary must recover exactly once after restart.
TEST_CASE("Windows absent original publishes without overwrite and resumes after process interruption") {
#if defined(_WIN32) && defined(FO4IDE_TEST_HOOKS)
    using namespace fo4ide::plugin::detail;
    for (const auto phase : {0, 1, 4, 5, 6}) {
        CAPTURE(phase);
        const auto root = std::filesystem::current_path() / ".probe" / ("intent_absent_restart_" + std::to_string(phase) + "_" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        Cleanup cleanup{root};
        std::filesystem::create_directories(root);
        PrivateRoot privateRoot(root);
        WindowsJournalDirectory journal(privateRoot.handle.value);
        journal.writeFile("stage", payload("created"));
        CHECK_THROWS(journal.readFile("destination", 1));
        const auto replacement = journal.snapshotFile("stage", 7);
        REQUIRE(runReplacementWorker(L"Windows absent-original worker", privateRoot.path, phase) == 77);
        journal.removeFile(".windows-replacement.lock");
        const auto beforeInspection = inventory(journal);
        const auto expectedState = phase < 4 ? WindowsJournalReplacementState::prepared : WindowsJournalReplacementState::applied;
        CHECK(journal.inspectReplacement(absentReplacementRequest(), expectation(journal, false)) == expectedState);
        requireUnchanged(journal, beforeInspection);
        REQUIRE(runReplacementWorker(L"Windows absent-original worker", privateRoot.path, -1) == 0);
        CHECK(journal.snapshotFile("destination", 7).identity == replacement.identity);
        CHECK(journal.readFile("destination", 7) == replacement.bytes);
        CHECK_THROWS(journal.readFile("stage", 7));
        CHECK_THROWS(journal.readFile("backup", 8));
        const auto completedFiles = journal.listFiles();
        CHECK_NOTHROW(journal.resumeReplacement(absentReplacementRequest()));
        CHECK(journal.listFiles() == completedFiles);
    }
#endif
}

// Detects: absent-original persistence must reject conflicts and preserve unexpected destination bytes.
TEST_CASE("Windows absent original retains unexpected destination and refuses backup collisions") {
#if defined(_WIN32) && defined(FO4IDE_TEST_HOOKS)
    using namespace fo4ide::plugin::detail;
    const auto root = std::filesystem::current_path() / ".probe" / ("intent_absent_conflict_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Cleanup cleanup{root};
    std::filesystem::create_directories(root);
    PrivateRoot privateRoot(root);
    WindowsJournalDirectory journal(privateRoot.handle.value);
    journal.writeFile("stage", payload("created"));
    const auto request = absentReplacementRequest();
    SECTION("destination exists before intent") {
        journal.writeFile("destination", payload("newcomer"));
        CHECK_THROWS(journal.persistReplacementIntent(request, std::nullopt, journal.snapshotFile("stage", 7)));
        CHECK(journal.readFile("destination", 8) == std::vector<std::byte>(payload("newcomer").begin(), payload("newcomer").end()));
    }
    SECTION("backup exists before intent") {
        journal.writeFile("backup", payload("newcomer"));
        CHECK_THROWS(journal.persistReplacementIntent(request, std::nullopt, journal.snapshotFile("stage", 7)));
        CHECK(journal.readFile("backup", 8) == std::vector<std::byte>(payload("newcomer").begin(), payload("newcomer").end()));
    }
    SECTION("destination appears after intent") {
        REQUIRE(runReplacementWorker(L"Windows absent-original worker", privateRoot.path, 1) == 77);
        journal.writeFile("destination", payload("newcomer"));
        CHECK_THROWS(journal.resumeReplacement(request));
        CHECK(journal.readFile("destination", 8) == std::vector<std::byte>(payload("newcomer").begin(), payload("newcomer").end()));
        CHECK(journal.readFile("stage", 7) == std::vector<std::byte>(payload("created").begin(), payload("created").end()));
    }
#endif
}

TEST_CASE("Windows intent inspection binds caller manifest evidence before replacement") {
#if defined(_WIN32) && defined(FO4IDE_TEST_HOOKS)
    using namespace fo4ide::plugin::detail;
    const auto root = std::filesystem::current_path() / ".probe" / ("intent_binding_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Cleanup cleanup{root};
    std::filesystem::create_directories(root);
    PrivateRoot privateRoot(root);
    WindowsJournalDirectory journal(privateRoot.handle.value);
    journal.writeFile("destination", payload("original"));
    journal.writeFile("stage", payload("updated"));
    auto request = replacementRequest();
    journal.persistReplacementIntent(request, journal.snapshotFile("destination", 8), journal.snapshotFile("stage", 7));
    auto expected = expectation(journal);
    const auto before = inventory(journal);
    SECTION("original hash") { expected.originalHash = std::string(64, '0'); }
    SECTION("original size") { ++expected.originalSize; }
    SECTION("original exists") { expected.originalExists = false; expected.originalHash.clear(); expected.originalSize = 0; }
    SECTION("final hash") { expected.finalHash = std::string(64, '0'); }
    SECTION("final size") { ++expected.finalSize; }
    SECTION("directory high identity byte") { expected.directory.file[15] ^= std::byte{1}; }
    SECTION("directory volume") { ++expected.directory.volume; }
    SECTION("transaction") { request.transaction[0] = '9'; }
    SECTION("entry") { ++request.entry; }
    SECTION("destination") { request.destination = "another"; }
    SECTION("staged") { request.staged = "another"; }
    SECTION("backup") { request.backup = "another"; }
    CHECK_THROWS(journal.inspectReplacement(request, expected));
    requireUnchanged(journal, before);
    CHECK_THROWS(journal.resumeReplacement(request, expected));
    requireUnchanged(journal, before);
#endif
}

TEST_CASE("Windows independent intent inspections preserve artifacts when the second entry conflicts") {
#if defined(_WIN32) && defined(FO4IDE_TEST_HOOKS)
    using namespace fo4ide::plugin::detail;
    const auto root = std::filesystem::current_path() / ".probe" / ("intent_batch_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Cleanup cleanup{root};
    std::filesystem::create_directories(root);
    PrivateRoot privateRoot(root);
    WindowsJournalDirectory journal(privateRoot.handle.value);
    const auto first = replacementRequest();
    auto second = first;
    second.entry = 1; second.destination = "second"; second.staged = "second.stage"; second.backup = "second.backup";
    journal.writeFile(first.destination, payload("original"));
    journal.writeFile(first.staged, payload("updated"));
    journal.writeFile(second.staged, payload("created"));
    journal.persistReplacementIntent(first, journal.snapshotFile(first.destination, 8), journal.snapshotFile(first.staged, 7));
    journal.persistReplacementIntent(second, std::nullopt, journal.snapshotFile(second.staged, 7));
    const auto expectedFirst = expectation(journal);
    auto expectedSecond = expectation(journal, false);
    SECTION("later manifest mismatch") { expectedSecond.finalHash = std::string(64, '0'); }
    SECTION("later destination newcomer") { journal.writeFile(second.destination, payload("newcomer")); }
    journal.removeFile(".windows-replacement.lock");
    const auto before = inventory(journal);
    CHECK(journal.inspectReplacement(first, expectedFirst) == WindowsJournalReplacementState::prepared);
    CHECK_THROWS(journal.inspectReplacement(second, expectedSecond));
    requireUnchanged(journal, before);
#endif
}

TEST_CASE("Windows private intent inspection binds a separate ordinary ACL destination without writes") {
#if defined(_WIN32) && defined(FO4IDE_TEST_HOOKS)
    using namespace fo4ide::plugin::detail;
    Handle token;
    REQUIRE(::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token.value));
    std::array<wchar_t, 32768> profile{};
    DWORD profileLength = static_cast<DWORD>(profile.size());
    REQUIRE(::GetUserProfileDirectoryW(token.value, profile.data(), &profileLength));
    const auto root = std::filesystem::path(profile.data()) / ("intent_separate_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    REQUIRE(::CreateDirectoryW(root.c_str(), nullptr));
    Cleanup cleanup{root};
    REQUIRE(::CreateDirectoryW((root / "data").c_str(), nullptr));
    PrivateRoot privateRoot(root);
    PrivateRoot dataRoot(root / "data", true);
    WindowsJournalDirectory journal(privateRoot.handle.value);
    CHECK_THROWS(WindowsJournalDirectory(dataRoot.handle.value));
    const auto request = replacementRequest();
    bool originalExists = true;
    bool persistenceConflict = false;
    bool inspectionConflict = false;
    bool copiedStore = false;
    std::string heldName = request.destination;
    auto state = WindowsJournalReplacementState::prepared;
    SECTION("prepared") {}
    SECTION("record copied to a different private store") { copiedStore = true; }
    SECTION("backed up") { state = WindowsJournalReplacementState::backedUp; }
    SECTION("applied") { state = WindowsJournalReplacementState::applied; }
    SECTION("absent prepared") { originalExists = false; }
    SECTION("absent applied") { originalExists = false; state = WindowsJournalReplacementState::applied; }
    SECTION("full directory identity disagreement") { inspectionConflict = true; }
    SECTION("persistence manifest hash disagreement") { persistenceConflict = true; }
    SECTION("bound destination name disagreement") { heldName = "different"; persistenceConflict = true; }
    auto expected = expectation(journal);
    FILE_ID_INFO directory{};
    REQUIRE(::GetFileInformationByHandleEx(dataRoot.handle.value, FileIdInfo, &directory, sizeof(directory)));
    expected.directory.volume = directory.VolumeSerialNumber;
    std::memcpy(expected.directory.file.data(), directory.FileId.Identifier, expected.directory.file.size());
    if (!originalExists) { expected.originalExists = false; expected.originalHash.clear(); expected.originalSize = 0; }
    for (const auto& file : {std::string("destination"), std::string("stage")}) {
        if (file == "destination" && !originalExists) continue;
        const auto bytes = payload(file == "destination" ? "original" : "updated");
        {
            std::ofstream output(dataRoot.path / file, std::ios::binary);
            output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            REQUIRE(output.good());
        }
        Handle actual{::CreateFileW((dataRoot.path / file).c_str(), GENERIC_READ | READ_CONTROL,
            FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
        REQUIRE(actual.value != INVALID_HANDLE_VALUE);
        PACL dacl = nullptr;
        PSECURITY_DESCRIPTOR security = nullptr;
        REQUIRE(::GetSecurityInfo(actual.value, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
            nullptr, nullptr, &dacl, nullptr, &security) == ERROR_SUCCESS);
        REQUIRE(dacl != nullptr);
        bool inheritedWorldRead = false;
        ACCESS_MASK worldAllowed = 0;
        for (DWORD i = 0; i < dacl->AceCount; ++i) {
            void* entry = nullptr;
            REQUIRE(::GetAce(dacl, i, &entry));
            const auto* ace = static_cast<const ACCESS_ALLOWED_ACE*>(entry);
            if (ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE &&
                ::IsWellKnownSid(const_cast<DWORD*>(&ace->SidStart), WinWorldSid)) {
                auto mask = ace->Mask;
                GENERIC_MAPPING mapping{FILE_GENERIC_READ, FILE_GENERIC_WRITE, FILE_GENERIC_EXECUTE, FILE_ALL_ACCESS};
                ::MapGenericMask(&mask, &mapping);
                worldAllowed |= mask;
                inheritedWorldRead |= (ace->Header.AceFlags & INHERITED_ACE) && (mask & FILE_READ_DATA);
            }
        }
        ::LocalFree(security);
        REQUIRE(inheritedWorldRead);
        REQUIRE((worldAllowed & (FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES | DELETE | WRITE_DAC | WRITE_OWNER)) == 0);
    }
    const auto before = inventory(journal);
    const auto identity = JournalDestination::captureRoot(dataRoot.path);
    const auto names = JournalDestination::listDirectory(dataRoot.path, {}, identity);
    const auto dataInventory = [&] {
        Inventory files;
        for (const auto& file : JournalDestination::listDirectory(dataRoot.path, {}, identity)) {
            JournalDestination captured(dataRoot.path, {}, file, false, identity);
            Handle actual{::CreateFileW((dataRoot.path / file).c_str(), GENERIC_READ, FILE_SHARE_READ,
                nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
            REQUIRE(actual.value != INVALID_HANDLE_VALUE);
            FILE_ID_INFO id{};
            REQUIRE(::GetFileInformationByHandleEx(actual.value, FileIdInfo, &id, sizeof(id)));
            WindowsJournalFileSnapshot snapshot;
            snapshot.identity.volume = id.VolumeSerialNumber;
            std::memcpy(snapshot.identity.file.data(), id.FileId.Identifier, snapshot.identity.file.size());
            snapshot.bytes = *captured.read(16384);
            files.emplace_back(file, std::move(snapshot));
        }
        return files;
    };
    const auto requireDataUnchanged = [&](const Inventory& expectedFiles) {
        const auto actual = dataInventory();
        REQUIRE(actual.size() == expectedFiles.size());
        for (std::size_t i = 0; i < actual.size(); ++i) {
            CHECK(actual[i].first == expectedFiles[i].first);
            CHECK(actual[i].second.identity == expectedFiles[i].second.identity);
            CHECK(actual[i].second.bytes == expectedFiles[i].second.bytes);
        }
    };
    if (originalExists) {
        Handle original{::CreateFileW((dataRoot.path / "destination").c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr)};
        Handle stage{::CreateFileW((dataRoot.path / "stage").c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr)};
        FILETIME creation{}; REQUIRE(::GetFileTime(original.value, &creation, nullptr, nullptr));
        REQUIRE(::SetFileTime(stage.value, &creation, nullptr, nullptr));
    }
    const auto dataBefore = dataInventory();
    { JournalDestination approved(dataRoot.path, {}, originalExists ? "destination" : "stage", false, identity);
      expected.metadata = approved.metadata(); }
    {
        JournalDestination destination(dataRoot.path, {}, heldName, false, identity);
        if (persistenceConflict) {
            if (heldName == request.destination) expected.finalHash = std::string(64, '0');
            CHECK_THROWS(journal.persistReplacementIntent(request, expected, destination));
            requireUnchanged(journal, before);
        } else {
            journal.persistReplacementIntent(request, expected, destination);
        }
    }
    CHECK(JournalDestination::listDirectory(dataRoot.path, {}, identity) == names);
    requireDataUnchanged(dataBefore);
    if (persistenceConflict) return;
    journal.removeFile(".windows-replacement.lock");
    if (state != WindowsJournalReplacementState::prepared && originalExists)
        std::filesystem::rename(dataRoot.path / "destination", dataRoot.path / "backup");
    if (state == WindowsJournalReplacementState::applied)
        std::filesystem::rename(dataRoot.path / "stage", dataRoot.path / "destination");
    if (inspectionConflict) expected.directory.file[15] ^= std::byte{1};
    const auto recordsBeforeInspection = inventory(journal);
    const auto filesBeforeInspection = dataInventory();
    JournalDestination destination(dataRoot.path, {}, request.destination, false, identity);
    if (copiedStore) {
        REQUIRE(::CreateDirectoryW((root / "copied").c_str(), nullptr));
        PrivateRoot otherRoot(root / "copied");
        WindowsJournalDirectory other(otherRoot.handle.value);
        for (const auto& [file, snapshot] : recordsBeforeInspection) other.writeFile(file, snapshot.bytes);
        const auto otherBefore = inventory(other);
        CHECK_THROWS(other.inspectReplacement(request, expected, destination));
        requireUnchanged(other, otherBefore);
    } else if (inspectionConflict) CHECK_THROWS(journal.inspectReplacement(request, expected, destination));
    else CHECK(journal.inspectReplacement(request, expected, destination) == state);
    requireUnchanged(journal, recordsBeforeInspection);
    requireDataUnchanged(filesBeforeInspection);
#endif
}
