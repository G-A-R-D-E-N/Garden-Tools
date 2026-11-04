#include "../../src/plugin/RadiclePluginBa2Source.hpp"
#include "../../src/plugin/RadiclePluginJournalSession.hpp"
#include "../../src/plugin/RadiclePluginJournalPeers.hpp"
#include "../../src/plugin/RadiclePluginJournalReadStrings.hpp"
#include "../../src/plugin/RadiclePluginHeldArchives.hpp"
#include "../../src/plugin/RadiclePluginHeldStrings.hpp"
#include "../../src/plugin/RadiclePluginHeldLocalization.hpp"
#include "plugin_bytes.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <fo4/archive/ba2.hpp>
#include <fo4/flags.hpp>
#include <fo4/io/sha256.hpp>
#include <fo4/localization/strings.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/plugin/subrecord_authoring.hpp>
#include <fo4/plugin/writer.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>
#include <nlohmann/json.hpp>

#ifdef __linux__
namespace {
using namespace fo4ide::plugin::detail;
struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); } };
std::filesystem::path tempRoot() {
    return std::filesystem::temp_directory_path() / ("fo4ide_archive_journal_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
}
void write(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
    std::filesystem::create_directories(path.parent_path()); std::ofstream stream(path, std::ios::binary);
    REQUIRE(stream.good()); stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(stream.good());
}
std::vector<std::byte> read(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate); REQUIRE(stream.good());
    const auto end = stream.tellg(); REQUIRE(end >= 0); std::vector<std::byte> data(static_cast<std::size_t>(end));
    stream.seekg(0); if (!data.empty()) stream.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    REQUIRE(stream.good()); return data;
}
void interrupt(std::size_t count) { if (count == 1) throw std::runtime_error("interrupted"); }
struct ResetHook { ~ResetHook() { setJournalCommitHook(nullptr); } };
std::atomic<std::size_t> archiveFullReads{0};
std::atomic<std::uint64_t> watchedArchiveSize{0};
void countArchiveReads(std::uint64_t offset, std::size_t count, std::uint64_t size) {
    if (offset == 0 && size == watchedArchiveSize.load() && count == size) ++archiveFullReads;
}
struct ResetBa2ReadHook { ~ResetBa2ReadHook() { setBa2ReadHook(nullptr); } };
void forbidLegacyStringResolver() { throw std::runtime_error("legacy string resolver was accessed"); }
struct ResetLegacyResolverHook { ~ResetLegacyResolverHook() { fo4ide::plugin::setLegacyStringResolverHook(nullptr); } };
fo4ide::plugin::RadiclePluginBackend* nestedBackend = nullptr;
nlohmann::json nestedProfile;
bool nestedLoaded = false;
std::filesystem::path nestedDataDirectory;
void loadNestedProfile() {
    fo4ide::plugin::setLoadMo2AfterPendingCheckHook(nullptr);
    nestedLoaded = nestedBackend->invoke("environment.LoadMo2", nestedProfile).has_value();
}
void changeDataDuringLoad() {
    fo4ide::plugin::setLoadMo2AfterPendingCheckHook(nullptr);
    nestedBackend->setDataDirectory(nestedDataDirectory);
}
struct ResetLoadHook { ~ResetLoadHook() { fo4ide::plugin::setLoadMo2AfterPendingCheckHook(nullptr); nestedBackend = nullptr; } };
}

// Detects: recovery must reconstruct the effective archive order, including mod-folder shadowing and unrelated providers.
TEST_CASE("Held archive provider order follows the loaded profile") {
    const auto root = tempRoot(); Cleanup cleanup{root};
    const auto data = root / "Data", mod = root / "Mod";
    const std::vector<std::byte> content{std::byte{1}};
    write(data / "Alpha.ba2", content); write(data / "Alpha - Main.ba2", content);
    write(data / "Patch - Main.ba2", content); write(data / "Unrelated.ba2", content);
    write(mod / "Patch - Main.ba2", content); write(mod / "Patch - Textures.ba2", content);
    auto dataRoot = std::make_shared<Ba2ReadRoot>(Ba2ReadRoot::authorize(data));
    auto modRoot = std::make_shared<Ba2ReadRoot>(Ba2ReadRoot::authorize(mod));
    const auto order = journalArchiveOrder(dataRoot, modRoot, {"Alpha.esm", "Patch.esp"}, "Patch.esp");
    REQUIRE(order.size() == 4);
    CHECK(order[0].rootSelector == data); CHECK(order[0].filename == "Alpha.ba2");
    CHECK(order[1].rootSelector == data); CHECK(order[1].filename == "Alpha - Main.ba2");
    CHECK(order[2].rootSelector == mod); CHECK(order[2].filename == "Patch - Main.ba2");
    CHECK(order[3].rootSelector == mod); CHECK(order[3].filename == "Patch - Textures.ba2");
    const auto same = journalArchiveOrder(dataRoot, dataRoot, {"Patch.esp", "Alpha.esm", "Patch.esp"}, "Patch.esp");
    REQUIRE(same.size() == 3);
    CHECK(same[0].filename == "Patch - Main.ba2"); CHECK(same[1].filename == "Alpha.ba2");
    CHECK(same[2].filename == "Alpha - Main.ba2");
    const auto onlyMod = journalArchiveOrder({}, modRoot, {"Alpha.esm", "Patch.esp"}, "Patch.esp");
    REQUIRE(onlyMod.size() == 2); CHECK(onlyMod[0].filename == "Patch - Main.ba2");
    write(data / "ALPHA.ba2", content);
    CHECK_THROWS(journalArchiveOrder(dataRoot, modRoot, {"Alpha.esm", "Patch.esp"}, "Patch.esp"));
}

// Detects: recovery must reject invalid peer plugins and changes after the exact parsed peer snapshot was captured.
TEST_CASE("Held profile peers remain identical after parsing") {
    const auto root = tempRoot(); Cleanup cleanup{root};
    const auto before = fo4::test::make_tes4_plugin(0, 1.0f, {});
    write(root / "Peer.esp", before);
    auto readRoot = std::make_shared<Ba2ReadRoot>(Ba2ReadRoot::authorize(root));
    const auto peer = snapshotJournalPeer(readRoot, "Peer.esp");
    CHECK_NOTHROW(verifyJournalPeer(peer));
    CHECK_THROWS(snapshotJournalPeer(readRoot, "Peer.esp", before.size() - 1));
    write(root / "Peer2.esp", before);
    const std::vector<JournalPeerInput> inputs{{readRoot, "Peer.esp"}, {readRoot, "Peer2.esp"}};
    CHECK_THROWS(snapshotJournalPeers(inputs, before.size() - 1, 2));
    CHECK_THROWS(snapshotJournalPeers(inputs, before.size(), 1));
    CHECK(snapshotJournalPeers(inputs, before.size(), 2).size() == 2);
    std::vector<JournalPeerInput> largerProfile;
    for (int i = 0; i < 256; ++i) {
        const auto name = "Peer" + std::to_string(i) + ".esp";
        write(root / name, before); largerProfile.push_back({readRoot, name});
    }
    CHECK(snapshotJournalPeers(largerProfile, before.size(), 256).size() == 256);
    auto changed = before; changed.back() ^= std::byte{1}; write(root / "Peer.esp", changed);
    CHECK_THROWS(verifyJournalPeer(peer));
    write(root / "Broken.esp", {std::byte{1}});
    CHECK_THROWS_AS(snapshotJournalPeer(readRoot, "Broken.esp"), JournalRecoveryProfileInvalid);
}

