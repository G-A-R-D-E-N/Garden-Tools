#include <chrono>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/schema/condition_scopes.hpp>

#include <fo4ide/app/PluginBackendTarget.hpp>
#include <fo4ide/mcp/McpServer.hpp>
#include <fo4ide/rpc/RpcDispatcher.hpp>
#include <fo4ide/settings/Settings.hpp>

#include "../../apps/fo4ide/McpCatalog.hpp"
#include "../helpers/OracleFixtures.hpp"

namespace {

using namespace oracle_fixture;

bool pending(fo4ide::plugin::RadiclePluginBackend& backend, const std::string& name) {
    const auto active = parseStringResult(backend.invoke("backend.GetActivePlugins", nlohmann::json::array()));
    for (const auto& row : active) {
        if (row.at("Name") == name) return row.at("Pending").get<bool>();
    }
    FAIL("plugin not listed: " + name);
}

bool diskLocalized(const std::filesystem::path& path) {
    return fo4::read_plugin_file(path).header.tes4_flags.is_localized();
}

struct McpHarness {
    fo4ide::settings::SharedSettings settings;
    fo4ide::rpc::Dispatcher rpc;
    fo4ide::mcp::Registry registry;

    McpHarness(fo4ide::plugin::IPluginBackend& backend, const std::filesystem::path& root)
        : settings([&] {
              fo4ide::settings::Settings value;
              value.mo2InstancePath = root;
              return value;
          }()) {
        REQUIRE(rpc.registerTarget("backend", std::make_shared<fo4ide::app::PluginBackendTarget>(backend, "backend"))
                    .has_value());
        REQUIRE(fo4ide::app::registerCatalogTools(registry, rpc, {}, settings,
                                                  [] { return std::vector<std::filesystem::path>{}; }));
    }
};

std::vector<std::byte> miscWithFull(std::uint32_t flags = 0, std::string_view full = std::string_view{"Hello\0", 6}) {
    fo4::test::ByteWriter out;
    writeHeader(out, 1.0f, flags, 0x801u);
    const auto group = fo4::test::begin_group(out, "MISC", 0);
    const auto record = fo4::test::begin_record(out, "MISC", 0, 0x800u, 131);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"Named\0", 6});
    fo4::test::write_subrecord(out, "FULL", full);
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    return out.bytes;
}

void ctda(fo4::test::ByteWriter& out, std::uint16_t function, std::int32_t param1, std::uint32_t runOn = 0,
          std::uint32_t reference = 0, std::int32_t param2 = 0) {
    fo4::test::ByteWriter body;
    body.u8(0);
    body.u8(0);
    body.u8(0);
    body.u8(0);
    body.u32(0x3F800000u);
    body.u16(function);
    body.u16(0);
    body.u32(static_cast<std::uint32_t>(param1));
    body.u32(static_cast<std::uint32_t>(param2));
    body.u32(runOn);
    body.u32(reference);
    body.u32(0xFFFFFFFFu);
    fo4::test::write_subrecord(out, "CTDA", body.bytes);
}

constexpr std::uint16_t kGetItemCount = 47;
constexpr std::uint16_t kGetIsSex = 70;
constexpr std::uint16_t kGetLevel = 80;
constexpr std::uint16_t kHasPerk = 448;
constexpr std::uint16_t kGetFactionRankDifference = 60;

std::vector<std::byte> recipePlugin(const std::function<void(fo4::test::ByteWriter&)>& extra = {}) {
    fo4::test::ByteWriter out;
    writeHeader(out, 1.0f, 0, 0x802u, {"A.esp", "B.esp"});
    const auto group = fo4::test::begin_group(out, "COBJ", 0);
    const auto recipe = fo4::test::begin_record(out, "COBJ", 0, 0x02000800u, 131);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"Recipe\0", 7});
    fo4::test::ByteWriter count;
    count.u32(5);
    fo4::test::write_subrecord(out, "CITC", count.bytes);
    ctda(out, kHasPerk, 0x00000801);
    ctda(out, kHasPerk, 0x01000802);
    ctda(out, kGetIsSex, 0x00000801);
    ctda(out, kGetLevel, 0, 2, 0x00000803u);
    ctda(out, kGetItemCount, 0x02000800);
    fo4::test::end_record(out, recipe);
    const auto plain = fo4::test::begin_record(out, "COBJ", 0, 0x02000801u, 131);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"Plain\0", 6});
    fo4::test::ByteWriter created;
    created.u32(0x01000802u);
    fo4::test::write_subrecord(out, "CNAM", created.bytes);
    fo4::test::end_record(out, plain);
    fo4::test::end_group(out, group);
    if (extra) extra(out);
    return out.bytes;
}

std::vector<std::byte> brokenVmadPlugin() {
    return recipePlugin([](fo4::test::ByteWriter& out) {
        const auto group = fo4::test::begin_group(out, "ACTI", 0);
        const auto record = fo4::test::begin_record(out, "ACTI", 0, 0x02000810u, 131);
        fo4::test::write_subrecord(out, "EDID", std::string_view{"Scripted\0", 9});
        fo4::test::ByteWriter vmad;
        vmad.u16(5);
        vmad.u16(2);
        vmad.u16(1);
        vmad.u16(20);
        vmad.u8('x');
        fo4::test::write_subrecord(out, "VMAD", vmad.bytes);
        fo4::test::end_record(out, record);
        fo4::test::end_group(out, group);
    });
}

std::vector<std::byte> splitConditionsPlugin() {
    return recipePlugin([](fo4::test::ByteWriter& out) {
        const auto group = fo4::test::begin_group(out, "COBJ", 0);
        const auto record = fo4::test::begin_record(out, "COBJ", 0, 0x02000820u, 131);
        fo4::test::write_subrecord(out, "EDID", std::string_view{"Split\0", 6});
        ctda(out, kHasPerk, 0x00000801);
        fo4::test::ByteWriter created;
        created.u32(0x01000802u);
        fo4::test::write_subrecord(out, "CNAM", created.bytes);
        ctda(out, kHasPerk, 0x01000802);
        fo4::test::end_record(out, record);
        fo4::test::end_group(out, group);
    });
}

std::vector<std::byte> otherRecordConditionsPlugin() {
    return recipePlugin([](fo4::test::ByteWriter& out) {
        for (const char* signature : {"QUST", "PERK"}) {
            const auto group = fo4::test::begin_group(out, signature, 0);
            const auto record = fo4::test::begin_record(out, signature, 0, signature[0] == 'Q' ? 0x02000830u : 0x02000831u, 131);
            fo4::test::write_subrecord(out, "EDID", std::string_view{"Other\0", 6});
            ctda(out, kHasPerk, 0x00000801);
            fo4::test::end_record(out, record);
            fo4::test::end_group(out, group);
        }
    });
}

std::vector<std::byte> param2Plugin() {
    fo4::test::ByteWriter out;
    writeHeader(out, 1.0f, 0, 0x802u, {"A.esp"});
    const auto group = fo4::test::begin_group(out, "COBJ", 0);
    const auto record = fo4::test::begin_record(out, "COBJ", 0, 0x01000800u, 131);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"Second\0", 7});
    fo4::test::ByteWriter count;
    count.u32(2);
    fo4::test::write_subrecord(out, "CITC", count.bytes);
    ctda(out, kGetFactionRankDifference, 0x01000800, 0, 0, 0x00000804);
    ctda(out, kGetItemCount, 0x01000800);
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    return out.bytes;
}

std::uint32_t citcOf(const fo4::Record& record) {
    for (const auto& subrecord : record.subrecords) {
        if (subrecord.signature != fo4::Signature::from_string("CITC")) continue;
        REQUIRE(subrecord.data.size() == 4);
        std::uint32_t value = 0;
        std::memcpy(&value, subrecord.data.data(), sizeof(value));
        return value;
    }
    FAIL("CITC missing");
}

