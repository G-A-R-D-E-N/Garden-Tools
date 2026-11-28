#pragma once

#include "fo4/plugin/container.hpp"

#include <optional>

namespace fo4::identical {

bool same_record(const Record& left, const Record& right, bool exact = false);

std::optional<Record> remapped_into(const Record& record,
                                    const Plugin& source,
                                    const PluginFormIdContext& target);

}
