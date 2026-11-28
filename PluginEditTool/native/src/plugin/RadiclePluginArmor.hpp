#pragma once

#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include <fo4/plugin/container.hpp>

#include <nlohmann/json.hpp>

#include <string_view>

namespace fo4ide::plugin {

// Requires field to be the PET path of one ARMO or ARMA model or icon row, and row the schema path that row edits.
struct ArmorModelPath {
    std::string_view field;
    std::string_view row;
};

// Constraint: the scalar router and setArmorField read this one table, so a routed path always has a schema row.
const ArmorModelPath* findArmorModelPath(std::string_view field);

Result<fo4::FormKey> createArmorLocked(fo4::Plugin& mutated, const nlohmann::json& payload);
Result<fo4::FormKey> createArmorAddonLocked(fo4::Plugin& mutated, const nlohmann::json& payload);

}
