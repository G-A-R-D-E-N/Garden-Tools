#include <chrono>
#include "../helpers/TempRoot.hpp"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/forms/components.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = fo4ide_test::uniqueTempName();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_components_" + id);
}

struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

std::vector<std::byte> componentPluginBytes() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(7);
    hedr.u32(0xF00);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto cmpoGroup = fo4::test::begin_group(out, "CMPO", 0);
    for (const std::uint32_t id : {0x00000801u, 0x00000802u}) {
        const auto cmpo = fo4::test::begin_record(out, "CMPO", 0, id, 131);
        fo4::test::end_record(out, cmpo);
    }
    fo4::test::end_group(out, cmpoGroup);

    const auto miscGroup = fo4::test::begin_group(out, "MISC", 0);
    auto misc = fo4::test::begin_record(out, "MISC", 0, 0x00000900, 131);
    fo4::test::write_subrecord(out, "EDID", std::string_view("Scrap"));
    fo4::test::ByteWriter data;
    data.u32(10);
    data.f32(1.0f);
    fo4::test::write_subrecord(out, "DATA", data.bytes);
    fo4::test::end_record(out, misc);
    fo4::test::end_group(out, miscGroup);

    const auto cobjGroup = fo4::test::begin_group(out, "COBJ", 0);
    auto cobj = fo4::test::begin_record(out, "COBJ", 0, 0x00000A00, 131);
    fo4::test::write_subrecord(out, "EDID", std::string_view("Recipe"));
    fo4::test::ByteWriter znam;
    znam.u32(0);
    fo4::test::write_subrecord(out, "ZNAM", znam.bytes);
    fo4::test::end_record(out, cobj);

    auto cobj2 = fo4::test::begin_record(out, "COBJ", 0, 0x00000E00, 131);
    fo4::test::write_subrecord(out, "EDID", std::string_view("SubRecipe"));
    fo4::test::end_record(out, cobj2);
    fo4::test::end_group(out, cobjGroup);

    const auto weapGroup = fo4::test::begin_group(out, "WEAP", 0);
    auto weap = fo4::test::begin_record(out, "WEAP", 0, 0x00000B00, 131);
    fo4::test::write_subrecord(out, "EDID", std::string_view("Gun"));
    fo4::test::end_record(out, weap);
    fo4::test::end_group(out, weapGroup);

    const auto qustGroup = fo4::test::begin_group(out, "QUST", 0);
    auto qust = fo4::test::begin_record(out, "QUST", 0, 0x00000C00, 131);
    fo4::test::write_subrecord(out, "EDID", std::string_view("Quest"));
    fo4::test::end_record(out, qust);
    fo4::test::end_group(out, qustGroup);
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
            {"source", "ComponentsTest"},
            {"resolved", true}
        }})}});
    REQUIRE(result.has_value());
}

const fo4::Subrecord* findSub(const fo4::Record& record, std::string_view signature) {
    const auto sig = fo4::Signature::from_string(signature);
    for (const auto& subrecord : record.subrecords) {
        if (subrecord.signature == sig) return &subrecord;
    }
    return nullptr;
}

bool isPending(fo4ide::plugin::RadiclePluginBackend& backend) {
    const auto active = backend.invoke("backend.GetActivePlugins", nlohmann::json::array());
    REQUIRE(active.has_value());
    return nlohmann::json::parse(active->get<std::string>()).at(0).at("Pending").get<bool>();
}

std::vector<std::byte> seedComponents(fo4ide::plugin::RadiclePluginBackend& backend,
                                      const std::filesystem::path& path,
                                      std::string_view record) {
    REQUIRE(backend.invokeBackend(
        "SetComponents",
        nlohmann::json::array({"Patch.esp", std::string(record),
                               R"([{"component":"000801:Patch.esp","count":5}])"})).has_value());
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    return fo4::test::original_bytes(fo4::read_plugin_file(path));
}

std::vector<fo4::forms::ComponentEntry> seededList() {
    return {{fo4::FormKey::parse("000801:Patch.esp"), 5}};
}

std::vector<fo4::forms::ComponentEntry> savedComponents(
    fo4ide::plugin::RadiclePluginBackend& backend, const std::filesystem::path& path,
    std::string_view record) {
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    return fo4::forms::decode_components(fo4::read_plugin_file(path),
                                         fo4::FormKey::parse(record));
}

}  // namespace

