#include "f4lod/plugin.hpp"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
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

void append_signature(std::vector<unsigned char>& out, std::string_view signature) {
    assert(signature.size() == 4);
    out.insert(out.end(), signature.begin(), signature.end());
}

std::uint32_t fourcc(std::string_view signature) {
    assert(signature.size() == 4);
    return static_cast<std::uint32_t>(static_cast<unsigned char>(signature[0])) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(signature[1])) << 8U) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(signature[2])) << 16U) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(signature[3])) << 24U);
}

void append_subrecord(
    std::vector<unsigned char>& out,
    std::string_view type,
    const std::vector<unsigned char>& data) {
    append_signature(out, type);
    append_u16(out, static_cast<std::uint16_t>(data.size()));
    out.insert(out.end(), data.begin(), data.end());
}

std::vector<unsigned char> make_record(
    std::string_view type,
    std::uint32_t form_id,
    const std::vector<unsigned char>& data = {}) {
    std::vector<unsigned char> out;
    append_signature(out, type);
    append_u32(out, static_cast<std::uint32_t>(data.size()));
    append_u32(out, 0);
    append_u32(out, form_id);
    append_u32(out, 0);
    append_u16(out, 131);
    append_u16(out, 0);
    out.insert(out.end(), data.begin(), data.end());
    return out;
}

std::vector<unsigned char> make_group(
    std::uint32_t label,
    std::uint32_t type,
    const std::vector<unsigned char>& payload) {
    std::vector<unsigned char> out;
    append_signature(out, "GRUP");
    append_u32(out, static_cast<std::uint32_t>(24 + payload.size()));
    append_u32(out, label);
    append_u32(out, type);
    append_u32(out, 0);
    append_u32(out, 0);
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

void append_bytes(std::vector<unsigned char>& out, const std::vector<unsigned char>& bytes) {
    out.insert(out.end(), bytes.begin(), bytes.end());
}

} // namespace

int main() {
    const auto root = std::filesystem::temp_directory_path() / "f4lodgen-group-context-smoke";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    constexpr std::uint32_t world_form = 0x01000100U;
    constexpr std::uint32_t cell_form = 0x01000200U;
    constexpr std::uint32_t land_form = 0x01000300U;
    constexpr std::uint32_t refr_form = 0x01000400U;

    std::vector<unsigned char> tes4_data;
    append_subrecord(tes4_data, "HEDR", std::vector<unsigned char>(12, 0));
    std::vector<unsigned char> master{'F','a','l','l','o','u','t','4','.','e','s','m',0};
    append_subrecord(tes4_data, "MAST", master);

    std::vector<unsigned char> temporary_payload;
    append_bytes(temporary_payload, make_record("LAND", land_form));
    append_bytes(temporary_payload, make_record("REFR", refr_form));
    const auto temporary_group = make_group(cell_form, 9, temporary_payload);

    std::vector<unsigned char> cell_children_payload;
    append_bytes(cell_children_payload, temporary_group);
    const auto cell_children_group = make_group(cell_form, 6, cell_children_payload);

    std::vector<unsigned char> subblock_payload;
    append_bytes(subblock_payload, make_record("CELL", cell_form));
    append_bytes(subblock_payload, cell_children_group);
    const auto subblock_group = make_group(0, 5, subblock_payload);

    std::vector<unsigned char> block_payload;
    append_bytes(block_payload, subblock_group);
    const auto block_group = make_group(0, 4, block_payload);

    std::vector<unsigned char> world_children_payload;
    append_bytes(world_children_payload, block_group);
    const auto world_children_group = make_group(world_form, 1, world_children_payload);

    std::vector<unsigned char> top_payload;
    append_bytes(top_payload, make_record("WRLD", world_form));
    append_bytes(top_payload, world_children_group);
    const auto top_group = make_group(fourcc("WRLD"), 0, top_payload);

    std::vector<unsigned char> plugin;
    append_bytes(plugin, make_record("TES4", 0, tes4_data));
    append_bytes(plugin, top_group);

    const auto path = root / "World.esp";
    {
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(plugin.data()), static_cast<std::streamsize>(plugin.size()));
    }

    const auto index = f4lod::index_lod_records(path);
    assert(index.inspection.valid);
    assert(index.lod_records.size() == 4);

    for (const auto& record : index.lod_records) {
        if (record.type == "WRLD") {
            assert(record.parent_world_raw_form_id == 0);
            assert(record.parent_cell_raw_form_id == 0);
        } else if (record.type == "CELL") {
            assert(record.parent_world_raw_form_id == world_form);
            assert(record.parent_cell_raw_form_id == 0);
        } else if (record.type == "LAND" || record.type == "REFR") {
            assert(record.parent_world_raw_form_id == world_form);
            assert(record.parent_cell_raw_form_id == cell_form);
        } else {
            assert(false);
        }
    }

    std::filesystem::remove_all(root);
    return 0;
}
