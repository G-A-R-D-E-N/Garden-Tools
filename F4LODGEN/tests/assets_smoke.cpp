#include "f4lod/assets.hpp"

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

void append_u64(std::vector<unsigned char>& out, std::uint64_t value) {
    append_u32(out, static_cast<std::uint32_t>(value & 0xFFFFFFFFULL));
    append_u32(out, static_cast<std::uint32_t>(value >> 32U));
}

void append_signature(std::vector<unsigned char>& out, std::string_view signature) {
    assert(signature.size() == 4);
    out.insert(out.end(), signature.begin(), signature.end());
}

void write_single_general_ba2(
    const std::filesystem::path& path,
    const std::string& name,
    const std::string& payload) {
    constexpr std::uint64_t header_size = 24;
    constexpr std::uint64_t record_size = 36;
    const std::uint64_t name_offset = header_size + record_size;
    const std::uint64_t data_offset = name_offset + 2 + name.size();

    std::filesystem::create_directories(path.parent_path());
    std::vector<unsigned char> bytes;
    append_signature(bytes, "BTDX");
    append_u32(bytes, 1);
    append_signature(bytes, "GNRL");
    append_u32(bytes, 1);
    append_u64(bytes, name_offset);

    append_u32(bytes, 0);
    append_u32(bytes, 0);
    append_u32(bytes, 0);
    append_u32(bytes, 0xBAADF00DU);
    append_u64(bytes, data_offset);
    append_u32(bytes, 0);
    append_u32(bytes, static_cast<std::uint32_t>(payload.size()));
    append_u32(bytes, 0);

    append_u16(bytes, static_cast<std::uint16_t>(name.size()));
    bytes.insert(bytes.end(), name.begin(), name.end());
    bytes.insert(bytes.end(), payload.begin(), payload.end());

    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

std::string text(const f4lod::AssetReadResult& result) {
    return std::string(result.data.begin(), result.data.end());
}

} // namespace

int main() {
    const auto root = std::filesystem::temp_directory_path() / "f4lodgen-assets-smoke";
    std::filesystem::remove_all(root);
    const auto data = root / "Data";
    std::filesystem::create_directories(data);

    const auto low_path = data / "Low.ba2";
    const auto high_path = data / "High.ba2";
    write_single_general_ba2(low_path, "lodsettings\\Commonwealth.lod", "low");
    write_single_general_ba2(high_path, "LODSETTINGS\\COMMONWEALTH.LOD", "high");

    const auto low = f4lod::inspect_ba2_index(low_path);
    const auto high = f4lod::inspect_ba2_index(high_path);
    assert(low.valid);
    assert(high.valid);

    const auto ambiguous = f4lod::discover_asset_sources(data, "lodsettings/Commonwealth.lod");
    assert(ambiguous.valid);
    assert(!ambiguous.loose_exists);
    assert(ambiguous.matching_archives.size() == 2U);
    assert(ambiguous.matching_archives[0].type == f4lod::Ba2ArchiveType::general);
    assert(ambiguous.matching_archives[1].type == f4lod::Ba2ArchiveType::general);

    f4lod::AssetResolver resolver(data, {low, high});
    auto result = resolver.read("lodsettings/Commonwealth.lod");
    assert(result.valid);
    assert(result.source.kind == f4lod::AssetSourceKind::general_ba2);
    assert(result.source.container == high_path);
    assert(text(result) == "high");

    std::filesystem::create_directories(data / "lodsettings");
    {
        std::ofstream file(data / "lodsettings" / "Commonwealth.lod", std::ios::binary);
        file << "loose";
    }

    const auto loose_discovery = f4lod::discover_asset_sources(data, "lodsettings/Commonwealth.lod");
    assert(loose_discovery.valid);
    assert(loose_discovery.loose_exists);
    assert(loose_discovery.matching_archives.empty());

    result = resolver.read("lodsettings/Commonwealth.lod");
    assert(result.valid);
    assert(result.source.kind == f4lod::AssetSourceKind::loose);
    assert(text(result) == "loose");

    assert(!resolver.read("../outside.bin").valid);
    assert(!resolver.read("C:\\absolute.bin").valid);
    assert(!resolver.read("missing.bin").valid);
    assert(!f4lod::discover_asset_sources(data, "../outside.bin").valid);
    assert(std::string(f4lod::asset_source_kind_name(f4lod::AssetSourceKind::general_ba2)) == "general-ba2");

    std::filesystem::remove_all(root);
    return 0;
}
