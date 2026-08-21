#include "f4lod/nif.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>
#include <string_view>
#include <utility>

namespace f4lod {
namespace {

constexpr std::uint32_t kNifVersion20207 = 335675399U;
constexpr std::uint32_t kOldNifVersionThreshold = 335544325U;
constexpr std::size_t kMaxHeaderString = 4096;
constexpr std::size_t kMaxTableEntries = 1'000'000;

class Cursor {
public:
    explicit Cursor(
        const std::vector<unsigned char>& data,
        std::size_t position = 0,
        std::size_t end = std::numeric_limits<std::size_t>::max())
        : data_(data), position_(position), end_(std::min(end, data.size())) {}

    [[nodiscard]] std::size_t position() const noexcept { return position_; }
    [[nodiscard]] std::size_t remaining() const noexcept {
        return position_ <= end_ ? end_ - position_ : 0;
    }

    bool read_u8(std::uint8_t& value) {
        if (remaining() < 1) return false;
        value = data_[position_++];
        return true;
    }

    bool read_u16(std::uint16_t& value) {
        if (remaining() < 2) return false;
        value = static_cast<std::uint16_t>(data_[position_]) |
                (static_cast<std::uint16_t>(data_[position_ + 1]) << 8U);
        position_ += 2;
        return true;
    }

    bool read_u32(std::uint32_t& value) {
        if (remaining() < 4) return false;
        value = static_cast<std::uint32_t>(data_[position_]) |
                (static_cast<std::uint32_t>(data_[position_ + 1]) << 8U) |
                (static_cast<std::uint32_t>(data_[position_ + 2]) << 16U) |
                (static_cast<std::uint32_t>(data_[position_ + 3]) << 24U);
        position_ += 4;
        return true;
    }

    bool read_i32(std::int32_t& value) {
        std::uint32_t bits = 0;
        if (!read_u32(bits)) return false;
        std::memcpy(&value, &bits, sizeof(value));
        return true;
    }

    bool read_f32(float& value) {
        std::uint32_t bits = 0;
        if (!read_u32(bits)) return false;
        std::memcpy(&value, &bits, sizeof(value));
        return true;
    }

    bool skip(std::size_t count) {
        if (count > remaining()) return false;
        position_ += count;
        return true;
    }

