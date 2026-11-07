#pragma once

#include <cstdint>

namespace fo4 {

class RecordFlags {
public:
  using value_type = std::uint32_t;

  static constexpr value_type master = 1u << 0;
  static constexpr value_type deleted = 1u << 5;
  static constexpr value_type localized = 1u << 7;
  static constexpr value_type small_file = 1u << 9;
  static constexpr value_type persistent = 1u << 10;
  static constexpr value_type initially_disabled = 1u << 11;
  static constexpr value_type partial_form = 1u << 14;
  static constexpr value_type compressed = 1u << 18;

  constexpr RecordFlags() noexcept = default;
  constexpr explicit RecordFlags(value_type bits) noexcept : bits_(bits) {}

  constexpr value_type value() const noexcept { return bits_; }
  constexpr bool test(value_type bit) const noexcept { return (bits_ & bit) != 0; }

  constexpr bool is_master() const noexcept { return test(master); }
  constexpr bool is_deleted() const noexcept { return test(deleted); }
  constexpr bool is_localized() const noexcept { return test(localized); }
  constexpr bool is_small_file() const noexcept { return test(small_file); }
  constexpr bool is_persistent() const noexcept { return test(persistent); }
  constexpr bool is_compressed() const noexcept { return test(compressed); }

  friend constexpr bool operator==(const RecordFlags&, const RecordFlags&) = default;

private:
  value_type bits_{0};
};

enum class GroupType : std::int32_t {
  top = 0,
  world_children = 1,
  interior_cell_block = 2,
  interior_cell_subblock = 3,
  exterior_cell_block = 4,
  exterior_cell_subblock = 5,
  cell_children = 6,
  topic_children = 7,
  cell_persistent_children = 8,
  cell_temporary_children = 9,
  cell_visible_distant_children = 10,
  quest_children = 10,
};

inline bool group_label_is_form_id(std::int32_t type) {
  return type == static_cast<std::int32_t>(GroupType::world_children) ||
         type == static_cast<std::int32_t>(GroupType::cell_children) ||
         type == static_cast<std::int32_t>(GroupType::topic_children) ||
         type == static_cast<std::int32_t>(GroupType::cell_persistent_children) ||
         type == static_cast<std::int32_t>(GroupType::cell_temporary_children) ||
         type == static_cast<std::int32_t>(GroupType::cell_visible_distant_children);
}

}  // namespace fo4
