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
#include <fo4ide/plugin/IPluginBackend.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = fo4ide_test::uniqueTempName();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_element_" + id);
}

struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

std::vector<std::byte> pluginBytes() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(3);
    hedr.u32(0x803);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto colors = fo4::test::begin_group(out, "CLFM", 0);
    auto color = fo4::test::begin_record(out, "CLFM", 0, 0x00000800, 131);
    fo4::test::end_record(out, color);
    color = fo4::test::begin_record(out, "CLFM", 0, 0x00000801, 131);
    fo4::test::end_record(out, color);
    fo4::test::end_group(out, colors);

    const auto races = fo4::test::begin_group(out, "RACE", 0);
    const auto race = fo4::test::begin_record(out, "RACE", 0, 0x00000802, 131);
    fo4::test::ByteWriter first;
    first.u32(0x00000800);
    fo4::test::write_subrecord(out, "AHCM", first.bytes);
    fo4::test::ByteWriter second;
    second.u32(0x00000801);
    fo4::test::write_subrecord(out, "AHCM", second.bytes);
    fo4::test::end_record(out, race);
    fo4::test::end_group(out, races);
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
            {"source", "ElementTest"},
            {"resolved", true}
        }})}});
    REQUIRE(result.has_value());
}

nlohmann::json parsed(const fo4ide::Result<nlohmann::json>& result) {
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    return nlohmann::json::parse(result->get<std::string>());
}

std::vector<std::uint32_t> rawLinks(const std::filesystem::path& path) {
    const auto plugin = fo4::read_plugin_file(path);
    const auto* race = fo4::find_record(
        plugin, fo4::FormKey::parse("000802:Patch.esp"));
    REQUIRE(race != nullptr);
    const auto signature = fo4::Signature::from_string("AHCM");
    std::vector<std::uint32_t> values;
    for (const auto& subrecord : race->subrecords) {
        if (subrecord.signature != signature) continue;
        REQUIRE(subrecord.data.size() == 4);
        std::uint32_t raw = 0;
        std::memcpy(&raw, subrecord.data.data(), sizeof(raw));
        values.push_back(raw);
    }
    return values;
}

}  // namespace

TEST_CASE("Native ElementService handles the current raw conflict-matrix FormLink path") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, pluginBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    fo4ide::plugin::IPluginBackend& elementBackend = backend;

    const auto actions = parsed(elementBackend.describeElement(nlohmann::json::array({
        "Patch.esp", "000802:Patch.esp", "Subrecords\\AHCM[0]"
    })));
    CHECK(actions.at("count") == 2);
    CHECK(actions.at("canAdd") == true);
    CHECK(actions.at("canRemove") == true);
    CHECK(actions.at("canMoveUp") == false);
    CHECK(actions.at("canMoveDown") == true);
    REQUIRE(actions.at("templates").size() == 1);
    CHECK(actions.at("templates")[0] == "FormLink<CLFM>");

    const auto added = elementBackend.addElement(nlohmann::json::array({
        "Patch.esp", "000802:Patch.esp", "Subrecords\\AHCM[0]", "FormLink<CLFM>"
    }));
    REQUIRE(added.has_value());
    CHECK(rawLinks(path) == std::vector<std::uint32_t>{0x00000800u, 0x00000801u});

    const auto moved = elementBackend.moveElement(nlohmann::json::array({
        "Patch.esp", "000802:Patch.esp", "Subrecords\\AHCM[0]", 1
    }));
    REQUIRE(moved.has_value());
    CHECK(rawLinks(path) == std::vector<std::uint32_t>{0x00000800u, 0x00000801u});

    const auto removed = elementBackend.removeElement(nlohmann::json::array({
        "Patch.esp", "000802:Patch.esp", "Subrecords\\AHCM[1]"
    }));
    REQUIRE(removed.has_value());
    CHECK(rawLinks(path) == std::vector<std::uint32_t>{0x00000800u, 0x00000801u});

    const auto cleared = elementBackend.clearElement(nlohmann::json::array({
        "Patch.esp", "000802:Patch.esp", "RACE\\MaleHairColors"
    }));
    REQUIRE(cleared.has_value());
    CHECK(rawLinks(path) == std::vector<std::uint32_t>{0x00000800u, 0x00000801u});

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    CHECK(rawLinks(path).empty());
}

TEST_CASE("Native ElementService fails closed for unregistered raw subrecords") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, pluginBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    fo4ide::plugin::IPluginBackend& elementBackend = backend;

    const auto actions = parsed(elementBackend.describeElement(nlohmann::json::array({
        "Patch.esp", "000802:Patch.esp", "Subrecords\\EDID[0]"
    })));
    REQUIRE(actions.contains("error"));
    CHECK(actions.at("error").get<std::string>().find("not registered") != std::string::npos);
}