// Detects: archive allocation must extract from the held winning provider and reject a same-index replacement.
TEST_CASE("Held archive provider order and payload generation stay coupled") {
    const auto root = tempRoot(); Cleanup cleanup{root};
    const auto data = root / "Data";
    const auto archiveBytes = [](std::byte value) {
        fo4::archive::Ba2GeneralEntry entry; entry.path = "Strings\\Patch_de.STRINGS";
        entry.chunks.push_back(fo4::archive::make_stored_ba2_chunk(std::vector<std::byte>{value, value}));
        fo4::archive::Ba2GeneralArchive archive; archive.entries.push_back(std::move(entry));
        return fo4::archive::write_ba2_general(archive);
    };
    fo4::archive::Ba2GeneralArchive first;
    fo4::archive::Ba2GeneralEntry shared; shared.path = "Strings\\Patch_de.STRINGS";
    shared.chunks.push_back(fo4::archive::make_stored_ba2_chunk(std::vector<std::byte>{std::byte{'A'}, std::byte{'A'}}));
    fo4::archive::Ba2GeneralEntry unique; unique.path = "Strings\\Patch_fr.STRINGS";
    unique.chunks.push_back(fo4::archive::make_stored_ba2_chunk(std::vector<std::byte>{std::byte{'F'}, std::byte{'F'}}));
    first.entries.push_back(std::move(shared)); first.entries.push_back(std::move(unique));
    write(data / "First.ba2", fo4::archive::write_ba2_general(first));
    write(data / "Second.ba2", archiveBytes(std::byte{'B'}));
    auto rootLease = std::make_shared<Ba2ReadRoot>(Ba2ReadRoot::authorize(data));
    JournalReadConfiguration config;
    config.approvedReadRoots.push_back({data, rootLease}); config.archiveDirectories.push_back({data, {}, {}});
    config.archiveProviderOrder = {{data, {}, "First.ba2"}, {data, {}, "Second.ba2"}};
    CHECK_THROWS(HeldArchiveSet::capture(config, {2, 1024 * 1024}));
    CHECK_THROWS(HeldArchiveSet::capture(config, {10, 1}));
    auto held = HeldArchiveSet::capture(config);
    const auto languages = held.languages("Patch.esp");
    const std::set<std::string> expectedLanguages{"de", "fr"}; CHECK(languages == expectedLanguages);
    CHECK_THROWS(held.languages(".esp"));
    const auto winner = held.extract("Strings/Patch_de.STRINGS", 16);
    REQUIRE(winner.has_value()); const std::vector<std::byte> expectedB{std::byte{'B'}, std::byte{'B'}}; CHECK(winner->bytes == expectedB);
    CHECK(winner->provider.filename == "Second.ba2");
    const auto batch = held.extractMany({"Strings/Patch_de.STRINGS", "Strings/missing.STRINGS"}, 16);
    REQUIRE(batch.size() == 2); REQUIRE(batch[0].has_value()); CHECK_FALSE(batch[1].has_value());
    CHECK(batch[0]->bytes == winner->bytes);
    CHECK_THROWS(held.extractMany({"Strings/Patch_de.STRINGS", "Strings/Patch_de.STRINGS"}, 5));
    CHECK_THROWS(held.extractMany({"Strings/Patch_de.STRINGS", "Strings/Patch_fr.STRINGS"}, 5));
    const auto all = held.extractAll({"Strings/Patch_de.STRINGS"}, 16);
    const std::vector<std::byte> expectedA{std::byte{'A'}, std::byte{'A'}};
    REQUIRE(all.size() == 2); CHECK(all[0].payload.bytes == expectedA);
    CHECK(all[1].payload.bytes == expectedB);
    CHECK_THROWS(held.extractAll({"Strings/Patch_de.STRINGS"}, 5));
    write(data / "Third.ba2", archiveBytes(std::byte{'D'}));
    CHECK_THROWS(held.verify());
    std::filesystem::remove(data / "Third.ba2");
    write(data / "Second.ba2", archiveBytes(std::byte{'C'}));
    CHECK_THROWS(held.languages("Patch.esp"));
    CHECK_THROWS(held.extract("Strings/Patch_de.STRINGS", 16));
}

// Detects: loose Data tables must retain exact parsed bytes and reject new siblings or post-capture replacement.
TEST_CASE("Held loose string inventory protects allocation inputs") {
    const auto root = tempRoot(); Cleanup cleanup{root};
    const auto data = root / "Data", strings = data / "Strings";
    using Kind = fo4::localization::StringTableKind;
    const auto table = fo4::localization::write_string_table({Kind::strings, {{300, {std::byte{'D'}}}}});
    write(strings / "Patch_de.STRINGS", table);
    auto lease = std::make_shared<Ba2ReadRoot>(Ba2ReadRoot::authorize(data));
    auto held = HeldStringDirectory::capture(lease, "Strings", "Patch.esp");
    REQUIRE(held.exists()); REQUIRE(held.tables().size() == 1);
    CHECK(held.tables()[0].language == "de"); CHECK(held.tables()[0].bytes == table);
    REQUIRE(held.tables()[0].table.entries.size() == 1); CHECK(held.tables()[0].table.entries[0].id == 300);
    // Detects: several individually acceptable tables must not exceed the combined held-source working budget.
    const auto oneTableBudget = 8 * 1024 * 1024 + table.size() + sizeof(fo4::localization::LocalizedStringEntry) + 72 +
                                3 * sizeof(std::size_t) * table.size() + 1;
    CHECK_NOTHROW(HeldStringDirectory::capture(lease, "Strings", "Patch.esp", oneTableBudget));
    auto budgeted = HeldStringDirectory::capture(lease, "Strings", "Patch.esp", oneTableBudget);
    const auto globalBudget = budgeted.accountedRetainedBytes() + oneTableBudget - 1;
    CHECK_THROWS(HeldStringDirectory::capture(lease, "Strings", "Patch.esp", globalBudget - budgeted.accountedRetainedBytes()));
    write(strings / "Patch_fr.STRINGS", table); CHECK_THROWS(held.verify());
    CHECK_THROWS(HeldStringDirectory::capture(lease, "Strings", "Patch.esp", oneTableBudget));
    std::filesystem::remove(strings / "Patch_fr.STRINGS");
    auto changed = table; changed.back() ^= std::byte{1}; write(strings / "Patch_de.STRINGS", changed);
    CHECK_THROWS(held.verify());
    std::filesystem::remove_all(strings);
    auto absent = HeldStringDirectory::capture(lease, "Strings", "Patch.esp");
    CHECK_FALSE(absent.exists()); std::filesystem::create_directory(strings); CHECK_THROWS(absent.verify());
}

// Detects: duplicate archive entries for one localized table must not hide IDs from allocation.
TEST_CASE("Held archive allocation refuses duplicate table names") {
    const auto root = tempRoot(); Cleanup cleanup{root}; const auto data = root / "Data";
    fo4::archive::Ba2GeneralArchive archive;
    for (const auto value : {std::byte{'A'}, std::byte{'B'}}) {
        fo4::archive::Ba2GeneralEntry entry; entry.path = "Strings\\Patch_de.STRINGS";
        entry.chunks.push_back(fo4::archive::make_stored_ba2_chunk(std::vector<std::byte>{value})); archive.entries.push_back(std::move(entry));
    }
    write(data / "Duplicate.ba2", fo4::archive::write_ba2_general(archive));
    auto lease = std::make_shared<Ba2ReadRoot>(Ba2ReadRoot::authorize(data));
    JournalReadConfiguration config; config.approvedReadRoots = {{data, lease}}; config.archiveDirectories = {{data, {}, {}}};
    config.archiveProviderOrder = {{data, {}, "Duplicate.ba2"}};
    const auto held = HeldArchiveSet::capture(config);
    CHECK_THROWS(held.extractAll({"Strings/Patch_de.STRINGS"}, 16));
}

