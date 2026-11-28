#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <fo4/nif/nif.hpp>
#include <fo4/nif/nif_import.hpp>
#include <fo4/nif/shape_mesh.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/previs/cdx.hpp>
#include <fo4/previs/csg.hpp>
#include <fo4/previs/lod_levels.hpp>
#include <fo4/previs/shared_geom_extra.hpp>
#include <fo4/previs/uvd.hpp>
#include <fo4/previs/uvd_tile_build.hpp>
#include <fo4/previs/uvd_tile_header.hpp>
#include <fo4/previs/uvd_tile_model.hpp>
#include <fo4/signature.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "../helpers/TempRoot.hpp"
#include "../../src/plugin/RadiclePluginPrevisPrep.hpp"
#include "env_node_fixture.hpp"
#include "plugin_bytes.hpp"

namespace {

void writeBytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void writeWorldCdx(const std::filesystem::path& dataDir, const std::string& stem) {
    fo4::previs::CdxFile file;
    file.rows.push_back({0u, 0x00000800u, 7u, 9u});
    writeBytes(dataDir / (stem + ".cdx"), fo4::previs::write_cdx(file));
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

nlohmann::json pluginEntry(std::string name, const std::filesystem::path& path) {
    return {{"name", std::move(name)}, {"path", path.string()}, {"source", "test"}, {"resolved", true}};
}

namespace en = fo4::test::envnode;

en::Node staticRef(std::uint32_t form, std::uint32_t base) {
    return en::rec("REFR", form,
                   {{.signature = "NAME", .data = en::bytes_of({base})},
                    {.signature = "DATA", .data = en::floats({0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f})}});
}

// Constraint: the pinned cell is an exterior grid cell, so the fixture needs the world, block and temporary groups.
en::Node exteriorCellGroup(const std::vector<std::uint32_t>& bases, std::uint32_t cellForm = 0x801u, bool precombined = false) {
    const auto world_children = static_cast<std::int32_t>(fo4::GroupType::world_children);
    const auto block = static_cast<std::int32_t>(fo4::GroupType::exterior_cell_block);
    const auto subblock = static_cast<std::int32_t>(fo4::GroupType::exterior_cell_subblock);
    const auto cell_children = static_cast<std::int32_t>(fo4::GroupType::cell_children);
    const auto cell_temporary = static_cast<std::int32_t>(fo4::GroupType::cell_temporary_children);
    std::vector<en::Node> refs;
    std::uint32_t form = 0x820u;
    for (const auto base : bases) refs.push_back(staticRef(form++, base));
    std::vector<en::Sub> cellSubs{{.signature = "DATA", .data = {std::byte{0}}},
                                  {.signature = "XCLC", .data = en::bytes_of({0u, 0u, 0u})}};
    // Constraint: the CELL reader requires PCMB to be exactly 2 bytes, so a wider stamp fails the plan before any gate runs.
    if (precombined) cellSubs.push_back({.signature = "PCMB", .data = {std::byte{0x34}, std::byte{0x12}}});
    return en::top("WRLD",
                   {en::rec("WRLD", 0x800u, {{.signature = "EDID", .data = en::zstring("Wilderness")}}),
                    en::grp(0x800u, world_children,
                            {en::grp(0u, block,
                                     {en::grp(0u, subblock,
                                              {en::rec("CELL", cellForm, cellSubs),
                                               en::grp(cellForm, cell_children,
                                                       {en::grp(cellForm, cell_temporary, refs)})})})})});
}

std::vector<std::byte> makeStatCellSource(const std::vector<std::string>& models, std::uint32_t cellForm = 0x801u, bool precombined = false) {
    std::vector<en::Node> stats;
    std::vector<std::uint32_t> bases;
    std::uint32_t form = 0x810u;
    for (const auto& model : models) {
        stats.push_back(en::rec("STAT", form, {{.signature = "MODL", .data = en::zstring(model)}}));
        bases.push_back(form++);
    }
    return en::make_plugin({}, {en::top("STAT", stats), exteriorCellGroup(bases, cellForm, precombined)});
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

// Constraint: a fan gives the weld grid enough triangles for a pinned fraction to be expressible on it.
std::string fanObj(std::size_t triangles) {
    std::string obj = "o tri\nv 0 0 0\n";
    for (std::size_t i = 0; i < triangles + 1; ++i) {
        const auto angle = 6.283185307179586 * static_cast<double>(i) / static_cast<double>(triangles);
        obj += "v " + std::to_string(50.0 * std::cos(angle)) + " " + std::to_string(50.0 * std::sin(angle)) + " 0\n";
    }
    for (std::size_t i = 1; i < triangles + 1; ++i) {
        obj += "f 1 " + std::to_string(i + 1) + " " + std::to_string(i + 2) + "\n";
    }
    return obj;
}

// Constraint: an expectation must use the narrowed half positions the generator writes.
std::vector<float> writtenPositions(const std::vector<std::byte>& nifBytes) {
    const auto shape = fo4::nif::read_first_shape_mesh(fo4::nif::read_nif(nifBytes));
    if (!shape.has_value()) return {};
    return fo4::previs::csg_mesh_positions(fo4::previs::csg_mesh_from_shape(*shape));
}

const fo4::nif::ImportOptions kOptions{.material = "PIPEMETAL:Materials\\metal\\PipeMetal.bgsm",
                                       .tex_diffuse = {},
                                       .tex_normal = {},
                                       .box_collision = false,
                                       .hull_collision = false,
                                       .mesh_collision = false,
                                       .body_mass = 0.0f,
                                       .from_blender = false,
                                       .skeleton = {}};

std::string cellTextOf(const std::filesystem::path& sourcePath) {
    auto source = fo4::read_plugin_file(sourcePath);
    fo4::assign_record_form_keys(source);
    for (const auto* rec : fo4::collect_records(source)) {
        if (rec->header.signature == fo4::Signature::from_string("CELL")) return rec->form_key.to_string();
    }
    return {};
}

nlohmann::json groupWithPath(const nlohmann::json& plan, std::string_view suffix) {
    for (const auto& group : plan.at("groups")) {
        const auto path = group.at("modelPath").get<std::string>();
        if (path.size() >= suffix.size() && path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0) {
            return group;
        }
    }
    return nlohmann::json{};
}

// Constraint: the _OC object record repeats the emitted level triangle counts, so the shipped file is what to read.
std::array<std::uint32_t, fo4::previs::k_previs_lod_levels> ocTriangleCounts(const std::filesystem::path& ocPath) {
    const auto bytes = readBytes(ocPath);
    const auto nif = fo4::nif::read_nif(bytes);
    for (const auto& block : nif.blocks()) {
        if (block.type != "BSPackedCombinedSharedGeomDataExtra") continue;
        const auto header = fo4::previs::read_shared_geom_header(
            std::span<const std::byte>(bytes).subspan(block.offset, block.size));
        REQUIRE(header.has_value());
        REQUIRE(header->objects.size() == 1u);
        const auto& object = header->objects.front();
        return {object.triangle_count[0], object.triangle_count[1], object.triangle_count[2]};
    }
    FAIL("the emitted _OC carries no shared geometry extra");
    return {};
}

}  // namespace

// Regression: a request naming an object's fractions must reach its plan group after key folding, and pin nothing else.
TEST_CASE("Native PrecombinePlan pins a requested object's LOD ratios") {
    fo4ide_test::TempRoot root{"fo4ide_previs_lod_plan"};
    const auto dataDir = root.path() / "data";
    const auto pluginDir = root.path() / "plugins";
    const auto sourcePath = pluginDir / "Source.esp";
    const auto patchPath = pluginDir / "Patch.esp";
    writeBytes(sourcePath, makeStatCellSource({"Meshes/Rock.nif", "Meshes/Pipe.nif"}));
    writeBytes(patchPath, makeEmptyPatch());
    writeBytes(dataDir / std::filesystem::path("Meshes/Rock.nif"), fo4::nif::import_obj(fanObj(200), kOptions));
    writeBytes(dataDir / std::filesystem::path("Meshes/Pipe.nif"), fo4::nif::import_obj(fanObj(200), kOptions));

    const auto cellText = cellTextOf(sourcePath);
    REQUIRE(!cellText.empty());

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins",
         nlohmann::json::array({pluginEntry("Source.esp", sourcePath), pluginEntry("Patch.esp", patchPath)})}};
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());
    backend.setDataDirectory(dataDir);

    // Constraint: the request spells the key in another case with forward slashes, so the key fold is what matches it.
    const auto arrayForm = backend.invokeBackend(
        "PrecombinePlan", nlohmann::json::array({cellText, std::string("Source.esp"), 1u,
                                                 nlohmann::json::parse(R"({"meshes/rock.nif": [0.42, 0.18]})")}));
    if (!arrayForm.has_value()) FAIL(arrayForm.error().type + ": " + arrayForm.error().message);
    REQUIRE((*arrayForm).contains("groups"));
    const auto rock = groupWithPath(*arrayForm, "Rock.nif");
    REQUIRE_FALSE(rock.is_null());
    REQUIRE(rock.contains("lodTarget"));
    CHECK(rock["lodTarget"]["level1"].get<float>() == 0.42f);
    CHECK(rock["lodTarget"]["level2"].get<float>() == 0.18f);
    const auto pipeUnpinned = groupWithPath(*arrayForm, "Pipe.nif");
    REQUIRE_FALSE(pipeUnpinned.is_null());
    CHECK_FALSE(pipeUnpinned.contains("lodTarget"));

    const auto objectForm = backend.invokeBackend(
        "PrecombinePlan",
        nlohmann::json::array({cellText, std::string("Source.esp"), 1u,
                               nlohmann::json::parse(R"({"MESHES\\PIPE.NIF": {"level1": 0.4, "level2": 0.2}})")}));
    if (!objectForm.has_value()) FAIL(objectForm.error().type + ": " + objectForm.error().message);
    const auto pipe = groupWithPath(*objectForm, "Pipe.nif");
    REQUIRE_FALSE(pipe.is_null());
    REQUIRE(pipe.contains("lodTarget"));
    CHECK(pipe["lodTarget"]["level1"].get<float>() == 0.4f);
    CHECK(pipe["lodTarget"]["level2"].get<float>() == 0.2f);
    CHECK_FALSE(groupWithPath(*objectForm, "Rock.nif").contains("lodTarget"));

    // Constraint: a key that names no planned object pins nothing rather than failing the whole plan.
    const auto unknown = backend.invokeBackend(
        "PrecombinePlan", nlohmann::json::array({cellText, std::string("Source.esp"), 1u,
                                                 nlohmann::json::parse(R"({"meshes/missing.nif": [0.5, 0.25]})")}));
    if (!unknown.has_value()) FAIL(unknown.error().type + ": " + unknown.error().message);
    for (const auto& group : (*unknown).at("groups")) CHECK_FALSE(group.contains("lodTarget"));
}

