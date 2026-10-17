#pragma once

#include "fo4/plugin/container.hpp"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <vector>

namespace fo4 {

struct FixedStrideLayout {
  const char* signature;
  std::size_t stride;
  std::initializer_list<std::size_t> slots;
  bool exact;
  std::size_t short_size;
  std::uint16_t min_form_version{0};
};

std::optional<std::vector<std::size_t>> fixed_stride_formid_offsets(
    const Subrecord& sub, std::uint16_t form_version, std::span<const FixedStrideLayout> layouts,
    bool strict_remap);
std::optional<std::vector<std::size_t>> nvnm_formid_offsets(std::span<const std::byte> data);
bool is_navmesh_mixed_payload(const Record& record, const Subrecord& sub);
std::optional<std::vector<std::size_t>> navmesh_mixed_formid_offsets(const Record& record,
                                                                    const Subrecord& sub);
std::optional<std::vector<std::size_t>> struct_payload_formid_offsets(const Record& record,
                                                                     const Subrecord& sub,
                                                                     bool strict_remap);
std::optional<std::vector<std::size_t>> obts_formid_offsets(const Subrecord& sub);
std::optional<std::vector<std::size_t>> perk_formid_offsets(const Record& record,
                                                           const Subrecord& sub);
std::optional<std::vector<std::size_t>> packed_formid_array_offsets(const Record& record,
                                                                   const Subrecord& sub,
                                                                   bool strict_remap);
std::optional<std::vector<std::size_t>> placed_reference_formid_offsets(const Record& record,
                                                                       const Subrecord& sub,
                                                                       bool strict_remap);
bool is_placed_reference_signature(Signature signature) noexcept;
std::optional<std::vector<std::size_t>> mixed_formid_offsets(const Record& record,
                                                            const Subrecord& sub,
                                                            bool strict_remap);
std::optional<std::vector<std::size_t>> mgef_assoc_item_formid_offsets(const Record& record,
                                                                      const Subrecord& sub);

}