std::vector<std::string> masterNames(const fo4::Plugin& plugin) {
    std::vector<std::string> names;
    for (const auto& master : plugin.header.masters) names.emplace_back(master.key.filename());
    return names;
}

std::size_t ctdaCount(const fo4::Record& record) {
    return static_cast<std::size_t>(std::count_if(record.subrecords.begin(), record.subrecords.end(),
        [](const auto& subrecord) { return subrecord.signature == fo4::Signature::from_string("CTDA"); }));
}

nlohmann::json stripArgs(const std::filesystem::path& source, const std::string& masters,
                         const std::filesystem::path& output, nlohmann::json dryRun) {
    return nlohmann::json::array({source.string(), masters, output.string(), std::move(dryRun)});
}

struct StripFixture {
    std::filesystem::path root = tempRoot();
    Cleanup cleanup{root};
    std::filesystem::path source = root / "Source.esp";
    std::filesystem::path output = root / "Clean.esp";
    fo4ide::plugin::RadiclePluginBackend backend;

    StripFixture() {
        writeBytes(source, recipePlugin());
        writeBytes(root / "Patch.esp", miscPlugin(1.0f, 0x801u, false, {0x800u}));
        loadPatch(backend, root / "Patch.esp");
    }
};


std::vector<std::byte> batchSource() {
    fo4::test::ByteWriter out;
    writeHeader(out, 1.0f, 0, 0x900u);
    const auto kywd = fo4::test::begin_group(out, "KYWD", 0);
    for (const auto& [id, name] : {std::pair{0x801u, std::string_view{"KwA\0", 4}}, std::pair{0x802u, std::string_view{"KwB\0", 4}},
                                   std::pair{0x803u, std::string_view{"KwC\0", 4}}}) {
        const auto record = fo4::test::begin_record(out, "KYWD", 0, id, 131);
        fo4::test::write_subrecord(out, "EDID", name);
        fo4::test::end_record(out, record);
    }
    fo4::test::end_group(out, kywd);
    const auto amdl = fo4::test::begin_group(out, "AMDL", 0);
    for (const auto id : {0x830u, 0x831u}) {
        const auto record = fo4::test::begin_record(out, "AMDL", 0, id, 131);
        fo4::test::end_record(out, record);
    }
    fo4::test::end_group(out, amdl);
    const auto ammo = fo4::test::begin_group(out, "AMMO", 0);
    for (const auto& [id, name] : {std::pair{0x840u, std::string_view{"AmmoA\0", 6}}, std::pair{0x841u, std::string_view{"AmmoB\0", 6}}}) {
        const auto record = fo4::test::begin_record(out, "AMMO", 0, id, 131);
        fo4::test::write_subrecord(out, "EDID", name);
        fo4::test::end_record(out, record);
    }
    fo4::test::end_group(out, ammo);
    const auto weap = fo4::test::begin_group(out, "WEAP", 0);
    struct Gun { std::uint32_t id; std::string name; std::uint16_t maxX; std::vector<std::uint32_t> keywords; };
    for (const auto& gun : {Gun{0x810u, "Alpha", 10, {0x801u, 0x802u}}, Gun{0x811u, "Beta", 20, {0x801u, 0x802u}},
                            Gun{0x812u, "Gamma", 30, {0x801u}}}) {
        const auto record = fo4::test::begin_record(out, "WEAP", 0, gun.id, 131);
        const auto name = gun.name + '\0';
        fo4::test::write_subrecord(out, "EDID", std::string_view{name});
        fo4::test::ByteWriter bounds;
        for (const auto value : {std::uint16_t{0}, std::uint16_t{0}, std::uint16_t{0}, gun.maxX, std::uint16_t{0}, std::uint16_t{0}}) {
            bounds.u16(value);
        }
        fo4::test::write_subrecord(out, "OBND", bounds.bytes);
        fo4::test::ByteWriter aim;
        aim.u32(0x830u);
        fo4::test::write_subrecord(out, "WAMD", aim.bytes);
        fo4::test::ByteWriter znam;
        znam.u32(0);
        fo4::test::write_subrecord(out, "ZNAM", znam.bytes);
        fo4::test::ByteWriter dnam;
        dnam.u32(0x840u);
        for (int pad = 0; pad < 32; ++pad) dnam.u32(0);
        fo4::test::write_subrecord(out, "DNAM", dnam.bytes);
        fo4::test::ByteWriter ksiz;
        ksiz.u32(static_cast<std::uint32_t>(gun.keywords.size()));
        fo4::test::write_subrecord(out, "KSIZ", ksiz.bytes);
        fo4::test::ByteWriter kwda;
        for (const auto keyword : gun.keywords) kwda.u32(keyword);
        fo4::test::write_subrecord(out, "KWDA", kwda.bytes);
        fo4::test::end_record(out, record);
    }
    fo4::test::end_group(out, weap);
    return out.bytes;
}

nlohmann::json batchArgs(const std::string& patch, const std::string& source, const std::string& type, const std::string& operations,
                         const std::string& filterField = {}, const std::string& filterValue = {}, nlohmann::json dryRun = nullptr,
                         nlohmann::json limit = nullptr) {
    return nlohmann::json::array({patch, source, type, operations, filterField, filterValue, std::move(dryRun), std::move(limit)});
}

const fo4::Subrecord* subrecordOf(const fo4::Record& record, std::string_view signature) {
    const auto wanted = fo4::Signature::from_string(signature);
    for (const auto& subrecord : record.subrecords) {
        if (subrecord.signature == wanted) return &subrecord;
    }
    return nullptr;
}

std::uint32_t u32At(const fo4::Subrecord& subrecord, std::size_t offset) {
    std::uint32_t value = 0;
    std::memcpy(&value, subrecord.data.data() + offset, sizeof(value));
    return value;
}

std::int16_t maxXOf(const fo4::Record& record) {
    const auto* bounds = subrecordOf(record, "OBND");
    REQUIRE(bounds != nullptr);
    std::int16_t value = 0;
    std::memcpy(&value, bounds->data.data() + 6, sizeof(value));
    return value;
}

std::vector<std::uint32_t> keywordsOf(const fo4::Record& record) {
    std::vector<std::uint32_t> values;
    const auto* list = subrecordOf(record, "KWDA");
    if (list == nullptr) return values;
    for (std::size_t offset = 0; offset + 4 <= list->data.size(); offset += 4) values.push_back(u32At(*list, offset));
    return values;
}

const fo4::Record* weapIn(const fo4::Plugin& plugin, std::uint32_t local) {
    return fo4::find_record(plugin, fo4::FormKey::parse(std::format("{:06X}", local) + ":Source.esp"));
}

bool listed(fo4ide::plugin::RadiclePluginBackend& backend, const std::string& name) {
    const auto active = parseStringResult(backend.invoke("backend.GetActivePlugins", nlohmann::json::array()));
    return std::any_of(active.begin(), active.end(), [&](const auto& row) { return row.at("Name") == name; });
}

const std::string kSetMaxX = R"([{"op":"set","field":"WEAP\\BoundMaxX","value":"99"}])";
std::vector<std::byte> topicChildrenSource() {
    fo4::test::ByteWriter out;
    writeHeader(out, 1.0f, 0, 0x900u);
    const auto dial = fo4::test::begin_group(out, "DIAL", 0);
    const auto topic = fo4::test::begin_record(out, "DIAL", 0, 0x800u, 131);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"Topic\0", 6});
    fo4::test::end_record(out, topic);
    const auto children = fo4::test::begin_group(out, std::string_view{"\x00\x08\x00\x00", 4}, 7);
    const auto info = fo4::test::begin_record(out, "INFO", 0, 0x801u, 131);
    fo4::test::ByteWriter enam;
    enam.u32(0);
    fo4::test::write_subrecord(out, "ENAM", enam.bytes);
    fo4::test::end_record(out, info);
    fo4::test::end_group(out, children);
    fo4::test::end_group(out, dial);
    return out.bytes;
}

