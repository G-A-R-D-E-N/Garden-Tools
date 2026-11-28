#pragma once

#include <optional>

#include <nlohmann/json.hpp>

#include <fo4/forms/cell_edit.hpp>
#include <fo4ide/core/Error.hpp>

namespace fo4ide::plugin::editargs {

std::optional<float> numberField(const nlohmann::json& object, const char* key);
std::optional<Error> parseOwnershipEdit(const nlohmann::json& value, fo4::forms::PlacedReferenceEdit& edit);
std::optional<Error> parsePrimitiveEdit(const nlohmann::json& value, fo4::forms::PlacedReferenceEdit& edit);
std::optional<Error> parseLinkedReferencesEdit(const nlohmann::json& value, fo4::forms::PlacedReferenceEdit& edit);
std::optional<Error> parseTeleportEdit(const nlohmann::json& value, fo4::forms::PlacedReferenceEdit& edit);

}  // namespace fo4ide::plugin::editargs