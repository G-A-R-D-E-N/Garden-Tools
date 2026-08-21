#include "f4lod/terrain_texture_graph.hpp"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

void u16(std::vector<unsigned char>& out, std::uint16_t value) {
    out.push_back(static_cast<unsigned char>(value & 0xFFU));
    out.push_back(static_cast<unsigned char>((value >> 8U) & 0xFFU));
}
void i16(std::vector<unsigned char>& out, std::int16_t value) {
    std::uint16_t raw = 0;
    std::memcpy(&raw, &value, sizeof(raw));
    u16(out, raw);
}
void u32(std::vector<unsigned char>& out, std::uint32_t value) {
    out.push_back(static_cast<unsigned char>(value & 0xFFU));
    out.push_back(static_cast<unsigned char>((value >> 8U) & 0xFFU));
    out.push_back(static_cast<unsigned char>((value >> 16U) & 0xFFU));
    out.push_back(static_cast<unsigned char>((value >> 24U) & 0xFFU));
}
void f32(std::vector<unsigned char>& out, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    u32(out, bits);
}
void sig(std::vector<unsigned char>& out, std::string_view value) {
    assert(value.size() == 4U);
    out.insert(out.end(), value.begin(), value.end());
}
void subrecord(std::vector<unsigned char>& out, std::string_view type, const std::vector<unsigned char>& data) {
    sig(out, type);
    u16(out, static_cast<std::uint16_t>(data.size()));
    out.insert(out.end(), data.begin(), data.end());
}
std::vector<unsigned char> zstring(std::string value) {
    std::vector<unsigned char> data(value.begin(), value.end());
    data.push_back(0U);
    return data;
}

f4lod::WinningRecord write_record(
    const std::filesystem::path& path,
    f4lod::FormKey key,
    const char* type,
    const char* source,
    const std::vector<unsigned char>& payload) {
    std::ofstream output(path, std::ios::binary);
    const std::vector<unsigned char> header(24U, 0U);
    output.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
    output.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));

    f4lod::WinningRecord record;
    record.key = std::move(key);
    record.type = type;
    record.source_plugin = source;
    record.source_path = path;
    record.data_size = static_cast<std::uint32_t>(payload.size());
    return record;
}

std::vector<unsigned char> layer_header(
    std::uint32_t raw_ltex,
    std::uint8_t quadrant,
    std::int16_t layer) {
    std::vector<unsigned char> data;
    u32(data, raw_ltex);
    data.push_back(quadrant);
    data.push_back(0U);
    i16(data, layer);
    return data;
}

} // namespace

