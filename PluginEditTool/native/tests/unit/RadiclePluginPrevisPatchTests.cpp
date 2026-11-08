#include <catch2/catch_test_macros.hpp>

#include <fstream>

#include <fo4/plugin/reader.hpp>
#include <fo4/previs/cdx.hpp>
#include <fo4/signature.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "../helpers/TempRoot.hpp"
#include "plugin_bytes.hpp"

namespace {

void writeBytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
}

std::vector<std::byte> readBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    REQUIRE(stream.good());
    stream.seekg(0, std::ios::end);
    const auto size = stream.tellg();
    REQUIRE(size > 0);
    stream.seekg(0, std::ios::beg);
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    stream.read(reinterpret_cast<char*>(bytes.data()), size);
    REQUIRE(stream.good());
    return bytes;
}

std::vector<std::byte> makeCellSource() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(0);
    hedr.u32(0x801);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto group = fo4::test::begin_group(out, "CELL", 0);
    const auto record = fo4::test::begin_record(out, "CELL", 0, 0x00000801);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"ExtCell\0", 8});
    fo4::test::ByteWriter data;
    data.u8(0x01);
    data.u8(0x00);
    fo4::test::write_subrecord(out, "DATA", data.bytes);
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    return out.bytes;
}

std::vector<std::byte> makeEmptyPatch() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(0);
    hedr.u32(0x801);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);
    const auto group = fo4::test::begin_group(out, "CELL", 0);
    fo4::test::end_group(out, group);
    return out.bytes;
}

nlohmann::json pluginEntry(std::string name, const std::filesystem::path& path) {
    return {{"name", std::move(name)},
            {"path", path.string()},
            {"source", "test"},
            {"resolved", true}};
}

}  // namespace

// Regression: native stamp must write PCMB/VISI/XCRI on a patch CELL override without Creation Kit.
TEST_CASE("Native StampCellPrevis writes previs subrecords on patch override") {
    fo4ide_test::TempRoot root{"fo4ide_previs_patch"};
    const auto sourcePath = root.path() / "Source.esp";
    const auto patchPath = root.path() / "Patch.esp";
    writeBytes(sourcePath, makeCellSource());
    writeBytes(patchPath, makeEmptyPatch());

    auto source = fo4::read_plugin_file(sourcePath);
    fo4::assign_record_form_keys(source);
    std::string cellText;
    for (const auto* rec : fo4::collect_records(source)) {
        if (rec->header.signature == fo4::Signature::from_string("CELL")) {
            cellText = rec->form_key.to_string();
            break;
        }
    }
    REQUIRE(!cellText.empty());

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins",
         nlohmann::json::array({pluginEntry("Source.esp", sourcePath), pluginEntry("Patch.esp", patchPath)})}};
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const auto stamped = backend.invokeBackend(
        "StampCellPrevis",
        nlohmann::json::array({std::string("Patch.esp"), cellText, std::string("Source.esp"), "0x1234", "0x5678",
                               nlohmann::json::object({{"mesh_ids", nlohmann::json::array({1})},
                                                       {"refs", nlohmann::json::array({nlohmann::json::array({0x55, 1})})}}),
                               true}));
    if (!stamped.has_value()) {
        FAIL(stamped.error().type + ": " + stamped.error().message);
    }
    REQUIRE(backend.invokeBackend("SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    auto plugin = fo4::read_plugin_file(patchPath);
    fo4::assign_record_form_keys(plugin);
    const auto pcmb = fo4::Signature::from_string("PCMB");
    bool hasPcmb = false;
    for (const auto* rec : fo4::collect_records(plugin)) {
        if (rec->header.signature != fo4::Signature::from_string("CELL")) continue;
        for (const auto& sub : rec->subrecords) {
            if (sub.signature == pcmb) hasPcmb = true;
        }
    }
    CHECK(hasPcmb);
}

// Regression: container writers must be reachable from the native backend without the Creation Kit.
TEST_CASE("Native WritePrevisContainer round-trips a CDX file") {
    fo4ide_test::TempRoot root{"fo4ide_previs_container"};
    fo4::previs::CdxFile file;
    file.rows.push_back({0x00000080u, 0x00000800u, 2u, 3u});
    const auto sourceBytes = fo4::previs::write_cdx(file);
    const auto sourcePath = root.path() / "source.cdx";
    const auto outputPath = root.path() / "out.cdx";
    writeBytes(sourcePath, sourceBytes);

    fo4ide::plugin::RadiclePluginBackend backend;
    const auto wrote = backend.invokeBackend(
        "WritePrevisContainer",
        nlohmann::json::array({std::string("cdx"), sourcePath.string(), outputPath.string()}));
    REQUIRE(wrote.has_value());
    const auto outBytes = readBytes(outputPath);
    REQUIRE(fo4::previs::validate_cdx(outBytes).empty());
    const auto parsed = fo4::previs::parse_cdx(outBytes);
    REQUIRE(parsed.rows.size() == 1u);
    CHECK(parsed.rows[0].form == 0x00000800u);
    CHECK(fo4::previs::write_cdx(parsed) == sourceBytes);
}
