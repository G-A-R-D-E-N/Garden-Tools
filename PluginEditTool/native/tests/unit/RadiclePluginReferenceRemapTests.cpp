#include <algorithm>
#include "../helpers/TempRoot.hpp"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/flags.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/schema/form_links.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path remapTempRoot() {
    const auto id = fo4ide_test::uniqueTempName();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_reference_remap_" + id);
}

struct RemapCleanup {
    std::filesystem::path root;
    ~RemapCleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

void remapWriteBytes(const std::filesystem::path& path,
                     const std::vector<std::byte>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    REQUIRE(stream.good());
}

void remapHeader(fo4::test::ByteWriter& out,
                 std::uint32_t nextFormId,
                 std::initializer_list<std::string_view> masters = {}) {
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(0);
    hedr.u32(nextFormId);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    for (const auto master : masters) {
        std::vector<std::byte> name;
        for (const char ch : master) name.push_back(std::byte{static_cast<std::uint8_t>(ch)});
        name.push_back(std::byte{0});
        fo4::test::write_subrecord(out, "MAST", name);
        fo4::test::ByteWriter data;
        data.u32(0);
        data.u32(0);
        fo4::test::write_subrecord(out, "DATA", data.bytes);
    }
    fo4::test::end_record(out, tes4);
}

std::vector<std::byte> colorPluginBytes(std::string_view editorId) {
    fo4::test::ByteWriter out;
    remapHeader(out, 0x801);
    const auto group = fo4::test::begin_group(out, "CLFM", 0);
    const auto record = fo4::test::begin_record(out, "CLFM", 0, 0x00000800u, 131);
    std::string text(editorId);
    text.push_back('\0');
    fo4::test::write_subrecord(out, "EDID", std::string_view{text.data(), text.size()});
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    return out.bytes;
}

std::vector<std::byte> referencingPluginBytes() {
    fo4::test::ByteWriter out;
    remapHeader(out, 0x801, {"Base.esm"});
    const auto group = fo4::test::begin_group(out, "RACE", 0);
    const auto record = fo4::test::begin_record(out, "RACE", 0, 0x01000800u, 131);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"UsesFrom\0", 9});
    fo4::test::ByteWriter link;
    link.u32(0x00000800u);
    fo4::test::write_subrecord(out, "AHCM", link.bytes);
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    return out.bytes;
}

std::vector<std::byte> emptyPatchBytes() {
    fo4::test::ByteWriter out;
    remapHeader(out, 0x800);
    return out.bytes;
}

std::vector<std::byte> undecodableMiscBytes() {
    fo4::test::ByteWriter out;
    remapHeader(out, 0x801);
    const auto group = fo4::test::begin_group(out, "MISC", 0);
    const auto record = fo4::test::begin_record(
        out, "MISC", fo4::RecordFlags::compressed, 0x00000801u, 131);
    out.u32(32);
    out.raw(std::string_view{"not-zlib-data-at-all"});
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    return out.bytes;
}

nlohmann::json remapPluginEntry(std::string name,
                                const std::filesystem::path& path) {
    return {
        {"name", std::move(name)},
        {"path", path.string()},
        {"source", "ReferenceRemapTest"},
        {"resolved", true}
    };
}

}  // namespace

