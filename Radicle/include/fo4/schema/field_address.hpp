#pragma once

#include "fo4/plugin_core/container.hpp"
#include "fo4/signature.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
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
  // Constraint: nonzero bits matches when any bit is set; equals, extra and negated stay unused.
  std::uint64_t bits{0};
  // Constraint: values past the inline array point at a generated table.
  const std::int64_t* extra{nullptr};
  std::size_t extra_count{0};
};

// Constraint: a mask or an appended table guards the row even when the inline list is empty.
inline constexpr bool discriminator_guarded(const FieldDiscriminator& when) {
  return when.count != 0 || when.bits != 0 || when.extra_count != 0;
}

// Constraint: count 0 means the row is not header-counted; the element count is counter[count_word].
struct SegmentAddress {
  std::size_t count_word{0};
  std::size_t index{0};
  std::size_t count{0};
};

inline constexpr std::size_t k_max_run_sections = 6;

// Constraint: count_offset k_run_inline_count means the section opens with its own four-byte count word at the running cursor and its elements follow it; any other count_offset is an absolute payload offset.
inline constexpr std::uint16_t k_run_inline_count = 0xFFFF;

// Constraint: count_width 0 is one fixed element; otherwise the element count is the little-endian count_width-byte word at count_offset in the payload.
struct RunSection {
  std::uint16_t count_offset{0};
  std::uint8_t count_width{0};
  std::uint16_t stride{0};
};

// Constraint: count 0 means the row is not run-addressed; otherwise the sections are laid end to end from start and index is this row's section.
struct RunAddress {
  std::uint16_t start{0};
  std::uint8_t count{0};
  std::uint8_t index{0};
  std::array<RunSection, k_max_run_sections> sections{};
};

struct FieldAddress {
  bool repeated{false};
  std::size_t stride{0};
  std::size_t array_offset{0};
  FieldDiscriminator when{};
  SegmentAddress segment{};
  RunAddress run{};
  // Constraint: has_subrecord_occurrence false keeps the default, where every copy of the
  // subrecord contributes its slots in record order; setting it picks one copy, as the RACE MODT
  // gender rows do for the male (0) and female (1) model information.
  std::size_t subrecord_occurrence{0};
  bool has_subrecord_occurrence{false};
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
  // Constraint: a payload that fails its row size contract is reported, never silently skipped.
  bool wrong_size{false};
};

std::size_t discriminator_width(DiscriminatorStorage storage) noexcept;

// Constraint: nullopt unless the sections end exactly at the payload end, so a miscounted payload resolves no slot.
std::optional<SegmentSpan> run_span(std::span<const std::byte> payload, const RunAddress& run);

// Constraint: requires the named subrecord exactly once; absent matches only when the discriminator declares it.
bool record_discriminator_matches(const Record& record, const FieldDiscriminator& when);

FieldSlots resolve_field_slots(const Record& record,
                               const FieldShape& shape,
                               const FieldAddress& address);

}
