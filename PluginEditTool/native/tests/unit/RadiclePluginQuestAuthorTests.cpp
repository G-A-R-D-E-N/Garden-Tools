#include <algorithm>
#include "../helpers/TempRoot.hpp"
#include <chrono>
#include <barrier>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/forms/quest.hpp>
#include <fo4/io/path_text.hpp>
#include <fo4/localization/strings.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>
#include <fo4ide/papyrus/SecureSourceSnapshot.hpp>
#include <fo4ide/papyrus/ScriptIndex.hpp>

#include "../helpers/GraphTestSupport.hpp"
#include "plugin_bytes.hpp"
#include "../../src/plugin/RadiclePluginQuestInternal.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>
#endif

namespace {

#ifdef _WIN32
std::filesystem::path snapshotSwapDirectory;
std::filesystem::path snapshotSwapTarget;
bool snapshotSwapSucceeded = false;
void convertSnapshotDirectoryToJunction() {
    const auto handle = ::CreateFileW(snapshotSwapDirectory.c_str(), GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot open test junction");
    const auto name = std::wstring(L"\\??\\") + snapshotSwapTarget.native();
    const auto length = static_cast<std::uint16_t>(name.size() * sizeof(wchar_t));
    std::vector<std::byte> buffer(16 + length + 4);
    const std::uint32_t tag = IO_REPARSE_TAG_MOUNT_POINT;
    const auto dataLength = static_cast<std::uint16_t>(8 + length + 4);
    const auto printOffset = static_cast<std::uint16_t>(length + 2);
    std::memcpy(buffer.data(), &tag, sizeof(tag));
    std::memcpy(buffer.data() + 4, &dataLength, sizeof(dataLength));
    std::memcpy(buffer.data() + 10, &length, sizeof(length));
    std::memcpy(buffer.data() + 12, &printOffset, sizeof(printOffset));
    std::memcpy(buffer.data() + 16, name.data(), length);
    DWORD written = 0;
    const bool changed = ::DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, buffer.data(),
        static_cast<DWORD>(buffer.size()), nullptr, 0, &written, nullptr) != 0;
    ::CloseHandle(handle);
    if (!changed) throw std::runtime_error("cannot create test junction");
    snapshotSwapSucceeded = true;
}
#endif

std::filesystem::path tempRoot() {
    const auto id = fo4ide_test::uniqueTempName();
    return std::filesystem::temp_directory_path() / ("fo4ide_quest_author_" + id);
}

struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

void failAfterLocalizedStringBatchInsert() {
    throw std::runtime_error("injected localized string staging failure");
}

struct LocalizedStringBatchHookCleanup {
    ~LocalizedStringBatchHookCleanup() {
        fo4ide::plugin::setLocalizedStringBatchAfterInsertHook(nullptr);
    }
};

void writeBytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(stream.good());
}

std::vector<std::byte> readBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    REQUIRE(stream.good());
    const auto size = stream.tellg();
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    stream.seekg(0);
    if (!bytes.empty()) stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return bytes;
}

std::vector<std::byte> emptyPlugin(std::uint32_t tes4Flags) {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", tes4Flags, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(0);
    hedr.u32(0x800);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);
    return out.bytes;
}

nlohmann::json parseResult(const fo4ide::Result<nlohmann::json>& result) {
    REQUIRE(result.has_value());
    if (result->is_string()) return nlohmann::json::parse(result->get<std::string>());
    return *result;
}

void loadPatch(fo4ide::plugin::RadiclePluginBackend& backend, const std::filesystem::path& path) {
    REQUIRE(backend.invoke("environment.LoadMo2", nlohmann::json{{"plugins", nlohmann::json::array({{
        {"name", "Patch.esp"}, {"path", path.string()}, {"source", "QuestAuthor"}, {"resolved", true}}})}})
                .has_value());
}


const fo4::Subrecord* subrecordOf(const fo4::Plugin& plugin, const fo4::FormKey& key, std::string_view signature) {
    const auto* record = fo4::find_record(plugin, key);
    REQUIRE(record != nullptr);
    const auto wanted = fo4::Signature::from_string(signature);
    for (const auto& sub : record->subrecords) {
        if (sub.signature == wanted) return &sub;
    }
    return nullptr;
}

std::string zstringOf(const fo4::Subrecord* sub) {
    REQUIRE(sub != nullptr);
    REQUIRE(!sub->data.empty());
    REQUIRE(sub->data.back() == std::byte{0});
    return std::string(reinterpret_cast<const char*>(sub->data.data()), sub->data.size() - 1);
}

std::vector<std::byte> rawBytes(std::string_view text) {
    std::vector<std::byte> bytes;
    bytes.reserve(text.size());
    for (const char ch : text) bytes.push_back(static_cast<std::byte>(ch));
    return bytes;
}

void writeStringTable(const std::filesystem::path& path,
                      fo4::localization::StringTableKind kind,
                      std::uint32_t id,
                      std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    fo4::localization::write_string_table_file({kind, {{id, rawBytes(text)}}}, path);
}

}  // namespace

