#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <fstream>
#include <string_view>

#include <fo4/archive/ba2.hpp>
#include <fo4/nif/nif_import.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/previs/uvd_tile_build.hpp>
#include <fo4/previs/bake_oc.hpp>
#include <fo4/previs/cdx.hpp>
#include <fo4/previs/xcri.hpp>
#include <fo4/signature.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "../../src/plugin/RadiclePluginPrevisAssets.hpp"
#include "../../src/plugin/RadiclePluginPrevisPrep.hpp"
#include "../helpers/TempRoot.hpp"
#include "env_node_fixture.hpp"
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

// One exterior CELL 0x801 with no worldspace holds one placed STAT; extra CELL subrecords follow DATA.
void writeRockCellSource(const std::filesystem::path& path, std::vector<fo4::test::envnode::Sub> extraCellSubs = {}) {
    namespace en = fo4::test::envnode;
    const auto cell_children = static_cast<std::int32_t>(fo4::GroupType::cell_children);
    const auto cell_temporary = static_cast<std::int32_t>(fo4::GroupType::cell_temporary_children);
    std::vector<en::Sub> cellSubs{{.signature = "DATA", .data = {std::byte{0}}}};
    for (auto& sub : extraCellSubs) cellSubs.push_back(std::move(sub));
    writeBytes(path,
               en::make_plugin(
                   {},
                   {en::top("STAT", {en::rec("STAT", 0x810u,
                                              {{.signature = "MODL", .data = en::zstring("Meshes/Rock.nif")}})}),
                    en::top("CELL",
                            {en::rec("CELL", 0x801u, std::move(cellSubs)),
                             en::grp(0x801u, cell_children,
                                     {en::grp(0x801u, cell_temporary,
                                              {en::rec("REFR", 0x820u,
                                                       {{.signature = "NAME", .data = en::bytes_of({0x810u})},
                                                        {.signature = "DATA",
                                                         .data = en::floats({0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                                                             0.0f})}})})})})}));
}

void writeRockMesh(const std::filesystem::path& dataDir) {
    constexpr std::string_view k_obj = "o tri\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    const fo4::nif::ImportOptions options{.material = "PIPEMETAL:Materials\\metal\\PipeMetal.bgsm",
                                          .tex_diffuse = {},
                                          .tex_normal = {},
                                          .box_collision = false,
                                          .hull_collision = false,
                                          .mesh_collision = false,
                                          .body_mass = 0.0f,
                                          .from_blender = false,
                                          .skeleton = {}};
    writeBytes(dataDir / std::filesystem::path("Meshes/Rock.nif"), fo4::nif::import_obj(k_obj, options));
}

std::size_t regularFileCount(const std::filesystem::path& dir) {
    std::size_t count = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
        if (entry.is_regular_file()) ++count;
    }
    return count;
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

