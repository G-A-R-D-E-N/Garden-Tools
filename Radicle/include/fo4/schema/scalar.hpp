#pragma once

#include "fo4/schema/field_address.hpp"
#include "fo4/signature.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::schema {

enum class ScalarStorage {
  u8,
  u16,
  u32,
  i32,
  f32,
  i8,
  i16,
  zstring,
  // Constraint: fixed_string is whole-payload only and its width is the descriptor's payload_size.
  fixed_string,
  // Constraint: bytes is whole-payload only, has no storage width, and carries its value as lowercase hex; an empty value is a zero-byte payload.
  bytes,
};

enum class ScalarPresentation {
  identity,
  percent_0_100,
  enumeration,
  flags,
};

// Invariant: value is the stored integer for an enumeration row and the single-bit mask (1 << bit) for a flags row.
struct ScalarName {
  std::int64_t value{0};
  std::string_view name;
};

struct ScalarDescriptor {
  Signature subrecord{};
  std::string_view field_name;
  ScalarStorage storage{ScalarStorage::u32};
  ScalarPresentation presentation{ScalarPresentation::identity};
  double minimum{0.0};
  double maximum{0.0};
  // Byte offset of the field within the subrecord payload. Meaningful only
  // when `embedded` is set.
  std::size_t offset{0};
  // False: the field *is* the whole payload, which must be exactly the storage
  // width -- the original shape, and still what REFR XHLT/XSCL are.
  // True: the field lives at `offset` inside a larger struct payload, as every
  // PROJ DNAM / EXPL DATA / HAZD DNAM field does. Such a payload carries many
  // fields, and a short payload simply does not expose the ones past its end
  // (EXPL DATA ships truncated at 52/56/60/64 bytes as well as 84).
  bool embedded{false};
  std::size_t payload_size{0};
  FieldAddress address{};
  std::size_t minimum_payload_size{0};
  std::span<const ScalarName> names{};
  std::uint16_t min_form_version{0};
  bool localized{false};
  std::uint16_t max_form_version{0};
};

struct ScalarFieldDescriptor {
  Signature record{};
  std::string_view path;
  ScalarDescriptor scalar;
  bool can_insert{false};
};

class ScalarError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Constraint: xEdit wbModelInfoDecider (Core/wbDefinitionsCommon.pas:5646) allows at
// most eight counter words, and a larger first word must be a different format.
inline constexpr std::size_t k_max_header_counters = 8;

struct CountedArrayDescriptor {
  Signature record{};
  std::string_view path;
  std::string_view label;
  Signature subrecord{};
  Signature count_subrecord{};
  std::size_t stride{0};
  // Constraint: segment_count 0 means a plain payload; otherwise the array is one of
  // segment_count header-counted segments that must share one payload.
  std::size_t count_word{0};
  std::size_t segment_index{0};
  std::size_t segment_count{0};
  // Constraint: 0 keeps the counter-count convention, where the first payload word counts the
  // counter words that follow it; otherwise the payload opens with exactly this many u32 words
  // and count_word indexes them directly. CELL XCRI's first word is a mesh count, not a header
  // length, so every segment of that payload declares the width.
  std::size_t header_words{0};
  // Invariant: an element count is the counter word divided by this, so a payload that stores
  // two 4-byte words per 8-byte element keeps its exact size accounting. A word that does not
  // divide evenly fails the read rather than rounding.
  std::size_t count_divisor{1};
  // Constraint: has_subrecord_occurrence false addresses every copy of the subrecord, which is
  // what the single-payload records want; setting it selects one copy, such as the male (0) or
  // female (1) RACE MODT.
  std::size_t subrecord_occurrence{0};
  bool has_subrecord_occurrence{false};
};

class CountedArrayError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

struct SubrecordGroupMemberDescriptor {
  Signature subrecord{};
  bool required{false};
  bool repeatable{false};
  // Constraint: payload_size 0 means a variable-length member and skips the size check.
  std::size_t payload_size{0};
  std::span<const std::uint8_t> default_payload{};
  bool localized{false};
  // Constraint: a skippable member may appear before the first leader, where it belongs to no run.
  bool skippable{false};
  // Invariant: a leading member is optional, precedes the leader in declaration and data order, and belongs to the run of the leader that follows it.
  bool leading{false};
  // Constraint: an older_default replaces the default when the record form version is below older_before.
  std::span<const std::uint8_t> older_default{};
  std::uint16_t older_before{0};
};

// Invariant: count_member names one of the group's own members whose whole four-byte payload counts the nested runs that follow it inside the enclosing run; an empty count_member means each run of that shape stands on its own, uncounted.
struct SubrecordGroupRunDescriptor {
  Signature count_member{};
  std::span<const SubrecordGroupMemberDescriptor> members{};
};