#ifdef _WIN32
// Detects: ordinary Windows case-insensitive root spelling must still resolve an authorized source handle.
TEST_CASE("Windows Papyrus snapshots accept ordinary case-insensitive root spelling") {
    const auto root = tempRoot(); Cleanup cleanup{root};
    std::filesystem::create_directories(root / "base");
    { std::ofstream out(root / "base" / "Quest.psc", std::ios::binary); out << "Scriptname Quest\n"; }
    const auto handle = ::CreateFileW((root / "BASE").c_str(), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    REQUIRE(handle != INVALID_HANDLE_VALUE);
    const auto files = fo4ide::papyrus::snapshotPapyrusSources(reinterpret_cast<std::intptr_t>(handle), 100, 128, 256);
    ::CloseHandle(handle);
    REQUIRE(files.size() == 1u);
    CHECK(files[0].relativePath == std::filesystem::path("Quest.psc"));
    CHECK(files[0].text == "Scriptname Quest\n");
}

// Detects: a root or ancestor replaced after authorization must not redirect a held import handle.
TEST_CASE("Windows authorized Papyrus root remains tied to its opened directory") {
    for (const bool replaceAncestor : {false, true}) {
        for (const bool junction : {false, true}) {
            const auto root = tempRoot(); Cleanup cleanup{root};
            const auto ancestor = root / "parent";
            const auto imports = ancestor / "imports";
            const auto replaced = replaceAncestor ? ancestor : imports;
            const auto moved = root / "held";
            const auto outside = root / "outside";
            std::filesystem::create_directories(imports);
            std::filesystem::create_directories(outside);
            { std::ofstream out(imports / "Original.psc", std::ios::binary); out << "Scriptname Original\n"; }
            { std::ofstream out(outside / "Outside.psc"); out << "Scriptname Outside\n"; }
            const auto handle = ::CreateFileW(imports.c_str(), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            REQUIRE(handle != INVALID_HANDLE_VALUE);
            if (replaceAncestor) {
                CHECK_THROWS_AS(std::filesystem::rename(replaced, moved), std::filesystem::filesystem_error);
            } else {
                std::filesystem::rename(replaced, moved);
                std::filesystem::create_directories(replaced);
                if (junction) {
                    snapshotSwapDirectory = replaced; snapshotSwapTarget = outside; snapshotSwapSucceeded = false;
                    convertSnapshotDirectoryToJunction();
                    REQUIRE(snapshotSwapSucceeded);
                } else {
                    std::ofstream out(replaced / "Outside.psc"); out << "Scriptname Outside\n";
                }
            }
            const auto files = fo4ide::papyrus::snapshotPapyrusSources(reinterpret_cast<std::intptr_t>(handle), 100, 128, 256);
            ::CloseHandle(handle);
            REQUIRE(files.size() == 1);
            CHECK(files[0].relativePath == "Original.psc");
            CHECK(files[0].text == "Scriptname Original\n");
            if (junction && !replaceAncestor) CHECK(::RemoveDirectoryW(replaced.c_str()) != 0);
        }
    }
}

// Detects: a directory converted to a junction after enumeration must not lead an import snapshot outside its root.
TEST_CASE("Windows Papyrus snapshots refuse a junction introduced before child open") {
    const auto root = tempRoot(); Cleanup cleanup{root};
    const auto imports = root / "imports";
    snapshotSwapSucceeded = false;
    snapshotSwapDirectory = imports / "swap";
    snapshotSwapTarget = root / "outside";
    std::filesystem::create_directories(snapshotSwapDirectory);
    std::filesystem::create_directories(snapshotSwapTarget);
    { std::ofstream out(snapshotSwapTarget / "Outside.psc"); out << "Scriptname Outside\n"; }
    const auto handle = ::CreateFileW(imports.c_str(), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    REQUIRE(handle != INVALID_HANDLE_VALUE);
    fo4ide::papyrus::setSnapshotBeforeOpenHook(convertSnapshotDirectoryToJunction);
    CHECK_THROWS(fo4ide::papyrus::snapshotPapyrusSources(reinterpret_cast<std::intptr_t>(handle), 100, 128, 256));
    fo4ide::papyrus::setSnapshotBeforeOpenHook(nullptr);
    CHECK(snapshotSwapSucceeded);
    ::CloseHandle(handle);
    CHECK(::RemoveDirectoryW(snapshotSwapDirectory.c_str()) != 0);
}

// Detects: converting an already opened directory to a junction must not read the junction target.
TEST_CASE("Windows Papyrus snapshots do not follow a junction introduced after child open") {
    const auto root = tempRoot(); Cleanup cleanup{root};
    const auto imports = root / "imports";
    snapshotSwapSucceeded = false;
    snapshotSwapDirectory = imports / "swap";
    snapshotSwapTarget = root / "outside";
    std::filesystem::create_directories(snapshotSwapDirectory);
    std::filesystem::create_directories(snapshotSwapTarget);
    { std::ofstream out(snapshotSwapTarget / "Outside.psc"); out << "Scriptname Outside\n"; }
    const auto handle = ::CreateFileW(imports.c_str(), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    REQUIRE(handle != INVALID_HANDLE_VALUE);
    fo4ide::papyrus::setSnapshotAfterDirectoryOpenHook(convertSnapshotDirectoryToJunction);
    try {
        const auto files = fo4ide::papyrus::snapshotPapyrusSources(reinterpret_cast<std::intptr_t>(handle), 100, 128, 256);
        CHECK(files.empty());
    } catch (const std::exception&) {
    }
    fo4ide::papyrus::setSnapshotAfterDirectoryOpenHook(nullptr);
    CHECK(snapshotSwapSucceeded);
    ::CloseHandle(handle);
    CHECK(::RemoveDirectoryW(snapshotSwapDirectory.c_str()) != 0);
}
#endif

// Detects: fragment compilation must use captured imports even if their files change after snapshot.
TEST_CASE("Papyrus snapshot index never reopens captured source files") {
    using namespace fo4ide::papyrus;
    const auto root = tempRoot();
    std::filesystem::create_directories(root / "nested");
    const auto path = root / "nested" / "Quest.psc";
    { std::ofstream out(path); out << "Scriptname Changed\n"; }
    ScriptIndex index;
    index.addSnapshot(root, {{std::filesystem::path("nested") / "Quest.psc", "Scriptname Quest\n"}});
    const auto names = index.scriptNames();
    REQUIRE(names.size() == 1);
    CHECK(names[0] == "nested:Quest");
    const auto* script = index.resolve("nested:Quest");
    REQUIRE(script != nullptr);
    CHECK(script->name == "Quest");
    std::filesystem::remove(path);
    REQUIRE(index.resolve("nested:Quest") != nullptr);
    CHECK(index.resolve("nested:Quest")->name == "Quest");
    std::filesystem::remove(root / "nested");
    std::error_code linkError;
    std::filesystem::create_directory_symlink(root.parent_path(), root / "nested", linkError);
    if (!linkError) {
        const auto afterSwap = index.scriptNames();
        REQUIRE(afterSwap.size() == 1);
        CHECK(afterSwap[0] == "nested:Quest");
    }
    CHECK(index.parseCached(root / "nested" / "other.psc") == nullptr);
    CHECK_THROWS(index.addSnapshot(root, {{"../outside.psc", "Scriptname Outside\n"}}));
    std::error_code ec; std::filesystem::remove_all(root, ec);
}

// Detects: fragment imports may read the two vanilla source roots but must refuse other game Data paths.
TEST_CASE("Quest fragment imports allow only the known vanilla source roots") {
    const auto root = tempRoot(); Cleanup cleanup{root};
    const auto data = root / "Data";
    const auto base = data / "Scripts" / "Source" / "Base";
    const auto user = data / "Scripts" / "Source" / "User";
    std::filesystem::create_directories(base);
    std::filesystem::create_directories(user);
    std::filesystem::create_directories(data / "Other");
    using fo4ide::plugin::questinternal::allowedVanillaFragmentImports;
    CHECK(allowedVanillaFragmentImports(base, data).has_value());
    CHECK(allowedVanillaFragmentImports(user, data).has_value());
    CHECK_FALSE(allowedVanillaFragmentImports(data, data).has_value());
    CHECK_FALSE(allowedVanillaFragmentImports(data / "Other", data).has_value());
    CHECK_FALSE(allowedVanillaFragmentImports(root, {}).has_value());
    const auto redirected = root / "Redirected";
    std::filesystem::create_directories(redirected / "Scripts" / "Source");
    std::error_code ec;
    std::filesystem::create_directory_symlink(redirected, redirected / "Scripts" / "Source" / "Base", ec);
    if (!ec) CHECK_FALSE(allowedVanillaFragmentImports(redirected / "Scripts" / "Source" / "Base", redirected).has_value());
#ifndef _WIN32
    const auto dataUpper = root / "Case" / "Data";
    const auto dataLower = root / "Case" / "data";
    std::filesystem::create_directories(dataUpper / "Scripts" / "Source");
    std::filesystem::create_directories(dataLower / "Scripts" / "Source" / "Base");
    ec.clear();
    std::filesystem::create_directory_symlink(dataLower / "Scripts" / "Source" / "Base",
                                               dataUpper / "Scripts" / "Source" / "Base", ec);
    if (!ec) CHECK_FALSE(allowedVanillaFragmentImports(dataUpper / "Scripts" / "Source" / "Base", dataUpper).has_value());
#endif
}

TEST_CASE("Native quest fragments refuse an unapproved import directory without writing files") {
    const auto root = tempRoot(); Cleanup cleanup{root}; const auto path = root / "Patch.esp";
    writeBytes(path, emptyPlugin(0u));
    fo4ide::plugin::RadiclePluginBackend backend; loadPatch(backend, path);
    const auto created = parseResult(backend.invokeBackend("CreateRecord", nlohmann::json::array({
        "Patch.esp", "QUST", nlohmann::json{{"editorId", "CoWelcome"}}})));
    const auto key = created.at("formKey").get<std::string>();
    REQUIRE(backend.invokeBackend("SetQuestStages", nlohmann::json::array({"Patch.esp", key,
        nlohmann::json::array({{{"index", 10}, {"logEntry", "Begin"}}, {{"index", 20}, {"logEntry", "Done"}}})})).has_value());
    const auto actions = nlohmann::json::array({{{"kind", "display"}, {"value", 0}},
                                                {{"kind", "complete"}, {"value", 1}},
                                                {{"kind", "stage"}, {"value", 20}}});
    const auto args = [&](int stage, const std::string& imports) {
        return nlohmann::json::array({"Patch.esp", key, stage, 0, "QF_CoWelcome", root.string(), imports, actions});
    };
    const auto failure = backend.invokeBackend("SetQuestFragment", args(10, (root / "missing").string()));
    REQUIRE_FALSE(failure.has_value());
    CHECK_FALSE(std::filesystem::exists(root / "QF_CoWelcome.psc"));
    CHECK_FALSE(std::filesystem::exists(root / "QF_CoWelcome.pex"));
    CHECK(readBytes(path) == emptyPlugin(0u));
    REQUIRE(backend.invoke("backend.DiscardPlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    CHECK_FALSE(std::filesystem::exists(root / "QF_CoWelcome.psc"));
    CHECK_FALSE(std::filesystem::exists(root / "QF_CoWelcome.pex"));
}

TEST_CASE("Native SetQuestFragment dispatches through invokeBackend like the test hook") {
    const auto root = tempRoot(); Cleanup cleanup{root}; const auto path = root / "Patch.esp";
    writeBytes(path, emptyPlugin(0u));
    const auto imports = root / "imports";
    std::filesystem::copy(fo4::graph::test::baseStubs(), imports, std::filesystem::copy_options::recursive);
    fo4ide::plugin::RadiclePluginBackend backend; loadPatch(backend, path);
    const auto created = parseResult(backend.invokeBackend("CreateRecord", nlohmann::json::array({
        "Patch.esp", "QUST", nlohmann::json{{"editorId", "CoWelcome"}}})));
    const auto key = created.at("formKey").get<std::string>();
    REQUIRE(backend.invokeBackend("SetQuestStages", nlohmann::json::array({"Patch.esp", key,
        nlohmann::json::array({{{"index", 10}, {"logEntry", "Begin"}}})})).has_value());
    const auto actions = nlohmann::json::array({{{"kind", "stage"}, {"value", 10}}});
    const auto args = [&](int stage) {
        return nlohmann::json::array({"Patch.esp", key, stage, 0, "QF_CoWelcome", root.string(), imports.string(), actions});
    };
    const auto viaBackend = backend.invokeBackend("SetQuestFragment", args(99));
    const auto viaHook = backend.testSetQuestFragment(args(99));
    REQUIRE_FALSE(viaBackend.has_value());
    REQUIRE_FALSE(viaHook.has_value());
    CHECK(viaBackend.error().message == viaHook.error().message);
    const auto staged = backend.invokeBackend("SetQuestFragment", args(10));
    REQUIRE(staged.has_value());
    CHECK(staged->at("function").get<std::string>() == "Fragment_Stage_0010_Item_00");
    REQUIRE_FALSE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    CHECK(readBytes(path) == emptyPlugin(0u));
    CHECK_FALSE(std::filesystem::exists(root / "QF_CoWelcome.psc"));
    REQUIRE(backend.invoke("backend.DiscardPlugin", nlohmann::json::array({"Patch.esp"})).has_value());
}

// Detects: failed compilation and discarded pending quest changes must not publish source, PEX or plugin changes.
TEST_CASE("Pending quest fragment compilation and discard leave disk unchanged") {
    const auto root = tempRoot(); Cleanup cleanup{root}; const auto path = root / "Patch.esp";
    writeBytes(path, emptyPlugin(0u));
    const auto imports = root / "imports";
    std::filesystem::copy(fo4::graph::test::baseStubs(), imports, std::filesystem::copy_options::recursive);
    std::filesystem::create_directories(root / "empty-imports");
    fo4ide::plugin::RadiclePluginBackend backend; loadPatch(backend, path);
    const auto created = parseResult(backend.invokeBackend("CreateRecord", nlohmann::json::array({
        "Patch.esp", "QUST", nlohmann::json{{"editorId", "CoWelcome"}}})));
    const auto key = created.at("formKey").get<std::string>();
    REQUIRE(backend.invokeBackend("SetQuestStages", nlohmann::json::array({"Patch.esp", key,
        nlohmann::json::array({{{"index", 10}, {"logEntry", "Begin"}}, {{"index", 20}, {"logEntry", "Done"}}})})).has_value());
    const auto actions = nlohmann::json::array({{{"kind", "display"}, {"value", 0}},
                                                {{"kind", "complete"}, {"value", 1}},
                                                {{"kind", "stage"}, {"value", 20}}});
    const auto args = [&](int stage, const std::filesystem::path& rootImports) {
        return nlohmann::json::array({"Patch.esp", key, stage, 0, "QF_CoWelcome", root.string(), rootImports.string(), actions});
    };
    REQUIRE_FALSE(backend.testSetQuestFragment(args(10, root / "empty-imports")).has_value());
    REQUIRE(backend.testSetQuestFragment(args(10, imports)).has_value());
    REQUIRE(backend.testSetQuestFragment(args(20, imports)).has_value());
    const auto other = parseResult(backend.invokeBackend("CreateRecord", nlohmann::json::array({
        "Patch.esp", "QUST", nlohmann::json{{"editorId", "OtherQuest"}}})));
    const auto otherKey = other.at("formKey").get<std::string>();
    REQUIRE(backend.invokeBackend("SetQuestStages", nlohmann::json::array({"Patch.esp", otherKey,
        nlohmann::json::array({{{"index", 10}, {"logEntry", "Begin"}}})})).has_value());
    const auto otherArgs = nlohmann::json::array({"Patch.esp", otherKey, 10, 0, "QF_CoWelcome",
        root.string(), imports.string(), actions});
    REQUIRE_FALSE(backend.testSetQuestFragment(otherArgs).has_value());
    REQUIRE_FALSE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    CHECK(readBytes(path) == emptyPlugin(0u));
    CHECK_FALSE(std::filesystem::exists(root / "QF_CoWelcome.psc"));
    CHECK_FALSE(std::filesystem::exists(root / "QF_CoWelcome.pex"));
    REQUIRE(backend.invoke("backend.DiscardPlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    CHECK_FALSE(std::filesystem::exists(root / "QF_CoWelcome.psc"));
    CHECK_FALSE(std::filesystem::exists(root / "QF_CoWelcome.pex"));
    const auto recreated = parseResult(backend.invokeBackend("CreateRecord", nlohmann::json::array({
        "Patch.esp", "QUST", nlohmann::json{{"editorId", "CoWelcome"}}})));
    REQUIRE(recreated.at("formKey").get<std::string>() == key);
    REQUIRE(backend.invokeBackend("SetQuestStages", nlohmann::json::array({"Patch.esp", key,
        nlohmann::json::array({{{"index", 10}, {"logEntry", "Begin"}}})})).has_value());
    CHECK(backend.testSetQuestFragment(args(10, imports)).has_value());
}

// Detects: two plugins racing to claim one script output must not both stage it.
TEST_CASE("Pending quest fragments serialize output ownership across plugins") {
    const auto root = tempRoot(); Cleanup cleanup{root};
    const auto first = root / "First.esp", second = root / "Second.esp";
    writeBytes(first, emptyPlugin(0u)); writeBytes(second, emptyPlugin(0u));
    const auto imports = root / "imports";
    std::filesystem::copy(fo4::graph::test::baseStubs(), imports, std::filesystem::copy_options::recursive);
    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", nlohmann::json{{"plugins", nlohmann::json::array({
        {{"name", "First.esp"}, {"path", first.string()}, {"source", "Test"}, {"resolved", true}},
        {{"name", "Second.esp"}, {"path", second.string()}, {"source", "Test"}, {"resolved", true}}})}}).has_value());
    const auto prepare = [&](const std::string& plugin) {
        const auto created = parseResult(backend.invokeBackend("CreateRecord", nlohmann::json::array({
            plugin, "QUST", nlohmann::json{{"editorId", "CoWelcome"}}})));
        const auto key = created.at("formKey").get<std::string>();
        REQUIRE(backend.invokeBackend("SetQuestStages", nlohmann::json::array({plugin, key,
            nlohmann::json::array({{{"index", 10}, {"logEntry", "Begin"}}})})).has_value());
        return nlohmann::json::array({plugin, key, 10, 0, "QF_Shared", root.string(), imports.string(),
            nlohmann::json::array({{{"kind", "stage"}, {"value", 10}}})});
    };
    const auto a = prepare("First.esp"), b = prepare("Second.esp");
    std::barrier start(3);
    bool aOk = false, bOk = false;
    std::string aError, bError;
    std::thread left([&] { start.arrive_and_wait(); const auto r = backend.testSetQuestFragment(a); aOk = r.has_value(); if (!r) aError = r.error().message; });
    std::thread right([&] { start.arrive_and_wait(); const auto r = backend.testSetQuestFragment(b); bOk = r.has_value(); if (!r) bError = r.error().message; });
    start.arrive_and_wait(); left.join(); right.join();
    CHECK(aOk != bOk);
    CHECK((aOk ? bError : aError).find("already staged by another plugin") != std::string::npos);
    CHECK_FALSE(backend.invoke("backend.SavePlugin", nlohmann::json::array({aOk ? "First.esp" : "Second.esp"})).has_value());
    CHECK_FALSE(std::filesystem::exists(root / "QF_Shared.pex"));
}

// Detects: CreateRecord QUST must write the FULL title and the DNAM flags, priority and type from its payload, so a created quest is complete after save.
TEST_CASE("Native CreateRecord QUST writes title, flags, priority and type from its payload") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, emptyPlugin(0u));

    fo4ide::plugin::RadiclePluginBackend backend;
    loadPatch(backend, path);
    const auto created = parseResult(backend.invokeBackend(
        "CreateRecord", nlohmann::json::array({"Patch.esp", "QUST", nlohmann::json{
            {"editorId", "CoTitle"}, {"name", "Welcome to Sanctuary"},
            {"flags", 0x11}, {"priority", 30}, {"type", 6}}})));
    const auto key = fo4::FormKey::parse(created.at("formKey").get<std::string>());
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    const auto disk = fo4::read_plugin_file(path);
    CHECK(zstringOf(subrecordOf(disk, key, "FULL")) == "Welcome to Sanctuary");
    const auto* dnam = subrecordOf(disk, key, "DNAM");
    REQUIRE(dnam != nullptr);
    REQUIRE(dnam->data.size() == 12u);
    CHECK(std::to_integer<std::uint8_t>(dnam->data[0]) == 0x11);
    CHECK(std::to_integer<std::uint8_t>(dnam->data[1]) == 0x00);
    CHECK(std::to_integer<std::uint8_t>(dnam->data[2]) == 30);
    CHECK(std::to_integer<std::uint8_t>(dnam->data[8]) == 6);
}

