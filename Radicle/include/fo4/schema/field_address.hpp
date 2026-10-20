#pragma once

#include "fo4/plugin/container.hpp"
#include "fo4/signature.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace fo4::schema {

enum class DiscriminatorStorage {
  u8,
  u16,
  u32,
  i32,
};

inline constexpr std::size_t k_max_discriminator_values = 8;

struct FieldDiscriminator {
  Signature subrecord{};
  std::size_t offset{0};
  DiscriminatorStorage storage{DiscriminatorStorage::u8};
  std::size_t count{0};
  std::array<std::int64_t, k_max_discriminator_values> equals{};
  bool match_absent{false};
  bool negated{false};
};

// Constraint: count 0 means the row is not header-counted; the element count is counter[count_word].
struct SegmentAddress {
  std::size_t count_word{0};
  std::size_t index{0};
  std::size_t count{0};
};

struct FieldAddress {
  bool repeated{false};
  std::size_t stride{0};
  std::size_t array_offset{0};
  FieldDiscriminator when{};
  SegmentAddress segment{};
};

// Constraint: base is the first element offset and count its element count.
struct SegmentSpan {
  std::size_t base{0};
  std::size_t count{0};
};

struct FieldShape {
  Signature subrecord{};
  std::size_t offset{0};
  std::size_t width{0};
  std::size_t payload_size{0};
  bool whole_payload{false};
  std::size_t minimum_payload_size{0};
  std::uint16_t min_form_version{0};
  std::uint16_t max_form_version{0};
};

struct FieldSlot {
  std::size_t subrecord{0};
  std::size_t offset{0};
  bool valid{false};
  bool misfit{false};
};

struct FieldSlots {
  std::vector<FieldSlot> slots;
  bool duplicate{false};
  bool malformed{false};
  bool misaligned{false};
};

std::size_t discriminator_width(DiscriminatorStorage storage) noexcept;

// Constraint: requires the named subrecord exactly once; absent matches only when the discriminator declares it.
bool record_discriminator_matches(const Record& record, const FieldDiscriminator& when);

FieldSlots resolve_field_slots(const Record& record,
                               const FieldShape& shape,
                               const FieldAddress& address);

}