TEST_CASE("Native PrecombinePlan bounds its groups and drops instances on request") {
    fo4ide_test::TempRoot root{"fo4ide_previs_plan_bounded"};
    const auto dataDir = root.path() / "data";
    const auto pluginDir = root.path() / "plugins";
    const auto sourcePath = pluginDir / "Source.esp";
    const auto patchPath = pluginDir / "Patch.esp";
    const std::vector<en::Node> stats{
        en::rec("STAT", 0x810u, {{.signature = "MODL", .data = en::zstring("Meshes/Pipe.nif")}}),
        en::rec("STAT", 0x811u, {{.signature = "MODL", .data = en::zstring("Meshes/Post.nif")}}),
        en::rec("STAT", 0x812u, {{.signature = "MODL", .data = en::zstring("Meshes/Rock.nif")}})};
    writeBytes(sourcePath, en::make_plugin({}, {en::top("STAT", stats),
                                                exteriorCellGroup({0x810u, 0x811u, 0x811u, 0x812u, 0x812u, 0x812u})}));
    writeBytes(patchPath, makeEmptyPatch());
    std::filesystem::create_directories(dataDir);

    const auto cellText = cellTextOf(sourcePath);
    REQUIRE(!cellText.empty());

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins",
         nlohmann::json::array({pluginEntry("Source.esp", sourcePath), pluginEntry("Patch.esp", patchPath)})}};
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());
    backend.setDataDirectory(dataDir);

    const auto plan = [&](nlohmann::json args) {
        auto result = backend.invokeBackend("PrecombinePlan", args);
        if (!result.has_value()) FAIL(result.error().type + ": " + result.error().message);
        return *result;
    };
    const auto full = plan(nlohmann::json::array({cellText, std::string("Source.esp"), 1u}));
    REQUIRE(full["groups"].size() == 3u);
    CHECK(full["groupCount"] == 3);
    CHECK(full["eligibleReferences"] == 6);
    CHECK(full["groupsShown"] == 3);
    CHECK(full["groupsOmitted"] == 0);
    CHECK(full["skipped"] == nlohmann::json({{"scripted", 0}, {"enableParent", 0}, {"foreignPlugin", 0}, {"unresolvedPlugin", 0}}));
    CHECK(full["groups"][0]["instanceCount"] == 1);
    CHECK(full["groups"][1]["instanceCount"] == 2);
    CHECK(full["groups"][2]["instanceCount"] == 3);
    CHECK(full["groups"][2]["instances"].size() == 3u);

    const auto nulls = nlohmann::json(nullptr);
    CHECK(plan(nlohmann::json::array({cellText, std::string("Source.esp"), 1u, nulls, nulls, nulls})) == full);
    CHECK(plan(nlohmann::json::array({cellText, std::string("Source.esp"), 1u, nulls, 0u, true})) == full);

    const auto limited = plan(nlohmann::json::array({cellText, std::string("Source.esp"), 1u, nulls, 2u}));
    CHECK(limited["groupCount"] == 3);
    CHECK(limited["eligibleReferences"] == 6);
    CHECK(limited["groupsShown"] == 2);
    CHECK(limited["groupsOmitted"] == 1);
    REQUIRE(limited["groups"].size() == 2u);
    CHECK(limited["groups"][0] == full["groups"][0]);
    CHECK(limited["groups"][1] == full["groups"][1]);

    const auto oversized = plan(nlohmann::json::array({cellText, std::string("Source.esp"), 1u, nulls, 10u}));
    CHECK(oversized["groupsShown"] == 3);
    CHECK(oversized["groupsOmitted"] == 0);
    CHECK(oversized["groups"] == full["groups"]);

    const auto counts = plan(nlohmann::json::array({cellText, std::string("Source.esp"), 1u, nulls, 2u, false}));
    CHECK(counts["eligibleReferences"] == 6);
    CHECK(counts["groupsShown"] == 2);
    CHECK(counts["groupsOmitted"] == 1);
    REQUIRE(counts["groups"].size() == 2u);
    CHECK(counts["groups"][0]["modelPath"] == full["groups"][0]["modelPath"]);
    CHECK(counts["groups"][0]["instanceCount"] == 1);
    CHECK(counts["groups"][0]["instances"].empty());
    CHECK(counts["groups"][1]["instanceCount"] == 2);
    CHECK(counts["groups"][1]["instances"].empty());

    const auto noInstances = plan(nlohmann::json::array({cellText, std::string("Source.esp"), 1u, nulls, nulls, false}));
    REQUIRE(noInstances["groups"].size() == 3u);
    CHECK(noInstances["groups"][2]["instanceCount"] == 3);
    CHECK(noInstances["groups"][2]["instances"].empty());
    CHECK(noInstances["groupsOmitted"] == 0);
}

