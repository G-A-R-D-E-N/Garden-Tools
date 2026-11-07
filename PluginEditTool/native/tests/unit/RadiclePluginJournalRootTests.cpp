#include <algorithm>
#include "../helpers/TempRoot.hpp"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "../../src/plugin/RadiclePluginJournalLock.hpp"
#include "../../src/plugin/RadiclePluginJournalRoot.hpp"
#include "../../src/plugin/RadiclePluginJournalSession.hpp"
#include "../../src/plugin/RadiclePluginWindowsJournalDirectory.hpp"
#include <fo4/io/sha256.hpp>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>
#include <sddl.h>
#include <userenv.h>

namespace {
struct Handle {
    HANDLE value{INVALID_HANDLE_VALUE};
    ~Handle() { if (value != INVALID_HANDLE_VALUE && value != nullptr) ::CloseHandle(value); }
};

std::wstring currentUserSid() {
    Handle token;
    REQUIRE(::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token.value));
    DWORD size = 0;
    ::GetTokenInformation(token.value, TokenUser, nullptr, 0, &size);
    REQUIRE((size != 0 && ::GetLastError() == ERROR_INSUFFICIENT_BUFFER));
    std::vector<std::uintptr_t> storage((size + sizeof(std::uintptr_t) - 1) / sizeof(std::uintptr_t));
    REQUIRE(::GetTokenInformation(token.value, TokenUser, storage.data(), size, &size));
    wchar_t* text = nullptr;
    REQUIRE(::ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(storage.data())->User.Sid, &text));
    std::wstring result(text);
    ::LocalFree(text);
    return result;
}

void makeDirectoryWithWorldAccess(const std::filesystem::path& path, std::wstring_view rights, std::wstring_view flags = L"") {
    const auto sid = currentUserSid();
    const auto sddl = std::wstring(L"O:") + sid + L"D:P(A;OICI;FA;;;" + sid + L")(A;" + std::wstring(flags) + L";" + std::wstring(rights) + L";;;WD)";
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    REQUIRE(::ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr));
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), descriptor, FALSE};
    const auto created = ::CreateDirectoryW(path.c_str(), &attributes);
    ::LocalFree(descriptor);
    REQUIRE(created != 0);
}

void makeJunction(const std::filesystem::path& path, const std::filesystem::path& target) {
    REQUIRE(::CreateDirectoryW(path.c_str(), nullptr));
    Handle directory{::CreateFileW(path.c_str(), GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    REQUIRE(directory.value != INVALID_HANDLE_VALUE);
    const auto substitute = std::wstring(L"\\??\\") + target.native();
    const auto length = static_cast<std::uint16_t>(substitute.size() * sizeof(wchar_t));
    std::vector<std::byte> buffer(16 + length + 4);
    const std::uint32_t tag = IO_REPARSE_TAG_MOUNT_POINT;
    const auto dataLength = static_cast<std::uint16_t>(8 + length + 4);
    const std::uint16_t printOffset = length + 2;
    std::memcpy(buffer.data(), &tag, sizeof(tag));
    std::memcpy(buffer.data() + 4, &dataLength, sizeof(dataLength));
    std::memcpy(buffer.data() + 10, &length, sizeof(length));
    std::memcpy(buffer.data() + 12, &printOffset, sizeof(printOffset));
    std::memcpy(buffer.data() + 16, substitute.data(), length);
    DWORD written = 0;
    REQUIRE(::DeviceIoControl(directory.value, FSCTL_SET_REPARSE_POINT, buffer.data(),
        static_cast<DWORD>(buffer.size()), nullptr, 0, &written, nullptr));
}

struct JunctionCleanup {
    std::filesystem::path path;
    ~JunctionCleanup() { ::RemoveDirectoryW(path.c_str()); }
};
}
#endif

namespace {
std::filesystem::path testRoot(std::string_view label) {
    auto parent = std::filesystem::temp_directory_path();
#ifdef _WIN32
    Handle token;
    REQUIRE(::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token.value));
    std::vector<wchar_t> profile(32768);
    DWORD length = static_cast<DWORD>(profile.size());
    REQUIRE(::GetUserProfileDirectoryW(token.value, profile.data(), &length));
    parent = std::filesystem::path(profile.data());
#endif
    return parent /
        ("fo4ide_journal_root_" + std::string(label) + "_" +
         fo4ide_test::uniqueTempName());
}

struct Cleanup {
    std::filesystem::path path;
    ~Cleanup() { std::error_code error; std::filesystem::remove_all(path, error); }
};
}

