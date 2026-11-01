#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <latch>
#include <string_view>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/flags.hpp>
#include <fo4/forms/quest.hpp>
#include <fo4/localization/strings.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_pending_lifecycle_" + std::to_string(id));
}

struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

void writeBytes(const std::filesystem::path& path,
                const std::vector<std::byte>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    REQUIRE(stream.good());
}

std::vector<std::byte> readBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    REQUIRE(stream.good());
    const auto size = stream.tellg();
    REQUIRE(size >= 0);
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    stream.seekg(0);
    if (!bytes.empty()) {
        stream.read(reinterpret_cast<char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));
        REQUIRE(stream.good());
    }
    return bytes;
}

std::vector<std::byte> rawBytes(std::string_view text) {
    std::vector<std::byte> bytes;
    bytes.reserve(text.size());
    for (const auto ch : text) {
        bytes.push_back(std::byte{static_cast<unsigned char>(ch)});
    }
    return bytes;
}

void writeU32(fo4::test::ByteWriter& out,
              std::string_view signature,
              std::uint32_t value) {
    fo4::test::ByteWriter data;
    data.u32(value);
    fo4::test::write_subrecord(out, signature, data.bytes);
}

std::vector<std::byte> makeLocalizedPlugin() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(
        out, "TES4", fo4::RecordFlags::localized, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(2);
    hedr.u32(0x802);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto refrGroup = fo4::test::begin_group(out, "REFR", 0);
    const auto refr = fo4::test::begin_record(out, "REFR", 0, 0x00000800);
    writeU32(out, "XHLT", 50);
    fo4::test::end_record(out, refr);
    fo4::test::end_group(out, refrGroup);

    const auto questGroup = fo4::test::begin_group(out, "QUST", 0);
    const auto quest = fo4::test::begin_record(out, "QUST", 0, 0x00000801);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"LifecycleQuest\0", 15});
    fo4::test::ByteWriter indx;
    indx.u16(10);
    indx.u8(0);
    indx.u8(0);
    fo4::test::write_subrecord(out, "INDX", indx.bytes);
    fo4::test::write_subrecord(out, "QSDT", std::vector<std::byte>{std::byte{0}});
    writeU32(out, "CNAM", 42);
    fo4::test::end_record(out, quest);
    fo4::test::end_group(out, questGroup);
    return out.bytes;
}

std::vector<std::byte> makeSecondPlugin() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(0x801);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto refrGroup = fo4::test::begin_group(out, "REFR", 0);
    const auto refr = fo4::test::begin_record(out, "REFR", 0, 0x00000800);
    writeU32(out, "XHLT", 10);
    fo4::test::end_record(out, refr);
    fo4::test::end_group(out, refrGroup);
    return out.bytes;
}

void writeLocalizationSet(const std::filesystem::path& root) {
    const auto directory = root / "Strings";
    std::filesystem::create_directories(directory);
    fo4::localization::write_string_table_file(
        {fo4::localization::StringTableKind::strings,
         {{100, rawBytes("Other")}}},
        directory / "Patch_en.STRINGS");
    fo4::localization::write_string_table_file(
        {fo4::localization::StringTableKind::dlstrings,
         {{42, rawBytes("Original")}}},
        directory / "Patch_en.DLSTRINGS");
    fo4::localization::write_string_table_file(
        {fo4::localization::StringTableKind::ilstrings,
         {{200, rawBytes("Other")}}},
        directory / "Patch_en.ILSTRINGS");
}

void loadEnvironment(fo4ide::plugin::RadiclePluginBackend& backend,
                     const std::vector<std::pair<std::string, std::filesystem::path>>& plugins) {
    nlohmann::json rows = nlohmann::json::array();
    for (const auto& [name, path] : plugins) {
        rows.push_back({
            {"name", name},
            {"path", path.string()},
            {"source", "PendingLifecycle"},
            {"resolved", true}
        });
    }
    REQUIRE(backend.invoke(
        "environment.LoadMo2",
        nlohmann::json{{"plugins", std::move(rows)}}).has_value());
}

