#include "RadiclePluginSupport.hpp"

#include <fo4/environment/cell_view.hpp>
#include <fo4/environment/world.hpp>
#include <fo4/forms/cell_edit.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4ide::plugin {

namespace {

std::int64_t integerValue(const nlohmann::json& value, const std::string& name,
                          std::int64_t minimum, std::int64_t maximum) {
    if (!value.is_number_integer()) throw std::invalid_argument(name + " must be an integer");
    const auto number = value.get<std::int64_t>();
    if (number < minimum || number > maximum) throw std::invalid_argument(name + " is out of range");
    return number;
}

std::int32_t int32Value(const nlohmann::json& value, const std::string& name) {
    return static_cast<std::int32_t>(integerValue(value, name,
                                                  std::numeric_limits<std::int32_t>::min(),
                                                  std::numeric_limits<std::int32_t>::max()));
}

float floatValue(const nlohmann::json& value, const std::string& name) {
    if (!value.is_number()) throw std::invalid_argument(name + " must be a number");
    return value.get<float>();
}

std::array<std::uint8_t, 3> colourValue(const nlohmann::json& value, const std::string& name) {
    if (!value.is_array() || value.size() != 3) {
        throw std::invalid_argument(name + " must be an [r, g, b] array");
    }
    std::array<std::uint8_t, 3> colour{};
    for (std::size_t channel = 0; channel < colour.size(); ++channel) {
        colour[channel] = static_cast<std::uint8_t>(integerValue(value[channel], name, 0, 255));
    }
    return colour;
}

std::optional<fo4::FormKey> linkValue(const nlohmann::json& value, const std::string& name) {
    if (value.is_null()) return std::nullopt;
    if (!value.is_string()) throw std::invalid_argument(name + " must be a FormKey or null");
    return fo4::FormKey::parse(value.get<std::string>());
}

std::vector<fo4::FormKey> linkArray(const nlohmann::json& value, const std::string& name) {
    if (!value.is_array()) throw std::invalid_argument(name + " must be an array of FormKeys");
    std::vector<fo4::FormKey> keys;
    for (const auto& item : value) {
        if (!item.is_string()) throw std::invalid_argument(name + " must be an array of FormKeys");
        keys.push_back(fo4::FormKey::parse(item.get<std::string>()));
    }
    return keys;
}

fo4::forms::CellLightingEdit& lightingEdit(fo4::forms::CellEdit& edit) {
    if (!edit.lighting) edit.lighting.emplace();
    return *edit.lighting;
}

fo4::forms::CellEdit cellEditFromChanges(const nlohmann::json& changes) {
    fo4::forms::CellEdit edit;
    for (const auto& item : changes.items()) {
        const std::string& name = item.key();
        const auto& value = item.value();
        if (name == "Flags") edit.flags = static_cast<std::uint16_t>(integerValue(value, name, 0, 65535));
        else if (name == "LightingAmbient") lightingEdit(edit).ambient = colourValue(value, name);
        else if (name == "LightingDirectional") lightingEdit(edit).directional = colourValue(value, name);
        else if (name == "LightingFogNearColor") lightingEdit(edit).fog_near_color = colourValue(value, name);
        else if (name == "LightingFogNear") lightingEdit(edit).fog_near = floatValue(value, name);
        else if (name == "LightingFogFar") lightingEdit(edit).fog_far = floatValue(value, name);
        else if (name == "LightingDirectionalRotationXY") lightingEdit(edit).directional_rotation_xy = int32Value(value, name);
        else if (name == "LightingDirectionalRotationZ") lightingEdit(edit).directional_rotation_z = int32Value(value, name);
        else if (name == "LightingDirectionalFade") lightingEdit(edit).directional_fade = floatValue(value, name);
        else if (name == "LightingFogClipDistance") lightingEdit(edit).fog_clip_distance = floatValue(value, name);
        else if (name == "LightingFogPower") lightingEdit(edit).fog_power = floatValue(value, name);
        else if (name == "LightingTemplate") edit.lighting_template = linkValue(value, name);
        else if (name == "WaterHeight") edit.water_height = floatValue(value, name);
        else if (name == "Water") edit.water = linkValue(value, name);
        else if (name == "ImageSpace") edit.image_space = linkValue(value, name);
        else if (name == "EncounterZone") edit.encounter_zone = linkValue(value, name);
        else if (name == "Location") edit.location = linkValue(value, name);
        else if (name == "AcousticSpace") edit.acoustic_space = linkValue(value, name);
        else if (name == "MusicType") edit.music_type = linkValue(value, name);
        else if (name == "SkyWeatherRegion") edit.sky_weather_region = linkValue(value, name);
        else if (name == "GodRays") edit.god_rays = linkValue(value, name);
        else if (name == "LockList") edit.lock_list = linkValue(value, name);
        else if (name == "Owner") edit.owner = linkValue(value, name);
        else if (name == "OwnerRank") edit.owner_rank = int32Value(value, name);
        else if (name == "Regions") edit.regions = linkArray(value, name);
        else throw std::invalid_argument("SetCellProperties does not edit " + name);
    }
    return edit;
}

std::optional<Error> cellEditError(const fo4::forms::CellEditResult& result,
                                   std::string_view patchName,
                                   const std::string& cellText) {
    using Status = fo4::forms::CellEditStatus;
    switch (result.status) {
        case Status::edited:
            return std::nullopt;
        case Status::cell_missing:
            return Error{"CellNotFound", "Cell is not present in " + std::string(patchName) + ": " + cellText};
        case Status::cell_deleted:
            return Error{"CellDeleted", "The winning version of " + cellText + " is deleted"};
        case Status::not_a_cell:
            return Error{"CellNotACell", cellText + " is not a CELL record"};
        case Status::target_missing:
            return Error{"CellTargetMissing", "A link target of " + cellText + " is not in the load order"};
        case Status::target_type_not_allowed:
            return Error{"CellTargetTypeNotAllowed", "A link target of " + cellText + " has the wrong record type"};
        case Status::target_not_master:
            return Error{"CellTargetNotMaster",
                         "A link target of " + cellText + " must be in the patch or one of its masters"};
        case Status::interior_flag_locked:
            return Error{"CellInteriorFlagLocked", "The interior flag of " + cellText + " cannot change"};
        case Status::invalid_value:
            return Error{"CellPropertyInvalid", "A value for " + cellText + " is out of range or not finite"};
        case Status::malformed:
            return Error{"CellMalformed", cellText + " has a malformed subrecord"};
    }
    return Error{"CellPropertyWriteFailed", cellText};
}

nlohmann::json colourJson(const fo4::env::ByteColor& colour) {
    return nlohmann::json::array({colour.red, colour.green, colour.blue});
}

nlohmann::json linkJson(const std::optional<fo4::FormKey>& key) {
    return key ? nlohmann::json(key->to_string()) : nlohmann::json(nullptr);
}

nlohmann::json cellPropertyValues(const fo4::env::CellData& data) {
    const auto* lighting = data.lighting ? &*data.lighting : nullptr;
    const nlohmann::json none = nullptr;
    nlohmann::json regions = nlohmann::json::array();
    for (const auto& region : data.regions) regions.push_back(region.to_string());
    return {
        {"Flags", data.flags ? nlohmann::json(*data.flags) : none},
        {"LightingAmbient", lighting ? colourJson(lighting->ambient) : none},
        {"LightingDirectional", lighting ? colourJson(lighting->directional) : none},
        {"LightingFogNearColor", lighting ? colourJson(lighting->fog_near_color) : none},
        {"LightingFogNear", lighting ? nlohmann::json(lighting->fog_near) : none},
        {"LightingFogFar", lighting ? nlohmann::json(lighting->fog_far) : none},
        {"LightingDirectionalRotationXY", lighting ? nlohmann::json(lighting->directional_rotation_xy) : none},
        {"LightingDirectionalRotationZ", lighting ? nlohmann::json(lighting->directional_rotation_z) : none},
        {"LightingDirectionalFade", lighting ? nlohmann::json(lighting->directional_fade) : none},
        {"LightingFogClipDistance", lighting ? nlohmann::json(lighting->fog_clip_distance) : none},
        {"LightingFogPower", lighting ? nlohmann::json(lighting->fog_power) : none},
        {"LightingTemplate", linkJson(data.lighting_template)},
        {"WaterHeight", data.water_height ? nlohmann::json(*data.water_height) : none},
        {"Water", linkJson(data.water)},
        {"ImageSpace", linkJson(data.image_space)},
        {"EncounterZone", linkJson(data.encounter_zone)},
        {"Location", linkJson(data.location)},
        {"AcousticSpace", linkJson(data.acoustic_space)},
        {"MusicType", linkJson(data.music_type)},
        {"SkyWeatherRegion", linkJson(data.sky_weather_region)},
        {"GodRays", linkJson(data.god_rays)},
        {"LockList", linkJson(data.lock_list)},
        {"Owner", linkJson(data.owner)},
        {"OwnerRank", data.owner_rank ? nlohmann::json(*data.owner_rank) : none},
        {"Regions", std::move(regions)},
    };
}

std::string kindName(fo4::forms::CellPropertyKind kind) {
    switch (kind) {
        case fo4::forms::CellPropertyKind::scalar:
            return "scalar";
        case fo4::forms::CellPropertyKind::link:
            return "link";
        case fo4::forms::CellPropertyKind::colour:
            return "colour";
        case fo4::forms::CellPropertyKind::flags:
            return "flags";
    }
    return "scalar";
}

nlohmann::json cellPropertyDescriptorsJson() {
    nlohmann::json descriptors = nlohmann::json::array();
    for (const auto& descriptor : fo4::forms::cell_property_descriptors()) {
        descriptors.push_back({
            {"name", descriptor.name},
            {"label", descriptor.label},
            {"kind", kindName(descriptor.kind)},
            {"minimum", descriptor.minimum},
            {"maximum", descriptor.maximum},
            {"multiple", descriptor.multiple},
            {"targets", descriptor.targets}
        });
    }
    return descriptors;
}

}  // namespace

