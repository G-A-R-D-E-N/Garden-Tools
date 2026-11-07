#include "RadiclePluginSupport.hpp"

#include <fo4ide/core/JsonInt.hpp>
#include <fo4ide/plugin/RadicleLightJson.hpp>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <fo4/environment/base_records.hpp>
#include <fo4/environment/cell_view.hpp>
#include <fo4/environment/world.hpp>
#include <fo4/forms/cell.hpp>
#include <fo4/forms/cell_edit.hpp>
#include <fo4/forms/placed_reference.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/plugin/writer.hpp>

namespace fo4ide::plugin {

namespace {

std::optional<int> intArgument(const nlohmann::json& args, std::size_t index) {
    if (!args.is_array() || index >= args.size()) return std::nullopt;
    return core::jsonInt(args[index]);
}

std::optional<float> floatArgument(const nlohmann::json& args, std::size_t index) {
    if (!args.is_array() || index >= args.size() || !args[index].is_number()) return std::nullopt;
    return args[index].get<float>();
}

std::optional<float> numberField(const nlohmann::json& object, const char* key) {
    if (!object.is_object() || !object.contains(key) || !object.at(key).is_number()) return std::nullopt;
    return object.at(key).get<float>();
}

std::optional<fo4::forms::PositionRotation> transformField(const nlohmann::json& value) {
    const auto x = numberField(value, "x");
    const auto y = numberField(value, "y");
    const auto z = numberField(value, "z");
    const auto rx = numberField(value, "rx");
    const auto ry = numberField(value, "ry");
    const auto rz = numberField(value, "rz");
    if (!x || !y || !z || !rx || !ry || !rz) return std::nullopt;
    return fo4::forms::PositionRotation{.x = *x, .y = *y, .z = *z, .rx = *rx, .ry = *ry, .rz = *rz};
}

std::optional<Error> placedReferenceEditError(const fo4::forms::PlacedReferenceEditResult& result,
                                              std::string_view patchName,
                                              const std::string& formKeyText) {
    using Status = fo4::forms::PlacedReferenceEditStatus;
    switch (result.status) {
        case Status::edited:
            return std::nullopt;
        case Status::reference_missing:
            if (!result.message.empty()) return Error{"PlacedReferenceNotFound", result.message};
            return Error{"PlacedReferenceNotFound",
                         "Placed reference is not present in " + std::string(patchName) + ": " + formKeyText};
        case Status::reference_deleted:
            return Error{"PlacedReferenceDeleted", result.message};
        case Status::target_cell_missing:
            return Error{"PlacedReferenceTargetCellMissing",
                         "No loaded exterior cell covers the new position of " + formKeyText};
        case Status::field_absent:
            return Error{"PlacedReferenceFieldAbsent",
                         "Placed reference " + formKeyText + " has no record for this edit"};
    }
    return Error{"PlacedReferenceEditFailed", result.message};
}

std::optional<Error> duplicateError(const fo4::forms::DuplicateResult& result,
                                    std::string_view patchName,
                                    const std::string& formKeyText) {
    using Status = fo4::forms::DuplicateStatus;
    switch (result.status) {
        case Status::duplicated:
            return std::nullopt;
        case Status::reference_missing:
            return Error{"PlacedReferenceNotFound",
                         "Placed reference is not present in " + std::string(patchName) + ": " + formKeyText};
        case Status::reference_deleted:
            return Error{"PlacedReferenceDeleted", "Placed reference is deleted: " + formKeyText};
        case Status::parent_cell_missing:
            return Error{"PlacedReferenceParentCellMissing", "No loaded cell holds the parent of " + formKeyText};
        case Status::unsupported_signature:
            return Error{"PlacedReferenceUnsupported", "Placed reference " + formKeyText + " cannot be duplicated"};
    }
    return Error{"PlacedReferenceDuplicateFailed", formKeyText};
}

std::optional<Error> deleteError(const fo4::forms::DeleteResult& result,
                                 std::string_view patchName,
                                 const std::string& formKeyText) {
    using Status = fo4::forms::DeleteStatus;
    switch (result.status) {
        case Status::deleted:
        case Status::marked_deleted:
            return std::nullopt;
        case Status::referenced:
            return Error{"PlacedReferenceReferenced",
                         formKeyText + " is referenced by other records and cannot be deleted"};
        case Status::reference_missing:
            return Error{"PlacedReferenceNotFound",
                         "Placed reference is not present in " + std::string(patchName) + ": " + formKeyText};
        case Status::already_deleted:
            return Error{"PlacedReferenceDeleted", "Placed reference is already deleted: " + formKeyText};
    }
    return Error{"PlacedReferenceDeleteFailed", formKeyText};
}

nlohmann::json cellEditHistoryJson(std::string_view pluginName, std::size_t undo, std::size_t redo) {
    return {{"plugin", std::string(pluginName)}, {"undoCount", undo}, {"redoCount", redo}};
}

nlohmann::json scolPartsJson(const fo4::env::BaseObjectInfo& base) {
    nlohmann::json parts = nlohmann::json::array();
    for (const auto& part : base.scol_parts) {
        nlohmann::json placements = nlohmann::json::array();
        for (const auto& placement : part.placements) {
            placements.push_back({
                {"x", placement.x}, {"y", placement.y}, {"z", placement.z},
                {"rx", placement.rx}, {"ry", placement.ry}, {"rz", placement.rz},
                {"scale", placement.scale}
            });
        }
        parts.push_back({
            {"modelPath", part.model_path},
            {"placements", std::move(placements)}
        });
    }
    return parts.empty() ? nlohmann::json(nullptr) : std::move(parts);
}

nlohmann::json lightOfBase(const fo4::env::Environment& env, const fo4::FormKey& baseKey) {
    const auto* record = env.winner(baseKey);
    if (record == nullptr) return nullptr;
    try {
        return lightJson(fo4::env::decode_light(env, *record));
    } catch (const std::exception&) {
        return nullptr;
    }
}

nlohmann::json cellLightingOf(const fo4::env::Environment& env, const fo4::FormKey& cellKey) {
    const auto* cellRecord = env.winner(cellKey);
    if (cellRecord == nullptr) return nullptr;
    try {
        const auto cellData = fo4::env::decode_cell(env, *cellRecord);
        std::optional<fo4::env::CellLighting> templateLighting;
        if (cellData.lighting_template) {
            if (const auto* templateRecord = env.winner(*cellData.lighting_template)) {
                templateLighting = fo4::env::decode_lighting_template(env, *templateRecord).lighting;
            }
        }
        return cellLightingJson(cellData.lighting, templateLighting);
    } catch (const std::exception&) {
        return nullptr;
    }
}

nlohmann::json placedReferencesJson(const fo4::env::Environment& env, const fo4::env::CellReferences& cells) {
    nlohmann::json references = nlohmann::json::array();
    std::map<fo4::FormKey, nlohmann::json> lightByBase;
    for (const auto& ref : cells.references) {
        const auto* base = ref.base_info ? &*ref.base_info : nullptr;
        nlohmann::json light = nullptr;
        if (base != nullptr && ref.base && base->signature.to_string() == "LIGH") {
            auto cached = lightByBase.find(*ref.base);
            if (cached == lightByBase.end()) {
                cached = lightByBase.emplace(*ref.base, lightOfBase(env, *ref.base)).first;
            }
            light = cached->second;
        }
        nlohmann::json decalDiffuse = nullptr;
        nlohmann::json decalWidth = nullptr;
        nlohmann::json decalHeight = nullptr;
        nlohmann::json scolParts = nullptr;
        if (base != nullptr && base->decal) {
            decalDiffuse = base->decal->diffuse;
            if (base->decal->width) decalWidth = *base->decal->width;
            if (base->decal->height) decalHeight = *base->decal->height;
        }
        if (base != nullptr) scolParts = scolPartsJson(*base);

        references.push_back({
            {"formKey", ref.key.to_string()},
            {"editorId", ref.editor_id},
            {"recordType", ref.record_type},
            {"baseType", base != nullptr ? base->type_name : std::string{}},
            {"baseFormKey", ref.base ? ref.base->to_string() : std::string{}},
            {"baseEditorId", base != nullptr ? base->editor_id : std::string{}},
            {"modelPath", base != nullptr ? base->model_path : std::string{}},
            {"decalDiffuse", std::move(decalDiffuse)},
            {"decalWidth", std::move(decalWidth)},
            {"decalHeight", std::move(decalHeight)},
            {"scolParts", std::move(scolParts)},
            {"position", {
                {"x", ref.transform.x}, {"y", ref.transform.y}, {"z", ref.transform.z}}},
            {"rotation", {
                {"x", ref.transform.rx}, {"y", ref.transform.ry}, {"z", ref.transform.rz}}},
            {"scale", ref.scale},
            {"persistent", ref.persistent},
            {"initiallyDisabled", ref.initially_disabled},
            {"isMarker", base != nullptr && base->editor_marker},
            {"light", std::move(light)}
        });
    }

    const auto& cell = cells.cell;
    const auto cellName = cell.localized || cell.name.kind != fo4::env::NameKind::inline_text
        ? std::string{}
        : std::string(cell.name.text);
    return {
        {"cellFormKey", cell.key.to_string()},
        {"cellEditorId", cell.editor_id},
        {"cellName", cellName},
        {"interior", cell.interior},
        {"referenceCount", references.size()},
        {"withModelCount", cells.with_model_count},
        {"lighting", cellLightingOf(env, cell.key)},
        {"references", std::move(references)}
    };
}

}

std::shared_ptr<const fo4::env::Environment> RadiclePluginBackend::borrowedEnvironment() const {
    std::scoped_lock lock(environmentMutex_);
    if (borrowedEnvironment_ && borrowedEnvironmentEpoch_ == mutationEpoch_) return borrowedEnvironment_;
    borrowedEnvironment_.reset();
    std::vector<const fo4::Plugin*> loadOrder;
    loadOrder.reserve(plugins_.size());
    for (const auto& loaded : plugins_) loadOrder.push_back(&loaded.plugin);
    borrowedEnvironment_ = std::make_shared<const fo4::env::Environment>(
        fo4::env::Environment::borrow(loadOrder));
    borrowedEnvironmentEpoch_ = mutationEpoch_;
    return borrowedEnvironment_;
}

void RadiclePluginBackend::dropBorrowedEnvironmentOnlyLocked() {
    std::scoped_lock lock(environmentMutex_);
    ++mutationEpoch_;
    borrowedEnvironment_.reset();
}

void RadiclePluginBackend::dropBorrowedEnvironmentLocked() {
    dropBorrowedEnvironmentOnlyLocked();
    cellEditHistories_.clear();
}

Result<nlohmann::json> RadiclePluginBackend::getPlacedReferences(
    const nlohmann::json& arguments) const {
    const auto cellId = support::stringArgument(arguments, 0);
    if (cellId.empty()) {
        return std::unexpected(Error{"CellMissing", "Cell FormKey or EditorID is empty"});
    }

    OpLock lock(*this, OpLock::Mode::Read);
    try {
        const auto env = borrowedEnvironment();
        const auto key = fo4::env::resolve_cell_id(*env, cellId);
        if (!key.has_value()) {
            return nlohmann::json(nlohmann::json{{"error", "Cell not found: " + cellId}}.dump());
        }
        return nlohmann::json(placedReferencesJson(*env, fo4::env::list_cell_references(*env, *key)).dump());
    } catch (const std::exception& error) {
        return std::unexpected(Error{"CellReadFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::resolveWorldspace(
    const nlohmann::json& arguments) const {
    const auto cellId = support::stringArgument(arguments, 0);
    if (cellId.empty()) {
        return std::unexpected(Error{"CellMissing", "Cell FormKey or EditorID is empty"});
    }

    OpLock lock(*this, OpLock::Mode::Read);
    try {
        const auto env = borrowedEnvironment();
        const auto key = fo4::env::resolve_cell_id(*env, cellId);
        if (!key.has_value()) {
            return nlohmann::json(nlohmann::json{{"error", "Cell not found: " + cellId}}.dump());
        }
        const auto summary = fo4::env::describe_cell(*env, *key);
        nlohmann::json worldspace = nullptr;
        if (summary && summary->worldspace) worldspace = summary->worldspace->to_string();
        return nlohmann::json(nlohmann::json{
            {"cellFormKey", key->to_string()},
            {"worldspaceFormKey", worldspace}
        }.dump());
    } catch (const std::exception& error) {
        return std::unexpected(Error{"CellReadFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::resolveWorldspaceId(
    const nlohmann::json& arguments) const {
    const auto worldId = support::stringArgument(arguments, 0);
    if (worldId.empty()) {
        return std::unexpected(Error{"WorldspaceMissing", "Worldspace FormKey or EditorID is empty"});
    }

    OpLock lock(*this, OpLock::Mode::Read);
    try {
        const auto env = borrowedEnvironment();
        const auto worldKey = fo4::env::resolve_worldspace_id(*env, worldId);
        if (!worldKey.has_value()) {
            return nlohmann::json(nlohmann::json{{"error", "Worldspace not found: " + worldId}}.dump());
        }
        return nlohmann::json(nlohmann::json{{"worldspaceFormKey", worldKey->to_string()}}.dump());
    } catch (const std::exception& error) {
        return std::unexpected(Error{"CellReadFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::getPlacedReferencesAtGrid(
    const nlohmann::json& arguments) const {
    const auto worldId = support::stringArgument(arguments, 0);
    const auto x = intArgument(arguments, 1);
    const auto y = intArgument(arguments, 2);
    if (worldId.empty() || !x.has_value() || !y.has_value()) {
        return std::unexpected(Error{
            "CellGridArgumentsMissing",
            "GetPlacedReferencesAtGrid requires a worldspace and 32-bit integer grid X and Y"
        });
    }

    OpLock lock(*this, OpLock::Mode::Read);
    try {
        const auto env = borrowedEnvironment();
        const auto worldKey = fo4::env::resolve_worldspace_id(*env, worldId);
        if (!worldKey.has_value()) {
            return nlohmann::json(nlohmann::json{{"error", "Worldspace not found: " + worldId}}.dump());
        }
        const auto cellKey = fo4::env::find_cell_at_grid(*env, *worldKey, *x, *y);
        if (!cellKey.has_value()) {
            return nlohmann::json(nlohmann::json{
                {"error", "Cell not found at grid " + std::to_string(*x) + "," + std::to_string(*y)}
            }.dump());
        }
        return nlohmann::json(placedReferencesJson(*env, fo4::env::list_cell_references(*env, *cellKey)).dump());
    } catch (const std::exception& error) {
        return std::unexpected(Error{"CellReadFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::setPlacedReferenceTransform(
    const nlohmann::json& arguments) {
    const auto formKeyText = support::stringArgument(arguments, 0);
    const auto patchName = support::stringArgument(arguments, 1);
    const auto x = floatArgument(arguments, 2);
    const auto y = floatArgument(arguments, 3);
    const auto z = floatArgument(arguments, 4);
    const auto rx = floatArgument(arguments, 5);
    const auto ry = floatArgument(arguments, 6);
    const auto rz = floatArgument(arguments, 7);
    if (formKeyText.empty() || patchName.empty() || !x || !y || !z || !rx || !ry || !rz) {
        return std::unexpected(Error{
            "PlacedTransformArgumentsMissing",
            "SetPlacedReferenceTransform requires FormKey, patch plugin and six numeric transform values"
        });
    }

    fo4::FormKey key;
    try {
        key = fo4::FormKey::parse(formKeyText);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InvalidFormKey", error.what()});
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
        fo4::forms::PlacedReferenceEdit edit;
        edit.transform = fo4::forms::PositionRotation{.x = *x, .y = *y, .z = *z, .rx = *rx, .ry = *ry, .rz = *rz};
        const auto result = editPlacedReferenceLocked(*patch, key, edit);
        if (!result) return std::unexpected(result.error());

        auto message = "Moved " + formKeyText + " in " + patchName + ".";
        if (!result->copied_from.empty()) {
            message += " Created the nested override from " + result->copied_from + ".";
        }
        return nlohmann::json(std::move(message));
    } catch (const std::exception& error) {
        return std::unexpected(Error{"PlacedTransformWriteFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::editPlacedReference(const nlohmann::json& arguments) {
    const auto formKeyText = support::stringArgument(arguments, 0);
    const auto patchName = support::stringArgument(arguments, 1);
    const auto changes = arguments.is_array() && arguments.size() > 2 ? arguments[2] : nlohmann::json();
    if (formKeyText.empty() || patchName.empty() || !changes.is_object()) {
        return std::unexpected(Error{
            "PlacedEditArgumentsMissing",
            "EditPlacedReference requires FormKey, patch plugin and an edit object"
        });
    }

    fo4::FormKey key;
    try {
        key = fo4::FormKey::parse(formKeyText);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InvalidFormKey", error.what()});
    }

    fo4::forms::PlacedReferenceEdit edit;
    if (changes.contains("transform")) {
        edit.transform = transformField(changes.at("transform"));
        if (!edit.transform) {
            return std::unexpected(Error{
                "PlacedTransformArgumentsMissing",
                "EditPlacedReference transform requires numeric x, y, z, rx, ry and rz"
            });
        }
    }
    if (changes.contains("scale")) {
        const auto& scale = changes.at("scale");
        if (!scale.is_null() && !scale.is_number()) {
            return std::unexpected(Error{"PlacedEditArgumentsMissing", "EditPlacedReference scale must be a number or null"});
        }
        edit.scale.emplace();
        if (scale.is_number()) edit.scale->emplace(scale.get<float>());
    }
    if (changes.contains("editorId")) {
        const auto& editorId = changes.at("editorId");
        if (!editorId.is_string() && !editorId.is_null()) {
            return std::unexpected(Error{"PlacedEditArgumentsMissing", "EditPlacedReference editorId must be a string or null"});
        }
        edit.editor_id = editorId.is_string() ? editorId.get<std::string>() : std::string{};
    }
    if (changes.contains("enableParent")) {
        const auto& enableParent = changes.at("enableParent");
        edit.enable_parent.emplace();
        if (!enableParent.is_null()) {
            if (!enableParent.is_object() || !enableParent.contains("parent") || !enableParent.at("parent").is_string()) {
                return std::unexpected(Error{
                    "PlacedEditArgumentsMissing",
                    "EditPlacedReference enableParent must be null or {parent: FormKey, flags: 0-255}"
                });
            }
            const auto flags = enableParent.contains("flags") ? core::jsonInt(enableParent.at("flags")) : std::optional<int>{0};
            if (!flags || *flags < 0 || *flags > 255) {
                return std::unexpected(Error{"PlacedEditArgumentsMissing", "EditPlacedReference enableParent flags must be 0-255"});
            }
            try {
                edit.enable_parent->parent = fo4::FormKey::parse(enableParent.at("parent").get<std::string>());
            } catch (const std::exception& error) {
                return std::unexpected(Error{"InvalidFormKey", error.what()});
            }
            edit.enable_parent->flags = static_cast<std::uint8_t>(*flags);
        }
    }
    if (!edit.transform && !edit.scale && !edit.editor_id && !edit.enable_parent) {
        return std::unexpected(Error{
            "PlacedEditArgumentsMissing",
            "EditPlacedReference supports transform, scale, editorId and enableParent"
        });
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
        const auto result = editPlacedReferenceLocked(*patch, key, edit);
        if (!result) return std::unexpected(result.error());
        return nlohmann::json(nlohmann::json{
            {"plugin", patchName},
            {"formKey", key.to_string()},
            {"copiedFrom", result->copied_from},
            {"relocated", result->relocated}
        }.dump());
    } catch (const std::exception& error) {
        return std::unexpected(Error{"PlacedTransformWriteFailed", error.what()});
    }
}

Result<fo4::forms::PlacedReferenceEditResult> RadiclePluginBackend::editPlacedReferenceLocked(
    LoadedPlugin& patch, const fo4::FormKey& key, const fo4::forms::PlacedReferenceEdit& edit) {
    CommitScope commit;
    const auto env = borrowedEnvironment();
    auto mutated = patch.plugin;
    fo4::forms::PlacedReferenceEditResult result;
    try {
        result = fo4::forms::edit_placed_reference(mutated, *env, key, edit);
    } catch (const fo4::forms::PlacedReferenceError& error) {
        return std::unexpected(Error{"PlacedEditInvalid", error.what()});
    }
    if (const auto error = placedReferenceEditError(result, patch.name, key.to_string())) {
        return std::unexpected(*error);
    }
    stageCellEditLocked(patch, std::move(mutated));
    return result;
}

void RadiclePluginBackend::stageCellEditLocked(LoadedPlugin& patch, fo4::Plugin mutated) {
    CommitScope commit;
    fo4::PluginEditHistory history = std::move(cellEditHistories_[patch.name]);
    history.record(patch.plugin);
    stagePluginMutationLocked(patch, std::move(mutated));
    cellEditHistories_[patch.name] = std::move(history);
}

Result<nlohmann::json> RadiclePluginBackend::undoCellEdit(const nlohmann::json& arguments) {
    return stepCellEditHistory(arguments, true);
}

Result<nlohmann::json> RadiclePluginBackend::redoCellEdit(const nlohmann::json& arguments) {
    return stepCellEditHistory(arguments, false);
}

Result<nlohmann::json> RadiclePluginBackend::stepCellEditHistory(const nlohmann::json& arguments, bool undo) {
    const auto pluginName = support::stringArgument(arguments, 0);
    if (pluginName.empty()) {
        return std::unexpected(Error{"CellEditPluginMissing", "Cell edit history requires a patch plugin"});
    }

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* patch = findPlugin(pluginName);
    if (patch == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }
    if (auto guard = writeGuard(*patch); guard.has_value()) return std::unexpected(*guard);

    try {
        CommitScope commit;
        const auto found = cellEditHistories_.find(patch->name);
        if (found == cellEditHistories_.end()) {
            return std::unexpected(Error{undo ? "NothingToUndo" : "NothingToRedo", "No cell edit to step in " + pluginName});
        }
        auto restored = undo ? found->second.undo(patch->plugin) : found->second.redo(patch->plugin);
        if (!restored.has_value()) {
            return std::unexpected(Error{undo ? "NothingToUndo" : "NothingToRedo", "No cell edit to step in " + pluginName});
        }
        fo4::PluginEditHistory history = std::move(found->second);
        stagePluginMutationLocked(*patch, std::move(*restored));
        auto response = nlohmann::json(cellEditHistoryJson(patch->name, history.undo_count(),
                                                           history.redo_count()).dump());
        cellEditHistories_[patch->name] = std::move(history);
        return response;
    } catch (const std::exception& error) {
        return std::unexpected(Error{"CellEditHistoryFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::getCellEditHistory(const nlohmann::json& arguments) const {
    const auto pluginName = support::stringArgument(arguments, 0);
    if (pluginName.empty()) {
        return std::unexpected(Error{"CellEditPluginMissing", "Cell edit history requires a patch plugin"});
    }

    OpLock lock(*this, OpLock::Mode::Read);
    const auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }
    const auto history = cellEditHistories_.find(loaded->name);
    if (history == cellEditHistories_.end()) {
        return nlohmann::json(cellEditHistoryJson(loaded->name, 0, 0).dump());
    }
    return nlohmann::json(cellEditHistoryJson(loaded->name, history->second.undo_count(),
                                              history->second.redo_count()).dump());
}

Result<nlohmann::json> RadiclePluginBackend::duplicatePlacedReference(const nlohmann::json& arguments) {
    const auto formKeyText = support::stringArgument(arguments, 0);
    const auto patchName = support::stringArgument(arguments, 1);
    const auto offset = arguments.is_array() && arguments.size() > 2 ? arguments[2] : nlohmann::json();
    if (formKeyText.empty() || patchName.empty()) {
        return std::unexpected(Error{
            "PlacedDuplicateArgumentsMissing",
            "DuplicatePlacedReference requires FormKey and patch plugin"
        });
    }

    fo4::forms::DuplicateOptions options;
    if (!offset.is_null()) {
        const auto transform = transformField(offset);
        if (!transform) {
            return std::unexpected(Error{
                "PlacedTransformArgumentsMissing",
                "DuplicatePlacedReference offset requires numeric x, y, z, rx, ry and rz"
            });
        }
        options.offset = *transform;
    }

    fo4::FormKey key;
    try {
        key = fo4::FormKey::parse(formKeyText);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InvalidFormKey", error.what()});
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
        const auto result = duplicatePlacedReferenceLocked(*patch, key, options);
        if (!result) return std::unexpected(result.error());
        return nlohmann::json(nlohmann::json{
            {"plugin", patchName},
            {"formKey", key.to_string()},
            {"newFormKey", result->new_reference.to_string()}
        }.dump());
    } catch (const std::exception& error) {
        return std::unexpected(Error{"PlacedReferenceDuplicateFailed", error.what()});
    }
}

Result<fo4::forms::DuplicateResult> RadiclePluginBackend::duplicatePlacedReferenceLocked(
    LoadedPlugin& patch, const fo4::FormKey& key, const fo4::forms::DuplicateOptions& options) {
    CommitScope commit;
    const auto env = borrowedEnvironment();
    auto mutated = patch.plugin;
    const auto result = fo4::forms::duplicate_placed_reference(mutated, *env, key, options);
    if (const auto error = duplicateError(result, patch.name, key.to_string())) return std::unexpected(*error);
    stageCellEditLocked(patch, std::move(mutated));
    return result;
}

Result<nlohmann::json> RadiclePluginBackend::deletePlacedReference(const nlohmann::json& arguments) {
    const auto formKeyText = support::stringArgument(arguments, 0);
    const auto patchName = support::stringArgument(arguments, 1);
    if (formKeyText.empty() || patchName.empty()) {
        return std::unexpected(Error{
            "PlacedDeleteArgumentsMissing",
            "DeletePlacedReference requires FormKey and patch plugin"
        });
    }

    fo4::FormKey key;
    try {
        key = fo4::FormKey::parse(formKeyText);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InvalidFormKey", error.what()});
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
        const auto result = deletePlacedReferenceLocked(*patch, key);
        if (!result) return std::unexpected(result.error());
        return nlohmann::json(nlohmann::json{
            {"plugin", patchName},
            {"formKey", key.to_string()},
            {"status", result->status == fo4::forms::DeleteStatus::deleted ? "deleted" : "markedDeleted"}
        }.dump());
    } catch (const std::exception& error) {
        return std::unexpected(Error{"PlacedReferenceDeleteFailed", error.what()});
    }
}

Result<fo4::forms::DeleteResult> RadiclePluginBackend::deletePlacedReferenceLocked(
    LoadedPlugin& patch, const fo4::FormKey& key) {
    CommitScope commit;
    const auto env = borrowedEnvironment();
    auto mutated = patch.plugin;
    const auto result = fo4::forms::delete_placed_reference(mutated, *env, key);
    if (const auto error = deleteError(result, patch.name, key.to_string())) return std::unexpected(*error);
    stageCellEditLocked(patch, std::move(mutated));
    return result;
}

Result<nlohmann::json> RadiclePluginBackend::createPlacedObject(const nlohmann::json& arguments) {
    const auto pluginName = support::stringArgument(arguments, 0);
    const auto cellText = support::stringArgument(arguments, 1);
    const auto baseText = support::stringArgument(arguments, 2);
    const auto x = floatArgument(arguments, 3);
    const auto y = floatArgument(arguments, 4);
    const auto z = floatArgument(arguments, 5);
    const auto rx = floatArgument(arguments, 6).value_or(0.0f);
    const auto ry = floatArgument(arguments, 7).value_or(0.0f);
    const auto rz = floatArgument(arguments, 8).value_or(0.0f);
    bool persistent = true;
    if (arguments.is_array() && arguments.size() > 9 && arguments[9].is_boolean()) {
        persistent = arguments[9].get<bool>();
    }
    bool initiallyDisabled = false;
    if (arguments.is_array() && arguments.size() > 10 && arguments[10].is_boolean()) {
        initiallyDisabled = arguments[10].get<bool>();
    }
    if (pluginName.empty() || cellText.empty() || baseText.empty() || !x || !y || !z) {
        return std::unexpected(Error{
            "PlacedObjectArgumentsMissing",
            "CreatePlacedObject requires plugin, cell FormKey, base FormKey and numeric x, y and z"
        });
    }

    fo4::FormKey cell;
    fo4::FormKey base;
    try {
        cell = fo4::FormKey::parse(cellText);
        base = fo4::FormKey::parse(baseText);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InvalidFormKey", error.what()});
    }

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }
    if (auto guard = writeGuard(*loaded); guard.has_value()) return std::unexpected(*guard);

    try {
        auto mutated = loaded->plugin;
        const auto key = fo4::forms::create_placed_reference(
            mutated,
            {
                .cell = cell,
                .base = base,
                .transform = {.x = *x, .y = *y, .z = *z, .rx = rx, .ry = ry, .rz = rz},
                .persistent = persistent,
                .initially_disabled = initiallyDisabled,
            });
        stagePluginMutationLocked(*loaded, std::move(mutated));
        return nlohmann::json(nlohmann::json{
            {"ok", true},
            {"plugin", pluginName},
            {"formKey", key.to_string()}
        }.dump());
    } catch (const std::exception& error) {
        return std::unexpected(Error{"PlacedReferenceCreateFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::createInteriorCell(const nlohmann::json& arguments) {
    const auto pluginName = support::stringArgument(arguments, 0);
    const auto editorId = support::stringArgument(arguments, 1);
    if (pluginName.empty() || editorId.empty()) {
        return std::unexpected(Error{
            "InteriorCellArgumentsMissing",
            "CreateInteriorCell requires plugin and editor_id"
        });
    }

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }
    if (auto guard = writeGuard(*loaded); guard.has_value()) return std::unexpected(*guard);

    try {
        auto mutated = loaded->plugin;
        const auto key = fo4::forms::create_interior_cell(mutated, editorId);
        stagePluginMutationLocked(*loaded, std::move(mutated));
        return nlohmann::json(nlohmann::json{
            {"ok", true},
            {"plugin", pluginName},
            {"formKey", key.to_string()}
        }.dump());
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InteriorCellCreateFailed", error.what()});
    }
}

}  // namespace fo4ide::plugin
