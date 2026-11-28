#pragma once

#include "fo4/binary/checked.hpp"
#include "fo4/plugin/mutation.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace fo4::detail {

// Invariant: the caller has already checked that the payload holds at least four bytes.
inline std::uint32_t read_counter(const std::vector<std::byte>& data) {
  return fo4::binary::checked::read_u32(data);
}

// Constraint: within limits a greedy walk to one enclosing parent element; only a group with a parent is walked that way.
std::vector<SubrecordGroupSpan> group_spans(const Record& record,
                                            const schema::SubrecordGroupDescriptor& group,
                                            const SubrecordGroupSpan* within = nullptr);

// Constraint: defined in src/forms/quest_aliases_decode.cpp; it runs the strict QUST alias decode and throws forms::QuestError like it.
std::optional<std::uint32_t> highest_quest_alias_id(const Plugin& plugin, const FormKey& record_key);

}  // namespace fo4::detail
