#include "../../src/plugin/RadiclePluginBa2Source.hpp"
#include "../helpers/TempRoot.hpp"
#include "../../src/plugin/RadiclePluginHeldStrings.hpp"
#include "../../src/plugin/RadiclePluginJournalReadStrings.hpp"
#include "../../src/plugin/RadiclePluginJournalSession.hpp"
#include "plugin_bytes.hpp"

#include <catch2/catch_test_macros.hpp>
#include <fo4/archive/ba2_reader.hpp>
#include <fo4/io/sha256.hpp>
#include <fo4/flags.hpp>
#include <fo4/localization/strings.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <span>
#include <vector>

#ifdef __linux__
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {
class CountedSource final : public fo4::archive::Ba2ByteSource {
public:
    explicit CountedSource(std::vector<std::byte> data) : data_(std::move(data)) {}
    std::uint64_t size() const noexcept override { return data_.size(); }
    void read(std::uint64_t offset, std::span<std::byte> out) const override {
        maxRead_ = std::max(maxRead_, out.size());
        std::copy_n(data_.begin() + static_cast<std::ptrdiff_t>(offset), out.size(), out.begin());
    }
    std::size_t maxRead() const noexcept { return maxRead_; }
private:
    std::vector<std::byte> data_;
    mutable std::size_t maxRead_{0};
};
}

TEST_CASE("Byte source hashes stream in bounded chunks") {
    using namespace fo4ide::plugin::detail;
    std::vector<std::byte> data(256 * 1024, std::byte{0x5a});
    auto source = std::make_shared<CountedSource>(data);
    CHECK(fo4::archive::hash_byte_source(source) == fo4::io::sha256_hex(data));
    CHECK(source->maxRead() <= 64 * 1024);

    HeldStringTable table; table.filename = "Patch_en.STRINGS";
    table.bytes = {std::byte{static_cast<unsigned char>('o')}, std::byte{static_cast<unsigned char>('k')}};
    const auto held = journalReadStringInventoryFromHeld(std::filesystem::absolute("Data"), {}, true, {table}, "Patch.esp");
    REQUIRE(held.files.size() == 1); CHECK(held.files[0].size == 2);
    CHECK(held.files[0].sha256 == fo4::io::sha256_hex(table.bytes));
}

#ifdef __linux__
namespace {
using namespace fo4ide::plugin::detail;
struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); } };
std::filesystem::path tempRoot() {
    return std::filesystem::temp_directory_path() / ("journal_loose_strings_" +
        fo4ide_test::uniqueTempName());
}
void write(const std::filesystem::path& path, std::string_view value) {
    std::filesystem::create_directories(path.parent_path()); std::ofstream out(path, std::ios::binary);
    REQUIRE(out.good()); out.write(value.data(), static_cast<std::streamsize>(value.size())); REQUIRE(out.good());
}
std::string hash(std::string_view value) {
    return fo4::io::sha256_hex(std::span(reinterpret_cast<const std::byte*>(value.data()), value.size()));
}
std::vector<std::byte> raw(std::string_view value) {
    const auto bytes = std::as_bytes(std::span(value));
    return {bytes.begin(), bytes.end()};
}
void interruptAfterSidecar(std::size_t count) { if (count == 1) throw std::runtime_error("interrupted"); }
std::size_t interruptCount = 0;
void interruptAtCount(std::size_t count) { if (count == interruptCount) throw std::runtime_error("interrupted"); }
void terminateDuringSidecarStage() { ::_exit(91); }
struct ResetJournalHook { ~ResetJournalHook() { setJournalCommitHook(nullptr); } };
}