// Regression: generation that reports success on a cell it cannot plan must fail instead.
TEST_CASE("Native GenerateCellPrevis refuses a cell it cannot plan") {
    fo4ide_test::TempRoot root{"fo4ide_previs_generate"};
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

    const auto generated = backend.invokeBackend(
        "GenerateCellPrevis",
        nlohmann::json::array(
            {std::string("Patch.esp"), cellText, std::string("Source.esp"), "shell_OC.NIF", 2, false}));
    REQUIRE_FALSE(generated.has_value());
    CHECK(generated.error().type == "PrevisGeneratePlan");
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

namespace {

std::vector<std::byte> makeExteriorCellSource() {
    namespace en = fo4::test::envnode;
    const auto world_children = static_cast<std::int32_t>(fo4::GroupType::world_children);
    const auto exterior_block = static_cast<std::int32_t>(fo4::GroupType::exterior_cell_block);
    const auto exterior_subblock = static_cast<std::int32_t>(fo4::GroupType::exterior_cell_subblock);
    const auto cell_children = static_cast<std::int32_t>(fo4::GroupType::cell_children);
    const auto cell_temporary = static_cast<std::int32_t>(fo4::GroupType::cell_temporary_children);
    const auto placed = [](std::uint32_t form_id, float x) {
        return en::rec("REFR", form_id,
                       {{.signature = "NAME", .data = en::bytes_of({0x810u})},
                        {.signature = "DATA", .data = en::floats({x, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f})}});
    };
    return en::make_plugin(
        {},
        {en::top("STAT", {en::rec("STAT", 0x810u,
                                  {{.signature = "MODL", .data = en::zstring("Meshes/Rock.nif")}})}),
         en::top("WRLD", {en::rec("WRLD", 0x800u),
                          en::grp(0x800u, world_children,
                                  {en::grp(0u, exterior_block,
                                           {en::grp(0u, exterior_subblock,
                                                    {en::rec("CELL", 0x801u,
                                                             {{.signature = "XCLC",
                                                               .data = en::bytes_of({0u, 0u, 0u})}}),
                                                     en::grp(0x801u, cell_children,
                                                             {en::grp(0x801u, cell_temporary,
                                                                      {placed(0x820u, 0.0f),
                                                                       placed(0x821u, 64.0f)})})})})})})});
}

}  // namespace

// Regression: exterior cells must generate one _OC per planned group instead of being refused as interior-only.
TEST_CASE("Native GenerateCellPrevis plans an exterior cell") {
    fo4ide_test::TempRoot root{"fo4ide_previs_exterior"};
    const auto sourcePath = root.path() / "Source.esp";
    const auto patchPath = root.path() / "Patch.esp";
    writeBytes(sourcePath, makeExteriorCellSource());
    writeBytes(patchPath, makeEmptyPatch());
    writeBytes(root.path() / std::filesystem::path("Meshes/Rock.nif"),
               {std::byte{0x4E}, std::byte{0x49}, std::byte{0x46}, std::byte{0}});
    writeBytes(root.path() / "shell_OC.NIF", {std::byte{0}});

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
    backend.setDataDirectory(root.path());

    const auto generated = backend.invokeBackend(
        "GenerateCellPrevis",
        nlohmann::json::array({std::string("Patch.esp"), cellText, std::string("Source.esp"),
                               (root.path() / "shell_OC.NIF").string(), 2, false}));
    if (!generated.has_value()) FAIL(generated.error().type + ": " + generated.error().message);
    CHECK((*generated)["cell"] == cellText);
    CHECK((*generated)["meshIds"] == 1);
    CHECK((*generated)["references"] == 2);
    REQUIRE((*generated)["groups"].size() == 1u);
    CHECK((*generated)["groups"][0]["ocId"] == 1);
    CHECK((*generated)["groups"][0]["instanceCount"] == 2);
    const std::filesystem::path ocPath((*generated)["groups"][0]["path"].get<std::string>());
    CHECK(ocPath.filename() == "00000801_00000001_OC.NIF");
    CHECK(ocPath.parent_path().filename() == "Patch.esp");
    CHECK(ocPath.parent_path().parent_path().filename() == "PreCombined");
}

namespace {

std::vector<std::byte> makeSingleStatCellSource() {
    namespace en = fo4::test::envnode;
    const auto cell_children = static_cast<std::int32_t>(fo4::GroupType::cell_children);
    const auto cell_temporary = static_cast<std::int32_t>(fo4::GroupType::cell_temporary_children);
    return en::make_plugin(
        {},
        {en::top("STAT", {en::rec("STAT", 0x810u,
                                  {{.signature = "MODL", .data = en::zstring("Meshes/Rock.nif")}})}),
         en::top("CELL", {en::rec("CELL", 0x801u, {{.signature = "DATA", .data = {std::byte{1}}}}),
                          en::grp(0x801u, cell_children,
                                  {en::grp(0x801u, cell_temporary,
                                           {en::rec("REFR", 0x820u,
                                                    {{.signature = "NAME", .data = en::bytes_of({0x810u})},
                                                     {.signature = "DATA",
                                                      .data = en::floats({0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                                                          0.0f})}})})})})});
}

}

std::vector<std::byte> makeSingleStatExteriorCellSource() {
    namespace en = fo4::test::envnode;
    const auto world_children = static_cast<std::int32_t>(fo4::GroupType::world_children);
    const auto block = static_cast<std::int32_t>(fo4::GroupType::exterior_cell_block);
    const auto subblock = static_cast<std::int32_t>(fo4::GroupType::exterior_cell_subblock);
    const auto cell_children = static_cast<std::int32_t>(fo4::GroupType::cell_children);
    const auto cell_temporary = static_cast<std::int32_t>(fo4::GroupType::cell_temporary_children);
    return en::make_plugin(
        {},
        {en::top("STAT", {en::rec("STAT", 0x810u,
                                  {{.signature = "MODL", .data = en::zstring("Meshes/Rock.nif")}})}),
         en::top("WRLD",
                 {en::rec("WRLD", 0x800u,
                          {{.signature = "EDID", .data = en::zstring("Wilderness")}}),
                  en::grp(0x800u, world_children,
                          {en::grp(0u, block,
                                   {en::grp(0u, subblock,
                                            {en::rec("CELL", 0x801u,
                                                     {{.signature = "DATA", .data = {std::byte{0}}},
                                                      {.signature = "XCLC",
                                                       .data = en::bytes_of({0u, 0u, 0u})}}),
                                             en::grp(0x801u, cell_children,
                                                     {en::grp(0x801u, cell_temporary,
                                                              {en::rec("REFR", 0x820u,
                                                                       {{.signature = "NAME", .data = en::bytes_of({0x810u})},
                                                                        {.signature = "DATA",
                                                                         .data = en::floats({0.0f, 0.0f, 0.0f, 0.0f,
                                                                                             0.0f, 0.0f})}})})})})})})})});
}

const fo4::Group* childGroupOfType(const fo4::Group& parent, std::int32_t type) {
    for (const auto& child : parent.children) {
        if (const auto* group = std::get_if<fo4::Group>(&child)) {
            if (group->header.group_type == type) return group;
        }
    }
    return nullptr;
}

// Regression: generation must bake a real _OC.NIF and stamp PCMB/VISI/XCRI on the saved CELL override.
TEST_CASE("Native GenerateCellPrevis bakes an _OC.NIF and stamps the saved CELL") {
    fo4ide_test::TempRoot root{"fo4ide_previs_bake"};
    const auto dataDir = root.path() / "data";
    const auto pluginDir = root.path() / "plugins";
    const auto sourcePath = pluginDir / "Source.esp";
    const auto patchPath = pluginDir / "Patch.esp";
    writeBytes(sourcePath, makeSingleStatCellSource());
    fo4::previs::CdxFile winnerCdx;
    winnerCdx.rows.push_back({0x80u, 0x00000801u, 3u, 4u});
    writeBytes(dataDir / "Source.cdx", fo4::previs::write_cdx(winnerCdx));
    writeBytes(patchPath, makeEmptyPatch());

    constexpr std::string_view k_obj = "o tri\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    const fo4::nif::ImportOptions options{.material = "PIPEMETAL:Materials\\metal\\PipeMetal.bgsm",
                                          .tex_diffuse = {},
                                          .tex_normal = {},
                                          .box_collision = false,
                                          .hull_collision = false,
                                          .mesh_collision = false,
                                          .body_mass = 0.0f,
                                          .from_blender = false,
                                          .skeleton = {}};
    const auto statBytes = fo4::nif::import_obj(k_obj, options);
    REQUIRE(fo4::previs::stat_mesh_nif(statBytes));
    writeBytes(dataDir / std::filesystem::path("Meshes/Rock.nif"), statBytes);

    const std::filesystem::path shell{FO4IDE_PREVIS_FIXTURES_DIR "/00000FEC_0A82376A_OC.NIF"};
    REQUIRE(std::filesystem::exists(shell));

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
    backend.setDataDirectory(dataDir);

    const auto generated = backend.invokeBackend(
        "GenerateCellPrevis",
        nlohmann::json::array({std::string("Patch.esp"), cellText, std::string("Source.esp"),
                               shell.string(), 1u, true}));
    if (!generated.has_value()) FAIL(generated.error().type + ": " + generated.error().message);
    CHECK((*generated)["dryRun"] == false);
    CHECK((*generated)["meshIds"] == 1);
    CHECK((*generated)["references"] == 1);
    REQUIRE((*generated)["groups"].size() == 1u);
    const std::filesystem::path ocPath((*generated)["groups"][0]["path"].get<std::string>());
    CHECK(ocPath.filename() == "00000801_00000001_OC.NIF");
    CHECK(ocPath == dataDir / "Meshes" / "PreCombined" / "Patch.esp" / "00000801_00000001_OC.NIF");
    REQUIRE(std::filesystem::exists(ocPath));
    const auto ocBytes = readBytes(ocPath);
    CHECK(fo4::previs::oc_nif_has_shared_combined_geom(ocBytes));

    REQUIRE(backend.invokeBackend("SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    auto patch = fo4::read_plugin_file(patchPath);
    fo4::assign_record_form_keys(patch);
    const auto* cell = fo4::find_record(patch, fo4::FormKey::parse(cellText));
    REQUIRE(cell != nullptr);
    const fo4::Subrecord* pcmb = nullptr;
    const fo4::Subrecord* visi = nullptr;
    const fo4::Subrecord* xcri = nullptr;
    for (const auto& sub : cell->subrecords) {
        if (sub.signature == fo4::Signature::from_string("PCMB")) pcmb = &sub;
        if (sub.signature == fo4::Signature::from_string("VISI")) visi = &sub;
        if (sub.signature == fo4::Signature::from_string("XCRI")) xcri = &sub;
    }
    REQUIRE(pcmb != nullptr);
    REQUIRE(visi != nullptr);
    REQUIRE(xcri != nullptr);
    const auto stamp = static_cast<std::uint16_t>((*generated)["pcmbStamp"].get<std::uint32_t>());
    REQUIRE(pcmb->data.size() == 2u);
    CHECK(std::to_integer<std::uint8_t>(pcmb->data[0]) == (stamp & 0xFFu));
    CHECK(std::to_integer<std::uint8_t>(pcmb->data[1]) == ((stamp >> 8) & 0xFFu));
    REQUIRE(visi->data.size() == 2u);
    CHECK(std::to_integer<std::uint8_t>(visi->data[0]) == (stamp & 0xFFu));
    const auto parsed = fo4::previs::parse_xcri_bytes(xcri->data);
    CHECK(parsed.mesh_ids == std::vector<std::uint32_t>{1u});
    REQUIRE(parsed.refs.size() == 1u);
    CHECK(parsed.refs[0].first == 0x820u);
    CHECK(parsed.refs[0].second == 1u);
    // Stamping must clear "No Pre Vis" (bit 7) and keep the record's payload marker intact.
    CHECK(!cell->header.flags.test(1u << 7));
    CHECK(cell->header.flags.is_compressed() == cell->compressed);

    const auto interiorCdx = fo4::previs::parse_cdx(readBytes(pluginDir / "Patch.cdx"));
    REQUIRE(interiorCdx.rows.size() == 1u);
    CHECK(interiorCdx.rows[0].form == 0x00000801u);
    CHECK(interiorCdx.rows[0].key == 0x80u);
    CHECK(interiorCdx.rows[0].value == 3u);
    CHECK(interiorCdx.rows[0].extra == 4u);
}

// Regression: an exterior cell must bake end to end; its WRLD -> block -> subblock chain used to be refused.
TEST_CASE("Native GenerateCellPrevis stamps an exterior CELL override") {
    fo4ide_test::TempRoot root{"fo4ide_previs_exterior"};
    const auto dataDir = root.path() / "data";
    const auto pluginDir = root.path() / "plugins";
    const auto sourcePath = pluginDir / "Source.esp";
    const auto patchPath = pluginDir / "Patch.esp";
    writeBytes(sourcePath, makeSingleStatExteriorCellSource());
    writeBytes(patchPath, makeEmptyPatch());
    fo4::previs::CdxFile winnerCdx;
    winnerCdx.rows.push_back({0u, 0x00000800u, 7u, 9u});
    writeBytes(dataDir / "Source.cdx", fo4::previs::write_cdx(winnerCdx));

    constexpr std::string_view k_obj = "o tri\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    const fo4::nif::ImportOptions options{.material = "PIPEMETAL:Materials\\metal\\PipeMetal.bgsm",
                                          .tex_diffuse = {},
                                          .tex_normal = {},
                                          .box_collision = false,
                                          .hull_collision = false,
                                          .mesh_collision = false,
                                          .body_mass = 0.0f,
                                          .from_blender = false,
                                          .skeleton = {}};
    const auto statBytes = fo4::nif::import_obj(k_obj, options);
    REQUIRE(fo4::previs::stat_mesh_nif(statBytes));
    writeBytes(dataDir / std::filesystem::path("Meshes/Rock.nif"), statBytes);

    const std::filesystem::path shell{FO4IDE_PREVIS_FIXTURES_DIR "/00000FEC_0A82376A_OC.NIF"};
    REQUIRE(std::filesystem::exists(shell));

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
    backend.setDataDirectory(dataDir);

    const auto generated = backend.invokeBackend(
        "GenerateCellPrevis",
        nlohmann::json::array({std::string("Patch.esp"), cellText, std::string("Source.esp"),
                               shell.string(), 1u, true}));
    if (!generated.has_value()) FAIL(generated.error().type + ": " + generated.error().message);
    CHECK((*generated)["dryRun"] == false);
    CHECK((*generated)["meshIds"] == 1);
    CHECK((*generated)["references"] == 1);
    REQUIRE((*generated)["groups"].size() == 1u);
    const std::filesystem::path ocPath((*generated)["groups"][0]["path"].get<std::string>());
    CHECK(ocPath == dataDir / "Meshes" / "PreCombined" / "Patch.esp" / "00000801_00000001_OC.NIF");
    REQUIRE(std::filesystem::exists(ocPath));
    CHECK(fo4::previs::oc_nif_has_shared_combined_geom(readBytes(ocPath)));

    REQUIRE(backend.invokeBackend("SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    auto patch = fo4::read_plugin_file(patchPath);
    fo4::assign_record_form_keys(patch);
    const auto key = fo4::FormKey::parse(cellText);
    const auto* cell = fo4::find_record(patch, key);
    REQUIRE(cell != nullptr);
    const fo4::Subrecord* pcmb = nullptr;
    const fo4::Subrecord* visi = nullptr;
    const fo4::Subrecord* xcri = nullptr;
    for (const auto& sub : cell->subrecords) {
        if (sub.signature == fo4::Signature::from_string("PCMB")) pcmb = &sub;
        if (sub.signature == fo4::Signature::from_string("VISI")) visi = &sub;
        if (sub.signature == fo4::Signature::from_string("XCRI")) xcri = &sub;
    }
    REQUIRE(pcmb != nullptr);
    REQUIRE(visi != nullptr);
    REQUIRE(xcri != nullptr);
    const auto stamp = static_cast<std::uint16_t>((*generated)["pcmbStamp"].get<std::uint32_t>());
    REQUIRE(pcmb->data.size() == 2u);
    CHECK(std::to_integer<std::uint8_t>(pcmb->data[0]) == (stamp & 0xFFu));
    CHECK(std::to_integer<std::uint8_t>(pcmb->data[1]) == ((stamp >> 8) & 0xFFu));
    REQUIRE(visi->data.size() == 2u);
    CHECK(std::to_integer<std::uint8_t>(visi->data[0]) == (stamp & 0xFFu));
    const auto parsed = fo4::previs::parse_xcri_bytes(xcri->data);
    CHECK(parsed.mesh_ids == std::vector<std::uint32_t>{1u});
    REQUIRE(parsed.refs.size() == 1u);
    CHECK(parsed.refs[0].first == 0x820u);
    CHECK(parsed.refs[0].second == 1u);
    // Stamping must clear "No Pre Vis" (bit 7) and keep the record's payload marker intact.
    CHECK(!cell->header.flags.test(1u << 7));
    CHECK(cell->header.flags.is_compressed() == cell->compressed);

    const fo4::Group* worldTop = nullptr;
    for (const auto& group : patch.groups) {
        if (group.header.group_type == 0 &&
            group.header.top_label() == fo4::Signature::from_string("WRLD")) {
            worldTop = &group;
        }
    }
    REQUIRE(worldTop != nullptr);
    const auto* children = childGroupOfType(*worldTop, 1);
    REQUIRE(children != nullptr);
    const auto* blockGroup = childGroupOfType(*children, 4);
    REQUIRE(blockGroup != nullptr);
    const auto* subblockGroup = childGroupOfType(*blockGroup, 5);
    REQUIRE(subblockGroup != nullptr);
    bool contained = false;
    for (const auto& child : subblockGroup->children) {
        const auto* record = std::get_if<fo4::Record>(&child);
        if (record != nullptr && record->form_key == key) contained = true;
    }
    CHECK(contained);
}

TEST_CASE("Native GenerateCellPrevis ships a CDX row so a generated cell is not left unindexed") {
    fo4ide_test::TempRoot root{"fo4ide_previs_cdx"};
    const auto dataDir = root.path() / "data";
    const auto pluginDir = root.path() / "plugins";
    const auto sourcePath = pluginDir / "Source.esp";
    const auto patchPath = pluginDir / "Patch.esp";
    writeBytes(sourcePath, makeSingleStatExteriorCellSource());
    writeBytes(patchPath, makeEmptyPatch());
    fo4::previs::CdxFile winnerCdx;
    winnerCdx.rows.push_back({0u, 0x00000800u, 7u, 9u});
    writeBytes(dataDir / "Source.cdx", fo4::previs::write_cdx(winnerCdx));

    constexpr std::string_view k_obj = "o tri\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    const fo4::nif::ImportOptions options{.material = "PIPEMETAL:Materials\\metal\\PipeMetal.bgsm",
                                          .tex_diffuse = {},
                                          .tex_normal = {},
                                          .box_collision = false,
                                          .hull_collision = false,
                                          .mesh_collision = false,
                                          .body_mass = 0.0f,
                                          .from_blender = false,
                                          .skeleton = {}};
    writeBytes(dataDir / std::filesystem::path("Meshes/Rock.nif"), fo4::nif::import_obj(k_obj, options));
    const std::filesystem::path shell{FO4IDE_PREVIS_FIXTURES_DIR "/00000FEC_0A82376A_OC.NIF"};
    REQUIRE(std::filesystem::exists(shell));

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
    backend.setDataDirectory(dataDir);

    const auto generated = backend.invokeBackend(
        "GenerateCellPrevis",
        nlohmann::json::array({std::string("Patch.esp"), cellText, std::string("Source.esp"),
                               shell.string(), 1u, true}));
    if (!generated.has_value()) FAIL(generated.error().type + ": " + generated.error().message);
    REQUIRE(generated->contains("cdx"));
    CHECK((*generated)["cdx"]["rows"] == 1);

    const auto cdxBytes = readBytes(pluginDir / "Patch.cdx");
    CHECK(fo4::previs::validate_cdx(cdxBytes).empty());
    const auto parsed = fo4::previs::parse_cdx(cdxBytes);
    REQUIRE(parsed.rows.size() == 1u);
    CHECK(parsed.rows[0].form == 0x00000800u);
    CHECK(parsed.rows[0].key == 0u);
    CHECK(parsed.rows[0].value == 7u);
    CHECK(parsed.rows[0].extra == 9u);
}

// Detects: a dry run must list only the limits it can know before apply. It looks up no CDX row and builds no tome, so a cell whose index has a row is not reported as missing one, while an exterior cell without a worldspace still is.
TEST_CASE("Native GenerateCellPrevis dry run lists only the limits it can know before apply") {
    fo4ide_test::TempRoot root{"fo4ide_previs_dry_run_limits"};
    const auto dataDir = root.path() / "data";
    const auto pluginDir = root.path() / "plugins";
    const auto patchPath = pluginDir / "Patch.esp";
    writeBytes(patchPath, makeEmptyPatch());
    writeRockMesh(dataDir);
    const std::filesystem::path shell{FO4IDE_PREVIS_FIXTURES_DIR "/00000FEC_0A82376A_OC.NIF"};
    REQUIRE(std::filesystem::exists(shell));

    const auto dryRun = [&](const std::filesystem::path& sourcePath) {
        fo4ide::plugin::RadiclePluginBackend backend;
        const nlohmann::json environment{
            {"plugins",
             nlohmann::json::array({pluginEntry("Source.esp", sourcePath), pluginEntry("Patch.esp", patchPath)})}};
        REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());
        backend.setDataDirectory(dataDir);
        const auto generated = backend.invokeBackend(
            "GenerateCellPrevis",
            nlohmann::json::array({std::string("Patch.esp"), std::string("000801:Source.esp"),
                                   std::string("Source.esp"), shell.string(), 1u, false}));
        if (!generated.has_value()) FAIL(generated.error().type + ": " + generated.error().message);
        CHECK((*generated)["dryRun"] == true);
        CHECK((*generated)["complete"] == false);
        CHECK((*generated)["ok"] == false);
        CHECK_FALSE(generated->contains("cdx"));
        return *generated;
    };

    // The worldspace cell's own index holds its row, as in the CDX test above.
    const auto worldSource = pluginDir / "world" / "Source.esp";
    writeBytes(worldSource, makeSingleStatExteriorCellSource());
    fo4::previs::CdxFile winnerCdx;
    winnerCdx.rows.push_back({0u, 0x00000800u, 7u, 9u});
    writeBytes(dataDir / "Source.cdx", fo4::previs::write_cdx(winnerCdx));
    const auto inWorld = dryRun(worldSource);
    CHECK(inWorld["limitations"] == nlohmann::json::array({"dry_run_only"}));
    CHECK_FALSE(std::filesystem::exists(pluginDir / "Patch.cdx"));
    REQUIRE(std::filesystem::remove(dataDir / "Source.cdx"));
    CHECK(dryRun(worldSource)["limitations"] == nlohmann::json::array({"dry_run_only"}));

    const auto noWorldSource = pluginDir / "noworld" / "Source.esp";
    writeRockCellSource(noWorldSource);
    const auto noWorld = dryRun(noWorldSource);
    CHECK(noWorld["limitations"] ==
          nlohmann::json::array({"the exterior cell has no worldspace; no CDX row was written", "dry_run_only"}));
}

namespace {

std::vector<std::byte> makeBaseCellMaster() {
    namespace en = fo4::test::envnode;
    return en::make_plugin(
        {}, {en::top("CELL", {en::rec("CELL", 0x801u, {{.signature = "DATA", .data = {std::byte{1}}}})})});
}

// A mod that places its own STAT twice in the master's interior cell: its own records carry index 01.
std::vector<std::byte> makeModPlacingStatsInBaseCell() {
    namespace en = fo4::test::envnode;
    const auto cell_children = static_cast<std::int32_t>(fo4::GroupType::cell_children);
    const auto cell_temporary = static_cast<std::int32_t>(fo4::GroupType::cell_temporary_children);
    const auto placed = [](std::uint32_t form_id, float x) {
        return en::rec("REFR", form_id,
                       {{.signature = "NAME", .data = en::bytes_of({0x01000810u})},
                        {.signature = "DATA", .data = en::floats({x, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f})}});
    };
    return en::make_plugin(
        {"Base.esm"},
        {en::top("STAT", {en::rec("STAT", 0x01000810u,
                                  {{.signature = "MODL", .data = en::zstring("Meshes/Rock.nif")}})}),
         en::top("CELL", {en::rec("CELL", 0x00000801u, {{.signature = "DATA", .data = {std::byte{1}}}}),
                          en::grp(0x00000801u, cell_children,
                                  {en::grp(0x00000801u, cell_temporary,
                                           {placed(0x01000820u, 0.0f), placed(0x01000821u, 64.0f)})})})});
}

// Constraint: an index plugin must master Base.esm to encode a Base.esm cell's Form-ID.
std::vector<std::byte> makeBaseMasteredIndexPlugin() {
    return fo4::test::envnode::make_plugin({"Base.esm"}, {});
}

// A third plugin overrides the master's cell and places one more copy of Mod.esp's STAT; that reference is its own (index 02), so a plan for Mod.esp cannot pair it.
std::vector<std::byte> makeOtherPlacingModStatInBaseCell() {
    namespace en = fo4::test::envnode;
    const auto cell_children = static_cast<std::int32_t>(fo4::GroupType::cell_children);
    const auto cell_temporary = static_cast<std::int32_t>(fo4::GroupType::cell_temporary_children);
    return en::make_plugin(
        {"Base.esm", "Mod.esp"},
        {en::top("CELL",
                 {en::rec("CELL", 0x00000801u, {{.signature = "DATA", .data = {std::byte{1}}}}),
                  en::grp(0x00000801u, cell_children,
                          {en::grp(0x00000801u, cell_temporary,
                                   {en::rec("REFR", 0x02000830u,
                                            {{.signature = "NAME", .data = en::bytes_of({0x01000810u})},
                                             {.signature = "DATA",
                                              .data = en::floats({128.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f})}})})})})});
}

// Base.esm owns interior cell 0x801, Mod.esp places two of its own STATs there, Base.cdx holds the cell's row and the rock mesh is loose under Data, so a Mod.esp run on 000801:Base.esm completes unless the test adds a limit.
struct BaseModCell {
    fo4ide_test::TempRoot root;
    std::filesystem::path dataDir = root.path() / "data";
    std::filesystem::path pluginDir = root.path() / "plugins";
    std::filesystem::path patchPath = pluginDir / "Patch.esp";
    std::filesystem::path shell{FO4IDE_PREVIS_FIXTURES_DIR "/00000FEC_0A82376A_OC.NIF"};
    nlohmann::json plugins = nlohmann::json::array();

    explicit BaseModCell(const char* name) : root{name} {
        writeBytes(pluginDir / "Base.esm", makeBaseCellMaster());
        writeBytes(pluginDir / "Mod.esp", makeModPlacingStatsInBaseCell());
        writeBytes(patchPath, makeEmptyPatch());
        fo4::previs::CdxFile baseCdx;
        baseCdx.rows.push_back({0u, 0x00000801u, 7u, 9u});
        writeBytes(dataDir / "Base.cdx", fo4::previs::write_cdx(baseCdx));
        writeRockMesh(dataDir);
        REQUIRE(std::filesystem::exists(shell));
        plugins.push_back(pluginEntry("Base.esm", pluginDir / "Base.esm"));
        plugins.push_back(pluginEntry("Mod.esp", pluginDir / "Mod.esp"));
    }

    nlohmann::json generate(fo4ide::plugin::RadiclePluginBackend& backend, bool copyVanilla) const {
        auto loaded = plugins;
        loaded.push_back(pluginEntry("Patch.esp", patchPath));
        REQUIRE(backend.invoke("environment.LoadMo2", nlohmann::json{{"plugins", loaded}}).has_value());
        backend.setDataDirectory(dataDir);
        const auto generated = backend.invokeBackend(
            "GenerateCellPrevis",
            nlohmann::json::array({std::string("Patch.esp"), std::string("000801:Base.esm"), std::string("Mod.esp"),
                                   shell.string(), 2u, true, copyVanilla}));
        if (!generated.has_value()) FAIL(generated.error().type + ": " + generated.error().message);
        return *generated;
    }

    // Writes a validated tome with the given number of bodies at Data\Vis\<folder>\00000801.uvd and returns its bytes.
    std::vector<std::byte> shipTome(std::string_view folder, std::size_t bodies) const {
        fo4ide::plugin::previs_prep::TomeRequest seed;
        seed.path = fo4::previs::uvd_tome_path(dataDir, folder, 0x801u);
        for (std::size_t index = 0; index < bodies; ++index) {
            const auto offset = 40.0f * static_cast<float>(index);
            seed.bodies.push_back(fo4::nif::PhysicsMeshInput{
                {offset, 0.0f, 0.0f, offset + 10.0f, 0.0f, 0.0f, offset, 10.0f, 5.0f}, {0, 1, 2}});
        }
        REQUIRE(fo4ide::plugin::previs_prep::write_tome(seed).has_value());
        return readBytes(seed.path);
    }
};

}  // namespace

// Detects: XCRI written with bare local ids. A mod's REFR in a master's cell then resolves to the master
// in the patch, so the engine pairs the combined mesh with a reference that does not exist. The patch must
// declare the REFR owner as a master and write its index in every XCRI reference word.
TEST_CASE("Native GenerateCellPrevis writes XCRI references as Form-IDs in the patch's master list") {
    fo4ide_test::TempRoot root{"fo4ide_previs_masters"};
    const auto dataDir = root.path() / "data";
    const auto pluginDir = root.path() / "plugins";
    const auto basePath = pluginDir / "Base.esm";
    const auto modPath = pluginDir / "Mod.esp";
    const auto patchPath = pluginDir / "Patch.esp";
    writeBytes(basePath, makeBaseCellMaster());
    writeBytes(modPath, makeModPlacingStatsInBaseCell());
    writeBytes(patchPath, makeEmptyPatch());
    fo4::previs::CdxFile baseCdx;
    baseCdx.rows.push_back({0u, 0x00000801u, 7u, 9u});
    writeBytes(dataDir / "Base.cdx", fo4::previs::write_cdx(baseCdx));

    constexpr std::string_view k_obj = "o tri\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    const fo4::nif::ImportOptions options{.material = "PIPEMETAL:Materials\\metal\\PipeMetal.bgsm",
                                          .tex_diffuse = {},
                                          .tex_normal = {},
                                          .box_collision = false,
                                          .hull_collision = false,
                                          .mesh_collision = false,
                                          .body_mass = 0.0f,
                                          .from_blender = false,
                                          .skeleton = {}};
    writeBytes(dataDir / std::filesystem::path("Meshes/Rock.nif"), fo4::nif::import_obj(k_obj, options));
    const std::filesystem::path shell{FO4IDE_PREVIS_FIXTURES_DIR "/00000FEC_0A82376A_OC.NIF"};
    REQUIRE(std::filesystem::exists(shell));

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({pluginEntry("Base.esm", basePath), pluginEntry("Mod.esp", modPath),
                                           pluginEntry("Patch.esp", patchPath)})}};
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());
    backend.setDataDirectory(dataDir);

    const std::string cellText = "000801:Base.esm";
    const auto generated = backend.invokeBackend(
        "GenerateCellPrevis",
        nlohmann::json::array({std::string("Patch.esp"), cellText, std::string("Mod.esp"), shell.string(), 2u,
                               true}));
    if (!generated.has_value()) FAIL(generated.error().type + ": " + generated.error().message);
    CHECK((*generated)["meshIds"] == 1);
    CHECK((*generated)["references"] == 2);
    REQUIRE(backend.invokeBackend("SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    auto patch = fo4::read_plugin_file(patchPath);
    fo4::assign_record_form_keys(patch);
    REQUIRE(patch.header.masters.size() == 2u);
    CHECK(patch.header.masters[0].key.filename() == "Base.esm");
    CHECK(patch.header.masters[1].key.filename() == "Mod.esp");
    const auto* cell = fo4::find_record(patch, fo4::FormKey::parse(cellText));
    REQUIRE(cell != nullptr);
    const fo4::Subrecord* xcri = nullptr;
    for (const auto& sub : cell->subrecords) {
        if (sub.signature == fo4::Signature::from_string("XCRI")) xcri = &sub;
    }
    REQUIRE(xcri != nullptr);
    const auto parsed = fo4::previs::parse_xcri_bytes(xcri->data);
    REQUIRE(parsed.refs.size() == 2u);
    const auto context = fo4::plugin_form_id_context(patch);
    CHECK(parsed.refs[0].first == 0x01000820u);
    CHECK(parsed.refs[1].first == 0x01000821u);
    CHECK(fo4::resolve_plugin_form_id(fo4::RawFormId{parsed.refs[0].first}, context) ==
          fo4::FormKey::parse("000820:Mod.esp"));
    CHECK(fo4::resolve_plugin_form_id(fo4::RawFormId{parsed.refs[1].first}, context) ==
          fo4::FormKey::parse("000821:Mod.esp"));
}

// Detects: copy_vanilla_tome read only the cell owner's Vis folder, so a tome the winning override shipped was missed, or only the winner's, so a cell that no override regenerated lost its owner's tome. The winner's folder is searched first, then the owner's.
TEST_CASE("Native GenerateCellPrevis copies the winning plugin's shipped tome before the cell owner's") {
    const auto copiedTome = [](const BaseModCell& fixture) {
        return readBytes(fo4::previs::uvd_tome_path(fixture.dataDir, "Patch.esp", 0x801u));
    };
    const auto warnsCopy = [](const nlohmann::json& generated) {
        return generated.contains("warnings") &&
               std::ranges::any_of(generated["warnings"], [](const nlohmann::json& item) {
                   return item.is_string() && item.get<std::string>().find("copy_vanilla_tome") != std::string::npos;
               });
    };

    {
        BaseModCell fixture{"fo4ide_previs_copy_winner_tome"};
        const auto winnerTome = fixture.shipTome("Mod.esp", 3);
        const auto ownerTome = fixture.shipTome("Base.esm", 2);
        REQUIRE(winnerTome != ownerTome);
        fo4ide::plugin::RadiclePluginBackend backend;
        const auto generated = fixture.generate(backend, true);
        CHECK(generated.value("complete", true));
        CHECK(warnsCopy(generated));
        CHECK(copiedTome(fixture) == winnerTome);
    }

    BaseModCell fixture{"fo4ide_previs_copy_owner_tome"};
    const auto ownerTome = fixture.shipTome("Base.esm", 2);
    fo4ide::plugin::RadiclePluginBackend backend;
    const auto generated = fixture.generate(backend, true);
    CHECK(generated.value("complete", true));
    CHECK(warnsCopy(generated));
    CHECK(copiedTome(fixture) == ownerTome);
}

// Detects: a STAT reference that another plugin owns cannot be paired in this patch's XCRI, so a run that finds one must return complete:false with the count and leave every file and the patch as they were.
TEST_CASE("Native GenerateCellPrevis refuses an empty winning tome without falling back") {
    BaseModCell fixture{"fo4ide_previs_empty_winner_tome"};
    fixture.shipTome("Mod.esp", 3);
    fixture.shipTome("Base.esm", 2);
    const auto winner = fo4::previs::uvd_tome_path(fixture.dataDir, "Mod.esp", 0x801u);
    {
        std::ofstream empty(winner, std::ios::binary | std::ios::trunc);
        REQUIRE(empty.good());
    }
    const auto patchBefore = readBytes(fixture.patchPath);
    const auto dataFilesBefore = regularFileCount(fixture.dataDir);
    const auto pluginFilesBefore = regularFileCount(fixture.pluginDir);
    auto plugins = fixture.plugins;
    plugins.push_back(pluginEntry("Patch.esp", fixture.patchPath));
    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", nlohmann::json{{"plugins", plugins}}).has_value());
    backend.setDataDirectory(fixture.dataDir);
    const auto result = backend.invokeBackend(
        "GenerateCellPrevis", nlohmann::json::array({"Patch.esp", "000801:Base.esm", "Mod.esp",
                                                       fixture.shell.string(), 2u, true, true}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "PrevisGenerateTome");
    CHECK(result.error().message.find("empty") != std::string::npos);
    CHECK(readBytes(fixture.patchPath) == patchBefore);
    CHECK(regularFileCount(fixture.dataDir) == dataFilesBefore);
    CHECK(regularFileCount(fixture.pluginDir) == pluginFilesBefore);
}

TEST_CASE("Native GenerateCellPrevis writes and stages nothing when another plugin owns a planned cell's STAT reference") {
    BaseModCell fixture{"fo4ide_previs_foreign_refs"};
    const auto otherPath = fixture.pluginDir / "Other.esp";
    writeBytes(otherPath, makeOtherPlacingModStatInBaseCell());
    fixture.plugins.push_back(pluginEntry("Other.esp", otherPath));
    const auto patchBefore = readBytes(fixture.patchPath);
    const auto dataFilesBefore = regularFileCount(fixture.dataDir);
    const auto pluginFilesBefore = regularFileCount(fixture.pluginDir);

    fo4ide::plugin::RadiclePluginBackend backend;
    const auto generated = fixture.generate(backend, false);
    CHECK(generated.at("complete") == false);
    CHECK(generated.at("ok") == false);
    CHECK(generated.at("planSkipped").at("foreignPlugin") == 1);
    CHECK(generated.value("limitations", nlohmann::json()) == nlohmann::json::array({"foreign_plugin_refs:1"}));
    CHECK_FALSE(generated.contains("csg"));
    CHECK_FALSE(generated.contains("uvd"));
    CHECK_FALSE(generated.contains("cdx"));
    CHECK(regularFileCount(fixture.dataDir) == dataFilesBefore);
    CHECK(regularFileCount(fixture.pluginDir) == pluginFilesBefore);
    CHECK(readBytes(fixture.patchPath) == patchBefore);

    REQUIRE(backend.invokeBackend("SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    auto saved = fo4::read_plugin_file(fixture.patchPath);
    fo4::assign_record_form_keys(saved);
    CHECK(fo4::find_record(saved, fo4::FormKey::parse("000801:Base.esm")) == nullptr);
}

TEST_CASE("Native GenerateCellPrevis marks the run incomplete when no CDX row can be written") {
    fo4ide_test::TempRoot root{"fo4ide_previs_cdx_missing"};
    const auto dataDir = root.path() / "data";
    const auto pluginDir = root.path() / "plugins";
    const auto basePath = pluginDir / "Base.esm";
    const auto modPath = pluginDir / "Mod.esp";
    const auto patchPath = pluginDir / "Patch.esp";
    writeBytes(basePath, makeBaseCellMaster());
    writeBytes(modPath, makeModPlacingStatsInBaseCell());
    writeBytes(patchPath, makeEmptyPatch());

    constexpr std::string_view k_obj = "o tri\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    const fo4::nif::ImportOptions options{.material = "PIPEMETAL:Materials\\metal\\PipeMetal.bgsm",
                                          .tex_diffuse = {},
                                          .tex_normal = {},
                                          .box_collision = false,
                                          .hull_collision = false,
                                          .mesh_collision = false,
                                          .body_mass = 0.0f,
                                          .from_blender = false,
                                          .skeleton = {}};
    writeBytes(dataDir / std::filesystem::path("Meshes/Rock.nif"), fo4::nif::import_obj(k_obj, options));
    const std::filesystem::path shell{FO4IDE_PREVIS_FIXTURES_DIR "/00000FEC_0A82376A_OC.NIF"};
    REQUIRE(std::filesystem::exists(shell));

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({pluginEntry("Base.esm", basePath), pluginEntry("Mod.esp", modPath),
                                           pluginEntry("Patch.esp", patchPath)})}};
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());
    backend.setDataDirectory(dataDir);

    const auto generated = backend.invokeBackend(
        "GenerateCellPrevis",
        nlohmann::json::array({std::string("Patch.esp"), std::string("000801:Base.esm"), std::string("Mod.esp"),
                               shell.string(), 2u, true}));
    if (!generated.has_value()) FAIL(generated.error().type + ": " + generated.error().message);
    CHECK_FALSE(generated->contains("cdx"));
    REQUIRE(generated->contains("complete"));
    CHECK((*generated)["complete"] == false);
    REQUIRE(generated->contains("ok"));
    CHECK((*generated)["ok"] == false);
    REQUIRE(generated->contains("limitations"));
    CHECK((*generated)["limitations"].size() >= 1u);
    CHECK_FALSE(generated->contains("csg"));
    REQUIRE(generated->contains("geometry"));
    CHECK_FALSE(std::filesystem::exists((*generated)["geometry"]["container"].get<std::string>()));
    REQUIRE(generated->contains("groups"));
    REQUIRE((*generated)["groups"].size() >= 1u);
    CHECK_FALSE(std::filesystem::exists((*generated)["groups"][0]["path"].get<std::string>()));
    REQUIRE(backend.invokeBackend("SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    auto saved = fo4::read_plugin_file(patchPath);
    fo4::assign_record_form_keys(saved);
    CHECK(fo4::find_record(saved, fo4::FormKey::parse("000801:Base.esm")) == nullptr);
}

TEST_CASE("Native GenerateCellPrevis marks an exterior cell without worldspace incomplete") {
    fo4ide_test::TempRoot root{"fo4ide_previs_no_worldspace"};
    const auto dataDir = root.path() / "data";
    const auto pluginDir = root.path() / "plugins";
    const auto sourcePath = pluginDir / "Source.esp";
    const auto patchPath = pluginDir / "Patch.esp";
    writeRockCellSource(sourcePath);
    writeBytes(patchPath, makeEmptyPatch());
    writeRockMesh(dataDir);
    const std::filesystem::path shell{FO4IDE_PREVIS_FIXTURES_DIR "/00000FEC_0A82376A_OC.NIF"};
    REQUIRE(std::filesystem::exists(shell));

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins",
         nlohmann::json::array({pluginEntry("Source.esp", sourcePath), pluginEntry("Patch.esp", patchPath)})}};
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());
    backend.setDataDirectory(dataDir);

    const auto generated = backend.invokeBackend(
        "GenerateCellPrevis",
        nlohmann::json::array({std::string("Patch.esp"), std::string("000801:Source.esp"),
                               std::string("Source.esp"), shell.string(), 1u, true}));
    if (!generated.has_value()) FAIL(generated.error().type + ": " + generated.error().message);
    CHECK((*generated)["complete"] == false);
    REQUIRE(generated->contains("limitations"));
    CHECK(std::ranges::any_of(
        (*generated)["limitations"],
        [](const nlohmann::json& item) {
            return item.is_string() && item.get<std::string>().find("worldspace") != std::string::npos;
        }));
}