// Detects: allocation must use loose-source precedence while retaining archive-only sibling IDs in one bounded generation.
TEST_CASE("Held localization generation combines loose and archive tables") {
    const auto root = tempRoot(); Cleanup cleanup{root};
    const auto mod = root / "Mod", data = root / "Data";
    using Kind = fo4::localization::StringTableKind;
    auto plugin = fo4::read_plugin(fo4::test::make_tes4_plugin(fo4::RecordFlags::localized, 1.0f, {}), mod / "Patch.esp");
    const std::array ids{fo4::LocalizedRowId{fo4::Signature::from_string("DESC"), 800}};
    REQUIRE(fo4::create_record_with_localized_rows(plugin, fo4::Signature::from_string("BOOK"), "Existing", ids).status == fo4::RecordCreateStatus::created);
    write(mod / "Patch.esp", fo4::write_plugin(plugin, mod / "Patch.esp"));
    const auto table = [](std::uint32_t id) { return fo4::localization::write_string_table({Kind::strings, {{id, {std::byte{'X'}}}}}); };
    write(mod / "Strings" / "Patch_en.STRINGS", table(17));
    write(data / "Strings" / "Patch_en.STRINGS", table(400));
    const auto archiveFor = [&](std::uint32_t id) {
        fo4::archive::Ba2GeneralEntry entry; entry.path = "Strings\\Patch_de.STRINGS";
        entry.chunks.push_back(fo4::archive::make_stored_ba2_chunk(table(id)));
        fo4::archive::Ba2GeneralArchive archive; archive.entries.push_back(std::move(entry));
        return fo4::archive::write_ba2_general(archive);
    };
    write(data / "Patch - Main.ba2", archiveFor(600));
    write(data / "Patch - Secondary.ba2", archiveFor(300));
    auto modLease = std::make_shared<Ba2ReadRoot>(Ba2ReadRoot::authorize(mod));
    auto dataLease = std::make_shared<Ba2ReadRoot>(Ba2ReadRoot::authorize(data));
    JournalReadConfiguration config; config.approvedReadRoots = {{data, dataLease}, {mod, modLease}};
    config.archiveDirectories = {{data, {}}, {mod, {}}}; config.archiveProviderOrder = {{data, {}, "Patch - Main.ba2"}, {data, {}, "Patch - Secondary.ba2"}};
    CHECK_THROWS(HeldLocalizationGeneration::capture(config, {{modLease, "Strings"}, {dataLease, "Strings"}},
                                                      "Patch.esp", {std::string(256, 'a')}, 64 * 1024 * 1024));
    CHECK_THROWS(HeldLocalizationGeneration::capture(config, {}, std::string("Patch\0.esp", 10), {"en"}, 64 * 1024 * 1024));
    CHECK_THROWS(HeldLocalizationGeneration::capture(config, {}, std::string(256, 'p'), {"en"}, 64 * 1024 * 1024));
    CHECK_THROWS(HeldLocalizationGeneration::capture(config, {}, "Patch.esp", std::vector<std::string>(1366, "en"), 64 * 1024 * 1024));
    auto excessiveRoots = config; excessiveRoots.approvedReadRoots.resize(17, {data, dataLease});
    CHECK_THROWS(HeldLocalizationGeneration::capture(excessiveRoots, {}, "Patch.esp", {"en"}, 64 * 1024 * 1024));
    auto oversizedProvider = config; oversizedProvider.archiveProviderOrder[0].filename = std::string(256, 'x');
    CHECK_THROWS(HeldLocalizationGeneration::capture(oversizedProvider, {}, "Patch.esp", {"en"}, 64 * 1024 * 1024));
    CHECK_THROWS(HeldLocalizationGeneration::capture({}, {}, "Patch.esp", {"en"}, 32 * 1024 * 1024));
    CHECK_THROWS(HeldLocalizationGeneration::capture(config, {{modLease, "Strings"}, {modLease, "Strings"}},
                                                      "Patch.esp", {"en"}, 64 * 1024 * 1024));
    const auto other = root / "Other"; std::filesystem::create_directory(other);
    auto otherLease = std::make_shared<Ba2ReadRoot>(Ba2ReadRoot::authorize(other));
    CHECK_THROWS(HeldLocalizationGeneration::capture(config, {{otherLease, "Strings"}},
                                                      "Patch.esp", {"en"}, 64 * 1024 * 1024));
    // Detects: adding languages must not multiply whole-archive reads during a held generation capture.
    watchedArchiveSize = archiveFor(600).size(); archiveFullReads = 0; ResetBa2ReadHook resetRead;
    setBa2ReadHook(&countArchiveReads);
    auto generation = HeldLocalizationGeneration::capture(config, {{modLease, "Strings"}, {dataLease, "Strings"}},
                                                            "Patch.esp", {"en"}, 64 * 1024 * 1024);
    const auto firstPasses = archiveFullReads.load();
    archiveFullReads = 0;
    const auto extraLanguages = HeldLocalizationGeneration::capture(config, {{modLease, "Strings"}, {dataLease, "Strings"}},
                                                                      "Patch.esp", {"en", "fr", "es", "it"}, 64 * 1024 * 1024);
    CHECK(extraLanguages.find("de", fo4::env::StringSource::normal).has_value());
    CHECK(firstPasses == 16); CHECK(archiveFullReads.load() == firstPasses);
    setBa2ReadHook(nullptr);
    const auto en = generation.find("en", fo4::env::StringSource::normal);
    const auto de = generation.find("de", fo4::env::StringSource::normal);
    REQUIRE(en); REQUIRE(de);
    REQUIRE(en->table->entries.size() == 1); CHECK(en->table->entries[0].id == 17);
    REQUIRE(de->table->entries.size() == 1); CHECK(de->table->entries[0].id == 300);
    CHECK(generation.maximumStringId() == 600);
    CHECK_THROWS(generation.reserveWorkingBytes(512ull * 1024 * 1024));
    CHECK_THROWS(generation.copySelectedSnapshots("en", 0));
    auto selected = generation.copySelectedSnapshots("en", 64 * 1024 * 1024);
    REQUIRE(selected.size() == 1); REQUIRE(selected[0].resolved.table.entries.size() == 1);
    CHECK(selected[0].resolved.table.entries[0].id == 17);
    CHECK(selected[0].resolved.path == mod / "Strings" / "Patch_en.STRINGS"); CHECK(selected[0].resolved.archive.empty());
    const auto dataInventory = generation.readOnlyInventory(data, "Strings");
    REQUIRE(dataInventory.files.size() == 1); CHECK(dataInventory.files[0].filename == "Patch_en.STRINGS");
    CHECK(dataInventory.files[0].sha256 == fo4::io::sha256_hex(table(400)));
    CHECK_THROWS(generation.readOnlyInventory(root / "unapproved", "Strings"));
    fo4ide::plugin::RadiclePluginBackend backend; backend.setDataDirectory(data); backend.testSetJournalRoot(root / "journal");
    const auto profile = nlohmann::json{{"plugins", nlohmann::json::array({{{"name", "Patch.esp"}, {"path", (mod / "Patch.esp").string()}, {"source", "HeldProfile"}, {"resolved", true}}})}};
    REQUIRE(backend.invoke("environment.LoadMo2", profile).has_value());
    // Detects: referenced and pending IDs must also constrain allocation beyond every physical source.
    const auto heldMaximum = backend.testHeldMaximumStringId("Patch.esp", "en");
    REQUIRE(heldMaximum.has_value()); CHECK(*heldMaximum == 800);
    REQUIRE(backend.invoke("strings.ImportStrings", nlohmann::json::array({"Patch.esp", "en", "STRINGS", R"({"rows":[{"id":900,"text":"Pending"}]})", "merge"})).has_value());
    const auto withPending = backend.testHeldMaximumStringId("Patch.esp", "en");
    REQUIRE(withPending.has_value()); CHECK(*withPending == 900);
    REQUIRE(backend.invokeBackend("CreateRecord", nlohmann::json::array({"Patch.esp", "QUST", {{"editorId", "Pending"}, {"name", "Next"}}})).has_value());
    const auto priorAllocation = backend.testHeldMaximumStringId("Patch.esp", "en");
    REQUIRE(priorAllocation.has_value()); CHECK(*priorAllocation == 901);
    write(data / "Strings" / "Patch_fr.STRINGS", table(400)); CHECK_THROWS(generation.verify());
    CHECK_THROWS(generation.readOnlyInventory(data, "Strings"));
}

