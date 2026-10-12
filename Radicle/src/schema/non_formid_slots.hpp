#pragma once

#include "fo4/plugin/container.hpp"

namespace fo4::schema {

bool corpus_non_formid_slot(const Record& record, const Subrecord& subrecord) noexcept;
bool scene_action_topic_data(const Record& record, const Subrecord& subrecord) noexcept;

}  // namespace fo4::schema