const std::string kRemoveKwB = R"([{"op":"remove","field":"Keywords","value":"KwB"}])";

struct BatchFixture {
    std::filesystem::path root = tempRoot();
    Cleanup cleanup{root};
    std::filesystem::path source = root / "Source.esp";
    std::filesystem::path patch = root / "Patch.esp";
    std::filesystem::path official = root / "Fallout4.esm";
    fo4ide::plugin::RadiclePluginBackend backend;

    explicit BatchFixture(const std::vector<std::byte>& sourceBytes = batchSource()) {
        writeBytes(source, sourceBytes);
        writeBytes(patch, miscPlugin(1.0f, 0x801u, false, {0x800u}));
        writeBytes(official, miscPlugin(1.0f, 0x801u, false, {0x800u}));
        loadPlugins(backend, {{"Source.esp", source}, {"Fallout4.esm", official}, {"Patch.esp", patch}});
    }

    fo4ide::Result<nlohmann::json> run(const std::string& type, const std::string& operations, const std::string& filterField = {},
                                       const std::string& filterValue = {}, nlohmann::json dryRun = false, nlohmann::json limit = nullptr) {
        return backend.invoke("backend.BatchPatchRecords",
                              batchArgs("Patch.esp", "Source.esp", type, operations, filterField, filterValue, std::move(dryRun), std::move(limit)));
    }

    std::string error(const std::string& type, const std::string& operations, nlohmann::json dryRun = false) {
        const auto result = run(type, operations, {}, {}, std::move(dryRun));
        REQUIRE_FALSE(result.has_value());
        CHECK_FALSE(pending(backend, "Patch.esp"));
        return result.error().type;
    }
};
}

TEST_CASE("Oracle set localized flag sets and clears the header bit, is idempotent and persists on save") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, miscPlugin(1.0f, 0x801u, false, {0x800u}));
    const auto before = readFile(path);
    fo4ide::plugin::RadiclePluginBackend backend;
    loadPatch(backend, path);

    const auto set = textResult(backend.invoke("backend.SetLocalizedFlag", nlohmann::json::array({"Patch.esp", true})));
    CHECK(set.find("Set the Localized flag on 'Patch.esp'") == 0);
    CHECK(set.find("WARNING: no Strings folder") != std::string::npos);
    CHECK(pending(backend, "Patch.esp"));
    CHECK(readFile(path) == before);

    const auto again = textResult(backend.invoke("backend.SetLocalizedFlag", nlohmann::json::array({"Patch.esp", true})));
    CHECK(again == "'Patch.esp' already has the Localized flag set.");

    savePatch(backend);
    CHECK(diskLocalized(path));
    CHECK_FALSE(pending(backend, "Patch.esp"));

    const auto cleared = textResult(backend.invoke("backend.SetLocalizedFlag", nlohmann::json::array({"Patch.esp", false})));
    CHECK(cleared.find("Cleared the Localized flag on 'Patch.esp'") == 0);
    CHECK(diskLocalized(path));
    savePatch(backend);
    CHECK_FALSE(diskLocalized(path));

    const auto clearedAgain =
        textResult(backend.invoke("backend.SetLocalizedFlag", nlohmann::json::array({"Patch.esp", false})));
    CHECK(clearedAgain == "'Patch.esp' already does not have the Localized flag set.");
    CHECK_FALSE(pending(backend, "Patch.esp"));
}

TEST_CASE("Oracle set localized flag defaults to set and warns only when no Strings folder exists") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, miscPlugin(1.0f, 0x801u, false, {0x800u}));
    std::filesystem::create_directories(root / "Strings");
    fo4ide::plugin::RadiclePluginBackend backend;
    loadPatch(backend, path);

    const auto set = textResult(backend.invoke("backend.SetLocalizedFlag", nlohmann::json::array({"Patch.esp", nullptr})));
    CHECK(set == "Set the Localized flag on 'Patch.esp'. SavePlugin to persist.");
    savePatch(backend);
    CHECK(diskLocalized(path));
}

TEST_CASE("Oracle set localized flag refuses a missing, protected, changed or malformed request and stages nothing") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto patch = root / "Patch.esp";
    const auto official = root / "Fallout4.esm";
    writeBytes(patch, miscPlugin(1.0f, 0x801u, false, {0x800u}));
    writeBytes(official, miscPlugin(1.0f, 0x801u, false, {0x800u}));
    const auto patchBefore = readFile(patch);
    const auto officialBefore = readFile(official);
    fo4ide::plugin::RadiclePluginBackend backend;
    loadPlugins(backend, {{"Fallout4.esm", official}, {"Patch.esp", patch}});

    const auto missing = backend.invoke("backend.SetLocalizedFlag", nlohmann::json::array({"Nope.esp", true}));
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().type == "PluginNotLoaded");

    const auto empty = backend.invoke("backend.SetLocalizedFlag", nlohmann::json::array({"", true}));
    REQUIRE_FALSE(empty.has_value());
    CHECK(empty.error().type == "PluginMissing");

    const auto malformed = backend.invoke("backend.SetLocalizedFlag", nlohmann::json::array({"Patch.esp", "yes"}));
    REQUIRE_FALSE(malformed.has_value());
    CHECK(malformed.error().type == "InvalidLocalizedFlag");

    const auto protectedResult = backend.invoke("backend.SetLocalizedFlag", nlohmann::json::array({"Fallout4.esm", true}));
    REQUIRE_FALSE(protectedResult.has_value());
    CHECK(protectedResult.error().type == "PluginReadOnly");

    CHECK_FALSE(pending(backend, "Patch.esp"));
    CHECK_FALSE(pending(backend, "Fallout4.esm"));

    writeBytes(patch, miscPlugin(1.0f, 0x802u, false, {0x800u, 0x801u}));
    std::filesystem::last_write_time(patch, std::filesystem::last_write_time(patch) + std::chrono::seconds(2));
    const auto changed = backend.invoke("backend.SetLocalizedFlag", nlohmann::json::array({"Patch.esp", true}));
    REQUIRE_FALSE(changed.has_value());
    CHECK(changed.error().type == "PluginChangedOnDisk");
    CHECK_FALSE(pending(backend, "Patch.esp"));
    CHECK(readFile(official) == officialBefore);
    CHECK(readFile(patch) != patchBefore);
    CHECK_FALSE(diskLocalized(patch));
}

std::vector<std::byte> fullIdBytes() {
    fo4::test::ByteWriter id;
    id.u32(0x00000042u);
    return id.bytes;
}