// Detects: a rejected profile reload must retain its previous held read authority until a replacement load commits.
TEST_CASE("Profile read authorities change only with a committed load") {
    const auto root = tempRoot(); Cleanup cleanup{root};
    const auto first = root / "first" / "First.esp", second = root / "second" / "Second.esp";
    const auto pluginBytes = fo4::test::make_tes4_plugin(0, 1.0f, {});
    write(first, pluginBytes); write(second, pluginBytes);
    const auto profile = [](const auto& path, std::string name) {
        return nlohmann::json{{"plugins", nlohmann::json::array({{{"name", name}, {"path", path.string()}, {"source", "Profile"}, {"resolved", true}}})}};
    };
    fo4ide::plugin::RadiclePluginBackend backend; backend.testSetJournalRoot(root / "journal");
    REQUIRE(backend.invoke("environment.LoadMo2", profile(first, "First.esp")).has_value());
    CHECK(backend.testHasReadAuthority(first.parent_path())); CHECK_FALSE(backend.testHasReadAuthority(second.parent_path()));
    const auto invalid = backend.invoke("environment.LoadMo2", profile(root / "missing" / "Missing.esp", "Missing.esp"));
    REQUIRE_FALSE(invalid.has_value());
    CHECK(backend.testHasReadAuthority(first.parent_path())); CHECK_FALSE(backend.testHasReadAuthority(second.parent_path()));
    const auto malformed = root / "broken" / "Broken.esp"; write(malformed, {std::byte{1}});
    const auto failedParse = backend.invoke("environment.LoadMo2", profile(malformed, "Broken.esp"));
    REQUIRE_FALSE(failedParse.has_value()); CHECK(failedParse.error().type == "JournalRecoveryProfileInvalid");
    CHECK(backend.testHasReadAuthority(first.parent_path())); CHECK_FALSE(backend.testHasReadAuthority(malformed.parent_path()));
    REQUIRE(backend.invokeBackend("CreateRecord", nlohmann::json::array({"First.esp", "QUST", {{"editorId", "PendingQuest"}}})).has_value());
    const auto refused = backend.invoke("environment.LoadMo2", profile(second, "Second.esp"));
    REQUIRE_FALSE(refused.has_value()); CHECK(refused.error().type == "PluginHasPendingChanges");
    CHECK(backend.testHasReadAuthority(first.parent_path())); CHECK_FALSE(backend.testHasReadAuthority(second.parent_path()));
    auto accepted = profile(second, "Second.esp"); accepted["discardPending"] = true;
    REQUIRE(backend.invoke("environment.LoadMo2", accepted).has_value());
    CHECK_FALSE(backend.testHasReadAuthority(first.parent_path())); CHECK(backend.testHasReadAuthority(second.parent_path()));
}

// Detects: a profile load superseded after its consent check must not replace the newer profile or its read roots.
TEST_CASE("Concurrent profile replacement invalidates prior load consent") {
    const auto root = tempRoot(); Cleanup cleanup{root};
    const auto first = root / "first" / "First.esp", second = root / "second" / "Second.esp", third = root / "third" / "Third.esp";
    const auto pluginBytes = fo4::test::make_tes4_plugin(0, 1.0f, {});
    write(first, pluginBytes); write(second, pluginBytes); write(third, pluginBytes);
    const auto profile = [](const auto& path, std::string name) {
        return nlohmann::json{{"plugins", nlohmann::json::array({{{"name", name}, {"path", path.string()}, {"source", "Profile"}, {"resolved", true}}})}};
    };
    fo4ide::plugin::RadiclePluginBackend backend; backend.testSetJournalRoot(root / "journal");
    REQUIRE(backend.invoke("environment.LoadMo2", profile(first, "First.esp")).has_value());
    nestedBackend = &backend; nestedProfile = profile(third, "Third.esp"); nestedLoaded = false; ResetLoadHook reset;
    fo4ide::plugin::setLoadMo2AfterPendingCheckHook(&loadNestedProfile);
    const auto superseded = backend.invoke("environment.LoadMo2", profile(second, "Second.esp"));
    REQUIRE(nestedLoaded); REQUIRE_FALSE(superseded.has_value());
    CHECK(superseded.error().type == "PluginHasPendingChanges");
    CHECK(backend.testHasReadAuthority(third.parent_path())); CHECK_FALSE(backend.testHasReadAuthority(second.parent_path()));
    const auto plugins = backend.invoke("appInterop.GetPlugins", nlohmann::json::object());
    REQUIRE(plugins.has_value()); CHECK(plugins->dump().find("Third.esp") != std::string::npos);
}

// Detects: changing the Data authority during a load must prevent installing that load's stale profile roots.
TEST_CASE("Data authority changes invalidate profile load consent") {
    const auto root = tempRoot(); Cleanup cleanup{root};
    const auto first = root / "first" / "First.esp", second = root / "second" / "Second.esp";
    const auto pluginBytes = fo4::test::make_tes4_plugin(0, 1.0f, {});
    write(first, pluginBytes); write(second, pluginBytes);
    std::filesystem::create_directories(root / "Data1"); std::filesystem::create_directories(root / "Data2");
    const auto profile = [](const auto& path, std::string name) {
        return nlohmann::json{{"plugins", nlohmann::json::array({{{"name", name}, {"path", path.string()}, {"source", "Profile"}, {"resolved", true}}})}};
    };
    fo4ide::plugin::RadiclePluginBackend backend; backend.setDataDirectory(root / "Data1"); backend.testSetJournalRoot(root / "journal");
    REQUIRE(backend.invoke("environment.LoadMo2", profile(first, "First.esp")).has_value());
    nestedBackend = &backend; nestedDataDirectory = root / "Data2"; ResetLoadHook reset;
    fo4ide::plugin::setLoadMo2AfterPendingCheckHook(&changeDataDuringLoad);
    const auto superseded = backend.invoke("environment.LoadMo2", profile(second, "Second.esp"));
    REQUIRE_FALSE(superseded.has_value()); CHECK(superseded.error().type == "PluginHasPendingChanges");
    CHECK(backend.testHasReadAuthority(first.parent_path())); CHECK_FALSE(backend.testHasReadAuthority(second.parent_path()));
}

