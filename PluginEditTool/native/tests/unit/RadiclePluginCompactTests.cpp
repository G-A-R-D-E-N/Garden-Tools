#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <fo4/flags.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

constexpr std::uint32_t kMasterOwner = 0x01000000u;
constexpr std::uint32_t kNextAfterOutOfRange = 0x5002u;

std::filesystem::path tempRoot() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / ("fo4ide_radicle_compact_" + std::to_string(id));
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

std::string readFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

void loadBackend(fo4ide::plugin::RadiclePluginBackend& backend,
                 const std::filesystem::path& path) {
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({{
            {"name", "Patch.esp"},
            {"path", path.string()},
            {"source", "Test Mod"},
            {"resolved", true}
        }})}
    };
    const auto loaded = backend.invoke("environment.LoadMo2", environment);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->at("loadedPlugins") == 1);
}

std::string eligibility(fo4ide::plugin::RadiclePluginBackend& backend) {
    const auto result = backend.invokeBackend(
        "CheckEslEligibility", nlohmann::json::array({"Patch.esp"}));
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    return result->get<std::string>();
}

void savePatch(fo4ide::plugin::RadiclePluginBackend& backend) {
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
}

std::string labelBytes(std::uint32_t id) {
    return {static_cast<char>(id), static_cast<char>(id >> 8),
            static_cast<char>(id >> 16), static_cast<char>(id >> 24)};
}

void writeHeader(fo4::test::ByteWriter& out,
                 float version,
                 std::uint32_t flags,
                 std::uint32_t nextFormId,
                 bool hasMaster) {
    const auto tes4 = fo4::test::begin_record(out, fo4::sig_tes4.to_string(), flags, 0, 131);
    fo4::test::ByteWriter hedr;
    hedr.f32(version);
    hedr.u32(0);
    hedr.u32(nextFormId);
    fo4::test::write_subrecord(out, fo4::sig_hedr.to_string(), hedr.bytes);
    if (hasMaster) {
        fo4::test::write_subrecord(out, fo4::sig_mast.to_string(), std::string_view{"Base.esm\0", 9});
        fo4::test::ByteWriter data;
        data.u32(0);
        data.u32(0);
        fo4::test::write_subrecord(out, fo4::sig_data.to_string(), data.bytes);
    }
    fo4::test::end_record(out, tes4);
}

void writeNamedRecord(fo4::test::ByteWriter& out,
                      std::string_view type,
                      std::uint32_t rawId,
                      const std::string& editorId) {
    const auto record = fo4::test::begin_record(out, type, 0, rawId, 131);
    const auto data = editorId + '\0';
    fo4::test::write_subrecord(out, fo4::sig_edid.to_string(), std::string_view{data});
    fo4::test::end_record(out, record);
}

std::vector<std::byte> miscPlugin(float version,
                                  std::uint32_t flags,
                                  std::uint32_t nextFormId,
                                  bool hasMaster,
                                  const std::vector<std::uint32_t>& localIds) {
    fo4::test::ByteWriter out;
    writeHeader(out, version, flags, nextFormId, hasMaster);
    const auto group = fo4::test::begin_group(out, "MISC", 0);
    for (const auto local : localIds) {
        writeNamedRecord(out, "MISC", (hasMaster ? kMasterOwner : 0u) | local,
                         "Record" + std::to_string(local));
    }
    fo4::test::end_group(out, group);
    return out.bytes;
}

std::vector<std::byte> childGroupPlugin(std::string_view parentType,
                                        std::uint32_t parentLocal,
                                        std::string_view childType,
                                        std::uint32_t childLocal,
                                        fo4::GroupType childGroup) {
    fo4::test::ByteWriter out;
    writeHeader(out, 1.0f, 0, kNextAfterOutOfRange, true);
    const auto top = fo4::test::begin_group(out, parentType, 0);
    writeNamedRecord(out, parentType, kMasterOwner | parentLocal, "Parent");
    const auto children = fo4::test::begin_group(
        out, labelBytes(kMasterOwner | parentLocal), static_cast<std::int32_t>(childGroup));
    writeNamedRecord(out, childType, kMasterOwner | childLocal, "Child");
    fo4::test::end_group(out, children);
    fo4::test::end_group(out, top);
    return out.bytes;
}

