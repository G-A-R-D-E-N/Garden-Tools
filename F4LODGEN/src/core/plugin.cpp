#include "f4lod/plugin.hpp"

#include <array>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <vector>

namespace f4lod {
namespace {

constexpr std::size_t kRecordHeaderSize = 24;
constexpr std::uint32_t kCompressedRecordFlag = 0x00040000U;
constexpr std::uint32_t kGroupWorldChildren = 1U;
constexpr std::uint32_t kGroupCellChildren = 6U;
constexpr std::uint32_t kGroupCellPersistentChildren = 8U;
constexpr std::uint32_t kGroupCellTemporaryChildren = 9U;
constexpr std::uint32_t kGroupCellVisibleDistantChildren = 10U;

struct GroupContext {
    std::uint64_t end_offset{};
    std::uint32_t label{};
    std::uint32_t type{};
};

std::uint16_t read_u16(const unsigned char* data) {
    return static_cast<std::uint16_t>(data[0]) |
           (static_cast<std::uint16_t>(data[1]) << 8U);
}

std::uint32_t read_u32(const unsigned char* data) {
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8U) |
           (static_cast<std::uint32_t>(data[2]) << 16U) |
           (static_cast<std::uint32_t>(data[3]) << 24U);
}

std::string signature(const unsigned char* data) {
    return std::string(reinterpret_cast<const char*>(data), 4);
}

std::string json_escape(std::string_view input) {
    std::ostringstream out;
    for (const unsigned char c : input) {
        switch (c) {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\b': out << "\\b"; break;
        case '\f': out << "\\f"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (c < 0x20) {
                out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c)
                    << std::dec << std::setfill(' ');
            } else {
                out << static_cast<char>(c);
            }
            break;
        }
    }
    return out.str();
}

bool read_exact(std::ifstream& input, unsigned char* data, std::size_t size) {
    input.read(reinterpret_cast<char*>(data), static_cast<std::streamsize>(size));
    return input.good() || input.gcount() == static_cast<std::streamsize>(size);
}

void count_record(PluginRecordCounts& counts, std::string_view type) {
    ++counts.total_records;
    if (type == "WRLD") {
        ++counts.worldspaces;
    } else if (type == "CELL") {
        ++counts.cells;
    } else if (type == "LAND") {
        ++counts.landscapes;
    } else if (type == "LTEX") {
        ++counts.landscape_textures;
    } else if (type == "TXST") {
        ++counts.texture_sets;
    } else if (type == "REFR") {
        ++counts.references;
    } else if (type == "STAT") {
        ++counts.statics;
    }
}

bool is_lod_record(std::string_view type) {
    return type == "WRLD" || type == "CELL" || type == "LAND" ||
           type == "LTEX" || type == "TXST" ||
           type == "REFR" || type == "STAT";
}

bool is_cell_context_group(std::uint32_t type) {
    return type == kGroupCellChildren ||
           type == kGroupCellPersistentChildren ||
           type == kGroupCellTemporaryChildren ||
           type == kGroupCellVisibleDistantChildren;
}

void find_group_context(
    const std::vector<GroupContext>& groups,
    std::uint32_t& world_form_id,
    std::uint32_t& cell_form_id) {
    world_form_id = 0;
    cell_form_id = 0;

    for (auto it = groups.rbegin(); it != groups.rend(); ++it) {
        if (cell_form_id == 0 && is_cell_context_group(it->type)) cell_form_id = it->label;
        if (world_form_id == 0 && it->type == kGroupWorldChildren) world_form_id = it->label;
        if (world_form_id != 0 && cell_form_id != 0) break;
    }
}

