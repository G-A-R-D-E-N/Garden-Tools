#include <catch2/catch_test_macros.hpp>
#include "../helpers/TempRoot.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <fo4/plugin/reader.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = fo4ide_test::uniqueTempName();
    return std::filesystem::temp_directory_path() / ("fo4ide_memory_" + id);
}

struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

void writeBytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    REQUIRE(stream.good());
}

std::vector<std::byte> makeBasePlugin(bool withSpare = false) {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(0);
    hedr.u32(0x900);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto weaponGroup = fo4::test::begin_group(out, "WEAP", 0);
    const auto weapon = fo4::test::begin_record(out, "WEAP", 0, 0x00000800);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"Weapon\0", 7});
    fo4::test::end_record(out, weapon);
    if (withSpare) {
        const auto spare = fo4::test::begin_record(out, "WEAP", 0, 0x00000801);
        fo4::test::write_subrecord(out, "EDID", std::string_view{"Spare\0", 6});
        fo4::test::end_record(out, spare);
    }
    fo4::test::end_group(out, weaponGroup);
    return out.bytes;
}

std::vector<std::byte> makeOverridePlugin() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(0);
    hedr.u32(0x900);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::write_subrecord(out, "MAST", std::string_view{"Base.esp\0", 9});
    fo4::test::ByteWriter data;
    data.u32(0);
    data.u32(0);
    fo4::test::write_subrecord(out, "DATA", data.bytes);
    fo4::test::end_record(out, tes4);
    return out.bytes;
}

nlohmann::json pluginEntry(const std::string& name, const std::filesystem::path& path) {
    return {{"name", name}, {"path", path.string()}, {"source", name}, {"resolved", true}};
}

int worklistTotal(fo4ide::plugin::RadiclePluginBackend& backend, const std::string& name) {
    const auto result = backend.invoke("backend.GetPluginWorklist", nlohmann::json::array({name}));
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    return nlohmann::json::parse(result->get<std::string>()).at("Total").get<int>();
}

}

TEST_CASE("Native release_memory drop_conflict_graph keeps an edit visible to later graph queries") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Base.esp", makeBasePlugin());
    writeBytes(root / "Top.esp", makeOverridePlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", nlohmann::json{{"plugins", nlohmann::json::array({
        pluginEntry("Base.esp", root / "Base.esp"),
        pluginEntry("Top.esp", root / "Top.esp")
    })}}).has_value());
    const auto before = worklistTotal(backend, "Top.esp");

    REQUIRE(backend.invoke("backend.CopyAsOverride",
        nlohmann::json::array({"Base.esp", "000800:Base.esp", "Top.esp", false})).has_value());
    const auto edited = worklistTotal(backend, "Top.esp");
    CHECK(edited == before + 1);

    const auto released = backend.releaseMemory(nlohmann::json::array({true}));
    REQUIRE(released.has_value());
    CHECK(released->get<std::string>().find("Dropped the cached conflict graph") != std::string::npos);
    CHECK(worklistTotal(backend, "Top.esp") == edited);
}

#ifdef FO4IDE_TEST_HOOKS
// Invariant: a dropped graph must be destroyed, not pinned by a proxy snapshot.
TEST_CASE("Native release_memory drop_conflict_graph destroys the cached graph") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Base.esp", makeBasePlugin());
    writeBytes(root / "Top.esp", makeOverridePlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", nlohmann::json{{"plugins", nlohmann::json::array({
        pluginEntry("Base.esp", root / "Base.esp"),
        pluginEntry("Top.esp", root / "Top.esp")
    })}}).has_value());
    worklistTotal(backend, "Base.esp");
    const auto graph = backend.cachedGraphForTest();
    REQUIRE_FALSE(graph.expired());

    REQUIRE(backend.releaseMemory(nlohmann::json::array({true})).has_value());
    CHECK(graph.expired());
}
#endif

TEST_CASE("Native release_memory trims the working set and keeps the backend usable") {
    fo4ide::plugin::RadiclePluginBackend backend;
    const auto result = backend.releaseMemory(nlohmann::json::array());
    REQUIRE(result.has_value());
    const auto message = result->get<std::string>();
    CHECK(message.find("Trimmed the process working set") != std::string::npos);
    CHECK(message.find("stays loaded") != std::string::npos);
}