// Regression: a generate request must emit the requested level counts, and a request without targets must not move.
TEST_CASE("Native GenerateCellPrevis emits the requested LOD ratios") {
    fo4ide_test::TempRoot root{"fo4ide_previs_lod_generate"};
    const auto dataDir = root.path() / "data";
    const auto pluginDir = root.path() / "plugins";
    const auto sourcePath = pluginDir / "Source.esp";
    const auto patchPath = pluginDir / "Patch.esp";
    writeBytes(sourcePath, makeStatCellSource({"Meshes/Rock.nif"}));
    writeBytes(patchPath, makeEmptyPatch());
    writeWorldCdx(dataDir, "Source");
    const auto meshBytes = fo4::nif::import_obj(fanObj(200), kOptions);
    writeBytes(dataDir / std::filesystem::path("Meshes/Rock.nif"), meshBytes);
    const std::filesystem::path shell{FO4IDE_PREVIS_FIXTURES_DIR "/00000FEC_0A82376A_OC.NIF"};
    REQUIRE(std::filesystem::exists(shell));

    const auto cellText = cellTextOf(sourcePath);
    REQUIRE(!cellText.empty());

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins",
         nlohmann::json::array({pluginEntry("Source.esp", sourcePath), pluginEntry("Patch.esp", patchPath)})}};
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());
    backend.setDataDirectory(dataDir);

    backend.setPrevisOutputRoot(root.path() / "pinned");
    const auto pinned = backend.invokeBackend(
        "GenerateCellPrevis",
        nlohmann::json::array({std::string("Patch.esp"), cellText, std::string("Source.esp"), shell.string(), 1u, true,
                               false, nlohmann::json::parse(R"({"Meshes/Rock.nif": [0.5, 0.25]})")}));
    if (!pinned.has_value()) FAIL(pinned.error().type + ": " + pinned.error().message);
    REQUIRE((*pinned).contains("csg"));
    REQUIRE((*pinned)["groups"].size() == 1u);
    const auto pinnedCounts = ocTriangleCounts((*pinned)["groups"][0]["path"].get<std::string>());
    REQUIRE(pinnedCounts[0] >= 50u);
    const auto pinnedRatio1 = static_cast<double>(pinnedCounts[1]) / static_cast<double>(pinnedCounts[0]);
    const auto pinnedRatio2 = static_cast<double>(pinnedCounts[2]) / static_cast<double>(pinnedCounts[0]);
    CHECK(std::fabs(pinnedRatio1 - 0.5) <= 0.06);
    CHECK(std::fabs(pinnedRatio2 - 0.25) <= 0.06);

    // Constraint: without a target map the emitted counts must stay the unpinned reduce_mesh_levels result.
    const auto nif = fo4::nif::read_nif(meshBytes);
    const auto shape = fo4::nif::read_first_shape_mesh(nif);
    REQUIRE(shape.has_value());
    const auto positions = writtenPositions(meshBytes);
    REQUIRE(!positions.empty());
    const auto expected = fo4::previs::reduce_mesh_levels(positions, shape->indices);

    backend.setPrevisOutputRoot(root.path() / "unpinned");
    const auto unpinned = backend.invokeBackend(
        "GenerateCellPrevis",
        nlohmann::json::array({std::string("Patch.esp"), cellText, std::string("Source.esp"), shell.string(), 1u, true,
                               false}));
    if (!unpinned.has_value()) FAIL(unpinned.error().type + ": " + unpinned.error().message);
    const auto unpinnedCounts = ocTriangleCounts((*unpinned)["groups"][0]["path"].get<std::string>());
    CHECK(unpinnedCounts[0] == expected[0].triangles.size() / 3u);
    CHECK(unpinnedCounts[1] == expected[1].triangles.size() / 3u);
    CHECK(unpinnedCounts[2] == expected[2].triangles.size() / 3u);

    // Constraint: a fraction outside (0, 1) names no reduction, so both levels invalid leave the edge rule standing.
    backend.setPrevisOutputRoot(root.path() / "invalid");
    const auto invalid = backend.invokeBackend(
        "GenerateCellPrevis",
        nlohmann::json::array({std::string("Patch.esp"), cellText, std::string("Source.esp"), shell.string(), 1u, true,
                               false, nlohmann::json::parse(R"({"Meshes/Rock.nif": {"level1": 2.0, "level2": 0.0}})")}));
    if (!invalid.has_value()) FAIL(invalid.error().type + ": " + invalid.error().message);
    const auto invalidCounts = ocTriangleCounts((*invalid)["groups"][0]["path"].get<std::string>());
    CHECK(invalidCounts == unpinnedCounts);

    // Constraint: this fan's coarsest weld requires two triangles, so a request for one lands on the nearest reachable count.
    backend.setPrevisOutputRoot(root.path() / "floor");
    const auto floor = backend.invokeBackend(
        "GenerateCellPrevis",
        nlohmann::json::array({std::string("Patch.esp"), cellText, std::string("Source.esp"), shell.string(), 1u, true,
                               false, nlohmann::json::parse(R"({"Meshes/Rock.nif": {"level1": 0.5, "level2": 0.004}})")}));
    if (!floor.has_value()) FAIL(floor.error().type + ": " + floor.error().message);
    const auto floorCounts = ocTriangleCounts((*floor)["groups"][0]["path"].get<std::string>());
    CHECK(floorCounts[2] >= 1u);
    CHECK(floorCounts[2] <= 2u);
    CHECK(floorCounts[1] > floorCounts[2]);
}

namespace {

std::vector<std::uint32_t> tomeObjectIds(const fo4::previs::UvdTileModel& model) {
    return fo4::previs::decode_uvd_header(model).objects.ids;
}

}  // namespace

// Regression: generation that still emits the retired geometry-page tome writes bytes the tile model cannot parse, drops or merges a body's object, or leaves the tome unequal between two identical runs.
// Constraint: a cell's own template ships inside a Data archive, so generate must find it without a typed path, and a run whose caller named no template must still refuse another cell's shell rather than borrow one.
TEST_CASE("Native GenerateCellPrevis resolves the cell's own _OC template from Data") {
    fo4ide_test::TempRoot root{"fo4ide_previs_shell_resolve"};
    const auto dataDir = root.path() / "data";
    const auto pluginDir = root.path() / "plugins";
    const auto sourcePath = pluginDir / "Source.esp";
    const auto patchPath = pluginDir / "Patch.esp";
    writeBytes(sourcePath, makeStatCellSource({"Meshes/Rock.nif"}));
    writeBytes(patchPath, makeEmptyPatch());
    writeWorldCdx(dataDir, "Source");
    writeBytes(dataDir / std::filesystem::path("Meshes/Rock.nif"), fo4::nif::import_obj(fanObj(200), kOptions));
    const std::filesystem::path shell{FO4IDE_PREVIS_FIXTURES_DIR "/00000FEC_0A82376A_OC.NIF"};
    REQUIRE(std::filesystem::exists(shell));
    const auto cellText = cellTextOf(sourcePath);
    REQUIRE(!cellText.empty());

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins",
         nlohmann::json::array({pluginEntry("Source.esp", sourcePath), pluginEntry("Patch.esp", patchPath)})}};
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());
    backend.setDataDirectory(dataDir);
    backend.setPrevisOutputRoot(root.path() / "run");
    CHECK(backend.previsOutputRoot() == root.path() / "run");

    const auto generate = [&](std::string templateText) {
        return backend.invokeBackend(
            "GenerateCellPrevis",
            nlohmann::json::array({std::string("Patch.esp"), cellText, std::string("Source.esp"),
                                   std::move(templateText), 1u, true, false}));
    };
    const auto nothingToFind = generate("");
    REQUIRE_FALSE(nothingToFind.has_value());
    CHECK(nothingToFind.error().type == "PrevisGenerateShell");

    // Constraint: another plugin's shell for the same cell number must never be taken as this cell's template.
    writeBytes(dataDir / "Meshes" / "PreCombined" / "Other.esp" / "00000801_00000002_OC.NIF", readBytes(shell));
    const auto otherPluginOnly = generate("");
    REQUIRE_FALSE(otherPluginOnly.has_value());
    CHECK(otherPluginOnly.error().type == "PrevisGenerateShell");

    // Constraint: the fixture pins the cell at local 0x801, so its own templates are the ones named 00000801.
    writeBytes(dataDir / "Meshes" / "PreCombined" / "Source.esp" / "00000801_00000001_OC.NIF", readBytes(shell));
    const auto resolved = generate("");
    if (!resolved.has_value()) FAIL(resolved.error().type + ": " + resolved.error().message);
    REQUIRE(resolved->contains("uvd"));
    REQUIRE(resolved->contains("shell"));
    CHECK((*resolved)["shell"]["autoResolved"] == true);
    CHECK((*resolved)["shell"]["candidates"] == 1);
    CHECK((*resolved)["shell"]["path"].get<std::string>().find("00000801_00000001_OC.NIF") != std::string::npos);
    CHECK(fo4::previs::validate_uvd_tome_tiled(readBytes((*resolved)["uvd"]["path"].get<std::string>())).empty());

    const auto missing = generate((root.path() / "no-such-shell.nif").string());
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().type == "PrevisGenerateShell");
}