std::uint32_t healthValue(const fo4::Plugin& plugin, std::string_view formKey) {
    const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(formKey));
    REQUIRE(record != nullptr);
    for (const auto& subrecord : record->subrecords) {
        if (subrecord.signature.to_string() != "XHLT") continue;
        REQUIRE(subrecord.data.size() == 4u);
        std::uint32_t value = 0;
        std::memcpy(&value, subrecord.data.data(), sizeof(value));
        return value;
    }
    REQUIRE_FALSE(true);
    return 0;
}

std::uint32_t healthOnDisk(const std::filesystem::path& path, std::string_view formKey) {
    return healthValue(fo4::read_plugin_file(path), formKey);
}

nlohmann::json activePlugins(fo4ide::plugin::RadiclePluginBackend& backend) {
    const auto raw = backend.invoke("backend.GetActivePlugins", nlohmann::json::array());
    REQUIRE(raw.has_value());
    REQUIRE(raw->is_string());
    return nlohmann::json::parse(raw->get<std::string>());
}

bool pluginPending(const nlohmann::json& rows, std::string_view name) {
    for (const auto& row : rows) {
        if (row.at("Name").get<std::string>() != name) continue;
        REQUIRE(row.contains("Pending"));
        return row.at("Pending").get<bool>();
    }
    REQUIRE_FALSE(true);
    return false;
}

}

TEST_CASE("Localized CNAM stages sidecars until SavePlugin") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makeLocalizedPlugin());
    writeLocalizationSet(root);
    const auto pluginBefore = readBytes(path);
    const auto dlBefore = readBytes(root / "Strings" / "Patch_en.DLSTRINGS");

    fo4ide::plugin::RadiclePluginBackend backend;
    loadEnvironment(backend, {{"Patch.esp", path}});

    const auto staged = backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({
            "Patch.esp",
            "000801:Patch.esp",
            nlohmann::json::array({{{"index", 10}, {"logEntry", "Staged"}}}),
            nlohmann::json{
                {"language", "en"},
                {"stringsDirectory", (root / "Strings").string()}
            }
        }));
    REQUIRE(staged.has_value());
    CHECK(staged->get<std::string>().find("SavePlugin") != std::string::npos);
    CHECK(readBytes(path) == pluginBefore);
    CHECK(readBytes(root / "Strings" / "Patch_en.DLSTRINGS") == dlBefore);
    CHECK(pluginPending(activePlugins(backend), "Patch.esp"));
    const auto treeRaw = backend.invoke("appInterop.GetPlugins", nlohmann::json::array());
    REQUIRE(treeRaw.has_value());
    const auto tree = nlohmann::json::parse(treeRaw->get<std::string>());
    REQUIRE(tree.size() == 1u);
    CHECK(tree[0].at("Key") == "Patch.esp");
    CHECK(tree[0].at("Pending").get<bool>());

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    CHECK(readBytes(path) != pluginBefore);
    const auto table = fo4::localization::read_string_table_file(
        root / "Strings" / "Patch_en.DLSTRINGS",
        fo4::localization::StringTableKind::dlstrings);
    REQUIRE(fo4::localization::find_string(table, 42u) != nullptr);
    REQUIRE(fo4::localization::find_string(table, 201u) != nullptr);
    CHECK_FALSE(pluginPending(activePlugins(backend), "Patch.esp"));
}