    bool read_bytes(std::size_t count, std::string& value) {
        if (count > remaining()) return false;
        value.assign(reinterpret_cast<const char*>(data_.data() + position_), count);
        position_ += count;
        return true;
    }

private:
    const std::vector<unsigned char>& data_;
    std::size_t position_{};
    std::size_t end_{};
};

std::vector<unsigned char> read_file(
    const std::filesystem::path& path,
    std::string& error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "Could not open NIF/BTR file.";
        return {};
    }
    input.seekg(0, std::ios::end);
    const auto end = input.tellg();
    if (end < 0) {
        error = "Could not determine NIF/BTR file size.";
        return {};
    }
    const auto size = static_cast<std::uint64_t>(end);
    if (size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        error = "NIF/BTR file is too large for this process.";
        return {};
    }
    input.seekg(0, std::ios::beg);
    std::vector<unsigned char> data(static_cast<std::size_t>(size));
    if (!data.empty()) {
        input.read(
            reinterpret_cast<char*>(data.data()),
            static_cast<std::streamsize>(data.size()));
        if (input.gcount() != static_cast<std::streamsize>(data.size())) {
            error = "NIF/BTR file is truncated while reading.";
            return {};
        }
    }
    return data;
}

std::string trim_nul(std::string value) {
    while (!value.empty() && value.back() == '\0') value.pop_back();
    return value;
}

bool read_short_string(Cursor& cursor, std::string& value) {
    std::uint8_t size = 0;
    if (!cursor.read_u8(size)) return false;
    if (!cursor.read_bytes(size, value)) return false;
    value = trim_nul(std::move(value));
    return true;
}

bool read_sized_string(Cursor& cursor, std::string& value) {
    std::uint32_t size = 0;
    if (!cursor.read_u32(size)) return false;
    if (size > kMaxHeaderString || size > cursor.remaining()) return false;
    return cursor.read_bytes(size, value);
}

bool read_line(Cursor& cursor, std::string& value) {
    value.clear();
    while (value.size() <= kMaxHeaderString) {
        std::uint8_t c = 0;
        if (!cursor.read_u8(c)) return false;
        if (c == '\n') return true;
        value.push_back(static_cast<char>(c));
    }
    return false;
}

bool valid_ref(std::int32_t ref, std::size_t block_count) noexcept {
    return ref >= 0 && static_cast<std::size_t>(ref) < block_count;
}

bool is_terrain_shape(std::string_view type) noexcept {
    return type == "BSTriShape" || type == "BSSubIndexTriShape" ||
           type == "BSSegmentedTriShape" || type == "NiTriShape" ||
           type == "BSLODTriShape" || type == "BSMeshLODTriShape";
}

bool parse_root_node(
    const std::vector<unsigned char>& data,
    BtrInspection& result) {
    if (result.nif.blocks.empty() ||
        result.nif.blocks.front().type != "BSMultiBoundNode") {
        result.error = "BTR root block is not BSMultiBoundNode.";
        return false;
    }

    const auto& block = result.nif.blocks.front();
    const auto end = block.offset + block.size;
    if (end > data.size()) {
        result.error = "BTR root block extends past end of file.";
        return false;
    }
    Cursor cursor(
        data,
        static_cast<std::size_t>(block.offset),
        static_cast<std::size_t>(end));

    if (!cursor.read_i32(result.root_name_index)) {
        result.error = "BTR root node is truncated at name index.";
        return false;
    }

    std::uint32_t extra_count = 0;
    if (!cursor.read_u32(extra_count) || extra_count > kMaxTableEntries ||
        !cursor.skip(static_cast<std::size_t>(extra_count) * 4U)) {
        result.error = "BTR root node has invalid extra-data references.";
        return false;
    }

    std::int32_t controller = -1;
    if (!cursor.read_i32(controller)) {
        result.error = "BTR root node is truncated at controller reference.";
        return false;
    }

    std::uint16_t flags = 0;
    if (!cursor.read_u16(flags)) {
        result.error = "BTR root node is truncated at flags.";
        return false;
    }
    if (result.nif.user_version >= 11U && result.nif.user_version2 >= 26U) {
        std::uint16_t flags2 = 0;
        if (!cursor.read_u16(flags2)) {
            result.error = "BTR root node is truncated at secondary flags.";
            return false;
        }
    }

    if (!cursor.skip(12U + 36U + 4U)) {
        result.error = "BTR root node is truncated in transform data.";
        return false;
    }

    if (result.nif.version <= kOldNifVersionThreshold ||
        result.nif.user_version <= 11U) {
        std::uint32_t property_count = 0;
        if (!cursor.read_u32(property_count) ||
            property_count > kMaxTableEntries ||
            !cursor.skip(static_cast<std::size_t>(property_count) * 4U)) {
            result.error = "BTR root node has invalid property references.";
            return false;
        }
    }

    std::int32_t collision = -1;
    if (!cursor.read_i32(collision)) {
        result.error = "BTR root node is truncated at collision reference.";
        return false;
    }

    std::uint32_t child_count = 0;
    if (!cursor.read_u32(child_count) || child_count > kMaxTableEntries ||
        static_cast<std::size_t>(child_count) * 4U > cursor.remaining()) {
        result.error = "BTR root node has invalid child references.";
        return false;
    }
    result.root_children.reserve(child_count);
    for (std::uint32_t i = 0; i < child_count; ++i) {
        std::int32_t child = -1;
        if (!cursor.read_i32(child)) {
            result.error = "BTR root node is truncated inside child references.";
            return false;
        }
        result.root_children.push_back(child);
    }

    if (result.nif.user_version2 < 130U) {
        std::uint32_t effect_count = 0;
        if (!cursor.read_u32(effect_count) || effect_count > kMaxTableEntries ||
            !cursor.skip(static_cast<std::size_t>(effect_count) * 4U)) {
            result.error = "BTR root node has invalid effect references.";
            return false;
        }
    }

    if (!cursor.read_i32(result.root_multibound)) {
        result.error = "BTR root node is truncated at multibound reference.";
        return false;
    }
    if (result.nif.user_version >= 12U &&
        !cursor.read_u32(result.root_cull_mode)) {
        result.error = "BTR root node is truncated at cull mode.";
        return false;
    }

    if (cursor.remaining() != 0) {
        result.error = "BTR root block size does not match BSMultiBoundNode layout.";
        return false;
    }

    const auto block_count = result.nif.blocks.size();
    if (!valid_ref(result.root_multibound, block_count) ||
        result.nif.blocks[static_cast<std::size_t>(result.root_multibound)].type !=
            "BSMultiBound") {
        result.error = "BTR root multibound reference is invalid.";
        return false;
    }
    if (result.root_children.empty()) {
        result.error = "BTR root node has no terrain child.";
        return false;
    }
    for (const auto child : result.root_children) {
        if (!valid_ref(child, block_count)) {
            result.error = "BTR root node contains an out-of-range child reference.";
            return false;
        }
    }

    result.terrain_shape = result.root_children.front();
    const auto& terrain_type =
        result.nif.blocks[static_cast<std::size_t>(result.terrain_shape)].type;
    if (!is_terrain_shape(terrain_type)) {
        result.error = "BTR first root child is not a supported terrain geometry block.";
        return false;
    }

    if (result.root_children.size() > 1) {
        const auto candidate = result.root_children[1];
        if (result.nif.blocks[static_cast<std::size_t>(candidate)].type ==
            "BSMultiBoundNode") {
            result.water_node = candidate;
        }
    }

    return true;
}

bool parse_bounds(
    const std::vector<unsigned char>& data,
    BtrInspection& result) {
    const auto& multibound =
        result.nif.blocks[static_cast<std::size_t>(result.root_multibound)];
    if (multibound.size < 4U ||
        multibound.offset + multibound.size > data.size()) {
        result.error = "BTR BSMultiBound block is truncated.";
        return false;
    }
    Cursor mb_cursor(
        data,
        static_cast<std::size_t>(multibound.offset),
        static_cast<std::size_t>(multibound.offset + multibound.size));
    if (!mb_cursor.read_i32(result.bounds_data) || mb_cursor.remaining() != 0) {
        result.error = "BTR BSMultiBound block has an invalid size.";
        return false;
    }

    if (!valid_ref(result.bounds_data, result.nif.blocks.size())) {
        result.error = "BTR multibound data reference is out of range.";
        return false;
    }
    const auto& aabb =
        result.nif.blocks[static_cast<std::size_t>(result.bounds_data)];
    if (aabb.type != "BSMultiBoundAABB" || aabb.size != 24U ||
        aabb.offset + aabb.size > data.size()) {
        result.error = "BTR multibound data is not a valid BSMultiBoundAABB.";
        return false;
    }

    Cursor aabb_cursor(
        data,
        static_cast<std::size_t>(aabb.offset),
        static_cast<std::size_t>(aabb.offset + aabb.size));
    for (float& value : result.bounds_position) {
        if (!aabb_cursor.read_f32(value)) {
            result.error = "BTR AABB position is truncated.";
            return false;
        }
    }
    for (float& value : result.bounds_extent) {
        if (!aabb_cursor.read_f32(value)) {
            result.error = "BTR AABB extent is truncated.";
            return false;
        }
    }
    if (aabb_cursor.remaining() != 0) {
        result.error = "BTR AABB block has trailing bytes.";
        return false;
    }
    return true;
}

} // namespace