std::string_view asText(const std::vector<std::byte>& bytes) {
    return std::string_view{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

std::string fullSubrecordText(const fo4::Plugin& plugin) {
    const auto* record = recordAt(plugin, 0x800u);
    REQUIRE(record != nullptr);
    for (const auto& subrecord : record->subrecords) {
        if (subrecord.signature != fo4::Signature::from_string("FULL")) continue;
        return std::string(reinterpret_cast<const char*>(subrecord.data.data()), subrecord.data.size());
    }
    FAIL("FULL missing");
}

TEST_CASE("Oracle set localized flag changes only the header bit and leaves translated fields byte-identical") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    const auto id = fullIdBytes();
    writeBytes(path, miscWithFull(0, asText(id)));
    fo4ide::plugin::RadiclePluginBackend backend;
    loadPatch(backend, path);

    REQUIRE(backend.invoke("backend.SetLocalizedFlag", nlohmann::json::array({"Patch.esp", true})).has_value());
    savePatch(backend);

    const auto reread = fo4::read_plugin_file(path);
    REQUIRE(reread.header.tes4_flags.is_localized());
    CHECK(fullSubrecordText(reread) == std::string(asText(id)));

    const auto inlinePath = root / "Inline.esp";
    writeBytes(inlinePath, miscWithFull(0x80u, std::string_view{"Hello\0", 6}));
    fo4ide::plugin::RadiclePluginBackend other;
    loadPlugins(other, {{"Patch.esp", inlinePath}});
    REQUIRE(other.invoke("backend.SetLocalizedFlag", nlohmann::json::array({"Patch.esp", false})).has_value());
    savePatch(other);
    CHECK_FALSE(diskLocalized(inlinePath));
    CHECK(fullSubrecordText(fo4::read_plugin_file(inlinePath)) == std::string("Hello\0", 6));
}

TEST_CASE("Oracle set localized flag refuses a plugin the game would misread and stages nothing") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto inlinePath = root / "Patch.esp";
    writeBytes(inlinePath, miscWithFull());
    const auto before = readFile(inlinePath);
    fo4ide::plugin::RadiclePluginBackend backend;
    loadPatch(backend, inlinePath);

    const auto setRefused = backend.invoke("backend.SetLocalizedFlag", nlohmann::json::array({"Patch.esp", true}));
    REQUIRE_FALSE(setRefused.has_value());
    CHECK(setRefused.error().type == "InlineTextPresent");
    CHECK_FALSE(pending(backend, "Patch.esp"));
    CHECK(readFile(inlinePath) == before);

    const auto idPath = root / "Ids.esp";
    writeBytes(idPath, miscWithFull(0x80u, asText(fullIdBytes())));
    fo4ide::plugin::RadiclePluginBackend localized;
    loadPlugins(localized, {{"Patch.esp", idPath}});
    const auto clearRefused = localized.invoke("backend.SetLocalizedFlag", nlohmann::json::array({"Patch.esp", false}));
    REQUIRE_FALSE(clearRefused.has_value());
    CHECK(clearRefused.error().type == "StringIdPresent");
    CHECK_FALSE(pending(localized, "Patch.esp"));
}

TEST_CASE("Oracle set localized flag is reachable through the backend target and the MCP catalog") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, miscPlugin(1.0f, 0x801u, false, {0x800u}));
    std::filesystem::create_directories(root / "Strings");
    fo4ide::plugin::RadiclePluginBackend backend;
    loadPatch(backend, path);

    fo4ide::app::PluginBackendTarget target(backend, "backend");
    const auto viaTarget = target.invoke("SetLocalizedFlag", nlohmann::json::array({"Patch.esp", true}));
    REQUIRE(viaTarget.has_value());
    CHECK(viaTarget->get<std::string>() == "Set the Localized flag on 'Patch.esp'. SavePlugin to persist.");

    McpHarness harness(backend, root);
    const auto viaMcp = harness.registry.call("set_localized_flag", {{"plugin", "Patch.esp"}, {"localized", false}});
    CHECK_FALSE(viaMcp.isError);
    CHECK(viaMcp.text.find("Cleared the Localized flag") == 0);

    const auto defaulted = harness.registry.call("set_localized_flag", {{"plugin", "Patch.esp"}});
    CHECK_FALSE(defaulted.isError);
    CHECK(defaulted.text.find("Set the Localized flag") == 0);
}

TEST_CASE("Oracle strip masters drops COBJ condition rows by parameter and reference, removes the unused master and remaps ids") {
    StripFixture fixture;
    const auto before = readFile(fixture.source);

    const auto result = textResult(fixture.backend.invoke(
        "backend.StripMastersClean", stripArgs(fixture.source, R"(["A.esp"])", fixture.output, false)));
    CHECK(result.find("Saved to") == 0);
    CHECK(result.find("dropped 2 condition(s) across 1 record(s)") != std::string::npos);
    CHECK(result.find("STILL PRESENT") == std::string::npos);
    CHECK(readFile(fixture.source) == before);

    const auto cleaned = fo4::read_plugin_file(fixture.output);
    CHECK(masterNames(cleaned) == std::vector<std::string>{"B.esp"});
    const auto* recipe = recordAt(cleaned, 0x800u);
    REQUIRE(recipe != nullptr);
    CHECK(ctdaCount(*recipe) == 3);
    CHECK(citcOf(*recipe) == 3);
    const auto* plain = recordAt(cleaned, 0x801u);
    REQUIRE(plain != nullptr);
    CHECK(ctdaCount(*plain) == 0);
    CHECK(formLinkOf(*plain, "CNAM") == 0x00000802u);

    const auto conditions = fo4::schema::read_root_conditions(*recipe);
    REQUIRE(conditions.ok);
    REQUIRE(conditions.conditions.size() == 3);
    CHECK(conditions.conditions[0].function == kHasPerk);
    CHECK(conditions.conditions[1].function == kGetIsSex);
    CHECK(conditions.conditions[1].param1 == 0x00000801);
    CHECK(conditions.conditions[2].function == kGetItemCount);
    CHECK(conditions.conditions[2].param1 == 0x01000800);
}

TEST_CASE("Oracle strip masters keeps a named master that remaining records still use and reports it") {
    StripFixture fixture;

    const auto result = textResult(fixture.backend.invoke(
        "backend.StripMastersClean", stripArgs(fixture.source, R"(["A.esp","B.esp"])", fixture.output, false)));
    CHECK(result.find("dropped 3 condition(s) across 1 record(s)") != std::string::npos);
    CHECK(result.find("STILL PRESENT (records reference them): B.esp.") != std::string::npos);

    const auto cleaned = fo4::read_plugin_file(fixture.output);
    CHECK(masterNames(cleaned) == std::vector<std::string>{"B.esp"});
}

TEST_CASE("Oracle strip masters dry run and a nothing-to-do request write nothing") {
    StripFixture fixture;
    const auto before = readFile(fixture.source);

    const auto dry = textResult(fixture.backend.invoke(
        "backend.StripMastersClean", stripArgs(fixture.source, R"(["A.esp"])", fixture.output, nullptr)));
    CHECK(dry.find("[DRY RUN]") == 0);
    CHECK(dry.find("2 condition(s) across 1 record(s)") != std::string::npos);
    CHECK(dry.find("Removable: A.esp.") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(fixture.output));

    const auto explicitDry = textResult(fixture.backend.invoke(
        "backend.StripMastersClean", stripArgs(fixture.source, R"(["A.esp"])", fixture.output, true)));
    CHECK(explicitDry == dry);

    const auto nothing = textResult(fixture.backend.invoke(
        "backend.StripMastersClean", stripArgs(fixture.source, R"(["Zzz.esp"])", fixture.output, false)));
    CHECK(nothing.find("None of the named masters are declared") != std::string::npos);
    CHECK(nothing.find("Nothing to do; file left untouched.") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(fixture.output));
    CHECK(readFile(fixture.source) == before);
}

TEST_CASE("Oracle strip masters refuses bad requests and leaves every file untouched") {
    StripFixture fixture;
    const auto before = readFile(fixture.source);
    writeBytes(fixture.output, {std::byte{1}, std::byte{2}});
    const auto outputBefore = readFile(fixture.output);
    const auto refuse = [&](const nlohmann::json& args, const std::string& type) {
        const auto result = fixture.backend.invoke("backend.StripMastersClean", args);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().type == type);
    };

    refuse(stripArgs(fixture.root / "Nope.esp", R"(["A.esp"])", fixture.output, false), "SourceNotFound");
    refuse(stripArgs("", R"(["A.esp"])", fixture.output, false), "SourceMissing");
    refuse(stripArgs(fixture.source, "[]", fixture.output, false), "MastersMissing");
    refuse(stripArgs(fixture.source, "", fixture.output, false), "MastersMissing");
    refuse(stripArgs(fixture.source, "not json", fixture.output, false), "InvalidMasters");
    refuse(stripArgs(fixture.source, R"({"a":1})", fixture.output, false), "InvalidMasters");
    refuse(stripArgs(fixture.source, "[1]", fixture.output, false), "InvalidMasters");
    refuse(stripArgs(fixture.source, R"(["A.esp"])", fixture.output, "yes"), "InvalidDryRunFlag");
    refuse(stripArgs(fixture.source, R"(["A.esp"])", "", false), "OutputMissing");
    refuse(stripArgs(fixture.source, R"(["A.esp"])", fixture.root / "notes.txt", false), "SavePathNotAllowed");
    refuse(stripArgs(fixture.source, R"(["A.esp"])", fixture.root.parent_path() / "elsewhere" / "Out.esp", false),
           "SavePathNotAllowed");
    const auto loadedBefore = readFile(fixture.root / "Patch.esp");
    refuse(stripArgs(fixture.source, R"(["A.esp"])", fixture.root / "Patch.esp", false), "OutputIsLoadedPlugin");
    CHECK(readFile(fixture.root / "Patch.esp") == loadedBefore);

    CHECK(readFile(fixture.source) == before);
    CHECK(readFile(fixture.output) == outputBefore);
    CHECK_FALSE(std::filesystem::exists(fixture.root / "notes.txt"));
}