// Detects: loose string recovery must reject malformed, aliased, changed, or unauthorized source inventories before writes.
TEST_CASE("Loose string inventory is strict and bound to held read roots") {
    const auto root = tempRoot(); Cleanup cleanup{root};
    const auto data = root / "Data"; write(data / "frank.STRINGS" / "Patch_en.STRINGS", "table");
    auto lease = std::make_shared<Ba2ReadRoot>(Ba2ReadRoot::authorize(data));
    const std::vector<JournalApprovedReadRoot> approved{{data, lease}};
    const std::vector<JournalArchiveReference> refs{{data, {"frank.STRINGS"}, {}}};
    const auto plugin = std::filesystem::path("Patch.esp");
    const auto inventory = captureJournalReadStringInventories(refs, approved, plugin);
    REQUIRE(inventory.size() == 1); REQUIRE(inventory[0].files.size() == 1);
    CHECK(inventory[0].files[0].filename == "Patch_en.STRINGS");
    CHECK(inventory[0].files[0].size == 5); CHECK(inventory[0].files[0].sha256 == hash("table"));
    CHECK_NOTHROW(verifyJournalReadStringInventories(inventory, approved, plugin));

    const auto encoded = encodeJournalReadStringInventories(inventory, plugin);
    CHECK(decodeJournalReadStringInventories(encoded, plugin).size() == 1);
    auto malformed = nlohmann::json::parse(encoded); malformed[0]["directories"] = {".."};
    CHECK_THROWS(decodeJournalReadStringInventories(malformed.dump(), plugin));
    malformed = nlohmann::json::parse(encoded); malformed[0]["root"] = data.string() + "/../Data";
    CHECK_THROWS(decodeJournalReadStringInventories(malformed.dump(), plugin));
    malformed = nlohmann::json::parse(encoded); malformed[0]["files"][0]["sha256"] = "BAD";
    CHECK_THROWS(decodeJournalReadStringInventories(malformed.dump(), plugin));
    malformed = nlohmann::json::parse(encoded); malformed[0]["files"][0]["filename"] = "../Patch_en.STRINGS";
    CHECK_THROWS(decodeJournalReadStringInventories(malformed.dump(), plugin));
    malformed = nlohmann::json::array();
    for (int i = 0; i != 17; ++i) malformed.push_back(nlohmann::json::parse(encoded)[0]);
    CHECK_THROWS(decodeJournalReadStringInventories(malformed.dump(), plugin));

    auto duplicate = inventory; duplicate.push_back(inventory.front());
    CHECK_THROWS(encodeJournalReadStringInventories(duplicate, plugin));
    CHECK_THROWS(verifyJournalReadStringInventories(inventory, {}, plugin));
    write(data / "frank.STRINGS" / "Patch_en.STRINGS", "other");
    CHECK_THROWS(verifyJournalReadStringInventories(inventory, approved, plugin));
    write(data / "frank.STRINGS" / "Patch_en.STRINGS", "table");
    write(data / "frank.STRINGS" / "PATCH_EN.strings", "other");
    CHECK_THROWS(verifyJournalReadStringInventories(inventory, approved, plugin));
    CHECK_THROWS(captureJournalReadStringInventories(refs, {{data / "alias", lease}}, plugin));

    const auto absent = captureJournalReadStringInventories({{data, {"missing"}, {}}}, approved, plugin);
    REQUIRE(absent.size() == 1); CHECK_FALSE(absent[0].exists); CHECK(absent[0].files.empty());
    CHECK_NOTHROW(verifyJournalReadStringInventories(absent, approved, plugin));
    std::filesystem::create_directories(data / "missing");
    CHECK_THROWS(verifyJournalReadStringInventories(absent, approved, plugin));

    const auto swap = root / "Swap"; write(swap / "Patch_en.STRINGS", "stable");
    auto swapLease = std::make_shared<Ba2ReadRoot>(Ba2ReadRoot::authorize(swap));
    const std::vector<JournalApprovedReadRoot> swapApproved{{swap, swapLease}};
    const auto swapSnapshot = captureJournalReadStringInventories({{swap, {}, {}}}, swapApproved, plugin);
    std::filesystem::rename(swap, root / "Held"); std::filesystem::create_directories(swap);
    CHECK_THROWS(verifyJournalReadStringInventories(swapSnapshot, swapApproved, plugin));
}