bool NifInspection::is_fallout4() const noexcept {
    return valid &&
           header_string == "Gamebryo File Format, Version 20.2.0.7" &&
           version == kNifVersion20207 && endian_type == 1U &&
           user_version == 12U && user_version2 == 130U;
}

NifInspection inspect_nif(const std::filesystem::path& path) {
    NifInspection result;
    result.path = path;
    std::string file_error;
    const auto data = read_file(path, file_error);
    if (!file_error.empty()) {
        result.error = std::move(file_error);
        return result;
    }
    if (data.empty()) {
        result.error = "NIF/BTR file is empty.";
        return result;
    }

    Cursor cursor(data);
    if (!read_line(cursor, result.header_string)) {
        result.error = "NIF header string is missing or too long.";
        return result;
    }

    std::uint32_t num_blocks = 0;
    if (!cursor.read_u32(result.version) ||
        !cursor.read_u8(result.endian_type) ||
        !cursor.read_u32(result.user_version) ||
        !cursor.read_u32(num_blocks) ||
        !cursor.read_u32(result.user_version2)) {
        result.error = "NIF header is truncated before block metadata.";
        return result;
    }
    if (result.endian_type != 1U) {
        result.error = "Only little-endian NIF files are supported.";
        return result;
    }
    if (num_blocks > kMaxTableEntries) {
        result.error = "NIF declares an unreasonable number of blocks.";
        return result;
    }

    std::string export_info;
    if (!read_short_string(cursor, result.creator) ||
        !read_short_string(cursor, export_info) ||
        !read_short_string(cursor, export_info)) {
        result.error = "NIF export-info header strings are truncated.";
        return result;
    }
    if (result.version == kNifVersion20207 && result.user_version2 == 130U) {
        if (!read_short_string(cursor, export_info)) {
            result.error = "FO4 NIF fourth export-info string is truncated.";
            return result;
        }
    }

    std::uint16_t num_block_types = 0;
    if (!cursor.read_u16(num_block_types) ||
        num_block_types > num_blocks + 256U) {
        result.error = "NIF block-type table is invalid.";
        return result;
    }
    result.block_types.reserve(num_block_types);
    for (std::uint16_t i = 0; i < num_block_types; ++i) {
        std::string type;
        if (!read_sized_string(cursor, type) || type.empty()) {
            result.error =
                "NIF block-type table contains a truncated or empty type name.";
            return result;
        }
        result.block_types.push_back(std::move(type));
    }

    std::vector<std::uint16_t> type_indices;
    type_indices.reserve(num_blocks);
    for (std::uint32_t i = 0; i < num_blocks; ++i) {
        std::uint16_t index = 0;
        if (!cursor.read_u16(index) || index >= result.block_types.size()) {
            result.error = "NIF block references an invalid block-type index.";
            return result;
        }
        type_indices.push_back(index);
    }

    std::vector<std::uint32_t> sizes(num_blocks, 0U);
    if (result.version > kOldNifVersionThreshold) {
        for (std::uint32_t i = 0; i < num_blocks; ++i) {
            if (!cursor.read_u32(sizes[i])) {
                result.error = "NIF block-size table is truncated.";
                return result;
            }
        }

        std::uint32_t num_strings = 0;
        std::uint32_t max_string_length = 0;
        if (!cursor.read_u32(num_strings) ||
            !cursor.read_u32(max_string_length) ||
            num_strings > kMaxTableEntries) {
            result.error = "NIF string table metadata is invalid.";
            return result;
        }
        result.strings.reserve(num_strings);
        for (std::uint32_t i = 0; i < num_strings; ++i) {
            std::string value;
            if (!read_sized_string(cursor, value)) {
                result.error = "NIF string table is truncated.";
                return result;
            }
            if (value.size() > max_string_length && max_string_length != 0U) {
                result.error = "NIF string exceeds declared maximum string length.";
                return result;
            }
            result.strings.push_back(std::move(value));
        }
    } else {
        result.error = "Pre-20.2 NIF streams are outside the v0.1 FO4 scope.";
        return result;
    }

    std::uint32_t unknown = 0;
    if (!cursor.read_u32(unknown)) {
        result.error = "NIF header is truncated before block data.";
        return result;
    }
    result.data_offset = cursor.position();

    std::uint64_t block_offset = result.data_offset;
    result.blocks.reserve(num_blocks);
    for (std::uint32_t i = 0; i < num_blocks; ++i) {
        const auto size = sizes[i];
        if (block_offset > data.size() ||
            static_cast<std::uint64_t>(size) > data.size() - block_offset) {
            result.error = "NIF block-size table extends beyond the file.";
            return result;
        }
        result.blocks.push_back(NifBlockInfo{
            result.block_types[type_indices[i]],
            size,
            block_offset,
        });
        block_offset += size;
    }

    result.footer_offset = block_offset;
    if (block_offset > data.size()) {
        result.error = "NIF footer offset is outside the file.";
        return result;
    }

    Cursor footer(
        data,
        static_cast<std::size_t>(block_offset),
        data.size());
    std::uint32_t root_count = 0;
    if (!footer.read_u32(root_count) || root_count > kMaxTableEntries) {
        result.error = "NIF footer root table is missing or invalid.";
        return result;
    }
    if (static_cast<std::uint64_t>(root_count) * 4ULL > footer.remaining()) {
        result.error = "NIF footer root table is truncated.";
        return result;
    }

    result.roots.reserve(root_count);
    for (std::uint32_t i = 0; i < root_count; ++i) {
        std::int32_t root = -1;
        if (!footer.read_i32(root)) {
            result.error = "NIF footer root table is truncated.";
            return result;
        }
        if (!valid_ref(root, result.blocks.size())) {
            result.error = "NIF footer contains an out-of-range root reference.";
            return result;
        }
        result.roots.push_back(root);
    }

    if (footer.remaining() != 0U) {
        result.error = "NIF contains trailing bytes after the footer root table.";
        return result;
    }

    result.valid = true;
    return result;
}

BtrInspection inspect_fallout4_btr(const std::filesystem::path& path) {
    BtrInspection result;
    result.nif = inspect_nif(path);
    if (!result.nif.valid) {
        result.error = result.nif.error;
        return result;
    }
    if (!result.nif.is_fallout4()) {
        result.error = "BTR is not a Fallout 4 user-version 12/130 NIF stream.";
        return result;
    }
    if (result.nif.roots.size() != 1U || result.nif.roots.front() != 0) {
        result.error = "Fallout 4 terrain BTR must have exactly one root reference to block 0.";
        return result;
    }

    std::string file_error;
    const auto data = read_file(path, file_error);
    if (!file_error.empty()) {
        result.error = std::move(file_error);
        return result;
    }
    if (!parse_root_node(data, result) || !parse_bounds(data, result)) {
        if (result.error.empty()) result.error = "BTR structure is malformed.";
        return result;
    }

    result.valid = true;
    return result;
}

} // namespace f4lod
