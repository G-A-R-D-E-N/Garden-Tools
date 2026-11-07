#pragma once

#include "write_model.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace fo4::nif::write_detail {

struct ChildList {
  std::size_t count_at{0};
  std::size_t first{0};
  std::size_t count{0};
};

// Constraint: an offset list names every i32 block reference of a block type with a known layout; any other type is refused.
std::vector<std::size_t> ref_offsets(const Model& model, std::uint32_t block);
// Constraint: only node types with a child list have one, and each child entry is one of the offsets ref_offsets names.
std::optional<ChildList> child_list(const Model& model, std::uint32_t block);
std::uint32_t uint_at(const Bytes& bytes, std::size_t at);
std::int32_t int_at(const Bytes& bytes, std::size_t at);

}  // namespace fo4::nif::write_detail