// Detects: recovery must accept its own published table while rejecting unrelated new string files.
TEST_CASE("Journal read inventory accepts its interrupted sidecar only") {
    const auto root = tempRoot(); Cleanup cleanup{root};
    const auto plugin = root / "Patch.esp", strings = root / "Strings";
    bool existing = true;
    SECTION("existing table") {}
    SECTION("new table") { existing = false; }
    write(plugin, "old-plugin");
    if (existing) write(strings / "Patch_en.STRINGS", "old-table");
    write(strings / "Patch_de.STRINGS", "sibling");
    auto lease = std::make_shared<Ba2ReadRoot>(Ba2ReadRoot::authorize(root));
    JournalRoots roots; roots.plugin = plugin; roots.pluginDirectoryIdentity = JournalDestination::captureRoot(root);
    roots.approvedRoots.push_back({root, *roots.pluginDirectoryIdentity});
    roots.approvedReadRoots.push_back({root, lease}); roots.readStringDirectories.push_back({root, {"Strings"}, {}});
    const auto inventory = captureJournalReadStringInventories(roots.readStringDirectories, roots.approvedReadRoots, plugin);
    JournalInput target{"plugin", "", true, hash("old-plugin"), 10, raw("new-plugin")};
    JournalInput table{"strings", "en", existing, existing ? hash("old-table") : "", existing ? 9u : 0u, raw("new-table")};
    table.rootSelector = root; table.directories = {"Strings"};
    {
        JournalSession journal(root / "journal"); ResetJournalHook reset; setJournalCommitHook(interruptAfterSidecar);
        CHECK_THROWS_AS(journal.publish(roots, {target, table}, {}, {}, inventory), JournalRecoveryRequired);
        setJournalCommitHook(nullptr);
    }
    write(strings / "Patch_fr.STRINGS", "unexpected");
    {
        JournalSession restarted(root / "journal");
        CHECK_THROWS_AS(restarted.recover({roots}), JournalRecoveryRequired);
        std::filesystem::remove(strings / "Patch_fr.STRINGS");
        CHECK_NOTHROW(restarted.recover({roots}));
    }
    std::ifstream pluginFile(plugin, std::ios::binary), tableFile(strings / "Patch_en.STRINGS", std::ios::binary);
    CHECK(std::string(std::istreambuf_iterator<char>(pluginFile), {}) == "new-plugin");
    CHECK(std::string(std::istreambuf_iterator<char>(tableFile), {}) == "new-table");
}

// Detects: recovery must create a missing Strings directory without accepting unrelated new tables or replacing the plugin early.
TEST_CASE("Journal recovers a newly created string directory") {
    const auto root = tempRoot(); Cleanup cleanup{root};
    const auto plugin = root / "Patch.esp", strings = root / "Strings";
    SECTION("interrupted before directory creation") { interruptCount = 0; }
    SECTION("interrupted after first sidecar") { interruptCount = 1; }
    write(plugin, "old-plugin");
    auto lease = std::make_shared<Ba2ReadRoot>(Ba2ReadRoot::authorize(root));
    JournalRoots roots; roots.plugin = plugin; roots.pluginDirectoryIdentity = JournalDestination::captureRoot(root);
    roots.approvedRoots.push_back({root, *roots.pluginDirectoryIdentity});
    roots.approvedReadRoots.push_back({root, lease}); roots.readStringDirectories.push_back({root, {"Strings"}, {}});
    const auto inventory = captureJournalReadStringInventories(roots.readStringDirectories, roots.approvedReadRoots, plugin);
    const std::vector<JournalSourceSet> sources{{"strings", root, {"Strings"}, {}, false}};
    JournalInput target{"plugin", "", true, hash("old-plugin"), 10, raw("new-plugin")};
    JournalInput table{"strings", "en", false, "", 0, raw("new-table")};
    table.rootSelector = root; table.directories = {"Strings"};
    {
        JournalSession journal(root / "journal"); ResetJournalHook reset; setJournalCommitHook(interruptAtCount);
        CHECK_THROWS_AS(journal.publish(roots, {target, table}, sources, {}, inventory), JournalRecoveryRequired);
        setJournalCommitHook(nullptr);
    }
    CHECK(std::filesystem::exists(strings) == (interruptCount == 1));
    write(strings / "Patch_fr.STRINGS", "unexpected");
    {
        JournalSession restarted(root / "journal");
        CHECK_THROWS_AS(restarted.recover({roots}), JournalRecoveryRequired);
        std::ifstream before(plugin, std::ios::binary);
        CHECK(std::string(std::istreambuf_iterator<char>(before), {}) == "old-plugin");
        std::filesystem::remove(strings / "Patch_fr.STRINGS");
        CHECK_NOTHROW(restarted.recover({roots}));
    }
    std::ifstream savedPlugin(plugin, std::ios::binary), savedTable(strings / "Patch_en.STRINGS", std::ios::binary);
    CHECK(std::string(std::istreambuf_iterator<char>(savedPlugin), {}) == "new-plugin");
    CHECK(std::string(std::istreambuf_iterator<char>(savedTable), {}) == "new-table");
}

