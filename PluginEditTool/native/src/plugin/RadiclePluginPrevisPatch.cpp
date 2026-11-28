#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include <fo4/archive/assets.hpp>
#include <fo4/environment/cell_view.hpp>
#include <fo4/formid.hpp>
#include <fo4/plugin/mutation.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/plugin/subrecord_authoring.hpp>
#include <fo4/plugin/visit.hpp>
#include <fo4/plugin/writer.hpp>
#include <fo4/environment/environment.hpp>
#include <fo4/plugin/container.hpp>
#include <fo4/previs/bake_oc.hpp>
#include <fo4/previs/precombine_plan.hpp>
#include <fo4/previs/cdx.hpp>
#include <fo4/previs/csg.hpp>
#include <fo4/previs/stamp.hpp>
#include <fo4/previs/uvd.hpp>
#include <fo4/nif/nif.hpp>
#include <fo4/nif/shape_mesh.hpp>
#include <fo4/nif/physics_nif.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <map>
#include <optional>

#include "RadiclePluginPrevisAssets.hpp"
#include "RadiclePluginPrevisIo.hpp"
#include "RadiclePluginPrevisPrep.hpp"
#include "RadiclePluginSupport.hpp"

namespace fo4ide::plugin {
namespace {

std::optional<std::uint16_t> parseStampWord(const nlohmann::json& value) {
    if (value.is_number_unsigned()) return static_cast<std::uint16_t>(value.get<std::uint32_t>());
    if (!value.is_string()) return std::nullopt;
    const auto text = value.get<std::string>();
    if (text.empty()) return std::nullopt;
    const int base = text.rfind("0x", 0) == 0 || text.rfind("0X", 0) == 0 ? 16 : 10;
    try {
        const auto parsed = std::stoul(text, nullptr, base);
        if (parsed > 0xFFFFu) return std::nullopt;
        return static_cast<std::uint16_t>(parsed);
    } catch (...) {
        return std::nullopt;
    }
}

fo4::previs::XcriRaw parseXcri(const nlohmann::json& value) {
    fo4::previs::XcriRaw out;
    if (!value.is_object()) return out;
    if (value.contains("mesh_ids") && value["mesh_ids"].is_array()) {
        for (const auto& mesh : value["mesh_ids"]) {
            if (mesh.is_number_unsigned()) out.mesh_ids.push_back(mesh.get<std::uint32_t>());
        }
    }
    if (value.contains("refs") && value["refs"].is_array()) {
        for (const auto& pair : value["refs"]) {
            if (!pair.is_array() || pair.size() < 2) continue;
            if (!pair[0].is_number_unsigned() || !pair[1].is_number_unsigned()) continue;
            out.refs.emplace_back(pair[0].get<std::uint32_t>(), pair[1].get<std::uint32_t>());
        }
    }
    return out;
}

bool recordHasSubrecord(const fo4::Plugin& plugin, const fo4::FormKey& key, fo4::Signature signature) {
    const auto* record = fo4::find_record(plugin, key);
    if (record == nullptr) return false;
    for (const auto& sub : record->subrecords) {
        if (sub.signature == signature) return true;
    }
    return false;
}

}  // namespace

Result<void> RadiclePluginBackend::copyCellOverrideIntoPlugin(fo4::Plugin& patch,
                                                            const fo4::Plugin& source,
                                                            const fo4::FormKey& key) {
    if (fo4::find_record(patch, key) != nullptr) return {};
    const auto pristine = patch;
    auto status = fo4::copy_record_as_override(patch, source, key, false);
    if (status == fo4::RecordCopyStatus::source_missing) {
        return std::unexpected(Error{"RecordNotFound", "Record not in source plugin: " + key.to_string()});
    }
    if (status == fo4::RecordCopyStatus::already_exists) return {};
    if (status == fo4::RecordCopyStatus::inserted || status == fo4::RecordCopyStatus::replaced) {
        return {};
    }
    patch = pristine;
    const auto deep = fo4::copy_record_deep(patch, source, key, false);
    status = deep.status;
    if (status == fo4::RecordCopyStatus::already_exists) return {};
    if (status == fo4::RecordCopyStatus::inserted || status == fo4::RecordCopyStatus::replaced) {
        return {};
    }
    return std::unexpected(
        Error{"CopyOverrideFailed", "Could not copy CELL override into patch"});
}

Result<void> RadiclePluginBackend::ensureCellOverrideLocked(LoadedPlugin& patch,
                                                             const fo4::Plugin& source,
                                                             const fo4::FormKey& key) {
    if (fo4::find_record(patch.plugin, key) != nullptr) return {};
    auto mutated = patch.plugin;
    if (auto copied = copyCellOverrideIntoPlugin(mutated, source, key); !copied) {
        return copied;
    }
    if (fo4::find_record(mutated, key) == nullptr) {
        return std::unexpected(
            Error{"CopyOverrideFailed", "Could not copy CELL override into patch"});
    }
    stagePluginMutationLocked(patch, std::move(mutated));
    return {};
}

void RadiclePluginBackend::clearPrevisSubrecordsOnCell(fo4::Plugin& plugin, const fo4::FormKey& key) {
    static const fo4::Signature signs[] = {
        fo4::Signature::from_string("PCMB"),
        fo4::Signature::from_string("VISI"),
        fo4::Signature::from_string("XCRI"),
        fo4::Signature::from_string("RVIS"),
        fo4::Signature::from_string("XPRI"),
    };
    for (const auto signature : signs) {
        for (;;) {
            if (fo4::remove_record_subrecord(plugin, key, signature, 0) !=
                fo4::SubrecordRemoveStatus::removed) {
                break;
            }
        }
    }
    setNoPreVisFlag(plugin, key, true);
}

Result<nlohmann::json> RadiclePluginBackend::stampCellPrevis(const nlohmann::json& arguments) {
    const auto patchName = support::stringArgument(arguments, 0);
    const auto cellText = support::stringArgument(arguments, 1);
    const auto sourceName = support::stringArgument(arguments, 2);
    if (patchName.empty() || cellText.empty()) {
        return std::unexpected(Error{"PrevisStampArgs", "stamp requires patch_plugin and cell"});
    }
    const auto pcmb = parseStampWord(arguments.size() > 3 ? arguments[3] : nlohmann::json{});
    const auto visi = parseStampWord(arguments.size() > 4 ? arguments[4] : nlohmann::json{});
    if (!pcmb || !visi) {
        return std::unexpected(Error{"PrevisStampArgs", "pcmb and visi must be 0..65535"});
    }
    bool apply = true;
    support::tryBoolArgument(arguments, 6, apply);
    nlohmann::json xcriJson = arguments.size() > 5 ? arguments[5] : nlohmann::json::object();
    if (xcriJson.is_string()) {
        try {
            xcriJson = nlohmann::json::parse(xcriJson.get<std::string>());
        } catch (const std::exception& error) {
            return std::unexpected(Error{"PrevisStampArgs", std::string("xcri JSON: ") + error.what()});
        }
    }

    fo4::FormKey key;
    try {
        key = fo4::FormKey::parse(cellText);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InvalidFormKey", error.what()});
    }

    std::string winnerName = sourceName;
    if (winnerName.empty()) {
        OpLock readLock(*this, OpLock::Mode::Read);
        const auto winner = support::winningPlugin(plugins_, key);
        if (!winner) return std::unexpected(winner.error());
        winnerName = (*winner)->key.filename();
    }

    if (!apply) {
        return nlohmann::json(std::string("DRY RUN: would stamp ") + key.to_string() + " in " +
                                patchName + " (PCMB=" + std::to_string(*pcmb) +
                                ", VISI=" + std::to_string(*visi) + ").");
    }

    OpLock lock(*this, OpLock::Mode::Write, {patchName, winnerName});
    auto* patch = findPlugin(patchName);
    if (patch == nullptr) {
        return std::unexpected(Error{"TargetPluginNotLoaded", "Patch plugin is not loaded: " + patchName});
    }
    if (const auto refused = previsPatchError(*patch)) return std::unexpected(*refused);
    const auto* source = findPlugin(winnerName);
    if (source == nullptr) {
        return std::unexpected(Error{"SourcePluginNotLoaded", "Source plugin is not loaded: " + winnerName});
    }
    if (auto copied = this->ensureCellOverrideLocked(*patch, source->plugin, key); !copied) {
        return std::unexpected(copied.error());
    }
    patch = findPlugin(patchName);
    if (patch == nullptr) {
        return std::unexpected(Error{"TargetPluginNotLoaded", "Patch plugin is not loaded: " + patchName});
    }
    const auto env = borrowedEnvironment();

    fo4::previs::StampRequest req{.pcmb_stamp = *pcmb,
                                  .visi_stamp = *visi,
                                  .xcri = parseXcri(xcriJson),
                                  .rvis_root = std::nullopt};

    try {
        auto mutated = patch->plugin;
        fo4::assign_record_form_keys(mutated);
// Constraint: RVIS is a 4-byte file Form-ID in the patch's own master list. rvis_local names a root cell of the stamped cell's plugin; otherwise an exterior cell keeps the root its override already carries and only becomes its own root when it has none.
        const auto patchContext = fo4::plugin_form_id_context(mutated);
        if (xcriJson.is_object() && xcriJson.contains("rvis_local") &&
            xcriJson["rvis_local"].is_number_unsigned()) {
            const fo4::FormKey root{key.owner(),
                                    fo4::LocalFormId{xcriJson["rvis_local"].get<std::uint32_t>() & 0xFFFFFFu}};
            req.rvis_root = fo4::encode_plugin_form_id(root, patchContext);
        } else if (const auto summary = fo4::env::describe_cell(*env, key);
                   summary && !summary->interior &&
                   !recordHasSubrecord(mutated, key, fo4::Signature::from_string("RVIS"))) {
            req.rvis_root = fo4::encode_plugin_form_id(key, patchContext);
        }
        const auto status = fo4::previs::stamp_cell_previs(mutated, key, req);
        if (status == fo4::previs::StampStatus::record_missing) {
            return std::unexpected(Error{"PrevisStampFailed", "CELL override missing in patch after copy"});
        }
        if (status != fo4::previs::StampStatus::ok) {
            return std::unexpected(Error{"PrevisStampFailed", "Could not write previs subrecords"});
        }
        // Stamp invariant: a stamped CELL must not keep the NoPreVis flag that suppresses previs.
        setNoPreVisFlag(mutated, key, false);
        stagePluginMutationLocked(*patch, std::move(mutated));
        dropBorrowedEnvironmentOnlyLocked();
        return nlohmann::json("Stamped previs on " + key.to_string() + " in " + patchName +
                               ". Save the plugin and pack OC NIF assets.");
    } catch (const std::exception& error) {
        return std::unexpected(Error{"PrevisStampFailed", error.what()});
    }
}

namespace {

bool ownsRecords(const fo4::Plugin& plugin) {
    const auto context = fo4::plugin_form_id_context(plugin);
    bool owns = false;
    fo4::visit_records(plugin, [&](const fo4::Record& record) {
        if (owns || record.header.signature == fo4::sig_tes4 || record.header.form_id.is_null()) return;
        try {
            owns = fo4::resolve_plugin_form_id(record.header.form_id, context).owner() == plugin.key;
        } catch (const fo4::FormIdError&) {
        }
    });
    return owns;
}

}  // namespace

std::optional<Error> RadiclePluginBackend::previsPatchError(const LoadedPlugin& patch) const {
    if (!isEditable(patch)) return Error{"PluginReadOnly", "Patch plugin is not writable: " + patch.name};
    // Constraint: a patch must be a new plugin; a session-created one is always accepted, any other must own no record and have no dependent.
    if (patch.source == "New plugins") return std::nullopt;
    for (const auto& other : plugins_) {
        const auto& masters = other.plugin.header.masters;
        if (std::ranges::any_of(masters, [&](const auto& master) { return master.key == patch.plugin.key; })) {
            return Error{"PrevisPatchNotNew",
                         "Patch plugin " + patch.name + " is a master of " + other.name +
                             "; previs goes into a new patch plugin only"};
        }
    }
    if (ownsRecords(patch.plugin)) {
        return Error{"PrevisPatchNotNew",
                     "Patch plugin " + patch.name + " owns records of its own; previs goes into a new patch plugin only"};
    }
    return std::nullopt;
}

Result<nlohmann::json> RadiclePluginBackend::disableCellPrevis(const nlohmann::json& arguments) {
    const auto cellText = support::stringArgument(arguments, 0);
    const auto patchName = support::stringArgument(arguments, 1);
    const auto sourceName = support::stringArgument(arguments, 2);
    if (cellText.empty() || patchName.empty()) {
        return std::unexpected(Error{"PrevisDisableArgs", "disable requires cell and patch_plugin"});
    }
    bool apply = true;
    support::tryBoolArgument(arguments, 3, apply);

    fo4::FormKey key;
    try {
        key = fo4::FormKey::parse(cellText);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InvalidFormKey", error.what()});
    }

    std::string winnerName = sourceName;
    {
        OpLock readLock(*this, OpLock::Mode::Read);
        const auto env = borrowedEnvironment();
        const auto summary = fo4::env::describe_cell(*env, key);
        if (!summary) {
            return std::unexpected(Error{"CellNotFound", "Cell not found: " + cellText});
        }
        if (summary->interior) {
            return std::unexpected(Error{
                "PrevisDisableInterior",
                "Interior cells are not supported for Disable PreVis (exterior previs/precombine only)."});
        }
        if (winnerName.empty()) {
            const auto winner = support::winningPlugin(plugins_, key);
            if (!winner) return std::unexpected(winner.error());
            winnerName = (*winner)->key.filename();
        }
        const auto* source = findPlugin(winnerName);
        const auto* patchPlugin = findPlugin(patchName);
        const auto* checked =
            patchPlugin != nullptr && fo4::find_record(patchPlugin->plugin, key) != nullptr ? patchPlugin : source;
        if (checked != nullptr && fo4::find_record(checked->plugin, key) != nullptr &&
            !recordHasSubrecord(checked->plugin, key, fo4::Signature::from_string("PCMB"))) {
            return nlohmann::json("Nothing to disable: " + key.to_string() + " has no PCMB in " + checked->name + ".");
        }
    }

    if (!apply) {
        return nlohmann::json("DRY RUN: would disable previs on " + key.to_string() + " into " + patchName + ".");
    }

    OpLock lock(*this, OpLock::Mode::Write, {patchName, winnerName});
    auto* patch = findPlugin(patchName);
    if (patch == nullptr) {
        return std::unexpected(Error{"TargetPluginNotLoaded", "Patch plugin is not loaded: " + patchName});
    }
    if (const auto refused = previsPatchError(*patch)) return std::unexpected(*refused);
    const auto* source = findPlugin(winnerName);
    if (source == nullptr) {
        return std::unexpected(Error{"SourcePluginNotLoaded", "Source plugin is not loaded: " + winnerName});
    }
    if (auto copied = this->ensureCellOverrideLocked(*patch, source->plugin, key); !copied) {
        return std::unexpected(copied.error());
    }
    patch = findPlugin(patchName);
    if (patch == nullptr) {
        return std::unexpected(Error{"TargetPluginNotLoaded", "Patch plugin is not loaded: " + patchName});
    }

    try {
        auto mutated = patch->plugin;
        clearPrevisSubrecordsOnCell(mutated, key);
        stagePluginMutationLocked(*patch, std::move(mutated));
        dropBorrowedEnvironmentOnlyLocked();
        return nlohmann::json("Disabled previs on " + key.to_string() + " in " + patchName +
                               " (NoPreVis + cleared PCMB/VISI/XCRI/RVIS/XPRI).");
    } catch (const std::exception& error) {
        return std::unexpected(Error{"PrevisDisableFailed", error.what()});
    }
}

namespace {

std::optional<float> jsonFloat(const nlohmann::json& value) {
    if (value.is_number()) return value.get<float>();
    return std::nullopt;
}

std::optional<std::vector<fo4::previs::OcPlacement>> parseOcPlacements(const nlohmann::json& value) {
    if (!value.is_array()) return std::nullopt;
    std::vector<fo4::previs::OcPlacement> out;
    for (const auto& row : value) {
        if (!row.is_object()) return std::nullopt;
        fo4::previs::OcPlacement placement;
        if (row.contains("translation") && row["translation"].is_array() && row["translation"].size() >= 3) {
            for (std::size_t i = 0; i < 3; ++i) {
                const auto field = jsonFloat(row["translation"][i]);
                if (!field.has_value()) return std::nullopt;
                placement.xform.translation[i] = *field;
            }
        } else if (row.contains("position") && row["position"].is_object()) {
            const auto& position = row["position"];
            const auto x = jsonFloat(position.value("x", nlohmann::json{}));
            const auto y = jsonFloat(position.value("y", nlohmann::json{}));
            const auto z = jsonFloat(position.value("z", nlohmann::json{}));
            if (!x.has_value() || !y.has_value() || !z.has_value()) return std::nullopt;
            float rx = 0.0f;
            float ry = 0.0f;
            float rz = 0.0f;
            float scale = 1.0f;
            if (row.contains("rotation") && row["rotation"].is_object()) {
                const auto& rotation = row["rotation"];
                rx = jsonFloat(rotation.value("x", nlohmann::json{})).value_or(0.0f);
                ry = jsonFloat(rotation.value("y", nlohmann::json{})).value_or(0.0f);
                rz = jsonFloat(rotation.value("z", nlohmann::json{})).value_or(0.0f);
            }
            scale = jsonFloat(row.value("scale", nlohmann::json{1.0})).value_or(1.0f);
            placement.xform =
                fo4::previs::placement_transform_from_euler(*x, *y, *z, rx, ry, rz, scale);
        } else {
            return std::nullopt;
        }
        if (row.contains("rotation") && row["rotation"].is_array() && row["rotation"].size() >= 9) {
            for (std::size_t i = 0; i < 9; ++i) {
                const auto field = jsonFloat(row["rotation"][i]);
                if (!field.has_value()) return std::nullopt;
                placement.xform.rotation[i] = *field;
            }
        }
        if (row.contains("scale")) {
            const auto scale = jsonFloat(row["scale"]);
            if (!scale.has_value()) return std::nullopt;
            placement.xform.scale = *scale;
        }
        out.push_back(std::move(placement));
    }
    return out;
}

std::optional<std::vector<fo4::previs::OcPlacement>> placementsFromPlanGroup(const nlohmann::json& plan,
                                                                              std::size_t group_index) {
    if (!plan.is_object() || !plan.contains("groups") || !plan["groups"].is_array()) return std::nullopt;
    const auto& groups = plan["groups"];
    if (group_index >= groups.size()) return std::nullopt;
    const auto& group = groups[group_index];
    if (!group.contains("instances") || !group["instances"].is_array()) return std::nullopt;
    const auto model_path = group.value("modelPath", std::string{});
    nlohmann::json array = nlohmann::json::array();
    for (const auto& instance : group["instances"]) array.push_back(instance);
    auto placements = parseOcPlacements(array);
    if (!placements.has_value()) return std::nullopt;
    if (!model_path.empty()) {
        for (auto& placement : *placements) placement.source_nif = std::filesystem::path(model_path);
    }
    return placements;
}

}  // namespace

Result<nlohmann::json> RadiclePluginBackend::bakeOcNif(const nlohmann::json& arguments) {
    const auto source = support::stringArgument(arguments, 0);
    const auto output = support::stringArgument(arguments, 1);
    if (source.empty() || output.empty()) {
        return std::unexpected(Error{"BakeOcArgs", "bake requires source_nif and output_oc_nif paths"});
    }
    fo4::previs::BakeOcStatus status = fo4::previs::BakeOcStatus::invalid_input;
    std::size_t placement_count = 0;
    if (arguments.size() > 2 && !arguments[2].is_null()) {
        std::optional<std::vector<fo4::previs::OcPlacement>> placements;
        if (arguments[2].is_object() && arguments[2].contains("groups")) {
            std::size_t groupIndex = 0;
            if (arguments.size() > 3 && arguments[3].is_number_unsigned()) {
                groupIndex = arguments[3].get<std::size_t>();
            }
            placements = placementsFromPlanGroup(arguments[2], groupIndex);
        } else {
            placements = parseOcPlacements(arguments[2]);
        }
        if (!placements.has_value() || placements->empty()) {
            return std::unexpected(Error{"BakeOcArgs", "placements must be a non-empty array or plan groups JSON"});
        }
        placement_count = placements->size();
        const auto shell = support::stringArgument(arguments, 4);
        const auto output_path = std::filesystem::path(output);
        if (!shell.empty() && fo4::previs::stat_mesh_nif(readBinaryFile(std::filesystem::path(source)))) {
            status = fo4::previs::bake_oc_author_from_stat_shell(std::filesystem::path(source),
                                                                 std::filesystem::path(shell), *placements,
                                                                 output_path);
        } else {
            fo4::previs::BakeOcPlacementsRequest request{.template_oc_nif = std::filesystem::path(source),
                                                         .placements = std::move(*placements)};
            status = fo4::previs::bake_oc_from_placements(request, output_path);
        }
    } else {
        status = fo4::previs::bake_oc_single_source(std::filesystem::path(source), std::filesystem::path(output));
    }
    switch (status) {
        case fo4::previs::BakeOcStatus::ok:
            if (placement_count > 0) {
                return nlohmann::json("Baked " + output + " from " + source + " with " +
                                    std::to_string(placement_count) + " placement(s).");
            }
            return nlohmann::json("Baked " + output + " from " + source + ".");
        case fo4::previs::BakeOcStatus::invalid_input:
        case fo4::previs::BakeOcStatus::unsupported:
            return std::unexpected(Error{
                "BakeOcRejected",
                placement_count > 0
                    ? "Template _OC count/layout does not match placements or Shared extra is not patchable yet."
                    : "Source is not a measured _OC.NIF with BSPackedCombinedSharedGeomDataExtra."});
        case fo4::previs::BakeOcStatus::io_error:
        default:
            return std::unexpected(Error{"BakeOcIo", "Could not read source or write output _OC.NIF"});
    }
}

Result<nlohmann::json> RadiclePluginBackend::writePrevisContainer(const nlohmann::json& arguments) {
    const auto kind = support::stringArgument(arguments, 0);
    const auto source = support::stringArgument(arguments, 1);
    const auto output = support::stringArgument(arguments, 2);
    if (kind.empty() || source.empty() || output.empty()) {
        return std::unexpected(Error{"PrevisContainerArgs", "write requires kind, source_path, output_path"});
    }
    const auto bytes = readBinaryFile(std::filesystem::path(source));
    if (bytes.empty()) {
        return std::unexpected(Error{"PrevisContainerIo", "Could not read source container: " + source});
    }
    std::vector<std::byte> out;
    try {
        if (kind == "cdx") {
            const auto parsed = fo4::previs::parse_cdx(bytes);
            out = fo4::previs::write_cdx(parsed);
            if (!fo4::previs::validate_cdx(out).empty()) {
                return std::unexpected(Error{"PrevisContainerInvalid", "CDX round-trip failed validation"});
            }
        } else if (kind == "csg") {
            const auto parsed = fo4::previs::parse_csg(bytes);
            out = fo4::previs::write_csg(parsed);
            if (!fo4::previs::validate_csg(out).empty()) {
                return std::unexpected(Error{"PrevisContainerInvalid", "CSG round-trip failed validation"});
            }
        } else if (kind == "uvd") {
            const auto parsed = fo4::previs::parse_uvd(bytes);
            if (!fo4::previs::validate_uvd(bytes).empty()) {
                return std::unexpected(Error{"PrevisContainerInvalid", "Source UVD failed validation"});
            }
            const std::string banner(parsed.banner);
            out = fo4::previs::write_uvd(
                fo4::previs::UvdWriteRequest{.banner = banner, .payload = parsed.body()});
            if (!fo4::previs::validate_uvd(out).empty()) {
                return std::unexpected(Error{"PrevisContainerInvalid", "UVD round-trip failed validation"});
            }
        } else {
            return std::unexpected(Error{"PrevisContainerArgs", "kind must be cdx, csg, or uvd"});
        }
    } catch (const std::exception& error) {
        return std::unexpected(Error{"PrevisContainerFailed", error.what()});
    }
    if (!writeBinaryFile(std::filesystem::path(output), out)) {
        return std::unexpected(Error{"PrevisContainerIo", "Could not write output container: " + output});
    }
    return nlohmann::json("Wrote " + kind + " container (" + std::to_string(out.size()) + " bytes) to " + output + ".");
}

nlohmann::json precombinePlanJson(const fo4::previs::PrecombinePlan& plan,
                                  std::size_t groupLimit = 0,
                                  bool includeInstances = true) {
    std::size_t eligibleReferences = 0;
    for (const auto& group : plan.groups) eligibleReferences += group.instances.size();
    const std::size_t groupsShown =
        groupLimit == 0 || groupLimit > plan.groups.size() ? plan.groups.size() : groupLimit;
    nlohmann::json groups = nlohmann::json::array();
    for (std::size_t index = 0; index < groupsShown; ++index) {
        const auto& group = plan.groups[index];
        nlohmann::json instances = nlohmann::json::array();
        if (includeInstances) {
            for (const auto& inst : group.instances) {
                instances.push_back({
                    {"reference", inst.reference.to_string()},
                    {"editorId", inst.editor_id.value_or("")},
                    {"position", {{"x", inst.transform.x}, {"y", inst.transform.y}, {"z", inst.transform.z}}},
                    {"rotation", {{"x", inst.transform.rx}, {"y", inst.transform.ry}, {"z", inst.transform.rz}}},
                    {"scale", inst.scale},
                });
            }
        }
        nlohmann::json entry = {{"modelPath", group.model_path},
                               {"instanceCount", group.instances.size()},
                               {"instances", instances}};
        // Constraint: a pinned target is reported with the group it pins, so a caller sees the ratios the writer will hit.
        if (group.lod_target.has_value()) {
            entry["lodTarget"] = {{"level1", group.lod_target->level1}, {"level2", group.lod_target->level2}};
        }
        groups.push_back(entry);
    }
    return {{"cell", plan.cell.to_string()},
            {"plugin", plan.plugin},
            {"interior", plan.interior},
            {"temporaryReferences", plan.temporary_considered},
            {"groupCount", plan.groups.size()},
            {"eligibleReferences", eligibleReferences},
            {"groupsShown", groupsShown},
            {"groupsOmitted", plan.groups.size() - groupsShown},
            {"skipped",
             {{"scripted", plan.skipped.scripted},
              {"enableParent", plan.skipped.enable_parent},
              {"foreignPlugin", plan.skipped.foreign_plugin},
              {"unresolvedPlugin", plan.skipped.unresolved_plugin}}},
            {"groups", groups}};
}

Result<nlohmann::json> RadiclePluginBackend::precombinePlan(const nlohmann::json& arguments) {
    const auto cellText = support::stringArgument(arguments, 0);
    const auto pluginName = support::stringArgument(arguments, 1);
    if (cellText.empty() || pluginName.empty()) {
        return std::unexpected(Error{"PrecombinePlanArgs", "plan requires cell and plugin"});
    }
    std::size_t minInstances = 2;
    if (arguments.size() > 2 && arguments[2].is_number_unsigned()) {
        minInstances = arguments[2].get<std::size_t>();
    }
    // Constraint: a request pins object ratios by model path, so arg 3 carries the level1/level2 fractions.
    const auto lodTargets =
        arguments.size() > 3 ? fo4::previs::lod_targets_from_json(arguments[3]) : fo4::previs::PrecombineLodTargets{};
    std::size_t groupLimit = 0;
    if (arguments.size() > 4 && arguments[4].is_number_unsigned()) {
        groupLimit = arguments[4].get<std::size_t>();
    }
    const bool includeInstances = arguments.size() > 5 && arguments[5].is_boolean() ? arguments[5].get<bool>() : true;
    fo4::FormKey key;
    try {
        key = fo4::FormKey::parse(cellText);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InvalidFormKey", error.what()});
    }
    OpLock readLock(*this, OpLock::Mode::Read);
    const auto env = borrowedEnvironment();
    if (env == nullptr) {
        return std::unexpected(Error{"NoEnvironment", "Load an MO2 profile before precombine planning."});
    }
    const auto plan = fo4::previs::build_precombine_plan(*env, key, pluginName, minInstances, lodTargets);
    if (!plan.has_value()) {
        return std::unexpected(
            Error{"PrecombinePlanFailed", "Cell is not in the loaded environment."});
    }
    return precombinePlanJson(*plan, groupLimit, includeInstances);
}


}  // namespace fo4ide::plugin