TEST_CASE("Repeated localized CNAM edits accumulate in staged sidecars until SavePlugin") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makeLocalizedPlugin());
    writeLocalizationSet(root);
    const auto pluginBefore = readBytes(path);
    const auto dlBefore = readBytes(root / "Strings" / "Patch_en.DLSTRINGS");
    const auto context = nlohmann::json{
        {"language", "en"},
        {"stringsDirectory", (root / "Strings").string()}
    };

    fo4ide::plugin::RadiclePluginBackend backend;
    loadEnvironment(backend, {{"Patch.esp", path}});

    const auto first = backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({
            "Patch.esp",
            "000801:Patch.esp",
            nlohmann::json::array({{{"index", 10}, {"logEntry", "First"}}}),
            context
        }));
    REQUIRE(first.has_value());
    CHECK(readBytes(path) == pluginBefore);
    CHECK(readBytes(root / "Strings" / "Patch_en.DLSTRINGS") == dlBefore);

    const auto second = backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({
            "Patch.esp",
            "000801:Patch.esp",
            nlohmann::json::array({{{"index", 10}, {"logEntry", "Second"}}}),
            context
        }));
    REQUIRE(second.has_value());
    CHECK(second->get<std::string>().find("SavePlugin") != std::string::npos);
    CHECK(readBytes(path) == pluginBefore);
    CHECK(readBytes(root / "Strings" / "Patch_en.DLSTRINGS") == dlBefore);
    CHECK(pluginPending(activePlugins(backend), "Patch.esp"));

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto table = fo4::localization::read_string_table_file(
        root / "Strings" / "Patch_en.DLSTRINGS",
        fo4::localization::StringTableKind::dlstrings);
    REQUIRE(fo4::localization::find_string(table, 42u) != nullptr);
    const auto* firstEntry = fo4::localization::find_string(table, 201u);
    const auto* secondEntry = fo4::localization::find_string(table, 202u);
    REQUIRE(firstEntry != nullptr);
    REQUIRE(secondEntry != nullptr);
    CHECK(firstEntry->text == rawBytes("First"));
    CHECK(secondEntry->text == rawBytes("Second"));

    const auto disk = fo4::read_plugin_file(path);
    const auto decoded = fo4::forms::decode_quest_stages(
        disk, fo4::FormKey::parse("000801:Patch.esp"));
    REQUIRE(decoded.stages.size() == 1u);
    REQUIRE(decoded.stages[0].log_entries.size() == 1u);
    REQUIRE(decoded.stages[0].log_entries[0].log_text.has_value());
    CHECK(decoded.stages[0].log_entries[0].log_text->id == 202u);
}

TEST_CASE("DiscardPlugin restores persisted state without writing disk") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makeLocalizedPlugin());
    writeLocalizationSet(root);
    const auto pluginBefore = readBytes(path);
    const auto dlBefore = readBytes(root / "Strings" / "Patch_en.DLSTRINGS");

    fo4ide::plugin::RadiclePluginBackend backend;
    loadEnvironment(backend, {{"Patch.esp", path}});

    REQUIRE(backend.setFieldValue(nlohmann::json::array({
        "Patch.esp", "000800:Patch.esp", "REFR\\HealthPercent", "75"
    })).has_value());
    REQUIRE(backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({
            "Patch.esp",
            "000801:Patch.esp",
            nlohmann::json::array({{{"index", 10}, {"logEntry", "Pending"}}}),
            nlohmann::json{
                {"language", "en"},
                {"stringsDirectory", (root / "Strings").string()}
            }
        })).has_value());
    CHECK(pluginPending(activePlugins(backend), "Patch.esp"));

    const auto discarded = backend.invoke(
        "backend.DiscardPlugin", nlohmann::json::array({"Patch.esp"}));
    REQUIRE(discarded.has_value());
    CHECK(readBytes(path) == pluginBefore);
    CHECK(readBytes(root / "Strings" / "Patch_en.DLSTRINGS") == dlBefore);
    CHECK_FALSE(pluginPending(activePlugins(backend), "Patch.esp"));
    CHECK(healthOnDisk(path, "000800:Patch.esp") == 50u);

    const auto matrix = backend.invoke(
        "backend.GetRecordsGrid",
        nlohmann::json::array({"Patch.esp", "REFR"}));
    REQUIRE(matrix.has_value());
    const auto grid = nlohmann::json::parse(matrix->get<std::string>());
    REQUIRE(grid.at("rows").size() == 1u);
    CHECK(grid.at("rows")[0].at("cells").get<std::vector<std::string>>().size() >= 1u);

    REQUIRE(backend.invoke(
        "backend.OpenPlugin",
        nlohmann::json::array({path.string()})).has_value());
}

