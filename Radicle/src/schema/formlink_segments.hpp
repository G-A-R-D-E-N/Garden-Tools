#pragma once

#include "fo4/plugin/container.hpp"
#include "fo4/schema/formlink_lists.hpp"

#include <cstddef>
#include <cstdint>

namespace fo4::schema::detail {

struct PackedSegment {
  std::size_t base{0};
  std::size_t count{0};
};

PackedSegment packed_segment(const Record& record,
                             const FormLinkListDescriptor& descriptor);

void reconcile_counter(Record& record,
                       const FormLinkListDescriptor& descriptor,
                       std::uint32_t count);

}  // namespace fo4::schema::detail