// Invariant: a named flag selection must write exactly the schema's bits, and a name the schema does not list must be refused.
TEST_CASE("Native CreateRecord QUST accepts schema flag names and refuses an unknown name") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, emptyPlugin(0u));

    fo4ide::plugin::RadiclePluginBackend backend;
    loadPatch(backend, path);
    const auto created = parseResult(backend.invokeBackend(
        "CreateRecord", nlohmann::json::array({"Patch.esp", "QUST", nlohmann::json{
            {"editorId", "CoNamed"}, {"name", "Named"},
            {"flags", nlohmann::json::array({"Start Game Enabled", "Starts Enabled"})}}})));
    const auto key = fo4::FormKey::parse(created.at("formKey").get<std::string>());
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(path);
    const auto* named = subrecordOf(disk, key, "DNAM");
    REQUIRE(named != nullptr);
    REQUIRE(named->data.size() == 12u);
    CHECK(std::to_integer<std::uint8_t>(named->data[0]) == 0x11);
    CHECK(std::to_integer<std::uint8_t>(named->data[1]) == 0x00);

    const auto refused = backend.invokeBackend(
        "CreateRecord", nlohmann::json::array({"Patch.esp", "QUST", nlohmann::json{
            {"editorId", "CoBad"},
            {"flags", nlohmann::json::array({"Not A Real Flag"})}}}));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "InvalidRecordCreationPayload");

    const auto mapped = parseResult(backend.invokeBackend(
        "CreateRecord", nlohmann::json::array({"Patch.esp", "QUST", nlohmann::json{
            {"editorId", "CoMap"},
            {"flags", nlohmann::json{{"Start Game Enabled", false}, {"Run Once", true}}}}})));
    const auto mapKey = fo4::FormKey::parse(mapped.at("formKey").get<std::string>());
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto after = fo4::read_plugin_file(path);
    const auto* bits = subrecordOf(after, mapKey, "DNAM");
    REQUIRE(bits != nullptr);
    REQUIRE(bits->data.size() == 12u);
    CHECK(std::to_integer<std::uint8_t>(bits->data[0]) == 0x00);
    CHECK(std::to_integer<std::uint8_t>(bits->data[1]) == 0x01);
}

