#include <chrono>
#include "../helpers/TempRoot.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/plugin/reader.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = fo4ide_test::uniqueTempName();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_list_item_" + id);
}

struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

// A MISC target, a KYWD target with an EditorID (to prove EditorID-valued adds),
// an empty FLST and a populated FLST.
std::vector<std::byte> pluginBytes() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(4);
    hedr.u32(0x812);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto miscGroup = fo4::test::begin_group(out, "MISC", 0);
    const auto misc = fo4::test::begin_record(out, "MISC", 0, 0x00000800, 131);
    fo4::test::end_record(out, misc);
    fo4::test::end_group(out, miscGroup);

    const auto kywdGroup = fo4::test::begin_group(out, "KYWD", 0);
    const auto kywd = fo4::test::begin_record(out, "KYWD", 0, 0x00000801, 131);
    fo4::test::write_subrecord(out, "EDID", std::string_view("MyKeyword"));
    fo4::test::end_record(out, kywd);
    fo4::test::end_group(out, kywdGroup);

    const auto flstGroup = fo4::test::begin_group(out, "FLST", 0);
    auto empty = fo4::test::begin_record(out, "FLST", 0, 0x00000810, 131);
    fo4::test::write_subrecord(out, "EDID", std::string_view("EmptyList"));
    fo4::test::end_record(out, empty);
    auto full = fo4::test::begin_record(out, "FLST", 0, 0x00000811, 131);
    fo4::test::write_subrecord(out, "EDID", std::string_view("FullList"));
    fo4::test::ByteWriter link;
    link.u32(0x00000800);
    fo4::test::write_subrecord(out, "LNAM", link.bytes);
    fo4::test::end_record(out, full);
    fo4::test::end_group(out, flstGroup);
    return out.bytes;
}

std::vector<std::byte> otherModBytes() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(0x801);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto kywdGroup = fo4::test::begin_group(out, "KYWD", 0);
    const auto kywd = fo4::test::begin_record(out, "KYWD", 0, 0x00000800, 131);
    fo4::test::write_subrecord(out, "EDID", std::string_view("ExternalKwd"));
    fo4::test::end_record(out, kywd);
    fo4::test::end_group(out, kywdGroup);
    return out.bytes;
}

std::vector<std::byte> patchWithEmptyFlst() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(0x810);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto flstGroup = fo4::test::begin_group(out, "FLST", 0);
    auto flst = fo4::test::begin_record(out, "FLST", 0, 0x00000810, 131);
    fo4::test::write_subrecord(out, "EDID", std::string_view("PatchList"));
    fo4::test::end_record(out, flst);
    fo4::test::end_group(out, flstGroup);
    return out.bytes;
}

std::vector<std::byte> keywordPluginBytes() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(5);
    hedr.u32(0x902);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto miscGroup = fo4::test::begin_group(out, "MISC", 0);
    const auto misc = fo4::test::begin_record(out, "MISC", 0, 0x00000800, 131);
    fo4::test::end_record(out, misc);
    fo4::test::end_group(out, miscGroup);

    const auto kywdGroup = fo4::test::begin_group(out, "KYWD", 0);
    for (const std::uint32_t id : {0x00000801u, 0x00000802u}) {
        const auto kywd = fo4::test::begin_record(out, "KYWD", 0, id, 131);
        fo4::test::end_record(out, kywd);
    }
    fo4::test::end_group(out, kywdGroup);

    const auto weapGroup = fo4::test::begin_group(out, "WEAP", 0);
    auto full = fo4::test::begin_record(out, "WEAP", 0, 0x00000900, 131);
    fo4::test::write_subrecord(out, "EDID", std::string_view("KeyedGun"));
    fo4::test::ByteWriter znam;
    znam.u32(0);
    fo4::test::write_subrecord(out, "ZNAM", znam.bytes);
    fo4::test::ByteWriter ksiz;
    ksiz.u32(1);
    fo4::test::write_subrecord(out, "KSIZ", ksiz.bytes);
    fo4::test::ByteWriter kwda;
    kwda.u32(0x00000801);
    fo4::test::write_subrecord(out, "KWDA", kwda.bytes);
    fo4::test::end_record(out, full);

    auto empty = fo4::test::begin_record(out, "WEAP", 0, 0x00000901, 131);
    fo4::test::write_subrecord(out, "EDID", std::string_view("BareGun"));
    fo4::test::ByteWriter znam2;
    znam2.u32(0);
    fo4::test::write_subrecord(out, "ZNAM", znam2.bytes);
    fo4::test::end_record(out, empty);
    fo4::test::end_group(out, weapGroup);
    return out.bytes;
}

