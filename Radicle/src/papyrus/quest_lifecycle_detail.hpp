#pragma once

#include "fo4/papyrus/quest_lifecycle.hpp"

#include <map>
#include <string>
#include <vector>

namespace fo4::papyrus::lifecycle_detail {

using ScriptProperties = std::map<std::string, Value>;

// Invariant: the result is parallel to scripts and keyed by folded property name; any refusal leaves it empty.
bool bindScriptProperties(const OwnerId& owner, const std::vector<std::string>& scripts,
                          const std::vector<QuestStartupProperty>& properties,
                          std::vector<ScriptProperties>& out, std::string& error);

std::string joinReasons(const std::vector<std::string>& reasons, const char* separator);

}  // namespace fo4::papyrus::lifecycle_detail
