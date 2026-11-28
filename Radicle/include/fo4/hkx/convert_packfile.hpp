#pragma once

#include "fo4/hkx/convert.hpp"
#include "fo4/hkx/decode.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::hkx {

struct HkxHeaderVersion {
  std::string version;
  std::size_t offset{0};
};

// Constraint: scans only the first 96 bytes; a token cut by the window edge reads as no version.
std::optional<HkxHeaderVersion> probe_havok_version(std::span<const std::byte> bytes);

enum class HkxPackfileConvertStatus : std::uint8_t {
  Ok,
  Unreadable,
  UnknownVersion,
  UnsupportedPair,
  DecodeRefused,
  ConversionRefused,
  EmitRefused
};

std::string_view packfile_convert_status_name(HkxPackfileConvertStatus status);

struct HkxFieldChange {
  std::uint64_t object{0};
  std::string class_name;
  std::string member;
  std::string before;
  std::string after;
};

// Invariant: bytes is empty unless the status is Ok; changes holds at most kMaxChanges rows and changed_total counts all of them.
struct HkxPackfileConversion {
  static constexpr std::size_t kMaxChanges = 200;

  HkxPackfileConvertStatus status{HkxPackfileConvertStatus::Ok};
  std::string reason;
  std::string source_version;
  std::string target_version;
  std::string evidence;
  bool same_version{false};
  std::size_t object_count{0};
  HkxConversionReport report;
  std::vector<HkxConversionDiagnostic> diagnostics;
  std::size_t changed_total{0};
  std::vector<HkxFieldChange> changes;
  std::vector<std::string> opaque_classes;
  HkxBlock block;
  std::uint8_t source_pointer_size{0};
  std::uint8_t target_pointer_size{0};
  bool relaid_out{false};
  bool derived_table_consistency_checked{false};
  bool narrow_layout_measured{false};
  bool round_trip_checked{false};
  bool fields_compared{false};
  std::size_t relayout_blocks{0};
  std::vector<std::string> relayout_classes;
  std::vector<std::byte> bytes;
};

// Invariant: every map names its evidence; the only built-in map is the hk_2014.1.0-r1 same-version identity map.
const std::vector<HkxConversionMap>& verified_conversion_maps();

// Constraint: a map whose target version differs from the packfile's is refused after conversion because emit_document cannot restamp or re-lay.
HkxPackfileConversion convert_packfile(std::span<const std::byte> bytes, std::string_view target_version,
                                       std::span<const HkxConversionMap> maps);

HkxPackfileConversion convert_packfile(std::span<const std::byte> bytes, std::string_view target_version);

// Constraint: only hk_2014.1.0-r1 files re-lay out and only the classes of relayout_packfile; the result is refused unless the 8-byte form decodes to the same document (objects renumbered by their moves) after a 4-byte round trip, and a 4-byte source is checked by block, fixup and class counts only (fields_compared is false, its field values are not decoded). The 4-byte layout is derived and the round trip uses it in both directions, so the proof cannot catch a wrong 4-byte offset on its own: hand-written literals in HkxRelayoutLayout.ClassLayoutsMatchHandWrittenHavok2014Layouts catch accidental table or generator edits, not a wrong derivation rule shared with them; derived_table_consistency_checked and round_trip_checked mean preservation under that derived table and narrowed scalar agreement with the decode table; narrow_layout_measured stays false until a real 32-bit file is compared (issue 1731). A refusal is ConversionRefused (Unreadable when the container cannot be read) with the reason, never bytes.
HkxPackfileConversion convert_packfile_pointer_size(std::span<const std::byte> bytes, std::uint8_t target_pointer_size);

}  // namespace fo4::hkx