// Detects: a cell whose RVIS names another cell shares that root's previs, and one run writes one cell's tome, so an apply must refuse the member before it writes any file or stages the patch, while a dry run must still report the plan and name the cluster root as a limitation.
TEST_CASE("Native GenerateCellPrevis refuses to apply a previs cluster member and a dry run lists it") {
    fo4ide_test::TempRoot root{"fo4ide_previs_cluster_member"};
    const auto dataDir = root.path() / "data";
    const auto pluginDir = root.path() / "plugins";
    const auto sourcePath = pluginDir / "Source.esp";
    const auto patchPath = pluginDir / "Patch.esp";
    // RVIS 0x802 names a cluster root other than this cell, 0x801.
    writeRockCellSource(sourcePath, {{.signature = "RVIS", .data = fo4::test::envnode::bytes_of({0x802u})}});
    writeBytes(patchPath, makeEmptyPatch());
    writeRockMesh(dataDir);
    const std::filesystem::path shell{FO4IDE_PREVIS_FIXTURES_DIR "/00000FEC_0A82376A_OC.NIF"};
    REQUIRE(std::filesystem::exists(shell));
    const auto patchBefore = readBytes(patchPath);

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins",
         nlohmann::json::array({pluginEntry("Source.esp", sourcePath), pluginEntry("Patch.esp", patchPath)})}};
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());
    backend.setDataDirectory(dataDir);
    const auto dataFilesBefore = regularFileCount(dataDir);

    const auto generated = backend.invokeBackend(
        "GenerateCellPrevis",
        nlohmann::json::array({std::string("Patch.esp"), std::string("000801:Source.esp"),
                               std::string("Source.esp"), shell.string(), 1u, true}));
    REQUIRE_FALSE(generated.has_value());
    CHECK(generated.error().type == "PrevisGenerateCluster");
    CHECK(generated.error().message.find("000802:Source.esp") != std::string::npos);
    CHECK(readBytes(patchPath) == patchBefore);
    CHECK(regularFileCount(dataDir) == dataFilesBefore);

    const auto planned = backend.invokeBackend(
        "GenerateCellPrevis",
        nlohmann::json::array({std::string("Patch.esp"), std::string("000801:Source.esp"),
                               std::string("Source.esp"), shell.string(), 1u, false}));
    if (!planned.has_value()) FAIL(planned.error().type + ": " + planned.error().message);
    CHECK((*planned)["dryRun"] == true);
    CHECK((*planned)["complete"] == false);
    REQUIRE(planned->contains("limitations"));
    const auto& limitations = (*planned)["limitations"];
    CHECK(std::ranges::any_of(limitations, [](const nlohmann::json& item) {
        return item == "previs_cluster_member:000802:Source.esp";
    }));
    REQUIRE_FALSE(limitations.empty());
    CHECK(limitations.back() == "dry_run_only");
    CHECK(readBytes(patchPath) == patchBefore);
    CHECK(regularFileCount(dataDir) == dataFilesBefore);
}