TEST_CASE("Native ChangeReferencingRecords copies winners adds target master and remaps links") {
    const auto root = remapTempRoot();
    RemapCleanup cleanup{root};
    const auto basePath = root / "Base.esm";
    const auto otherPath = root / "Other.esm";
    const auto modPath = root / "Mod.esp";
    const auto patchPath = root / "Patch.esp";
    remapWriteBytes(basePath, colorPluginBytes("FromColor"));
    remapWriteBytes(otherPath, colorPluginBytes("ToColor"));
    remapWriteBytes(modPath, referencingPluginBytes());
    remapWriteBytes(patchPath, emptyPatchBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    const auto loaded = backend.invoke(
        "environment.LoadMo2",
        nlohmann::json{{
            "plugins",
            nlohmann::json::array({
                remapPluginEntry("Base.esm", basePath),
                remapPluginEntry("Other.esm", otherPath),
                remapPluginEntry("Mod.esp", modPath),
                remapPluginEntry("Patch.esp", patchPath)
            })
        }});
    REQUIRE(loaded.has_value());

    const auto preview = backend.changeReferencingRecords(
        nlohmann::json::array({
            "000800:Base.esm", "000800:Other.esm", "Patch.esp", false, 300
        }));
    REQUIRE(preview.has_value());
    REQUIRE(preview->is_string());
    CHECK(preview->get<std::string>().starts_with("DRY RUN: 1 record(s) reference"));
    CHECK(preview->get<std::string>().find("UsesFrom") != std::string::npos);

    const auto beforeSave = fo4::test::original_bytes(fo4::read_plugin_file(patchPath));

    const auto applied = backend.changeReferencingRecords(
        nlohmann::json::array({
            "000800:Base.esm", "000800:Other.esm", "Patch.esp", true, 300
        }));
    REQUIRE(applied.has_value());
    REQUIRE(applied->is_string());
    CHECK(applied->get<std::string>().find("Repointed 1 of 1 referencing record(s)") != std::string::npos);

    CHECK(fo4::test::original_bytes(fo4::read_plugin_file(patchPath)) == beforeSave);
    CHECK(applied->get<std::string>().find("SavePlugin") != std::string::npos);
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    const auto patch = fo4::read_plugin_file(patchPath);
    const auto* race = fo4::find_record(
        patch, fo4::FormKey::parse("000800:Mod.esp"));
    REQUIRE(race != nullptr);
    CHECK(race->header.signature == fo4::Signature::from_string("RACE"));

    const auto hasMaster = [&](std::string_view name) {
        return std::any_of(patch.header.masters.begin(), patch.header.masters.end(),
                           [&](const auto& master) { return master.key.filename() == name; });
    };
    CHECK(hasMaster("Base.esm"));
    CHECK(hasMaster("Mod.esp"));
    CHECK(hasMaster("Other.esm"));

    const auto links = fo4::collect_form_links(patch);
    const auto sourceKey = fo4::FormKey::parse("000800:Mod.esp");
    const auto toKey = fo4::FormKey::parse("000800:Other.esm");
    const auto found = std::find_if(links.begin(), links.end(), [&](const auto& link) {
        return link.record == sourceKey && link.key == toKey;
    });
    CHECK(found != links.end());
}

TEST_CASE("Native ChangeReferencingRecords previews a brand-new target without creating it") {
    const auto root = remapTempRoot();
    RemapCleanup cleanup{root};
    const auto basePath = root / "Base.esm";
    const auto otherPath = root / "Other.esm";
    const auto modPath = root / "Mod.esp";
    remapWriteBytes(basePath, colorPluginBytes("FromColor"));
    remapWriteBytes(otherPath, colorPluginBytes("ToColor"));
    remapWriteBytes(modPath, referencingPluginBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    const auto loaded = backend.invoke(
        "environment.LoadMo2",
        nlohmann::json{{
            "plugins",
            nlohmann::json::array({
                remapPluginEntry("Base.esm", basePath),
                remapPluginEntry("Other.esm", otherPath),
                remapPluginEntry("Mod.esp", modPath)
            })
        }});
    REQUIRE(loaded.has_value());

    const auto missingPath = root / "NewPatch.esp";
    REQUIRE_FALSE(std::filesystem::exists(missingPath));
    const auto preview = backend.changeReferencingRecords(
        nlohmann::json::array({
            "000800:Base.esm", "000800:Other.esm", "NewPatch.esp", false, 300
        }));
    REQUIRE(preview.has_value());
    REQUIRE(preview->is_string());
    CHECK(preview->get<std::string>().starts_with("DRY RUN: 1 record(s) reference"));
    CHECK(preview->get<std::string>().find("UsesFrom") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(missingPath));
}

TEST_CASE("Native ChangeReferencingRecords refuses while a loaded plugin has an undecodable record") {
    const auto root = remapTempRoot();
    RemapCleanup cleanup{root};
    const auto basePath = root / "Base.esm";
    const auto otherPath = root / "Other.esm";
    const auto modPath = root / "Mod.esp";
    const auto hiddenPath = root / "Hidden.esp";
    remapWriteBytes(basePath, colorPluginBytes("FromColor"));
    remapWriteBytes(otherPath, colorPluginBytes("ToColor"));
    remapWriteBytes(modPath, referencingPluginBytes());
    remapWriteBytes(hiddenPath, undecodableMiscBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    const auto loaded = backend.invoke(
        "environment.LoadMo2",
        nlohmann::json{{
            "plugins",
            nlohmann::json::array({
                remapPluginEntry("Base.esm", basePath),
                remapPluginEntry("Other.esm", otherPath),
                remapPluginEntry("Mod.esp", modPath),
                remapPluginEntry("Hidden.esp", hiddenPath)
            })
        }});
    REQUIRE(loaded.has_value());

    const auto preview = backend.changeReferencingRecords(
        nlohmann::json::array({
            "000800:Base.esm", "000800:Other.esm", "NewPatch.esp", false, 300
        }));
    REQUIRE_FALSE(preview.has_value());
    CHECK(preview.error().type == "ReferenceCoverageIncomplete");
    CHECK(preview.error().message.find("reference remap") != std::string::npos);
}