std::vector<std::byte> masterlessLinkPlugin() {
    fo4::test::ByteWriter out;
    writeHeader(out, 1.0f, 0, 0x901u, false);
    const auto misc = fo4::test::begin_group(out, "MISC", 0);
    writeNamedRecord(out, "MISC", 0x700u, "Moved");
    fo4::test::end_group(out, misc);
    const auto races = fo4::test::begin_group(out, "RACE", 0);
    const auto race = fo4::test::begin_record(out, "RACE", 0, 0x900u, 131);
    fo4::test::write_subrecord(out, fo4::sig_edid.to_string(), std::string_view{"Referrer\0", 9});
    fo4::test::ByteWriter link;
    link.u32(0x700u);
    fo4::test::write_subrecord(out, "AHCM", link.bytes);
    fo4::test::end_record(out, race);
    fo4::test::end_group(out, races);
    return out.bytes;
}

const fo4::Record* recordAt(const fo4::Plugin& plugin, std::uint32_t local) {
    return fo4::find_record(plugin, fo4::FormKey{plugin.key, fo4::LocalFormId{local}});
}

std::string editorIdOf(const fo4::Record& record) {
    for (const auto& subrecord : record.subrecords) {
        if (subrecord.signature != fo4::sig_edid) continue;
        const auto* begin = reinterpret_cast<const char*>(subrecord.data.data());
        const auto* end = begin + subrecord.data.size();
        const auto* nul = std::find(begin, end, '\0');
        return std::string(begin, nul);
    }
    return {};
}

std::uint32_t formLinkOf(const fo4::Record& record, std::string_view signature) {
    const auto wanted = fo4::Signature::from_string(signature);
    const fo4::Subrecord* found = nullptr;
    for (const auto& subrecord : record.subrecords) {
        if (subrecord.signature != wanted) continue;
        found = &subrecord;
        break;
    }
    if (found == nullptr) FAIL("missing FormLink subrecord");
    REQUIRE(found->data.size() == 4);
    std::uint32_t value = 0;
    std::memcpy(&value, found->data.data(), sizeof(value));
    return value;
}

std::string refuseAndCheckUnchanged(const std::vector<std::byte>& bytes,
                                    std::string_view summary,
                                    std::string_view code) {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, bytes);
    const auto before = readFile(path);

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);
    const auto report = eligibility(backend);
    CHECK(report.find("would be BLOCKED") != std::string::npos);
    CHECK(report.find(summary) != std::string::npos);
    CHECK(report.find("can fit") == std::string::npos);

    const auto result = backend.compactToEsl(nlohmann::json::array({"Patch.esp"}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == code);
    savePatch(backend);
    CHECK(readFile(path) == before);
    return result.error().message;
}

}  // namespace

TEST_CASE("Native CompactToEsl refuses an out-of-range parent with child groups and leaves the file unchanged") {
    const std::vector<std::tuple<std::vector<std::byte>, std::string, std::string>> rows = {
        {childGroupPlugin("DIAL", 0x5000u, "INFO", 0x801u, fo4::GroupType::topic_children),
         "1 DIAL with child groups",
         "Cannot renumber 005000:Patch.esp: it has child groups labelled with its form ID; "
         "parent GRUP label rewriting is not supported yet."},
        {childGroupPlugin("WRLD", 0x1234u, "CELL", 0x800u, fo4::GroupType::world_children),
         "1 WRLD with child groups",
         ""},
    };
    const auto& [bytes, summary, expected] = rows[GENERATE(std::size_t{0}, std::size_t{1})];
    const auto message = refuseAndCheckUnchanged(bytes, summary, "ParentGroupRelabelUnsupported");
    if (!expected.empty()) CHECK(message == expected);
}

TEST_CASE("Native ESL eligibility and CompactToEsl refuse an out-of-range REFR nested in CELL children") {
    refuseAndCheckUnchanged(
        childGroupPlugin("CELL", 0x800u, "REFR", 0x5001u, fo4::GroupType::cell_children),
        "1 REFR nested",
        "NestedRecordCompactionUnsupported");
}

TEST_CASE("Native ESL compaction hints at masters.SetLight only when a compatible plugin is not light-flagged") {
    bool light = false;
    SECTION("A compatible plugin without the light flag gets the hint") {}
    SECTION("A compatible light-flagged plugin gets no hint") { light = true; }

    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, miscPlugin(1.0f, light ? fo4::RecordFlags::small_file : 0u, 0x802u, true,
                                {0x001u, 0x800u}));

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);
    const auto report = eligibility(backend);
    const auto compacted = backend.compactToEsl(nlohmann::json::array({"Patch.esp"}));
    REQUIRE(compacted.has_value());
    REQUIRE(compacted->is_string());
    const auto text = compacted->get<std::string>();
    CHECK(text.find("already ESL-compatible") != std::string::npos);
    CHECK(report.find("already ESL-compatible") != std::string::npos);
    if (light) {
        CHECK(text.find("masters.SetLight") == std::string::npos);
        CHECK(report.find("masters.SetLight") == std::string::npos);
    } else {
        CHECK(text.find("Run masters.SetLight to flag the plugin light") != std::string::npos);
        CHECK(report.find("Run masters.SetLight to flag the plugin light") != std::string::npos);
    }
}