int main() {
    const auto root = std::filesystem::temp_directory_path() / "f4lodgen-terrain-texture-graph-smoke";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    const f4lod::FormKey world{"Fallout4.esm", 0x18AA2U};
    const f4lod::FormKey cell{"Landscape.esp", 0x10U};
    const f4lod::FormKey land{"Landscape.esp", 0x20U};
    const f4lod::FormKey ltex{"Textures.esp", 0x100U};
    const f4lod::FormKey txst{"Textures.esp", 0x200U};

    // Landscape.esp masters: [Fallout4.esm, Textures.esp], so master index 1
    // resolves to Textures.esp.
    std::vector<unsigned char> land_payload;
    subrecord(land_payload, "BTXT", layer_header(0x01000100U, 2U, 0));
    subrecord(land_payload, "BTXT", layer_header(0U, 3U, 2)); // explicit NULL LTEX
    subrecord(land_payload, "ATXT", layer_header(0x01000100U, 1U, 1));
    std::vector<unsigned char> vtxt;
    u16(vtxt, 17U); u16(vtxt, 0U); f32(vtxt, 0.25F);
    u16(vtxt, 33U); u16(vtxt, 0U); f32(vtxt, 0.75F);
    subrecord(land_payload, "VTXT", vtxt);

    // Textures.esp masters: [Fallout4.esm], so own-file index 1 resolves TXST.
    std::vector<unsigned char> ltex_payload;
    subrecord(ltex_payload, "EDID", zstring("GroundDirt01"));
    std::vector<unsigned char> tnam; u32(tnam, 0x01000200U);
    subrecord(ltex_payload, "TNAM", tnam);

    std::vector<unsigned char> txst_payload;
    subrecord(txst_payload, "EDID", zstring("GroundDirt01Set"));
    subrecord(txst_payload, "TX00", zstring("textures/landscape/dirt01_d.dds"));
    subrecord(txst_payload, "TX01", zstring("textures/landscape/dirt01_n.dds"));
    subrecord(txst_payload, "TX04", zstring("textures/landscape/dirt01_h.dds"));

    auto land_record = write_record(root / "land.bin", land, "LAND", "Landscape.esp", land_payload);
    land_record.cell = cell;
    const auto ltex_record = write_record(root / "ltex.bin", ltex, "LTEX", "Textures.esp", ltex_payload);
    const auto txst_record = write_record(root / "txst.bin", txst, "TXST", "Textures.esp", txst_payload);

    f4lod::RecordGraph graph;
    graph.valid = true;
    graph.sources_low_to_high.push_back({0U, "Textures.esp", {}, {"Fallout4.esm"}});
    graph.sources_low_to_high.push_back({1U, "Landscape.esp", {}, {"Fallout4.esm", "Textures.esp"}});
    graph.candidates_low_to_high = {land_record, ltex_record, txst_record};
    graph.winners = {land_record, ltex_record, txst_record};

    f4lod::TerrainWorld terrain;
    terrain.complete = true;
    terrain.worldspace = world;
    f4lod::TerrainCell terrain_cell;
    terrain_cell.cell = cell;
    terrain_cell.land = land;
    terrain_cell.land_source_plugin = "Landscape.esp";
    terrain.cells.push_back(terrain_cell);

    const auto texture_graph = f4lod::build_terrain_texture_graph(graph, terrain);
    assert(texture_graph.valid);
    assert(texture_graph.diagnostics.empty());
    assert(texture_graph.cells.size() == 1U);
    assert(texture_graph.cells[0].layers.size() == 3U);

    const auto& base = texture_graph.cells[0].layers[0];
    assert(!base.alpha);
    assert(base.quadrant == 2U);
    assert(base.layer == 0);
    assert(base.has_landscape_texture);
    assert(base.landscape_texture.plugin == "Textures.esp");
    assert(base.landscape_texture.id == 0x100U);

    const auto& null_layer = texture_graph.cells[0].layers[1];
    assert(!null_layer.alpha);
    assert(!null_layer.has_landscape_texture);

    const auto& alpha = texture_graph.cells[0].layers[2];
    assert(alpha.alpha);
    assert(alpha.quadrant == 1U);
    assert(alpha.layer == 1);
    assert(alpha.alpha_samples.size() == 2U);
    assert(alpha.alpha_samples[0].position == 17U);
    assert(alpha.alpha_samples[0].opacity == 0.25F);
    assert(alpha.alpha_samples[1].position == 33U);
    assert(alpha.alpha_samples[1].opacity == 0.75F);

    assert(texture_graph.landscape_textures.size() == 1U);
    const auto& ltex_dependency = texture_graph.landscape_textures[0];
    assert(ltex_dependency.key.plugin == "Textures.esp");
    assert(ltex_dependency.editor_id == "GroundDirt01");
    assert(ltex_dependency.has_texture_set);
    assert(ltex_dependency.texture_set.id == 0x200U);

    assert(texture_graph.texture_sets.size() == 1U);
    const auto& txst_dependency = texture_graph.texture_sets[0];
    assert(txst_dependency.editor_id == "GroundDirt01Set");
    assert(txst_dependency.texture_paths[0] == "textures/landscape/dirt01_d.dds");
    assert(txst_dependency.texture_paths[1] == "textures/landscape/dirt01_n.dds");
    assert(txst_dependency.texture_paths[2].empty());
    assert(txst_dependency.texture_paths[4] == "textures/landscape/dirt01_h.dds");

    // Reusable payload FormID resolver follows source-plugin master tables.
    f4lod::FormKey resolved;
    std::string error;
    assert(f4lod::resolve_record_form_id(graph, "Landscape.esp", 0x01000100U, resolved, error));
    assert(resolved.plugin == "Textures.esp" && resolved.id == 0x100U);
    assert(f4lod::resolve_record_form_id(graph, "Textures.esp", 0x01000200U, resolved, error));
    assert(resolved.plugin == "Textures.esp" && resolved.id == 0x200U);
    assert(!f4lod::resolve_record_form_id(graph, "Missing.esp", 0x01000200U, resolved, error));

    std::filesystem::remove_all(root);
    return 0;
}