TEST_CASE("Native GenerateCellPrevis writes a conservative tile-model tome with one object per body") {
    fo4ide_test::TempRoot root{"fo4ide_previs_tome_generate"};
    const auto dataDir = root.path() / "data";
    const auto pluginDir = root.path() / "plugins";
    const auto sourcePath = pluginDir / "Source.esp";
    const auto patchPath = pluginDir / "Patch.esp";
    writeBytes(sourcePath, makeStatCellSource({"Meshes/Rock.nif", "Meshes/Pipe.nif"}));
    writeBytes(patchPath, makeEmptyPatch());
    writeWorldCdx(dataDir, "Source");
    const auto meshBytes = fo4::nif::import_obj(fanObj(200), kOptions);
    writeBytes(dataDir / std::filesystem::path("Meshes/Rock.nif"), meshBytes);
    writeBytes(dataDir / std::filesystem::path("Meshes/Pipe.nif"), meshBytes);
    const std::filesystem::path shell{FO4IDE_PREVIS_FIXTURES_DIR "/00000FEC_0A82376A_OC.NIF"};
    REQUIRE(std::filesystem::exists(shell));

    const auto cellText = cellTextOf(sourcePath);
    REQUIRE(!cellText.empty());

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins",
         nlohmann::json::array({pluginEntry("Source.esp", sourcePath), pluginEntry("Patch.esp", patchPath)})}};
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());
    backend.setDataDirectory(dataDir);

    std::vector<std::vector<std::byte>> tomes;
    for (const auto* run : {"first", "second"}) {
        backend.setPrevisOutputRoot(root.path() / run);
        const auto generated = backend.invokeBackend(
            "GenerateCellPrevis",
            nlohmann::json::array({std::string("Patch.esp"), cellText, std::string("Source.esp"), shell.string(), 1u,
                                   true, false}));
        if (!generated.has_value()) FAIL(generated.error().type + ": " + generated.error().message);
        REQUIRE((*generated).contains("uvd"));
        CHECK((*generated)["uvd"]["objects"].get<std::size_t>() == 2u);
        // Constraint: two coincident bodies could each look like the other's shell, so the cull must skip a body's own triangles and drop neither.
        CHECK((*generated)["uvd"]["culled"].get<std::size_t>() == 0u);
        for (const auto& warning : (*generated).value("warnings", nlohmann::json::array())) {
            CHECK(warning.get<std::string>().find("tome") == std::string::npos);
        }
        tomes.push_back(readBytes((*generated)["uvd"]["path"].get<std::string>()));
        CHECK((*generated)["uvd"]["bytes"].get<std::size_t>() == tomes.back().size());
    }
    CHECK(tomes[0] == tomes[1]);

    const auto& bytes = tomes[0];
    CHECK(fo4::previs::validate_uvd_tome_tiled(bytes).empty());
    const auto model = fo4::previs::parse_uvd_tiles(bytes);
    CHECK(model.slots.size() == 1u);
    auto ids = tomeObjectIds(model);
    std::sort(ids.begin(), ids.end());
    CHECK(ids == std::vector<std::uint32_t>{0xFD000000u, 0xFD000001u});

    const auto positions = writtenPositions(meshBytes);
    const auto expectedBox = fo4::previs::uvd_positions_box(positions);
    REQUIRE(expectedBox.has_value());
    const auto header = fo4::previs::decode_uvd_header(model);
    REQUIRE(header.objects.boxes.size() == 2u);
    for (const auto& box : header.objects.boxes) CHECK(box == *expectedBox);

    // Constraint: the exterior grid cell (0, 0) is a 4096-unit square, so the world box must cover it as well as both bodies.
    CHECK(header.world_min[0] <= 0.0f);
    CHECK(header.world_min[1] <= 0.0f);
    CHECK(header.world_max[0] >= 4096.0f);
    CHECK(header.world_max[1] >= 4096.0f);
}

// Regression: a body without finite positions must be dropped with a warning (not corrupt the whole tome), the surviving bodies keep ids from their own index, and an interior cell's world box must be only what its bodies cover.
TEST_CASE("Native write_tome skips an unusable body with a warning and numbers the rest by body index") {
    fo4ide_test::TempRoot root{"fo4ide_previs_tome_bodies"};
    using fo4ide::plugin::previs_prep::TomeRequest;
    TomeRequest request;
    request.path = root.path() / "Vis" / "Patch.esp" / "00000800.uvd";
    request.bodies = {fo4::nif::PhysicsMeshInput{{0.0f, 0.0f, 0.0f, 10.0f, 0.0f, 0.0f, 0.0f, 10.0f, 5.0f}, {0, 1, 2}},
                      fo4::nif::PhysicsMeshInput{{0.0f, std::numeric_limits<float>::quiet_NaN(), 0.0f}, {0, 0, 0}},
                      fo4::nif::PhysicsMeshInput{{100.0f, 100.0f, 0.0f, 110.0f, 100.0f, 0.0f, 100.0f, 110.0f, 7.0f}, {0, 1, 2}}};

    const auto first = fo4ide::plugin::previs_prep::write_tome(request);
    REQUIRE(first.has_value());
    CHECK(first->objects == 2u);
    CHECK_FALSE(first->copied_vanilla);
    REQUIRE(first->warnings.size() == 1u);
    CHECK(first->warnings.front().find("body 1") != std::string::npos);

    const auto bytes = readBytes(request.path);
    CHECK(first->bytes == bytes.size());
    CHECK(fo4::previs::validate_uvd_tome_tiled(bytes).empty());
    const auto header = fo4::previs::decode_uvd_header(fo4::previs::parse_uvd_tiles(bytes));
    CHECK(header.objects.ids == std::vector<std::uint32_t>{0xFD000000u, 0xFD000002u});
    CHECK(header.world_min == std::array<float, 3>{0.0f, 0.0f, 0.0f});
    CHECK(header.world_max == std::array<float, 3>{110.0f, 110.0f, 7.0f});

    // Constraint: an exterior footprint widens x and y only, and z stays what the bodies cover.
    request.cell_xy = std::array<float, 4>{-4096.0f, 0.0f, 0.0f, 4096.0f};
    request.path = root.path() / "Vis" / "Patch.esp" / "exterior.uvd";
    const auto exterior = fo4ide::plugin::previs_prep::write_tome(request);
    REQUIRE(exterior.has_value());
    const auto exteriorHeader =
        fo4::previs::decode_uvd_header(fo4::previs::parse_uvd_tiles(readBytes(request.path)));
    CHECK(exteriorHeader.world_min == std::array<float, 3>{-4096.0f, 0.0f, 0.0f});
    CHECK(exteriorHeader.world_max == std::array<float, 3>{110.0f, 4096.0f, 7.0f});

    // Constraint: a cell whose every body is unusable has nothing to list, so no tome file is written.
    TomeRequest empty;
    empty.path = root.path() / "Vis" / "Patch.esp" / "empty.uvd";
    empty.bodies = {fo4::nif::PhysicsMeshInput{}};
    const auto none = fo4ide::plugin::previs_prep::write_tome(empty);
    REQUIRE(none.has_value());
    CHECK(none->objects == 0u);
    CHECK(none->bytes == 0u);
    CHECK_FALSE(std::filesystem::exists(empty.path));
}

namespace {

// Constraint: a closed box of twelve wall triangles encloses, and an empty skip list drops the named faces so a shell can be left open.
std::vector<std::array<float, 9>> wallShell(const std::array<float, 6>& box,
                                            std::initializer_list<int> skip_faces = {}) {
    const float x0 = box[0], y0 = box[1], z0 = box[2], x1 = box[3], y1 = box[4], z1 = box[5];
    const std::vector<std::array<float, 9>> faces{
        {x1, y0, z0, x1, y1, z0, x1, y1, z1}, {x1, y0, z0, x1, y1, z1, x1, y0, z1},
        {x0, y0, z0, x0, y0, z1, x0, y1, z1}, {x0, y0, z0, x0, y1, z1, x0, y1, z0},
        {x0, y1, z0, x0, y1, z1, x1, y1, z1}, {x0, y1, z0, x1, y1, z1, x1, y1, z0},
        {x0, y0, z0, x1, y0, z0, x1, y0, z1}, {x0, y0, z0, x1, y0, z1, x0, y0, z1},
        {x0, y0, z1, x1, y0, z1, x1, y1, z1}, {x0, y0, z1, x1, y1, z1, x0, y1, z1},
        {x0, y0, z0, x0, y1, z0, x1, y1, z0}, {x0, y0, z0, x1, y1, z0, x1, y0, z0}};
    std::vector<std::array<float, 9>> kept;
    for (int face = 0; face < 6; ++face) {
        if (std::find(skip_faces.begin(), skip_faces.end(), face) != skip_faces.end()) continue;
        kept.push_back(faces[static_cast<std::size_t>(2 * face)]);
        kept.push_back(faces[static_cast<std::size_t>(2 * face + 1)]);
    }
    return kept;
}

}  // namespace

