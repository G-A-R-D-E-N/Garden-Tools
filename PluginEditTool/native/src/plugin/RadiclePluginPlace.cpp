#include "RadiclePluginSupport.hpp"

#include <fo4ide/plugin/BaseObjectTypes.hpp>

#include <cstddef>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <fo4/environment/environment.hpp>
#include <fo4/forms/cell_edit.hpp>
#include <fo4/forms/placed_reference.hpp>
#include <fo4/plugin/reader.hpp>

namespace fo4ide::plugin {

namespace {

constexpr std::size_t k_max_placements = 200;

struct PlacementRequest {
    fo4::FormKey base;
    fo4::forms::PositionRotation transform{};
    std::optional<float> scale;
    std::optional<std::string> editorId;
};

std::optional<float> readNumber(const nlohmann::json& object, const char* key) {
    if (!object.is_object() || !object.contains(key) || !object.at(key).is_number()) return std::nullopt;
    return object.at(key).get<float>();
}

std::optional<fo4::forms::PositionRotation> readTransform(const nlohmann::json& value) {
    const auto x = readNumber(value, "x");
    const auto y = readNumber(value, "y");
    const auto z = readNumber(value, "z");
    const auto rx = readNumber(value, "rx");
    const auto ry = readNumber(value, "ry");
    const auto rz = readNumber(value, "rz");
    if (!x || !y || !z || !rx || !ry || !rz) return std::nullopt;
    return fo4::forms::PositionRotation{.x = *x, .y = *y, .z = *z, .rx = *rx, .ry = *ry, .rz = *rz};
}

Result<PlacementRequest> readRequest(const nlohmann::json& item) {
    if (!item.is_object() || !item.contains("baseFormKey") || !item.at("baseFormKey").is_string()) {
        return std::unexpected(Error{"PlacedObjectArgumentsMissing", "Each placement requires baseFormKey"});
    }
    PlacementRequest request;
    try {
        request.base = fo4::FormKey::parse(item.at("baseFormKey").get<std::string>());
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InvalidFormKey", error.what()});
    }
    const auto transform = item.contains("transform") ? readTransform(item.at("transform")) : std::nullopt;
    if (!transform) {
        return std::unexpected(Error{"PlacedTransformArgumentsMissing",
                                     "Each placement requires numeric x, y, z, rx, ry and rz"});
    }
    request.transform = *transform;
    if (item.contains("scale") && item.at("scale").is_number()) request.scale = item.at("scale").get<float>();
    if (item.contains("editorId") && item.at("editorId").is_string()) {
        request.editorId = item.at("editorId").get<std::string>();
    }
    return request;
}

}  // namespace

Result<nlohmann::json> RadiclePluginBackend::placeReferences(const nlohmann::json& arguments) {
    const auto patchName = support::stringArgument(arguments, 0);
    const auto cellText = support::stringArgument(arguments, 1);
    const auto placements = arguments.is_array() && arguments.size() > 2 ? arguments[2] : nlohmann::json();
    const auto options = arguments.is_array() && arguments.size() > 3 ? arguments[3] : nlohmann::json();
    if (patchName.empty() || cellText.empty()) {
        return std::unexpected(Error{"PlacedObjectArgumentsMissing",
                                     "PlaceReferences requires patch plugin, cell FormKey and placements"});
    }

    nlohmann::json items = placements;
    if (placements.is_string()) {
        try {
            items = nlohmann::json::parse(placements.get<std::string>());
        } catch (const std::exception&) {
            return std::unexpected(Error{"PlacedObjectArgumentsMissing", "placements_json is not valid JSON"});
        }
    }
    if (!items.is_array() || items.empty() || items.size() > k_max_placements) {
        return std::unexpected(Error{"PlacedObjectArgumentsMissing",
                                     "placements must be a non-empty array of at most 200 items"});
    }

    bool persistent = false;
    bool initiallyDisabled = false;
    if (options.is_object()) {
        if (options.contains("persistent") && options.at("persistent").is_boolean()) {
            persistent = options.at("persistent").get<bool>();
        }
        if (options.contains("initiallyDisabled") && options.at("initiallyDisabled").is_boolean()) {
            initiallyDisabled = options.at("initiallyDisabled").get<bool>();
        }
    }

    std::vector<PlacementRequest> requests;
    requests.reserve(items.size());
    for (const auto& item : items) {
        auto request = readRequest(item);
        if (!request) return std::unexpected(request.error());
        requests.push_back(std::move(*request));
    }

    fo4::FormKey cell;
    try {
        cell = fo4::FormKey::parse(cellText);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InvalidFormKey", error.what()});
    }

    std::vector<fo4::FormKey> created;
    {
        OpLock lock(*this, OpLock::Mode::Write, {patchName});
        auto* patch = findPlugin(patchName);
        if (patch == nullptr) {
            return std::unexpected(Error{"TargetPluginNotLoaded", "Patch plugin is not loaded yet: " + patchName});
        }
        if (auto guard = writeGuard(*patch); guard.has_value()) return std::unexpected(*guard);

        try {
            CommitScope commit;
            const auto env = borrowedEnvironment();
            const auto* cellWinner = env->winner(cell);
            if (cellWinner == nullptr || cellWinner->record->header.signature != fo4::Signature::from_string("CELL") ||
                cellWinner->record->header.flags.is_deleted()) {
                return std::unexpected(Error{"PlacedCellNotFound", "Cell not found: " + cellText});
            }

            std::vector<fo4::forms::PlacedReferenceKind> kinds;
            kinds.reserve(requests.size());
            for (const auto& request : requests) {
                const auto* base = env->winner(request.base);
                if (base == nullptr) {
                    return std::unexpected(Error{"PlacedBaseMissing", "Base object not found: " + request.base.to_string()});
                }
                if (base->record->header.flags.is_deleted()) {
                    return std::unexpected(Error{"PlacedBaseDeleted", "Base object is deleted: " + request.base.to_string()});
                }
                const auto signature = base->record->header.signature.to_string();
                if (!placeableBaseTypeName(signature)) {
                    return std::unexpected(Error{"PlacedBaseNotPlaceable",
                                                 request.base.to_string() + " (" + signature + ") cannot be placed"});
                }
                kinds.push_back(signature == "NPC_" ? fo4::forms::PlacedReferenceKind::npc
                                                    : fo4::forms::PlacedReferenceKind::object);
            }

            auto mutated = patch->plugin;
            if (fo4::find_record(mutated, cell) == nullptr) {
                const auto copied = fo4::forms::copy_cell_as_override(mutated, env->plugin(cellWinner->plugin), cell);
                if (copied != fo4::forms::CellOverrideStatus::inserted) {
                    return std::unexpected(Error{"PlacedCellNotFound", "Cell could not be overridden: " + cellText});
                }
            }

            for (std::size_t index = 0; index < requests.size(); ++index) {
                const auto& request = requests[index];
                ensureOwnerMaster(mutated, request.base.owner());
                fo4::forms::PlacedReferenceCreation creation;
                creation.cell = cell;
                creation.base = request.base;
                creation.transform = request.transform;
                creation.persistent = persistent;
                creation.initially_disabled = initiallyDisabled;
                creation.kind = kinds[index];
                creation.cell_is_override = true;
                const auto key = fo4::forms::create_placed_reference(mutated, creation);
                if (request.scale && !fo4::forms::set_placed_reference_scale(mutated, key, request.scale)) {
                    throw std::runtime_error("placed reference scale could not be written");
                }
                if (request.editorId && !request.editorId->empty() &&
                    !fo4::forms::set_placed_reference_editor_id(mutated, key, *request.editorId)) {
                    throw std::runtime_error("placed reference editor ID could not be written");
                }
                created.push_back(key);
            }
            stageCellEditLocked(*patch, std::move(mutated));
        } catch (const std::exception& error) {
            return std::unexpected(Error{"PlacedReferenceCreateFailed", error.what()});
        }
    }

    nlohmann::json createdKeys = nlohmann::json::array();
    std::set<std::string> wanted;
    for (const auto& key : created) {
        createdKeys.push_back(key.to_string());
        wanted.insert(key.to_string());
    }
    const auto listed = getPlacedReferences(nlohmann::json::array({cell.to_string()}));
    if (!listed) return std::unexpected(listed.error());
    try {
        const auto document = nlohmann::json::parse(listed->get<std::string>());
        nlohmann::json references = nlohmann::json::array();
        for (const auto& reference : document.at("references")) {
            if (wanted.contains(reference.at("formKey").get<std::string>())) references.push_back(reference);
        }
        return nlohmann::json(nlohmann::json{
            {"plugin", patchName},
            {"cellFormKey", cell.to_string()},
            {"created", std::move(createdKeys)},
            {"references", std::move(references)},
        }.dump());
    } catch (const std::exception& error) {
        return std::unexpected(Error{"PlacedReferenceCreateFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::placeReference(const nlohmann::json& arguments) {
    if (!arguments.is_array() || arguments.size() < 4) {
        return std::unexpected(Error{"PlacedObjectArgumentsMissing",
                                     "PlaceReference requires patch plugin, cell, base and transform"});
    }
    nlohmann::json item{{"baseFormKey", arguments[2]}, {"transform", arguments[3]}};
    const auto options = arguments.size() > 4 ? arguments[4] : nlohmann::json();
    if (options.is_object()) {
        if (options.contains("scale")) item["scale"] = options.at("scale");
        if (options.contains("editorId")) item["editorId"] = options.at("editorId");
    }
    return placeReferences(nlohmann::json::array({arguments[0], arguments[1],
                                                  nlohmann::json::array({std::move(item)}), options}));
}

}  // namespace fo4ide::plugin
