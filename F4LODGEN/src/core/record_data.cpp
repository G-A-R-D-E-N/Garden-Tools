#include "f4lod/record_data.hpp"

#include <miniz.h>

#include <cstring>
#include <fstream>
#include <utility>
#include <vector>

namespace f4lod {
namespace {

constexpr std::uint32_t kCompressedRecordFlag = 0x00040000U;
constexpr std::uint32_t kMaxDecodedRecordSize = 512U * 1024U * 1024U;

std::uint16_t read_u16(const unsigned char* data) noexcept {
    return static_cast<std::uint16_t>(data[0]) |
           (static_cast<std::uint16_t>(data[1]) << 8U);
}

std::uint32_t read_u32(const unsigned char* data) noexcept {
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8U) |
           (static_cast<std::uint32_t>(data[2]) << 16U) |
           (static_cast<std::uint32_t>(data[3]) << 24U);
}

std::string signature(const unsigned char* data) {
    return std::string(reinterpret_cast<const char*>(data), 4);
}

bool parse_subrecords(RecordPayload& payload) {
    std::size_t cursor = 0;
    std::uint32_t extended_size = 0;

    while (cursor < payload.data.size()) {
        if (payload.data.size() - cursor < 6) {
            payload.error = "Subrecord header is truncated.";
            return false;
        }

        const auto type = signature(payload.data.data() + cursor);
        const auto short_size = read_u16(payload.data.data() + cursor + 4);
        cursor += 6;

        if (type == "XXXX") {
            if (short_size != 4 || payload.data.size() - cursor < 4) {
                payload.error = "XXXX extended-size subrecord is malformed.";
                return false;
            }
            extended_size = read_u32(payload.data.data() + cursor);
            cursor += 4;
            continue;
        }

        const std::uint32_t size = extended_size != 0 ? extended_size : short_size;
        extended_size = 0;
        if (size > payload.data.size() - cursor) {
            payload.error = "Subrecord payload extends past record boundary.";
            return false;
        }

        SubrecordView subrecord;
        subrecord.type = type;
        subrecord.data.assign(
            payload.data.begin() + static_cast<std::ptrdiff_t>(cursor),
            payload.data.begin() + static_cast<std::ptrdiff_t>(cursor + size));
        payload.subrecords.push_back(std::move(subrecord));
        cursor += size;
    }

    if (extended_size != 0) {
        payload.error = "XXXX extended-size marker is not followed by a subrecord.";
        return false;
    }
    return true;
}

bool read_raw_record_data(const WinningRecord& record, std::vector<unsigned char>& raw, std::string& error) {
    std::ifstream input(record.source_path, std::ios::binary);
    if (!input) {
        error = "Could not open source plugin.";
        return false;
    }

    input.seekg(0, std::ios::end);
    const auto end = input.tellg();
    if (end < 0) {
        error = "Could not determine source plugin size.";
        return false;
    }
    const auto file_size = static_cast<std::uint64_t>(end);
    constexpr std::uint64_t record_header_size = 24;
    if (record.file_offset > file_size ||
        record_header_size > file_size - record.file_offset ||
        static_cast<std::uint64_t>(record.data_size) > file_size - record.file_offset - record_header_size) {
        error = "Record payload lies outside source plugin.";
        return false;
    }

    input.seekg(static_cast<std::streamoff>(record.file_offset + record_header_size), std::ios::beg);
    if (!input) {
        error = "Could not seek to record payload.";
        return false;
    }

    raw.resize(record.data_size);
    if (raw.empty()) {
        return true;
    }

    input.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(raw.size()));
    if (input.gcount() != static_cast<std::streamsize>(raw.size())) {
        error = "Record payload is truncated.";
        return false;
    }
    return true;
}

bool decode_record_data(
    const WinningRecord& record,
    const std::vector<unsigned char>& raw,
    RecordPayload& result) {
    if ((record.flags & kCompressedRecordFlag) == 0) {
        result.data = raw;
        return true;
    }

    if (raw.size() < 10) {
        result.error = "Compressed record payload is too small.";
        return false;
    }

    const auto decoded_size = read_u32(raw.data());
    if (decoded_size == 0 || decoded_size > kMaxDecodedRecordSize) {
        result.error = "Compressed record declares an invalid decoded size.";
        return false;
    }

    result.data.resize(decoded_size);
    mz_ulong destination_size = static_cast<mz_ulong>(result.data.size());
    const auto source_size = static_cast<mz_ulong>(raw.size() - 4);
    const int status = mz_uncompress(
        result.data.data(),
        &destination_size,
        raw.data() + 4,
        source_size);

    if (status != MZ_OK) {
        result.error = "zlib decompression failed with status " + std::to_string(status) + '.';
        result.data.clear();
        return false;
    }
    if (destination_size != decoded_size) {
        result.error = "Compressed record decoded to an unexpected size.";
        result.data.clear();
        return false;
    }
    return true;
}

} // namespace

RecordPayload read_record_payload(const WinningRecord& record) {
    RecordPayload result;
    std::vector<unsigned char> raw;
    if (!read_raw_record_data(record, raw, result.error)) {
        return result;
    }
    if (!decode_record_data(record, raw, result)) {
        return result;
    }
    if (!parse_subrecords(result)) {
        return result;
    }

    result.valid = true;
    return result;
}

const SubrecordView* find_subrecord(
    const RecordPayload& payload,
    const std::string& type) noexcept {
    for (const auto& subrecord : payload.subrecords) {
        if (subrecord.type == type) {
            return &subrecord;
        }
    }
    return nullptr;
}

std::string read_zstring(const SubrecordView& subrecord) {
    if (subrecord.data.empty()) {
        return {};
    }

    std::size_t length = 0;
    while (length < subrecord.data.size() && subrecord.data[length] != 0) {
        ++length;
    }
    return std::string(reinterpret_cast<const char*>(subrecord.data.data()), length);
}

bool read_i32_pair(
    const SubrecordView& subrecord,
    std::int32_t& first,
    std::int32_t& second) noexcept {
    if (subrecord.data.size() < 8) {
        return false;
    }

    const auto first_u = read_u32(subrecord.data.data());
    const auto second_u = read_u32(subrecord.data.data() + 4);
    std::memcpy(&first, &first_u, sizeof(first));
    std::memcpy(&second, &second_u, sizeof(second));
    return true;
}

bool read_f32(
    const SubrecordView& subrecord,
    float& value) noexcept {
    if (subrecord.data.size() < 4) {
        return false;
    }
    const auto bits = read_u32(subrecord.data.data());
    std::memcpy(&value, &bits, sizeof(value));
    return true;
}

bool read_f32_pair(
    const SubrecordView& subrecord,
    float& first,
    float& second) noexcept {
    if (subrecord.data.size() < 8) {
        return false;
    }
    const auto first_bits = read_u32(subrecord.data.data());
    const auto second_bits = read_u32(subrecord.data.data() + 4);
    std::memcpy(&first, &first_bits, sizeof(first));
    std::memcpy(&second, &second_bits, sizeof(second));
    return true;
}

} // namespace f4lod