// Detects: a changed read-only archive provider must stop replay before the remaining plugin write, even when size and mtime are restored.
TEST_CASE("Journal replay verifies the full held archive provider hash") {
    const auto root = tempRoot(); Cleanup cleanup{root};
    const auto plugin = root / "mod" / "Patch.esp", strings = root / "mod" / "Strings", data = root / "Data";
    const auto oldPlugin = fo4::test::make_tes4_plugin(0, 1.0f, {});
    const auto newPlugin = fo4::test::make_tes4_plugin(fo4::RecordFlags::localized, 1.0f, {});
    const std::vector<std::byte> oldTable{std::byte{1}}, newTable{std::byte{2}};
    fo4::archive::Ba2GeneralArchive archive;
    fo4::archive::Ba2GeneralEntry entry; entry.path = "Strings\\Patch_de.STRINGS";
    entry.chunks.push_back(fo4::archive::make_stored_ba2_chunk(std::vector<std::byte>{std::byte{3}, std::byte{4}}));
    archive.entries.push_back(std::move(entry));
    const auto archiveBytes = fo4::archive::write_ba2_general(archive);
    const auto archivePath = data / "Provider.ba2";
    write(plugin, oldPlugin); write(strings / "Patch_en.STRINGS", oldTable); write(archivePath, archiveBytes);
    const auto archiveIndex = fo4::archive::Ba2Reader::open(archivePath);
    REQUIRE(archiveIndex.entries().size() == 1);
    REQUIRE(archiveIndex.entries()[0].chunks.size() == 1);
    const auto payloadOffset = static_cast<std::size_t>(archiveIndex.entries()[0].chunks[0].offset);
    JournalRoots roots; roots.plugin = plugin;
    roots.pluginDirectoryIdentity = JournalDestination::captureRoot(plugin.parent_path());
    roots.approvedRoots.push_back({plugin.parent_path(), *roots.pluginDirectoryIdentity});
    auto readRoot = std::make_shared<Ba2ReadRoot>(Ba2ReadRoot::authorize(data));
    roots.approvedReadRoots.push_back({data, readRoot}); roots.archiveDirectories.push_back({data, {}, {}});
    roots.archiveProviderOrder.push_back({data, {}, "Provider.ba2"});
    JournalArchiveInventory inventory{data, {}, {{"Provider.ba2", archiveBytes.size(), fo4::io::sha256_hex(archiveBytes)}}};
    JournalInput target{"plugin", "", true, fo4::io::sha256_hex(oldPlugin), oldPlugin.size(), newPlugin};
    JournalInput table{"strings", "en", true, fo4::io::sha256_hex(oldTable), oldTable.size(), newTable};
    table.rootSelector = plugin.parent_path(); table.directories = {"Strings"};
    {
        JournalSession journal(root / "journal"); ResetHook reset; setJournalCommitHook(&interrupt);
        CHECK_THROWS_AS(journal.publish(roots, {target, table}, {}, {inventory}), JournalRecoveryRequired);
        setJournalCommitHook(nullptr);
    }
    // Detects: recovery preparation must see the verified final plugin payload and stop before remaining writes on failure.
    {
        bool prepared = false;
        JournalSession restarted(root / "journal");
        CHECK_THROWS_AS(restarted.recover({roots}, [&](const JournalManifest&, const std::vector<std::byte>& finalPlugin, JournalReadConfiguration&) {
            prepared = true; CHECK(finalPlugin == newPlugin); throw JournalRecoveryProfileInvalid("Broken.esp");
        }), JournalRecoveryProfileInvalid);
        CHECK(prepared); CHECK(read(plugin) == oldPlugin); CHECK(read(strings / "Patch_en.STRINGS") == newTable);
    }
    // Detects: a damaged final plugin payload must be rejected before recovery preparation can inspect it.
    {
        std::filesystem::path pluginPayload;
        for (const auto& entry : std::filesystem::directory_iterator(root / "journal"))
            if (entry.path().filename().string().ends_with("_0.payload")) pluginPayload = entry.path();
        REQUIRE_FALSE(pluginPayload.empty());
        const auto original = read(pluginPayload); auto damaged = original; damaged.at(0) ^= std::byte{1};
        write(pluginPayload, damaged);
        bool prepared = false;
        JournalSession restarted(root / "journal");
        CHECK_THROWS_AS(restarted.recover({roots}, [&](const JournalManifest&, const std::vector<std::byte>&, JournalReadConfiguration&) {
            prepared = true;
        }), JournalRecoveryRequired);
        CHECK_FALSE(prepared); CHECK(read(plugin) == oldPlugin); CHECK(read(strings / "Patch_en.STRINGS") == newTable);
        write(pluginPayload, original);
    }
    // Detects: a peer changed after preparation must block replay before any remaining destination is written.
    {
        const auto peerPath = root / "peer" / "Other.esp";
        const auto peerBytes = fo4::test::make_tes4_plugin(0, 1.0f, {});
        write(peerPath, peerBytes);
        auto peerRoot = std::make_shared<Ba2ReadRoot>(Ba2ReadRoot::authorize(peerPath.parent_path()));
        JournalSession restarted(root / "journal");
        CHECK_THROWS_AS(restarted.recover({roots}, [&](const JournalManifest&, const std::vector<std::byte>&, JournalReadConfiguration& config) {
            config.peers.push_back(snapshotJournalPeer(peerRoot, "Other.esp"));
            auto changedPeer = peerBytes; changedPeer.back() ^= std::byte{1}; write(peerPath, changedPeer);
        }), JournalRecoveryRequired);
        CHECK(read(plugin) == oldPlugin); CHECK(read(strings / "Patch_en.STRINGS") == newTable);
    }
    const auto time = std::filesystem::last_write_time(archivePath);
    auto changed = archiveBytes; changed.at(payloadOffset) ^= std::byte{1}; write(archivePath, changed);
    std::filesystem::last_write_time(archivePath, time);
    {
        JournalSession restarted(root / "journal");
        CHECK_THROWS_AS(restarted.recover({roots}), JournalRecoveryRequired);
        CHECK(read(plugin) == oldPlugin); CHECK(read(strings / "Patch_en.STRINGS") == newTable);
        write(archivePath, archiveBytes);
        std::filesystem::remove(archivePath);
        CHECK_THROWS_AS(restarted.recover({roots}), JournalRecoveryRequired);
        CHECK(read(plugin) == oldPlugin); write(archivePath, archiveBytes);
        const auto moved = data / "Moved.data";
        std::filesystem::rename(archivePath, moved);
        std::filesystem::create_symlink(moved.filename(), archivePath);
        CHECK_THROWS_AS(restarted.recover({roots}), JournalRecoveryRequired);
        CHECK(read(plugin) == oldPlugin);
        std::filesystem::remove(archivePath); std::filesystem::rename(moved, archivePath);
        write(data / "Added.ba2", archiveBytes);
        CHECK_THROWS_AS(restarted.recover({roots}), JournalRecoveryRequired);
        CHECK(read(plugin) == oldPlugin);
        std::filesystem::remove(data / "Added.ba2");
        const auto order = roots.archiveProviderOrder; roots.archiveProviderOrder.clear();
        CHECK_THROWS_AS(restarted.recover({roots}), JournalRecoveryRequired);
        CHECK(read(plugin) == oldPlugin); roots.archiveProviderOrder = order;
        CHECK_NOTHROW(restarted.recover({roots}));
    }
    CHECK(read(plugin) == newPlugin);
}

// Detects: changed read-only Data tables must block interrupted recovery before the remaining plugin write.
TEST_CASE("Journal recovery revalidates independent Data string inventory") {
    const auto root = tempRoot(); Cleanup cleanup{root};
    const auto plugin = root / "mod" / "Patch.esp", modStrings = root / "mod" / "Strings", data = root / "Data";
    const auto oldPlugin = fo4::test::make_tes4_plugin(0, 1.0f, {}), newPlugin = fo4::test::make_tes4_plugin(fo4::RecordFlags::localized, 1.0f, {});
    const std::vector<std::byte> oldSidecar{std::byte{1}}, newSidecar{std::byte{2}}, dataBytes{std::byte{3}};
    write(plugin, oldPlugin); write(modStrings / "Patch_en.STRINGS", oldSidecar); write(data / "Strings" / "Patch_de.STRINGS", dataBytes);
    auto readRoot = std::make_shared<Ba2ReadRoot>(Ba2ReadRoot::authorize(data));
    JournalRoots roots; roots.plugin = plugin; roots.pluginDirectoryIdentity = JournalDestination::captureRoot(plugin.parent_path());
    roots.approvedRoots.push_back({plugin.parent_path(), *roots.pluginDirectoryIdentity}); roots.approvedReadRoots.push_back({data, readRoot});
    roots.readStringDirectories.push_back({data, {"Strings"}, {}});
    const auto inventories = captureJournalReadStringInventories(roots.readStringDirectories, roots.approvedReadRoots, plugin);
    JournalInput target{"plugin", "", true, fo4::io::sha256_hex(oldPlugin), oldPlugin.size(), newPlugin};
    JournalInput table{"strings", "en", true, fo4::io::sha256_hex(oldSidecar), oldSidecar.size(), newSidecar};
    table.rootSelector = plugin.parent_path(); table.directories = {"Strings"};
    {
        JournalSession journal(root / "journal"); ResetHook reset; setJournalCommitHook(&interrupt);
        CHECK_THROWS_AS(journal.publish(roots, {target, table}, {}, {}, inventories), JournalRecoveryRequired);
        setJournalCommitHook(nullptr);
    }
    // Detects: recovery must not treat a recorded Data selector as independent read authorization.
    {
        auto unauthorized = roots; unauthorized.readStringDirectories[0].rootSelector = plugin.parent_path();
        JournalSession restarted(root / "journal"); CHECK_THROWS_AS(restarted.recover({unauthorized}), JournalRecoveryRequired);
        CHECK(read(plugin) == oldPlugin); CHECK(read(modStrings / "Patch_en.STRINGS") == newSidecar);
    }
    write(data / "Strings" / "Patch_de.STRINGS", {std::byte{4}});
    {
        JournalSession restarted(root / "journal"); CHECK_THROWS_AS(restarted.recover({roots}), JournalRecoveryRequired);
        CHECK(read(plugin) == oldPlugin); CHECK(read(modStrings / "Patch_en.STRINGS") == newSidecar);
        write(data / "Strings" / "Patch_de.STRINGS", dataBytes);
        CHECK_NOTHROW(restarted.recover({roots})); CHECK(read(plugin) == newPlugin);
    }
}