// Constraint: a cell owned by a master keeps its row in the owner's index, so a patch whose winning plugin ships no CDX must copy the row from the index that has it rather than leave the cell unindexed.
TEST_CASE("Native GenerateCellPrevis refuses to overwrite a validated Vis tome") {
    fo4ide_test::TempRoot root{"fo4ide_previs_tome_guard"};
    const auto dataDir = root.path() / "data";
    const auto pluginDir = root.path() / "plugins";
    const auto basePath = pluginDir / "Base.esm";
    const auto modPath = pluginDir / "Mod.esp";
    const auto patchPath = pluginDir / "Patch.esp";
    writeBytes(basePath, makeBaseCellMaster());
    writeBytes(modPath, makeModPlacingStatsInBaseCell());
    writeBytes(patchPath, makeEmptyPatch());

    constexpr std::string_view k_obj = "o tri\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    const fo4::nif::ImportOptions options{.material = "PIPEMETAL:Materials\\metal\\PipeMetal.bgsm",
                                          .tex_diffuse = {},
                                          .tex_normal = {},
                                          .box_collision = false,
                                          .hull_collision = false,
                                          .mesh_collision = false,
                                          .body_mass = 0.0f,
                                          .from_blender = false,
                                          .skeleton = {}};
    writeBytes(dataDir / std::filesystem::path("Meshes/Rock.nif"), fo4::nif::import_obj(k_obj, options));
    const std::filesystem::path shell{FO4IDE_PREVIS_FIXTURES_DIR "/00000FEC_0A82376A_OC.NIF"};
    REQUIRE(std::filesystem::exists(shell));

    fo4ide::plugin::previs_prep::TomeRequest seed;
    seed.path = dataDir / "Vis" / "Patch.esp" / "00000801.uvd";
    seed.bodies = {fo4::nif::PhysicsMeshInput{{0.0f, 0.0f, 0.0f, 10.0f, 0.0f, 0.0f, 0.0f, 10.0f, 5.0f}, {0, 1, 2}}};
    REQUIRE(fo4ide::plugin::previs_prep::write_tome(seed).has_value());

    fo4::previs::CdxFile baseCdx;
    baseCdx.rows.push_back({0u, 0x00000801u, 7u, 9u});
    writeBytes(dataDir / "Base.cdx", fo4::previs::write_cdx(baseCdx));

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({pluginEntry("Base.esm", basePath), pluginEntry("Mod.esp", modPath),
                                           pluginEntry("Patch.esp", patchPath)})}};
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());
    backend.setDataDirectory(dataDir);

    const auto generated = backend.invokeBackend(
        "GenerateCellPrevis",
        nlohmann::json::array({std::string("Patch.esp"), std::string("000801:Base.esm"), std::string("Mod.esp"),
                               shell.string(), 2u, true}));
    REQUIRE_FALSE(generated.has_value());
    CHECK(generated.error().type == "PrevisGenerateTome");
}

