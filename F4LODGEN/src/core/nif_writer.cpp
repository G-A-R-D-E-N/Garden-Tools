#include "f4lod/nif_writer.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <unordered_map>

namespace f4lod {
namespace {

constexpr std::string_view kHeader = "Gamebryo File Format, Version 20.2.0.7";
constexpr std::uint32_t kVersion = 335675399U;
constexpr std::uint32_t kUserVersion = 12U;
constexpr std::uint32_t kBethesdaVersion = 130U;

void write_u8(std::ofstream& out, std::uint8_t value) {
    out.put(static_cast<char>(value));
}

void write_u16(std::ofstream& out, std::uint16_t value) {
    write_u8(out, static_cast<std::uint8_t>(value & 0xFFU));
    write_u8(out, static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
}

void write_u32(std::ofstream& out, std::uint32_t value) {
    write_u8(out, static_cast<std::uint8_t>(value & 0xFFU));
    write_u8(out, static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    write_u8(out, static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
    write_u8(out, static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
}

void write_i32(std::ofstream& out, std::int32_t value) {
    write_u32(out, static_cast<std::uint32_t>(value));
}

bool write_header_string(std::ofstream& out, const std::string& value) {
    if (value.size() >= 255U) return false;
    write_u8(out, static_cast<std::uint8_t>(value.size() + 1U));
    out.write(value.data(), static_cast<std::streamsize>(value.size()));
    write_u8(out, 0);
    return static_cast<bool>(out);
}

bool write_sized_string(std::ofstream& out, const std::string& value) {
    if (value.size() > std::numeric_limits<std::uint32_t>::max()) return false;
    write_u32(out, static_cast<std::uint32_t>(value.size()));
    out.write(value.data(), static_cast<std::streamsize>(value.size()));
    return static_cast<bool>(out);
}

} // namespace

NifWriteResult write_fallout4_nif(
    const std::filesystem::path& path,
    const NifWriteDocument& document) {
    NifWriteResult result;

    if (document.blocks.empty()) {
        result.error = "NIF document contains no blocks.";
        return result;
    }
    if (document.blocks.size() > std::numeric_limits<std::uint32_t>::max()) {
        result.error = "NIF document has too many blocks.";
        return result;
    }
    if (document.roots.empty()) {
        result.error = "NIF document contains no root references.";
        return result;
    }
    for (const auto root : document.roots) {
        if (root < 0 || static_cast<std::size_t>(root) >= document.blocks.size()) {
            result.error = "NIF document contains an invalid root reference.";
            return result;
        }
    }

    std::vector<std::string> block_types;
    std::vector<std::uint16_t> block_type_indices;
    block_type_indices.reserve(document.blocks.size());
    std::unordered_map<std::string, std::uint16_t> type_lookup;

    for (const auto& block : document.blocks) {
        if (block.type.empty()) {
            result.error = "NIF block has an empty type name.";
            return result;
        }
        if (block.data.size() > std::numeric_limits<std::uint32_t>::max()) {
            result.error = "NIF block is larger than the 32-bit block-size field.";
            return result;
        }

        const auto found = type_lookup.find(block.type);
        if (found != type_lookup.end()) {
            block_type_indices.push_back(found->second);
            continue;
        }
        if (block_types.size() >= std::numeric_limits<std::uint16_t>::max()) {
            result.error = "NIF document has too many unique block types.";
            return result;
        }
        const auto index = static_cast<std::uint16_t>(block_types.size());
        block_types.push_back(block.type);
        type_lookup.emplace(block.type, index);
        block_type_indices.push_back(index);
    }

    if (document.strings.size() > std::numeric_limits<std::uint32_t>::max()) {
        result.error = "NIF document has too many strings.";
        return result;
    }
    std::uint32_t max_string_length = 0;
    for (const auto& value : document.strings) {
        if (value.size() > std::numeric_limits<std::uint32_t>::max()) {
            result.error = "NIF string is too long.";
            return result;
        }
        max_string_length = std::max(max_string_length, static_cast<std::uint32_t>(value.size()));
    }

    std::error_code ec;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            result.error = "Could not create NIF output directory: " + ec.message();
            return result;
        }
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        result.error = "Could not create NIF output file.";
        return result;
    }

    out.write(kHeader.data(), static_cast<std::streamsize>(kHeader.size()));
    write_u8(out, '\n');
    write_u32(out, kVersion);
    write_u8(out, 1U);
    write_u32(out, kUserVersion);
    write_u32(out, static_cast<std::uint32_t>(document.blocks.size()));
    write_u32(out, kBethesdaVersion);

    if (!write_header_string(out, document.creator) ||
        !write_header_string(out, "") ||
        !write_header_string(out, "") ||
        !write_header_string(out, "")) {
        result.error = "Could not write NIF export-info strings.";
        return result;
    }

    write_u16(out, static_cast<std::uint16_t>(block_types.size()));
    for (const auto& type : block_types) {
        if (!write_sized_string(out, type)) {
            result.error = "Could not write NIF block-type table.";
            return result;
        }
    }
    for (const auto index : block_type_indices) write_u16(out, index);
    for (const auto& block : document.blocks) {
        write_u32(out, static_cast<std::uint32_t>(block.data.size()));
    }

    write_u32(out, static_cast<std::uint32_t>(document.strings.size()));
    write_u32(out, max_string_length);
    for (const auto& value : document.strings) {
        if (!write_sized_string(out, value)) {
            result.error = "Could not write NIF string table.";
            return result;
        }
    }
    write_u32(out, 0U); // Unknown/unused header field for this NIF version.

    for (const auto& block : document.blocks) {
        if (!block.data.empty()) {
            out.write(reinterpret_cast<const char*>(block.data.data()),
                      static_cast<std::streamsize>(block.data.size()));
        }
    }

    write_u32(out, static_cast<std::uint32_t>(document.roots.size()));
    for (const auto root : document.roots) write_i32(out, root);

    if (!out) {
        result.error = "Failed while writing NIF data.";
        return result;
    }

    const auto position = out.tellp();
    if (position < 0) {
        result.error = "Could not determine written NIF size.";
        return result;
    }
    result.bytes_written = static_cast<std::uint64_t>(position);
    result.valid = true;
    return result;
}

} // namespace f4lod
