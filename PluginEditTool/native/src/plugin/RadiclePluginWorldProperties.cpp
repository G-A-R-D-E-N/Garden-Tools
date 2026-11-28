#include "RadiclePluginCellArgs.hpp"
#include "RadiclePluginSupport.hpp"

#include <fo4/environment/environment.hpp>
#include <fo4/environment/world.hpp>
#include <fo4/forms/world_edit.hpp>
#include <fo4/plugin/reader.hpp>

#include <array>
#include <cstdint>
#include <exception>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace fo4ide::plugin {

namespace {

using cell_args::floatValue;
using cell_args::integerValue;
using cell_args::linkValue;

nlohmann::json linkJson(const std::optional<fo4::FormKey>& key) {
    return key ? nlohmann::json(key->to_string()) : nlohmann::json(nullptr);
}

std::optional<Error> worldEditError(const fo4::forms::WorldEditResult& result,
                                    std::string_view patchName,
                                    const std::string& worldText) {
    using Status = fo4::forms::WorldEditStatus;
    switch (result.status) {
        case Status::edited:
            return std::nullopt;
        case Status::world_missing:
            return Error{"WorldNotFound", "Worldspace is not present in " + std::string(patchName) + ": " + worldText};
        case Status::world_deleted:
            return Error{"WorldDeleted", "The winning version of " + worldText + " is deleted"};
        case Status::not_a_world:
            return Error{"WorldNotAWorld", worldText + " is not a WRLD record"};
        case Status::target_missing:
            return Error{"WorldTargetMissing", "A link target of " + worldText + " is not in the load order"};
        case Status::target_type_not_allowed:
            return Error{"WorldTargetTypeNotAllowed", "A link target of " + worldText + " has the wrong record type"};
        case Status::target_not_master:
            return Error{"WorldTargetNotMaster",
                         "A link target of " + worldText + " must be in the patch or one of its masters"};
        case Status::invalid_value:
            return Error{"WorldPropertyInvalid", "A value for " + worldText + " is out of range or not finite"};
        case Status::malformed:
            return Error{"WorldMalformed", worldText + " has a malformed subrecord"};
    }
    return Error{"WorldPropertyWriteFailed", worldText};
}

nlohmann::json pointJson(const std::optional<std::array<float, 2>>& point) {
    if (!point) return nlohmann::json(nullptr);
    return nlohmann::json::array({(*point)[0], (*point)[1]});
}

nlohmann::json worldPropertyValues(const fo4::env::WorldspaceData& data) {
    const nlohmann::json none = nullptr;
    return {
        {"Parent", linkJson(data.parent)},
        {"ParentFlags", data.parent_flags ? nlohmann::json(*data.parent_flags) : none},
        {"Climate", linkJson(data.climate)},
        {"Water", linkJson(data.water)},
        {"LightingTemplate", linkJson(data.lighting_template)},
        {"DefaultLandHeight", data.default_land_height ? nlohmann::json(*data.default_land_height) : none},
        {"DefaultWaterHeight", data.default_water_height ? nlohmann::json(*data.default_water_height) : none},
        {"BoundsMin", pointJson(data.bounds_min)},
        {"BoundsMax", pointJson(data.bounds_max)},
        {"Flags", data.flags ? nlohmann::json(*data.flags) : none},
        {"LodWater", linkJson(data.lod_water)},
        {"LodWaterHeight", data.lod_water_height ? nlohmann::json(*data.lod_water_height) : none},
    };
}

nlohmann::json worldPropertyDescriptorsJson() {
    nlohmann::json descriptors = nlohmann::json::array();
    for (const auto& descriptor : fo4::forms::world_property_descriptors()) {
        const std::string kind = descriptor.kind == fo4::forms::WorldPropertyKind::link
                                     ? "link"
                                     : descriptor.kind == fo4::forms::WorldPropertyKind::flags ? "flags" : "scalar";
        descriptors.push_back({
            {"name", descriptor.name},
            {"label", descriptor.label},
            {"kind", kind},
            {"minimum", descriptor.minimum},
            {"maximum", descriptor.maximum},
            {"targets", descriptor.targets}
        });
    }
    return descriptors;
}

std::array<float, 2> pointValue(const nlohmann::json& value, const std::string& name) {
    if (!value.is_array() || value.size() != 2) {
        throw std::invalid_argument(name + " must be an [x, y] array");
    }
    return {floatValue(value[0], name), floatValue(value[1], name)};
}

fo4::forms::WorldEdit worldEditFromChanges(const nlohmann::json& changes) {
    fo4::forms::WorldEdit edit;
    for (const auto& item : changes.items()) {
        const std::string& name = item.key();
        const auto& value = item.value();
        if (name == "Flags") edit.flags = static_cast<std::uint8_t>(integerValue(value, name, 0, 255));
        else if (name == "ParentFlags") edit.parent_flags = static_cast<std::uint16_t>(integerValue(value, name, 0, 65535));
        else if (name == "Parent") edit.parent = linkValue(value, name);
        else if (name == "Climate") edit.climate = linkValue(value, name);
        else if (name == "Water") edit.water = linkValue(value, name);
        else if (name == "LightingTemplate") edit.lighting_template = linkValue(value, name);
        else if (name == "DefaultLandHeight") edit.default_land_height = floatValue(value, name);
        else if (name == "DefaultWaterHeight") edit.default_water_height = floatValue(value, name);
        else if (name == "BoundsMin") edit.bounds_min = pointValue(value, name);
        else if (name == "BoundsMax") edit.bounds_max = pointValue(value, name);
        else if (name == "LodWater") edit.lod_water = linkValue(value, name);
        else if (name == "LodWaterHeight") edit.lod_water_height = floatValue(value, name);
        else throw std::invalid_argument("SetWorldProperties does not edit " + name);
    }
    return edit;
}

}  // namespace

