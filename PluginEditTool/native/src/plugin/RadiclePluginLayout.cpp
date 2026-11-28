#include "RadiclePluginSupport.hpp"

#include <fo4ide/core/JsonInt.hpp>
#include <fo4ide/layout/LayoutMath.hpp>

#include <cmath>
#include <cstddef>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <fo4/environment/environment.hpp>
#include <fo4/forms/cell_edit.hpp>
#include <fo4/forms/placed_reference.hpp>

namespace fo4ide::plugin {

namespace {

constexpr std::size_t k_layout_max_items = 200;
constexpr double k_quarter_turn_radians = 1.57079632679489661923;

nlohmann::json argumentAt(const nlohmann::json& arguments, std::size_t index) {
    return arguments.is_array() && index < arguments.size() ? arguments[index] : nlohmann::json();
}

std::string layoutString(const nlohmann::json& object, const char* key) {
    if (!object.is_object() || !object.contains(key) || !object.at(key).is_string()) return {};
    return object.at(key).get<std::string>();
}

std::optional<float> layoutFloat(const nlohmann::json& object, const char* key) {
    if (!object.is_object() || !object.contains(key) || !object.at(key).is_number()) return std::nullopt;
    return object.at(key).get<float>();
}

std::optional<fo4::forms::PositionRotation> layoutTransform(const nlohmann::json& value) {
    const auto x = layoutFloat(value, "x");
    const auto y = layoutFloat(value, "y");
    const auto z = layoutFloat(value, "z");
    const auto rx = layoutFloat(value, "rx");
    const auto ry = layoutFloat(value, "ry");
    const auto rz = layoutFloat(value, "rz");
    if (!x || !y || !z || !rx || !ry || !rz) return std::nullopt;
    // Constraint: a JSON number past the float range converts to infinity, which must not reach a record.
    for (const float component : {*x, *y, *z, *rx, *ry, *rz}) {
        if (!std::isfinite(component)) return std::nullopt;
    }
    return fo4::forms::PositionRotation{.x = *x, .y = *y, .z = *z, .rx = *rx, .ry = *ry, .rz = *rz};
}

std::optional<fo4ide::layout::Vec3> layoutPoint(const nlohmann::json& object) {
    for (const char* name : {"x", "y", "z"}) {
        if (!object.is_object() || !object.contains(name) || !object.at(name).is_number()) return std::nullopt;
    }
    return fo4ide::layout::Vec3{object.at("x").get<double>(), object.at("y").get<double>(),
                                object.at("z").get<double>()};
}

std::optional<fo4ide::layout::Vec3> layoutCorner(const nlohmann::json& value) {
    if (!value.is_array() || value.size() != 3) return std::nullopt;
    for (const auto& component : value) {
        if (!component.is_number()) return std::nullopt;
    }
    return fo4ide::layout::Vec3{value[0].get<double>(), value[1].get<double>(), value[2].get<double>()};
}

std::optional<fo4ide::layout::Bounds> layoutBounds(const nlohmann::json& value) {
    if (!value.is_object() || !value.contains("min") || !value.contains("max")) return std::nullopt;
    const auto low = layoutCorner(value.at("min"));
    const auto high = layoutCorner(value.at("max"));
    if (!low || !high) return std::nullopt;
    return fo4ide::layout::Bounds{*low, *high};
}

nlohmann::json cornerJson(fo4ide::layout::Vec3 v) {
    return nlohmann::json::array({v.x, v.y, v.z});
}

std::optional<int> sizeField(const nlohmann::json& object, const char* key) {
    if (!object.is_object() || !object.contains(key)) return std::nullopt;
    return core::jsonInt(object.at(key));
}

struct KitPiece {
    std::string baseFormKey;
    fo4ide::layout::Bounds bounds;
};

Result<std::optional<KitPiece>> readKitPiece(const nlohmann::json& kit, const char* role) {
    if (!kit.contains(role) || kit.at(role).is_null()) return std::optional<KitPiece>{};
    const auto& value = kit.at(role);
    const auto key = layoutString(value, "baseFormKey");
    const auto bounds = layoutBounds(value);
    if (key.empty() || !bounds) {
        return std::unexpected(Error{"RoomKitInvalid",
                                     std::string("Kit piece ") + role + " needs baseFormKey and min/max bounds"});
    }
    return std::optional<KitPiece>{KitPiece{key, *bounds}};
}

struct TransformEdit {
    fo4::FormKey key;
    fo4::forms::PositionRotation transform{};
};

Result<std::vector<TransformEdit>> readTransformEdits(const nlohmann::json& items) {
    if (!items.is_array() || items.empty() || items.size() > k_layout_max_items) {
        return std::unexpected(Error{"LayoutItemsInvalid",
                                     "A batch needs 1 to 200 placed references; split larger batches across calls"});
    }
    std::vector<TransformEdit> edits;
    edits.reserve(items.size());
    for (const auto& item : items) {
        const auto transform = item.is_object() && item.contains("transform")
                                   ? layoutTransform(item.at("transform"))
                                   : std::nullopt;
        if (!transform) {
            return std::unexpected(Error{"PlacedTransformArgumentsMissing",
                                         "Each batch item needs numeric x, y, z, rx, ry and rz"});
        }
        TransformEdit edit;
        try {
            edit.key = fo4::FormKey::parse(layoutString(item, "formKey"));
        } catch (const std::exception& error) {
            return std::unexpected(Error{"InvalidFormKey", error.what()});
        }
        edit.transform = *transform;
        edits.push_back(edit);
    }
    return edits;
}

}  // namespace

Result<nlohmann::json> RadiclePluginBackend::listModelBases(const nlohmann::json& arguments) const {
    const auto filter = arguments.is_array() ? argumentAt(arguments, 0) : arguments;
    OpLock lock(*this, OpLock::Mode::Read);
    try {
        return modelBaseRows(*baseObjectIndex(), filter);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"BaseObjectListFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::editPlacedReferences(const nlohmann::json& arguments) {
    const auto patchName = support::stringArgument(arguments, 0);
    if (patchName.empty()) {
        return std::unexpected(Error{"PlacedEditArgumentsMissing", "Batch edit requires a patch plugin"});
    }
    auto edits = readTransformEdits(argumentAt(arguments, 1));
    if (!edits) return std::unexpected(edits.error());

    OpLock lock(*this, OpLock::Mode::Write, {patchName});
    auto* patch = findPlugin(patchName);
    if (patch == nullptr) {
        return std::unexpected(Error{"TargetPluginNotLoaded", "Patch plugin is not loaded yet: " + patchName});
    }
    if (auto guard = writeGuard(*patch); guard.has_value()) return std::unexpected(*guard);

    try {
        CommitScope commit;
        const auto env = borrowedEnvironment();
        auto mutated = patch->plugin;
        nlohmann::json edited = nlohmann::json::array();
        for (const auto& edit : *edits) {
            fo4::forms::PlacedReferenceEdit change;
            change.transform = edit.transform;
            fo4::forms::PlacedReferenceEditResult result;
            try {
                result = fo4::forms::edit_placed_reference(mutated, *env, edit.key, change);
            } catch (const fo4::forms::PlacedReferenceError& error) {
                return std::unexpected(Error{"PlacedEditInvalid", edit.key.to_string() + ": " + error.what()});
            }
            if (result.status != fo4::forms::PlacedReferenceEditStatus::edited) {
                return std::unexpected(Error{"PlacedReferenceEditFailed",
                                             edit.key.to_string() + ": " + result.message});
            }
            edited.push_back({{"formKey", edit.key.to_string()},
                              {"copiedFrom", result.copied_from},
                              {"relocated", result.relocated}});
        }
        stageCellEditLocked(*patch, std::move(mutated));
        return nlohmann::json{{"plugin", patchName}, {"edited", std::move(edited)}};
    } catch (const std::exception& error) {
        return std::unexpected(Error{"PlacedTransformWriteFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::buildRoom(const nlohmann::json& arguments) {
    const auto patchName = support::stringArgument(arguments, 0);
    const auto cellText = support::stringArgument(arguments, 1);
    const auto kit = argumentAt(arguments, 2);
    const auto size = argumentAt(arguments, 3);
    const auto origin = argumentAt(arguments, 4);
    const auto dryRunValue = argumentAt(arguments, 5);
    const bool dryRun = dryRunValue.is_boolean() && dryRunValue.get<bool>();
    if (patchName.empty() || cellText.empty() || !kit.is_object() || !size.is_object()) {
        return std::unexpected(Error{"RoomArgumentsMissing",
                                     "BuildRoom requires patch plugin, cell FormKey, kit and size objects"});
    }
    const auto originPoint = layoutPoint(origin);
    if (!originPoint) return std::unexpected(Error{"RoomArgumentsMissing", "BuildRoom origin needs numeric x, y and z"});
    const auto cellsX = sizeField(size, "cells_x");
    const auto cellsY = sizeField(size, "cells_y");
    const auto heightCells = sizeField(size, "height_cells").value_or(1);
    if (!cellsX || !cellsY) return std::unexpected(Error{"RoomSizeInvalid", "BuildRoom size needs cells_x and cells_y"});
    constexpr int maxDimension = fo4ide::layout::k_room_max_dimension;
    if (*cellsX < 1 || *cellsY < 1 || heightCells < 1 || *cellsX > maxDimension || *cellsY > maxDimension ||
        heightCells > maxDimension) {
        return std::unexpected(Error{"RoomSizeInvalid", "BuildRoom cells_x, cells_y and height_cells must be 1 to 64"});
    }
    const long long footprint = static_cast<long long>(*cellsX) * static_cast<long long>(*cellsY);
    const long long walls = 2LL * (static_cast<long long>(*cellsX) + static_cast<long long>(*cellsY)) * heightCells;
    if (footprint + walls > static_cast<long long>(k_layout_max_items)) {
        return std::unexpected(Error{"RoomTooLarge", "The room footprint plus walls exceeds 200 placements; build it in smaller parts"});
    }

    fo4ide::layout::RoomKit roomKit;
    std::map<fo4ide::layout::PieceCategory, std::string> keyOf;
    const auto attach = [&](const char* role, fo4ide::layout::PieceCategory category,
                            std::optional<fo4ide::layout::Bounds>& slot) -> std::optional<Error> {
        auto piece = readKitPiece(kit, role);
        if (!piece) return piece.error();
        if (!piece->has_value()) return std::nullopt;
        slot = (*piece)->bounds;
        keyOf[category] = (*piece)->baseFormKey;
        return std::nullopt;
    };
    if (auto error = attach("floor", fo4ide::layout::PieceCategory::floor, roomKit.floor)) {
        return std::unexpected(*error);
    }
    if (auto error = attach("wall", fo4ide::layout::PieceCategory::wall, roomKit.wall)) {
        return std::unexpected(*error);
    }
    if (auto error = attach("corner", fo4ide::layout::PieceCategory::corner, roomKit.corner)) {
        return std::unexpected(*error);
    }
    if (auto error = attach("ceiling", fo4ide::layout::PieceCategory::ceiling, roomKit.ceiling)) {
        return std::unexpected(*error);
    }
    if (auto error = attach("door", fo4ide::layout::PieceCategory::door_frame, roomKit.door)) {
        return std::unexpected(*error);
    }

    const auto plan = fo4ide::layout::planRoom(
        roomKit, fo4ide::layout::RoomSize{*cellsX, *cellsY, heightCells}, *originPoint);
    if (!plan.error.empty()) return std::unexpected(Error{"RoomPlanInvalid", plan.error});
    if (plan.placements.size() > k_layout_max_items) {
        return std::unexpected(Error{"RoomTooLarge",
                                     "The room needs " + std::to_string(plan.placements.size()) +
                                         " placements; the limit is 200 per call. Build it in smaller parts "
                                         "(fewer cells or height) and call build_room once per part."});
    }

    nlohmann::json placements = nlohmann::json::array();
    nlohmann::json items = nlohmann::json::array();
    for (const auto& placement : plan.placements) {
        const auto found = keyOf.find(placement.category);
        if (found == keyOf.end()) return std::unexpected(Error{"RoomPlanInvalid", "No kit piece for " + placement.role});
        const nlohmann::json transform{{"x", placement.position.x},
                                       {"y", placement.position.y},
                                       {"z", placement.position.z},
                                       {"rx", 0.0},
                                       {"ry", 0.0},
                                       {"rz", placement.quarterTurns * k_quarter_turn_radians}};
        items.push_back(nlohmann::json{{"baseFormKey", found->second}, {"transform", transform}});
        placements.push_back(nlohmann::json{{"role", placement.role},
                                            {"baseFormKey", found->second},
                                            {"transform", transform}});
    }

    nlohmann::json summary{{"plugin", patchName},
                           {"cellFormKey", cellText},
                           {"count", placements.size()},
                           {"footprint", nlohmann::json{{"min", cornerJson(plan.footprint.min)},
                                                        {"max", cornerJson(plan.footprint.max)}}}};
    if (dryRun) {
        summary["dry_run"] = true;
        summary["placements"] = std::move(placements);
        return summary;
    }

    auto placed = placeReferences(nlohmann::json::array({patchName, cellText, items, nlohmann::json::object()}));
    if (!placed) return std::unexpected(placed.error());
    auto doc = placed->is_string() ? nlohmann::json::parse(placed->get<std::string>(), nullptr, false) : *placed;
    if (doc.is_discarded() || !doc.is_object()) {
        return std::unexpected(Error{"RoomPlaceFailed", "Placement returned an unreadable document"});
    }
    summary["dry_run"] = false;
    summary["created"] = doc.value("created", nlohmann::json::array());
    summary["references"] = doc.value("references", nlohmann::json::array());
    return summary;
}

}  // namespace fo4ide::plugin