TEST_CASE("Native ESL compaction gives an .esl-named plugin without the light flag no SetLight hint") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esl";
    writeBytes(path, miscPlugin(1.0f, 0u, 0x802u, true, {0x001u, 0x800u}));

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({{
            {"name", "Patch.esl"},
            {"path", path.string()},
            {"source", "Test Mod"},
            {"resolved", true}
        }})}
    };
    const auto loaded = backend.invoke("environment.LoadMo2", environment);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->at("loadedPlugins") == 1);

    const auto compacted = backend.compactToEsl(nlohmann::json::array({"Patch.esl"}));
    REQUIRE(compacted.has_value());
    REQUIRE(compacted->is_string());
    const auto text = compacted->get<std::string>();
    CHECK(text.find("already ESL-compatible") != std::string::npos);
    CHECK(text.find("masters.SetLight") == std::string::npos);
}

TEST_CASE("Native CompactToEsl moves out-of-range IDs to the 0x800 floor for HEDR 0.95 plugins with a master") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, miscPlugin(0.95f, 0u, 0x5001u, true, {0x005u, 0x5000u}));

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);
    const auto result = backend.compactToEsl(nlohmann::json::array({"Patch.esp"}));
    REQUIRE(result.has_value());
    CHECK(result->get<std::string>().find("remapped 2 record") != std::string::npos);
    CHECK(result->get<std::string>().find("masters.SetLight") != std::string::npos);
    savePatch(backend);

    const auto disk = fo4::read_plugin_file(path);
    CHECK_FALSE(disk.header.tes4_flags.is_small_file());
    CHECK(disk.header.next_form_id == 0x802u);
    CHECK(recordAt(disk, 0x005u) == nullptr);
    CHECK(recordAt(disk, 0x5000u) == nullptr);
    REQUIRE(recordAt(disk, 0x800u) != nullptr);
    CHECK(editorIdOf(*recordAt(disk, 0x800u)) == "Record5");
    REQUIRE(recordAt(disk, 0x801u) != nullptr);
    CHECK(editorIdOf(*recordAt(disk, 0x801u)) == "Record20480");
}

TEST_CASE("Native CompactToEsl keeps an already-light plugin light and fills the 0x001 floor") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, miscPlugin(1.0f, fo4::RecordFlags::small_file, 0x5001u, true,
                                {0x005u, 0x5000u}));
    const auto before = fo4::read_plugin_file(path);

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);
    const auto result = backend.compactToEsl(nlohmann::json::array({"Patch.esp"}));
    REQUIRE(result.has_value());
    CHECK(result->get<std::string>().find("remapped 1 record") != std::string::npos);
    CHECK(result->get<std::string>().find("masters.SetLight") == std::string::npos);
    savePatch(backend);

    const auto disk = fo4::read_plugin_file(path);
    CHECK(disk.header.tes4_flags.is_small_file());
    CHECK(disk.header.tes4_flags.value() == before.header.tes4_flags.value());
    CHECK(disk.header.next_form_id == 0x002u);
    CHECK(recordAt(disk, 0x005u) != nullptr);
    CHECK(recordAt(disk, 0x5000u) == nullptr);
    REQUIRE(recordAt(disk, 0x001u) != nullptr);
    CHECK(editorIdOf(*recordAt(disk, 0x001u)) == "Record20480");
}

TEST_CASE("Native CompactToEsl rewrites a masterless FormLink to a record moved to the 0x800 floor") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, masterlessLinkPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);
    const auto result = backend.compactToEsl(nlohmann::json::array({"Patch.esp"}));
    REQUIRE(result.has_value());
    CHECK(result->get<std::string>().find("remapped 1 record") != std::string::npos);
    savePatch(backend);

    const auto disk = fo4::read_plugin_file(path);
    CHECK(recordAt(disk, 0x700u) == nullptr);
    REQUIRE(recordAt(disk, 0x800u) != nullptr);
    CHECK(editorIdOf(*recordAt(disk, 0x800u)) == "Moved");
    REQUIRE(recordAt(disk, 0x900u) != nullptr);
    CHECK(formLinkOf(*recordAt(disk, 0x900u), "AHCM") == 0x800u);
}