TEST_CASE("Oracle strip masters refuses a malformed source and keeps the existing output file") {
    StripFixture fixture;
    writeBytes(fixture.root / "Broken.esp", {std::byte{'T'}, std::byte{'E'}, std::byte{'S'}, std::byte{'4'}, std::byte{9}});
    writeBytes(fixture.output, recipePlugin());
    const auto outputBefore = readFile(fixture.output);

    const auto result = fixture.backend.invoke(
        "backend.StripMastersClean", stripArgs(fixture.root / "Broken.esp", R"(["A.esp"])", fixture.output, false));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "StripMastersFailed");
    CHECK(readFile(fixture.output) == outputBefore);
}

TEST_CASE("Oracle strip masters can overwrite its own source and leaves loaded plugins alone") {
    StripFixture fixture;

    REQUIRE(fixture.backend.invoke("backend.StripMastersClean",
                                   stripArgs(fixture.source, R"(["A.esp"])", fixture.source, false)).has_value());
    CHECK(masterNames(fo4::read_plugin_file(fixture.source)) == std::vector<std::string>{"B.esp"});
    CHECK_FALSE(pending(fixture.backend, "Patch.esp"));
}

TEST_CASE("Oracle strip masters refuses a protected output name with the protected-name reason") {
    StripFixture fixture;
    for (const char* name : {"Evil.esp.exe", "Clean.esp:stream"}) {
        const auto result = fixture.backend.invoke(
            "backend.StripMastersClean", stripArgs(fixture.source, R"(["A.esp"])", fixture.root / name, false));
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().type == "SavePathNotAllowed");
        CHECK(result.error().message.find("Refusing to write ") == 0);
        CHECK((result.error().message.find("not a writable file type") != std::string::npos ||
               result.error().message.find("alternate data stream") != std::string::npos));
        CHECK_FALSE(std::filesystem::exists(fixture.root / name));
    }
}

TEST_CASE("Oracle strip masters refuses an incomplete master scan and writes nothing") {
    StripFixture fixture;
    writeBytes(fixture.source, brokenVmadPlugin());
    const auto before = readFile(fixture.source);

    const auto result = fixture.backend.invoke(
        "backend.StripMastersClean", stripArgs(fixture.source, R"(["A.esp"])", fixture.output, false));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "MasterUsageIncomplete");
    CHECK(result.error().message.find("nothing was written") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(fixture.output));
    CHECK(readFile(fixture.source) == before);
}

TEST_CASE("Oracle strip masters refuses a COBJ whose conditions cannot be handled losslessly and writes nothing") {
    StripFixture fixture;
    writeBytes(fixture.source, splitConditionsPlugin());
    const auto before = readFile(fixture.source);

    const auto result = fixture.backend.invoke(
        "backend.StripMastersClean", stripArgs(fixture.source, R"(["A.esp"])", fixture.output, false));
    REQUIRE_FALSE(result.has_value());
    CHECK((result.error().type == "ConditionReadFailed" || result.error().type == "ConditionWriteFailed"));
    CHECK(result.error().message.find("820") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(fixture.output));
    CHECK(readFile(fixture.source) == before);
}

TEST_CASE("Oracle strip masters leaves conditions on non-COBJ records untouched and keeps the master they use") {
    StripFixture fixture;
    writeBytes(fixture.source, otherRecordConditionsPlugin());

    const auto result = textResult(fixture.backend.invoke(
        "backend.StripMastersClean", stripArgs(fixture.source, R"(["A.esp"])", fixture.output, false)));
    CHECK(result.find("dropped 2 condition(s) across 1 record(s)") != std::string::npos);
    CHECK(result.find("STILL PRESENT (records reference them): A.esp.") != std::string::npos);

    const auto cleaned = fo4::read_plugin_file(fixture.output);
    CHECK(masterNames(cleaned) == std::vector<std::string>{"A.esp", "B.esp"});
    for (const auto local : {0x830u, 0x831u}) {
        const auto* record = recordAt(cleaned, local);
        REQUIRE(record != nullptr);
        CHECK(ctdaCount(*record) == 1);
    }
}

TEST_CASE("Oracle strip masters matches a condition that names the stripped master only in its second parameter") {
    StripFixture fixture;
    writeBytes(fixture.source, param2Plugin());

    const auto result = textResult(fixture.backend.invoke(
        "backend.StripMastersClean", stripArgs(fixture.source, R"(["A.esp"])", fixture.output, false)));
    CHECK(result.find("dropped 1 condition(s) across 1 record(s)") != std::string::npos);

    const auto cleaned = fo4::read_plugin_file(fixture.output);
    CHECK(masterNames(cleaned).empty());
    const auto* record = recordAt(cleaned, 0x800u);
    REQUIRE(record != nullptr);
    CHECK(ctdaCount(*record) == 1);
    CHECK(citcOf(*record) == 1);
    const auto conditions = fo4::schema::read_root_conditions(*record);
    REQUIRE(conditions.ok);
    REQUIRE(conditions.conditions.size() == 1);
    CHECK(conditions.conditions[0].function == kGetItemCount);
}

TEST_CASE("Oracle strip masters is reachable through the backend target and the MCP catalog with a default dry run") {
    StripFixture fixture;

    fo4ide::app::PluginBackendTarget target(fixture.backend, "backend");
    const auto viaTarget = target.invoke("StripMastersClean", stripArgs(fixture.source, R"(["A.esp"])", fixture.output, nullptr));
    REQUIRE(viaTarget.has_value());
    CHECK(viaTarget->get<std::string>().find("[DRY RUN]") == 0);

    McpHarness harness(fixture.backend, fixture.root);
    const auto dry = harness.registry.call("strip_masters_clean", {{"source_path", fixture.source.string()},
                                                                   {"masters", R"(["A.esp"])"},
                                                                   {"output_path", fixture.output.string()}});
    CHECK_FALSE(dry.isError);
    CHECK(dry.text.find("[DRY RUN]") == 0);
    CHECK_FALSE(std::filesystem::exists(fixture.output));

    const auto dryWithoutOutput = harness.registry.call(
        "strip_masters_clean", {{"source_path", fixture.source.string()}, {"masters", R"(["A.esp"])"}});
    CHECK_FALSE(dryWithoutOutput.isError);
    CHECK(dryWithoutOutput.text.find("[DRY RUN]") == 0);
    CHECK_FALSE(std::filesystem::exists(fixture.output));

    const auto wrote = harness.registry.call("strip_masters_clean", {{"source_path", fixture.source.string()},
                                                                     {"masters", R"(["A.esp"])"},
                                                                     {"output_path", fixture.output.string()},
                                                                     {"dry_run", false}});
    CHECK_FALSE(wrote.isError);
    CHECK(wrote.text.find("Saved to") == 0);
    CHECK(masterNames(fo4::read_plugin_file(fixture.output)) == std::vector<std::string>{"B.esp"});
}

