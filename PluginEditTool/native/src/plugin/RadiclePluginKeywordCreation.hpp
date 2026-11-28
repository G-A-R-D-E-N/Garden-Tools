#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

#include <fo4/schema/record_constraints.hpp>

#include <nlohmann/json.hpp>

#include "RadiclePluginSupport.hpp"

namespace fo4ide::plugin::keyword_creation {

// Constraint: do not move after apply(); rules point into this struct.
struct AuthoringRules {
    std::vector<fo4::schema::SubrecordAuthoringConstraint> rules;
    std::array<std::uint8_t, 4> color{};
    std::array<std::uint8_t, 4> type{};
};

Result<void> apply(std::string_view signature,
                   const nlohmann::json& payload,
                   AuthoringRules& out,
                   std::vector<fo4::schema::SubrecordAuthoringConstraint>& rules,
                   bool& explicitRules);

}