// Regression: a tome must stop listing a body standing inside a shell of separate wall bodies, the reported object count and cell box must follow the survivors, and an open wall must leave the body listed so a doorway can never punch a hole in the cell.
TEST_CASE("Native write_tome culls a body inside a shell of wall triangles") {
    fo4ide_test::TempRoot root{"fo4ide_previs_tome_occluded"};
    using fo4ide::plugin::previs_prep::TomeRequest;
    TomeRequest request;
    request.path = root.path() / "Vis" / "Patch.esp" / "00000801.uvd";
    // Constraint: body 0 sits inside the shell and body 1 does not, so only body 0 may be culled.
    request.bodies = {fo4::nif::PhysicsMeshInput{{-16.0f, -16.0f, -16.0f, 16.0f, -16.0f, -16.0f, 0.0f, 16.0f, 16.0f}, {0, 1, 2}},
                      fo4::nif::PhysicsMeshInput{{400.0f, 0.0f, 0.0f, 464.0f, 0.0f, 0.0f, 400.0f, 64.0f, 64.0f}, {0, 1, 2}}};
    request.occluders = wallShell({-32.0f, -32.0f, -32.0f, 32.0f, 32.0f, 32.0f});

    const auto culled = fo4ide::plugin::previs_prep::write_tome(request);
    REQUIRE(culled.has_value());
    CHECK(culled->objects == 1u);
    CHECK(culled->culled == 1u);
    const auto header = fo4::previs::decode_uvd_header(fo4::previs::parse_uvd_tiles(readBytes(request.path)));
    REQUIRE(header.objects.ids.size() == 1u);
    CHECK(header.objects.ids.front() == 0xFD000001u);
    CHECK(header.world_min[0] >= 400.0f);

    // Constraint: an open +X wall leaves the axis clear, so the enclosed body must be listed again.
    request.occluders = wallShell({-32.0f, -32.0f, -32.0f, 32.0f, 32.0f, 32.0f}, {0});
    request.path = root.path() / "Vis" / "Patch.esp" / "00000803.uvd";
    const auto open_wall = fo4ide::plugin::previs_prep::write_tome(request);
    REQUIRE(open_wall.has_value());
    CHECK(open_wall->objects == 2u);
    CHECK(open_wall->culled == 0u);

    // Constraint: without any occluder both bodies list, so the difference is the cull and not the input.
    request.occluders.clear();
    request.path = root.path() / "Vis" / "Patch.esp" / "00000802.uvd";
    const auto open = fo4ide::plugin::previs_prep::write_tome(request);
    REQUIRE(open.has_value());
    CHECK(open->objects == 2u);
    CHECK(fo4::previs::decode_uvd_header(fo4::previs::parse_uvd_tiles(readBytes(request.path))).objects.ids.size() == 2u);
}

// Constraint: a run that culls every body must not leave the previous run's tome in place for generate to patch into Data.
TEST_CASE("Native write_tome removes a stale tome when every body is culled or none is given") {
    fo4ide_test::TempRoot root{"fo4ide_previs_tome_stale"};
    using fo4ide::plugin::previs_prep::TomeRequest;
    const auto stale = std::vector<std::byte>(64, std::byte{0x5A});
    TomeRequest request;
    request.path = root.path() / "Vis" / "Patch.esp" / "00000801.uvd";
    request.bodies = {fo4::nif::PhysicsMeshInput{{-16.0f, -16.0f, -16.0f, 16.0f, -16.0f, -16.0f, 0.0f, 16.0f, 16.0f}, {0, 1, 2}}};
    request.occluders = wallShell({-32.0f, -32.0f, -32.0f, 32.0f, 32.0f, 32.0f});
    writeBytes(request.path, stale);

    const auto culled = fo4ide::plugin::previs_prep::write_tome(request);
    REQUIRE(culled.has_value());
    CHECK(culled->objects == 0u);
    CHECK(culled->culled == 1u);
    CHECK_FALSE(culled->warnings.empty());
    CHECK_FALSE(std::filesystem::exists(request.path));

    writeBytes(request.path, stale);
    request.bodies.clear();
    request.occluders.clear();
    const auto none = fo4ide::plugin::previs_prep::write_tome(request);
    REQUIRE(none.has_value());
    CHECK(none->objects == 0u);
    CHECK_FALSE(std::filesystem::exists(request.path));
}

// Constraint: a configured previs output root must keep a default run out of the game's Data folder, and only an explicit patch_into_data may write the tome there.
TEST_CASE("Native GenerateCellPrevis routes outside Data when an output root is set and patch_into_data opts back in") {
    fo4ide_test::TempRoot root{"fo4ide_previs_output_root"};
    const auto dataDir = root.path() / "data";
    const auto pluginDir = root.path() / "plugins";
    const auto sourcePath = pluginDir / "Source.esp";
    const auto patchPath = pluginDir / "Patch.esp";
    writeBytes(sourcePath, makeStatCellSource({"Meshes/Rock.nif"}));
    writeBytes(patchPath, makeEmptyPatch());
    writeWorldCdx(dataDir, "Source");
    writeBytes(dataDir / std::filesystem::path("Meshes/Rock.nif"), fo4::nif::import_obj(fanObj(200), kOptions));
    const std::filesystem::path shell{FO4IDE_PREVIS_FIXTURES_DIR "/00000FEC_0A82376A_OC.NIF"};
    REQUIRE(std::filesystem::exists(shell));
    const auto cellText = cellTextOf(sourcePath);
    REQUIRE(!cellText.empty());

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins",
         nlohmann::json::array({pluginEntry("Source.esp", sourcePath), pluginEntry("Patch.esp", patchPath)})}};
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());
    backend.setDataDirectory(dataDir);
    const auto outputRoot = root.path() / "previs-out";
    backend.setPrevisOutputRoot(outputRoot);

    const auto underRoot = [](const std::filesystem::path& parent, const std::filesystem::path& child) {
        return child.lexically_relative(parent).string().rfind("..", 0) != 0;
    };
    const nlohmann::json pins = nlohmann::json::parse(R"({"Meshes/Rock.nif": [0.5, 0.25]})");
    const auto defaulted = backend.invokeBackend(
        "GenerateCellPrevis",
        nlohmann::json::array({std::string("Patch.esp"), cellText, std::string("Source.esp"), shell.string(), 1u, true, false}));
    if (!defaulted.has_value()) FAIL(defaulted.error().type + ": " + defaulted.error().message);
    CHECK(underRoot(outputRoot, (*defaulted)["csg"]["path"].get<std::string>()));
    CHECK(underRoot(outputRoot, (*defaulted)["uvd"]["path"].get<std::string>()));
    for (const auto& group : (*defaulted)["groups"]) CHECK(underRoot(outputRoot, group["path"].get<std::string>()));
    CHECK_FALSE(defaulted->contains("patched"));
    CHECK_FALSE(std::filesystem::exists(dataDir / "Vis"));
    CHECK_FALSE(std::filesystem::exists(dataDir / "Meshes" / "PreCombined"));
    CHECK(std::filesystem::exists(outputRoot / "Patch - Geometry.csg"));

    const auto optedIn = backend.invokeBackend(
        "GenerateCellPrevis",
        nlohmann::json::array({std::string("Patch.esp"), cellText, std::string("Source.esp"), shell.string(), 1u, true, false, pins, true}));
    if (!optedIn.has_value()) FAIL(optedIn.error().type + ": " + optedIn.error().message);
    REQUIRE(optedIn->contains("patched"));
    CHECK((*optedIn)["patched"]["path"].get<std::string>() == (dataDir / "Vis" / "Patch.esp" / "00000801.uvd").string());
    CHECK((*optedIn)["patched"]["path"].get<std::string>() == fo4::previs::uvd_tome_path(dataDir, "Patch.esp", 0x801u).string());
    CHECK(std::filesystem::exists(dataDir / "Vis" / "Patch.esp" / "00000801.uvd"));
    CHECK_FALSE(std::filesystem::exists(dataDir / "Meshes" / "PreCombined"));
}

// Constraint: a late write failure must leave no earlier file behind; a directory on the tome path makes the tome the last write to fail.
TEST_CASE("Native GenerateCellPrevis puts every file back when a late write fails") {
    fo4ide_test::TempRoot root{"fo4ide_previs_rollback"};
    const auto dataDir = root.path() / "data";
    const auto pluginDir = root.path() / "plugins";
    const auto sourcePath = pluginDir / "Source.esp";
    const auto patchPath = pluginDir / "Patch.esp";
    writeBytes(sourcePath, makeStatCellSource({"Meshes/Rock.nif"}));
    writeBytes(patchPath, makeEmptyPatch());
    writeWorldCdx(dataDir, "Source");
    writeBytes(dataDir / std::filesystem::path("Meshes/Rock.nif"), fo4::nif::import_obj(fanObj(200), kOptions));
    const std::filesystem::path shell{FO4IDE_PREVIS_FIXTURES_DIR "/00000FEC_0A82376A_OC.NIF"};
    REQUIRE(std::filesystem::exists(shell));
    const auto cellText = cellTextOf(sourcePath);
    REQUIRE(!cellText.empty());

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins",
         nlohmann::json::array({pluginEntry("Source.esp", sourcePath), pluginEntry("Patch.esp", patchPath)})}};
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());
    backend.setDataDirectory(dataDir);
    const auto outputRoot = root.path() / "previs-out";
    backend.setPrevisOutputRoot(outputRoot);
    const auto tomeTarget = outputRoot / "Vis" / "Patch.esp" / "00000801.uvd";

    const auto filesUnder = [](const std::filesystem::path& folder) {
        std::map<std::string, std::vector<std::byte>> files;
        if (!std::filesystem::exists(folder)) return files;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(folder)) {
            if (!entry.is_regular_file()) continue;
            files[entry.path().lexically_relative(folder).generic_string()] = readBytes(entry.path());
        }
        return files;
    };
    const auto generate = [&] {
        return backend.invokeBackend(
            "GenerateCellPrevis",
            nlohmann::json::array({std::string("Patch.esp"), cellText, std::string("Source.esp"), shell.string(), 1u, true, false}));
    };

    std::filesystem::create_directories(tomeTarget);
    const auto refused = generate();
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "PrevisGenerateWrite");
    CHECK(filesUnder(outputRoot).empty());
    CHECK_FALSE(std::filesystem::exists(outputRoot / "Meshes"));
    CHECK(std::filesystem::is_directory(tomeTarget));

    std::filesystem::remove(tomeTarget);
    const auto first = generate();
    if (!first.has_value()) FAIL(first.error().type + ": " + first.error().message);
    auto written = filesUnder(outputRoot);
    REQUIRE(written.size() >= 3u);
    REQUIRE(written.erase("Vis/Patch.esp/00000801.uvd") == 1u);

    std::filesystem::remove(tomeTarget);
    std::filesystem::create_directories(tomeTarget);
    const auto second = generate();
    REQUIRE_FALSE(second.has_value());
    CHECK(second.error().type == "PrevisGenerateWrite");
    CHECK(filesUnder(outputRoot) == written);
}