// Detects: replacing an absent Strings directory with a symlink must not redirect recovery into another directory.
TEST_CASE("Journal refuses a redirected new string directory") {
    const auto root = tempRoot(); Cleanup cleanup{root};
    const auto plugin = root / "Patch.esp", strings = root / "Strings", other = root / "other";
    write(plugin, "old-plugin"); std::filesystem::create_directories(other);
    auto lease = std::make_shared<Ba2ReadRoot>(Ba2ReadRoot::authorize(root));
    JournalRoots roots; roots.plugin = plugin; roots.pluginDirectoryIdentity = JournalDestination::captureRoot(root);
    roots.approvedRoots.push_back({root, *roots.pluginDirectoryIdentity});
    roots.approvedReadRoots.push_back({root, lease}); roots.readStringDirectories.push_back({root, {"Strings"}, {}});
    const auto inventory = captureJournalReadStringInventories(roots.readStringDirectories, roots.approvedReadRoots, plugin);
    const std::vector<JournalSourceSet> sources{{"strings", root, {"Strings"}, {}, false}};
    JournalInput target{"plugin", "", true, hash("old-plugin"), 10, raw("new-plugin")};
    JournalInput table{"strings", "en", false, "", 0, raw("new-table")};
    table.rootSelector = root; table.directories = {"Strings"};
    {
        JournalSession journal(root / "journal"); ResetJournalHook reset; interruptCount = 0; setJournalCommitHook(interruptAtCount);
        CHECK_THROWS_AS(journal.publish(roots, {target, table}, sources, {}, inventory), JournalRecoveryRequired);
    }
    std::filesystem::create_directory_symlink(other, strings);
    { JournalSession restarted(root / "journal"); CHECK_THROWS_AS(restarted.recover({roots}), JournalRecoveryRequired); }
    CHECK_FALSE(std::filesystem::exists(other / "Patch_en.STRINGS"));
    std::ifstream before(plugin, std::ios::binary);
    CHECK(std::string(std::istreambuf_iterator<char>(before), {}) == "old-plugin");
}