TEST_CASE("DiscardPlugin is scoped to one plugin") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto firstPath = root / "Patch.esp";
    const auto secondPath = root / "Other.esp";
    writeBytes(firstPath, makeLocalizedPlugin());
    writeBytes(secondPath, makeSecondPlugin());
    writeLocalizationSet(root);
    const auto secondBefore = readBytes(secondPath);

    fo4ide::plugin::RadiclePluginBackend backend;
    loadEnvironment(backend, {
        {"Patch.esp", firstPath},
        {"Other.esp", secondPath}
    });

    REQUIRE(backend.setFieldValue(nlohmann::json::array({
        "Patch.esp", "000800:Patch.esp", "REFR\\HealthPercent", "75"
    })).has_value());
    REQUIRE(backend.setFieldValue(nlohmann::json::array({
        "Other.esp", "000800:Other.esp", "REFR\\HealthPercent", "20"
    })).has_value());
    CHECK(pluginPending(activePlugins(backend), "Patch.esp"));
    CHECK(pluginPending(activePlugins(backend), "Other.esp"));

    REQUIRE(backend.invoke(
        "backend.DiscardPlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    CHECK_FALSE(pluginPending(activePlugins(backend), "Patch.esp"));
    CHECK(pluginPending(activePlugins(backend), "Other.esp"));
    CHECK(readBytes(secondPath) == secondBefore);

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Other.esp"})).has_value());
    CHECK(healthOnDisk(secondPath, "000800:Other.esp") == 20u);
    CHECK(healthOnDisk(firstPath, "000800:Patch.esp") == 50u);
}

TEST_CASE("DiscardPlugin refuses PluginChangedOnDisk and keeps the edit pending when the file changed") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makeLocalizedPlugin());
    writeLocalizationSet(root);

    fo4ide::plugin::RadiclePluginBackend backend;
    loadEnvironment(backend, {{"Patch.esp", path}});
    REQUIRE(backend.setFieldValue(nlohmann::json::array({
        "Patch.esp", "000800:Patch.esp", "REFR\\HealthPercent", "75"
    })).has_value());
    CHECK(pluginPending(activePlugins(backend), "Patch.esp"));

    const auto replacement = makeSecondPlugin();
    writeBytes(path, replacement);
    std::filesystem::last_write_time(
        path, std::filesystem::last_write_time(path) + std::chrono::seconds(60));

    const auto discarded = backend.invoke(
        "backend.DiscardPlugin", nlohmann::json::array({"Patch.esp"}));
    REQUIRE_FALSE(discarded.has_value());
    CHECK(discarded.error().type == "PluginChangedOnDisk");
    CHECK(pluginPending(activePlugins(backend), "Patch.esp"));
    CHECK(readBytes(path) == replacement);
}

#ifdef FO4IDE_TEST_HOOKS
std::filesystem::path discardTouchedPath;

void touchDiscardPathLater() {
    std::filesystem::last_write_time(
        discardTouchedPath,
        std::filesystem::last_write_time(discardTouchedPath) + std::chrono::seconds(60));
}

// Invariant: discard must refuse a file that changes after its read.
TEST_CASE("DiscardPlugin refuses PluginChangedOnDisk when the file changes during its read") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makeLocalizedPlugin());
    writeLocalizationSet(root);

    fo4ide::plugin::RadiclePluginBackend backend;
    loadEnvironment(backend, {{"Patch.esp", path}});
    REQUIRE(backend.setFieldValue(nlohmann::json::array({
        "Patch.esp", "000800:Patch.esp", "REFR\\HealthPercent", "75"
    })).has_value());

    discardTouchedPath = path;
    fo4ide::plugin::setLoadReadWindowHook(&touchDiscardPathLater);
    const auto discarded = backend.invoke(
        "backend.DiscardPlugin", nlohmann::json::array({"Patch.esp"}));
    fo4ide::plugin::setLoadReadWindowHook(nullptr);

    REQUIRE_FALSE(discarded.has_value());
    CHECK(discarded.error().type == "PluginChangedOnDisk");
    CHECK(pluginPending(activePlugins(backend), "Patch.esp"));
}
#endif