// Invariant: a story manager node must be creatable, keep a setting whose subrecord the schema leaves optional, and hold the quest attached to it.
TEST_CASE("Native CreateRecord SMQN creates a story manager node and attaches a quest") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, emptyPlugin(0u));

    fo4ide::plugin::RadiclePluginBackend backend;
    loadPatch(backend, path);
    const auto quest = parseResult(backend.invokeBackend(
        "CreateRecord", nlohmann::json::array({"Patch.esp", "QUST", nlohmann::json{
            {"editorId", "SmQuest"}, {"name", "SmQuest"}}})));
    const auto questKey = quest.at("formKey").get<std::string>();
    const auto node = backend.invokeBackend(
        "CreateRecord", nlohmann::json::array({"Patch.esp", "SMQN", nlohmann::json{
            {"editorId", "SmNode"}, {"schema", true}}}));
    if (!node.has_value()) FAIL("SMQN create failed: " << node.error().type << ": " << node.error().message);
    const auto nodeText = parseResult(node).at("formKey").get<std::string>();
    const auto nodeKey = fo4::FormKey::parse(nodeText);

    // Invariant: XNAM is optional in the SMQN authoring schema, so this also covers creating an absent subrecord.
    const auto settings = backend.invokeBackend(
        "SetField", nlohmann::json::array({"Patch.esp", nodeText, "SMQN\\MaxConcurrentQuests", "2"}));
    if (!settings.has_value()) FAIL("SMQN setting failed: " << settings.error().type << ": " << settings.error().message);
    const auto link = backend.invokeBackend(
        "InsertFormLinkListElement", nlohmann::json::array({"Patch.esp", nodeText, "SMQN\\Quests", 0, questKey}));
    if (!link.has_value()) FAIL("SMQN quest link failed: " << link.error().type << ": " << link.error().message);
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    const auto disk = fo4::read_plugin_file(path);
    CHECK(zstringOf(subrecordOf(disk, nodeKey, "EDID")) == "SmNode");
    const auto* limit = subrecordOf(disk, nodeKey, "XNAM");
    REQUIRE(limit != nullptr);
    REQUIRE_FALSE(limit->data.empty());
    CHECK(std::to_integer<std::uint8_t>(limit->data[0]) == 2);
    const auto* quests = subrecordOf(disk, nodeKey, "NNAM");
    REQUIRE(quests != nullptr);
    const auto linked = std::to_integer<std::uint32_t>(quests->data[0]) |
                        (std::to_integer<std::uint32_t>(quests->data[1]) << 8) |
                        (std::to_integer<std::uint32_t>(quests->data[2]) << 16) |
                        (std::to_integer<std::uint32_t>(quests->data[3]) << 24);
    CHECK(linked != 0u);
}

