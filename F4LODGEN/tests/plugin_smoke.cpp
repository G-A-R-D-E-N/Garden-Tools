#include "f4lod/plugin.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
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

void append_signature(std::vector<unsigned char>& out, const char (&type)[5]) {
    out.insert(out.end(), type, type + 4);
}

void append_record(
    std::vector<unsigned char>& out,
    const char (&type)[5],
    const std::vector<unsigned char>& data = {},
    std::uint32_t form_id = 0) {
    append_signature(out, type);
    append_u32(out, static_cast<std::uint32_t>(data.size()));
    append_u32(out, 0);
    append_u32(out, form_id);
    append_u32(out, 0);
    append_u16(out, 131);
    append_u16(out, 0);
    out.insert(out.end(), data.begin(), data.end());
}

void append_subrecord(
    std::vector<unsigned char>& out,
    const char (&type)[5],
    const std::vector<unsigned char>& data) {
    append_signature(out, type);
    append_u16(out, static_cast<std::uint16_t>(data.size()));
    out.insert(out.end(), data.begin(), data.end());
}

void append_group_header(std::vector<unsigned char>& out, std::uint32_t group_size) {
    append_signature(out, "GRUP");
    append_u32(out, group_size);
    append_signature(out, "WRLD");
    append_u32(out, 0);
    append_u32(out, 0);
    append_u32(out, 0);
}

void write_binary(const std::filesystem::path& path, const std::vector<unsigned char>& bytes) {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

} // namespace

int main() {
    const auto root = std::filesystem::temp_directory_path() / "f4lodgen-plugin-smoke";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    std::vector<unsigned char> tes4_data;
    std::vector<unsigned char> hedr(12, 0);
    append_subrecord(tes4_data, "HEDR", hedr);

    const std::string master_name = "Fallout4.esm";
    std::vector<unsigned char> master(master_name.begin(), master_name.end());
    master.push_back(0);
    append_subrecord(tes4_data, "MAST", master);

    std::vector<unsigned char> plugin;
    append_record(plugin, "TES4", tes4_data);

    constexpr std::uint32_t record_count_in_group = 5;
    constexpr std::uint32_t record_header_size = 24;
    append_group_header(plugin, record_header_size + record_count_in_group * record_header_size);
    append_record(plugin, "WRLD", {}, 0x01000001U);
    append_record(plugin, "CELL", {}, 0x01000002U);
    append_record(plugin, "LAND", {}, 0x01000003U);
    append_record(plugin, "REFR", {}, 0x01000004U);
    append_record(plugin, "STAT", {}, 0x01000005U);

    const auto valid_path = root / "Fixture.esp";
    write_binary(valid_path, plugin);

    const auto inspection = f4lod::inspect_plugin(valid_path);
    assert(inspection.valid);
    assert(inspection.error.empty());
    assert(inspection.masters.size() == 1);
    assert(inspection.masters[0] == "Fallout4.esm");
    assert(inspection.records.total_records == 6);
    assert(inspection.records.groups == 1);
    assert(inspection.records.worldspaces == 1);
    assert(inspection.records.cells == 1);
    assert(inspection.records.landscapes == 1);
    assert(inspection.records.references == 1);
    assert(inspection.records.statics == 1);

    const auto json = f4lod::format_plugin_inspection_json(inspection);
    assert(json.find("\"valid\": true") != std::string::npos);
    assert(json.find("Fallout4.esm") != std::string::npos);

    const auto broken_path = root / "Broken.esp";
    write_binary(broken_path, {0x01, 0x02, 0x03});
    assert(!f4lod::inspect_plugin(broken_path).valid);

    std::vector<unsigned char> leading_group;
    append_group_header(leading_group, 24);
    const auto leading_group_path = root / "LeadingGroup.esp";
    write_binary(leading_group_path, leading_group);
    const auto leading_group_result = f4lod::inspect_plugin(leading_group_path);
    assert(!leading_group_result.valid);
    assert(leading_group_result.error == "First plugin record is not TES4.");

    std::vector<unsigned char> duplicate_tes4;
    append_record(duplicate_tes4, "TES4", tes4_data);
    append_record(duplicate_tes4, "TES4", tes4_data);
    const auto duplicate_tes4_path = root / "DuplicateTES4.esp";
    write_binary(duplicate_tes4_path, duplicate_tes4);
    const auto duplicate_tes4_result = f4lod::inspect_plugin(duplicate_tes4_path);
    assert(!duplicate_tes4_result.valid);
    assert(duplicate_tes4_result.error == "Plugin contains a second TES4 header record.");

    std::filesystem::remove_all(root);
    return 0;
}
