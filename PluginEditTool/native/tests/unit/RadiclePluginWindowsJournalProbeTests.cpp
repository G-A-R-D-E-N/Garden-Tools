#include "RadiclePluginWindowsJournalTestSupport.hpp"
#include "../helpers/TempRoot.hpp"
#include <array>
#include <chrono>
#include <span>
#include <optional>
#include <tuple>
#include "../../src/plugin/RadiclePluginJournalSession.hpp"
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
struct JunctionCleanup {
    std::filesystem::path path;
    ~JunctionCleanup() { ::RemoveDirectoryW(path.c_str()); }
};
void makeJunction(const std::filesystem::path& path, const std::filesystem::path& target) {
    Handle directory{::CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    REQUIRE(directory.value != INVALID_HANDLE_VALUE);
    const auto substitute = std::wstring(L"\\??\\") + target.native();
    const auto length = static_cast<std::uint16_t>(substitute.size() * sizeof(wchar_t));
    std::vector<std::byte> buffer(16 + length + 4);
    const std::uint32_t tag = IO_REPARSE_TAG_MOUNT_POINT;
    const auto dataLength = static_cast<std::uint16_t>(8 + length + 4);
    const auto printOffset = static_cast<std::uint16_t>(length + 2);
    std::memcpy(buffer.data(), &tag, sizeof(tag)); std::memcpy(buffer.data() + 4, &dataLength, sizeof(dataLength));
    std::memcpy(buffer.data() + 10, &length, sizeof(length)); std::memcpy(buffer.data() + 12, &printOffset, sizeof(printOffset));
    std::memcpy(buffer.data() + 16, substitute.data(), length);
    DWORD written = 0;
    REQUIRE(::DeviceIoControl(directory.value, FSCTL_SET_REPARSE_POINT, buffer.data(),
        static_cast<DWORD>(buffer.size()), nullptr, 0, &written, nullptr) != 0);
}
}
#endif
// Detects: journal operations must remain relative to the held directory when its old ancestor becomes a junction.
TEST_CASE("Windows journal handle probe creates renames and deletes inside held directory") {
#ifdef _WIN32
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / ("fo4ide_journal_probe_" + fo4ide_test::uniqueTempName());
    REQUIRE(fs::create_directory(root));
    Cleanup cleanup{root};
    const auto parent = root / "parent", held = parent / "journal", moved = root / "moved", outside = root / "outside";
    fs::create_directories(held); fs::create_directories(outside / "journal");
    { std::ofstream out(outside / "journal" / "sentinel"); out << "untouched"; }
    Handle rootHandle{::CreateFileW(held.c_str(), FILE_LIST_DIRECTORY | FILE_ADD_FILE | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    REQUIRE(rootHandle.value != INVALID_HANDLE_VALUE);
    std::error_code moveError;
    fs::rename(parent, moved, moveError);
    CAPTURE(moveError.value(), moveError.message());
    std::optional<JunctionCleanup> junction;
    auto actual = held;
    if (!moveError) {
        fs::create_directory(parent); makeJunction(parent, outside);
        junction.emplace(parent);
        actual = moved / "journal";
    } else {
        REQUIRE((moveError.value() == ERROR_ACCESS_DENIED || moveError.value() == ERROR_SHARING_VIOLATION));
        CHECK(fs::is_directory(held));
        CHECK_FALSE(fs::exists(moved));
    }
    std::wstring stage = L"probe.stage";
    UNICODE_STRING name{}; name.Length = static_cast<USHORT>(stage.size() * sizeof(wchar_t));
    name.MaximumLength = name.Length; name.Buffer = stage.data();
    OBJECT_ATTRIBUTES attributes{}; attributes.Length = sizeof(attributes);
    attributes.RootDirectory = rootHandle.value; attributes.ObjectName = &name;
    IO_STATUS_BLOCK block{}; HANDLE raw = nullptr;
    const auto status = ::NtCreateFile(&raw, GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE, &attributes,
        &block, nullptr, FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ, FILE_CREATE,
        FILE_OPEN_REPARSE_POINT | FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT, nullptr, 0);
    CAPTURE(status); REQUIRE(status >= 0); Handle child{raw};
    const char payload[] = "journal"; DWORD written = 0;
    REQUIRE(::WriteFile(child.value, payload, sizeof(payload) - 1, &written, nullptr) != 0);
    REQUIRE(written == sizeof(payload) - 1);
    const std::wstring finalName = L"probe.final";
    using fo4ide::plugin::detail::WindowsJournalRenameInfo;
    const auto renameLength = offsetof(WindowsJournalRenameInfo, FileName) + finalName.size() * sizeof(wchar_t);
    std::vector<std::uintptr_t> storage((renameLength + sizeof(std::uintptr_t) - 1) / sizeof(std::uintptr_t));
    auto* rename = reinterpret_cast<WindowsJournalRenameInfo*>(storage.data());
    rename->ReplaceIfExists = FALSE; rename->RootDirectory = rootHandle.value;
    rename->FileNameLength = static_cast<ULONG>(finalName.size() * sizeof(wchar_t));
    std::memcpy(rename->FileName, finalName.data(), rename->FileNameLength);
    IO_STATUS_BLOCK renameStatus{};
    const auto renamed = fo4ide::plugin::detail::NtSetInformationFile(child.value, &renameStatus, rename,
        static_cast<ULONG>(renameLength), fo4ide::plugin::detail::WindowsJournalFileInformationClass::Rename);
    CAPTURE(renamed); REQUIRE(renamed >= 0);
    CHECK(fs::exists(actual / "probe.final"));
    CHECK_FALSE(fs::exists(outside / "journal" / "probe.final"));
    FILE_DISPOSITION_INFO disposition{}; disposition.DeleteFile = TRUE;
    REQUIRE(::SetFileInformationByHandle(child.value, FileDispositionInfo, &disposition, sizeof(disposition)) != 0);
    REQUIRE(::CloseHandle(child.value) != 0); child.value = INVALID_HANDLE_VALUE;
    CHECK_FALSE(fs::exists(actual / "probe.final"));
    CHECK(fs::exists(outside / "journal" / "sentinel"));
    if (junction) REQUIRE(::RemoveDirectoryW(parent.c_str()) != 0);
#else
    SUCCEED("Windows handle probe");
#endif
}
TEST_CASE("Windows split-store recovery worker", "[.split-store-worker]") {
#if defined(_WIN32) && defined(FO4IDE_TEST_HOOKS)
    const auto root = std::filesystem::path(splitEnvironment(L"FO4IDE_SPLIT_ROOT"));
    const bool original = splitEnvironment(L"FO4IDE_SPLIT_ORIGINAL") == L"1";
    splitCrashPhase = std::stoi(splitEnvironment(L"FO4IDE_SPLIT_PHASE"));
    JournalRoot records(root / "records");
    WindowsJournalDirectory journal(records.nativeHandle());
    WindowsJournalReplacementExpectation expected;
    expected.directory = splitIdentity(root / "data", true);
    expected.finalSize = 7;
    const auto finalBytes = splitBytes("updated");
    expected.finalHash = fo4::io::sha256_hex(std::vector<std::byte>(finalBytes.begin(), finalBytes.end()));
    expected.originalExists = original;
    const auto originalBytes = splitBytes("original");
    expected.originalSize = original ? 8 : 0;
    expected.originalHash = original ? fo4::io::sha256_hex(std::vector<std::byte>(originalBytes.begin(), originalBytes.end())) : "";
    const auto approvedBytes = readSplitFile(root / "approved-metadata");
    const auto approved = nlohmann::json::parse(approvedBytes);
    expected.metadata = WindowsJournalFileMetadata{approved.at("security").get<std::string>(), approved.at("attributes").get<std::uint32_t>(),
        approved.at("creationTime").get<std::int64_t>()};
    JournalDestination destination(root / "data", {}, "destination");
    if (splitCrashPhase == 0) ::TerminateProcess(::GetCurrentProcess(), 77);
    setWindowsJournalIntentHook(splitCrash);
    journal.resumeReplacement(splitRequest(), expected, destination);
    if (splitCrashPhase != -1) FAIL("split-store worker missed crash boundary");
#endif
}
TEST_CASE("Windows split-store recovery survives process interruption and preserves file identity ACL and attributes") {
#if defined(_WIN32) && defined(FO4IDE_TEST_HOOKS)
    for (const bool original : {true, false}) for (int phase = 0; phase <= 4; ++phase) {
        if (!original && phase == 1) continue;
        CAPTURE(original, phase);
        const auto root = makeSplitRoot();
        Cleanup cleanup{root};
        makeSplitData(root / "data");
        JournalRoot records(root / "records");
        WindowsJournalDirectory journal(records.nativeHandle());
        if (original) writeSplitFile(root / "data" / "destination", "original");
        writeSplitFile(root / "data" / "stage", "updated");
        if (!original) resetToInheritedDacl(root / "data" / "stage");
        REQUIRE(::SetFileAttributesW((root / "data" / "stage").c_str(), original ? FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_ARCHIVE : FILE_ATTRIBUTE_ARCHIVE));
        const auto stageId = splitIdentity(root / "data" / "stage");
        const auto stageAcl = splitSecurity(root / "data" / "stage");
        const auto stageAttributes = ::GetFileAttributesW((root / "data" / "stage").c_str());
        WindowsJournalFileIdentity originalId;
        std::wstring originalAcl;
        DWORD originalAttributes = 0;
        if (original) {
            REQUIRE(::SetFileAttributesW((root / "data" / "destination").c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_ARCHIVE));
            originalId = splitIdentity(root / "data" / "destination");
            originalAcl = splitSecurity(root / "data" / "destination");
            originalAttributes = ::GetFileAttributesW((root / "data" / "destination").c_str());
            CHECK(stageAcl == originalAcl);
            CHECK(stageAttributes == originalAttributes);
        }
        if (original) setSplitCreation(root / "data" / "stage", splitCreation(root / "data" / "destination"));
        const auto expected = splitExpected(root / "data", original);
        writeSplitFile(root / "approved-metadata", nlohmann::json{{"security", expected.metadata->security},
            {"attributes", expected.metadata->attributes}, {"creationTime", expected.metadata->creationTime}}.dump());
        {
            JournalDestination destination(root / "data", {}, "destination");
            journal.persistReplacementIntent(splitRequest(), expected, destination);
        }
        REQUIRE(runSplitWorker(root, original, phase) == 77);
        REQUIRE(runSplitWorker(root, original, -1) == 0);
        CHECK(splitIdentity(root / "data" / "destination") == stageId);
        CHECK(splitCreation(root / "data" / "destination") == expected.metadata->creationTime);
        { JournalDestination published(root / "data", {}, "destination");
          CHECK(published.metadata() == *expected.metadata); }
        CHECK(splitSecurity(root / "data" / "destination") == stageAcl);
        CHECK(::GetFileAttributesW((root / "data" / "destination").c_str()) == stageAttributes);
        CHECK(readSplitFile(root / "data" / "destination") == std::vector<std::byte>(splitBytes("updated").begin(), splitBytes("updated").end()));
        if (original) {
            CHECK(splitIdentity(root / "data" / "backup") == originalId);
            CHECK(splitCreation(root / "data" / "backup") == expected.metadata->creationTime);
            CHECK(splitSecurity(root / "data" / "backup") == originalAcl);
            CHECK(::GetFileAttributesW((root / "data" / "backup").c_str()) == originalAttributes);
            CHECK(readSplitFile(root / "data" / "backup") == std::vector<std::byte>(splitBytes("original").begin(), splitBytes("original").end()));
        } else CHECK_FALSE(std::filesystem::exists(root / "data" / "backup"));
        CHECK_FALSE(std::filesystem::exists(root / "data" / "stage"));
        const auto completed = journal.listFiles();
        REQUIRE(runSplitWorker(root, original, -1) == 0);
        CHECK(journal.listFiles() == completed);
    }
#endif
}
TEST_CASE("Windows split-store recovery rejects newcomers occupied backups manifest mismatch and deny-delete readers") {
#if defined(_WIN32) && defined(FO4IDE_TEST_HOOKS)
    const auto root = makeSplitRoot();
    Cleanup cleanup{root};
    makeSplitData(root / "data");
    JournalRoot records(root / "records");
    WindowsJournalDirectory journal(records.nativeHandle());
    writeSplitFile(root / "data" / "destination", "original");
    writeSplitFile(root / "data" / "stage", "updated");
    setSplitCreation(root / "data" / "stage", splitCreation(root / "data" / "destination"));
    auto expected = splitExpected(root / "data", true);
    writeSplitFile(root / "approved-metadata", nlohmann::json{{"security", expected.metadata->security},
        {"attributes", expected.metadata->attributes}, {"creationTime", expected.metadata->creationTime}}.dump());
    {
        JournalDestination destination(root / "data", {}, "destination");
        journal.persistReplacementIntent(splitRequest(), expected, destination);
    }
    auto request = splitRequest();
    std::optional<JournalDestination> reader;
    bool alternateStream = false;
    bool extendedAttributes = false;
    bool originalEa = false;
    bool backupEa = false;
    SECTION("newcomer after backup") {
        REQUIRE(runSplitWorker(root, true, 1) == 77);
        writeSplitFile(root / "data" / "destination", "newcomer");
    }
    SECTION("occupied backup") { writeSplitFile(root / "data" / "backup", "newcomer"); }
    SECTION("manifest hash mismatch") { expected.finalHash = std::string(64, '0'); }
    SECTION("caller owner mismatch") { expected.metadata->security.replace(0, expected.metadata->security.find("G:"), "O:SY"); }
    SECTION("stage alternate data stream") { writeSplitFile(root / "data" / "stage:unapproved", "preserve"); alternateStream = true; }
    SECTION("original extended attributes") { setSplitEa(root / "data" / "destination"); originalEa = true; }
    SECTION("backup extended attributes") {
        REQUIRE(runSplitWorker(root, true, 1) == 77);
        setSplitEa(root / "data" / "backup"); backupEa = true;
    }
    SECTION("stage extended attributes") { setSplitEa(root / "data" / "stage"); extendedAttributes = true; }
    SECTION("stage sparse file") {
        Handle file{::CreateFileW((root / "data" / "stage").c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr)};
        REQUIRE(file.value != INVALID_HANDLE_VALUE);
        DWORD returned = 0;
        REQUIRE(::DeviceIoControl(file.value, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &returned, nullptr));
    }
    SECTION("stage attributes mismatch") { REQUIRE(::SetFileAttributesW((root / "data" / "stage").c_str(), FILE_ATTRIBUTE_HIDDEN)); }
    SECTION("stage creation time mismatch") { setSplitCreation(root / "data" / "stage", expected.metadata->creationTime + 10000000); }
    SECTION("stage metadata changed after backup") {
        REQUIRE(runSplitWorker(root, true, 1) == 77);
        REQUIRE(::SetFileAttributesW((root / "data" / "stage").c_str(), FILE_ATTRIBUTE_HIDDEN));
    }
    SECTION("stage mandatory label differs with identical SACL controls") {
        const auto setLabel = [&](const wchar_t* name, const wchar_t* sddl) {
            PSECURITY_DESCRIPTOR security = nullptr;
            REQUIRE(::ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &security, nullptr));
            PACL label = nullptr; BOOL present = FALSE, defaulted = FALSE;
            REQUIRE(::GetSecurityDescriptorSacl(security, &present, &label, &defaulted));
            auto path = (root / "data" / name).native();
            const auto error = ::SetNamedSecurityInfoW(path.data(), SE_FILE_OBJECT, LABEL_SECURITY_INFORMATION, nullptr, nullptr, nullptr, label);
            ::LocalFree(security);
            if (error == ERROR_ACCESS_DENIED || error == ERROR_PRIVILEGE_NOT_HELD) SKIP("Token cannot set mandatory labels");
            REQUIRE(error == ERROR_SUCCESS);
        };
        setLabel(L"destination", L"S:(ML;;NW;;;LW)"); setLabel(L"stage", L"S:(ML;;NW;;;LW)");
        expected = splitExpected(root / "data", true); request.entry = 1;
        { JournalDestination destination(root / "data", {}, "destination"); journal.persistReplacementIntent(request, expected, destination); }
        setLabel(L"stage", L"S:(ML;;NW;;;ME)");
        REQUIRE(splitSecurity(root / "data" / "destination").find(L"(ML;;NW;;;LW)") != std::wstring::npos);
        REQUIRE(splitSecurity(root / "data" / "stage").find(L"(ML;;NW;;;ME)") != std::wstring::npos);
    }
    SECTION("stage DACL mismatch") {
        PSECURITY_DESCRIPTOR security = nullptr;
        const auto sddl = splitSecurity(root / "data" / "destination");
        const auto ownerEnd = sddl.find(L"G:");
        REQUIRE(ownerEnd != sddl.npos);
        const auto owner = sddl.substr(2, ownerEnd - 2);
        const auto privateAcl = L"D:P(A;;FA;;;" + owner + L")";
        REQUIRE(::ConvertStringSecurityDescriptorToSecurityDescriptorW(privateAcl.c_str(), SDDL_REVISION_1, &security, nullptr));
        PACL dacl = nullptr; BOOL present = FALSE, defaulted = FALSE;
        REQUIRE(::GetSecurityDescriptorDacl(security, &present, &dacl, &defaulted));
        auto stagePath = (root / "data" / "stage").native();
        const auto error = ::SetNamedSecurityInfoW(stagePath.data(), SE_FILE_OBJECT,
            DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr, nullptr, dacl, nullptr);
        ::LocalFree(security);
        REQUIRE(error == ERROR_SUCCESS);
    }
    SECTION("unrelated deny-delete reader") { reader.emplace(root / "data", std::vector<std::string>{}, "destination"); }
    const bool destinationExists = std::filesystem::exists(root / "data" / "destination");
    const auto before = destinationExists ? readSplitFile(root / "data" / "destination") : std::vector<std::byte>{};
    const auto id = destinationExists ? splitIdentity(root / "data" / "destination") : WindowsJournalFileIdentity{};
    const auto originalEaBytes = originalEa ? readSplitEa(root / "data" / "destination") : std::vector<std::byte>{};
    const auto originalSecurity = destinationExists ? splitSecurity(root / "data" / "destination") : std::wstring{};
    const auto originalCreation = destinationExists ? splitCreation(root / "data" / "destination") : 0;
    const auto originalAttributes = destinationExists ? ::GetFileAttributesW((root / "data" / "destination").c_str()) : 0;
    const auto stageEa = extendedAttributes ? readSplitEa(root / "data" / "stage") : std::vector<std::byte>{};
    const auto stageId = splitIdentity(root / "data" / "stage");
    const auto stageSecurity = splitSecurity(root / "data" / "stage");
    const auto stageCreation = splitCreation(root / "data" / "stage");
    const auto stageAttributes = ::GetFileAttributesW((root / "data" / "stage").c_str());
    const bool backupExists = std::filesystem::exists(root / "data" / "backup");
    const auto backupBytes = backupExists ? readSplitFile(root / "data" / "backup") : std::vector<std::byte>{};
    const auto backupId = backupExists ? splitIdentity(root / "data" / "backup") : WindowsJournalFileIdentity{};
    const auto backupEaBytes = backupEa ? readSplitEa(root / "data" / "backup") : std::vector<std::byte>{};
    const auto backupSecurity = backupExists ? splitSecurity(root / "data" / "backup") : std::wstring{};
    const auto backupCreation = backupExists ? splitCreation(root / "data" / "backup") : 0;
    const auto backupAttributes = backupExists ? ::GetFileAttributesW((root / "data" / "backup").c_str()) : 0;
    JournalDestination destination(root / "data", {}, "destination");
    CHECK_THROWS(journal.resumeReplacement(request, expected, destination));
    CHECK(std::filesystem::exists(root / "data" / "destination") == destinationExists);
    if (destinationExists) {
        CHECK(readSplitFile(root / "data" / "destination") == before);
        CHECK(splitIdentity(root / "data" / "destination") == id);
        if (originalEa) CHECK(readSplitEa(root / "data" / "destination") == originalEaBytes);
        CHECK(splitSecurity(root / "data" / "destination") == originalSecurity);
        CHECK(splitCreation(root / "data" / "destination") == originalCreation);
        CHECK(::GetFileAttributesW((root / "data" / "destination").c_str()) == originalAttributes);
    }
    CHECK(readSplitFile(root / "data" / "stage") == std::vector<std::byte>(splitBytes("updated").begin(), splitBytes("updated").end()));
    CHECK(splitIdentity(root / "data" / "stage") == stageId);
    CHECK(splitSecurity(root / "data" / "stage") == stageSecurity);
    CHECK(splitCreation(root / "data" / "stage") == stageCreation);
    CHECK(::GetFileAttributesW((root / "data" / "stage").c_str()) == stageAttributes);
    if (extendedAttributes) CHECK(readSplitEa(root / "data" / "stage") == stageEa);
    if (alternateStream) CHECK(readSplitFile(root / "data" / "stage:unapproved") ==
        std::vector<std::byte>(splitBytes("preserve").begin(), splitBytes("preserve").end()));
    CHECK(std::filesystem::exists(root / "data" / "backup") == backupExists);
    if (backupExists) {
        CHECK(readSplitFile(root / "data" / "backup") == backupBytes);
        CHECK(splitIdentity(root / "data" / "backup") == backupId);
        if (backupEa) CHECK(readSplitEa(root / "data" / "backup") == backupEaBytes);
        CHECK(splitSecurity(root / "data" / "backup") == backupSecurity);
        CHECK(splitCreation(root / "data" / "backup") == backupCreation);
        CHECK(::GetFileAttributesW((root / "data" / "backup").c_str()) == backupAttributes);
    }
    CHECK_FALSE(std::filesystem::exists(root / "records" / "wr-abcdef0123456789abcdef0123456789-0.done"));
#endif
}
TEST_CASE("Windows split-store intent rejects mismatched stage attributes before publication") {
#if defined(_WIN32) && defined(FO4IDE_TEST_HOOKS)
    const auto root = makeSplitRoot();
    Cleanup cleanup{root};
    makeSplitData(root / "data");
    JournalRoot records(root / "records");
    WindowsJournalDirectory journal(records.nativeHandle());
    writeSplitFile(root / "data" / "destination", "original");
    writeSplitFile(root / "data" / "stage", "updated");
    setSplitCreation(root / "data" / "stage", splitCreation(root / "data" / "destination"));
    const auto expected = splitExpected(root / "data", true);
    bool originalEa = false;
    SECTION("original extended attributes") { setSplitEa(root / "data" / "destination"); originalEa = true; }
    SECTION("attribute mismatch") { REQUIRE(::SetFileAttributesW((root / "data" / "stage").c_str(), FILE_ATTRIBUTE_HIDDEN)); }
    SECTION("creation time mismatch") { setSplitCreation(root / "data" / "stage", expected.metadata->creationTime + 10000000); }
    const auto before = journal.listFiles();
    const auto originalEaBytes = originalEa ? readSplitEa(root / "data" / "destination") : std::vector<std::byte>{};
    const auto originalId = splitIdentity(root / "data" / "destination");
    const auto originalSecurity = splitSecurity(root / "data" / "destination");
    const auto originalCreation = splitCreation(root / "data" / "destination");
    const auto originalAttributes = ::GetFileAttributesW((root / "data" / "destination").c_str());
    const auto stageId = splitIdentity(root / "data" / "stage");
    const auto stageSecurity = splitSecurity(root / "data" / "stage");
    const auto stageCreation = splitCreation(root / "data" / "stage");
    const auto stageAttributes = ::GetFileAttributesW((root / "data" / "stage").c_str());
    JournalDestination destination(root / "data", {}, "destination");
    CHECK_THROWS(journal.persistReplacementIntent(splitRequest(), expected, destination));
    CHECK(journal.listFiles() == before);
    CHECK(splitIdentity(root / "data" / "destination") == originalId);
    if (originalEa) CHECK(readSplitEa(root / "data" / "destination") == originalEaBytes);
    CHECK(splitSecurity(root / "data" / "destination") == originalSecurity);
    CHECK(splitCreation(root / "data" / "destination") == originalCreation);
    CHECK(::GetFileAttributesW((root / "data" / "destination").c_str()) == originalAttributes);
    CHECK(splitIdentity(root / "data" / "stage") == stageId);
    CHECK(splitSecurity(root / "data" / "stage") == stageSecurity);
    CHECK(splitCreation(root / "data" / "stage") == stageCreation);
    CHECK(::GetFileAttributesW((root / "data" / "stage").c_str()) == stageAttributes);
    CHECK(readSplitFile(root / "data" / "destination") == std::vector<std::byte>(splitBytes("original").begin(), splitBytes("original").end()));
    CHECK(readSplitFile(root / "data" / "stage") == std::vector<std::byte>(splitBytes("updated").begin(), splitBytes("updated").end()));
#endif
}

TEST_CASE("Windows destination creates nested missing journal directories through held handles") {
#if defined(_WIN32) && defined(FO4IDE_TEST_HOOKS)
    namespace fs = std::filesystem;
    const auto root = makeSplitRoot(); Cleanup cleanup{root};
    makeSplitData(root / "data");
    {
        fo4ide::plugin::detail::JournalDestination destination(root / "data", {"alpha", "beta", "gamma"}, "destination", true);
        CHECK_FALSE(destination.read(1).has_value());
    }
    CHECK(fs::is_directory(root / "data" / "alpha" / "beta" / "gamma"));
    CHECK(splitSecurity(root / "data" / "alpha").find(L"D:P") == std::wstring::npos);
    CHECK(splitSecurity(root / "data" / "alpha" / "beta").find(L"D:P") == std::wstring::npos);
    CHECK(splitSecurity(root / "data" / "alpha").find(L"OICI") != std::wstring::npos);
    CHECK_FALSE(fs::exists(root / "data" / "alpha" / "beta" / "gamma" / "destination"));
#endif
}

TEST_CASE("Windows destination reuses existing journal directories and creates only the missing tail") {
#if defined(_WIN32) && defined(FO4IDE_TEST_HOOKS)
    namespace fs = std::filesystem;
    const auto root = makeSplitRoot(); Cleanup cleanup{root};
    makeSplitData(root / "data");
    fs::create_directory(root / "data" / "existing");
    writeSplitFile(root / "data" / "existing" / "keep", "kept");
    const auto identity = splitIdentity(root / "data" / "existing", true);
    const auto kept = readSplitFile(root / "data" / "existing" / "keep");
    {
        fo4ide::plugin::detail::JournalDestination destination(root / "data", {"existing", "missing"}, "destination", true);
        CHECK_FALSE(destination.read(1).has_value());
    }
    CHECK(splitIdentity(root / "data" / "existing", true) == identity);
    CHECK(readSplitFile(root / "data" / "existing" / "keep") == kept);
    CHECK(fs::is_directory(root / "data" / "existing" / "missing"));
#endif
}

TEST_CASE("Windows destination never creates an absent authorized root or absent source directories") {
#if defined(_WIN32) && defined(FO4IDE_TEST_HOOKS)
    namespace fs = std::filesystem;
    const auto root = makeSplitRoot(); Cleanup cleanup{root};
    makeSplitData(root / "data");
    CHECK_THROWS_AS(fo4ide::plugin::detail::JournalDestination(root / "data" / "absent" / "root", {"child"}, "destination", true), std::runtime_error);
    CHECK_FALSE(fs::exists(root / "data" / "absent"));
    const auto identity = fo4ide::plugin::detail::JournalDestination::captureRoot(root / "data");
    CHECK_THROWS_AS(fo4ide::plugin::detail::JournalDestination::listDirectory(root / "data", {"absent", "child"}, identity), fo4ide::plugin::detail::JournalDirectoryAbsent);
    CHECK_FALSE(fs::exists(root / "data" / "absent"));
    CHECK_THROWS_AS(fo4ide::plugin::detail::JournalDestination(root / "data", {"absent", "child"}, "destination", false), fo4ide::plugin::detail::JournalDirectoryAbsent);
    CHECK_FALSE(fs::exists(root / "data" / "absent"));
#endif
}

TEST_CASE("Windows destination refuses file and reparse point components instead of creating beneath them") {
#if defined(_WIN32) && defined(FO4IDE_TEST_HOOKS)
    namespace fs = std::filesystem;
    const auto root = makeSplitRoot(); Cleanup cleanup{root};
    makeSplitData(root / "data");
    writeSplitFile(root / "data" / "plain", "file");
    CHECK_THROWS(fo4ide::plugin::detail::JournalDestination(root / "data", {"plain", "after"}, "destination", true));
    CHECK_FALSE(fs::exists(root / "data" / "plain" / "after"));
    fs::create_directory(root / "data" / "target");
    fs::create_directory(root / "data" / "link");
    makeJunction(root / "data" / "link", root / "data" / "target");
    JunctionCleanup junction{root / "data" / "link"};
    CHECK_THROWS(fo4ide::plugin::detail::JournalDestination(root / "data", {"link", "after"}, "destination", true));
    CHECK_FALSE(fs::exists(root / "data" / "target" / "after"));
#endif
}
