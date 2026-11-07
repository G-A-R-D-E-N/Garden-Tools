#include <chrono>
#include "../helpers/TempRoot.hpp"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <fo4ide/papyrus/AnalysisService.hpp>
#include <fo4ide/papyrus/Parser.hpp>
#include <fo4ide/papyrus/ScriptIndex.hpp>
#include <fo4ide/papyrus/SecureSourceSnapshot.hpp>
#ifdef __linux__
#include <fcntl.h>
#include <unistd.h>
#endif
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
std::filesystem::path tempRoot() {
    return std::filesystem::temp_directory_path() / ("papyrus_flag_snapshot_" + fo4ide_test::uniqueTempName());
}
struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); } };
#ifdef __linux__
std::filesystem::path changingFlag;
void replaceFlagAfterRead() {
    { std::ofstream out(changingFlag, std::ios::binary | std::ios::trunc); out << "Flag Hidden 3\n"; }
    const auto time = std::filesystem::last_write_time(changingFlag);
    std::filesystem::last_write_time(changingFlag, time + std::chrono::seconds(2));
}
#endif
#ifdef _WIN32
struct WinHandle { HANDLE value{INVALID_HANDLE_VALUE}; ~WinHandle() { if (value != INVALID_HANDLE_VALUE) ::CloseHandle(value); } };
std::filesystem::path windowsFlag;
bool writeBlocked = false;
void tryWriteHeldFlag() {
    WinHandle file{::CreateFileW(windowsFlag.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (file.value == INVALID_HANDLE_VALUE && ::GetLastError() == ERROR_SHARING_VIOLATION) writeBlocked = true;
}
struct HookReset { ~HookReset() { fo4ide::papyrus::setSnapshotReadHook(nullptr); } };
#endif
}

// Detects: captured flag definitions must match public compiler semantics after the source path changes.
TEST_CASE("Snapshot compilation preserves public Papyrus flag semantics") {
#ifdef __linux__
    using namespace fo4ide::papyrus;
    const auto root = tempRoot(); Cleanup cleanup{root}; std::filesystem::create_directories(root);
    { std::ofstream out(root / "Probe.psc"); out << "Scriptname Probe Hidden\n"; }
    { std::ofstream out(root / "Institute_Papyrus_Flags.flg"); out << "Flag Hidden 5\n"; }
    const auto output = (root / "compiled").string(), flagPath = (root / "Institute_Papyrus_Flags.flg").string();
    const auto report = AnalysisService::compile((root / "Probe.psc").string(), &output, nullptr, false, true, &flagPath);
    INFO(report);
    std::ifstream pexFile(root / "compiled" / "Probe.pex", std::ios::binary); REQUIRE(pexFile.good());
    const std::vector<std::uint8_t> pexBytes((std::istreambuf_iterator<char>(pexFile)), std::istreambuf_iterator<char>());
    const auto publicPex = fo4ide::papyrus::PexFile::fromBytes(pexBytes); REQUIRE(publicPex.objects.size() == 1);
    const int fd = ::open(root.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW); REQUIRE(fd >= 0);
    const auto captured = snapshotPapyrusSources(fd, 20, 1024, 4096); ::close(fd);
    ScriptIndex snapshot; snapshot.addSnapshot(root, captured);
    { std::ofstream out(root / "Institute_Papyrus_Flags.flg", std::ios::trunc); out << "Flag Hidden 3\n"; }
    auto script = Parser::parse("Scriptname Probe Hidden\n");
    auto result = compileScript(snapshot, &script, (root / "Probe.psc").string(), {});
    REQUIRE(result.success()); REQUIRE(result.pex->objects.size() == 1);
    CHECK(result.pex->objects[0].userFlags == publicPex.objects[0].userFlags);
    CHECK(result.pex->objects[0].userFlags == (1u << 5));
#else
    SUCCEED("Linux held snapshot fixture");
#endif
}

// Detects: a linked or concurrently replaced flag file must not feed compiler flags.
TEST_CASE("Snapshot refuses linked or changed Papyrus flag files") {
#ifdef __linux__
    using namespace fo4ide::papyrus;
    const auto root = tempRoot(); Cleanup cleanup{root}; std::filesystem::create_directories(root / "imports");
    const auto outside = root / "outside.flg"; { std::ofstream out(outside); out << "Flag Hidden 5\n"; }
    const auto flag = root / "imports" / "Institute_Papyrus_Flags.flg";
    std::filesystem::create_symlink(outside, flag);
    const int fd = ::open((root / "imports").c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW); REQUIRE(fd >= 0);
    CHECK_THROWS(snapshotPapyrusSources(fd, 20, 1024, 4096));
    std::filesystem::remove(flag); { std::ofstream out(flag); out << "Flag Hidden 5\n"; }
    changingFlag = flag; setSnapshotReadHook(replaceFlagAfterRead);
    CHECK_THROWS(snapshotPapyrusSources(fd, 20, 1024, 4096));
    setSnapshotReadHook(nullptr); ::close(fd);
#else
    SUCCEED("Linux held snapshot fixture");
#endif
}

// Detects: Windows snapshots must retain flag semantics while denying a concurrent writer.
TEST_CASE("Windows Papyrus flag snapshot matches public compiler") {
#ifdef _WIN32
    using namespace fo4ide::papyrus;
    const auto root = tempRoot(); Cleanup cleanup{root}; std::filesystem::create_directories(root);
    { std::ofstream out(root / "Probe.psc"); out << "Scriptname Probe Hidden\n"; }
    const auto flag = root / "Institute_Papyrus_Flags.flg"; { std::ofstream out(flag); out << "Flag Hidden 5\n"; }
    const auto output = (root / "compiled").string(), flagPath = flag.string();
    const auto report = AnalysisService::compile((root / "Probe.psc").string(), &output, nullptr, false, true, &flagPath); INFO(report);
    std::ifstream pexFile(root / "compiled" / "Probe.pex", std::ios::binary); REQUIRE(pexFile.good());
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(pexFile)), std::istreambuf_iterator<char>());
    const auto publicPex = PexFile::fromBytes(bytes); REQUIRE(publicPex.objects.size() == 1);
    WinHandle directory{::CreateFileW(root.c_str(), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    REQUIRE(directory.value != INVALID_HANDLE_VALUE);
    windowsFlag = flag; writeBlocked = false; HookReset reset; setSnapshotReadHook(tryWriteHeldFlag);
    const auto captured = snapshotPapyrusSources(reinterpret_cast<std::intptr_t>(directory.value), 20, 1024, 4096);
    setSnapshotReadHook(nullptr); CHECK(writeBlocked);
    ScriptIndex index; index.addSnapshot(root, captured);
    { std::ofstream out(flag, std::ios::trunc); out << "Flag Hidden 3\n"; }
    auto script = Parser::parse("Scriptname Probe Hidden\n");
    auto compiled = compileScript(index, &script, (root / "Probe.psc").string(), {});
    REQUIRE(compiled.success()); REQUIRE(compiled.pex->objects.size() == 1);
    CHECK(compiled.pex->objects[0].userFlags == publicPex.objects[0].userFlags);
    CHECK(compiled.pex->objects[0].userFlags == (1u << 5));
#else
    SUCCEED("Windows held snapshot fixture");
#endif
}

// Detects: a hard-linked Windows flag file must not be accepted as a trusted compiler input.
TEST_CASE("Windows Papyrus flag snapshot refuses hard links") {
#ifdef _WIN32
    using namespace fo4ide::papyrus;
    const auto root = tempRoot(); Cleanup cleanup{root}; std::filesystem::create_directories(root / "imports");
    const auto external = root / "outside.flg"; { std::ofstream out(external); out << "Flag Hidden 5\n"; }
    const auto flag = root / "imports" / "Institute_Papyrus_Flags.flg";
    std::error_code error; std::filesystem::create_hard_link(external, flag, error);
    if (error) SKIP("hard links are unavailable on this filesystem");
    WinHandle directory{::CreateFileW((root / "imports").c_str(), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    REQUIRE(directory.value != INVALID_HANDLE_VALUE);
    CHECK_THROWS(snapshotPapyrusSources(reinterpret_cast<std::intptr_t>(directory.value), 20, 1024, 4096));
#else
    SUCCEED("Windows held snapshot fixture");
#endif
}
