#include "RadiclePluginSupport.hpp"

#include <fo4ide/core/JsonInt.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <exception>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <fo4/environment/placed.hpp>
#include <fo4/forms/cell.hpp>
#include <fo4/forms/placed_reference.hpp>
#include <fo4/forms/static_collection.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/plugin/visit.hpp>
#include <fo4/plugin/writer.hpp>
#include <fo4/signature.hpp>

namespace fo4ide::plugin {

using support::editorId;

namespace {

constexpr auto k_cell = fo4::Signature::from_string("CELL");
constexpr auto k_wrld = fo4::Signature::from_string("WRLD");
constexpr auto k_scol = fo4::Signature::from_string("SCOL");
constexpr auto k_txst = fo4::Signature::from_string("TXST");
constexpr auto k_tx00 = fo4::Signature::from_string("TX00");
constexpr auto k_full = fo4::Signature::from_string("FULL");

std::optional<int> intArgument(const nlohmann::json& args, std::size_t index) {
    if (!args.is_array() || index >= args.size()) return std::nullopt;
    return core::jsonInt(args[index]);
}

std::optional<float> floatArgument(const nlohmann::json& args, std::size_t index) {
    if (!args.is_array() || index >= args.size() || !args[index].is_number()) return std::nullopt;
    return args[index].get<float>();
}

std::string lower(std::string_view value) {
    std::string out(value);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return out;
}

std::string zstring(const fo4::Record& record, fo4::Signature signature) {
    for (const auto& subrecord : record.subrecords) {
        if (subrecord.signature != signature || subrecord.data.empty()) continue;
        const auto* begin = reinterpret_cast<const char*>(subrecord.data.data());
        const auto* end = begin + subrecord.data.size();
        const auto* nul = std::find(begin, end, '\0');
        return std::string(begin, nul);
    }
    return {};
}

std::string primaryModelPath(const fo4::Record& record) {
    fo4::env::RecordRef ref{};
    ref.record = &record;
    const auto path = fo4::env::primary_model_path(ref);
    return path ? std::string(*path) : std::string{};
}

std::string friendlyBaseType(fo4::Signature signature) {
    const auto value = signature.to_string();
    if (value == "STAT") return "Static";
    if (value == "SCOL") return "StaticCollection";
    if (value == "ACTI") return "Activator";
    if (value == "CONT") return "Container";
    if (value == "FURN") return "Furniture";
    if (value == "LIGH") return "Light";
    if (value == "NPC_") return "Npc";
    if (value == "DOOR") return "Door";
    if (value == "MSTT") return "MovableStatic";
    if (value == "TXST") return "TextureSet";
    return value;
}

bool cellInterior(const fo4::Record& record) {
    for (const auto& subrecord : record.subrecords) {
        if (subrecord.signature != fo4::sig_data) continue;
        if (subrecord.data.size() < 2) return false;
        const auto low = std::to_integer<unsigned int>(subrecord.data[0]);
        const auto high = std::to_integer<unsigned int>(subrecord.data[1]);
        const auto flags = static_cast<unsigned int>(low | (high << 8));
        return (flags & 0x1u) != 0;
    }
    return false;
}

template <class Plugins>
std::optional<fo4::FormKey> resolveRecordKey(
    const Plugins& plugins,
    std::string_view id,
    fo4::Signature requiredSignature) {
    if (id.empty()) return std::nullopt;
    if (id.find(':') != std::string_view::npos) {
        try {
            auto key = fo4::FormKey::parse(id);
            for (auto it = plugins.rbegin(); it != plugins.rend(); ++it) {
                const auto* record = fo4::find_record(it->plugin, key);
                if (record != nullptr && record->header.signature == requiredSignature) return key;
            }
            return std::nullopt;
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }

    const auto wanted = lower(id);
    std::optional<fo4::FormKey> result;
    for (const auto& loaded : plugins) {
        fo4::visit_records(loaded.plugin, [&](const fo4::Record& record) {
            if (record.header.signature != requiredSignature) return;
            if (lower(editorId(record)) == wanted) result = record.form_key;
        });
    }
    return result;
}

struct MergedReference {
    fo4::FormKey key;
    fo4::forms::PlacedReferenceKind kind{fo4::forms::PlacedReferenceKind::object};
    std::optional<fo4::FormKey> base;
    std::string editorId;
    fo4::forms::PositionRotation transform{};
    float scale{1.0f};
};

template <class Plugins>
const fo4::Record* winningRecord(const Plugins& plugins, const fo4::FormKey& key) {
    for (auto it = plugins.rbegin(); it != plugins.rend(); ++it) {
        if (const auto* record = fo4::find_record(it->plugin, key); record != nullptr) return record;
    }
    return nullptr;
}

template <class Plugins>
nlohmann::json staticCollectionPartsJson(
    const Plugins& plugins,
    const fo4::Plugin& owner,
    const fo4::Record& collection) {
    const auto decoded = fo4::forms::decode_static_collection_parts(owner, collection);
    nlohmann::json parts = nlohmann::json::array();
    for (const auto& part : decoded) {
        const auto* staticRecord = winningRecord(plugins, part.static_record);
        if (staticRecord == nullptr) continue;
        const auto modelPath = primaryModelPath(*staticRecord);
        if (modelPath.empty() || part.placements.empty()) continue;

        nlohmann::json placements = nlohmann::json::array();
        for (const auto& placement : part.placements) {
            placements.push_back({
                {"x", placement.x}, {"y", placement.y}, {"z", placement.z},
                {"rx", placement.rx}, {"ry", placement.ry}, {"rz", placement.rz},
                {"scale", placement.scale}
            });
        }
        parts.push_back({
            {"modelPath", modelPath},
            {"placements", std::move(placements)}
        });
    }
    return parts.empty() ? nlohmann::json(nullptr) : std::move(parts);
}

template <class Plugins>
nlohmann::json placedReferencesJson(const Plugins& plugins, const fo4::FormKey& cellKey) {
    const fo4::Record* winningCell = nullptr;
    bool winningCellLocalized = false;
    for (auto it = plugins.rbegin(); it != plugins.rend(); ++it) {
        const auto* record = fo4::find_record(it->plugin, cellKey);
        if (record == nullptr) continue;
        winningCell = record;
        winningCellLocalized = it->plugin.header.tes4_flags.is_localized();
        break;
    }
    if (winningCell == nullptr || winningCell->header.signature != k_cell) {
        return {{"error", "Cell not found: " + cellKey.to_string()}};
    }

    std::map<fo4::FormKey, MergedReference> merged;
    for (const auto& loaded : plugins) {
        const auto* cellVersion = fo4::find_record(loaded.plugin, cellKey);
        if (cellVersion == nullptr || cellVersion->header.signature != k_cell) continue;
        const auto refs = fo4::forms::placed_references_for_cell(loaded.plugin, cellKey);
        for (const auto& ref : refs) {
            if (ref.record == nullptr) continue;
            const auto key = ref.record->form_key;
            if (ref.deleted) {
                merged.erase(key);
                continue;
            }
            merged[key] = MergedReference{
                .key = key,
                .kind = ref.kind,
                .base = ref.base,
                .editorId = ref.editor_id,
                .transform = ref.transform,
                .scale = ref.scale,
            };
        }
    }

    nlohmann::json references = nlohmann::json::array();
    std::size_t withModel = 0;
    for (const auto& [key, ref] : merged) {
        std::string baseEditorId;
        std::string baseType;
        std::string modelPath;
        nlohmann::json decalDiffuse = nullptr;
        nlohmann::json scolParts = nullptr;
        if (ref.base.has_value()) {
            for (auto it = plugins.rbegin(); it != plugins.rend(); ++it) {
                const auto* baseRecord = fo4::find_record(it->plugin, *ref.base);
                if (baseRecord == nullptr) continue;
                baseEditorId = editorId(*baseRecord);
                baseType = friendlyBaseType(baseRecord->header.signature);
                modelPath = primaryModelPath(*baseRecord);
                if (baseRecord->header.signature == k_scol) {
                    scolParts = staticCollectionPartsJson(plugins, it->plugin, *baseRecord);
                } else if (baseRecord->header.signature == k_txst) {
                    const auto diffuse = zstring(*baseRecord, k_tx00);
                    if (!diffuse.empty()) decalDiffuse = diffuse;
                }
                break;
            }
        }
        if (!modelPath.empty()) ++withModel;

        references.push_back({
            {"formKey", key.to_string()},
            {"editorId", ref.editorId},
            {"recordType", ref.kind == fo4::forms::PlacedReferenceKind::object ? "PlacedObject"
                           : ref.kind == fo4::forms::PlacedReferenceKind::variant ? "PlacedTrap"
                                                                                 : "PlacedNpc"},
            {"baseType", baseType},
            {"baseFormKey", ref.base.has_value() ? ref.base->to_string() : std::string{}},
            {"baseEditorId", baseEditorId},
            {"modelPath", modelPath},
            {"decalDiffuse", std::move(decalDiffuse)},
            {"decalWidth", nullptr},
            {"decalHeight", nullptr},
            {"scolParts", std::move(scolParts)},
            {"position", {
                {"x", ref.transform.x}, {"y", ref.transform.y}, {"z", ref.transform.z}}},
            {"rotation", {
                {"x", ref.transform.rx}, {"y", ref.transform.ry}, {"z", ref.transform.rz}}},
            {"scale", ref.scale}
        });
    }

    const auto cellName = winningCellLocalized ? std::string{} : zstring(*winningCell, k_full);
    return {
        {"cellFormKey", cellKey.to_string()},
        {"cellEditorId", editorId(*winningCell)},
        {"cellName", cellName},
        {"interior", cellInterior(*winningCell)},
        {"referenceCount", references.size()},
        {"withModelCount", withModel},
        {"references", std::move(references)}
    };
}

}

Result<nlohmann::json> RadiclePluginBackend::getPlacedReferences(
    const nlohmann::json& arguments) const {
    const auto cellId = support::stringArgument(arguments, 0);
    if (cellId.empty()) {
        return std::unexpected(Error{"CellMissing", "Cell FormKey or EditorID is empty"});
    }

    OpLock lock(*this, OpLock::Mode::Read);
    const auto key = resolveRecordKey(plugins_, cellId, k_cell);
    if (!key.has_value()) {
        return nlohmann::json(nlohmann::json{{"error", "Cell not found: " + cellId}}.dump());
    }
    try {
        return nlohmann::json(placedReferencesJson(plugins_, *key).dump());
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
    const auto worldKey = resolveRecordKey(plugins_, worldId, k_wrld);
    if (!worldKey.has_value()) {
        return nlohmann::json(nlohmann::json{{"error", "Worldspace not found: " + worldId}}.dump());
    }

    std::optional<fo4::FormKey> cellKey;
    try {
        for (const auto& loaded : plugins_) {
            if (const auto* cell = fo4::forms::find_exterior_cell(
                    loaded.plugin, *worldKey, *x, *y); cell != nullptr) {
                cellKey = cell->form_key;
            }
        }
        if (!cellKey.has_value()) {
            return nlohmann::json(nlohmann::json{
                {"error", "Cell not found at grid " + std::to_string(*x) + "," + std::to_string(*y)}
            }.dump());
        }
        return nlohmann::json(placedReferencesJson(plugins_, *cellKey).dump());
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
