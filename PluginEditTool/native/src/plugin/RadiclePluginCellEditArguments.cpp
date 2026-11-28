#include "RadiclePluginCellEditArguments.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <exception>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <fo4ide/core/JsonInt.hpp>
#include <fo4/forms/cell_edit.hpp>
#include <fo4/forms/placed_reference.hpp>

namespace fo4ide::plugin::editargs {

std::optional<float> numberField(const nlohmann::json& object, const char* key) {
    if (!object.is_object() || !object.contains(key) || !object.at(key).is_number()) return std::nullopt;
    // Constraint: a JSON number too large for a float converts to infinity, which must not reach a record.
    const auto value = object.at(key).get<float>();
    if (!std::isfinite(value)) return std::nullopt;
    return value;
}

std::optional<Error> parseOwnershipEdit(const nlohmann::json& value, fo4::forms::PlacedReferenceEdit& edit) {
    const auto invalid = Error{
        "PlacedEditArgumentsMissing",
        "EditPlacedReference ownership must be null or {owner: FormKey, noCrime: bool, rank: integer or null}"
    };
    if (value.is_null()) {
        edit.ownership = fo4::forms::OwnershipEdit{};
        return std::nullopt;
    }
    if (!value.is_object() || !value.contains("owner") || !value.at("owner").is_string()) return invalid;
    fo4::forms::OwnershipEdit ownership;
    try {
        ownership.owner = fo4::FormKey::parse(value.at("owner").get<std::string>());
    } catch (const std::exception& error) {
        return Error{"InvalidFormKey", error.what()};
    }
    if (value.contains("noCrime")) {
        if (!value.at("noCrime").is_boolean()) return invalid;
        ownership.no_crime = value.at("noCrime").get<bool>();
    }
    if (value.contains("rank") && !value.at("rank").is_null()) {
        const auto rank = core::jsonInt(value.at("rank"));
        if (!rank) return invalid;
        ownership.faction_rank = *rank;
    }
    edit.ownership = std::move(ownership);
    return std::nullopt;
}

std::optional<Error> parsePrimitiveEdit(const nlohmann::json& value, fo4::forms::PlacedReferenceEdit& edit) {
    const auto invalid = Error{
        "PlacedEditArgumentsMissing",
        "EditPlacedReference primitive must be {bounds: {x, y, z}, color: [r, g, b, a]} and may omit either part"
    };
    if (!value.is_object()) return invalid;
    if (value.contains("type")) return Error{"PlacedEditInvalid", "The primitive shape type is read-only"};
    fo4::forms::PrimitiveValuesEdit values;
    if (value.contains("bounds")) {
        const auto& bounds = value.at("bounds");
        const auto x = numberField(bounds, "x");
        const auto y = numberField(bounds, "y");
        const auto z = numberField(bounds, "z");
        if (!x || !y || !z) return invalid;
        if (*x < 0.0f || *y < 0.0f || *z < 0.0f) {
            return Error{"PlacedEditArgumentsMissing", "EditPlacedReference primitive bounds must be non-negative"};
        }
        values.bounds = std::array<float, 3>{*x, *y, *z};
    }
    if (value.contains("color")) {
        const auto& color = value.at("color");
        if (!color.is_array() || color.size() != 4) return invalid;
        std::array<float, 4> rgba{};
        for (std::size_t i = 0; i < rgba.size(); ++i) {
            if (!color[i].is_number()) return invalid;
            rgba[i] = color[i].get<float>();
        }
        values.color = rgba;
    }
    if (!values.bounds && !values.color) return invalid;
    edit.primitive_values = std::move(values);
    return std::nullopt;
}

std::optional<Error> parseLinkedReferencesEdit(const nlohmann::json& value, fo4::forms::PlacedReferenceEdit& edit) {
    const auto invalid = Error{
        "PlacedEditArgumentsMissing",
        "EditPlacedReference linkedReferences must be an array of {keyword: FormKey or null, reference: FormKey}"
    };
    if (!value.is_array()) return invalid;
    std::vector<fo4::forms::LinkedReferenceEdit> links;
    for (const auto& entry : value) {
        if (!entry.is_object() || !entry.contains("reference") || !entry.at("reference").is_string()) return invalid;
        fo4::forms::LinkedReferenceEdit link;
        try {
            link.reference = fo4::FormKey::parse(entry.at("reference").get<std::string>());
            if (entry.contains("keyword") && !entry.at("keyword").is_null()) {
                if (!entry.at("keyword").is_string()) return invalid;
                link.keyword = fo4::FormKey::parse(entry.at("keyword").get<std::string>());
            }
        } catch (const std::exception& error) {
            return Error{"InvalidFormKey", error.what()};
        }
        links.push_back(std::move(link));
    }
    edit.linked_references = std::move(links);
    return std::nullopt;
}

std::optional<Error> parseTeleportEdit(const nlohmann::json& value, fo4::forms::PlacedReferenceEdit& edit) {
    const auto invalid = Error{
        "PlacedEditArgumentsMissing",
        "EditPlacedReference teleport must be {door: DOOR FormKey, position: {x, y, z}, rotation: {x, y, z}}"
    };
    if (!value.is_object() || !value.contains("door") || !value.at("door").is_string() ||
        !value.contains("position") || !value.contains("rotation")) {
        return invalid;
    }
    const auto& position = value.at("position");
    const auto& rotation = value.at("rotation");
    const auto x = numberField(position, "x");
    const auto y = numberField(position, "y");
    const auto z = numberField(position, "z");
    const auto rx = numberField(rotation, "x");
    const auto ry = numberField(rotation, "y");
    const auto rz = numberField(rotation, "z");
    if (!x || !y || !z || !rx || !ry || !rz) return invalid;
    fo4::forms::TeleportEdit teleport;
    try {
        teleport.door = fo4::FormKey::parse(value.at("door").get<std::string>());
    } catch (const std::exception& error) {
        return Error{"InvalidFormKey", error.what()};
    }
    teleport.destination = fo4::forms::PositionRotation{.x = *x, .y = *y, .z = *z, .rx = *rx, .ry = *ry, .rz = *rz};
    edit.teleport = std::move(teleport);
    return std::nullopt;
}

}  // namespace fo4ide::plugin::editargs