#pragma once

#include "fo4/plugin_core/container.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace fo4::schema {

// Constraint: a record flag row names one bit of the record header, so the mask is what a
// caller tests; the header keeps the storage and the writer keeps its own bit rules.
struct RecordFlagDescriptor {
  Signature signature{};
  std::string_view path{};
  std::string_view label{};
  std::uint32_t mask{0};
};

std::span<const RecordFlagDescriptor> record_flags(Signature signature);

const RecordFlagDescriptor* record_flag_descriptor(std::string_view path);

bool record_flag_set(const Record& record, const RecordFlagDescriptor& descriptor);

// Absent when the path names no row or the row belongs to another signature.
std::optional<bool> record_flag_value(const Record& record, std::string_view path);

}  // namespace fo4::schema