TEST_CASE("Windows Session refuses bounded uncommitted sidecars without adopting or deleting them") {
#if defined(_WIN32) && defined(FO4IDE_TEST_HOOKS)
    using namespace fo4ide::plugin::detail;
    const auto base = testRoot("session_orphans");
    REQUIRE(::CreateDirectoryW(base.c_str(), nullptr));
    Cleanup cleanup{base};
    JournalRoot data(base / "data"), records(base / "records");
    const auto original = std::as_bytes(std::span("old", 3));
    data.writeFile("Patch.esp", original);
    for (unsigned index = 0; index < 8; ++index) records.writeFile(std::to_string(index) + ".authority", original);
    WindowsJournalDirectory heldData(data.nativeHandle()), heldRecords(records.nativeHandle());
    const auto before = heldData.snapshotFile("Patch.esp", 3);
    std::vector<WindowsJournalFileSnapshot> sidecars;
    for (unsigned index = 0; index < 8; ++index) sidecars.push_back(heldRecords.snapshotFile(std::to_string(index) + ".authority", 3));
    JournalRoots roots; roots.plugin = base / "data" / "Patch.esp";
    roots.pluginDirectoryIdentity = JournalDestination::captureRoot(base / "data");
    const std::vector<std::byte> bytes(original.begin(), original.end());
    JournalInput input{"plugin", "", true, fo4::io::sha256_hex(bytes), 3, {std::byte{'n'}, std::byte{'e'}, std::byte{'w'}}};
    JournalSession session(base / "records");
    const auto names = records.listFiles();
    CHECK_THROWS_WITH(session.publish(roots, {input}), "uncommitted Windows journal files require manual attention");
    CHECK(records.listFiles() == names);
    const auto after = heldData.snapshotFile("Patch.esp", 3);
    CHECK(after.identity == before.identity); CHECK(after.bytes == before.bytes);
    CHECK(data.listFiles() == std::vector<std::string>{"Patch.esp"});
    for (unsigned index = 0; index < 8; ++index) {
        const auto snapshot = heldRecords.snapshotFile(std::to_string(index) + ".authority", 3);
        CHECK(snapshot.identity == sidecars[index].identity); CHECK(snapshot.bytes == sidecars[index].bytes);
    }
#endif
}

TEST_CASE("JournalRoot creates a private held root and refuses unsafe ancestors") {
    using namespace fo4ide::plugin::detail;
    const auto base = testRoot("authority");
    Cleanup cleanup{base};
    std::filesystem::create_directories(base / "safe");
    const auto rootPath = base / "safe" / "journal";
    {
        JournalRoot root(rootPath);
        const auto expected = std::as_bytes(std::span("first", 5));
        root.writeFile("stage", expected);
        CHECK(root.readFile("stage", 5) == std::vector<std::byte>(expected.begin(), expected.end()));
    }
    {
        JournalRoot root(rootPath);
        CHECK(root.readFile("stage", 5).size() == 5);
    }

#ifdef _WIN32
    const auto reparse = base / "alias";
    std::filesystem::create_directories(base / "real");
    makeJunction(reparse, base / "real");
    JunctionCleanup junctionCleanup{reparse};
    CHECK_THROWS(JournalRoot(reparse / "journal"));
    CHECK_FALSE(std::filesystem::exists(base / "real" / "journal"));

    const auto unsafe = base / "unsafe";
    makeDirectoryWithWorldAccess(unsafe, L"FA");
    CHECK_THROWS(JournalRoot(unsafe / "journal"));
    CHECK_FALSE(std::filesystem::exists(unsafe / "journal"));
#else
    const auto unsafe = base / "unsafe";
    std::filesystem::create_directory(unsafe);
    std::filesystem::permissions(unsafe, std::filesystem::perms::all);
    CHECK_THROWS(JournalRoot(unsafe / "journal"));
    CHECK_FALSE(std::filesystem::exists(unsafe / "journal"));
    const auto reparse = base / "alias";
    std::filesystem::create_directories(base / "real");
    std::filesystem::create_directory_symlink(base / "real", reparse);
    CHECK_THROWS(JournalRoot(reparse / "journal"));
    CHECK_FALSE(std::filesystem::exists(base / "real" / "journal"));
#endif
}