TEST_CASE("PluginEditTool set_components writes CVPA and identity CDIX on a MISC") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, componentPluginBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto result = backend.invokeBackend(
        "SetComponents",
        nlohmann::json::array({
            "Patch.esp", "000900:Patch.esp",
            R"([{"component":"000801:Patch.esp","count":5},{"component":"000802:Patch.esp","count":2}])"}));
    REQUIRE(result.has_value());
    CHECK(result->get<std::string>() ==
          "Set 2 component(s) on 000900:Patch.esp in Patch.esp. SavePlugin to persist.");

    const auto beforeSave = fo4::read_plugin_file(path);
    const auto* beforeSaveMisc = fo4::find_record(
        beforeSave, fo4::FormKey::parse("000900:Patch.esp"));
    REQUIRE(beforeSaveMisc != nullptr);
    CHECK(findSub(*beforeSaveMisc, "CVPA") == nullptr);
    CHECK(findSub(*beforeSaveMisc, "CDIX") == nullptr);

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(path);
    const auto* misc = fo4::find_record(disk, fo4::FormKey::parse("000900:Patch.esp"));
    REQUIRE(misc != nullptr);
    const auto* cvpa = findSub(*misc, "CVPA");
    const auto* cdix = findSub(*misc, "CDIX");
    REQUIRE(cvpa != nullptr);
    REQUIRE(cdix != nullptr);
    CHECK(cvpa->data.size() == 16u);
    CHECK(cdix->data.size() == 2u);
}

TEST_CASE("PluginEditTool set_components writes FVPA and no CDIX on a COBJ") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, componentPluginBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto result = backend.invokeBackend(
        "SetComponents",
        nlohmann::json::array({
            "Patch.esp", "000A00:Patch.esp",
            R"([{"component":"000801:Patch.esp","count":1}])"}));
    REQUIRE(result.has_value());
    CHECK(result->get<std::string>().find("Written to disk") == std::string::npos);
    CHECK(result->get<std::string>().find("SavePlugin") != std::string::npos);

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(path);
    const auto* cobj = fo4::find_record(disk, fo4::FormKey::parse("000A00:Patch.esp"));
    REQUIRE(cobj != nullptr);
    CHECK(findSub(*cobj, "FVPA") != nullptr);
    CHECK(findSub(*cobj, "CDIX") == nullptr);
}

TEST_CASE("PluginEditTool set_components refuses a non-CMPO component on a MISC") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, componentPluginBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto before = seedComponents(backend, path, "000900:Patch.esp");
    const auto result = backend.invokeBackend(
        "SetComponents",
        nlohmann::json::array({
            "Patch.esp", "000900:Patch.esp",
            R"([{"component":"000801:Patch.esp","count":1},{"component":"000B00:Patch.esp","count":1}])"}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "ComponentEntriesRefused");
    CHECK(result.error().message ==
          "Refused: 1 of 2 component entries failed: #2: 000B00:Patch.esp: WEAP is not a CMPO component.");

    CHECK_FALSE(isPending(backend));
    CHECK(fo4::test::original_bytes(fo4::read_plugin_file(path)) == before);
    const auto saved = savedComponents(backend, path, "000900:Patch.esp");
    CHECK(saved == seededList());
}

TEST_CASE("PluginEditTool set_components refuses an empty components string and keeps the list") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, componentPluginBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto before = seedComponents(backend, path, "000900:Patch.esp");
    const auto result = backend.invokeBackend(
        "SetComponents", nlohmann::json::array({"Patch.esp", "000900:Patch.esp", ""}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "InvalidComponentsJson");
    CHECK(result.error().message == "components JSON is empty; pass [] to clear");

    CHECK_FALSE(isPending(backend));
    CHECK(fo4::test::original_bytes(fo4::read_plugin_file(path)) == before);
    const auto saved = savedComponents(backend, path, "000900:Patch.esp");
    CHECK(saved == seededList());
}