TEST_CASE("Native GenerateCellPrevis copies a CDX row from another loaded index when the winner ships none") {
    fo4ide_test::TempRoot root{"fo4ide_previs_cdx_fallback"};
    const auto dataDir = root.path() / "data";
    const auto pluginDir = root.path() / "plugins";
    const auto basePath = pluginDir / "Base.esm";
    const auto modPath = pluginDir / "Mod.esp";
    const auto patchPath = pluginDir / "Patch.esp";
    writeBytes(basePath, makeBaseCellMaster());
    writeBytes(modPath, makeModPlacingStatsInBaseCell());
    writeBytes(patchPath, makeEmptyPatch());
    fo4::previs::CdxFile baseCdx;
    baseCdx.rows.push_back({0u, 0x00000801u, 7u, 9u});
    writeBytes(dataDir / "Base.cdx", fo4::previs::write_cdx(baseCdx));

    constexpr std::string_view k_obj = "o tri\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    const fo4::nif::ImportOptions options{.material = "PIPEMETAL:Materials\\metal\\PipeMetal.bgsm",
                                          .tex_diffuse = {},
                                          .tex_normal = {},
                                          .box_collision = false,
                                          .hull_collision = false,
                                          .mesh_collision = false,
                                          .body_mass = 0.0f,
                                          .from_blender = false,
                                          .skeleton = {}};
    writeBytes(dataDir / std::filesystem::path("Meshes/Rock.nif"), fo4::nif::import_obj(k_obj, options));
    const std::filesystem::path shell{FO4IDE_PREVIS_FIXTURES_DIR "/00000FEC_0A82376A_OC.NIF"};
    REQUIRE(std::filesystem::exists(shell));

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({pluginEntry("Base.esm", basePath), pluginEntry("Mod.esp", modPath),
                                           pluginEntry("Patch.esp", patchPath)})}};
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());
    backend.setDataDirectory(dataDir);

    const std::string cellText = "000801:Base.esm";
    const auto generated = backend.invokeBackend(
        "GenerateCellPrevis",
        nlohmann::json::array({std::string("Patch.esp"), cellText, std::string("Mod.esp"), shell.string(), 2u,
                               true}));
    if (!generated.has_value()) FAIL(generated.error().type + ": " + generated.error().message);
    REQUIRE(generated->contains("cdx"));
    CHECK((*generated)["cdx"]["source"] == "Base.cdx");
    CHECK((*generated)["cdx"]["rows"] == 1);

    const auto parsed = fo4::previs::parse_cdx(readBytes(pluginDir / "Patch.cdx"));
    REQUIRE(parsed.rows.size() == 1u);
    CHECK(parsed.rows[0].form == 0x00000801u);
    CHECK(parsed.rows[0].key == 0u);
    CHECK(parsed.rows[0].value == 7u);
    CHECK(parsed.rows[0].extra == 9u);
}