void writeBytes(const std::filesystem::path& path,
                const std::vector<std::byte>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    REQUIRE(stream.good());
}

void load(fo4ide::plugin::RadiclePluginBackend& backend,
          const std::filesystem::path& path) {
    const auto result = backend.invoke(
        "environment.LoadMo2",
        nlohmann::json{{"plugins", nlohmann::json::array({{
            {"name", "Patch.esp"},
            {"path", path.string()},
            {"source", "ListItemTest"},
            {"resolved", true}
        }})}});
    REQUIRE(result.has_value());
}

std::size_t countSubrecords(const fo4::Record& record, std::string_view signature) {
    const auto sig = fo4::Signature::from_string(signature);
    std::size_t count = 0;
    for (const auto& subrecord : record.subrecords) {
        if (subrecord.signature == sig) ++count;
    }
    return count;
}

std::uint32_t nthRaw(const fo4::Record& record,
                     std::string_view signature,
                     std::size_t wanted) {
    const auto sig = fo4::Signature::from_string(signature);
    std::size_t ordinal = 0;
    const fo4::Subrecord* found = nullptr;
    for (const auto& subrecord : record.subrecords) {
        if (subrecord.signature != sig) continue;
        if (ordinal++ != wanted) continue;
        found = &subrecord;
        break;
    }
    if (found == nullptr) FAIL("missing requested subrecord ordinal");
    REQUIRE(found->data.size() == 4u);
    std::uint32_t raw = 0;
    std::memcpy(&raw, found->data.data(), sizeof(raw));
    return raw;
}

std::uint32_t ksizValue(const fo4::Record& record) {
    const auto sig = fo4::Signature::from_string("KSIZ");
    const fo4::Subrecord* found = nullptr;
    for (const auto& subrecord : record.subrecords) {
        if (subrecord.signature != sig) continue;
        found = &subrecord;
        break;
    }
    if (found == nullptr) FAIL("no KSIZ subrecord");
    REQUIRE(found->data.size() == 4u);
    std::uint32_t value = 0;
    std::memcpy(&value, found->data.data(), sizeof(value));
    return value;
}

std::vector<std::uint32_t> kwdaEntries(const fo4::Record& record) {
    const auto sig = fo4::Signature::from_string("KWDA");
    std::vector<std::uint32_t> out;
    for (const auto& subrecord : record.subrecords) {
        if (subrecord.signature != sig) continue;
        for (std::size_t offset = 0; offset + 4 <= subrecord.data.size(); offset += 4) {
            std::uint32_t value = 0;
            std::memcpy(&value, subrecord.data.data() + offset, sizeof(value));
            out.push_back(value);
        }
    }
    return out;
}

}  // namespace

TEST_CASE("PluginEditTool add_list_item appends a FormLink to an empty FLST") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, pluginBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto result = backend.invokeBackend(
        "AddListItem",
        nlohmann::json::array({
            "Patch.esp", "000810:Patch.esp", "Items", "000800:Patch.esp"}));
    REQUIRE(result.has_value());
    CHECK(result->get<std::string>() ==
          "Added 000800:Patch.esp to Items on 000810:Patch.esp in Patch.esp (now 1 item(s)).");

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(path);
    const auto* flst = fo4::find_record(disk, fo4::FormKey::parse("000810:Patch.esp"));
    REQUIRE(flst != nullptr);
    REQUIRE(countSubrecords(*flst, "LNAM") == 1u);
    CHECK(nthRaw(*flst, "LNAM", 0) == 0x00000800u);
}

TEST_CASE("PluginEditTool add_list_item appends to a populated FLST") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, pluginBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto result = backend.invokeBackend(
        "AddListItem",
        nlohmann::json::array({
            "Patch.esp", "000811:Patch.esp", "Items", "000801:Patch.esp"}));
    REQUIRE(result.has_value());
    CHECK(result->get<std::string>() ==
          "Added 000801:Patch.esp to Items on 000811:Patch.esp in Patch.esp (now 2 item(s)).");

    const auto beforeSave = fo4::read_plugin_file(path);
    const auto* beforeSaveFlst = fo4::find_record(
        beforeSave, fo4::FormKey::parse("000811:Patch.esp"));
    REQUIRE(beforeSaveFlst != nullptr);
    REQUIRE(countSubrecords(*beforeSaveFlst, "LNAM") == 1u);
    CHECK(nthRaw(*beforeSaveFlst, "LNAM", 0) == 0x00000800u);

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(path);
    const auto* flst = fo4::find_record(disk, fo4::FormKey::parse("000811:Patch.esp"));
    REQUIRE(flst != nullptr);
    REQUIRE(countSubrecords(*flst, "LNAM") == 2u);
    CHECK(nthRaw(*flst, "LNAM", 0) == 0x00000800u);
    CHECK(nthRaw(*flst, "LNAM", 1) == 0x00000801u);
}