TEST_CASE("LoadMo2 refuses to replace pending plugins unless discardPending is set") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makeLocalizedPlugin());
    writeLocalizationSet(root);
    const auto pluginBefore = readBytes(path);
    const auto environment = nlohmann::json{{"plugins", nlohmann::json::array({
        {{"name", "Patch.esp"}, {"path", path.string()}, {"source", "PendingLifecycle"}, {"resolved", true}}
    })}};

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());
    REQUIRE(backend.setFieldValue(nlohmann::json::array({
        "Patch.esp", "000800:Patch.esp", "REFR\\HealthPercent", "75"
    })).has_value());
    CHECK(pluginPending(activePlugins(backend), "Patch.esp"));

    const auto blocked = backend.invoke("environment.LoadMo2", environment);
    REQUIRE_FALSE(blocked.has_value());
    CHECK(blocked.error().type == "PluginHasPendingChanges");
    CHECK(blocked.error().message.find("Patch.esp") != std::string::npos);
    CHECK(pluginPending(activePlugins(backend), "Patch.esp"));
    CHECK(readBytes(path) == pluginBefore);

    auto discarding = environment;
    discarding["discardPending"] = true;
    REQUIRE(backend.invoke("environment.LoadMo2", discarding).has_value());
    CHECK_FALSE(pluginPending(activePlugins(backend), "Patch.esp"));
    CHECK(healthOnDisk(path, "000800:Patch.esp") == 50u);
    CHECK(readBytes(path) == pluginBefore);
}

#ifdef FO4IDE_TEST_HOOKS
std::latch* loadMo2AfterCheckLatch = nullptr;
std::latch* loadMo2ResumeLatch = nullptr;

void loadMo2BarrierHook() {
    if (loadMo2AfterCheckLatch) loadMo2AfterCheckLatch->count_down();
    if (loadMo2ResumeLatch) loadMo2ResumeLatch->wait();
}

struct LoadMo2Barrier {
    std::latch afterCheck{1};
    std::latch resume{1};

    LoadMo2Barrier() {
        loadMo2AfterCheckLatch = &afterCheck;
        loadMo2ResumeLatch = &resume;
        fo4ide::plugin::setLoadMo2AfterPendingCheckHook(&loadMo2BarrierHook);
    }

    ~LoadMo2Barrier() {
        fo4ide::plugin::setLoadMo2AfterPendingCheckHook(nullptr);
        loadMo2AfterCheckLatch = nullptr;
        loadMo2ResumeLatch = nullptr;
    }

    void release() {
        if (!resume.try_wait()) resume.count_down();
    }
};

struct JoinReload {
    std::thread& thread;
    LoadMo2Barrier& barrier;
    ~JoinReload() {
        barrier.release();
        if (thread.joinable()) thread.join();
    }
};
#endif

TEST_CASE("LoadMo2 fails closed if a plugin becomes pending after the first check") {
#ifdef FO4IDE_TEST_HOOKS
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makeLocalizedPlugin());
    writeLocalizationSet(root);
    const auto pluginBefore = readBytes(path);
    const auto environment = nlohmann::json{{"plugins", nlohmann::json::array({
        {{"name", "Patch.esp"}, {"path", path.string()}, {"source", "PendingLifecycle"}, {"resolved", true}}
    })}};

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());
    CHECK_FALSE(pluginPending(activePlugins(backend), "Patch.esp"));

    LoadMo2Barrier barrier;
    fo4ide::Result<nlohmann::json> reloadResult = nlohmann::json(nullptr);
    std::thread reloader([&] {
        reloadResult = backend.invoke("environment.LoadMo2", environment);
    });
    JoinReload join{reloader, barrier};
    barrier.afterCheck.wait();
    REQUIRE(backend.setFieldValue(nlohmann::json::array({
        "Patch.esp", "000800:Patch.esp", "REFR\\HealthPercent", "75"
    })).has_value());
    CHECK(pluginPending(activePlugins(backend), "Patch.esp"));
    barrier.release();
    if (reloader.joinable()) reloader.join();

    REQUIRE_FALSE(reloadResult.has_value());
    CHECK(reloadResult.error().type == "PluginHasPendingChanges");
    CHECK(reloadResult.error().message.find("Patch.esp") != std::string::npos);
    CHECK(pluginPending(activePlugins(backend), "Patch.esp"));
    CHECK(readBytes(path) == pluginBefore);
    CHECK(healthOnDisk(path, "000800:Patch.esp") == 50u);
