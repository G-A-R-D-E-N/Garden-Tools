#include "RadiclePluginSupport.hpp"

#include <fo4ide/core/JsonInt.hpp>

#include <cstddef>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <fo4/environment/cell_view.hpp>
#include <fo4/forms/cell.hpp>
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

nlohmann::json placedReferencesJson(const fo4::env::CellReferences& cells) {
    nlohmann::json references = nlohmann::json::array();
    for (const auto& ref : cells.references) {
        const auto* base = ref.base_info ? &*ref.base_info : nullptr;
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
            {"isMarker", base != nullptr && base->editor_marker}
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

void RadiclePluginBackend::dropBorrowedEnvironmentLocked() {
    std::scoped_lock lock(environmentMutex_);
    ++mutationEpoch_;
    borrowedEnvironment_.reset();
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
        return nlohmann::json(placedReferencesJson(fo4::env::list_cell_references(*env, *key)).dump());
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
        return nlohmann::json(placedReferencesJson(fo4::env::list_cell_references(*env, *cellKey)).dump());
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
        auto mutated = patch->plugin;
        std::string copiedFrom;
        if (fo4::find_record(mutated, key) == nullptr) {
            const LoadedPlugin* source = nullptr;
            const fo4::Record* winning = nullptr;
            for (auto it = plugins_.rbegin(); it != plugins_.rend(); ++it) {
                const auto* candidate = fo4::find_record(it->plugin, key);
                if (candidate == nullptr) continue;
                source = &*it;
                winning = candidate;
                break;
            }
            if (source == nullptr || winning == nullptr) {
                return std::unexpected(Error{
                    "PlacedReferenceNotFound",
                    "Placed reference is not present in the loaded environment: " + formKeyText
                });
            }
            if (winning->header.flags.is_deleted()) {
                return std::unexpected(Error{
                    "PlacedReferenceDeleted",
                    "The winning version of " + formKeyText + " is deleted in " + source->name
                });
            }

            const auto copyStatus = fo4::forms::copy_placed_reference_as_override(
                mutated, source->plugin, key, false);
            switch (copyStatus) {
                case fo4::forms::PlacedReferenceCopyStatus::inserted:
                case fo4::forms::PlacedReferenceCopyStatus::replaced:
                    copiedFrom = source->name;
                    break;
                case fo4::forms::PlacedReferenceCopyStatus::already_exists:
                    break;
                case fo4::forms::PlacedReferenceCopyStatus::source_missing:
                    return std::unexpected(Error{
                        "PlacedReferenceNotFound",
                        "Winning source no longer contains placed reference " + formKeyText
                    });
            }
        }

        const bool changed = fo4::forms::set_placed_reference_transform(
            mutated,
            key,
            {.x = *x, .y = *y, .z = *z, .rx = *rx, .ry = *ry, .rz = *rz});
        if (!changed) {
            return std::unexpected(Error{
                "PlacedReferenceNotFound",
                "Placed reference is not present in " + patchName + ": " + formKeyText
            });
        }
        stagePluginMutationLocked(*patch, std::move(mutated));

        auto message = "Moved " + formKeyText + " in " + patchName + ".";
        if (!copiedFrom.empty()) {
            message += " Created the nested override from " + copiedFrom + ".";
        }
        return nlohmann::json(std::move(message));
    } catch (const std::exception& error) {
        return std::unexpected(Error{"PlacedTransformWriteFailed", error.what()});
    }
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
