#include "f4lod/record_graph.hpp"
#include "f4lod/worldspace.hpp"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <vector>

namespace {

void append_u16(std::vector<unsigned char>& out, std::uint16_t value) {
    out.push_back(static_cast<unsigned char>(value & 0xFFU));
    out.push_back(static_cast<unsigned char>((value >> 8U) & 0xFFU));
}
void append_u32(std::vector<unsigned char>& out, std::uint32_t value) {
    out.push_back(static_cast<unsigned char>(value & 0xFFU));
    out.push_back(static_cast<unsigned char>((value >> 8U) & 0xFFU));
    out.push_back(static_cast<unsigned char>((value >> 16U) & 0xFFU));
    out.push_back(static_cast<unsigned char>((value >> 24U) & 0xFFU));
}
void append_f32(std::vector<unsigned char>& out, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    append_u32(out, bits);
}
void append_signature(std::vector<unsigned char>& out, std::string_view signature) {
    assert(signature.size() == 4); out.insert(out.end(), signature.begin(), signature.end());
}
std::uint32_t fourcc(std::string_view signature) {
    assert(signature.size() == 4);
    return static_cast<std::uint32_t>(static_cast<unsigned char>(signature[0])) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(signature[1])) << 8U) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(signature[2])) << 16U) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(signature[3])) << 24U);
}
void append_subrecord(std::vector<unsigned char>& out, std::string_view type, const std::vector<unsigned char>& data) {
    append_signature(out, type); append_u16(out, static_cast<std::uint16_t>(data.size())); out.insert(out.end(), data.begin(), data.end());
}
std::vector<unsigned char> make_record(std::string_view type, std::uint32_t form_id, const std::vector<unsigned char>& data = {}) {
    std::vector<unsigned char> out; append_signature(out, type); append_u32(out, static_cast<std::uint32_t>(data.size()));
    append_u32(out, 0); append_u32(out, form_id); append_u32(out, 0); append_u16(out, 131); append_u16(out, 0);
    out.insert(out.end(), data.begin(), data.end()); return out;
}
std::vector<unsigned char> make_group(std::uint32_t label, std::uint32_t type, const std::vector<unsigned char>& payload) {
    std::vector<unsigned char> out; append_signature(out, "GRUP"); append_u32(out, static_cast<std::uint32_t>(24 + payload.size()));
    append_u32(out, label); append_u32(out, type); append_u32(out, 0); append_u32(out, 0); out.insert(out.end(), payload.begin(), payload.end()); return out;
}
void append_bytes(std::vector<unsigned char>& out, const std::vector<unsigned char>& bytes) { out.insert(out.end(), bytes.begin(), bytes.end()); }

} // namespace

int main() {
    const auto root = std::filesystem::temp_directory_path() / "f4lodgen-worldspace-smoke";
    std::filesystem::remove_all(root); std::filesystem::create_directories(root);

    constexpr std::uint32_t world_form = 0x01000100U;
    constexpr std::uint32_t cell_form = 0x01000200U;
    constexpr std::uint32_t land_form = 0x01000300U;

    std::vector<unsigned char> tes4_data;
    append_subrecord(tes4_data, "HEDR", std::vector<unsigned char>(12, 0));
    std::vector<unsigned char> master{'F','a','l','l','o','u','t','4','.','e','s','m',0};
    append_subrecord(tes4_data, "MAST", master);

    std::vector<unsigned char> wrld_data;
    std::vector<unsigned char> edid{'C','o','m','m','o','n','w','e','a','l','t','h',0};
    append_subrecord(wrld_data, "EDID", edid);
    std::vector<unsigned char> dnam; append_f32(dnam, -2048.0F); append_f32(dnam, 64.0F);
    append_subrecord(wrld_data, "DNAM", dnam);

    std::vector<unsigned char> cell_data;
    std::vector<unsigned char> xclc; append_u32(xclc, static_cast<std::uint32_t>(-3)); append_u32(xclc, 7U);
    append_subrecord(cell_data, "XCLC", xclc);
    append_subrecord(cell_data, "DATA", std::vector<unsigned char>{0x02U});
    std::vector<unsigned char> xclw; append_f32(xclw, 96.0F); append_subrecord(cell_data, "XCLW", xclw);

    const auto temporary_group = make_group(cell_form, 9, make_record("LAND", land_form));
    const auto cell_children_group = make_group(cell_form, 6, temporary_group);
    std::vector<unsigned char> subblock_payload;
    append_bytes(subblock_payload, make_record("CELL", cell_form, cell_data));
    append_bytes(subblock_payload, cell_children_group);
    const auto subblock_group = make_group(0, 5, subblock_payload);
    const auto block_group = make_group(0, 4, subblock_group);
    const auto world_children_group = make_group(world_form, 1, block_group);
    std::vector<unsigned char> top_payload;
    append_bytes(top_payload, make_record("WRLD", world_form, wrld_data)); append_bytes(top_payload, world_children_group);
    const auto top_group = make_group(fourcc("WRLD"), 0, top_payload);

    std::vector<unsigned char> plugin;
    append_bytes(plugin, make_record("TES4", 0, tes4_data)); append_bytes(plugin, top_group);
    const auto path = root / "World.esp";
    { std::ofstream file(path, std::ios::binary); file.write(reinterpret_cast<const char*>(plugin.data()), static_cast<std::streamsize>(plugin.size())); }

    const std::vector<f4lod::RecordGraphPlugin> load_order{{0, "World.esp", path}};
    const auto graph = f4lod::build_lod_record_graph(load_order); assert(graph.valid); assert(graph.counts.winners == 3);
    const auto catalog = f4lod::build_worldspace_catalog(graph);
    assert(catalog.complete); assert(catalog.diagnostics.empty()); assert(catalog.worldspaces.size() == 1);
    assert(catalog.exterior_cells.size() == 1); assert(catalog.landscape_cell_count() == 1);

    const auto* commonwealth = f4lod::find_worldspace_by_editor_id(catalog, "commonwealth");
    assert(commonwealth != nullptr); assert(commonwealth->key.plugin == "World.esp"); assert(commonwealth->key.id == 0x100U);
    assert(commonwealth->default_land_height == -2048.0F); assert(commonwealth->default_water_height == 64.0F);

    const auto& cell = catalog.exterior_cells.front();
    assert(cell.worldspace.plugin == "World.esp"); assert(cell.worldspace.id == 0x100U);
    assert(cell.x == -3); assert(cell.y == 7); assert(cell.has_landscape);
    assert(cell.has_water); assert(cell.has_explicit_water_height); assert(cell.water_height == 96.0F);

    std::filesystem::remove_all(root); return 0;
}
