#pragma once

#include "f4lod/record_graph.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace f4lod {

struct SubrecordView {
    std::string type;
    std::vector<unsigned char> data;
};

struct RecordPayload {
    bool valid{};
    std::string error;
    std::vector<unsigned char> data;
    std::vector<SubrecordView> subrecords;
};

[[nodiscard]] RecordPayload read_record_payload(const WinningRecord& record);
[[nodiscard]] const SubrecordView* find_subrecord(
    const RecordPayload& payload,
    const std::string& type) noexcept;
[[nodiscard]] std::string read_zstring(const SubrecordView& subrecord);
[[nodiscard]] bool read_i32_pair(
    const SubrecordView& subrecord,
    std::int32_t& first,
    std::int32_t& second) noexcept;
[[nodiscard]] bool read_f32(
    const SubrecordView& subrecord,
    float& value) noexcept;
[[nodiscard]] bool read_f32_pair(
    const SubrecordView& subrecord,
    float& first,
    float& second) noexcept;

} // namespace f4lod
