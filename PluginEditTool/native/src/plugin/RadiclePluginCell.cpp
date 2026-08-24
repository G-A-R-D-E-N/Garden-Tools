#include <fo4ide/plugin/RadiclePluginBackend.hpp>

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

#include <fo4/forms/cell.hpp>
#include <fo4/forms/placed_reference.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/plugin/visit.hpp>
#include <fo4/plugin/writer.hpp>
#include <fo4/signature.hpp>

namespace fo4ide::plugin {
namespace {

constexpr auto k_cell = fo4::Signature::from_string("CELL");
constexpr auto k_wrld = fo4::Signature::from_string("WRLD");
constexpr auto k_modl = fo4::Signature::from_string("MODL");
constexpr auto k_full = fo4::Signature::from_string("FULL");

std::string stringArgument(const nlohmann::json& args, std::size_t index) {
    if (!args.is_array() || index >= args.size() || !args[index].is_string()) return {};
    return args[index].get<std::string>();
}

std::optional<int> intArgument(const nlohmann::json& args, std::size_t index) {
    if (!args.is_array() || index >= args.size() || !args[index].is_number_integer()) return std::nullopt;
    return args[index].get<int>();
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
            if (lower(zstring(record, fo4::sig_edid)) == wanted) result = record.form_key;
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
        if (ref.base.has_value()) {
            if (const auto* baseRecord = winningRecord(plugins, *ref.base); baseRecord != nullptr) {
                baseEditorId = zstring(*baseRecord, fo4::sig_edid);
                baseType = friendlyBaseType(baseRecord->header.signature);
                modelPath = zstring(*baseRecord, k_modl);
            }
        }
        if (!modelPath.empty()) ++withModel;

        references.push_back({
            {"formKey", key.to_string()},
            {"editorId", ref.editorId},
            {"recordType", ref.kind == fo4::forms::PlacedReferenceKind::object
                               ? "PlacedObject" : "PlacedNpc"},
            {"baseType", baseType},
            {"baseFormKey", ref.base.has_value() ? ref.base->to_string() : std::string{}},
            {"baseEditorId", baseEditorId},
            {"modelPath", modelPath},
            {"decalDiffuse", ""},
            {"decalWidth", 0.0},
            {"decalHeight", 0.0},
            {"scolParts", nlohmann::json::array()},
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
        {"cellEditorId", zstring(*winningCell, fo4::sig_edid)},
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
    const auto cellId = stringArgument(arguments, 0);
    if (cellId.empty()) {
        return std::unexpected(Error{"CellMissing", "Cell FormKey or EditorID is empty"});
    }

    std::scoped_lock lock(mutex_);
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
    const auto worldId = stringArgument(arguments, 0);
    const auto x = intArgument(arguments, 1);
    const auto y = intArgument(arguments, 2);
    if (worldId.empty() || !x.has_value() || !y.has_value()) {
        return std::unexpected(Error{
            "CellGridArgumentsMissing",
            "GetPlacedReferencesAtGrid requires worldspace, grid X and grid Y"
        });
    }

    std::scoped_lock lock(mutex_);
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
    const auto formKeyText = stringArgument(arguments, 0);
    const auto patchName = stringArgument(arguments, 1);
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

    std::scoped_lock lock(mutex_);
    auto* patch = findPlugin(patchName);
    if (patch == nullptr) {
        return std::unexpected(Error{
            "TargetPluginNotLoaded",
            "Patch plugin is not loaded yet: " + patchName
        });
    }
    if (auto guard = writeGuard(*patch); guard.has_value()) return std::unexpected(*guard);
    if (fo4::find_record(patch->plugin, key) == nullptr) {
        return std::unexpected(Error{
            "NestedOverrideRequired",
            "Placed reference " + formKeyText + " is not already present in " + patchName +
                "; native CELL-child copy-as-override must be completed before this transform can be written safely"
        });
    }

    try {
        auto mutated = patch->plugin;
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
        fo4::write_plugin_file(mutated, patch->path);
        patch->plugin = std::move(mutated);
        {
            std::error_code ec;
            patch->loadedTime = std::filesystem::last_write_time(patch->path, ec);
        }
        rebuildGraphLocked();
        return nlohmann::json("Moved " + formKeyText + " in " + patchName + ".");
    } catch (const std::exception& error) {
        return std::unexpected(Error{"PlacedTransformWriteFailed", error.what()});
    }
}

}  // namespace fo4ide::plugin