// Detects: archive inventories must reject mismatched leases and ambiguous duplicate directory authority.
TEST_CASE("Journal archive inventory rejects mismatched and duplicate read roots") {
    const auto root = tempRoot(); Cleanup cleanup{root};
    const auto plugin = root / "mod" / "Patch.esp", data = root / "Data", other = root / "Other";
    const auto before = fo4::test::make_tes4_plugin(0, 1.0f, {}), after = fo4::test::make_tes4_plugin(fo4::RecordFlags::localized, 1.0f, {});
    fo4::archive::Ba2GeneralArchive archive;
    fo4::archive::Ba2GeneralEntry entry; entry.path = "Strings\\Patch_de.STRINGS";
    entry.chunks.push_back(fo4::archive::make_stored_ba2_chunk(std::vector<std::byte>{std::byte{3}})); archive.entries.push_back(std::move(entry));
    const auto content = fo4::archive::write_ba2_general(archive);
    write(plugin, before); write(data / "Provider.ba2", content); write(other / "Provider.ba2", content);
    JournalRoots roots; roots.plugin = plugin; roots.pluginDirectoryIdentity = JournalDestination::captureRoot(plugin.parent_path());
    roots.approvedReadRoots.push_back({other, std::make_shared<Ba2ReadRoot>(Ba2ReadRoot::authorize(data))});
    roots.archiveDirectories.push_back({other, {}, {}}); roots.archiveProviderOrder.push_back({other, {}, "Provider.ba2"});
    const JournalArchiveInventory inventory{other, {}, {{"Provider.ba2", content.size(), fo4::io::sha256_hex(content)}}};
    const JournalInput target{"plugin", "", true, fo4::io::sha256_hex(before), before.size(), after};
    { JournalSession journal(root / "journal"); CHECK_THROWS(journal.publish(roots, {target}, {}, {inventory})); }
    CHECK(read(plugin) == before);
    const auto held = roots.approvedReadRoots[0].lease;
    roots.approvedReadRoots = {{data, held}};
    roots.archiveDirectories = {{data, {}}, {data, {}}}; roots.archiveProviderOrder = {{data, {}, "Provider.ba2"}};
    const JournalArchiveInventory duplicate{data, {}, {{"Provider.ba2", content.size(), fo4::io::sha256_hex(content)}}};
    { JournalSession journal(root / "journal"); CHECK_THROWS(journal.publish(roots, {target}, {}, {duplicate, duplicate})); }
    CHECK(read(plugin) == before);
    roots.archiveDirectories = {{data, {}}};
    roots.archiveProviderOrder = {{data, {}, "Provider.ba2"}, {data, {}, "Provider.ba2"}};
    { JournalSession journal(root / "journal"); CHECK_THROWS(journal.publish(roots, {target}, {}, {duplicate})); }
    CHECK(read(plugin) == before);
    const std::vector<JournalArchiveInventory> tooMany(17, duplicate);
    { JournalSession journal(root / "journal"); CHECK_THROWS(journal.publish(roots, {target}, {}, tooMany)); }
    CHECK(read(plugin) == before);
    auto oversized = duplicate; oversized.providers.clear();
    for (int i = 0; i < 5; ++i) oversized.providers.push_back({std::to_string(i) + ".ba2", kJournalMaxArchiveBytes, std::string(64, 'a')});
    { JournalSession journal(root / "journal"); CHECK_THROWS(journal.publish(roots, {target}, {}, {oversized})); }
    CHECK(read(plugin) == before);
}

// Detects: malformed archive inventories must be rejected before recovery can consume unbounded or ambiguous provider data.
TEST_CASE("Journal archive manifest rejects malformed and oversized inventory") {
    const auto root = tempRoot(); Cleanup cleanup{root};
    const std::string id(32, 'a'), hash(64, 'b');
    const nlohmann::json file{{"kind", "plugin"}, {"name", ""}, {"payload", id + "_0.payload"},
        {"originalExists", true}, {"originalHash", hash}, {"originalSize", 1}, {"finalHash", hash}, {"finalSize", 1}};
    const nlohmann::json provider{{"filename", "Provider.ba2"}, {"size", 1}, {"sha256", hash}};
    nlohmann::json document{{"version", 2}, {"transaction", id}, {"plugin", (root / "Patch.esp").string()},
        {"files", nlohmann::json::array({file})},
        {"archives", nlohmann::json::array({{{"root", root.string()}, {"directories", nlohmann::json::array()},
                                             {"providers", nlohmann::json::array({provider})}}})},
        {"archiveOrder", nlohmann::json::array({{{"root", root.string()}, {"directories", nlohmann::json::array()}, {"filename", "Provider.ba2"}}})}};
    CHECK_NOTHROW(decodeJournalManifest(document.dump()));
    // Detects: archive-aware manifests must require their preconditions and remain distinct from legacy journal records.
    auto legacy = document; legacy.erase("archives"); legacy.erase("archiveOrder"); legacy["version"] = 1;
    CHECK_NOTHROW(decodeJournalManifest(legacy.dump()));
    auto missingArchives = document; missingArchives.erase("archives"); missingArchives.erase("archiveOrder");
    CHECK_THROWS(decodeJournalManifest(missingArchives.dump()));
    auto invalidLegacy = document; invalidLegacy["version"] = 1;
    CHECK_THROWS(decodeJournalManifest(invalidLegacy.dump()));
    auto readOnly = document; readOnly["version"] = 3; readOnly["readStrings"] = nlohmann::json::array();
    CHECK_NOTHROW(decodeJournalManifest(readOnly.dump()));
    readOnly.erase("readStrings"); CHECK_THROWS(decodeJournalManifest(readOnly.dump()));
    auto unexpectedReadOnly = document; unexpectedReadOnly["readStrings"] = nlohmann::json::array();
    CHECK_THROWS(decodeJournalManifest(unexpectedReadOnly.dump()));
    auto invalid = document; invalid["archives"][0]["providers"][0]["sha256"] = "bad";
    CHECK_THROWS(decodeJournalManifest(invalid.dump()));
    invalid = document; invalid["archives"][0]["providers"][0]["size"] = kJournalMaxArchiveBytes + 1;
    CHECK_THROWS(decodeJournalManifest(invalid.dump()));
    invalid = document; invalid["archiveOrder"][0]["filename"] = "../Provider.ba2";
    CHECK_THROWS(decodeJournalManifest(invalid.dump()));
    invalid = document; invalid["archiveOrder"].push_back(invalid["archiveOrder"][0]);
    CHECK_THROWS(decodeJournalManifest(invalid.dump()));
    invalid = document; auto duplicate = provider; duplicate["filename"] = "provider.ba2";
    invalid["archives"][0]["providers"].push_back(duplicate);
    CHECK_THROWS(decodeJournalManifest(invalid.dump()));
    invalid = document; invalid["archiveOrder"] = nlohmann::json::array();
    for (std::size_t i = 0; i <= kJournalMaxArchiveProviders; ++i) invalid["archiveOrder"].push_back(document["archiveOrder"][0]);
    CHECK_THROWS(decodeJournalManifest(invalid.dump()));
    invalid = document; invalid["archives"][0]["providers"] = nlohmann::json::array();
    for (std::size_t i = 0; i <= kJournalMaxArchiveProviders; ++i) invalid["archives"][0]["providers"].push_back(provider);
    CHECK_THROWS(decodeJournalManifest(invalid.dump()));
    invalid = document; invalid["archives"] = nlohmann::json::array();
    for (std::size_t i = 0; i < 17; ++i) invalid["archives"].push_back(document["archives"][0]);
    CHECK_THROWS(decodeJournalManifest(invalid.dump()));
    CHECK_THROWS(decodeJournalManifest(std::string(kJournalMaxManifestBytes + 1, ' ')));
}