#ifdef _WIN32
TEST_CASE("JournalRoot rejects ancestor mutation rights but permits read and creation rights") {
    using namespace fo4ide::plugin::detail;
    const auto base = testRoot("ancestor_acl");
    Cleanup cleanup{base};
    std::filesystem::create_directories(base);
    const auto readable = base / "readable";
    makeDirectoryWithWorldAccess(readable, L"0x001200a9");
    CHECK_NOTHROW(JournalRoot(readable / "journal"));

    const auto creatable = base / "creatable";
    makeDirectoryWithWorldAccess(creatable, L"0x00000006");
    {
        JournalRoot root(creatable / "journal");
        CHECK_FALSE(::MoveFileExW((creatable / "journal").c_str(), (creatable / "moved").c_str(), 0));
        CHECK(::GetLastError() == ERROR_SHARING_VIOLATION);
    }
    const auto unsafeChild = creatable / "unsafe";
    makeDirectoryWithWorldAccess(unsafeChild, L"FA");
    CHECK_THROWS(JournalRoot(unsafeChild / "journal"));

    for (const auto* right : {L"0x00000010", L"0x00000100", L"0x00000040", L"0x00010000", L"0x00040000", L"0x00080000", L"GW", L"GA"}) {
        const auto path = base / right;
        makeDirectoryWithWorldAccess(path, right);
        CHECK_THROWS(JournalRoot(path / "journal"));
        CHECK_FALSE(std::filesystem::exists(path / "journal"));
    }
    const auto inherited = base / "inherited";
    makeDirectoryWithWorldAccess(inherited, L"0x00000040", L"OICIIO");
    std::filesystem::create_directory(inherited / "child");
    CHECK_THROWS(JournalRoot(inherited / "child" / "journal"));
    CHECK_FALSE(std::filesystem::exists(inherited / "child" / "journal"));
}
#endif

TEST_CASE("JournalRoot bounds enumeration, publishes without replacement, and locks privately") {
    using namespace fo4ide::plugin::detail;
    const auto base = testRoot("operations");
    Cleanup cleanup{base};
    std::filesystem::create_directories(base);
    JournalRoot root(base / "journal");
    root.writeFile("stage", std::as_bytes(std::span("new", 3)));
    root.writeFile("published", std::as_bytes(std::span("old", 3)));
    CHECK_THROWS(root.publishFile("stage", "published"));
    CHECK(root.readFile("stage", 3).size() == 3);
    CHECK(root.readFile("published", 3).size() == 3);
    CHECK_THROWS(root.listFiles(1));
    CHECK_THROWS(root.listFiles(4097));
    {
        JournalLock lock(root, "global.lock");
        const auto files = root.listFiles();
        CHECK(std::find(files.begin(), files.end(), "global.lock") != files.end());
    }
}

TEST_CASE("JournalRoot allows bounded cleanup of excess orphaned payloads") {
    using namespace fo4ide::plugin::detail;
    const auto base = testRoot("orphan_cleanup");
    Cleanup cleanup{base};
    std::filesystem::create_directories(base);
    JournalRoot root(base / "journal");
    for (std::size_t index = 0; index <= kJournalMaxDirectoryEntries; ++index) {
        root.writeFile(std::string(32 - std::to_string(index).size(), '0') + std::to_string(index) + "_0.payload", {});
    }
    CHECK_THROWS(root.listFiles());
    const auto files = root.listFiles(4096);
    REQUIRE(files.size() == kJournalMaxDirectoryEntries + 1);
    for (const auto& file : files) root.removeFile(file);
    CHECK(root.listFiles().empty());
    CHECK_THROWS(root.listFiles(4097));
}