// Detects: set_field on QUST\Flags, QUST\Priority, QUST\Type and QUST\Name must update a created quest and keep the values after save and reload.
TEST_CASE("Native set_field edits quest flags, priority, type and title and keeps them after save") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, emptyPlugin(0u));

    fo4ide::plugin::RadiclePluginBackend backend;
    loadPatch(backend, path);
    const auto created = parseResult(backend.invokeBackend(
        "CreateRecord", nlohmann::json::array({"Patch.esp", "QUST", nlohmann::json{
            {"editorId", "CoEdit"}, {"name", "Old Title"}}})));
    const auto key = created.at("formKey").get<std::string>();

    const std::pair<std::string, std::string> edits[] = {
        {"QUST\\Flags", "17"}, {"QUST\\Priority", "30"}, {"QUST\\Type", "6"}, {"QUST\\Name", "New Title"}};
    for (const auto& [field, value] : edits) {
        INFO("set " + field);
        const auto result = backend.invokeBackend(
            "SetField", nlohmann::json::array({"Patch.esp", key, field, value}));
        REQUIRE(result.has_value());
    }
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    const auto disk = fo4::read_plugin_file(path);
    const auto formKey = fo4::FormKey::parse(key);
    CHECK(zstringOf(subrecordOf(disk, formKey, "FULL")) == "New Title");
    const auto* dnam = subrecordOf(disk, formKey, "DNAM");
    REQUIRE(dnam != nullptr);
    CHECK(std::to_integer<std::uint8_t>(dnam->data[0]) == 0x11);
    CHECK(std::to_integer<std::uint8_t>(dnam->data[2]) == 30);
    CHECK(std::to_integer<std::uint8_t>(dnam->data[8]) == 6);
}