TEST_CASE("PluginEditTool set_components clears CVPA and CDIX on an empty list") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, componentPluginBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    REQUIRE(backend.invokeBackend(
        "SetComponents",
        nlohmann::json::array({
            "Patch.esp", "000900:Patch.esp",
            R"([{"component":"000801:Patch.esp","count":1}])"})).has_value());
    const auto cleared = backend.invokeBackend(
        "SetComponents",
        nlohmann::json::array({"Patch.esp", "000900:Patch.esp", "[]"}));
    REQUIRE(cleared.has_value());
    CHECK(cleared->get<std::string>() ==
          "Set 0 component(s) on 000900:Patch.esp in Patch.esp. SavePlugin to persist.");

    const auto disk = fo4::read_plugin_file(path);
    const auto* misc = fo4::find_record(disk, fo4::FormKey::parse("000900:Patch.esp"));
    REQUIRE(misc != nullptr);
    CHECK(findSub(*misc, "CVPA") == nullptr);
    CHECK(findSub(*misc, "CDIX") == nullptr);
}

TEST_CASE("PluginEditTool set_components rejects a record that has no components") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, componentPluginBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto result = backend.invokeBackend(
        "SetComponents",
        nlohmann::json::array({
            "Patch.esp", "000B00:Patch.esp",
            R"([{"component":"000801:Patch.esp","count":1}])"}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "ComponentsUnsupported");
}

TEST_CASE("PluginEditTool set_components accepts broad item types on a COBJ") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, componentPluginBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto result = backend.invokeBackend(
        "SetComponents",
        nlohmann::json::array({
            "Patch.esp", "000A00:Patch.esp",
            R"([{"component":"000B00:Patch.esp","count":1},{"component":"000900:Patch.esp","count":2}])"}));
    REQUIRE(result.has_value());
    CHECK(result->get<std::string>() ==
          "Set 2 component(s) on 000A00:Patch.esp in Patch.esp. SavePlugin to persist.");

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(path);
    const auto* cobj = fo4::find_record(disk, fo4::FormKey::parse("000A00:Patch.esp"));
    REQUIRE(cobj != nullptr);
    const auto* fvpa = findSub(*cobj, "FVPA");
    REQUIRE(fvpa != nullptr);
    CHECK(fvpa->data.size() == 16u);
}

TEST_CASE("PluginEditTool set_components refuses a non-item component on a COBJ") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, componentPluginBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto before = seedComponents(backend, path, "000A00:Patch.esp");
    const auto result = backend.invokeBackend(
        "SetComponents",
        nlohmann::json::array({
            "Patch.esp", "000A00:Patch.esp",
            R"([{"component":"000B00:Patch.esp","count":1},{"component":"000C00:Patch.esp","count":1}])"}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "ComponentEntriesRefused");
    CHECK(result.error().message ==
          "Refused: 1 of 2 component entries failed: #2: 000C00:Patch.esp: QUST is not a valid recipe item.");

    CHECK_FALSE(isPending(backend));
    CHECK(fo4::test::original_bytes(fo4::read_plugin_file(path)) == before);
    const auto saved = savedComponents(backend, path, "000A00:Patch.esp");
    CHECK(saved == seededList());
}

TEST_CASE("PluginEditTool set_components refuses negative and oversized counts") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, componentPluginBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto before = seedComponents(backend, path, "000900:Patch.esp");
    const auto result = backend.invokeBackend(
        "SetComponents",
        nlohmann::json::array({
            "Patch.esp", "000900:Patch.esp",
            R"([{"component":"000801:Patch.esp","count":-1},{"component":"000802:Patch.esp","count":4294967296}])"}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "ComponentEntriesRefused");
    CHECK(result.error().message ==
          "Refused: 2 of 2 component entries failed: #1: 000801:Patch.esp: count must be "
          "1..4294967295; #2: 000802:Patch.esp: count must be 1..4294967295.");

    CHECK_FALSE(isPending(backend));
    CHECK(fo4::test::original_bytes(fo4::read_plugin_file(path)) == before);
    const auto saved = savedComponents(backend, path, "000900:Patch.esp");
    CHECK(saved == seededList());
}

TEST_CASE("PluginEditTool set_components accepts a COBJ as a recipe component") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, componentPluginBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto result = backend.invokeBackend(
        "SetComponents",
        nlohmann::json::array({
            "Patch.esp", "000A00:Patch.esp",
            R"([{"component":"000E00:Patch.esp","count":1}])"}));
    REQUIRE(result.has_value());
    CHECK(result->get<std::string>() ==
          "Set 1 component(s) on 000A00:Patch.esp in Patch.esp. SavePlugin to persist.");

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(path);
    const auto* cobj = fo4::find_record(disk, fo4::FormKey::parse("000A00:Patch.esp"));
    REQUIRE(cobj != nullptr);
    CHECK(findSub(*cobj, "FVPA") != nullptr);
}