bool parse_tes4_fields(
    const std::vector<unsigned char>& data,
    std::vector<std::string>& masters,
    std::string& error) {
    std::size_t cursor = 0;
    std::uint32_t extended_size = 0;

    while (cursor < data.size()) {
        if (data.size() - cursor < 6) {
            error = "TES4 subrecord header is truncated.";
            return false;
        }

        const auto type = signature(data.data() + cursor);
        const auto short_size = read_u16(data.data() + cursor + 4);
        cursor += 6;

        std::uint32_t size = short_size;
        if (type == "XXXX") {
            if (short_size != 4 || data.size() - cursor < 4) {
                error = "TES4 XXXX extended-size subrecord is malformed.";
                return false;
            }
            extended_size = read_u32(data.data() + cursor);
            cursor += 4;
            continue;
        }

        if (extended_size != 0) {
            size = extended_size;
            extended_size = 0;
        }
        if (size > data.size() - cursor) {
            error = "TES4 subrecord payload extends past the record boundary.";
            return false;
        }

        if (type == "MAST") {
            const char* begin = reinterpret_cast<const char*>(data.data() + cursor);
            std::size_t length = 0;
            while (length < size && begin[length] != '\0') ++length;
            masters.emplace_back(begin, length);
        }
        cursor += size;
    }

    if (extended_size != 0) {
        error = "TES4 XXXX extended-size marker is not followed by a subrecord.";
        return false;
    }
    return true;
}

PluginRecordIndex parse_plugin(const std::filesystem::path& path, bool collect_lod_records) {
    PluginRecordIndex result;
    result.inspection.path = path;

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        result.inspection.error = "Could not open plugin file.";
        return result;
    }

    input.seekg(0, std::ios::end);
    const auto end = input.tellg();
    if (end < 0) {
        result.inspection.error = "Could not determine plugin file size.";
        return result;
    }
    const auto file_size = static_cast<std::uint64_t>(end);
    input.seekg(0, std::ios::beg);

    if (file_size < kRecordHeaderSize) {
        result.inspection.error = "Plugin is smaller than a Fallout 4 record header.";
        return result;
    }

    std::uint64_t position = 0;
    bool first_record = true;
    std::vector<GroupContext> groups;

    while (position < file_size) {
        while (!groups.empty() && position >= groups.back().end_offset) {
            if (position > groups.back().end_offset) {
                result.inspection.error = "Record stream advanced past a GRUP boundary.";
                return result;
            }
            groups.pop_back();
        }

        if (file_size - position < kRecordHeaderSize) {
            result.inspection.error = "Trailing bytes do not contain a complete record header.";
            return result;
        }

        std::array<unsigned char, kRecordHeaderSize> header{};
        if (!read_exact(input, header.data(), header.size())) {
            result.inspection.error = "Failed while reading a record header.";
            return result;
        }

        const auto type = signature(header.data());
        const auto data_size = read_u32(header.data() + 4);

        if (first_record && type != "TES4") {
            result.inspection.error = "First plugin record is not TES4.";
            return result;
        }
        if (!first_record && type == "TES4") {
            result.inspection.error = "Plugin contains a second TES4 header record.";
            return result;
        }

        if (type == "GRUP") {
            const auto group_size = static_cast<std::uint64_t>(data_size);
            if (group_size < kRecordHeaderSize || group_size > file_size - position) {
                result.inspection.error = "GRUP header has an invalid size.";
                return result;
            }
            const auto group_end = position + group_size;
            if (!groups.empty() && group_end > groups.back().end_offset) {
                result.inspection.error = "Nested GRUP extends beyond its parent boundary.";
                return result;
            }

            groups.push_back(GroupContext{
                group_end,
                read_u32(header.data() + 8),
                read_u32(header.data() + 12),
            });
            ++result.inspection.records.groups;
            position += kRecordHeaderSize;
            continue;
        }

        const auto record_end = position + kRecordHeaderSize + static_cast<std::uint64_t>(data_size);
        if (record_end > file_size) {
            result.inspection.error = "Record payload extends past end of file.";
            return result;
        }
        if (!groups.empty() && record_end > groups.back().end_offset) {
            result.inspection.error = "Record payload extends past its GRUP boundary.";
            return result;
        }

        const auto flags = read_u32(header.data() + 8);
        const auto raw_form_id = read_u32(header.data() + 12);
        count_record(result.inspection.records, type);

        if (collect_lod_records && is_lod_record(type)) {
            std::uint32_t parent_world = 0;
            std::uint32_t parent_cell = 0;
            find_group_context(groups, parent_world, parent_cell);
            result.lod_records.push_back(PluginRecordHeader{
                type,
                flags,
                raw_form_id,
                data_size,
                position,
                parent_world,
                parent_cell,
            });
        }

        if (type == "TES4") {
            if ((flags & kCompressedRecordFlag) != 0) {
                result.inspection.error = "Compressed TES4 header records are not supported.";
                return result;
            }

            std::vector<unsigned char> data(data_size);
            if (data_size != 0 && !read_exact(input, data.data(), data.size())) {
                result.inspection.error = "Failed while reading TES4 header fields.";
                return result;
            }
            if (!parse_tes4_fields(data, result.inspection.masters, result.inspection.error)) return result;
        } else {
            input.seekg(static_cast<std::streamoff>(data_size), std::ios::cur);
            if (!input) {
                result.inspection.error = "Failed while skipping a record payload.";
                return result;
            }
        }

        position = record_end;
        first_record = false;
    }

    result.inspection.valid = position == file_size && result.inspection.records.total_records != 0;
    if (!result.inspection.valid && result.inspection.error.empty()) {
        result.inspection.error = "Plugin did not contain a complete record stream.";
    }
    return result;
}

} // namespace