// Detects: CreateRecord QUST must allocate localized title text above IDs used by other languages, stage it with STRINGS, and preserve its FULL ID through save/reload.
TEST_CASE("Native CreateRecord QUST stages localized title text and persists its string ID") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, emptyPlugin(fo4::RecordFlags::localized));
    writeStringTable(root / "Strings" / "Patch_en.STRINGS", fo4::localization::StringTableKind::strings, 17, "Existing");
    writeStringTable(root / "Strings" / "Patch_de.STRINGS", fo4::localization::StringTableKind::strings, 300, "Vorhanden");

    fo4ide::plugin::RadiclePluginBackend backend;
    loadPatch(backend, path);
    backend.testSetJournalRoot(root / "journal");
    const auto created = parseResult(backend.invokeBackend(
        "CreateRecord", nlohmann::json::array({"Patch.esp", "QUST", nlohmann::json{
            {"editorId", "CoLocal"}, {"name", "Localized title"}}})));
    const auto key = fo4::FormKey::parse(created.at("formKey").get<std::string>());
    const auto staged = parseResult(backend.invoke(
        "strings.ExportStrings", nlohmann::json::array({"Patch.esp", "en", "STRINGS"})));
    bool stagedTitle = false;
    for (const auto& row : staged.at("rows")) {
        if (row.at("id") == 301 && row.at("text") == "Localized title") stagedTitle = true;
    }
    CHECK(stagedTitle);
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    REQUIRE(backend.invoke("strings.SaveStringTables", nlohmann::json::array({"Patch.esp", ""})).has_value());

    const auto disk = fo4::read_plugin_file(path);
    const auto* full = subrecordOf(disk, key, "FULL");
    REQUIRE(full != nullptr);
    REQUIRE(full->data.size() == sizeof(std::uint32_t));
    std::uint32_t id = 0;
    std::memcpy(&id, full->data.data(), sizeof(id));
    CHECK(id == 301u);
    const auto table = fo4::localization::read_string_table_file(
        root / "Strings" / "Patch_en.STRINGS", fo4::localization::StringTableKind::strings);
    const auto entry = std::find_if(table.entries.begin(), table.entries.end(), [](const auto& row) {
        return row.id == 301u;
    });
    REQUIRE(entry != table.entries.end());
    CHECK(std::string(reinterpret_cast<const char*>(entry->text.data()), entry->text.size()) == "Localized title");

    fo4ide::plugin::RadiclePluginBackend reload;
    loadPatch(reload, path);
    const auto saved = parseResult(reload.invoke(
        "strings.ExportStrings", nlohmann::json::array({"Patch.esp", "en", "STRINGS"})));
    CHECK(std::any_of(saved.at("rows").begin(), saved.at("rows").end(), [](const auto& row) {
        return row.at("id") == 301 && row.at("text") == "Localized title";
    }));
}

// Detects: a failed localized title insertion must preserve plugin bytes and any prior staged table edit.
TEST_CASE("Native CreateRecord QUST preserves prior staging on failure") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, emptyPlugin(fo4::RecordFlags::localized));
    const auto original = readBytes(path);
    writeStringTable(root / "Strings" / "Patch_en.STRINGS", fo4::localization::StringTableKind::strings, 17, "Existing");

    fo4ide::plugin::RadiclePluginBackend backend;
    loadPatch(backend, path);
    LocalizedStringBatchHookCleanup hookCleanup;
    // Detects: a failed localized quest creation must preserve unrelated preexisting staged table edits.
    REQUIRE(backend.invoke("strings.SetString", nlohmann::json::array({"Patch.esp", "en", "STRINGS", 17, "Pending"})).has_value());
    fo4ide::plugin::setLocalizedStringBatchAfterInsertHook(&failAfterLocalizedStringBatchInsert);
    const auto failed = backend.invokeBackend(
        "CreateRecord", nlohmann::json::array({"Patch.esp", "QUST", nlohmann::json{
            {"editorId", "CoFailed"}, {"name", "Not staged"}}}));
    fo4ide::plugin::setLocalizedStringBatchAfterInsertHook(nullptr);
    REQUIRE_FALSE(failed.has_value());
    CHECK(failed.error().type == "RecordCreateFailed");

    const auto rows = parseResult(backend.invoke(
        "strings.ExportStrings", nlohmann::json::array({"Patch.esp", "en", "STRINGS"})));
    REQUIRE(rows.at("rows").size() == 1u);
    CHECK(rows.at("rows")[0] == nlohmann::json{{"id", 17}, {"text", "Pending"}});
    CHECK(readBytes(path) == original);
}