// Detects: a process killed while staging the first sidecar must leave a recoverable transaction for a new Strings directory.
TEST_CASE("Journal recovers a process death during first sidecar staging") {
    const auto root = tempRoot(); Cleanup cleanup{root};
    const auto plugin = root / "Patch.esp", strings = root / "Strings";
    write(plugin, "old-plugin");
    auto lease = std::make_shared<Ba2ReadRoot>(Ba2ReadRoot::authorize(root));
    JournalRoots roots; roots.plugin = plugin; roots.pluginDirectoryIdentity = JournalDestination::captureRoot(root);
    roots.approvedRoots.push_back({root, *roots.pluginDirectoryIdentity});
    roots.approvedReadRoots.push_back({root, lease}); roots.readStringDirectories.push_back({root, {"Strings"}, {}});
    const auto inventory = captureJournalReadStringInventories(roots.readStringDirectories, roots.approvedReadRoots, plugin);
    const std::vector<JournalSourceSet> sources{{"strings", root, {"Strings"}, {}, false}};
    JournalInput target{"plugin", "", true, hash("old-plugin"), 10, raw("new-plugin")};
    JournalInput table{"strings", "en", false, "", 0, raw("new-table")};
    table.rootSelector = root; table.directories = {"Strings"};
    const auto child = ::fork(); REQUIRE(child >= 0);
    if (child == 0) {
        try { JournalSession journal(root / "journal"); setJournalDestinationStageHook(terminateDuringSidecarStage);
              journal.publish(roots, {target, table}, sources, {}, inventory); }
        catch (...) { ::_exit(92); }
        ::_exit(93);
    }
    int status = 0; REQUIRE(::waitpid(child, &status, 0) == child);
    REQUIRE(WIFEXITED(status)); REQUIRE(WEXITSTATUS(status) == 91);
    REQUIRE(std::filesystem::is_directory(strings));
    CHECK_FALSE(std::filesystem::exists(strings / "Patch_en.STRINGS"));
    CHECK(std::any_of(std::filesystem::directory_iterator(strings), std::filesystem::directory_iterator(),
                      [](const auto& entry) { return entry.path().extension() == ".stage"; }));
    { std::ifstream before(plugin, std::ios::binary);
      CHECK(std::string(std::istreambuf_iterator<char>(before), {}) == "old-plugin"); }
    { JournalSession restarted(root / "journal"); CHECK_NOTHROW(restarted.recover({roots})); }
    CHECK(std::none_of(std::filesystem::directory_iterator(strings), std::filesystem::directory_iterator(),
                       [](const auto& entry) { return entry.path().extension() == ".stage"; }));
    std::ifstream savedPlugin(plugin, std::ios::binary), savedTable(strings / "Patch_en.STRINGS", std::ios::binary);
    CHECK(std::string(std::istreambuf_iterator<char>(savedPlugin), {}) == "new-plugin");
    CHECK(std::string(std::istreambuf_iterator<char>(savedTable), {}) == "new-table");
}