PluginInspection inspect_plugin(const std::filesystem::path& path) {
    return parse_plugin(path, false).inspection;
}

PluginRecordIndex index_lod_records(const std::filesystem::path& path) {
    return parse_plugin(path, true);
}

std::string format_plugin_inspection_text(const PluginInspection& inspection) {
    std::ostringstream out;
    out << "F4LODGen plugin inspection\n"
        << "Path: " << inspection.path.string() << '\n'
        << "Result: " << (inspection.valid ? "PASS" : "FAIL") << '\n';

    if (!inspection.error.empty()) out << "Error: " << inspection.error << '\n';

    out << "\nMasters: " << inspection.masters.size() << '\n';
    for (const auto& master : inspection.masters) out << "  " << master << '\n';

    out << "\nRecords\n"
        << "  total: " << inspection.records.total_records << '\n'
        << "  groups: " << inspection.records.groups << '\n'
        << "  WRLD: " << inspection.records.worldspaces << '\n'
        << "  CELL: " << inspection.records.cells << '\n'
        << "  LAND: " << inspection.records.landscapes << '\n'
        << "  LTEX: " << inspection.records.landscape_textures << '\n'
        << "  TXST: " << inspection.records.texture_sets << '\n'
        << "  REFR: " << inspection.records.references << '\n'
        << "  STAT: " << inspection.records.statics << '\n';
    return out.str();
}

std::string format_plugin_inspection_json(const PluginInspection& inspection) {
    std::ostringstream out;
    out << "{\n"
        << "  \"valid\": " << (inspection.valid ? "true" : "false") << ",\n"
        << "  \"path\": \"" << json_escape(inspection.path.generic_string()) << "\",\n"
        << "  \"error\": \"" << json_escape(inspection.error) << "\",\n"
        << "  \"masters\": [";

    for (std::size_t i = 0; i < inspection.masters.size(); ++i) {
        if (i != 0) out << ", ";
        out << '"' << json_escape(inspection.masters[i]) << '"';
    }

    out << "],\n"
        << "  \"records\": {\n"
        << "    \"total\": " << inspection.records.total_records << ",\n"
        << "    \"groups\": " << inspection.records.groups << ",\n"
        << "    \"wrld\": " << inspection.records.worldspaces << ",\n"
        << "    \"cell\": " << inspection.records.cells << ",\n"
        << "    \"land\": " << inspection.records.landscapes << ",\n"
        << "    \"ltex\": " << inspection.records.landscape_textures << ",\n"
        << "    \"txst\": " << inspection.records.texture_sets << ",\n"
        << "    \"refr\": " << inspection.records.references << ",\n"
        << "    \"stat\": " << inspection.records.statics << "\n"
        << "  }\n"
        << "}\n";
    return out.str();
}

} // namespace f4lod