// Constraint: file names must carry the cell id in uppercase hex; the id has letters and the shell is auto-resolved, so the prefix filter must match too.
TEST_CASE("Native GenerateCellPrevis names every file with the cell id in uppercase hex") {
    fo4ide_test::TempRoot root{"fo4ide_previs_upper_hex"};
    const auto dataDir = root.path() / "data";
    const auto pluginDir = root.path() / "plugins";
    const auto sourcePath = pluginDir / "Source.esp";
    const auto patchPath = pluginDir / "Patch.esp";
    writeBytes(sourcePath, makeStatCellSource({"Meshes/Rock.nif"}, 0xFECu));
    writeBytes(patchPath, makeEmptyPatch());
    writeWorldCdx(dataDir, "Source");
    writeBytes(dataDir / std::filesystem::path("Meshes/Rock.nif"), fo4::nif::import_obj(fanObj(200), kOptions));
    const std::filesystem::path shell{FO4IDE_PREVIS_FIXTURES_DIR "/00000FEC_0A82376A_OC.NIF"};
    REQUIRE(std::filesystem::exists(shell));
    writeBytes(dataDir / "Meshes" / "PreCombined" / "Source.esp" / "00000FEC_0A82376A_OC.NIF", readBytes(shell));
    const auto cellText = cellTextOf(sourcePath);
    REQUIRE(!cellText.empty());

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins",
         nlohmann::json::array({pluginEntry("Source.esp", sourcePath), pluginEntry("Patch.esp", patchPath)})}};
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());
    backend.setDataDirectory(dataDir);
    const auto outputRoot = root.path() / "previs-out";
    backend.setPrevisOutputRoot(outputRoot);

    const nlohmann::json pins = nlohmann::json::parse(R"({"Meshes/Rock.nif": [0.5, 0.25]})");
    const auto generated = backend.invokeBackend(
        "GenerateCellPrevis",
        nlohmann::json::array({std::string("Patch.esp"), cellText, std::string("Source.esp"), std::string(), 1u, true, false, pins, true}));
    if (!generated.has_value()) FAIL(generated.error().type + ": " + generated.error().message);
    REQUIRE((*generated)["groups"].size() == 1u);
    CHECK((*generated)["groups"][0]["ocIdText"] == "00000001");
    CHECK(std::filesystem::path((*generated)["groups"][0]["path"].get<std::string>()).filename().string() ==
          "00000FEC_00000001_OC.NIF");
    CHECK(std::filesystem::path((*generated)["uvd"]["path"].get<std::string>()).filename().string() == "00000FEC.uvd");
    CHECK(std::filesystem::path((*generated)["patched"]["path"].get<std::string>()).filename().string() == "00000FEC.uvd");

    const auto namesUnder = [](const std::filesystem::path& folder) {
        std::vector<std::string> names;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(folder)) {
            if (entry.is_regular_file()) names.push_back(entry.path().filename().string());
        }
        return names;
    };
    const auto written = namesUnder(outputRoot);
    CHECK(std::count(written.begin(), written.end(), "00000FEC_00000001_OC.NIF") == 1);
    CHECK(std::count(written.begin(), written.end(), "00000FEC.uvd") == 1);
    if (generated->contains("physics")) CHECK(std::count(written.begin(), written.end(), "00000FEC_Physics.NIF") == 1);
    for (const auto& name : written) {
        if (name.rfind("Patch", 0) == 0) continue;
        CHECK(name.rfind("00000FEC", 0) == 0);
    }
    const auto patched = namesUnder(dataDir / "Vis");
    REQUIRE(patched.size() == 1u);
    CHECK(patched.front() == "00000FEC.uvd");
}

// Constraint: a second cell generated into the same patch must extend its geometry container, so the first cell's _OC offsets still address its own meshes.
TEST_CASE("Native GenerateCellPrevis keeps the first cell addressable when a second cell joins the patch") {
    fo4ide_test::TempRoot root{"fo4ide_previs_two_cells"};
    const auto dataDir = root.path() / "data";
    const auto pluginDir = root.path() / "plugins";
    const auto firstPath = pluginDir / "First.esp";
    const auto secondPath = pluginDir / "Second.esp";
    const auto patchPath = pluginDir / "Patch.esp";
    writeBytes(firstPath, makeStatCellSource({"Meshes/Rock.nif"}, 0x801u));
    writeBytes(secondPath, makeStatCellSource({"Meshes/Pipe.nif"}, 0x802u));
    writeBytes(patchPath, makeEmptyPatch());
    writeWorldCdx(dataDir, "First");
    writeWorldCdx(dataDir, "Second");
    writeBytes(dataDir / std::filesystem::path("Meshes/Rock.nif"), fo4::nif::import_obj(fanObj(200), kOptions));
    writeBytes(dataDir / std::filesystem::path("Meshes/Pipe.nif"), fo4::nif::import_obj(fanObj(120), kOptions));
    const std::filesystem::path shell{FO4IDE_PREVIS_FIXTURES_DIR "/00000FEC_0A82376A_OC.NIF"};
    REQUIRE(std::filesystem::exists(shell));
    const auto firstCell = cellTextOf(firstPath);
    const auto secondCell = cellTextOf(secondPath);
    REQUIRE(!firstCell.empty());
    REQUIRE(!secondCell.empty());

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({pluginEntry("First.esp", firstPath), pluginEntry("Second.esp", secondPath),
                                           pluginEntry("Patch.esp", patchPath)})}};
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());
    backend.setDataDirectory(dataDir);
    const auto outputRoot = root.path() / "previs-out";
    backend.setPrevisOutputRoot(outputRoot);
    const auto csgPath = outputRoot / "Patch - Geometry.csg";

    const auto generate = [&](const std::string& cell, const std::string& source) {
        return backend.invokeBackend(
            "GenerateCellPrevis",
            nlohmann::json::array({std::string("Patch.esp"), cell, source, shell.string(), 1u, true, false}));
    };
    const auto first = generate(firstCell, "First.esp");
    if (!first.has_value()) FAIL(first.error().type + ": " + first.error().message);
    const auto firstFile = fo4::previs::parse_csg(readBytes(csgPath));
    const auto firstStream = fo4::previs::inflate_csg_stream(firstFile);
    REQUIRE(firstFile.rows.size() == 1u);

    const auto second = generate(secondCell, "Second.esp");
    if (!second.has_value()) FAIL(second.error().type + ": " + second.error().message);
    const auto container = readBytes(csgPath);
    REQUIRE(fo4::previs::validate_csg(container).empty());
    const auto file = fo4::previs::parse_csg(container);
    const auto stream = fo4::previs::inflate_csg_stream(file);
    REQUIRE(file.rows.size() == 2u);
    REQUIRE(stream.size() > firstStream.size());
    CHECK(std::equal(firstStream.begin(), firstStream.end(), stream.begin()));
    CHECK(std::equal(firstFile.rows[0].bytes.begin(), firstFile.rows[0].bytes.end(), file.rows[0].bytes.begin()));

    const auto hash = fo4::previs::csg_file_hash("Patch.esp");
    const auto ocObjects = [](const std::filesystem::path& ocPath) {
        const auto bytes = readBytes(ocPath);
        const auto nif = fo4::nif::read_nif(bytes);
        for (const auto& block : nif.blocks()) {
            if (block.type != "BSPackedCombinedSharedGeomDataExtra") continue;
            const auto header = fo4::previs::read_shared_geom_header(
                std::span<const std::byte>(bytes).subspan(block.offset, block.size));
            REQUIRE(header.has_value());
            return header->objects;
        }
        FAIL("the emitted _OC carries no shared geometry extra");
        return std::vector<fo4::previs::SharedGeomObject>{};
    };
    const auto checkAddressable = [&](const nlohmann::json& run) {
        for (const auto& group : run.at("groups")) {
            const auto objects = ocObjects(group.at("path").get<std::string>());
            REQUIRE(objects.size() == 1u);
            CHECK(objects.front().file_hash == hash);
            bool found = false;
            for (const auto& raw : file.rows) {
                const auto row = fo4::previs::decode_csg_row(std::span<const std::byte>(raw.bytes));
                if (row.data_offset != objects.front().data_offset) continue;
                CHECK(row.vertex_count == objects.front().vertex_count);
                found = true;
            }
            CHECK(found);
        }
    };
    checkAddressable(*first);
    checkAddressable(*second);
    const auto firstRow = fo4::previs::decode_csg_row(std::span<const std::byte>(file.rows[0].bytes));
    const auto secondRow = fo4::previs::decode_csg_row(std::span<const std::byte>(file.rows[1].bytes));
    CHECK(firstRow.data_offset == 0u);
    CHECK(secondRow.data_offset == firstStream.size());
    CHECK(firstRow.vertex_count != secondRow.vertex_count);

    // Constraint: a container that cannot be read must be refused, never overwritten.
    writeBytes(csgPath, std::vector<std::byte>(64, std::byte{0x41}));
    const auto refused = generate(firstCell, "First.esp");
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "PrevisGenerateCsg");
    CHECK(readBytes(csgPath) == std::vector<std::byte>(64, std::byte{0x41}));
    CHECK(refused.error().message.find("cannot be extended") != std::string::npos);

    // Constraint: a non-empty container that cannot be opened must not be treated as missing and rewritten from offset 0.