namespace {

std::string editorIdInBackend(fo4ide::plugin::RadiclePluginBackend& backend) {
    const auto result =
        backend.invoke("backend.GetConflictMatrix", nlohmann::json::array({"000800:Base.esp"}));
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    return nlohmann::json::parse(result->get<std::string>()).at("EditorID").get<std::string>();
}

std::string editorIdInFile(const std::filesystem::path& path) {
    const auto plugin = fo4::read_plugin_file(path);
    const auto* record = fo4::find_record(plugin, fo4::FormKey::parse("000800:Base.esp"));
    REQUIRE(record != nullptr);
    const auto edid = record->subrecords.first_nonempty_data(fo4::sig_edid);
    REQUIRE(edid.has_value());
    const std::string text(reinterpret_cast<const char*>(edid->data()), edid->size());
    return text.substr(0, text.find('\0'));
}

void loadBase(fo4ide::plugin::RadiclePluginBackend& backend, const std::filesystem::path& path) {
    REQUIRE(backend.invoke("environment.LoadMo2", nlohmann::json{{"plugins", nlohmann::json::array({
        pluginEntry("Base.esp", path)
    })}}).has_value());
}

}

TEST_CASE("Native release_memory keeps an unsaved field edit readable") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Base.esp", makeBasePlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBase(backend, root / "Base.esp");
    REQUIRE(backend.invokeBackend("SetField", nlohmann::json::array({
        "Base.esp", "000800:Base.esp", "Subrecords\\EDID[0]", "EditedWeapon"
    })).has_value());

    REQUIRE(backend.releaseMemory(nlohmann::json::array({true})).has_value());
    CHECK(editorIdInBackend(backend) == "EditedWeapon");
}

#ifdef FO4IDE_TEST_HOOKS
namespace {

std::vector<std::byte> readBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    REQUIRE(stream.good());
    std::vector<std::byte> bytes(static_cast<std::size_t>(stream.tellg()));
    stream.seekg(0);
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(stream.good());
    return bytes;
}

struct ReleaseDuringSave {
    fo4ide::plugin::RadiclePluginBackend* backend{nullptr};
    std::atomic<bool> releaseFinished{false};
    std::atomic<bool> finishedWhileSaving{false};
    std::thread releaser;

    ~ReleaseDuringSave() {
        if (releaser.joinable()) releaser.join();
    }
};

ReleaseDuringSave* releaseDuringSave = nullptr;

void releaseWhileSaveHoldsIo() {
    auto* probe = releaseDuringSave;
    if (probe == nullptr) return;
    releaseDuringSave = nullptr;
    probe->releaser = std::thread([probe] {
        (void)probe->backend->releaseMemory(nlohmann::json::array({true}));
        probe->releaseFinished = true;
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!probe->releaseFinished && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    probe->finishedWhileSaving = probe->releaseFinished.load();
}

std::vector<std::byte> saveEditedBase(const std::filesystem::path& dir, ReleaseDuringSave* probe,
                                      fo4ide::plugin::RadiclePluginBackend& backend) {
    writeBytes(dir / "Base.esp", makeBasePlugin(true));
    writeBytes(dir / "Idle.esp", makeBasePlugin(true));
    REQUIRE(backend.invoke("environment.LoadMo2", nlohmann::json{{"plugins", nlohmann::json::array({
        pluginEntry("Base.esp", dir / "Base.esp"),
        pluginEntry("Idle.esp", dir / "Idle.esp")
    })}}).has_value());
    backend.decodeSubrecordsForTest("Base.esp", "000801:Base.esp");
    backend.decodeSubrecordsForTest("Idle.esp", "000801:Idle.esp");
    REQUIRE(backend.invokeBackend("SetField", nlohmann::json::array({
        "Base.esp", "000800:Base.esp", "Subrecords\\EDID[0]", "EditedWeapon"
    })).has_value());

    struct HookReset {
        ~HookReset() {
            fo4ide::plugin::setSaveIoLockedHook(nullptr);
            releaseDuringSave = nullptr;
        }
    } hookReset;
    if (probe != nullptr) {
        releaseDuringSave = probe;
        fo4ide::plugin::setSaveIoLockedHook(&releaseWhileSaveHoldsIo);
    }
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Base.esp"})).has_value());
    return readBytes(dir / "Base.esp");
}

}

TEST_CASE("Native release_memory skips a plugin whose save is in flight and still releases an idle one") {
    const auto root = tempRoot();
    Cleanup cleanup{root};

    fo4ide::plugin::RadiclePluginBackend control;
    const auto controlBytes = saveEditedBase(root / "control", nullptr, control);

    fo4ide::plugin::RadiclePluginBackend backend;
    ReleaseDuringSave probe;
    probe.backend = &backend;
    const auto savedBytes = saveEditedBase(root / "hooked", &probe, backend);
    if (probe.releaser.joinable()) probe.releaser.join();

    CHECK(probe.finishedWhileSaving.load());
    CHECK(backend.subrecordsMaterializedForTest("Base.esp", "000801:Base.esp"));
    CHECK_FALSE(backend.subrecordsMaterializedForTest("Idle.esp", "000801:Idle.esp"));
    CHECK(savedBytes == controlBytes);
    CHECK(editorIdInFile(root / "hooked" / "Base.esp") == "EditedWeapon");
    CHECK(editorIdInBackend(backend) == "EditedWeapon");
}
#endif
