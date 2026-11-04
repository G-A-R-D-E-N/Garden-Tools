#pragma once
#include <array>
#include <chrono>
#include <span>
#include <optional>
#include <tuple>
#include <fo4/io/sha256.hpp>
#include <nlohmann/json.hpp>
#include "../../src/plugin/RadiclePluginJournalRoot.hpp"
#include "../../src/plugin/RadiclePluginJournalDestination.hpp"
#include "../../src/plugin/RadiclePluginWindowsJournalDirectory.hpp"
#include "../../src/plugin/RadiclePluginWindowsJournalIntent.hpp"
#include "../../src/plugin/RadiclePluginWindowsJournalNative.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <catch2/catch_test_macros.hpp>
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
}
#endif
#if defined(_WIN32) && defined(FO4IDE_TEST_HOOKS)
namespace {
using namespace fo4ide::plugin::detail;
std::span<const std::byte> splitBytes(std::string_view value) { return std::as_bytes(std::span(value.data(), value.size())); }
void writeSplitFile(const std::filesystem::path& path, std::string_view text) {
    std::ofstream file(path, std::ios::binary);
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    REQUIRE(file.good());
}
std::vector<std::byte> readSplitFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    REQUIRE(file.good());
    const std::string bytes((std::istreambuf_iterator<char>(file)), {});
    return {reinterpret_cast<const std::byte*>(bytes.data()), reinterpret_cast<const std::byte*>(bytes.data() + bytes.size())};
}
WindowsJournalFileIdentity splitIdentity(const std::filesystem::path& path, bool directory = false) {
    Handle file{::CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | (directory ? FILE_FLAG_BACKUP_SEMANTICS : 0), nullptr)};
    REQUIRE(file.value != INVALID_HANDLE_VALUE);
    FILE_ID_INFO id{};
    REQUIRE(::GetFileInformationByHandleEx(file.value, FileIdInfo, &id, sizeof(id)));
    WindowsJournalFileIdentity result;
    result.volume = id.VolumeSerialNumber;
    std::memcpy(result.file.data(), id.FileId.Identifier, result.file.size());
    return result;
}
extern "C" NTSYSAPI NTSTATUS NTAPI NtSetEaFile(HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG);
extern "C" NTSYSAPI NTSTATUS NTAPI NtQueryEaFile(HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, BOOLEAN, PVOID, ULONG, PULONG, BOOLEAN);
void setSplitEa(const std::filesystem::path& path) {
    Handle file{::CreateFileW(path.c_str(), FILE_WRITE_EA, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr)};
    REQUIRE(file.value != INVALID_HANDLE_VALUE);
    struct Ea { ULONG next{}; UCHAR flags{}; UCHAR nameLength{4}; USHORT valueLength{4}; char data[9]{'T','E','S','T',0,'k','e','e','p'}; } ea;
    IO_STATUS_BLOCK status{};
    REQUIRE(NtSetEaFile(file.value, &status, &ea, offsetof(Ea, data) + sizeof(ea.data)) >= 0);
}
std::vector<std::byte> readSplitEa(const std::filesystem::path& path) {
    Handle file{::CreateFileW(path.c_str(), FILE_READ_EA, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr)};
    REQUIRE(file.value != INVALID_HANDLE_VALUE);
    std::vector<std::byte> bytes(256);
    IO_STATUS_BLOCK status{};
    REQUIRE(NtQueryEaFile(file.value, &status, bytes.data(), static_cast<ULONG>(bytes.size()), FALSE, nullptr, 0, nullptr, TRUE) >= 0);
    REQUIRE(status.Information <= bytes.size());
    bytes.resize(status.Information);
    return bytes;
}
std::int64_t splitCreation(const std::filesystem::path& path, bool directory = false) {
    Handle file{::CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | (directory ? FILE_FLAG_BACKUP_SEMANTICS : 0), nullptr)};
    REQUIRE(file.value != INVALID_HANDLE_VALUE);
    FILE_BASIC_INFO info{};
    REQUIRE(::GetFileInformationByHandleEx(file.value, FileBasicInfo, &info, sizeof(info)));
    return info.CreationTime.QuadPart;
}
void setSplitCreation(const std::filesystem::path& path, std::int64_t value) {
    Handle file{::CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr)};
    REQUIRE(file.value != INVALID_HANDLE_VALUE);
    ULARGE_INTEGER time{}; time.QuadPart = static_cast<ULONGLONG>(value);
    FILETIME creation{time.LowPart, time.HighPart};
    REQUIRE(::SetFileTime(file.value, &creation, nullptr, nullptr));
}
void resetToInheritedDacl(const std::filesystem::path& path) {
    ACL empty{};
    REQUIRE(::InitializeAcl(&empty, sizeof(empty), ACL_REVISION));
    REQUIRE(::SetNamedSecurityInfoW(const_cast<wchar_t*>(path.c_str()), SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION | UNPROTECTED_DACL_SECURITY_INFORMATION, nullptr, nullptr, &empty, nullptr) == ERROR_SUCCESS);
}
std::wstring splitSecurity(const std::filesystem::path& path) {
    PSECURITY_DESCRIPTOR security = nullptr;
    REQUIRE(::GetNamedSecurityInfoW(path.c_str(), SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION | LABEL_SECURITY_INFORMATION,
        nullptr, nullptr, nullptr, nullptr, &security) == ERROR_SUCCESS);
    wchar_t* text = nullptr;
    const auto converted = ::ConvertSecurityDescriptorToStringSecurityDescriptorW(security, SDDL_REVISION_1,
        OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION | SACL_SECURITY_INFORMATION | LABEL_SECURITY_INFORMATION, &text, nullptr);
    ::LocalFree(security);
    REQUIRE(converted);
    const std::wstring result(text);
    ::LocalFree(text);
    return result;
}
std::filesystem::path makeSplitRoot() {
    Handle token;
    REQUIRE(::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token.value));
    std::array<wchar_t, 32768> profile{};
    DWORD length = static_cast<DWORD>(profile.size());
    REQUIRE(::GetUserProfileDirectoryW(token.value, profile.data(), &length));
    const auto path = std::filesystem::path(profile.data()) / ("journal_split_apply_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    REQUIRE(::CreateDirectoryW(path.c_str(), nullptr));
    return path;
}
void makeSplitData(const std::filesystem::path& path) {
    Handle token;
    REQUIRE(::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token.value));
    DWORD size = 0;
    ::GetTokenInformation(token.value, TokenUser, nullptr, 0, &size);
    std::vector<std::uintptr_t> buffer((size + sizeof(std::uintptr_t) - 1) / sizeof(std::uintptr_t));
    REQUIRE(::GetTokenInformation(token.value, TokenUser, buffer.data(), size, &size));
    wchar_t* sid = nullptr;
    REQUIRE(::ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid, &sid));
    const auto sddl = std::wstring(L"O:") + sid + L"D:P(A;OICI;FA;;;" + sid + L")(A;OICI;GR;;;WD)";
    ::LocalFree(sid);
    PSECURITY_DESCRIPTOR security = nullptr;
    REQUIRE(::ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &security, nullptr));
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), security, FALSE};
    const auto created = ::CreateDirectoryW(path.c_str(), &attributes);
    ::LocalFree(security);
    REQUIRE(created);
}
WindowsJournalReplacementRequest splitRequest() { return {"abcdef0123456789abcdef0123456789", 0, "destination", "stage", "backup"}; }
WindowsJournalReplacementExpectation splitExpected(const std::filesystem::path& data, bool original) {
    const auto originalBytes = splitBytes("original"), finalBytes = splitBytes("updated");
    WindowsJournalReplacementExpectation result{splitIdentity(data, true), original, original ? 8u : 0u,
        original ? fo4::io::sha256_hex(std::vector<std::byte>(originalBytes.begin(), originalBytes.end())) : "",
        7, fo4::io::sha256_hex(std::vector<std::byte>(finalBytes.begin(), finalBytes.end()))};
    JournalDestination approved(data, {}, original ? "destination" : "stage");
    result.metadata = approved.metadata();
    return result;
}
std::wstring splitEnvironment(const wchar_t* name) {
    std::array<wchar_t, 32768> value{};
    const auto length = ::GetEnvironmentVariableW(name, value.data(), static_cast<DWORD>(value.size()));
    REQUIRE(length > 0);
    REQUIRE(length < value.size());
    return value.data();
}
int splitCrashPhase = -1;
void splitCrash(WindowsJournalIntentStep step) {
    int phase = -2;
    if (step == WindowsJournalIntentStep::originalBackedUp) phase = 1;
    if (step == WindowsJournalIntentStep::replacementPublished) phase = 2;
    if (step == WindowsJournalIntentStep::doneWritten) phase = 3;
    if (step == WindowsJournalIntentStep::donePublished) phase = 4;
    if (phase == splitCrashPhase) ::TerminateProcess(::GetCurrentProcess(), 77);
}
DWORD runSplitWorker(const std::filesystem::path& root, bool original, int phase,
    std::wstring_view test = L"Windows split-store recovery worker") {
    std::array<wchar_t, 32768> executable{};
    REQUIRE(::GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size())));
    auto command = std::wstring(L"\"") + executable.data() + L"\" \"" + std::wstring(test) + L"\" --reporter compact";
    std::wstring environment = L"FO4IDE_SPLIT_ROOT=" + root.native();
    environment.push_back(L'\0');
    environment += L"FO4IDE_SPLIT_ORIGINAL=" + std::wstring(original ? L"1" : L"0");
    environment.push_back(L'\0');
    environment += L"FO4IDE_SPLIT_PHASE=" + std::to_wstring(phase);
    environment.push_back(L'\0'); environment.push_back(L'\0');
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    REQUIRE(::CreateProcessW(executable.data(), command.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, environment.data(), nullptr, &startup, &process));
    Handle processHandle{process.hProcess}, threadHandle{process.hThread};
    const auto wait = ::WaitForSingleObject(processHandle.value, 15000);
    if (wait != WAIT_OBJECT_0) { ::TerminateProcess(processHandle.value, 78); ::WaitForSingleObject(processHandle.value, 5000); }
    REQUIRE(wait == WAIT_OBJECT_0);
    DWORD code = 0;
    REQUIRE(::GetExitCodeProcess(processHandle.value, &code));
    return code;
}
}
#endif
