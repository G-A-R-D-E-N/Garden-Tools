#pragma once

#include "fo4/plugin/container.hpp"

#include <cstddef>
#include <optional>
#include <vector>

namespace fo4 {

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

}
