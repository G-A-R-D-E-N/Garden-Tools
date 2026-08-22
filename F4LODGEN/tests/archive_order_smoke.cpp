#include "f4lod/archive_order.hpp"
#include "f4lod/assets.hpp"

#include <cassert>
#include <cstdint>
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
void u32(std::vector<unsigned char>& out, std::uint32_t value) {
    out.push_back(static_cast<unsigned char>(value & 0xFFU));
    out.push_back(static_cast<unsigned char>((value >> 8U) & 0xFFU));
    out.push_back(static_cast<unsigned char>((value >> 16U) & 0xFFU));
    out.push_back(static_cast<unsigned char>((value >> 24U) & 0xFFU));
}
void u64(std::vector<unsigned char>& out, std::uint64_t value) {
    u32(out, static_cast<std::uint32_t>(value & 0xFFFFFFFFULL));
    u32(out, static_cast<std::uint32_t>(value >> 32U));
}
void signature(std::vector<unsigned char>& out, std::string_view value) {
    assert(value.size() == 4U);
    out.insert(out.end(), value.begin(), value.end());
}

void write_general_ba2(
    const std::filesystem::path& path,
    const std::string& game_path,
    const std::string& payload) {
    constexpr std::uint64_t header_size = 24U;
    constexpr std::uint64_t record_size = 36U;
    const std::uint64_t name_offset = header_size + record_size;
    const std::uint64_t data_offset = name_offset + 2U + game_path.size();

    std::vector<unsigned char> bytes;
    signature(bytes, "BTDX");
    u32(bytes, 1U);
    signature(bytes, "GNRL");
    u32(bytes, 1U);
    u64(bytes, name_offset);

    u32(bytes, 0U); u32(bytes, 0U); u32(bytes, 0U); u32(bytes, 0xBAADF00DU);
    u64(bytes, data_offset);
    u32(bytes, 0U);
    u32(bytes, static_cast<std::uint32_t>(payload.size()));
    u32(bytes, 0U);

    u16(bytes, static_cast<std::uint16_t>(game_path.size()));
    bytes.insert(bytes.end(), game_path.begin(), game_path.end());
    bytes.insert(bytes.end(), payload.begin(), payload.end());

    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void write_texture_ba2(
    const std::filesystem::path& path,
    const std::string& game_path,
    const std::vector<unsigned char>& payload) {
    constexpr std::uint64_t header_size = 24U;
    constexpr std::uint64_t record_size = 24U;
    constexpr std::uint64_t chunk_size = 24U;
    const std::uint64_t name_offset = header_size + record_size + chunk_size;
    const std::uint64_t data_offset = name_offset + 2U + game_path.size();

    std::vector<unsigned char> bytes;
    signature(bytes, "BTDX");
    u32(bytes, 1U);
    signature(bytes, "DX10");
    u32(bytes, 1U);
    u64(bytes, name_offset);

    u32(bytes, 0U); u32(bytes, 0x00736464U); u32(bytes, 0U);
    bytes.push_back(0U);
    bytes.push_back(1U);
    u16(bytes, 24U);
    u16(bytes, 4U);
    u16(bytes, 4U);
    bytes.push_back(1U);
    bytes.push_back(71U);
    u16(bytes, 0U);

    u64(bytes, data_offset);
    u32(bytes, 0U);
    u32(bytes, static_cast<std::uint32_t>(payload.size()));
    u16(bytes, 0U); u16(bytes, 0U); u32(bytes, 0xBAADF00DU);

    u16(bytes, static_cast<std::uint16_t>(game_path.size()));
    bytes.insert(bytes.end(), game_path.begin(), game_path.end());
    bytes.insert(bytes.end(), payload.begin(), payload.end());

    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

std::string payload_text(const f4lod::AssetReadResult& result) {
    return std::string(result.data.begin(), result.data.end());
}

} // namespace

int main() {
    const auto root = std::filesystem::temp_directory_path() / "f4lodgen-archive-order-smoke";
    std::filesystem::remove_all(root);
    const auto data = root / "Data";
    std::filesystem::create_directories(data);

    constexpr std::string_view asset = "meshes\\terrain\\Commonwealth\\fixture.bin";
    constexpr std::string_view texture_asset = "textures\\landscape\\fixture.dds";
    const std::vector<unsigned char> texture_payload(8U, 0x5AU);

    write_general_ba2(data / "Index.ba2", std::string(asset), "index");
    write_general_ba2(data / "Startup.ba2", std::string(asset), "startup");
    write_general_ba2(data / "PrimaryOnly.ba2", std::string(asset), "primary");
    write_general_ba2(data / "Custom.ba2", std::string(asset), "custom");
    write_general_ba2(data / "A - Main.ba2", std::string(asset), "a");
    write_general_ba2(data / "B - Main.ba2", std::string(asset), "b");
    write_texture_ba2(data / "B - Textures.ba2", std::string(texture_asset), texture_payload);
    write_general_ba2(data / "Disabled - Main.ba2", std::string(asset), "disabled");

    const auto primary_ini = root / "Fallout4.ini";
    {
        std::ofstream out(primary_ini);
        out << "[Archive]\n"
            << "sResourceIndexFileList=Index.ba2\n"
            << "sResourceStartUpArchiveList=Startup.ba2\n"
            << "sResourceArchiveList=PrimaryOnly.ba2, A - Main.ba2\n"
            << "sResourceArchiveList2=Missing.ba2\n";
    }
    const auto custom_ini = root / "Fallout4Custom.ini";
    {
        std::ofstream out(custom_ini);
        out << "[Archive]\n"
            << "sResourceArchiveList=Custom.ba2, A - Main.ba2\n";
    }

    f4lod::ArchiveOrderOptions options;
    options.data_root = data;
    options.primary_ini = primary_ini;
    options.custom_ini = custom_ini;
    options.enabled_plugins_low_to_high = {"A.esp", "B.esp"};

    const auto order = f4lod::build_fallout4_archive_order(options);
    assert(order.valid);
    assert(order.entries_low_to_high.size() == 6U);
    assert(order.archives_low_to_high.size() == 6U);
    assert(order.general_archive_count == 5U);
    assert(order.texture_archive_count == 1U);
    assert(order.warnings.size() == 1U);

    assert(order.entries_low_to_high[0].path.filename() == "Index.ba2");
    assert(order.entries_low_to_high[1].path.filename() == "Startup.ba2");
    assert(order.entries_low_to_high[2].path.filename() == "Custom.ba2");
    assert(order.entries_low_to_high[3].path.filename() == "A - Main.ba2");
    assert(order.entries_low_to_high[4].path.filename() == "B - Main.ba2");
    assert(order.entries_low_to_high[5].path.filename() == "B - Textures.ba2");
    assert(order.entries_low_to_high[5].type == f4lod::Ba2ArchiveType::texture);
    assert(order.entries_low_to_high[0].source == f4lod::ArchiveOrderSource::ini);
    assert(order.entries_low_to_high[3].source == f4lod::ArchiveOrderSource::ini);
    assert(order.entries_low_to_high[4].source == f4lod::ArchiveOrderSource::plugin);
    assert(order.entries_low_to_high[5].source == f4lod::ArchiveOrderSource::plugin);

    for (const auto& entry : order.entries_low_to_high) {
        assert(entry.path.filename() != "PrimaryOnly.ba2");
        assert(entry.path.filename() != "Disabled - Main.ba2");
    }

    f4lod::AssetResolver resolver(data, order.archives_low_to_high);
    const auto resolved = resolver.read(std::string(asset));
    assert(resolved.valid);
    assert(resolved.source.container.filename() == "B - Main.ba2");
    assert(payload_text(resolved) == "b");

    const auto texture = resolver.read(std::string(texture_asset));
    assert(texture.valid);
    assert(texture.source.kind == f4lod::AssetSourceKind::texture_ba2);
    assert(texture.source.container.filename() == "B - Textures.ba2");
    assert(texture.data.size() == 148U + texture_payload.size());

    std::filesystem::create_directories(data / "meshes" / "terrain" / "Commonwealth");
    {
        std::ofstream loose(data / "meshes" / "terrain" / "Commonwealth" / "fixture.bin", std::ios::binary);
        loose << "loose";
    }
    const auto loose = resolver.read(std::string(asset));
    assert(loose.valid);
    assert(loose.source.kind == f4lod::AssetSourceKind::loose);
    assert(payload_text(loose) == "loose");

    assert(std::string(f4lod::archive_order_source_name(f4lod::ArchiveOrderSource::ini)) == "ini");
    assert(std::string(f4lod::archive_order_source_name(f4lod::ArchiveOrderSource::plugin)) == "plugin");

    std::filesystem::remove_all(root);
    return 0;
}