#if defined(_WIN32) && defined(FO4IDE_TEST_HOOKS)
#include "../../src/plugin/RadiclePluginJournalDestination.hpp"
#include <fstream>

TEST_CASE("Windows destination capture reads ordinary trusted ACLs and plugin names") {
    using namespace fo4ide::plugin::detail;
    const auto base = testRoot("destination");
    Cleanup cleanup{base};
    const auto sddl = std::wstring(L"O:") + currentUserSid() + L"D:P(A;OICI;FA;;;" + currentUserSid() +
        L")(A;OICI;FA;;;BA)(A;OICI;FA;;;SY)(A;;FR;;;WD)";
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    REQUIRE(::ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr));
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), descriptor, FALSE};
    const auto created = ::CreateDirectoryW(base.c_str(), &attributes);
    ::LocalFree(descriptor);
    REQUIRE(created != 0);
    const auto name = std::string("My Plugin (Test).esp");
    { std::ofstream out(base / name, std::ios::binary); out << "payload"; }
    const auto identity = JournalDestination::captureRoot(base);
    REQUIRE(identity.lease != nullptr);
    JournalDestination destination(base, {}, name, false, identity);
    REQUIRE(destination.read(7).has_value());
    CHECK(destination.read(7)->size() == 7);
    CHECK_THROWS(destination.read(6));
    CHECK(JournalDestination::listDirectory(base, {}, identity) == std::vector<std::string>{name});
    JournalDestination missing(base, {}, "missing.esp", false, identity);
    CHECK_FALSE(missing.read(1).has_value());
    CHECK_THROWS_AS(JournalDestination::listDirectory(base, {"missing"}, identity), JournalDirectoryAbsent);
    CHECK_THROWS(destination.replace(true, "", 7, {}, "stage"));
    CHECK(destination.read(7)->size() == 7);
    const auto nestedPath = base / L"Source \u00e9";
    std::filesystem::create_directory(nestedPath);
    { std::ofstream out(nestedPath / L"Quest \u00e9.psc"); out << "source"; }
    const std::string directoryName = "Source \xc3\xa9";
    const std::string sourceName = "Quest \xc3\xa9.psc";
    JournalDestination source(base, {directoryName}, sourceName, false, identity);
    REQUIRE(source.read(6).has_value());
    CHECK(source.read(6)->size() == 6);
    CHECK(JournalDestination::listDirectory(base, {directoryName}, identity) == std::vector<std::string>{sourceName});
}