// Constraint: an existing patch CDX that cannot be read must be refused before any file is written, so a failed run leaves the output folder absent and the CDX untouched.
TEST_CASE("Native GenerateCellPrevis refuses an unreadable patch CDX before writing any file") {
    fo4ide_test::TempRoot root{"fo4ide_previs_cdx_unreadable"};
    const auto dataDir = root.path() / "data";
    const auto pluginDir = root.path() / "plugins";
    const auto outputRoot = root.path() / "out";
    const auto basePath = pluginDir / "Base.esm";
    const auto modPath = pluginDir / "Mod.esp";
    const auto patchPath = pluginDir / "Patch.esp";
    writeBytes(basePath, makeBaseCellMaster());
    writeBytes(modPath, makeModPlacingStatsInBaseCell());
    writeBytes(patchPath, makeEmptyPatch());
    fo4::previs::CdxFile baseCdx;
    baseCdx.rows.push_back({0u, 0x00000801u, 7u, 9u});
    writeBytes(dataDir / "Base.cdx", fo4::previs::write_cdx(baseCdx));
    const std::vector<std::byte> garbage(32, std::byte{0x41});
    writeBytes(pluginDir / "Patch.cdx", garbage);

    constexpr std::string_view k_obj = "o tri\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    const fo4::nif::ImportOptions options{.material = "PIPEMETAL:Materials\\metal\\PipeMetal.bgsm",
                                          .tex_diffuse = {},
                                          .tex_normal = {},
                                          .box_collision = false,
                                          .hull_collision = false,
                                          .mesh_collision = false,
                                          .body_mass = 0.0f,
                                          .from_blender = false,
                                          .skeleton = {}};
    writeBytes(dataDir / std::filesystem::path("Meshes/Rock.nif"), fo4::nif::import_obj(k_obj, options));
    const std::filesystem::path shell{FO4IDE_PREVIS_FIXTURES_DIR "/00000FEC_0A82376A_OC.NIF"};
    REQUIRE(std::filesystem::exists(shell));

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({pluginEntry("Base.esm", basePath), pluginEntry("Mod.esp", modPath),
                                           pluginEntry("Patch.esp", patchPath)})}};
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());
    backend.setDataDirectory(dataDir);
    backend.setPrevisOutputRoot(outputRoot);

    const auto refused = backend.invokeBackend(
        "GenerateCellPrevis",
        nlohmann::json::array({std::string("Patch.esp"), std::string("000801:Base.esm"), std::string("Mod.esp"),
                               shell.string(), 2u, true}));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "PrevisGenerateCdx");
    CHECK_FALSE(std::filesystem::exists(outputRoot));
    CHECK(readBytes(pluginDir / "Patch.cdx") == garbage);
}