TEST_CASE("Oracle batch patch dry run lists the matches, honours the filter and limit and stages nothing") {
    BatchFixture fixture;
    const auto before = readFile(fixture.patch);

    const auto all = textResult(fixture.backend.invoke(
        "backend.BatchPatchRecords", batchArgs("Patch.esp", "Source.esp", "WEAP", kSetMaxX)));
    CHECK(all.find("DRY RUN -- 3 of 3 WEAP records in 'Source.esp' match (no filter):") == 0);
    CHECK(all.find("Alpha [000810:Source.esp]") != std::string::npos);
    CHECK(all.find("set WEAP\\BoundMaxX = \"99\"") != std::string::npos);
    CHECK(all.find("dry_run=false to write 3 records into 'Patch.esp'") != std::string::npos);

    const auto byName = textResult(fixture.run("weap", kSetMaxX, "EditorID", "ALPH", true));
    CHECK(byName.find("DRY RUN -- 1 of 3 WEAP records in 'Source.esp' match (EditorID contains ALPH):") == 0);
    CHECK(byName.find("Beta") == std::string::npos);

    const auto byScalar = textResult(fixture.run("WEAP", kSetMaxX, "WEAP\\BoundMaxX", "20", true));
    CHECK(byScalar.find("1 of 3") != std::string::npos);
    CHECK(byScalar.find("Beta [000811:Source.esp]") != std::string::npos);

    const auto byList = textResult(fixture.run("WEAP", kSetMaxX, "Keywords", "000802", true));
    CHECK(byList.find("2 of 3") != std::string::npos);
    CHECK(byList.find("Gamma") == std::string::npos);

    const auto limited = textResult(fixture.run("WEAP", kSetMaxX, {}, {}, true, 1));
    CHECK(limited.find("1 of 3 WEAP") != std::string::npos);
    CHECK(limited.find("Alpha") != std::string::npos);
    CHECK(limited.find("Beta") == std::string::npos);

    CHECK(textResult(fixture.run("WEAP", kSetMaxX, "EditorID", "zzz", true)).find("No WEAP records in 'Source.esp' match the filter") == 0);
    CHECK(textResult(fixture.run("COBJ", "", {}, {}, true)).find("No records of type 'COBJ' found in 'Source.esp'") == 0);
    CHECK(textResult(fixture.run("WEAP", "", {}, {}, true)).find("DRY RUN -- 3 of 3") == 0);

    CHECK_FALSE(pending(fixture.backend, "Patch.esp"));
    CHECK(readFile(fixture.patch) == before);
}

TEST_CASE("Oracle batch patch sets scalars and FormLinks, adds and removes list entries and persists on save") {
    BatchFixture fixture;
    const std::string operations =
        R"([{"op":"set","field":"WEAP\\BoundMaxX","value":"99"},{"field":"WEAP\\AimModel","value":"000831:Source.esp"},)"
        R"({"op":"remove","field":"Keywords","value":"KwA"},{"op":"add","field":"Keywords","value":"000803:Source.esp"}])";
    const auto before = readFile(fixture.patch);

    const auto text = textResult(fixture.run("WEAP", operations, "EditorID", "Alpha"));
    CHECK(text == "Batch patched 1 of 1 records into 'Patch.esp' (4 field op(s) applied, 1 new override(s)). SavePlugin to persist.");
    CHECK(pending(fixture.backend, "Patch.esp"));
    CHECK(readFile(fixture.patch) == before);

    savePatch(fixture.backend);
    const auto disk = fo4::read_plugin_file(fixture.patch);
    const auto* alpha = weapIn(disk, 0x810u);
    REQUIRE(alpha != nullptr);
    CHECK(maxXOf(*alpha) == 99);
    CHECK(u32At(*subrecordOf(*alpha, "WAMD"), 0) == 0x831u);
    CHECK(keywordsOf(*alpha) == std::vector<std::uint32_t>{0x802u, 0x803u});
    CHECK(u32At(*subrecordOf(*alpha, "KSIZ"), 0) == 2u);
    CHECK(weapIn(disk, 0x811u) == nullptr);
    CHECK(weapIn(disk, 0x812u) == nullptr);
    CHECK(recordAt(disk, 0x800u) != nullptr);
    CHECK(masterNames(disk) == std::vector<std::string>{"Source.esp"});
}

TEST_CASE("Oracle batch patch edits an override already in the patch in place and reports a no-op") {
    BatchFixture fixture;
    REQUIRE(fixture.run("WEAP", kSetMaxX, "EditorID", "Alpha").has_value());
    savePatch(fixture.backend);

    const auto second = textResult(fixture.run("WEAP", R"([{"field":"WEAP\\BoundMaxX","value":"5"}])"));
    CHECK(second == "Batch patched 3 of 3 records into 'Patch.esp' (3 field op(s) applied, 2 new override(s)). SavePlugin to persist.");
    savePatch(fixture.backend);
    const auto disk = fo4::read_plugin_file(fixture.patch);
    for (const auto local : {0x810u, 0x811u, 0x812u}) {
        const auto* record = weapIn(disk, local);
        REQUIRE(record != nullptr);
        CHECK(maxXOf(*record) == 5);
    }

    const auto same = textResult(fixture.run("WEAP", R"([{"field":"WEAP\\BoundMaxX","value":"5"}])"));
    CHECK(same == "No change: every matched record in 'Patch.esp' already has these values.");
    CHECK_FALSE(pending(fixture.backend, "Patch.esp"));
}

TEST_CASE("Oracle batch patch is all-or-nothing: one failing record stages nothing") {
    BatchFixture fixture;
    const auto before = readFile(fixture.patch);

    const auto failed = fixture.run("WEAP", kRemoveKwB);
    REQUIRE_FALSE(failed.has_value());
    CHECK(failed.error().type == "BatchPatchFailed");
    CHECK(failed.error().message.find("1 of 3 record(s) failed; nothing was staged.") == 0);
    CHECK(failed.error().message.find("Gamma [000812:Source.esp]: no entry matching KwB found in Keywords") != std::string::npos);
    CHECK_FALSE(pending(fixture.backend, "Patch.esp"));

    const auto badValue = fixture.run("WEAP", R"([{"field":"WEAP\\BoundMaxX","value":"not a number"}])");
    REQUIRE_FALSE(badValue.has_value());
    CHECK(badValue.error().type == "BatchPatchFailed");
    CHECK_FALSE(pending(fixture.backend, "Patch.esp"));

    const auto next = textResult(fixture.run("WEAP", kSetMaxX, "EditorID", "Alpha"));
    CHECK(next.find("(1 field op(s) applied, 1 new override(s))") != std::string::npos);
    savePatch(fixture.backend);
    CHECK(readFile(fixture.patch) != before);
    CHECK(weapIn(fo4::read_plugin_file(fixture.patch), 0x811u) == nullptr);
}

