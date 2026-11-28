#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <fo4/archive/ba2_reader.hpp>
#include <fo4/environment/environment.hpp>
#include <fo4/nif/nif.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/previs/csg.hpp>
#include <fo4/previs/precombine_plan.hpp>
#include <fo4/previs/shared_geom_extra.hpp>
#include <fo4/previs/uvd_tile_build.hpp>
#include <fo4/previs/uvd_tile_header.hpp>
#include <fo4/previs/uvd_tile_model.hpp>
#include <fo4/previs/xcri.hpp>
#include <fo4/signature.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "../helpers/TempRoot.hpp"
#include "plugin_bytes.hpp"

namespace {

namespace fs = std::filesystem;

// Constraint: the profile needs the six masters and the mesh archive, so a partial install skips.
std::optional<fs::path> vanillaDataDir() {
    fs::path data = "D:/SteamLibrary/steamapps/common/Fallout 4/Data";
    if (const char* env = std::getenv("FO4_DATA_DIR"); env != nullptr && *env != '\0') data = env;
    std::error_code error;
    if (!fs::is_regular_file(data / "Fallout4.esm", error)) return std::nullopt;
    if (!fs::is_regular_file(data / "Fallout4 - MeshesExtra.ba2", error)) return std::nullopt;
    return data;
}

std::vector<fs::path> mastersOf(const fs::path& data) {
    const std::vector<std::string> names{"Fallout4.esm",   "DLCRobot.esm",    "DLCworkshop01.esm",
                                         "DLCCoast.esm",   "DLCworkshop03.esm", "DLCNukaWorld.esm"};
    std::vector<fs::path> paths;
    for (const auto& name : names) {
        const auto path = data / name;
        if (!fs::is_regular_file(path)) return {};
        paths.push_back(path);
    }
    return paths;
}

std::vector<std::byte> readBytes(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    REQUIRE(stream.good());
    stream.seekg(0, std::ios::end);
    const auto size = stream.tellg();
    REQUIRE(size > 0);
    stream.seekg(0, std::ios::beg);
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(stream.good());
    return bytes;
}

void writeBytes(const fs::path& path, std::span<const std::byte> bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

std::vector<std::byte> emptyPatch() {
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

// Constraint: the shell must be a shipped _OC carrying the table layout the writer patches.
std::optional<std::vector<std::byte>> shippedShell(const fs::path& meshesExtra) {
    auto reader = fo4::archive::Ba2Reader::open(meshesExtra);
    std::size_t scanned = 0;
    for (std::size_t index = 0; index < reader.entries().size() && scanned < 400; ++index) {
        std::string key = reader.entries()[index].path;
        for (auto& ch : key) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        if (key.size() < 7 || key.compare(key.size() - 7, 7, "_oc.nif") != 0) continue;
        ++scanned;
        const auto bytes = reader.extract(index);
        if (bytes.empty()) continue;
        try {
            const auto nif = fo4::nif::read_nif(bytes);
            for (const auto& block : nif.blocks()) {
                if (block.type != "BSPackedCombinedSharedGeomDataExtra") continue;
                const auto header = fo4::previs::read_shared_geom_header(
                    std::span<const std::byte>(bytes).subspan(block.offset, block.size));
                if (!header.has_value() || header->objects.empty()) continue;
                if (header->objects.front().vertex_count == 0) continue;
                return bytes;
            }
        } catch (...) {
            continue;
        }
    }
    return std::nullopt;
}

struct Picked {
    fo4::FormKey interior;
    fo4::FormKey exterior;
    std::size_t interiorGroups{0};
    std::size_t exteriorGroups{0};
};

// Constraint: each cell is chosen by planning, so the pair is the first of each kind with geometry.
Picked pickCells(const fo4::env::Environment& env) {
    Picked picked{};
    const auto cellSignature = fo4::Signature::from_string("CELL");
    const auto dataSignature = fo4::Signature::from_string("DATA");
    for (const auto* ref : env.plugin_records(0)) {
        if (ref == nullptr || ref->record == nullptr) continue;
        if (ref->record->header.signature != cellSignature) continue;
        bool interior = false;
        for (const auto& sub : ref->record->subrecords) {
            if (sub.signature != dataSignature || sub.data.empty()) continue;
            interior = (static_cast<std::uint8_t>(sub.data.front()) & 1u) != 0;
            break;
        }
        if (interior && picked.interiorGroups != 0) continue;
        if (!interior && picked.exteriorGroups != 0) continue;
        const auto plan = fo4::previs::build_precombine_plan(env, ref->record->form_key, "Fallout4.esm", 1);
        if (!plan.has_value() || plan->groups.empty()) continue;
        if (interior) {
            picked.interior = ref->record->form_key;
            picked.interiorGroups = plan->groups.size();
        } else {
            picked.exterior = ref->record->form_key;
            picked.exteriorGroups = plan->groups.size();
        }
        if (picked.interiorGroups != 0 && picked.exteriorGroups != 0) break;
    }
    return picked;
}

std::map<std::string, std::uintmax_t> snapshotTree(const fs::path& root) {
    std::map<std::string, std::uintmax_t> snapshot;
    std::error_code error;
    for (fs::recursive_directory_iterator it(root, error), end; !error && it != end; it.increment(error)) {
        if (!it->is_regular_file(error)) continue;
        snapshot[it->path().string()] = it->file_size(error);
    }
    return snapshot;
}

}  // namespace

// Detects: an unpreparable cell, an _OC misnaming the patch container at its row offset, a wrong XCRI count, a tome the tile model rejects or that lists fewer objects than bodies, or a write into Data.
TEST_CASE("PrevisGenerate drives vanilla interior and exterior cells into a scratch output root", "[slow]") {
    const auto data = vanillaDataDir();
    if (!data) {
        SKIP("set FO4_DATA_DIR to a Fallout 4 Data folder with the six masters and MeshesExtra");
        return;
    }
    const auto masters = mastersOf(*data);
    REQUIRE(!masters.empty());

    const auto env = fo4::env::Environment::load(masters);
    const auto picked = pickCells(env);
    if (picked.interiorGroups == 0 || picked.exteriorGroups == 0) {
        SKIP("no vanilla interior and exterior cell both plan a combination");
        return;
    }
    const auto shellBytes = shippedShell(*data / "Fallout4 - MeshesExtra.ba2");
    if (!shellBytes.has_value()) {
        SKIP("no shipped _OC in MeshesExtra carries the table layout");
        return;
    }

    fo4ide_test::TempRoot root{"fo4ide_previs_real_cell"};
    const auto shellPath = root.path() / "shell_OC.NIF";
    writeBytes(shellPath, *shellBytes);
    const auto patchPath = root.path() / "ZZPrevisHarness.esp";
    writeBytes(patchPath, emptyPatch());
    const auto assetRoot = root.path() / "assets";
    const auto before = snapshotTree(*data);

    fo4ide::plugin::RadiclePluginBackend backend;
    backend.setDataDirectory(*data);
    // Constraint: every authored asset goes to the scratch root, so the game's Data folder is never written.
    backend.setPrevisOutputRoot(assetRoot);
    nlohmann::json plugins = nlohmann::json::array();
    for (const auto& master : masters) {
        plugins.push_back({{"name", master.filename().string()}, {"path", master.string()}, {"source", "data"}, {"resolved", true}});
    }
    plugins.push_back({{"name", "ZZPrevisHarness.esp"}, {"path", patchPath.string()}, {"source", "test"}, {"resolved", true}});
    REQUIRE(backend.invoke("environment.LoadMo2", nlohmann::json{{"plugins", plugins}}).has_value());

    const std::vector<std::pair<std::string, fo4::FormKey>> cells{{"interior", picked.interior},
                                                                 {"exterior", picked.exterior}};
    std::size_t containerRows = 0;
    for (const auto& [kind, cell] : cells) {
        INFO(kind << ' ' << cell.to_string());
        const nlohmann::json arguments = nlohmann::json::array(
            {"ZZPrevisHarness.esp", cell.to_string(), "Fallout4.esm", shellPath.string(), 1, true, false});
        const auto result = backend.invoke("backend.GenerateCellPrevis", arguments);
        REQUIRE(result.has_value());
        const auto& json = *result;
        REQUIRE(json.at("meshIds").get<std::size_t>() > 0);
        REQUIRE(json.at("references").get<std::size_t>() > 0);

        // Constraint: every CSG row must stay inside the container's inflated stream.
        const auto csgPath = json.at("csg").at("path").get<std::string>();
        REQUIRE(csgPath.starts_with(assetRoot.string()));
        const auto csgBytes = readBytes(csgPath);
        REQUIRE(fo4::previs::validate_csg(csgBytes).empty());
        const auto csg = fo4::previs::parse_csg(csgBytes);
        const auto stream = fo4::previs::inflate_csg_stream(csg);
        std::size_t plannedObjects = 0;
        for (const auto& group : json.at("groups")) plannedObjects += group.at("objects").get<std::size_t>();
        // Constraint: the second cell extends the container, so its rows are counted on top of the first cell's.
        containerRows += plannedObjects;
        REQUIRE(csg.rows.size() == containerRows);
        const auto hash = fo4::previs::csg_file_hash("ZZPrevisHarness.esp");

        std::vector<std::uint32_t> rowOffsets;
        for (const auto& row : csg.rows) {
            rowOffsets.push_back(fo4::previs::decode_csg_row(std::span<const std::byte>(row.bytes)).data_offset);
        }
        for (std::size_t index = 0; index < csg.rows.size(); ++index) {
            const auto decoded = fo4::previs::decode_csg_row(std::span<const std::byte>(csg.rows[index].bytes));
            REQUIRE(decoded.data_offset < stream.size());
            if (index != 0) REQUIRE(rowOffsets[index - 1] < decoded.data_offset);
        }

        // Constraint: every _OC object names this patch's container at its row offset.
        for (const auto& group : json.at("groups")) {
            const auto path = group.at("path").get<std::string>();
            INFO(path);
            REQUIRE(path.starts_with(assetRoot.string()));
            const auto bytes = readBytes(path);
            bool patchable = false;
            const auto nif = fo4::nif::read_nif(bytes);
            for (const auto& block : nif.blocks()) {
                if (block.type != "BSPackedCombinedSharedGeomDataExtra") continue;
                const auto header = fo4::previs::read_shared_geom_header(
                    std::span<const std::byte>(bytes).subspan(block.offset, block.size));
                REQUIRE(header.has_value());
                REQUIRE(!header->objects.empty());
                for (const auto& object : header->objects) {
                    REQUIRE(object.file_hash == hash);
                    REQUIRE(std::find(rowOffsets.begin(), rowOffsets.end(), object.data_offset) != rowOffsets.end());
                    patchable = true;
                }
            }
            REQUIRE(patchable);
        }

        // Constraint: the saved CELL carries the XCRI the assets were numbered from.
        REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"ZZPrevisHarness.esp"})).has_value());
        const auto saved = fo4::read_plugin_file(patchPath);
        const auto* record = fo4::find_record(saved, cell);
        REQUIRE(record != nullptr);
        const fo4::Subrecord* xcri = nullptr;
        for (const auto& sub : record->subrecords) {
            if (sub.signature != fo4::Signature::from_string("XCRI")) continue;
            xcri = &sub;
            break;
        }
        REQUIRE(xcri != nullptr);
        const auto raw = fo4::previs::parse_xcri_bytes(xcri->data);
        REQUIRE(raw.mesh_ids.size() == json.at("meshIds").get<std::size_t>());
        REQUIRE(raw.refs.size() == json.at("references").get<std::size_t>());

        // Constraint: the cell's tome is a tile-model tome with one distinct combined-object id per placed physics body.
        REQUIRE(json.contains("uvd"));
        const auto tomeBytes = readBytes(json.at("uvd").at("path").get<std::string>());
        REQUIRE(tomeBytes.size() == json.at("uvd").at("bytes").get<std::size_t>());
        REQUIRE(fo4::previs::validate_uvd_tome_tiled(tomeBytes).empty());
        const auto tileModel = fo4::previs::parse_uvd_tiles(tomeBytes);
        REQUIRE(tileModel.slots.size() == 1u);
        const auto tileHeader = fo4::previs::decode_uvd_header(tileModel);
        REQUIRE(tileHeader.objects.ids.size() == json.at("uvd").at("objects").get<std::size_t>());
        // Constraint: a body inside a closed shell is dropped on purpose, so the survivors plus the dropped bodies must still cover every reference.
        const auto culledBodies = json.at("uvd").at("culled").get<std::size_t>();
        REQUIRE(tileHeader.objects.ids.size() + culledBodies == json.at("references").get<std::size_t>());
        const std::set<std::uint32_t> distinctIds(tileHeader.objects.ids.begin(), tileHeader.objects.ids.end());
        REQUIRE(distinctIds.size() == tileHeader.objects.ids.size());
        for (const auto id : tileHeader.objects.ids) REQUIRE((id >> 24) == 0xFDu);
        // Constraint: an id carries its body index in the low 24 bits, so culling must leave every listed id pointing at a real body.
        for (const auto id : tileHeader.objects.ids) REQUIRE((id & 0x00FFFFFFu) < json.at("references").get<std::size_t>());
        for (const auto& box : tileHeader.objects.boxes) {
            for (std::size_t axis = 0; axis < 3; ++axis) {
                REQUIRE(tileHeader.world_min[axis] <= box[axis]);
                REQUIRE(tileHeader.world_max[axis] >= box[3 + axis]);
            }
        }
    }

    // Constraint: a rigged shell must be refused before any container ships, so nothing lands in the root.
    const auto refusedRoot = root.path() / "refused";
    backend.setPrevisOutputRoot(refusedRoot);
    const auto bogusShell = root.path() / "bogus_OC.NIF";
    writeBytes(bogusShell, std::vector<std::byte>(64, std::byte{0}));
    const auto refused = backend.invoke(
        "backend.GenerateCellPrevis",
        nlohmann::json::array({"ZZPrevisHarness.esp", picked.interior.to_string(), "Fallout4.esm",
                               bogusShell.string(), 1, true, false}));
    REQUIRE_FALSE(refused.has_value());
    REQUIRE(snapshotTree(refusedRoot).empty());
    backend.setPrevisOutputRoot(assetRoot);

    // Constraint: a write into the game's Data folder would change its listing.
    const auto after = snapshotTree(*data);
    REQUIRE(before.size() == after.size());
    for (const auto& [path, size] : before) {
        const auto found = after.find(path);
        REQUIRE(found != after.end());
        REQUIRE(found->second == size);
    }
}