TEST_CASE("Native SetLight lowers an out-of-range HEDR next ID to one past the highest own ID") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, miscPlugin(1.0f, 0u, 0x5000u, true, {0x005u, 0x801u}));

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);
    const auto lighted = backend.invoke("masters.SetLight", nlohmann::json::array({"Patch.esp", true}));
    REQUIRE(lighted.has_value());
    REQUIRE(lighted->is_string());
    CHECK(lighted->get<std::string>().find("lowered HEDR nextFormID to 0x802") != std::string::npos);

    const auto created = backend.invokeLegacyBackend(
        "CreateRecord",
        nlohmann::json::array({"Patch.esp", "EYES", nlohmann::json{
            {"editorId", "LowNext"},
            {"fullText", "Low Next"},
            {"icon", "Textures\\Actors\\Character\\Eyes\\low.dds"}
        }}));
    REQUIRE(created.has_value());
    CHECK(nlohmann::json::parse(created->get<std::string>()).at("formKey") == "000802:Patch.esp");
}

TEST_CASE("Native RenumberRecord to the same key reports unchanged for a DIAL with child groups") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, childGroupPlugin("DIAL", 0x5000u, "INFO", 0x801u, fo4::GroupType::topic_children));

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);
    const auto result = backend.invokeBackend(
        "RenumberRecord",
        nlohmann::json::array({"Patch.esp", "005000:Patch.esp", "005000:Patch.esp"}));
    REQUIRE(result.has_value());
    CHECK(nlohmann::json::parse(result->get<std::string>()).at("changed") == false);
}

TEST_CASE("Native RenumberRecord onto an occupied key reports RecordFormIdExists for a DIAL with child groups") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, childGroupPlugin("DIAL", 0x5000u, "INFO", 0x801u, fo4::GroupType::topic_children));

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);
    const auto result = backend.invokeBackend(
        "RenumberRecord",
        nlohmann::json::array({"Patch.esp", "005000:Patch.esp", "000801:Patch.esp"}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "RecordFormIdExists");
}

TEST_CASE("Native RenumberRecord refuses a parent with child groups and leaves the file unchanged") {
    const std::vector<std::tuple<std::vector<std::byte>, std::string, std::string>> rows = {
        {childGroupPlugin("WRLD", 0x1234u, "CELL", 0x800u, fo4::GroupType::world_children), "001234:Patch.esp", ""},
        {childGroupPlugin("DIAL", 0x5000u, "INFO", 0x801u, fo4::GroupType::topic_children), "005000:Patch.esp", "005000:Patch.esp"},
    };
    const auto& [bytes, oldKey, fragment] = rows[GENERATE(std::size_t{0}, std::size_t{1})];
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, bytes);
    const auto before = readFile(path);

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);
    const auto result = backend.invokeBackend(
        "RenumberRecord",
        nlohmann::json::array({"Patch.esp", oldKey, "000950:Patch.esp"}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "ParentGroupRelabelUnsupported");
    if (!fragment.empty()) CHECK(result.error().message.find(fragment) != std::string::npos);
    savePatch(backend);
    CHECK(readFile(path) == before);
}

TEST_CASE("Native RenumberPluginFormIds refuses a block that would move a DIAL with child groups") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, childGroupPlugin("DIAL", 0x5000u, "INFO", 0x1000u, fo4::GroupType::topic_children));
    const auto before = readFile(path);

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);
    const auto result = backend.renumberPluginFormIds(
        nlohmann::json::array({"Patch.esp", "001000", true}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "ParentGroupRelabelUnsupported");
    savePatch(backend);
    CHECK(readFile(path) == before);
}

TEST_CASE("Native RenumberPluginFormIds dry run refuses a block that would move a DIAL with child groups") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, childGroupPlugin("DIAL", 0x5000u, "INFO", 0x1000u, fo4::GroupType::topic_children));
    const auto before = readFile(path);

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);
    const auto result = backend.renumberPluginFormIds(
        nlohmann::json::array({"Patch.esp", "001000", false}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "ParentGroupRelabelUnsupported");
    CHECK(result.error().message == "Cannot renumber 005000:Patch.esp: it has child groups labelled with its "
                                    "form ID; parent GRUP label rewriting is not supported yet.");
    savePatch(backend);
    CHECK(readFile(path) == before);
}