#ifdef _WIN32
    const HANDLE lockHandle = ::CreateFileW(csgPath.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
                                            FILE_ATTRIBUTE_NORMAL, nullptr);
    REQUIRE(lockHandle != INVALID_HANDLE_VALUE);
    const auto unreadable = generate(firstCell, "First.esp");
    ::CloseHandle(lockHandle);
    REQUIRE_FALSE(unreadable.has_value());
    CHECK(unreadable.error().type == "PrevisGenerateCsg");
    CHECK(unreadable.error().message.find("could not be read") != std::string::npos);
    CHECK(readBytes(csgPath) == std::vector<std::byte>(64, std::byte{0x41}));
#else
    if (::geteuid() != 0) {
        REQUIRE(::chmod(csgPath.c_str(), 0000) == 0);
        const auto unreadable = generate(firstCell, "First.esp");
        REQUIRE(::chmod(csgPath.c_str(), 0644) == 0);
        REQUIRE_FALSE(unreadable.has_value());
        CHECK(unreadable.error().type == "PrevisGenerateCsg");
        CHECK(unreadable.error().message.find("could not be read") != std::string::npos);
        CHECK(readBytes(csgPath) == std::vector<std::byte>(64, std::byte{0x41}));
    }
#endif
}

// Constraint: previs must only ever go into a new patch plugin, so a patch that owns records, has a dependent, or is a game master must be refused by generate, stamp and disable.
TEST_CASE("Native previs refuses a patch plugin that is not a new patch") {
    fo4ide_test::TempRoot root{"fo4ide_previs_patch_refusal"};
    const auto dataDir = root.path() / "data";
    const auto pluginDir = root.path() / "plugins";
    const auto sourcePath = pluginDir / "Source.esp";
    const auto patchPath = pluginDir / "Patch.esp";
    const auto childPath = pluginDir / "Child.esp";
    const auto officialPath = pluginDir / "Fallout4.esm";
    writeBytes(sourcePath, makeStatCellSource({"Meshes/Rock.nif"}, 0x801u, true));
    writeBytes(patchPath, makeEmptyPatch());
    writeBytes(childPath, en::make_plugin({"Patch.esp"}, {}));
    writeBytes(officialPath, makeEmptyPatch());
    writeBytes(dataDir / std::filesystem::path("Meshes/Rock.nif"), fo4::nif::import_obj(fanObj(200), kOptions));
    const auto cellText = cellTextOf(sourcePath);
    REQUIRE(!cellText.empty());

    const auto load = [&](fo4ide::plugin::RadiclePluginBackend& backend, bool withChild) {
        auto entries = nlohmann::json::array({pluginEntry("Fallout4.esm", officialPath), pluginEntry("Source.esp", sourcePath),
                                              pluginEntry("Patch.esp", patchPath)});
        if (withChild) entries.push_back(pluginEntry("Child.esp", childPath));
        REQUIRE(backend.invoke("environment.LoadMo2", nlohmann::json{{"plugins", entries}}).has_value());
        backend.setDataDirectory(dataDir);
        backend.setPrevisOutputRoot(root.path() / "out");
    };
    const auto generate = [&](fo4ide::plugin::RadiclePluginBackend& backend, const std::string& patch) {
        return backend.invokeBackend("GenerateCellPrevis",
                                     nlohmann::json::array({patch, cellText, std::string("Source.esp"), std::string(), 1u, true, false}));
    };

    fo4ide::plugin::RadiclePluginBackend plain;
    load(plain, false);
    const auto ownsRecords = generate(plain, "Source.esp");
    REQUIRE_FALSE(ownsRecords.has_value());
    CHECK(ownsRecords.error().type == "PrevisPatchNotNew");
    const auto official = generate(plain, "Fallout4.esm");
    REQUIRE_FALSE(official.has_value());
    CHECK(official.error().type == "PluginReadOnly");
    // Constraint: a new empty patch must pass the gate, so the run reaches the template lookup and stops there.
    const auto accepted = generate(plain, "Patch.esp");
    REQUIRE_FALSE(accepted.has_value());
    CHECK(accepted.error().type == "PrevisGenerateShell");

    fo4ide::plugin::RadiclePluginBackend dependent;
    load(dependent, true);
    const auto generated = generate(dependent, "Patch.esp");
    REQUIRE_FALSE(generated.has_value());
    CHECK(generated.error().type == "PrevisPatchNotNew");
    const auto stamped = dependent.invokeBackend(
        "StampCellPrevis", nlohmann::json::array({std::string("Patch.esp"), cellText, std::string("Source.esp"), "0x1234", "0x5678",
                                                  nlohmann::json::object(), true}));
    REQUIRE_FALSE(stamped.has_value());
    CHECK(stamped.error().type == "PrevisPatchNotNew");
    const auto disabled = dependent.invokeBackend(
        "DisableCellPrevis", nlohmann::json::array({cellText, std::string("Patch.esp"), std::string("Source.esp"), true}));
    REQUIRE_FALSE(disabled.has_value());
    CHECK(disabled.error().type == "PrevisPatchNotNew");
}

namespace {

struct TomeReplaceFixture {
    fo4ide_test::TempRoot root;
    std::filesystem::path dataDir = root.path() / "data";
    std::filesystem::path pluginDir = root.path() / "plugins";
    std::filesystem::path sourcePath = pluginDir / "Source.esp";
    std::filesystem::path outputRoot = root.path() / "previs-out";
    std::filesystem::path outTome = outputRoot / "Vis" / "Patch.esp" / "00000801.uvd";
    std::filesystem::path dataTome = dataDir / "Vis" / "Patch.esp" / "00000801.uvd";
    std::filesystem::path shell{FO4IDE_PREVIS_FIXTURES_DIR "/00000FEC_0A82376A_OC.NIF"};
    std::string cellText;

    explicit TomeReplaceFixture(const char* name) : root{name} {
        writeBytes(pluginDir / "Patch.esp", makeEmptyPatch());
        writeBytes(dataDir / std::filesystem::path("Meshes/Rock.nif"), fo4::nif::import_obj(fanObj(200), kOptions));
        writeBytes(dataDir / std::filesystem::path("Meshes/Pipe.nif"), fo4::nif::import_obj(fanObj(200), kOptions));
        writeWorldCdx(dataDir, "Source");
        useSource({"Meshes/Rock.nif"});
        REQUIRE(std::filesystem::exists(shell));
    }

    void useSource(const std::vector<std::string>& models) {
        writeBytes(sourcePath, makeStatCellSource(models));
        cellText = cellTextOf(sourcePath);
        REQUIRE(!cellText.empty());
    }