// Detects: Windows quest localization must preserve a non-ASCII custom Strings directory when staging text.
TEST_CASE("Windows quest stage accepts a Unicode custom Strings directory") {
#ifdef _WIN32
    const auto root = tempRoot(); Cleanup cleanup{root}; const auto plugin = root / "Patch.esp";
    const auto custom = root / fo4::io::from_utf8("\xC3\x84" "Custom") / "Strings";
    writeBytes(plugin, emptyPlugin(fo4::RecordFlags::localized));
    writeStringTable(custom / "Patch_en.STRINGS", fo4::localization::StringTableKind::strings, 10, "Name");
    writeStringTable(custom / "Patch_en.DLSTRINGS", fo4::localization::StringTableKind::dlstrings, 20, "Log");
    writeStringTable(custom / "Patch_en.ILSTRINGS", fo4::localization::StringTableKind::ilstrings, 30, "Other");
    fo4ide::plugin::RadiclePluginBackend backend; loadPatch(backend, plugin);
    const auto created = parseResult(backend.invokeBackend("CreateRecord", nlohmann::json::array({"Patch.esp", "QUST", nlohmann::json{{"editorId", "UnicodePath"}}})));
    const auto key = created.at("formKey").get<std::string>();
    REQUIRE(backend.invokeBackend("SetQuestStages", nlohmann::json::array({"Patch.esp", key,
        nlohmann::json::array({{{"index", 10}, {"logEntry", "New log"}}}),
        nlohmann::json{{"language", "en"}, {"stringsDirectory", fo4::io::utf8(custom)}}})).has_value());
#else
    SUCCEED("Windows Unicode path fixture runs on Windows");
#endif
}

// Detects: a quest title exceeding its row size must be rejected without creating a truncated record.
TEST_CASE("Native CreateRecord QUST rejects an oversized title") {
    const auto root = tempRoot(); Cleanup cleanup{root}; const auto path = root / "Patch.esp";
    writeBytes(path, emptyPlugin(0u));
    fo4ide::plugin::RadiclePluginBackend backend; loadPatch(backend, path);
    const auto result = backend.invokeBackend("CreateRecord", nlohmann::json::array({"Patch.esp", "QUST",
        nlohmann::json{{"editorId", "Oversized"}, {"name", std::string(65536, 'A')}}}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "InvalidRecordCreationPayload");
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    CHECK(readBytes(path) == emptyPlugin(0u));
}

// Detects: an end-to-end quest must survive save and reload with its reference alias, GetStage objective targets, stage flags, title and start-game flag, and its SEQ entry must name the quest.
TEST_CASE("Native quest build reads back after save: alias, GetStage objective, stages, start-game flag and SEQ") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, emptyPlugin(0u));

    fo4ide::plugin::RadiclePluginBackend backend;
    loadPatch(backend, path);
    const auto created = parseResult(backend.invokeBackend(
        "CreateRecord", nlohmann::json::array({"Patch.esp", "QUST", nlohmann::json{
            {"editorId", "CoWelcome"}, {"name", "Welcome"}, {"flags", 0x11}}})));
    const auto questKey = created.at("formKey").get<std::string>();

    REQUIRE(backend.invokeBackend("SetQuestAliases", nlohmann::json::array({"Patch.esp", questKey,
        nlohmann::json::array({{{"kind", "reference"}, {"id", 1}, {"name", "Player"}}})})).has_value());
    REQUIRE(backend.invokeBackend("SetQuestStages", nlohmann::json::array({"Patch.esp", questKey,
        nlohmann::json::array({
            {{"index", 10}, {"flags", 1}, {"logEntry", "Caf\xC3\xA9"}},
            {{"index", 20}, {"logEntries", nlohmann::json::array({{{"note", "Caf\xC3\xA9"}, {"logText", "Done"}}})}}})})).has_value());
    REQUIRE(backend.invokeBackend("SetQuestObjectives", nlohmann::json::array({"Patch.esp", questKey,
        nlohmann::json::array({{
            {"index", 0}, {"flags", 0}, {"displayText", "Caf\xC3\xA9"},
            {"targets", nlohmann::json::array({{
                {"alias", "Player"}, {"flags", 0},
                {"conditions", nlohmann::json::array({{
                    {"function", "GetStage"}, {"operator", "=="}, {"param1", questKey}, {"value", 10}}})}}})}}})})).has_value());

    const auto seqResult = backend.createSeqFile(
        nlohmann::json::array({"Patch.esp", root.string()}));
    if (!seqResult.has_value()) INFO(seqResult.error().type + ": " + seqResult.error().message);
    REQUIRE(seqResult.has_value());
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    const auto seqBytes = readBytes(root / "Patch.seq");
    REQUIRE(seqBytes.size() == 4u);
    std::uint32_t entry = 0;
    std::memcpy(&entry, seqBytes.data(), sizeof(entry));
    CHECK(entry == 0x00000800u);

    const auto disk = fo4::read_plugin_file(path);
    const auto key = fo4::FormKey::parse(questKey);
    CHECK(zstringOf(subrecordOf(disk, key, "FULL")) == "Welcome");
    const auto stages = fo4::forms::decode_quest_stages(disk, key);
    REQUIRE(stages.stages.size() == 2u);
    CHECK(stages.stages[0].index == 10);
    CHECK(stages.stages[0].flags == 1);
    const auto objectives = fo4::forms::decode_quest_objectives(disk, key);
    REQUIRE(objectives.objectives.size() == 1u);
    REQUIRE(objectives.objectives[0].targets.size() == 1u);
    CHECK(objectives.objectives[0].targets[0].alias == 1);
    const auto aliases = fo4::forms::decode_quest_aliases(disk, key);
    REQUIRE(aliases.aliases.size() == 1u);
    CHECK(aliases.aliases[0].name == std::optional<std::string>{"Player"});

    const auto readStages = parseResult(backend.invokeBackend(
        "GetQuestStages", nlohmann::json::array({"Patch.esp", questKey})));
    REQUIRE(readStages.at("stages").size() == 2u);
    CHECK(readStages.at("stages")[0].at("index") == 10);
    CHECK(readStages.at("stages")[0].at("flags") == 1);
    CHECK(readStages.at("stages")[0].at("logEntries")[0].at("logText") == "Caf\xC3\xA9");
    CHECK(readStages.at("stages")[1].at("logEntries")[0].at("note") == "Caf\xC3\xA9");

    const auto readAliases = parseResult(backend.invokeBackend(
        "GetQuestAliases", nlohmann::json::array({"Patch.esp", questKey})));
    REQUIRE(readAliases.at("aliases").size() == 1u);
    CHECK(readAliases.at("aliases")[0].at("name") == "Player");
    CHECK(readAliases.at("aliases")[0].at("kind") == "reference");

    const auto readObjectives = parseResult(backend.invokeBackend(
        "GetQuestObjectives", nlohmann::json::array({"Patch.esp", questKey})));
    CHECK(readObjectives.at("objectives")[0].at("displayText") == "Caf\xC3\xA9");
    CHECK(readObjectives.at("objectives")[0].at("targets")[0].at("aliasName") == "Player");
}