Result<nlohmann::json> RadiclePluginBackend::getWorldProperties(const nlohmann::json& arguments) const {
    const auto worldText = support::stringArgument(arguments, 0);
    if (worldText.empty()) return std::unexpected(Error{"WorldMissing", "Worldspace FormKey is empty"});

    fo4::FormKey key;
    try {
        key = fo4::FormKey::parse(worldText);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InvalidFormKey", error.what()});
    }

    OpLock lock(*this, OpLock::Mode::Read);
    try {
        const auto env = borrowedEnvironment();
        const auto* winner = env->winner(key);
        if (winner == nullptr) {
            return std::unexpected(Error{"WorldNotFound", "Worldspace not found: " + key.to_string()});
        }
        if (winner->record->header.flags.is_deleted()) {
            return std::unexpected(Error{"WorldDeleted",
                                         "The winning version of " + key.to_string() + " is deleted"});
        }
        if (winner->record->header.signature != fo4::Signature::from_string("WRLD")) {
            return std::unexpected(Error{"WorldNotAWorld", key.to_string() + " is not a WRLD record"});
        }
        const auto data = fo4::env::decode_worldspace(*env, *winner);
        return nlohmann::json(nlohmann::json{
            {"formKey", key.to_string()},
            {"values", worldPropertyValues(data)},
            {"descriptors", worldPropertyDescriptorsJson()}
        }.dump());
    } catch (const std::exception& error) {
        return std::unexpected(Error{"WorldPropertiesReadFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::setWorldProperties(const nlohmann::json& arguments) {
    const auto worldText = support::stringArgument(arguments, 0);
    const auto patchName = support::stringArgument(arguments, 1);
    const auto changes = arguments.is_array() && arguments.size() > 2 ? arguments[2] : nlohmann::json();
    if (worldText.empty() || patchName.empty() || !changes.is_object() || changes.empty()) {
        return std::unexpected(Error{
            "WorldPropertiesArgumentsMissing",
            "SetWorldProperties requires a worldspace FormKey, patch plugin and a nonempty changes object"
        });
    }

    fo4::FormKey key;
    try {
        key = fo4::FormKey::parse(worldText);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InvalidFormKey", error.what()});
    }
    fo4::forms::WorldEdit edit;
    try {
        edit = worldEditFromChanges(changes);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"WorldPropertyInvalid", error.what()});
    }

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
        const auto result = fo4::forms::edit_world(mutated, *env, key, edit);
        if (const auto error = worldEditError(result, patch->name, key.to_string())) {
            return std::unexpected(*error);
        }
        stageCellEditLocked(*patch, std::move(mutated));
        return nlohmann::json(nlohmann::json{
            {"plugin", patchName},
            {"formKey", key.to_string()},
            {"copiedFrom", result.copied_from}
        }.dump());
    } catch (const std::exception& error) {
        return std::unexpected(Error{"WorldPropertyWriteFailed", error.what()});
    }
}

}  // namespace fo4ide::plugin