// Detects: a new localized plugin must create and recover quest text when its string tables or directory do not yet exist.
TEST_CASE("New localized quest creates missing string tables") {
    const auto root = tempRoot(); Cleanup cleanup{root};
    const auto plugin = root / "mod" / "Patch.esp", strings = root / "mod" / "Strings", data = root / "Data";
    const auto bytes = fo4::test::make_tes4_plugin(fo4::RecordFlags::localized, 1.0f, {});
    write(plugin, std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    std::filesystem::create_directories(data);
    SECTION("tables absent") { std::filesystem::create_directories(strings); }
    SECTION("directory absent") {}
    const nlohmann::json profile{{"plugins", nlohmann::json::array({{{"name", "Patch.esp"}, {"path", plugin.string()}, {"source", "NewQuest"}, {"resolved", true}}})}};
    fo4ide::plugin::RadiclePluginBackend backend; backend.setDataDirectory(data); backend.testSetJournalRoot(root / "journal");
    REQUIRE(backend.invoke("environment.LoadMo2", profile).has_value());
    const auto created = backend.invokeBackend("CreateRecord", nlohmann::json::array({"Patch.esp", "QUST", {{"editorId", "NewQuest"}, {"name", "Title"}}}));
    REQUIRE(created.has_value());
    const auto quest = nlohmann::json::parse(created->get<std::string>()).at("formKey").get<std::string>();
    const nlohmann::json context{{"language", "en"}, {"stringsDirectory", strings.string()}};
    REQUIRE(backend.invokeBackend("SetQuestStages", nlohmann::json::array({"Patch.esp", quest, nlohmann::json::array({{{"index", 10}, {"logEntry", "Begin"}}}), context})).has_value());
    REQUIRE(backend.invokeBackend("SetQuestObjectives", nlohmann::json::array({"Patch.esp", quest, nlohmann::json::array({{{"index", 1}, {"displayText", "Objective"}, {"targets", nlohmann::json::array()}}}), context})).has_value());
    { ResetJournalHook reset; setJournalCommitHook(interruptAfterSidecar);
      const auto saved = backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"}));
      REQUIRE_FALSE(saved.has_value()); CHECK(saved.error().type == "JournalRecoveryRequired"); }
    CHECK((std::filesystem::exists(strings / "Patch_en.STRINGS") ||
           std::filesystem::exists(strings / "Patch_en.DLSTRINGS")));
    fo4ide::plugin::RadiclePluginBackend restarted; restarted.setDataDirectory(data); restarted.testSetJournalRoot(root / "journal");
    REQUIRE(restarted.invoke("environment.LoadMo2", profile).has_value());
    const auto stages = restarted.invokeBackend("GetQuestStages", nlohmann::json::array({"Patch.esp", quest, context}));
    REQUIRE(stages.has_value()); CHECK(stages->at("stages").at(0).at("logEntries").at(0).at("logText") == "Begin");
    const auto objectives = restarted.invokeBackend("GetQuestObjectives", nlohmann::json::array({"Patch.esp", quest, context}));
    REQUIRE(objectives.has_value()); CHECK(objectives->at("objectives").at(0).at("displayText") == "Objective");
    const auto exported = restarted.invoke("strings.ExportStrings", nlohmann::json::array({"Patch.esp", "en", "STRINGS"}));
    REQUIRE(exported.has_value());
    const auto rows = nlohmann::json::parse(exported->get<std::string>()).at("rows");
    CHECK(std::any_of(rows.begin(), rows.end(), [](const auto& row) { return row.at("text") == "Title"; }));
}

// Detects: an absent writable table must not mask a matching Data table when staging localized quest text.
TEST_CASE("Localized quest rejects a missing table resolved from Data") {
    const auto root = tempRoot(); Cleanup cleanup{root};
    const auto plugin = root / "mod" / "Patch.esp", strings = root / "mod" / "Strings", data = root / "Data";
    const auto bytes = fo4::test::make_tes4_plugin(fo4::RecordFlags::localized, 1.0f, {});
    write(plugin, std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    std::filesystem::create_directories(strings);
    const auto dataTable = fo4::localization::write_string_table({fo4::localization::StringTableKind::dlstrings, {{5, {std::byte{'O'}}}}});
    write(data / "Strings" / "Patch_en.DLSTRINGS", std::string_view(reinterpret_cast<const char*>(dataTable.data()), dataTable.size()));
    const nlohmann::json profile{{"plugins", nlohmann::json::array({{{"name", "Patch.esp"}, {"path", plugin.string()}, {"source", "DataFallback"}, {"resolved", true}}})}};
    fo4ide::plugin::RadiclePluginBackend backend; backend.setDataDirectory(data); backend.testSetJournalRoot(root / "journal");
    REQUIRE(backend.invoke("environment.LoadMo2", profile).has_value());
    const auto heldMaximum = backend.testHeldMaximumStringId("Patch.esp", "en");
    REQUIRE(heldMaximum.has_value()); CHECK(*heldMaximum == 5);
    const auto created = backend.invokeBackend("CreateRecord", nlohmann::json::array({"Patch.esp", "QUST", {{"editorId", "FallbackQuest"}}}));
    REQUIRE(created.has_value());
    const auto quest = nlohmann::json::parse(created->get<std::string>()).at("formKey").get<std::string>();
    const nlohmann::json context{{"language", "en"}, {"stringsDirectory", strings.string()}};
    const auto edited = backend.invokeBackend("SetQuestStages", nlohmann::json::array({"Patch.esp", quest, nlohmann::json::array({{{"index", 10}, {"logEntry", "Begin"}}}), context}));
    REQUIRE_FALSE(edited.has_value()); CHECK(edited.error().type == "LocalizedAllocationSnapshotUnsupported");
    CHECK_FALSE(std::filesystem::exists(strings / "Patch_en.DLSTRINGS"));
    const auto stages = backend.invokeBackend("GetQuestStages", nlohmann::json::array({"Patch.esp", quest}));
    REQUIRE(stages.has_value()); CHECK(stages->at("stages").empty());
}
#endif
