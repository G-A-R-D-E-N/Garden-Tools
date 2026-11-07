#pragma once

#include "fo4/plugin/mutation.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace fo4::detail {

// Invariant: the caller has already checked that the payload holds at least four bytes.
inline std::uint32_t read_counter(const std::vector<std::byte>& data) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[i])) << (8 * i);
  }
  return value;
}

std::vector<SubrecordGroupSpan> group_spans(const Record& record,
                                            const schema::SubrecordGroupDescriptor& group);

}  // namespace fo4::detail