// Detects: profile loading must reconstruct independently authorized archive roots and replay before parsing the plugin.
TEST_CASE("Profile load recovers an archive-guarded transaction before plugin read") {
    const auto root = tempRoot(); Cleanup cleanup{root};
    const auto plugin = root / "mod" / "Patch.esp", data = root / "Data", strings = root / "mod" / "Strings";
    const auto before = fo4::test::make_tes4_plugin(0, 1.0f, {}), after = fo4::test::make_tes4_plugin(fo4::RecordFlags::localized, 1.0f, {});
    using Kind = fo4::localization::StringTableKind;
    const auto oldTable = fo4::localization::write_string_table({Kind::strings, {{10, {std::byte{'O'}}}}});
    const auto newTable = fo4::localization::write_string_table({Kind::strings, {{10, {std::byte{'N'}}}}});
    fo4::archive::Ba2GeneralArchive archive; fo4::archive::Ba2GeneralEntry entry; entry.path = "Strings\\Patch_de.STRINGS";
    entry.chunks.push_back(fo4::archive::make_stored_ba2_chunk(std::vector<std::byte>{std::byte{3}})); archive.entries.push_back(std::move(entry));
    const auto archiveBytes = fo4::archive::write_ba2_general(archive);
    write(plugin, before); write(strings / "Patch_en.STRINGS", oldTable); write(data / "Patch - Main.ba2", archiveBytes);
    JournalRoots roots; roots.plugin = plugin; roots.pluginDirectoryIdentity = JournalDestination::captureRoot(plugin.parent_path());
    roots.approvedRoots.push_back({plugin.parent_path(), *roots.pluginDirectoryIdentity});
    roots.approvedReadRoots.push_back({data, std::make_shared<Ba2ReadRoot>(Ba2ReadRoot::authorize(data))});
    roots.approvedReadRoots.push_back({plugin.parent_path(), std::make_shared<Ba2ReadRoot>(Ba2ReadRoot::authorize(plugin.parent_path()))});
    roots.archiveDirectories = {{data, {}}, {plugin.parent_path(), {}}};
    roots.archiveProviderOrder = {{data, {}, "Patch - Main.ba2"}};
    const std::vector<JournalArchiveInventory> inventories{{data, {}, {{"Patch - Main.ba2", archiveBytes.size(), fo4::io::sha256_hex(archiveBytes)}}},
                                                          {plugin.parent_path(), {}, {}}};
    JournalInput target{"plugin", "", true, fo4::io::sha256_hex(before), before.size(), after};
    JournalInput table{"strings", "en", true, fo4::io::sha256_hex(oldTable), oldTable.size(), newTable};
    table.rootSelector = plugin.parent_path(); table.directories = {"Strings"};
    {
        JournalSession journal(root / "journal"); ResetHook reset; setJournalCommitHook(&interrupt);
        CHECK_THROWS_AS(journal.publish(roots, {target, table}, {}, inventories), JournalRecoveryRequired);
        setJournalCommitHook(nullptr);
    }
    fo4ide::plugin::RadiclePluginBackend backend; backend.setDataDirectory(data); backend.testSetJournalRoot(root / "journal");
    const auto args = nlohmann::json{{"plugins", nlohmann::json::array({{{"name", "pAtCh.esp"}, {"path", plugin.string()}, {"source", "ArchiveRecovery"}, {"resolved", true}}})}};
    // Detects: replacing an authorized Data root must stop recovery before any remaining destination changes.
    const auto pluginAtInterruption = read(plugin), tableAtInterruption = read(strings / "Patch_en.STRINGS");
    std::filesystem::rename(data, root / "original-data");
    write(data / "Patch - Main.ba2", archiveBytes);
    const auto warned = backend.invoke("environment.LoadMo2", args);
    REQUIRE(warned.has_value());
    CHECK(warned->at("warnings").dump().find("read root") != std::string::npos);
    CHECK(read(plugin) == pluginAtInterruption);
    CHECK(read(strings / "Patch_en.STRINGS") == tableAtInterruption);
    std::filesystem::remove_all(data);
    std::filesystem::rename(root / "original-data", data);
    // Detects: an unparseable profile member must not silently change the archive provider set during recovery.
    const auto broken = root / "other" / "Broken.esp";
    write(broken, {std::byte{1}});
    auto invalidProfile = args;
    invalidProfile["plugins"].push_back({{"name", "Broken.esp"}, {"path", broken.string()}, {"source", "Other"}, {"resolved", true}});
    const auto invalidLoad = backend.invoke("environment.LoadMo2", invalidProfile);
    REQUIRE_FALSE(invalidLoad.has_value());
    CHECK(invalidLoad.error().type == "JournalRecoveryProfileInvalid");
    CHECK(invalidLoad.error().message.find("Broken.esp") != std::string::npos);
    CHECK(read(plugin) == pluginAtInterruption);
    CHECK(read(strings / "Patch_en.STRINGS") == tableAtInterruption);
    const auto loaded = backend.invoke("environment.LoadMo2", args);
    REQUIRE(loaded.has_value()); CHECK(loaded->at("loadedPlugins") == 1);
    const auto readback = backend.invoke("strings.ExportStrings", nlohmann::json::array({"Patch.esp", "en", "STRINGS"}));
    REQUIRE(readback.has_value());
    CHECK(nlohmann::json::parse(readback->get<std::string>())["rows"][0]["text"] == "N");
}

