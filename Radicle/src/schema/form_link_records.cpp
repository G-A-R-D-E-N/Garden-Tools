#include "fo4/schema/form_link_records.hpp"
#include "fo4/schema/form_link_mixed.hpp"
#include "fo4/binary/raw.hpp"

#include <cstdint>
#include <span>

namespace fo4 {
namespace {

using fo4::binary::raw::read_u32;

std::optional<std::vector<std::size_t>> malformed(bool strict_remap, const char* message) {
  if (strict_remap) throw FormIdError(message);
  return std::vector<std::size_t>{};
}

struct RecordFixedLayout {
  const char* record;
  FixedStrideLayout layout;
};

const RecordFixedLayout k_record_fixed_layouts[] = {
    {"LCTN", {"ACPR", 12, {0, 4}, false, 0}},
    {"LCTN", {"LCPR", 12, {0, 4}, false, 0}},
    {"LCTN", {"RCPR", 4, {0}, false, 0}},
    {"LCTN", {"ACUN", 12, {0, 4, 8}, false, 0}},
    {"LCTN", {"LCUN", 12, {0, 4, 8}, false, 0}},
    {"LCTN", {"RCUN", 4, {0}, false, 0}},
    {"LCTN", {"ACSR", 16, {0, 4, 8}, false, 0}},
    {"LCTN", {"LCSR", 16, {0, 4, 8}, false, 0}},
    {"LCTN", {"RCSR", 4, {0}, false, 0}},
    {"LCTN", {"ACID", 4, {0}, false, 0}},
    {"LCTN", {"LCID", 4, {0}, false, 0}},
    {"LCTN", {"ACEP", 12, {0, 4}, false, 0}},
    {"LCTN", {"LCEP", 12, {0, 4}, false, 0}},
    {"BPTD", {"BPND", 101, {4, 8, 16, 20, 52, 56, 67, 79, 83, 87, 91}, true, 0}},
    {"CELL", {"XPRI", 4, {0}, false, 0}},
    {"IDLM", {"IDLA", 4, {0}, false, 0}},
    {"MUST", {"SNAM", 4, {0}, false, 0}},
    {"LAND", {"BTXT", 8, {0}, true, 0}},
    {"LAND", {"ATXT", 8, {0}, true, 0}},
    {"RACE", {"TTEC", 14, {0}, false, 0}},
    {"REGN", {"RDOT", 52, {0}, false, 0}},
    {"REGN", {"RDGS", 8, {0}, false, 0}},
    {"REGN", {"RDWT", 12, {0, 8}, false, 0}},
    {"REGN", {"RDSA", 12, {0}, false, 0}},
};

std::optional<std::vector<std::size_t>> record_fixed_layout_offsets(const Record& record,
                                                                   const Subrecord& sub,
                                                                   bool strict_remap) {
  for (const auto& entry : k_record_fixed_layouts) {
    if (record.header.signature != Signature::from_string(entry.record)) continue;
    if (auto offsets = fixed_stride_formid_offsets(
            sub, record.header.form_version,
            std::span<const FixedStrideLayout>(&entry.layout, 1), strict_remap)) {
      return offsets;
    }
  }
  return std::nullopt;
}

std::optional<std::vector<std::size_t>> worldspace_cells_offsets(const Record& record,
                                                                const Subrecord& sub,
                                                                bool strict_remap) {
  if (record.header.signature != sig_lctn) return std::nullopt;
  if (sub.signature != sig_acec && sub.signature != sig_lcec && sub.signature != sig_rcec) {
    return std::nullopt;
  }
  if (sub.data.size() < 4 || sub.data.size() % 4 != 0) {
    return malformed(strict_remap, "cannot safely remap malformed LCTN worldspace cell payload");
  }
  return std::vector<std::size_t>{0};
}

std::optional<std::vector<std::size_t>> combined_reference_offsets(const Record& record,
                                                                  const Subrecord& sub,
                                                                  bool strict_remap) {
  if (record.header.signature != sig_cell || sub.signature != sig_xcri) return std::nullopt;
  const auto size = sub.data.size();
  if (size < 8) return malformed(strict_remap, "cannot safely remap malformed CELL XCRI payload");
  const auto meshes = std::uint64_t{read_u32(sub.data, 0)};
  const auto reference_words = std::uint64_t{read_u32(sub.data, 4)};
  if (reference_words % 2 != 0 || std::uint64_t{size - 8} != 4 * meshes + 4 * reference_words) {
    return malformed(strict_remap, "cannot safely remap malformed CELL XCRI payload");
  }
  const std::size_t first = 8 + 4 * static_cast<std::size_t>(meshes);
  std::vector<std::size_t> offsets;
  for (std::size_t reference = 0; reference < reference_words / 2; ++reference) {
    offsets.push_back(first + 8 * reference);
  }
  return offsets;
}

std::optional<std::vector<std::size_t>> race_data_offsets(const Record& record,
                                                         const Subrecord& sub,
                                                         bool strict_remap) {
  if (record.header.signature != sig_race || sub.signature != sig_data) return std::nullopt;
  const auto version = record.header.form_version;
  const std::size_t severable = (version >= 109 ? 32 : 8) + (version >= 124 ? 114 : 110);
  const std::size_t expected = severable + 24 + (version >= 96 ? 18 : 0) +
                               (version >= 98 ? 4 : 0) + (version >= 101 ? 4 : 0) +
                               (version >= 118 ? 4 : 0);
  if (sub.data.size() != expected) {
    return malformed(strict_remap, "cannot safely remap malformed RACE DATA payload");
  }
  std::vector<std::size_t> offsets{severable,      severable + 4,  severable + 8,
                                   severable + 12, severable + 16, severable + 20};
  std::size_t cursor = severable + 24;
  if (version >= 96) {
    offsets.push_back(cursor + 6);
    offsets.push_back(cursor + 10);
    offsets.push_back(cursor + 14);
    cursor += 18;
  }
  if (version >= 118) offsets.push_back(cursor);
  return offsets;
}

std::optional<std::vector<std::size_t>> stag_tnam_offsets(const Record& record,
                                                         const Subrecord& sub,
                                                         bool strict_remap) {
  if (record.header.signature != sig_stag || sub.signature != sig_tnam) return std::nullopt;
  if (sub.data.size() < 4) {
    return malformed(strict_remap, "cannot safely remap malformed STAG TNAM payload");
  }
  return std::vector<std::size_t>{0};
}

std::optional<std::vector<std::size_t>> transient_type_offsets(const Record& record,
                                                              const Subrecord& sub,
                                                              bool strict_remap) {
  if (record.header.signature != sig_tes4 || sub.signature != sig_tnam) return std::nullopt;
  const auto size = sub.data.size();
  if (size < 4 || size % 4 != 0) {
    return malformed(strict_remap, "cannot safely remap malformed TES4 TNAM payload");
  }
  std::vector<std::size_t> offsets;
  for (std::size_t offset = 4; offset < size; offset += 4) offsets.push_back(offset);
  return offsets;
}

std::optional<std::vector<std::size_t>> large_reference_offsets(const Record& record,
                                                               const Subrecord& sub,
                                                               bool strict_remap) {
  if (record.header.signature != sig_wrld || sub.signature != sig_rnam) return std::nullopt;
  const auto size = sub.data.size();
  if (size < 8) return malformed(strict_remap, "cannot safely remap malformed WRLD RNAM payload");
  const auto count = read_u32(sub.data, 4);
  if (std::uint64_t{size - 8} != std::uint64_t{8} * count) {
    return malformed(strict_remap, "cannot safely remap malformed WRLD RNAM payload");
  }
  std::vector<std::size_t> offsets;
  for (std::size_t reference = 0; reference < count; ++reference) {
    offsets.push_back(8 + 8 * reference);
  }
  return offsets;
}

std::optional<std::vector<std::size_t>> nvnm_outside_navm_offsets(const Record& record,
                                                                 const Subrecord& sub,
                                                                 bool strict_remap) {
  const auto signature = record.header.signature;
  if (sub.signature != sig_nvnm ||
      (signature != sig_acti && signature != sig_furn && signature != sig_stat)) {
    return std::nullopt;
  }
  if (record.header.form_version < 128 || sub.data.size() < 4 || read_u32(sub.data, 0) != 15) {
    return malformed(strict_remap, "cannot safely remap malformed NVNM payload");
  }
  auto offsets = nvnm_formid_offsets(sub.data);
  if (!offsets) return malformed(strict_remap, "cannot safely remap malformed NVNM payload");
  return offsets;
}

}  // namespace

std::optional<std::vector<std::size_t>> record_struct_formid_offsets(const Record& record,
                                                                    const Subrecord& sub,
                                                                    bool strict_remap) {
  if (auto offsets = record_fixed_layout_offsets(record, sub, strict_remap)) return offsets;
  if (auto offsets = worldspace_cells_offsets(record, sub, strict_remap)) return offsets;
  if (auto offsets = combined_reference_offsets(record, sub, strict_remap)) return offsets;
  if (auto offsets = race_data_offsets(record, sub, strict_remap)) return offsets;
  if (auto offsets = stag_tnam_offsets(record, sub, strict_remap)) return offsets;
  if (auto offsets = transient_type_offsets(record, sub, strict_remap)) return offsets;
  if (auto offsets = large_reference_offsets(record, sub, strict_remap)) return offsets;
  return nvnm_outside_navm_offsets(record, sub, strict_remap);
}

}  // namespace fo4
