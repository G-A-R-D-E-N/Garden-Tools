#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include <cstdint>
#include <exception>
#include <string>
#include <utility>
#include <vector>

#include "RadiclePluginSupport.hpp"

#include <fo4/forms/furniture_markers.hpp>

namespace fo4ide::plugin {
namespace {

Result<std::vector<fo4::forms::FurnitureMarkerEdit>> furnitureMarkersFromJson(const nlohmann::json& argument) {
    nlohmann::json parsed = argument;
    if (parsed.is_null() || (parsed.is_string() && parsed.get<std::string>().find_first_not_of(" \t\r\n") == std::string::npos)) {
        parsed = nlohmann::json::parse("[{}]");
    } else if (parsed.is_string()) {
        parsed = nlohmann::json::parse(parsed.get<std::string>(), nullptr, false);
    }
    if (!parsed.is_array()) return std::unexpected(Error{"InvalidFurnitureMarkers", "markers must be a JSON array"});

    std::vector<fo4::forms::FurnitureMarkerEdit> markers;
    for (const auto& item : parsed) {
        if (!item.is_object()) return std::unexpected(Error{"InvalidFurnitureMarkers", "every marker must be an object"});
        fo4::forms::FurnitureMarkerEdit marker;
        if (item.contains("enabled")) {
            if (!item.at("enabled").is_boolean()) return std::unexpected(Error{"InvalidFurnitureMarkers", "enabled must be a boolean"});
            marker.enabled = item.at("enabled").get<bool>();
        }
        const std::pair<const char*, float*> floats[] = {{"offsetX", &marker.offset_x}, {"offsetY", &marker.offset_y},
                                                          {"offsetZ", &marker.offset_z}, {"rotationZ", &marker.rotation_z}};
        for (const auto& [name, target] : floats) {
            if (!item.contains(name)) continue;
            const auto value = support::finiteFloat(item.at(name));
            if (!value) return std::unexpected(Error{"InvalidFurnitureMarkers", std::string(name) + " must be a finite number"});
            *target = *value;
        }
        if (item.contains("entryTypes")) {
            const auto value = support::integerField<std::uint8_t>(item, "entryTypes");
            if (!value) return std::unexpected(Error{"InvalidFurnitureMarkers", "entryTypes must be an integer from 0 to 255"});
            marker.entry_types = *value;
        }
        markers.push_back(marker);
    }
    return markers;
}

Error furnitureWriteError(fo4::forms::FurnitureMarkersWrite status, const std::string& key) {
    using Write = fo4::forms::FurnitureMarkersWrite;
    switch (status) {
        case Write::written: break;
        case Write::record_missing: return Error{"RecordNotFound", "Record is not present: " + key};
        case Write::unsupported_record: return Error{"WrongRecordType", "set_furniture_markers requires a FURN record: " + key};
        case Write::too_many_markers: return Error{"InvalidFurnitureMarkers", "A FURN record holds at most 22 markers"};
        case Write::unexpected_layout:
            return Error{"FurnitureMarkersLayout", "The marker or interaction subrecords of " + key + " are not in the expected layout"};
        case Write::no_insertion_point:
            return Error{"ElementUnknownOrder", "A valid insertion order cannot be established for " + key};
    }
    return Error{"FurnitureMarkersWriteFailed", "Furniture markers could not be written to " + key};
}

}  // namespace

Result<nlohmann::json> RadiclePluginBackend::setFurnitureMarkers(const nlohmann::json& arguments) {
    const auto pluginName = support::stringArgument(arguments, 0);
    const auto key = support::formKeyArgument(support::stringArgument(arguments, 1));
    if (!key) return std::unexpected(key.error());
    const auto markers = furnitureMarkersFromJson(support::argumentAt(arguments, 2));
    if (!markers) return std::unexpected(markers.error());
    if (pluginName.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    if (const auto guard = writeGuard(*loaded)) return std::unexpected(*guard);

    auto mutated = loaded->plugin;
    try {
        const auto status = fo4::forms::set_furniture_markers(mutated, *key, *markers);
        if (status != fo4::forms::FurnitureMarkersWrite::written) {
            return std::unexpected(furnitureWriteError(status, key->to_string()));
        }
    } catch (const std::exception& error) {
        return std::unexpected(Error{"FurnitureMarkersWriteFailed", error.what()});
    }
    stagePluginMutationLocked(*loaded, std::move(mutated));
    return nlohmann::json("Set " + std::to_string(markers->size()) + " furniture marker(s) on " + key->to_string() +
                          " in " + pluginName + ". Call save_plugin to persist.");
}

}  // namespace fo4ide::plugin