TEST_CASE("Oracle batch patch refuses bespoke fields, bad operations and bad requests with typed errors") {
    BatchFixture fixture;

    CHECK(fixture.error("ARMO", R"([{"field":"ARMO\\Value","value":"1"}])", true) == "BatchFieldRefused");
    CHECK(fixture.error("EYES", R"([{"field":"EYES\\Full","value":"x"}])", true) == "BatchFieldRefused");
    CHECK(fixture.error("LVSP", R"([{"field":"LVSP\\ChanceNone","value":"1"}])", true) == "BatchFieldRefused");
    CHECK(fixture.error("WEAP", R"([{"field":"EDID","value":"X"}])", true) == "BatchFieldRefused");
    CHECK(fixture.error("WEAP", R"([{"field":"WEAP\\Nope","value":"1"}])", true) == "BatchFieldUnsupported");
    CHECK(fixture.error("WEAP", R"([{"field":"AMMO\\BoundMaxX","value":"1"}])", true) == "BatchFieldRecordMismatch");
    CHECK(fixture.error("WEAP", R"([{"op":"mul","field":"WEAP\\BoundMaxX","value":"1"}])", true) == "InvalidOperations");
    CHECK(fixture.error("WEAP", R"([{"value":"1"}])", true) == "InvalidOperations");
    CHECK(fixture.error("WEAP", R"([{"field":"WEAP\\BoundMaxX","value":1}])", true) == "InvalidOperations");
    CHECK(fixture.error("WEAP", R"({"op":"set"})", true) == "InvalidOperations");
    CHECK(fixture.error("WEAP", "[", true) == "InvalidOperations");
    CHECK(fixture.error("WEAP", R"([{"op":"add","field":"WEAP\\BoundMaxX","value":"000801:Source.esp"}])", true) == "ListFieldUnsupported");
    CHECK(fixture.error("WEAP", R"([{"op":"add","field":"Keywords","value":""}])", true) == "ListValueMissing");
    CHECK(fixture.error("WEAP", R"([{"op":"add","field":"Keywords","value":"NoSuchKeyword"}])", true) == "ListValueUnresolved");
    CHECK(fixture.error("WEAP", R"([{"op":"add","field":"Keywords","value":"000830:Source.esp"}])", true) == "FormLinkTargetTypeMismatch");
    CHECK(fixture.error("WEAP", R"([{"field":"WEAP\\AimModel","value":"NULL"}])", true) == "FormLinkNullNotAllowed");
    CHECK(fixture.error("WEAP", R"([{"field":"WEAP\\AimModel","value":"000801:Source.esp"}])", true) == "FormLinkTargetTypeMismatch");
    CHECK(fixture.error("WEAP", "", false) == "OperationsMissing");
    CHECK(fixture.error("WE", kSetMaxX, true) == "InvalidRecordType");
    CHECK(fixture.error("WEAP", kSetMaxX, "yes") == "InvalidDryRunFlag");

    const auto badFilter = fixture.run("WEAP", kSetMaxX, "WEAP\\Nope", "1", true);
    REQUIRE_FALSE(badFilter.has_value());
    CHECK(badFilter.error().type == "FilterFieldUnknown");
    for (const auto& bad : {nlohmann::json(0), nlohmann::json(-3), nlohmann::json("5")}) {
        const auto result = fixture.run("WEAP", kSetMaxX, {}, {}, true, bad);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().type == "InvalidLimit");
    }

    const auto ghost = fixture.backend.invoke("backend.BatchPatchRecords", batchArgs("Ghost.esp", "Source.esp", "WEAP", kSetMaxX, {}, {}, false));
    REQUIRE_FALSE(ghost.has_value());
    CHECK(ghost.error().type == "TargetPluginNotLoaded");
    const auto noSource = fixture.backend.invoke("backend.BatchPatchRecords", batchArgs("Patch.esp", "Nope.esp", "WEAP", kSetMaxX, {}, {}, false));
    REQUIRE_FALSE(noSource.has_value());
    CHECK(noSource.error().type == "SourcePluginNotLoaded");
    const auto same = fixture.backend.invoke("backend.BatchPatchRecords", batchArgs("source.esp", "Source.esp", "WEAP", kSetMaxX, {}, {}, false));
    REQUIRE_FALSE(same.has_value());
    CHECK(same.error().type == "PatchPluginIsSource");
    const auto readOnly = fixture.backend.invoke("backend.BatchPatchRecords", batchArgs("Fallout4.esm", "Source.esp", "WEAP", kSetMaxX, {}, {}, false));
    REQUIRE_FALSE(readOnly.has_value());
    CHECK(readOnly.error().type == "PluginReadOnly");
    CHECK_FALSE(pending(fixture.backend, "Fallout4.esm"));

    writeBytes(fixture.patch, miscPlugin(1.0f, 0x802u, false, {0x800u, 0x801u}));
    std::filesystem::last_write_time(fixture.patch, std::filesystem::last_write_time(fixture.patch) + std::chrono::seconds(2));
    const auto changed = fixture.run("WEAP", kSetMaxX);
    REQUIRE_FALSE(changed.has_value());
    CHECK(changed.error().type == "PluginChangedOnDisk");
    CHECK_FALSE(pending(fixture.backend, "Patch.esp"));
}