Result<nlohmann::json> RadiclePluginBackend::getCellProperties(const nlohmann::json& arguments) const {
    const auto cellId = support::stringArgument(arguments, 0);
    if (cellId.empty()) {
        return std::unexpected(Error{"CellMissing", "Cell FormKey or EditorID is empty"});
    }

    OpLock lock(*this, OpLock::Mode::Read);
    try {
        const auto env = borrowedEnvironment();
        const auto key = fo4::env::resolve_cell_id(*env, cellId);
        const auto summary = key ? fo4::env::describe_cell(*env, *key) : std::nullopt;
        if (!summary) {
            return std::unexpected(Error{"CellNotFound", "Cell not found: " + cellId});
        }
        const auto* winner = env->winner(summary->key);
        if (winner == nullptr) {
            return std::unexpected(Error{"CellNotFound", "Cell has no winning record: " + cellId});
        }
        const auto data = fo4::env::decode_cell(*env, *winner);
        return nlohmann::json(nlohmann::json{
            {"cellFormKey", summary->key.to_string()},
            {"cellEditorId", summary->editor_id},
            {"values", cellPropertyValues(data)},
            {"descriptors", cellPropertyDescriptorsJson()}
        }.dump());
    } catch (const std::exception& error) {
        return std::unexpected(Error{"CellPropertiesReadFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::setCellProperties(const nlohmann::json& arguments) {
    const auto cellText = support::stringArgument(arguments, 0);
    const auto patchName = support::stringArgument(arguments, 1);
    const auto changes = arguments.is_array() && arguments.size() > 2 ? arguments[2] : nlohmann::json();
    if (cellText.empty() || patchName.empty() || !changes.is_object() || changes.empty()) {
        return std::unexpected(Error{
            "CellPropertiesArgumentsMissing",
            "SetCellProperties requires a cell FormKey, patch plugin and a nonempty changes object"
        });
    }

    fo4::FormKey key;
    try {
        key = fo4::FormKey::parse(cellText);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InvalidFormKey", error.what()});
    }
    fo4::forms::CellEdit edit;
    try {
        edit = cellEditFromChanges(changes);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"CellPropertyInvalid", error.what()});
    }

    OpLock lock(*this, OpLock::Mode::Write, {patchName});
    auto* patch = findPlugin(patchName);
    if (patch == nullptr) {
        return std::unexpected(Error{
            "TargetPluginNotLoaded",
            "Patch plugin is not loaded yet: " + patchName
        });
    }
    if (auto guard = writeGuard(*patch); guard.has_value()) return std::unexpected(*guard);

    try {
        const auto result = setCellPropertiesLocked(*patch, key, edit);
        if (!result) return std::unexpected(result.error());
        return nlohmann::json(nlohmann::json{
            {"plugin", patchName},
            {"formKey", key.to_string()},
            {"copiedFrom", result->copied_from}
        }.dump());
    } catch (const std::exception& error) {
        return std::unexpected(Error{"CellPropertyWriteFailed", error.what()});
    }
}

Result<fo4::forms::CellEditResult> RadiclePluginBackend::setCellPropertiesLocked(
    LoadedPlugin& patch, const fo4::FormKey& key, const fo4::forms::CellEdit& edit) {
    CommitScope commit;
    const auto env = borrowedEnvironment();
    auto mutated = patch.plugin;
    const auto result = fo4::forms::edit_cell(mutated, *env, key, edit);
    if (const auto error = cellEditError(result, patch.name, key.to_string())) return std::unexpected(*error);
    stageCellEditLocked(patch, std::move(mutated));
    return result;
}

}  // namespace fo4ide::plugin