// Constraint: the CDX fallback must not stop after a fixed number of indexes without a row; each pad masters Base.esm so it counts as tried.
TEST_CASE("Native GenerateCellPrevis finds a CDX row behind many readable indexes that have none") {
    fo4ide_test::TempRoot root{"fo4ide_previs_cdx_fallback_many"};
    const auto dataDir = root.path() / "data";
    const auto pluginDir = root.path() / "plugins";
    const auto basePath = pluginDir / "Base.esm";
    const auto modPath = pluginDir / "Mod.esp";
    const auto lastPath = pluginDir / "Last.esp";
    const auto patchPath = pluginDir / "Patch.esp";
    writeBytes(basePath, makeBaseCellMaster());
    writeBytes(modPath, makeModPlacingStatsInBaseCell());
    writeBytes(lastPath, makeBaseMasteredIndexPlugin());
    writeBytes(patchPath, makeEmptyPatch());
    nlohmann::json loadOrder = nlohmann::json::array();
    loadOrder.push_back(pluginEntry("Base.esm", basePath));
    loadOrder.push_back(pluginEntry("Mod.esp", modPath));
    const auto emptyCdx = fo4::previs::write_cdx(fo4::previs::CdxFile{});
    for (int index = 0; index < 9; ++index) {
        const auto name = "Pad" + std::to_string(index) + ".esp";
        writeBytes(pluginDir / name, makeBaseMasteredIndexPlugin());
        writeBytes(dataDir / ("Pad" + std::to_string(index) + ".cdx"), emptyCdx);
        loadOrder.push_back(pluginEntry(name, pluginDir / name));
    }
    loadOrder.push_back(pluginEntry("Last.esp", lastPath));
    loadOrder.push_back(pluginEntry("Patch.esp", patchPath));
    fo4::previs::CdxFile lastCdx;
    lastCdx.rows.push_back({0u, 0x00000801u, 7u, 9u});
    writeBytes(dataDir / "Last.cdx", fo4::previs::write_cdx(lastCdx));

    constexpr std::string_view k_obj = "o tri\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    const fo4::nif::ImportOptions options{.material = "PIPEMETAL:Materials\\metal\\PipeMetal.bgsm",
                                          .tex_diffuse = {},
                                          .tex_normal = {},
                                          .box_collision = false,
                                          .hull_collision = false,
                                          .mesh_collision = false,
                                          .body_mass = 0.0f,
                                          .from_blender = false,
                                          .skeleton = {}};
    writeBytes(dataDir / std::filesystem::path("Meshes/Rock.nif"), fo4::nif::import_obj(k_obj, options));
    const std::filesystem::path shell{FO4IDE_PREVIS_FIXTURES_DIR "/00000FEC_0A82376A_OC.NIF"};
    REQUIRE(std::filesystem::exists(shell));

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", nlohmann::json{{"plugins", loadOrder}}).has_value());
    backend.setDataDirectory(dataDir);

    const auto generated = backend.invokeBackend(
        "GenerateCellPrevis",
        nlohmann::json::array({std::string("Patch.esp"), std::string("000801:Base.esm"), std::string("Mod.esp"),
                               shell.string(), 2u, true}));
    if (!generated.has_value()) FAIL(generated.error().type + ": " + generated.error().message);
    REQUIRE(generated->contains("cdx"));
    CHECK((*generated)["cdx"]["source"] == "Last.cdx");
    CHECK((*generated)["cdx"]["rows"] == 1);
}

