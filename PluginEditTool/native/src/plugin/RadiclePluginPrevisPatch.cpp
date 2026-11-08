#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include <fo4/environment/cell_view.hpp>
#include <fo4/plugin/mutation.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/plugin/subrecord_authoring.hpp>
#include <fo4/plugin/visit.hpp>
#include <fo4/previs/bake_oc.hpp>
#include <fo4/previs/cdx.hpp>
#include <fo4/previs/csg.hpp>
#include <fo4/previs/stamp.hpp>
#include <fo4/previs/uvd.hpp>

#include <fstream>

#include "RadiclePluginSupport.hpp"

namespace fo4ide::plugin {
namespace {

constexpr auto k_cell = fo4::Signature::from_string("CELL");
constexpr std::uint32_t k_no_pre_vis = 1u << 18;

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

std::vector<std::byte> readBinaryFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    in.seekg(0, std::ios::end);
    const auto size = in.tellg();
    if (size <= 0) return {};
    in.seekg(0, std::ios::beg);
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    in.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!in) return {};
    return bytes;
}

bool writeBinaryFile(const std::filesystem::path& path, std::span<const std::byte> bytes) {
    if (path.empty() || bytes.empty()) return false;
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
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

Result<void> RadiclePluginBackend::ensureCellOverrideLocked(LoadedPlugin& patch,
                                                             const fo4::Plugin& source,
                                                             const fo4::FormKey& key) {
    if (fo4::find_record(patch.plugin, key) != nullptr) return {};
    auto mutated = patch.plugin;
    auto status = fo4::copy_record_as_override(mutated, source, key, false);
    if (status == fo4::RecordCopyStatus::source_missing) {
        return std::unexpected(Error{"RecordNotFound", "Record not in source plugin: " + key.to_string()});
    }
    if (status == fo4::RecordCopyStatus::already_exists) return {};
    if (status == fo4::RecordCopyStatus::inserted || status == fo4::RecordCopyStatus::replaced) {
        stagePluginMutationLocked(patch, std::move(mutated));
        return {};
    }
    mutated = patch.plugin;
    const auto deep = fo4::copy_record_deep(mutated, source, key, false);
    status = deep.status;
    if (status == fo4::RecordCopyStatus::already_exists) return {};
    if (status == fo4::RecordCopyStatus::inserted || status == fo4::RecordCopyStatus::replaced) {
        stagePluginMutationLocked(patch, std::move(mutated));
        return {};
    }
    return std::unexpected(
        Error{"CopyOverrideFailed", "Could not copy CELL override into patch"});
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
    fo4::visit_records(plugin, [&](fo4::Record& record) {
        if (record.header.signature != k_cell || record.form_key != key) return;
        fo4::require_decodable(record);
        record.header.flags = fo4::RecordFlags(record.header.flags.value() | k_no_pre_vis);
        record.original_range.clear();
        plugin.original_range.clear();
    });
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
    if (!isEditable(*patch)) {
        return std::unexpected(Error{"PluginReadOnly", "Patch plugin is not writable: " + patchName});
    }
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
                                  .xcri = parseXcri(xcriJson)};
    if (xcriJson.is_object() && xcriJson.contains("rvis_local") &&
        xcriJson["rvis_local"].is_number_unsigned()) {
        req.rvis_local_cell = static_cast<std::uint16_t>(xcriJson["rvis_local"].get<std::uint32_t>());
    } else if (const auto summary = fo4::env::describe_cell(*env, key); summary && !summary->interior) {
        req.rvis_local_cell = static_cast<std::uint16_t>(key.local().value() & 0xFFFFu);
    }

    try {
        auto mutated = patch->plugin;
        const auto status = fo4::previs::stamp_cell_previs(mutated, key, req);
        if (status == fo4::previs::StampStatus::record_missing) {
            return std::unexpected(Error{"PrevisStampFailed", "CELL override missing in patch after copy"});
        }
        if (status != fo4::previs::StampStatus::ok) {
            return std::unexpected(Error{"PrevisStampFailed", "Could not write previs subrecords"});
        }
        stagePluginMutationLocked(*patch, std::move(mutated));
        dropBorrowedEnvironmentOnlyLocked();
        return nlohmann::json("Stamped previs on " + key.to_string() + " in " + patchName +
                               ". Save the plugin and pack OC NIF assets.");
    } catch (const std::exception& error) {
        return std::unexpected(Error{"PrevisStampFailed", error.what()});
    }
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
            const auto pcmb = fo4::Signature::from_string("PCMB");
            if (!recordHasSubrecord(**winner, key, pcmb)) {
                return nlohmann::json("Nothing to disable: " + key.to_string() + " has no PCMB on the winner.");
            }
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
    if (!isEditable(*patch)) {
        return std::unexpected(Error{"PluginReadOnly", "Patch plugin is not writable: " + patchName});
    }
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

Result<nlohmann::json> RadiclePluginBackend::bakeOcNif(const nlohmann::json& arguments) {
    const auto source = support::stringArgument(arguments, 0);
    const auto output = support::stringArgument(arguments, 1);
    if (source.empty() || output.empty()) {
        return std::unexpected(Error{"BakeOcArgs", "bake requires source_nif and output_oc_nif paths"});
    }
    const auto status = fo4::previs::bake_oc_single_source(std::filesystem::path(source),
                                                           std::filesystem::path(output));
    switch (status) {
        case fo4::previs::BakeOcStatus::ok:
            return nlohmann::json("Baked " + output + " from " + source + ".");
        case fo4::previs::BakeOcStatus::invalid_input:
        case fo4::previs::BakeOcStatus::unsupported:
            return std::unexpected(Error{
                "BakeOcRejected",
                "Source is not a measured _OC.NIF with BSPackedCombinedSharedGeomDataExtra (native encoder pending)."});
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

}  // namespace fo4ide::plugin