    std::unique_ptr<fo4ide::plugin::RadiclePluginBackend> load() const {
        auto backend = std::make_unique<fo4ide::plugin::RadiclePluginBackend>();
        const nlohmann::json environment{
            {"plugins", nlohmann::json::array({pluginEntry("Source.esp", sourcePath),
                                               pluginEntry("Patch.esp", pluginDir / "Patch.esp")})}};
        REQUIRE(backend->invoke("environment.LoadMo2", environment).has_value());
        backend->setDataDirectory(dataDir);
        backend->setPrevisOutputRoot(outputRoot);
        return backend;
    }

    auto generate(fo4ide::plugin::RadiclePluginBackend& backend, bool intoData, bool copyVanilla = false) const {
        nlohmann::json arguments = nlohmann::json::array(
            {std::string("Patch.esp"), cellText, std::string("Source.esp"), shell.string(), 1u, true, copyVanilla});
        if (intoData) {
            arguments.push_back(nlohmann::json::object());
            arguments.push_back(true);
        }
        return backend.invokeBackend("GenerateCellPrevis", arguments);
    }

    std::map<std::string, std::vector<std::byte>> filesUnder(const std::filesystem::path& folder) const {
        std::map<std::string, std::vector<std::byte>> files;
        if (!std::filesystem::exists(folder)) return files;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(folder)) {
            if (entry.is_regular_file()) files[entry.path().lexically_relative(folder).generic_string()] = readBytes(entry.path());
        }
        return files;
    }

    void seedForeignTome(const std::filesystem::path& path, std::size_t bodies = 3) const {
        fo4ide::plugin::previs_prep::TomeRequest seed;
        seed.path = path;
        for (std::size_t index = 0; index < bodies; ++index) {
            const auto offset = 40.0f * static_cast<float>(index);
            seed.bodies.push_back(fo4::nif::PhysicsMeshInput{
                {offset, 0.0f, 0.0f, offset + 10.0f, 0.0f, 0.0f, offset, 10.0f, 5.0f}, {0, 1, 2}});
        }
        REQUIRE(fo4ide::plugin::previs_prep::write_tome(seed).has_value());
        REQUIRE(fo4::previs::validate_uvd(readBytes(path)).empty());
    }
};

}  // namespace

// Constraint: a tome this tool generated is replaced by a later run for the same patch and cell, in the output root and in the Data copy.
TEST_CASE("Native GenerateCellPrevis replaces its own tome after the source changed") {
    TomeReplaceFixture fixture{"fo4ide_previs_tome_replace"};
    {
        const auto backend = fixture.load();
        const auto first = fixture.generate(*backend, true);
        if (!first.has_value()) FAIL(first.error().type + ": " + first.error().message);
    }
    const auto before = readBytes(fixture.outTome);
    CHECK(readBytes(fixture.dataTome) == before);
    CHECK(std::filesystem::exists(fixture.pluginDir / "Patch - Generated tomes.json"));

    fixture.useSource({"Meshes/Rock.nif", "Meshes/Pipe.nif"});
    const auto backend = fixture.load();
    const auto second = fixture.generate(*backend, true);
    if (!second.has_value()) FAIL(second.error().type + ": " + second.error().message);
    const auto after = readBytes(fixture.outTome);
    CHECK(after != before);
    CHECK(fo4::previs::validate_uvd_tome_tiled(after).empty());
    CHECK(readBytes(fixture.dataTome) == after);
    CHECK((*second)["uvd"]["objects"].get<std::size_t>() == 2u);

    const auto third = fixture.generate(*backend, true);
    if (!third.has_value()) FAIL(third.error().type + ": " + third.error().message);
    CHECK(readBytes(fixture.outTome) == after);
}

// Constraint: a validated tome this tool did not generate, or one edited since, may hold cluster data, so a run refuses it and writes nothing else.
TEST_CASE("Native GenerateCellPrevis keeps a foreign or edited tome and every file beside it") {
    TomeReplaceFixture fixture{"fo4ide_previs_tome_foreign"};
    fixture.seedForeignTome(fixture.dataTome);
    const auto foreign = readBytes(fixture.dataTome);
    {
        const auto backend = fixture.load();
        const auto refused = fixture.generate(*backend, true);
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().type == "PrevisGenerateTome");
        CHECK(readBytes(fixture.dataTome) == foreign);
        CHECK(fixture.filesUnder(fixture.outputRoot).empty());
        CHECK_FALSE(std::filesystem::exists(fixture.pluginDir / "Patch - Generated tomes.json"));
    }
    std::filesystem::remove(fixture.dataTome);

    {
        const auto backend = fixture.load();
        const auto first = fixture.generate(*backend, false);
        if (!first.has_value()) FAIL(first.error().type + ": " + first.error().message);
    }
    fixture.seedForeignTome(fixture.outTome);
    const auto edited = readBytes(fixture.outTome);
    const auto written = fixture.filesUnder(fixture.outputRoot);

    fixture.useSource({"Meshes/Rock.nif", "Meshes/Pipe.nif"});
    const auto backend = fixture.load();
    const auto refused = fixture.generate(*backend, false);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "PrevisGenerateTome");
    CHECK(readBytes(fixture.outTome) == edited);
    CHECK(fixture.filesUnder(fixture.outputRoot) == written);
}

// Constraint: the ledger is staged with the tomes, so a failed commit must leave the ledger as it was, and an unreadable ledger must stop the run before any file is written.
TEST_CASE("Native GenerateCellPrevis keeps the tome ledger when the commit fails or the ledger is unreadable") {
    TomeReplaceFixture fixture{"fo4ide_previs_tome_ledger_state"};
    const auto ledgerPath = fixture.pluginDir / "Patch - Generated tomes.json";
    {
        const auto backend = fixture.load();
        const auto first = fixture.generate(*backend, false);
        if (!first.has_value()) FAIL(first.error().type + ": " + first.error().message);
    }
    const auto ledgerBefore = readBytes(ledgerPath);
    const auto written = fixture.filesUnder(fixture.outputRoot);
    fixture.useSource({"Meshes/Rock.nif", "Meshes/Pipe.nif"});

    std::filesystem::remove(fixture.outTome);
    std::filesystem::create_directories(fixture.outTome);
    {
        const auto backend = fixture.load();
        const auto failed = fixture.generate(*backend, false);
        REQUIRE_FALSE(failed.has_value());
        CHECK(failed.error().type == "PrevisGenerateWrite");
    }
    CHECK(readBytes(ledgerPath) == ledgerBefore);
    std::filesystem::remove(fixture.outTome);
    writeBytes(fixture.outTome, written.at("Vis/Patch.esp/00000801.uvd"));

    const auto garbage = std::vector<std::byte>(16, std::byte{0x41});
    writeBytes(ledgerPath, garbage);
    const auto backend = fixture.load();
    const auto refused = fixture.generate(*backend, false);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "PrevisGenerateTome");
    CHECK(refused.error().message.find("tome ledger") != std::string::npos);
    CHECK(readBytes(ledgerPath) == garbage);
    CHECK(fixture.filesUnder(fixture.outputRoot) == written);
}

// Constraint: a copied vanilla cluster tome holds data for other cells, so a later single-cell run must not replace or remove it, while repeating the same copy is a no-op.
TEST_CASE("Native GenerateCellPrevis keeps a tome copied from a vanilla cluster") {
    TomeReplaceFixture fixture{"fo4ide_previs_tome_copied"};
    const auto vanillaTome = fixture.dataDir / "Vis" / "Source.esp" / "00000801.uvd";
    fixture.seedForeignTome(vanillaTome);
    const auto vanilla = readBytes(vanillaTome);

    {
        const auto backend = fixture.load();
        const auto copied = fixture.generate(*backend, false, true);
        if (!copied.has_value()) FAIL(copied.error().type + ": " + copied.error().message);
    }
    CHECK(readBytes(fixture.outTome) == vanilla);
    const auto written = fixture.filesUnder(fixture.outputRoot);

    {
        const auto backend = fixture.load();
        const auto refused = fixture.generate(*backend, false);
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().type == "PrevisGenerateTome");
        CHECK(refused.error().message.find("copied cluster tome") != std::string::npos);
    }
    CHECK(readBytes(fixture.outTome) == vanilla);
    CHECK(fixture.filesUnder(fixture.outputRoot) == written);

    {
        const auto backend = fixture.load();
        const auto again = fixture.generate(*backend, false, true);
        if (!again.has_value()) FAIL(again.error().type + ": " + again.error().message);
    }
    CHECK(readBytes(fixture.outTome) == vanilla);

    fixture.seedForeignTome(fixture.outTome, 2);
    const auto edited = readBytes(fixture.outTome);
    REQUIRE(edited != vanilla);
    const auto backend = fixture.load();
    const auto refusedEdited = fixture.generate(*backend, false);
    REQUIRE_FALSE(refusedEdited.has_value());
    CHECK(refusedEdited.error().type == "PrevisGenerateTome");
    CHECK(readBytes(fixture.outTome) == edited);
}
