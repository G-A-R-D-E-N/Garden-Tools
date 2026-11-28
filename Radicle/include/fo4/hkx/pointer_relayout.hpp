#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::hkx {

// Invariant: bytes is empty unless ok; reason names the first refusal and is empty when ok; classes lists the converted class names, sorted and unique.
struct HkxRelayoutResult {
  bool ok{false};
  std::string reason;
  std::uint8_t source_pointer_size{0};
  std::uint8_t target_pointer_size{0};
  bool unchanged{false};
  std::size_t object_count{0};
  std::size_t block_count{0};
  std::size_t local_fixups{0};
  std::size_t global_fixups{0};
  std::size_t virtual_fixups{0};
  std::size_t source_data_size{0};
  std::size_t target_data_size{0};
  std::vector<std::string> classes;
  // Invariant: one (source offset, target offset) pair per virtual fixup in table order; empty when the width is unchanged or the file is refused.
  std::vector<std::pair<std::uint32_t, std::uint32_t>> object_moves;
  std::vector<std::byte> bytes;
};

// Constraint: a header-only read; nullopt unless the file has the packfile magic, version 11 and canonical rules for a 4 or 8 byte pointer.
std::optional<std::uint8_t> packfile_pointer_size(std::span<const std::byte> bytes);

// Constraint: only the classes of decode_classes() convert; any other class in a virtual fixup is refused, never guessed. A same-width request returns the bytes unchanged after the signature check.
HkxRelayoutResult relayout_packfile(std::span<const std::byte> bytes, std::uint8_t target_pointer_size);

// Invariant: the instance size of a verified class at that pointer width (4 or 8); nullopt for any other class or width.
std::optional<std::uint32_t> relayout_class_size(std::string_view class_name, std::uint8_t pointer_size);

// Constraint: compares scalar members of each narrowed object at the decode-table 8-byte offsets in the source against the generated 4-byte offsets in the result without calling relayout again; empty when they match.
std::string verify_narrow_layout_crosscheck(std::span<const std::byte> wide, std::span<const std::byte> narrow,
                                            std::span<const std::pair<std::uint32_t, std::uint32_t>> object_moves);

}  // namespace fo4::hkx