TEST_CASE("PluginEditTool add_list_item resolves an EditorID value") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, pluginBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto result = backend.invokeBackend(
        "AddListItem",
        nlohmann::json::array({
            "Patch.esp", "000810:Patch.esp", "Items", "MyKeyword"}));
    REQUIRE(result.has_value());
    CHECK(result->get<std::string>() ==
          "Added MyKeyword to Items on 000810:Patch.esp in Patch.esp (now 1 item(s)).");

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(path);
    const auto* flst = fo4::find_record(disk, fo4::FormKey::parse("000810:Patch.esp"));
    REQUIRE(flst != nullptr);
    CHECK(nthRaw(*flst, "LNAM", 0) == 0x00000801u);
}

TEST_CASE("PluginEditTool add_list_item matches the field name case-insensitively") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, pluginBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto result = backend.invokeBackend(
        "AddListItem",
        nlohmann::json::array({
            "Patch.esp", "000810:Patch.esp", "items", "000800:Patch.esp"}));
    REQUIRE(result.has_value());
    CHECK(result->get<std::string>() ==
          "Added 000800:Patch.esp to items on 000810:Patch.esp in Patch.esp (now 1 item(s)).");

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(path);
    const auto* flst = fo4::find_record(disk, fo4::FormKey::parse("000810:Patch.esp"));
    REQUIRE(flst != nullptr);
    CHECK(countSubrecords(*flst, "LNAM") == 1u);
}

TEST_CASE("PluginEditTool remove_list_item matches the field name case-insensitively") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, pluginBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto result = backend.invokeBackend(
        "RemoveListItem",
        nlohmann::json::array({
            "Patch.esp", "000811:Patch.esp", "ITEMS", "000800:Patch.esp"}));
    REQUIRE(result.has_value());
    CHECK(result->get<std::string>().find("Written to disk") == std::string::npos);
    CHECK(result->get<std::string>().find("SavePlugin") != std::string::npos);

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(path);
    const auto* flst = fo4::find_record(disk, fo4::FormKey::parse("000811:Patch.esp"));
    REQUIRE(flst != nullptr);
    CHECK(countSubrecords(*flst, "LNAM") == 0u);
}

TEST_CASE("PluginEditTool remove_list_item drops the matching FormLink") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, pluginBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto result = backend.invokeBackend(
        "RemoveListItem",
        nlohmann::json::array({
            "Patch.esp", "000811:Patch.esp", "Items", "000800:Patch.esp"}));
    REQUIRE(result.has_value());
    CHECK(result->get<std::string>() ==
          "Removed 1 entr(y/ies) matching 000800:Patch.esp from Items (now 0) on "
          "000811:Patch.esp. SavePlugin to persist.");

    const auto beforeSave = fo4::read_plugin_file(path);
    const auto* beforeSaveFlst = fo4::find_record(
        beforeSave, fo4::FormKey::parse("000811:Patch.esp"));
    REQUIRE(beforeSaveFlst != nullptr);
    CHECK(countSubrecords(*beforeSaveFlst, "LNAM") == 1u);

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(path);
    const auto* flst = fo4::find_record(disk, fo4::FormKey::parse("000811:Patch.esp"));
    REQUIRE(flst != nullptr);
    CHECK(countSubrecords(*flst, "LNAM") == 0u);
}

TEST_CASE("PluginEditTool remove_list_item reports a missing value") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, pluginBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto result = backend.invokeBackend(
        "RemoveListItem",
        nlohmann::json::array({
            "Patch.esp", "000811:Patch.esp", "Items", "000801:Patch.esp"}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "ListItemNotFound");
}

TEST_CASE("PluginEditTool add_list_item rejects an unsupported field") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, pluginBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto result = backend.invokeBackend(
        "AddListItem",
        nlohmann::json::array({
            "Patch.esp", "000811:Patch.esp", "Nonsense", "000800:Patch.esp"}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "ListFieldUnsupported");
}