// Detects: toggling only startUp on an existing stage must not erase its unrelated INDX flag bits.
TEST_CASE("Native quest stage startUp bit round-trips and preserves unrelated flags on startup-only updates") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, emptyPlugin(0u));

    fo4ide::plugin::RadiclePluginBackend backend;
    loadPatch(backend, path);
    const auto created = parseResult(backend.invokeBackend(
        "CreateRecord", nlohmann::json::array({"Patch.esp", "QUST", nlohmann::json{
            {"editorId", "StartupFlagQuest"}, {"name", "Startup flag test"}}})));
    const auto questKey = created.at("formKey").get<std::string>();
    const auto initial = nlohmann::json::array({
        {{"index", 10}, {"flags", 0xA5}, {"startUp", true}},
        {{"index", 20}, {"flags", 0xA7}, {"startUp", false}}
    });
    REQUIRE(backend.invokeBackend(
        "SetQuestStages", nlohmann::json::array({"Patch.esp", questKey, initial})).has_value());
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    auto read = parseResult(backend.invokeBackend(
        "GetQuestStages", nlohmann::json::array({"Patch.esp", questKey})));
    REQUIRE(read.at("stages").size() == 2u);
    CHECK(read["stages"][0]["startUp"] == true);
    CHECK(read["stages"][0]["flags"] == 0xA7);
    CHECK(read["stages"][1]["startUp"] == false);
    CHECK(read["stages"][1]["flags"] == 0xA5);
    CHECK(read["stages"][0]["flagNames"] == nlohmann::json::array({"RunOnStart", "RunOnStop"}));
    CHECK(read["stages"][0]["unknownFlagBits"] == 0xA1);
    CHECK(read["stages"][1]["flagNames"] == nlohmann::json::array({"RunOnStop"}));
    CHECK(read["stages"][1]["unknownFlagBits"] == 0xA1);

    const auto update = nlohmann::json::array({
        {{"index", 10}, {"startUp", false}},
        {{"index", 30}}
    });
    REQUIRE(backend.invokeBackend(
        "SetQuestStages", nlohmann::json::array({"Patch.esp", questKey, update})).has_value());
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    auto decoded = fo4::forms::decode_quest_stages(fo4::read_plugin_file(path), fo4::FormKey::parse(questKey));
    REQUIRE(decoded.stages.size() == 2u);
    CHECK(decoded.stages[0].flags == 0xA5u);
    CHECK(decoded.stages[1].flags == 0u);

    const auto beforeInvalid = readBytes(path);
    const auto invalid = backend.invokeBackend(
        "SetQuestStages", nlohmann::json::array({"Patch.esp", questKey,
            nlohmann::json::array({{{"index", 10}, {"startUp", 1}}})}));
    REQUIRE_FALSE(invalid.has_value());
    CHECK(invalid.error().type == "InvalidQuestStagePayload");
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    CHECK(readBytes(path) == beforeInvalid);
}

// Detects: GetQuestStages and GetQuestObjectives must name the known flag bits and report the unnamed remainder, leaving the raw flags unchanged.
TEST_CASE("Native quest readback names known stage and objective flag bits and reports the unknown remainder") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, emptyPlugin(0u));

    fo4ide::plugin::RadiclePluginBackend backend;
    loadPatch(backend, path);
    const auto created = parseResult(backend.invokeBackend(
        "CreateRecord", nlohmann::json::array({"Patch.esp", "QUST", nlohmann::json{
            {"editorId", "FlagNameQuest"}, {"name", "Flag name test"}}})));
    const auto questKey = created.at("formKey").get<std::string>();
    REQUIRE(backend.invokeBackend("SetQuestStages", nlohmann::json::array({"Patch.esp", questKey,
        nlohmann::json::array({{{"index", 10}, {"flags", 0x0F}}, {{"index", 20}, {"flags", 0}}})})).has_value());
    REQUIRE(backend.invokeBackend("SetQuestObjectives", nlohmann::json::array({"Patch.esp", questKey,
        nlohmann::json::array({{{"index", 10}, {"flags", 0x80000003u}, {"displayText", "Go"},
                                {"targets", nlohmann::json::array()}}})})).has_value());
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    auto stages = parseResult(backend.invokeBackend(
        "GetQuestStages", nlohmann::json::array({"Patch.esp", questKey})));
    REQUIRE(stages.at("stages").size() == 2u);
    CHECK(stages["stages"][0]["flags"] == 0x0F);
    CHECK(stages["stages"][0]["flagNames"] == nlohmann::json::array(
        {"RunOnStart", "RunOnStop", "KeepInstanceDataFromHereOn"}));
    CHECK(stages["stages"][0]["unknownFlagBits"] == 0x01);
    CHECK(stages["stages"][1]["flagNames"] == nlohmann::json::array());
    CHECK(stages["stages"][1]["unknownFlagBits"] == 0);

    auto objectives = parseResult(backend.invokeBackend(
        "GetQuestObjectives", nlohmann::json::array({"Patch.esp", questKey})));
    REQUIRE(objectives.at("objectives").size() == 1u);
    CHECK(objectives["objectives"][0]["flags"] == 0x80000003u);
    CHECK(objectives["objectives"][0]["flagNames"] == nlohmann::json::array(
        {"ORedWithPrevious", "NoStatsTracking"}));
    CHECK(objectives["objectives"][0]["unknownFlagBits"] == 0x80000000u);
}