#else
    SUCCEED("FO4IDE_TEST_HOOKS is required for the LoadMo2 pending-check race");
#endif
}

TEST_CASE("LoadMo2 discardPending does not drop an edit created after consent") {
#ifdef FO4IDE_TEST_HOOKS
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makeLocalizedPlugin());
    writeLocalizationSet(root);
    const auto pluginBefore = readBytes(path);
    const auto environment = nlohmann::json{{"plugins", nlohmann::json::array({
        {{"name", "Patch.esp"}, {"path", path.string()}, {"source", "PendingLifecycle"}, {"resolved", true}}
    })}};

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());
    REQUIRE(backend.setFieldValue(nlohmann::json::array({
        "Patch.esp", "000800:Patch.esp", "REFR\\HealthPercent", "60"
    })).has_value());

    auto discarding = environment;
    discarding["discardPending"] = true;
    LoadMo2Barrier barrier;
    fo4ide::Result<nlohmann::json> reloadResult = nlohmann::json(nullptr);
    std::thread reloader([&] {
        reloadResult = backend.invoke("environment.LoadMo2", discarding);
    });
    JoinReload join{reloader, barrier};
    barrier.afterCheck.wait();
    REQUIRE(backend.setFieldValue(nlohmann::json::array({
        "Patch.esp", "000800:Patch.esp", "REFR\\HealthPercent", "75"
    })).has_value());
    barrier.release();
    if (reloader.joinable()) reloader.join();

    REQUIRE_FALSE(reloadResult.has_value());
    CHECK(reloadResult.error().type == "PluginHasPendingChanges");
    CHECK(pluginPending(activePlugins(backend), "Patch.esp"));
    CHECK(readBytes(path) == pluginBefore);
#else
    SUCCEED("FO4IDE_TEST_HOOKS is required for the LoadMo2 discard consent race");
#endif
}

TEST_CASE("LoadMo2 discardPending does not drop a SetLight made after consent") {
#ifdef FO4IDE_TEST_HOOKS
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makeLocalizedPlugin());
    writeLocalizationSet(root);
    const auto pluginBefore = readBytes(path);
    const auto environment = nlohmann::json{{"plugins", nlohmann::json::array({
        {{"name", "Patch.esp"}, {"path", path.string()}, {"source", "PendingLifecycle"}, {"resolved", true}}
    })}};

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    auto discarding = environment;
    discarding["discardPending"] = true;
    LoadMo2Barrier barrier;
    fo4ide::Result<nlohmann::json> reloadResult = nlohmann::json(nullptr);
    std::thread reloader([&] {
        reloadResult = backend.invoke("environment.LoadMo2", discarding);
    });
    JoinReload join{reloader, barrier};
    barrier.afterCheck.wait();
    REQUIRE(backend.invoke(
        "masters.SetLight", nlohmann::json::array({"Patch.esp", true})).has_value());
    barrier.release();
    if (reloader.joinable()) reloader.join();

    REQUIRE_FALSE(reloadResult.has_value());
    CHECK(reloadResult.error().type == "PluginHasPendingChanges");
    CHECK(pluginPending(activePlugins(backend), "Patch.esp"));
    CHECK(readBytes(path) == pluginBefore);
#else
    SUCCEED("FO4IDE_TEST_HOOKS is required for the LoadMo2 SetLight consent race");
#endif
}