TEST_CASE("Oracle batch patch through the backend target auto-creates the patch plugin and rolls it back on failure") {
    BatchFixture fixture;
    const auto created = fixture.root / "overwrite";
    REQUIRE(fixture.backend.setTrustedNewPluginRoot(created).has_value());
    fo4ide::app::PluginBackendTarget target(fixture.backend, "backend");

    const auto dry = target.invoke("BatchPatchRecords", batchArgs("Auto", "Source.esp", "WEAP", kSetMaxX, {}, {}, nullptr));
    REQUIRE(dry.has_value());
    CHECK(dry->get<std::string>().find("DRY RUN -- 3 of 3") == 0);
    CHECK_FALSE(listed(fixture.backend, "Auto.esp"));
    CHECK_FALSE(std::filesystem::exists(created / "Auto.esp"));

    const auto failed = target.invoke("BatchPatchRecords", batchArgs("Auto", "Source.esp", "WEAP", kRemoveKwB, {}, {}, false));
    REQUIRE_FALSE(failed.has_value());
    CHECK(failed.error().type == "BatchPatchFailed");
    CHECK_FALSE(listed(fixture.backend, "Auto.esp"));
    CHECK_FALSE(std::filesystem::exists(created / "Auto.esp"));

    const auto noMatch = target.invoke("BatchPatchRecords", batchArgs("Auto", "Source.esp", "WEAP", kSetMaxX, "EditorID", "zzz", false));
    REQUIRE(noMatch.has_value());
    CHECK(noMatch->get<std::string>().find("No WEAP records") == 0);
    CHECK_FALSE(listed(fixture.backend, "Auto.esp"));

    const auto wrote = target.invoke("BatchPatchRecords", batchArgs("Auto", "Source.esp", "WEAP", kSetMaxX, {}, {}, false));
    REQUIRE(wrote.has_value());
    CHECK(wrote->get<std::string>().find("Batch patched 3 of 3 records into 'Auto.esp'") == 0);
    CHECK(listed(fixture.backend, "Auto.esp"));
    CHECK(pending(fixture.backend, "Auto.esp"));
    REQUIRE(fixture.backend.invoke("backend.SavePlugin", nlohmann::json::array({"Auto.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(created / "Auto.esp");
    REQUIRE(weapIn(disk, 0x812u) != nullptr);
    CHECK(maxXOf(*weapIn(disk, 0x812u)) == 99);

    const auto again = target.invoke("BatchPatchRecords", batchArgs("Auto.esp", "Source.esp", "WEAP", kSetMaxX, {}, {}, false));
    REQUIRE(again.has_value());
    CHECK(again->get<std::string>().find("No change") == 0);
    CHECK(listed(fixture.backend, "Auto.esp"));
}

TEST_CASE("Oracle batch patch is reachable through the MCP catalog with a default dry run") {
    BatchFixture fixture;
    McpHarness harness(fixture.backend, fixture.root);

    const auto dry = harness.registry.call("batch_patch_records", {{"patch_plugin", "Patch.esp"},
                                                                   {"source_plugin", "Source.esp"},
                                                                   {"type", "WEAP"},
                                                                   {"operations", kSetMaxX}});
    CHECK_FALSE(dry.isError);
    CHECK(dry.text.find("DRY RUN -- 3 of 3 WEAP") == 0);
    CHECK_FALSE(pending(fixture.backend, "Patch.esp"));

    const auto wrote = harness.registry.call("batch_patch_records", {{"patch_plugin", "Patch.esp"},
                                                                     {"source_plugin", "Source.esp"},
                                                                     {"type", "WEAP"},
                                                                     {"operations", kSetMaxX},
                                                                     {"filter_field", "EditorID"},
                                                                     {"filter_value", "Beta"},
                                                                     {"dry_run", false},
                                                                     {"limit", 10}});
    CHECK_FALSE(wrote.isError);
    CHECK(wrote.text.find("Batch patched 1 of 1 records into 'Patch.esp'") == 0);
    CHECK(pending(fixture.backend, "Patch.esp"));

    const auto refused = harness.registry.call("batch_patch_records", {{"patch_plugin", "Patch.esp"},
                                                                       {"source_plugin", "Source.esp"},
                                                                       {"type", "ARMO"},
                                                                       {"operations", R"([{"field":"ARMO\\Value","value":"1"}])"}});
    CHECK(refused.isError);
}

namespace {

std::vector<std::byte> gmstPlugin(std::uint32_t flags, std::string_view editorId, std::string_view data) {
    fo4::test::ByteWriter out;
    writeHeader(out, 1.0f, flags, 0x801u);
    const auto group = fo4::test::begin_group(out, "GMST", 0);
    const auto record = fo4::test::begin_record(out, "GMST", 0, 0x800u, 131);
    fo4::test::write_subrecord(out, "EDID", editorId);
    fo4::test::write_subrecord(out, "DATA", data);
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    return out.bytes;
}

std::vector<std::byte> malformedKeywordSource() {
    fo4::test::ByteWriter out;
    writeHeader(out, 1.0f, 0, 0x900u);
    const auto weap = fo4::test::begin_group(out, "WEAP", 0);
    for (const auto& [id, name, keywords] : {std::tuple{0x810u, std::string_view{"Good\0", 5}, std::size_t{4}},
                                             std::tuple{0x811u, std::string_view{"Broken\0", 7}, std::size_t{5}}}) {
        const auto record = fo4::test::begin_record(out, "WEAP", 0, id, 131);
        fo4::test::write_subrecord(out, "EDID", name);
        fo4::test::ByteWriter bounds;
        for (const auto value : {std::uint16_t{0}, std::uint16_t{0}, std::uint16_t{0}, std::uint16_t{30}, std::uint16_t{0}, std::uint16_t{0}}) {
            bounds.u16(value);
        }
        fo4::test::write_subrecord(out, "OBND", bounds.bytes);
        fo4::test::ByteWriter ksiz;
        ksiz.u32(1);
        fo4::test::write_subrecord(out, "KSIZ", ksiz.bytes);
        fo4::test::ByteWriter kwda;
        for (std::size_t byte = 0; byte < keywords; ++byte) kwda.u8(byte == 0 ? 1 : 0);
        fo4::test::write_subrecord(out, "KWDA", kwda.bytes);
        fo4::test::end_record(out, record);
    }
    fo4::test::end_group(out, weap);
    return out.bytes;
}

}

TEST_CASE("Oracle set localized flag reads four-byte inline text as inline and checks scalar-flagged string rows") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    const auto refusal = [&](const std::vector<std::byte>& bytes, nlohmann::json flag) {
        writeBytes(path, bytes);
        fo4ide::plugin::RadiclePluginBackend backend;
        loadPatch(backend, path);
        const auto result = backend.invoke("backend.SetLocalizedFlag", nlohmann::json::array({"Patch.esp", flag}));
        if (result.has_value()) return std::string{};
        CHECK_FALSE(pending(backend, "Patch.esp"));
        return result.error().type;
    };

    CHECK(refusal(miscWithFull(0, std::string_view{"Axe\0", 4}), true) == "InlineTextPresent");
    CHECK(refusal(miscWithFull(0, std::string_view{"A\0", 2}), true) == "InlineTextPresent");
    CHECK(refusal(miscWithFull(0, asText(fullIdBytes())), true).empty());

    CHECK(refusal(gmstPlugin(0, std::string_view{"sName\0", 6}, std::string_view{"Hello\0", 6}), true) == "InlineTextPresent");
    CHECK(refusal(gmstPlugin(0x80u, std::string_view{"sName\0", 6}, asText(fullIdBytes())), false) == "StringIdPresent");
    CHECK(refusal(gmstPlugin(0, std::string_view{"fSpeed\0", 7}, std::string_view{"\x00\x00\x80\x3F", 4}), true).empty());
    CHECK(refusal(gmstPlugin(0x80u, std::string_view{"fSpeed\0", 7}, std::string_view{"\x00\x00\x80\x3F", 4}), false).empty());
}

TEST_CASE("Oracle batch patch sets a nullable FormLink to NULL and a FormLink by EditorID") {
    BatchFixture fixture;
    const auto before = readFile(fixture.patch);

    const auto nulled = textResult(fixture.run("WEAP", R"([{"field":"WEAP\\Ammo","value":"NULL"}])", "EditorID", "Alpha"));
    CHECK(nulled == "Batch patched 1 of 1 records into 'Patch.esp' (1 field op(s) applied, 1 new override(s)). SavePlugin to persist.");
    const auto byName = textResult(fixture.run("WEAP", R"([{"field":"WEAP\\Ammo","value":"AmmoB"}])", "EditorID", "Beta"));
    CHECK(byName.find("Batch patched 1 of 1 records") == 0);
    const auto emptied = textResult(fixture.run("WEAP", R"([{"field":"WEAP\\Ammo","value":""}])", "EditorID", "Gamma"));
    CHECK(emptied.find("Batch patched 1 of 1 records") == 0);
    CHECK(readFile(fixture.patch) == before);

    savePatch(fixture.backend);
    const auto disk = fo4::read_plugin_file(fixture.patch);
    REQUIRE(weapIn(disk, 0x810u) != nullptr);
    REQUIRE(weapIn(disk, 0x811u) != nullptr);
    REQUIRE(weapIn(disk, 0x812u) != nullptr);
    CHECK(u32At(*subrecordOf(*weapIn(disk, 0x810u), "DNAM"), 0) == 0u);
    CHECK(u32At(*subrecordOf(*weapIn(disk, 0x811u), "DNAM"), 0) == 0x841u);
    CHECK(u32At(*subrecordOf(*weapIn(disk, 0x812u), "DNAM"), 0) == 0u);

    CHECK(fixture.error("WEAP", R"([{"field":"WEAP\\Ammo","value":"AmmoNope"}])") == "ListValueUnresolved");
}

TEST_CASE("Oracle batch patch reports a record that needs nested source containment and stages nothing") {
    BatchFixture fixture(topicChildrenSource());
    const auto before = readFile(fixture.patch);

    const auto dry = textResult(fixture.run("INFO", R"([{"field":"INFO\\Flags","value":"1"}])", {}, {}, true));
    CHECK(dry.find("DRY RUN -- 1 of 1 INFO") == 0);

    const auto failed = fixture.run("INFO", R"([{"field":"INFO\\Flags","value":"1"}])");
    REQUIRE_FALSE(failed.has_value());
    CHECK(failed.error().type == "BatchPatchFailed");
    CHECK(failed.error().message.find("1 of 1 record(s) failed; nothing was staged.") == 0);
    CHECK(failed.error().message.find("requires nested source GRUP containment") != std::string::npos);
    CHECK_FALSE(pending(fixture.backend, "Patch.esp"));
    CHECK(readFile(fixture.patch) == before);
}

TEST_CASE("Oracle batch patch treats a record whose fields cannot be read as a non-match instead of aborting the run") {
    BatchFixture fixture(malformedKeywordSource());

    const auto text = textResult(fixture.run("WEAP", kSetMaxX, "WEAP\\BoundMaxX", "30", true));
    CHECK(text.find("DRY RUN -- 1 of 2 WEAP records in 'Source.esp' match (WEAP\\BoundMaxX contains 30):") == 0);
    CHECK(text.find("Good [000810:Source.esp]") != std::string::npos);
    CHECK(text.find("Broken") == std::string::npos);
}

TEST_CASE("Oracle batch patch through the backend target previews by default before any patch plugin exists") {
    BatchFixture fixture;
    fo4ide::app::PluginBackendTarget target(fixture.backend, "backend");

    const auto dry = target.invoke("BatchPatchRecords", batchArgs("Fresh", "Source.esp", "WEAP", kSetMaxX));
    REQUIRE(dry.has_value());
    CHECK(dry->get<std::string>().find("DRY RUN -- 3 of 3") == 0);
    CHECK_FALSE(listed(fixture.backend, "Fresh.esp"));
    CHECK_FALSE(std::filesystem::exists(fixture.root / "Fresh.esp"));
}