// Invariant: a block group's counter opens a block that closer ends, the block exists only while it holds an element, and the u16 at link_offset of an element names another element's index or link_none.
struct SubrecordGroupBlockDescriptor {
  Signature closer{};
  std::size_t leader_min_size{0};
  std::size_t link_offset{0};
  std::uint16_t link_none{0xFFFF};
};

struct SubrecordGroupDescriptor {
  Signature record{};
  std::string_view path;
  std::string_view label;
  Signature count_subrecord{};
  std::span<const SubrecordGroupMemberDescriptor> members{};
  std::span<const Signature> successors{};
  SubrecordGroupRunDescriptor run{};
  bool requires_existing_count{false};
  // Invariant: a greedy element runs from its leader to the next leader, a stop, or through the closer.
  std::span<const Signature> stops{};
  Signature closer{};
  // Constraint: after places a new element behind the last element, else behind the first subrecord with this signature.
  Signature after{};
  bool greedy{false};
  // Invariant: a parent names the greedy group whose element encloses every element of this group, so Parent[i]\Child addresses the child elements inside parent element i.
  std::string_view parent{};
  SubrecordGroupBlockDescriptor block{};
};

class SubrecordGroupError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Constraint: thrown before any change, so a refused block edit leaves the record untouched.
class SubrecordGroupBlockError : public SubrecordGroupError {
public:
  enum class Kind { malformed, no_insertion_point, referenced };
  SubrecordGroupBlockError(Kind kind, const std::string& what) : SubrecordGroupError(what), kind_(kind) {}
  Kind kind() const noexcept { return kind_; }

private:
  Kind kind_;
};

// Resolves a signature only if exactly one schema row names it and that row is whole-payload, non-repeated, non-string; else use record plus path.
std::optional<ScalarDescriptor> scalar_descriptor(Signature subrecord) noexcept;
std::optional<ScalarFieldDescriptor> scalar_field_descriptor(
    std::string_view path) noexcept;
std::optional<ScalarFieldDescriptor> scalar_field_descriptor(
    Signature record, std::string_view path) noexcept;
std::span<const ScalarFieldDescriptor> scalar_fields(Signature record) noexcept;
std::vector<Signature> record_signatures_with_schema();
std::vector<CountedArrayDescriptor> counted_arrays(Signature record);
std::vector<SubrecordGroupDescriptor> subrecord_groups(Signature record);
std::size_t scalar_storage_size(ScalarStorage storage) noexcept;
std::optional<CountedArrayDescriptor> counted_array_descriptor(Signature record,
                                                               std::string_view path) noexcept;
bool is_counted_array_count_subrecord(Signature record, Signature subrecord) noexcept;

// Constraint: nullopt when the payload is not the modelled arm, never a partial read.
std::optional<SegmentSpan> header_segment(std::span<const std::byte> payload,
                                          Signature record,
                                          Signature subrecord,
                                          const FieldAddress& address);
std::optional<SubrecordGroupDescriptor> subrecord_group_descriptor(Signature record,
                                                                   std::string_view path) noexcept;
std::optional<SubrecordGroupDescriptor> subrecord_group_for_member(Signature record,
                                                                   Signature subrecord) noexcept;
bool is_subrecord_group_count_subrecord(Signature record, Signature subrecord) noexcept;
bool is_derived_scalar_row(Signature record, Signature subrecord) noexcept;

// Constraint: the OBTS and OMOD DATA include and property counts size the payload, so they are read-only rows.
bool is_object_template_count_row(const ScalarFieldDescriptor& field) noexcept;

// The bytes of one field within a subrecord payload. Empty when an embedded
// field lies past the end of a legitimately shorter payload -- absent, not
// malformed. Throws for a whole-payload field of the wrong width.
std::span<const std::byte> scalar_field_bytes(std::span<const std::byte> payload,
                                              const ScalarDescriptor& descriptor);

FieldShape scalar_field_shape(const ScalarDescriptor& descriptor) noexcept;

std::span<const std::byte> scalar_slot_bytes(std::span<const std::byte> payload,
                                             const FieldSlot& slot,
                                             const ScalarDescriptor& descriptor);

// Writes an encoded field back into a payload at the descriptor's offset,
// leaving every other byte untouched. A whole-payload field replaces it.
void patch_scalar_field(std::span<std::byte> payload,
                        const ScalarDescriptor& descriptor,
                        std::span<const std::byte> encoded);

std::optional<std::string_view> scalar_value_name(const ScalarDescriptor& descriptor,
                                                  std::int64_t value) noexcept;
std::optional<std::int64_t> scalar_name_value(const ScalarDescriptor& descriptor,
                                              std::string_view name) noexcept;

std::string decode_scalar(std::span<const std::byte> data,
                          const ScalarDescriptor& descriptor);
std::vector<std::byte> encode_scalar(std::string_view text,
                                     const ScalarDescriptor& descriptor);

}  // namespace fo4::schema
