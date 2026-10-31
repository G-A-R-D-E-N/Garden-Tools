#pragma once

#include "fo4/environment/world.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace fo4::previs {

// Wire format invariant: u32 mesh count, u32 reference word count, mesh ids,
// then (REFR local id, OC id) word pairs; size must equal 8 + 4*meshes + 4*words.
inline constexpr std::uint16_t k_xcri_min_form_version = 0x72;

struct CombinedReference {
  std::uint32_t mesh{0};
  FormKey ref{};
};

struct CombinedIndex {
  std::vector<std::uint32_t> mesh_ids;
  std::vector<CombinedReference> refs;
};

struct XcriRaw {
  std::vector<std::uint32_t> mesh_ids;
  std::vector<std::pair<std::uint32_t, std::uint32_t>> refs;
};

// Malformed payload must throw fo4::parse_error.
XcriRaw parse_xcri_bytes(std::span<const std::byte> data);
// Serialization must round-trip: write(parse(x)) == x on every vanilla payload.
std::vector<std::byte> write_xcri_bytes(const XcriRaw& raw);

// A malformed present subrecord must throw fo4::env::WorldDecodeError.
std::optional<CombinedIndex> decode_combined_index(const env::Environment& env,
                                                   const env::RecordRef& cell_ref);

// Packed date invariant: year = 2000 + (v >> 9), month = (v >> 5) & 15, day = v & 31.
struct PackedDate {
  unsigned year{0};
  unsigned month{0};
  unsigned day{0};
};

PackedDate decode_packed_date(std::uint16_t value) noexcept;
// A newer child version date requires the CELL stamp to be restamped.
bool is_precalc_stale(std::uint16_t cell_stamp, std::uint16_t child_date) noexcept;

}  // namespace fo4::previs
