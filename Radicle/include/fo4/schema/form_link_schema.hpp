#pragma once

#include "fo4/plugin/container.hpp"
#include "fo4/signature.hpp"

#include <cstddef>
#include <vector>

namespace fo4::schema {

using SubrecordFormLinkOffsets = std::vector<std::vector<std::size_t>>;

bool qust_non_formid_signature(Signature signature) noexcept;
bool deliberate_non_formid_subrecord(const Record& record, const Subrecord& subrecord) noexcept;
SubrecordFormLinkOffsets schema_formlink_offsets(const Record& record);

}  // namespace fo4::schema