// Detects: an archive-only sibling language must protect its IDs through localized creation, journal save, and reload.
TEST_CASE("Localized quest save preserves archive sibling allocation") {
    const auto root = tempRoot(); Cleanup cleanup{root};
    const auto plugin = root / "mod" / "Patch.esp", data = root / "Data";
    using Kind = fo4::localization::StringTableKind;
    write(plugin, fo4::test::make_tes4_plugin(fo4::RecordFlags::localized, 1.0f, {}));
    write(root / "mod" / "Strings" / "Patch_en.STRINGS", fo4::localization::write_string_table({Kind::strings, {{17, {std::byte{'O'}}}}}));
    write(root / "mod" / "Strings" / "Patch_en.DLSTRINGS", fo4::localization::write_string_table({Kind::dlstrings, {}}));
    write(root / "mod" / "Strings" / "Patch_en.ILSTRINGS", fo4::localization::write_string_table({Kind::ilstrings, {}}));
    fo4::archive::Ba2GeneralEntry entry; entry.path = "Strings\\Patch_de.STRINGS";
    entry.chunks.push_back(fo4::archive::make_stored_ba2_chunk(fo4::localization::write_string_table({Kind::strings, {{300, {std::byte{'D'}}}}})));
    fo4::archive::Ba2GeneralArchive archive; archive.entries.push_back(std::move(entry));
    write(data / "Patch - Main.ba2", fo4::archive::write_ba2_general(archive));
    const auto args = nlohmann::json{{"plugins", nlohmann::json::array({{{"name", "Patch.esp"}, {"path", plugin.string()}, {"source", "ArchiveSave"}, {"resolved", true}}})}};
    fo4ide::plugin::RadiclePluginBackend backend; backend.setDataDirectory(data); backend.testSetJournalRoot(root / "journal");
    REQUIRE(backend.invoke("environment.LoadMo2", args).has_value());
    // Detects: held-source quest allocation must not reopen the legacy archive resolver.
    ResetLegacyResolverHook resetLegacy; fo4ide::plugin::setLegacyStringResolverHook(&forbidLegacyStringResolver);
    const auto created = backend.invokeBackend("CreateRecord", nlohmann::json::array({"Patch.esp", "QUST", {{"editorId", "ArchiveQuest"}, {"name", "New"}}}));
    REQUIRE(created.has_value()); const auto quest = nlohmann::json::parse(created->get<std::string>()).at("formKey").get<std::string>();
    fo4ide::plugin::setLegacyStringResolverHook(nullptr);
    // Detects: changing a provider's payload or order after allocation must refuse save before plugin or sidecar writes.
    SECTION("provider payload is replaced with the same index") {
        fo4::archive::Ba2GeneralEntry replacement; replacement.path = "Strings\\Patch_de.STRINGS";
        replacement.chunks.push_back(fo4::archive::make_stored_ba2_chunk(fo4::localization::write_string_table({Kind::strings, {{300, {std::byte{'E'}}}}})));
        fo4::archive::Ba2GeneralArchive changed; changed.entries.push_back(std::move(replacement));
        const auto originalPlugin = read(plugin), originalTable = read(root / "mod" / "Strings" / "Patch_en.STRINGS");
        const auto originalTime = std::filesystem::last_write_time(data / "Patch - Main.ba2");
        write(data / "Patch - Main.ba2", fo4::archive::write_ba2_general(changed));
        std::filesystem::last_write_time(data / "Patch - Main.ba2", originalTime);
        const auto refused = backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"}));
        REQUIRE_FALSE(refused.has_value()); CHECK(refused.error().type == "LocalizedSidecarChangedOnDisk");
        CHECK(read(plugin) == originalPlugin); CHECK(read(root / "mod" / "Strings" / "Patch_en.STRINGS") == originalTable);
    }
    SECTION("provider order changes") {
        const auto originalPlugin = read(plugin), originalTable = read(root / "mod" / "Strings" / "Patch_en.STRINGS");
        write(data / "Patch - Secondary.ba2", fo4::archive::write_ba2_general(archive));
        const auto refused = backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"}));
        REQUIRE_FALSE(refused.has_value()); CHECK(refused.error().type == "LocalizedSidecarChangedOnDisk");
        CHECK(read(plugin) == originalPlugin); CHECK(read(root / "mod" / "Strings" / "Patch_en.STRINGS") == originalTable);
    }
    // Detects: changing the Data root after allocation must not publish using stale sources.
    SECTION("Data root is cleared after allocation") {
        const auto originalPlugin = read(plugin), originalTable = read(root / "mod" / "Strings" / "Patch_en.STRINGS");
        backend.setDataDirectory({});
        const nlohmann::json context{{"language", "en"}, {"stringsDirectory", (root / "mod" / "Strings").string()}};
        const auto edited = backend.invokeBackend("SetQuestStages", nlohmann::json::array({"Patch.esp", quest, nlohmann::json::array({{{"index", 10}, {"logEntry", "Begin"}}}), context}));
        REQUIRE_FALSE(edited.has_value()); CHECK(edited.error().type == "LocalizedSidecarChangedOnDisk");
        const auto refused = backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"}));
        REQUIRE_FALSE(refused.has_value()); CHECK(refused.error().type == "LocalizedSidecarChangedOnDisk");
        CHECK(read(plugin) == originalPlugin); CHECK(read(root / "mod" / "Strings" / "Patch_en.STRINGS") == originalTable);
    }
    // Detects: recovery must reject a changed archive and then finish after the original provider is restored.
    SECTION("interrupted save verifies archive and recovers its own sidecar") {
        const auto archivePath = data / "Patch - Main.ba2";
        const auto originalArchive = read(archivePath);
        ResetHook reset; setJournalCommitHook(interrupt);
        const auto saved = backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"}));
        setJournalCommitHook(nullptr);
        REQUIRE_FALSE(saved.has_value());
        CHECK(saved.error().type == "JournalRecoveryRequired");
        const auto interruptedPlugin = read(plugin);
        const auto interruptedTable = read(root / "mod" / "Strings" / "Patch_en.STRINGS");
        fo4::archive::Ba2GeneralEntry replacement; replacement.path = "Strings\\Patch_de.STRINGS";
        replacement.chunks.push_back(fo4::archive::make_stored_ba2_chunk(fo4::localization::write_string_table({Kind::strings, {{300, {std::byte{'E'}}}}})));
        fo4::archive::Ba2GeneralArchive changed; changed.entries.push_back(std::move(replacement));
        write(archivePath, fo4::archive::write_ba2_general(changed));
        fo4ide::plugin::RadiclePluginBackend restarted; restarted.setDataDirectory(data); restarted.testSetJournalRoot(root / "journal");
        const auto warned = restarted.invoke("environment.LoadMo2", args);
        REQUIRE(warned.has_value());
        CHECK(warned->at("warnings").dump().find("journal archive content changed") != std::string::npos);
        CHECK(read(plugin) == interruptedPlugin);
        CHECK(read(root / "mod" / "Strings" / "Patch_en.STRINGS") == interruptedTable);
        const auto refusedSave = restarted.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"}));
        REQUIRE_FALSE(refusedSave.has_value());
        CHECK(refusedSave.error().type == "JournalRecoveryRequired");
        CHECK(read(plugin) == interruptedPlugin);
        write(archivePath, originalArchive);
        REQUIRE(restarted.invoke("environment.LoadMo2", args).has_value());
        const auto exported = restarted.invoke("strings.ExportStrings", nlohmann::json::array({"Patch.esp", "en", "STRINGS"}));
        REQUIRE(exported.has_value());
        const auto rows = nlohmann::json::parse(exported->get<std::string>()).at("rows");
        CHECK(std::any_of(rows.begin(), rows.end(), [](const auto& row) { return row.at("id") == 301 && row.at("text") == "New"; }));
    }
    SECTION("unchanged providers save and reload") {
    const nlohmann::json context{{"language", "en"}, {"stringsDirectory", (root / "mod" / "Strings").string()}};
    fo4ide::plugin::setLegacyStringResolverHook(&forbidLegacyStringResolver); REQUIRE(backend.invokeBackend("SetQuestStages", nlohmann::json::array({"Patch.esp", quest, nlohmann::json::array({{{"index", 10}, {"logEntry", "Begin"}}}), context})).has_value());
    REQUIRE(backend.invokeBackend("SetQuestObjectives", nlohmann::json::array({"Patch.esp", quest, nlohmann::json::array({{{"index", 1}, {"displayText", "Objective"}, {"targets", nlohmann::json::array()}}}), context})).has_value()); fo4ide::plugin::setLegacyStringResolverHook(nullptr);
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    fo4ide::plugin::RadiclePluginBackend restarted; restarted.setDataDirectory(data); restarted.testSetJournalRoot(root / "journal");
    REQUIRE(restarted.invoke("environment.LoadMo2", args).has_value());
    const auto exported = restarted.invoke("strings.ExportStrings", nlohmann::json::array({"Patch.esp", "en", "STRINGS"}));
    REQUIRE(exported.has_value());
    const auto rows = nlohmann::json::parse(exported->get<std::string>()).at("rows");
    CHECK(std::any_of(rows.begin(), rows.end(), [](const auto& row) { return row.at("id") == 301 && row.at("text") == "New"; }));
    const auto stages = restarted.invokeBackend("GetQuestStages", nlohmann::json::array({"Patch.esp", quest, context}));
    REQUIRE(stages.has_value()); CHECK(stages->at("stages").at(0).at("logEntries").at(0).at("logText") == "Begin");
    const auto objectives = restarted.invokeBackend("GetQuestObjectives", nlohmann::json::array({"Patch.esp", quest, context}));
    REQUIRE(objectives.has_value()); CHECK(objectives->at("objectives").at(0).at("displayText") == "Objective");
    }
}
#endif