TEST_CASE("Windows destination capture rejects untrusted creation rights and unsafe entries") {
    using namespace fo4ide::plugin::detail;
    const auto base = testRoot("destination_reject");
    Cleanup cleanup{base};
    std::filesystem::create_directory(base);
    SECTION("world add file") {
        makeDirectoryWithWorldAccess(base / "unsafe", L"0x2");
        CHECK_THROWS(JournalDestination::captureRoot(base / "unsafe"));
    }
    SECTION("world add subdirectory") {
        makeDirectoryWithWorldAccess(base / "unsafe", L"0x4");
        CHECK_THROWS(JournalDestination::captureRoot(base / "unsafe"));
    }
    SECTION("unsafe ancestor") {
        makeDirectoryWithWorldAccess(base / "unsafe", L"0x40");
        std::filesystem::create_directory(base / "unsafe" / "nested");
        CHECK_THROWS(JournalDestination::captureRoot(base / "unsafe" / "nested"));
    }
    SECTION("hardlink") {
        { std::ofstream out(base / "original.esp"); out << "payload"; }
        std::filesystem::create_hard_link(base / "original.esp", base / "alias.esp");
        const auto identity = JournalDestination::captureRoot(base);
        CHECK_THROWS(JournalDestination(base, {}, "alias.esp", false, identity));
    }
    SECTION("junction") {
        std::filesystem::create_directory(base / "target");
        makeJunction(base / "junction", base / "target");
        JunctionCleanup junction{base / "junction"};
        CHECK_THROWS(JournalDestination::captureRoot(base / "junction"));
    }
    SECTION("root authorization cannot transfer") {
        std::filesystem::create_directory(base / "first");
        std::filesystem::create_directory(base / "second");
        const auto identity = JournalDestination::captureRoot(base / "first");
        CHECK_THROWS(JournalDestination::listDirectory(base / "second", {}, identity));
        CHECK_THROWS(JournalDestination::listDirectory(base / "first", {}, JournalDirectoryIdentity{}));
        std::error_code error;
        std::filesystem::rename(base / "first", base / "moved", error);
        CHECK(error);
    }
    SECTION("unsafe names") {
        const auto identity = JournalDestination::captureRoot(base);
        for (const auto* name : {"..", "a:b", "a/b", "a\\b", "NUL", "a.", "a "})
            CHECK_THROWS(JournalDestination(base, {}, name, false, identity));
    }
}

TEST_CASE("Windows destination leases prevent root ancestor and file replacement") {
    using namespace fo4ide::plugin::detail;
    const auto base = testRoot("destination_lease");
    Cleanup cleanup{base};
    const auto root = base / "parent" / "data";
    std::filesystem::create_directories(root);
    { std::ofstream out(root / "Plugin.esp"); out << "original"; }
    { std::ofstream out(root / "candidate.esp"); out << "replacement"; }
    const auto identity = JournalDestination::captureRoot(root);
    JournalDestination destination(root, {}, "Plugin.esp", false, identity);
    const auto rootRenamed = ::MoveFileExW(root.c_str(), (base / "renamed-root").c_str(), 0);
    const auto rootError = ::GetLastError();
    CHECK_FALSE(rootRenamed);
    CHECK(rootError == ERROR_SHARING_VIOLATION);
    const auto parentRenamed = ::MoveFileExW((base / "parent").c_str(), (base / "renamed-parent").c_str(), 0);
    const auto parentError = ::GetLastError();
    CHECK_FALSE(parentRenamed);
    CHECK(parentError == ERROR_SHARING_VIOLATION);
    const auto deleted = ::DeleteFileW((root / "Plugin.esp").c_str());
    const auto deleteError = ::GetLastError();
    CHECK_FALSE(deleted);
    CHECK(deleteError == ERROR_SHARING_VIOLATION);
    const auto replaced = ::MoveFileExW((root / "candidate.esp").c_str(), (root / "Plugin.esp").c_str(), MOVEFILE_REPLACE_EXISTING);
    const auto replaceError = ::GetLastError();
    CHECK_FALSE(replaced);
    CHECK((replaceError == ERROR_SHARING_VIOLATION || replaceError == ERROR_ACCESS_DENIED));
    Handle writer{::CreateFileW((root / "Plugin.esp").c_str(), GENERIC_WRITE, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
    const auto writeError = ::GetLastError();
    CHECK(writer.value == INVALID_HANDLE_VALUE);
    CHECK(writeError == ERROR_SHARING_VIOLATION);
    const auto bytes = destination.read(8);
    REQUIRE(bytes.has_value());
    CHECK(std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size()) == "original");
    CHECK(std::filesystem::exists(root / "candidate.esp"));
}

TEST_CASE("Windows destination roots reject UNC and device namespace paths") {
    using namespace fo4ide::plugin::detail;
    CHECK_THROWS(JournalDestination::captureRoot(LR"(\\server\share\data)"));
    CHECK_THROWS(JournalDestination::captureRoot(LR"(\\?\C:\data)"));
    CHECK_THROWS(JournalDestination::captureRoot(LR"(\\.\C:\data)"));
    CHECK_THROWS(JournalDestination::captureRoot(L"C:data"));
}
#endif