TEST_CASE("PluginEditTool add_list_item declares an external target's owner master") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto otherPath = root / "OtherMod.esp";
    const auto patchPath = root / "Patch.esp";
    writeBytes(otherPath, otherModBytes());
    writeBytes(patchPath, patchWithEmptyFlst());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke(
        "environment.LoadMo2",
        nlohmann::json{{"plugins", nlohmann::json::array({
            {{"name", "OtherMod.esp"}, {"path", otherPath.string()},
             {"source", "Other"}, {"resolved", true}},
            {{"name", "Patch.esp"}, {"path", patchPath.string()},
             {"source", "ListItemTest"}, {"resolved", true}}
        })}}).has_value());

    const auto result = backend.invokeBackend(
        "AddListItem",
        nlohmann::json::array({
            "Patch.esp", "000810:Patch.esp", "Items", "000800:OtherMod.esp"}));
    REQUIRE(result.has_value());
    CHECK(result->get<std::string>() ==
          "Added 000800:OtherMod.esp to Items on 000810:Patch.esp in Patch.esp (now 1 item(s)).");

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(patchPath);
    bool declaresOtherMod = false;
    for (const auto& master : disk.header.masters) {
        if (master.key.filename() == "OtherMod.esp") declaresOtherMod = true;
    }
    CHECK(declaresOtherMod);
    const auto* flst = fo4::find_record(disk, fo4::FormKey::parse("000810:Patch.esp"));
    REQUIRE(flst != nullptr);
    REQUIRE(countSubrecords(*flst, "LNAM") == 1u);
    CHECK(nthRaw(*flst, "LNAM", 0) == 0x00000800u);
}

TEST_CASE("PluginEditTool add_list_item appends a keyword into a packed KWDA array") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, keywordPluginBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto result = backend.invokeBackend(
        "AddListItem",
        nlohmann::json::array({
            "Patch.esp", "000900:Patch.esp", "Keywords", "000802:Patch.esp"}));
    REQUIRE(result.has_value());
    CHECK(result->get<std::string>() ==
          "Added 000802:Patch.esp to Keywords on 000900:Patch.esp in Patch.esp (now 2 item(s)).");

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(path);
    const auto* weap = fo4::find_record(disk, fo4::FormKey::parse("000900:Patch.esp"));
    REQUIRE(weap != nullptr);
    CHECK(countSubrecords(*weap, "KWDA") == 1u);
    CHECK(ksizValue(*weap) == 2u);
    const auto entries = kwdaEntries(*weap);
    REQUIRE(entries.size() == 2u);
    CHECK(entries[0] == 0x00000801u);
    CHECK(entries[1] == 0x00000802u);
}

TEST_CASE("PluginEditTool add_list_item creates the KSIZ/KWDA block when a record has no keywords") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, keywordPluginBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto result = backend.invokeBackend(
        "AddListItem",
        nlohmann::json::array({
            "Patch.esp", "000901:Patch.esp", "Keywords", "000801:Patch.esp"}));
    REQUIRE(result.has_value());
    CHECK(result->get<std::string>() ==
          "Added 000801:Patch.esp to Keywords on 000901:Patch.esp in Patch.esp (now 1 item(s)).");

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(path);
    const auto* weap = fo4::find_record(disk, fo4::FormKey::parse("000901:Patch.esp"));
    REQUIRE(weap != nullptr);
    CHECK(ksizValue(*weap) == 1u);
    const auto entries = kwdaEntries(*weap);
    REQUIRE(entries.size() == 1u);
    CHECK(entries[0] == 0x00000801u);
}

TEST_CASE("PluginEditTool remove_list_item drops the last keyword and its KSIZ/KWDA block") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, keywordPluginBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto result = backend.invokeBackend(
        "RemoveListItem",
        nlohmann::json::array({
            "Patch.esp", "000900:Patch.esp", "Keywords", "000801:Patch.esp"}));
    REQUIRE(result.has_value());
    CHECK(result->get<std::string>() ==
          "Removed 1 entr(y/ies) matching 000801:Patch.esp from Keywords (now 0) on "
          "000900:Patch.esp. SavePlugin to persist.");

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(path);
    const auto* weap = fo4::find_record(disk, fo4::FormKey::parse("000900:Patch.esp"));
    REQUIRE(weap != nullptr);
    CHECK(countSubrecords(*weap, "KWDA") == 0u);
    CHECK(countSubrecords(*weap, "KSIZ") == 0u);
}

TEST_CASE("PluginEditTool add_list_item rejects a non-keyword target for Keywords") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, keywordPluginBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto result = backend.invokeBackend(
        "AddListItem",
        nlohmann::json::array({
            "Patch.esp", "000900:Patch.esp", "Keywords", "000800:Patch.esp"}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "FormLinkTargetTypeMismatch");
}