// Detects: a 2-byte RVIS on a stamped exterior cell, which the previs scan reports as rvis_malformed. The
// stamp must write the cell as its own root in a 4-byte Form-ID.
TEST_CASE("Native StampCellPrevis writes a 4-byte RVIS root on an exterior cell") {
    fo4ide_test::TempRoot root{"fo4ide_previs_rvis"};
    const auto sourcePath = root.path() / "Source.esp";
    const auto patchPath = root.path() / "Patch.esp";
    writeBytes(sourcePath, makeSingleStatExteriorCellSource());
    writeBytes(patchPath, makeEmptyPatch());

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins",
         nlohmann::json::array({pluginEntry("Source.esp", sourcePath), pluginEntry("Patch.esp", patchPath)})}};
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const std::string cellText = "000801:Source.esp";
    const auto stamped = backend.invokeBackend(
        "StampCellPrevis",
        nlohmann::json::array({std::string("Patch.esp"), cellText, std::string("Source.esp"), "0x1234", "0x5678",
                               nlohmann::json::object({{"mesh_ids", nlohmann::json::array()},
                                                       {"refs", nlohmann::json::array()}}),
                               true}));
    if (!stamped.has_value()) FAIL(stamped.error().type + ": " + stamped.error().message);
    REQUIRE(backend.invokeBackend("SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    auto patch = fo4::read_plugin_file(patchPath);
    fo4::assign_record_form_keys(patch);
    const auto key = fo4::FormKey::parse(cellText);
    const auto* cell = fo4::find_record(patch, key);
    REQUIRE(cell != nullptr);
    const fo4::Subrecord* rvis = nullptr;
    std::size_t rvisCount = 0;
    for (const auto& sub : cell->subrecords) {
        if (sub.signature != fo4::Signature::from_string("RVIS")) continue;
        rvis = &sub;
        ++rvisCount;
    }
    REQUIRE(rvisCount == 1u);
    REQUIRE(rvis->data.size() == 4u);
    std::uint32_t raw = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        raw |= std::to_integer<std::uint32_t>(rvis->data[i]) << (8 * i);
    }
    CHECK(fo4::resolve_plugin_form_id(fo4::RawFormId{raw}, fo4::plugin_form_id_context(patch)) == key);
}

TEST_CASE("readVisTome resolves archived tomes in plugin load order") {
    fo4ide_test::TempRoot root{"fo4ide_read_vis_tome_order"};
    const auto dataDir = root.path() / "data";
    std::filesystem::create_directories(dataDir);
    auto archivePath = fo4::previs::uvd_tome_path(dataDir, "Fallout4.esm", 0x801u)
                           .lexically_relative(dataDir)
                           .generic_string();
    std::ranges::replace(archivePath, '/', '\\');
    const auto writeArchive = [&](const char* fileName, std::string_view payload) {
        fo4::archive::Ba2GeneralArchive archive;
        fo4::archive::Ba2GeneralEntry entry;
        entry.path = archivePath;
        std::vector<std::byte> bytes;
        bytes.reserve(payload.size());
        for (const char ch : payload) bytes.push_back(static_cast<std::byte>(ch));
        entry.chunks.push_back(fo4::archive::make_stored_ba2_chunk(bytes));
        archive.entries.push_back(std::move(entry));
        fo4::archive::write_ba2_general_file(archive, dataDir / fileName);
    };
    writeArchive("Loser - Main.ba2", "low");
    writeArchive("Winner - Main.ba2", "high");
    const std::vector<std::string> loadOrder{"Winner.esp", "Loser.esp"};
    const auto bytes = fo4ide::plugin::previs_assets::readVisTome(dataDir, "Fallout4.esm", 0x801u, loadOrder, {}, {});
    REQUIRE(bytes.has_value());
    REQUIRE(bytes->size() == 3u);
    CHECK((*bytes)[0] == std::byte{'l'});
}

TEST_CASE("readVisTome prefers an MO2 archive over a lower root loose tome") {
    fo4ide_test::TempRoot root{"fo4ide_read_vis_tome_mo2"};
    const auto dataDir = root.path() / "data";
    const auto modDir = root.path() / "mod";
    std::filesystem::create_directories(modDir);
    const auto path = fo4::previs::uvd_tome_path(dataDir, "Fallout4.esm", 0x801u);
    writeBytes(path, {std::byte{'l'}});
    auto archivePath = path.lexically_relative(dataDir).generic_string();
    std::ranges::replace(archivePath, '/', '\\');
    fo4::archive::Ba2GeneralArchive archive;
    fo4::archive::Ba2GeneralEntry entry;
    entry.path = archivePath;
    const std::vector<std::byte> expected{std::byte{'h'}};
    entry.chunks.push_back(fo4::archive::make_stored_ba2_chunk(expected));
    archive.entries.push_back(std::move(entry));
    fo4::archive::write_ba2_general_file(archive, modDir / "Mod - Main.ba2");
    const std::vector<std::string> loadOrder{"Mod.esp"};
    const std::vector<std::filesystem::path> roots{dataDir, modDir};
    CHECK(fo4ide::plugin::previs_assets::readVisTome(dataDir, "Fallout4.esm", 0x801u, loadOrder, roots) == expected);
}

// Constraint: disable must refuse a cell with no PCMB even when source_plugin is named.
TEST_CASE("Native DisableCellPrevis refuses an explicit source with no PCMB") {
    fo4ide_test::TempRoot root{"fo4ide_previs_disable_refuse"};
    const auto sourcePath = root.path() / "Source.esp";
    const auto patchPath = root.path() / "Patch.esp";
    writeBytes(sourcePath, makeSingleStatExteriorCellSource());
    writeBytes(patchPath, makeEmptyPatch());

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins",
         nlohmann::json::array({pluginEntry("Source.esp", sourcePath), pluginEntry("Patch.esp", patchPath)})}};
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const auto disabled = backend.invokeBackend(
        "DisableCellPrevis",
        nlohmann::json::array({std::string("000801:Source.esp"), std::string("Patch.esp"), std::string("Source.esp"), true}));
    if (!disabled.has_value()) FAIL(disabled.error().type + ": " + disabled.error().message);
    CHECK(disabled->get<std::string>() == "Nothing to disable: 000801:Source.esp has no PCMB in Source.esp.");
}

// Constraint: a cell stamped into the patch must be checked on the patch copy, not on the source.
TEST_CASE("Native DisableCellPrevis checks the patch's stamped cell when the source has no PCMB") {
    fo4ide_test::TempRoot root{"fo4ide_previs_disable_stamped"};
    const auto sourcePath = root.path() / "Source.esp";
    const auto patchPath = root.path() / "Patch.esp";
    writeBytes(sourcePath, makeSingleStatExteriorCellSource());
    writeBytes(patchPath, makeEmptyPatch());

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins",
         nlohmann::json::array({pluginEntry("Source.esp", sourcePath), pluginEntry("Patch.esp", patchPath)})}};
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const std::string cellText = "000801:Source.esp";
    const auto stamped = backend.invokeBackend(
        "StampCellPrevis",
        nlohmann::json::array({std::string("Patch.esp"), cellText, std::string("Source.esp"), "0x1234", "0x5678",
                               nlohmann::json::object({{"mesh_ids", nlohmann::json::array()},
                                                       {"refs", nlohmann::json::array()}}),
                               true}));
    if (!stamped.has_value()) FAIL(stamped.error().type + ": " + stamped.error().message);

    const auto disabled = backend.invokeBackend(
        "DisableCellPrevis", nlohmann::json::array({cellText, std::string("Patch.esp"), std::string("Source.esp"), true}));
    if (!disabled.has_value()) FAIL(disabled.error().type + ": " + disabled.error().message);
    REQUIRE(backend.invokeBackend("SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    auto patch = fo4::read_plugin_file(patchPath);
    fo4::assign_record_form_keys(patch);
    const auto* cell = fo4::find_record(patch, fo4::FormKey::parse(cellText));
    REQUIRE(cell != nullptr);
    CHECK((cell->header.flags.value() & (1u << 7)) != 0u);
    for (const auto& sub : cell->subrecords) {
        CHECK(sub.signature != fo4::Signature::from_string("PCMB"));
        CHECK(sub.signature != fo4::Signature::from_string("VISI"));
        CHECK(sub.signature != fo4::Signature::from_string("RVIS"));
    }
}
