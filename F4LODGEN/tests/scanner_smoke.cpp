#include "f4lod/scanner.hpp"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

void touch(const std::filesystem::path& path) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file << 'x';
}

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file << text;
}

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

void append_u64(std::vector<unsigned char>& out, std::uint64_t value) {
    append_u32(out, static_cast<std::uint32_t>(value & 0xFFFFFFFFULL));
    append_u32(out, static_cast<std::uint32_t>(value >> 32U));
}

void append_signature(std::vector<unsigned char>& out, std::string_view signature) {
    assert(signature.size() == 4);
    out.insert(out.end(), signature.begin(), signature.end());
}

void append_subrecord(
    std::vector<unsigned char>& out,
    std::string_view type,
    const std::vector<unsigned char>& data) {
    append_signature(out, type);
    append_u16(out, static_cast<std::uint16_t>(data.size()));
    out.insert(out.end(), data.begin(), data.end());
}

void append_record(
    std::vector<unsigned char>& out,
    std::string_view type,
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

void append_group_header(std::vector<unsigned char>& out, std::uint32_t group_size) {
    append_signature(out, "GRUP");
    append_u32(out, group_size);
    append_signature(out, "WRLD");
    append_u32(out, 0);
    append_u32(out, 0);
    append_u32(out, 0);
}

void write_plugin(
    const std::filesystem::path& path,
    const std::vector<std::string>& masters,
    const std::vector<std::string>& records = {}) {
    std::filesystem::create_directories(path.parent_path());

    std::vector<unsigned char> tes4_data;
    append_subrecord(tes4_data, "HEDR", std::vector<unsigned char>(12, 0));
    for (const auto& master_name : masters) {
        std::vector<unsigned char> master(master_name.begin(), master_name.end());
        master.push_back(0);
        append_subrecord(tes4_data, "MAST", master);
    }

    std::vector<unsigned char> bytes;
    append_record(bytes, "TES4", tes4_data);

    if (!records.empty()) {
        constexpr std::uint32_t record_header_size = 24;
        append_group_header(
            bytes,
            record_header_size + static_cast<std::uint32_t>(records.size()) * record_header_size);
        std::uint32_t form_id = 0x01000001U;
        for (const auto& type : records) {
            append_record(bytes, type, {}, form_id++);
        }
    }

    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void write_general_ba2(
    const std::filesystem::path& path,
    const std::vector<std::string>& names) {
    constexpr std::uint64_t header_size = 24;
    constexpr std::uint64_t general_record_size = 36;
    const auto name_offset = header_size + general_record_size * names.size();

    std::vector<unsigned char> bytes;
    append_signature(bytes, "BTDX");
    append_u32(bytes, 1);
    append_signature(bytes, "GNRL");
    append_u32(bytes, static_cast<std::uint32_t>(names.size()));
    append_u64(bytes, name_offset);
    bytes.resize(static_cast<std::size_t>(name_offset), 0);

    for (const auto& name : names) {
        append_u16(bytes, static_cast<std::uint16_t>(name.size()));
        bytes.insert(bytes.end(), name.begin(), name.end());
    }

    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

bool has_code(const f4lod::ScanReport& report, const std::string& code) {
    for (const auto& diagnostic : report.diagnostics) {
        if (diagnostic.code == code) {
            return true;
        }
    }
    return false;
}

} // namespace

int main() {
    const auto root = std::filesystem::temp_directory_path() / "f4lodgen-scanner-smoke";
    std::filesystem::remove_all(root);

    const auto game = root / "Fallout 4";
    const auto data = game / "Data";
    const auto plugins = root / "profile" / "plugins.txt";

    touch(game / "Fallout4.exe");
    write_plugin(data / "Fallout4.esm", {});
    write_plugin(
        data / "TestA.esp",
        {"Fallout4.esm"},
        {"WRLD", "CELL", "LAND", "REFR", "STAT"});
    write_plugin(data / "TestB.esp", {"TestA.esp"}, {"REFR"});
    write_general_ba2(
        data / "Fallout4 - Meshes.ba2",
        {
            "Meshes\\Packed\\packed.nif",
            "Materials\\Packed\\packed.bgsm",
            "Textures\\Packed\\packed.dds",
        });
    touch(data / "Meshes" / "Architecture" / "test.nif");
    touch(data / "Materials" / "test.bgsm");
    touch(data / "Textures" / "test.dds");

    write_text(
        plugins,
        "\xEF\xBB\xBF# generated fixture\n"
        "*TestA.esp\n"
        "TestB.esp\n"
        "*Missing.esp\n"
        "*TestA.esp\n");

    auto report = f4lod::scan_environment(f4lod::ScanOptions{
        .game_root = game,
        .plugins_file = plugins,
    });

    assert(!report.success());
    assert(report.plugins.size() == 3);
    assert(report.enabled_plugin_count() == 2);
    assert(report.plugins[0].name == "TestA.esp");
    assert(report.plugins[0].enabled);
    assert(report.plugins[0].parsed);
    assert(report.plugins[0].masters.size() == 1);
    assert(report.plugins[0].records.worldspaces == 1);
    assert(report.plugins[1].name == "TestB.esp");
    assert(!report.plugins[1].enabled);
    assert(!report.plugins[1].parsed);
    assert(!report.plugins[2].exists);
    assert(has_code(report, "active-plugin-missing"));
    assert(has_code(report, "duplicate-plugin-entry"));
    assert(report.active_records.worldspaces == 1);
    assert(report.active_records.cells == 1);
    assert(report.active_records.landscapes == 1);
    assert(report.active_records.references == 1);
    assert(report.active_records.statics == 1);
    assert(report.assets.ba2_archives == 1);
    assert(report.assets.indexed_ba2_archives == 1);
    assert(report.assets.ba2_files == 3);
    assert(report.assets.visible_meshes == 1);
    assert(report.assets.visible_materials == 1);
    assert(report.assets.visible_textures == 1);
    assert(report.assets.packed_meshes == 1);
    assert(report.assets.packed_materials == 1);
    assert(report.assets.packed_textures == 1);

    write_plugin(data / "Missing.esp", {"Fallout4.esm"}, {"CELL"});
    write_text(plugins, "*TestA.esp\n*TestB.esp\n*Missing.esp\n");

    report = f4lod::scan_environment(f4lod::ScanOptions{
        .game_root = game,
        .plugins_file = plugins,
    });

    assert(report.success());
    assert(report.enabled_plugin_count() == 3);
    assert(!has_code(report, "active-plugin-missing"));
    assert(report.active_records.total_records == 10);
    assert(report.active_records.references == 2);
    assert(report.active_records.cells == 2);

    const auto text = f4lod::format_scan_report_text(report);
    assert(text.find("WRLD:  1") != std::string::npos);
    assert(text.find("indexed archives: 1/1") != std::string::npos);
    const auto json = f4lod::format_scan_report_json(report);
    assert(json.find("\"success\": true") != std::string::npos);
    assert(json.find("\"parsed\": true") != std::string::npos);
    assert(json.find("\"activeRecords\"") != std::string::npos);
    assert(json.find("\"packedMeshes\": 1") != std::string::npos);
    assert(json.find("TestA.esp") != std::string::npos);

    write_text(plugins, "*TestB.esp\n*TestA.esp\n");
    report = f4lod::scan_environment(f4lod::ScanOptions{
        .game_root = game,
        .plugins_file = plugins,
    });
    assert(!report.success());
    assert(has_code(report, "plugin-master-after-dependent"));

    write_text(plugins, "TestA.esp\n*TestB.esp\n");
    report = f4lod::scan_environment(f4lod::ScanOptions{
        .game_root = game,
        .plugins_file = plugins,
    });
    assert(!report.success());
    assert(has_code(report, "plugin-master-disabled"));

    std::filesystem::remove_all(root);
    return 0;
}
