#pragma once

#include "fo4/plugin/container.hpp"
#include "fo4/schema/formlink_lists.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

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

void erase_list_element(Record& record,
                        const FormLinkListDescriptor& descriptor,
                        std::size_t position);

void move_list_element(Record& record,
                       const FormLinkListDescriptor& descriptor,
                       const std::vector<std::size_t>& positions,
                       std::size_t from,
                       std::size_t to);

}  // namespace fo4::schema::detail
