#pragma once

#include "fo4/plugin/container.hpp"

#include <optional>
#include <vector>

namespace fo4 {

std::optional<std::vector<std::size_t>> behavior_field_candidates(const Record& record, Signature subrecord,
                                                                 bool repeated);
std::optional<std::vector<std::vector<std::size_t>>> behavior_field_candidate_scopes(
    const Record& record, Signature subrecord, bool repeated);
bool behavior_field_payload_valid(const Record& record, const Subrecord& subrecord);
std::optional<std::vector<std::size_t>> behavior_formid_offsets(const Record& record,
                                                              const Subrecord& subrecord,
                                                              bool strict_remap);

}
