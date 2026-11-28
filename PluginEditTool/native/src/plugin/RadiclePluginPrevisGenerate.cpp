#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include <fo4/archive/assets.hpp>
#include <fo4/environment/cell_view.hpp>
#include <fo4/environment/environment.hpp>
#include <fo4/environment/world.hpp>
#include <fo4/formid.hpp>
#include <fo4/plugin/container.hpp>
#include <fo4/plugin/mutation.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/plugin/visit.hpp>
#include <fo4/plugin/writer.hpp>
#include <fo4/previs/bake_oc.hpp>
#include <fo4/previs/cdx.hpp>
#include <fo4/previs/csg.hpp>
#include <fo4/previs/precombine_plan.hpp>
#include <fo4/previs/stamp.hpp>
#include <fo4/previs/uvd.hpp>
#include <fo4/previs/uvd_tile_build.hpp>
#include <fo4/nif/nif.hpp>
#include <fo4/nif/physics_nif.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "RadiclePluginPrevisAssets.hpp"
#include "RadiclePluginPrevisIo.hpp"
#include "RadiclePluginPrevisPrep.hpp"
#include "RadiclePluginPrevisTomeLedger.hpp"
#include "RadiclePluginSupport.hpp"

namespace fo4ide::plugin {

namespace {

// Constraint: retail ships 0003C0F0.UVD and Windows matches names case-insensitively, so an existing name keeps its casing.
std::filesystem::path existingFileIgnoringCase(const std::filesystem::path& dir, const std::string& name) {
    std::error_code error;
    if (!std::filesystem::is_directory(dir, error)) return {};
    for (std::filesystem::directory_iterator it(dir, error), end; !error && it != end; it.increment(error)) {
        if (!it->is_regular_file(error)) continue;
        const auto candidate = it->path().filename().string();
        if (candidate.size() != name.size()) continue;
        bool same = true;
        for (std::size_t index = 0; index < candidate.size() && same; ++index) {
            same = std::tolower(static_cast<unsigned char>(candidate[index])) ==
                   std::tolower(static_cast<unsigned char>(name[index]));
        }
        if (same) return it->path();
    }
    return {};
}

std::uint16_t packedDateToday() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    const auto year = static_cast<std::uint32_t>(std::clamp(local.tm_year + 1900, 2000, 2031));
    const auto month = static_cast<std::uint32_t>(std::clamp(local.tm_mon + 1, 1, 12));
    const auto day = static_cast<std::uint32_t>(std::clamp(local.tm_mday, 1, 31));
    return static_cast<std::uint16_t>(((year - 2000) << 9) | (month << 5) | day);
}

// Constraint: the cell stamp must be at least the newest same-plugin child version date.
std::uint16_t previsStampForCell(const fo4::env::Environment& env, const fo4::FormKey& key) {
    std::uint16_t stamp = packedDateToday();
    const auto* winner = env.winner(key);
    if (winner == nullptr || winner->record == nullptr) return stamp;
    stamp = std::max(stamp, winner->record->header.timestamp);
    for (const auto& child : env.cell_children(key)) {
        if (child.ref == nullptr || child.ref->record == nullptr) continue;
        if (child.ref->plugin != winner->plugin) continue;
        if (fo4::is_partial_form(*child.ref->record)) continue;
        stamp = std::max(stamp, child.ref->record->header.timestamp);
    }
    return stamp;
}

// Constraint: retail names every previs file with eight uppercase hex digits, so a case-sensitive lookup must find the same name.
std::string hex8(std::uint32_t value) {
    char buffer[9]{};
    std::snprintf(buffer, sizeof(buffer), "%08X", value);
    return buffer;
}

bool recordHasSubrecord(const fo4::Plugin& plugin, const fo4::FormKey& key, fo4::Signature signature) {
    const fo4::Record* record = nullptr;
    fo4::visit_records(plugin, [&](const fo4::Record& candidate) {
        if (record != nullptr || candidate.header.signature != fo4::Signature::from_string("CELL") ||
            candidate.form_key != key) {
            return;
        }
        record = &candidate;
    });
    if (record == nullptr) return false;
    return std::ranges::any_of(record->subrecords,
                               [&](const fo4::Subrecord& sub) { return sub.signature == signature; });
}

constexpr std::string_view kNoWorldspaceCdxNote = "the exterior cell has no worldspace; no CDX row was written";

// Constraint: every generated cell needs its CDX row, an exterior cell without a worldspace included, so dry-run and apply both report the no-worldspace CDX limitation.
void attachGenerateCompleteness(nlohmann::json& result,
                                const fo4::previs::PrecombinePlan& plan,
                                bool cdxWritten,
                                bool tomeWritten,
                                const std::string& cdxNote) {
    nlohmann::json limitations = nlohmann::json::array();
    if (plan.skipped.foreign_plugin > 0) {
        limitations.push_back("foreign_plugin_refs:" + std::to_string(plan.skipped.foreign_plugin));
    }
    if (plan.skipped.unresolved_plugin > 0) {
        limitations.push_back("unresolved_plugin_refs:" + std::to_string(plan.skipped.unresolved_plugin));
    }
    if (!cdxWritten) {
        limitations.push_back(cdxNote.empty() ? "cdx_row_missing" : cdxNote);
    }
    if (!tomeWritten) {
        limitations.push_back("tome_not_written");
    }
    if (!limitations.empty()) {
        result["limitations"] = std::move(limitations);
        result["complete"] = false;
        result["ok"] = false;
    } else {
        result["complete"] = true;
        result["ok"] = true;
    }
}

}  // namespace

Result<nlohmann::json> RadiclePluginBackend::generateCellPrevis(const nlohmann::json& arguments) {
    const auto patchName = support::stringArgument(arguments, 0);
    const auto cellText = support::stringArgument(arguments, 1);
    const auto sourceName = support::stringArgument(arguments, 2);
    const auto shellText = support::stringArgument(arguments, 3);
    // Constraint: the CK groups by material and covers every renderable ref, so a min-instance filter is not offered.
    constexpr std::size_t minInstances = 1;
    // Constraint: the output root is read once under the read lock, so a settings save cannot split one run across two roots.
    std::filesystem::path configuredRoot;
    {
        OpLock readLock(*this, OpLock::Mode::Read);
        configuredRoot = previsOutputRoot_;
    }
    // Constraint: a caller that redirected the output root did not ask for the data root to change, so the tome patch follows the configured root and only an explicit patch_into_data opts back into writing the game's Data folder.
    bool apply = true, copyTome = false, patchIntoData = configuredRoot.empty();
    support::tryBoolArgument(arguments, 5, apply); support::tryBoolArgument(arguments, 6, copyTome);
    // Constraint: the IDE action patches the tome into the data root by default, so a caller that only wants files opts out.
    support::tryBoolArgument(arguments, 8, patchIntoData);
    // Constraint: a generate request pins the same per-object ratios as a plan request, so arg 7 carries them.
    const auto lodTargets =
        arguments.size() > 7 ? fo4::previs::lod_targets_from_json(arguments[7]) : fo4::previs::PrecombineLodTargets{};
    if (patchName.empty() || cellText.empty()) {
        return std::unexpected(Error{"PrevisGenerateArgs", "generate requires patch_plugin and cell"});
    }
    fo4::FormKey key;
    try {
        key = fo4::FormKey::parse(cellText);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InvalidFormKey", error.what()});
    }

    std::string winnerName = sourceName;
    fo4::previs::PrecombinePlan plan;
    std::optional<fo4::FormKey> worldspaceKey;
    std::optional<fo4::env::CellGrid> cellGrid;
    // Constraint: a CDX row's kind and opaque words are copied or reported, never invented.
    std::vector<fo4::previs::CdxRow> cdxRows;
    std::string cdxNote;
    std::string cdxSource;
    std::optional<fo4::FormKey> clusterRoot;
    try {
        OpLock readLock(*this, OpLock::Mode::Read);
        const auto env = borrowedEnvironment();
        if (env == nullptr) {
            return std::unexpected(Error{"NoEnvironment", "Load an MO2 profile before generating previs."});
        }
        if (winnerName.empty()) {
            const auto winner = support::winningPlugin(plugins_, key);
            if (!winner) return std::unexpected(winner.error());
            winnerName = (*winner)->key.filename();
        }
        // Constraint: the plan must see the cell's winning plugin; a patch plugin owns none of the placed statics.
        auto built = fo4::previs::build_precombine_plan(*env, key, winnerName, minInstances, lodTargets);
        if (!built.has_value()) {
            return std::unexpected(Error{
                "PrevisGeneratePlan",
                "Cell is missing or could not be planned; generation needs a cell with placed STAT references."});
        }
        plan = std::move(*built);
        // Constraint: a cell whose winning RVIS names another cell shares that root's tome, and one run writes one cell's tome, so an apply refuses a cluster member before any file is built, and a dry run still reports the plan with the member as a limitation.
        if (const auto* cellRef = env->winner(key); cellRef != nullptr) {
            const auto decoded = fo4::env::decode_previs_root(*env, *cellRef);
            if (decoded.root.has_value() && *decoded.root != key) clusterRoot = *decoded.root;
        }
        if (apply && clusterRoot.has_value()) {
            return std::unexpected(Error{
                "PrevisGenerateCluster",
                "Cell " + key.to_string() + " shares the previs of cluster root " + clusterRoot->to_string() +
                    "; generating one member of a previs cluster is not supported, so nothing was written."});
        }
        if (const auto summary = fo4::env::describe_cell(*env, key)) {
            worldspaceKey = summary->worldspace;
            cellGrid = summary->grid;
        }
    } catch (const std::exception& error) {
        return std::unexpected(Error{"PrevisGeneratePlan", std::string("planning failed: ") + error.what()});
    }
    if (plan.groups.empty()) {
        return std::unexpected(Error{
            "PrevisGeneratePlan",
            "No STAT model group reaches min_instances in this cell; nothing can be precombined."});
    }
    if (dataDirectory_.empty()) {
        return std::unexpected(Error{"PrevisDataDir", "no Data folder is configured; set one in Settings"});
    }
    std::filesystem::path pluginPath;
    {
        OpLock readLock(*this, OpLock::Mode::Read);
        const auto* loadedPlugin = findPlugin(patchName);
        if (loadedPlugin != nullptr) pluginPath = loadedPlugin->path;
        // Constraint: a patch that cannot take the CELL override must be refused before any mesh is read or asset is written.
        if (apply) {
            if (loadedPlugin == nullptr) {
                return std::unexpected(Error{"TargetPluginNotLoaded", "Patch plugin is not loaded: " + patchName});
            }
            if (const auto refused = previsPatchError(*loadedPlugin)) return std::unexpected(*refused);
        }
    }
    // Constraint: authored assets are staged outside the game's Data folder whenever a write root is configured.
    const auto outputRoot = previs_assets::resolveOutputRoot(dataDirectory_, configuredRoot, pluginPath);

    const auto patchStem = std::filesystem::path(patchName).stem().string();
    const auto csgPath = outputRoot / (patchStem + " - Geometry.csg");

    // Constraint: a dry run reports the plan, so it must not need the STAT meshes to be readable.
    std::optional<previs_prep::Prepared> prepared;
    if (apply) {
        // Constraint: an earlier cell's _OC files name offsets in this container, so it is extended and never rewritten, and one that cannot be read is refused.
        const auto existingCsg = readBinaryFile(csgPath);
        std::error_code sizeError;
        if (existingCsg.empty() && std::filesystem::file_size(csgPath, sizeError) > 0 && !sizeError) {
            return std::unexpected(Error{"PrevisGenerateCsg", "the existing geometry container " + csgPath.string() +
                                                                 " could not be read, so it was not replaced"});
        }
        if (!existingCsg.empty()) {
            const auto csgIssues = fo4::previs::validate_csg(existingCsg);
            if (!csgIssues.empty()) {
                return std::unexpected(Error{"PrevisGenerateCsg", "the existing geometry container " + csgPath.string() +
                                                                     " cannot be extended: " + csgIssues.front()});
            }
        }
        // Constraint: vanilla STAT meshes live inside the general archives, so the plan is prepared through a Data reader.
        auto loaded = previs_prep::prepare(dataDirectory_, plan, previs_assets::archiveModelReader(dataDirectory_), existingCsg);
        if (!loaded) return std::unexpected(Error{"PrevisGenerateMesh", std::move(loaded.error())});
        plan = std::move(loaded->plan);
        prepared = std::move(*loaded);
    }
    const auto csgHash = fo4::previs::csg_file_hash(patchName);
    // Constraint: the material list reports what the runs grouped by, empty until a real run prepares meshes.
    const auto materials = prepared ? prepared->materials : std::vector<std::string>{};
    const auto cellLow24 = key.local().value() & 0xFFFFFFu;
    const auto cellPrefix = hex8(cellLow24);
    const auto pluginFolder = std::filesystem::path(patchName).filename().string();
    // Constraint: the CK ships one _OC per shader material, so a material run of the ordered plan is one file.
    const auto runs = previs_assets::materialRuns(prepared ? &*prepared : nullptr, plan.groups.size());
    std::vector<std::uint32_t> groupIds(plan.groups.size(), 0);
    std::vector<std::uint32_t> ocIds;
    std::vector<std::filesystem::path> outputs;
    ocIds.reserve(runs.size());
    outputs.reserve(runs.size());
    nlohmann::json groups = nlohmann::json::array();
    for (std::size_t run = 0; run < runs.size(); ++run) {
        const auto ocId = static_cast<std::uint32_t>(run + 1);
        const auto idText = hex8(ocId);
        // Constraint: the _OC file id must equal the XCRI mesh id, or the cell points at no NIF.
        const auto output = outputRoot / "Meshes" / "PreCombined" / pluginFolder /
                            (cellPrefix + '_' + idText + "_OC.NIF");
        ocIds.push_back(ocId);
        outputs.push_back(output);
        std::size_t instances = 0;
        for (const auto index : runs[run].groupIndices) {
            groupIds[index] = ocId;
            instances += plan.groups[index].instances.size();
        }
        groups.push_back({{"ocId", ocId},
                          {"ocIdText", idText},
                          {"material", runs[run].material},
                          {"objects", runs[run].groupIndices.size()},
                          {"instanceCount", instances},
                          {"path", output.string()}});
    }
    std::size_t referenceCount = 0;
    for (const auto& group : plan.groups) referenceCount += group.instances.size();
    if (referenceCount == 0) {
        return std::unexpected(Error{"PrevisGeneratePlan", "the plan produced no XCRI reference pairs"});
    }

    std::uint16_t stamp = 0;
    std::vector<std::string> pluginLoadOrder;
    std::vector<std::filesystem::path> assetRoots;
    {
        OpLock readLock(*this, OpLock::Mode::Read);
        const auto env = borrowedEnvironment();
        if (env == nullptr) {
            return std::unexpected(Error{"NoEnvironment", "The environment was dropped; load the profile again."});
        }
        stamp = previsStampForCell(*env, key);
        pluginLoadOrder.reserve(plugins_.size());
        for (const auto& loaded : plugins_) pluginLoadOrder.push_back(loaded.name);
        assetRoots = assetRootsLowToHigh_;
    }

    auto result = nlohmann::json{{"cell", key.to_string()},
                                 {"plugin", patchName},
                                 {"dryRun", !apply},
                                 {"pcmbStamp", stamp},
                                 {"visiStamp", stamp},
                                 {"meshIds", ocIds.size()},
                                 {"references", referenceCount},
                                 {"groups", groups}};
    // Constraint: the run must name the geometry it consumed, so no caller reads a tome page for occluder geometry.
    result["geometry"] = {{"source", std::string(fo4::previs::k_occluder_geometry_source)},
                          {"container", csgPath.string()},
                          {"readFrom", "planned STAT model shapes decoded from Data, packed into this CSG and addressed "
                                        "by the baked _OC.NIF Shared extras"}};
    result["planSkipped"] = {{"foreignPlugin", plan.skipped.foreign_plugin},
                             {"unresolvedPlugin", plan.skipped.unresolved_plugin}};
    if (!apply) {
        // Constraint: a dry run looks up no CDX row and builds no tome, so it lists only the limits already known to stop apply: foreign references, an exterior cell without a worldspace, and a previs cluster member.
        const bool cdxKnownMissing = !plan.interior && !worldspaceKey;
        attachGenerateCompleteness(result, plan, !cdxKnownMissing, true,
                                   cdxKnownMissing ? std::string(kNoWorldspaceCdxNote) : std::string{});
        if (!result.contains("limitations")) {
            result["limitations"] = nlohmann::json::array();
        }
        if (clusterRoot.has_value()) result["limitations"].push_back("previs_cluster_member:" + clusterRoot->to_string());
        result["limitations"].push_back("dry_run_only");
        result["complete"] = false;
        result["ok"] = false;
        return result;
    }

    // Constraint: a rejected shell must fail before any asset ships, so every _OC is built in memory first.
    const auto& models = prepared->models;
    // Constraint: a shipped _OC lives inside a general archive, so the template resolves through the Data reader, and a caller that names none gets the cell's own templates.
    const auto shells = previs_assets::ocShells(
        dataDirectory_, shellText, cellPrefix,
        {std::filesystem::path(winnerName).filename().string(), std::string(key.owner().filename())});
    if (shells.empty()) {
        return std::unexpected(
            Error{"PrevisGenerateShell",
                  shellText.empty()
                      ? "no _OC.NIF template for cell " + cellPrefix + " was found under Data; name the template explicitly"
                      : "could not read the _OC template " + shellText + " as a loose file or from the Data archives"});
    }
    std::vector<std::vector<std::byte>> baked(runs.size());
    std::string usedShell = shells.front().path;

    std::size_t physicsTriangles = 0;
    std::vector<std::array<float, 9>> occluderTriangles;
    const auto physicsMeshes = previs_prep::placed_physics_meshes(plan, models, physicsTriangles, occluderTriangles);

    for (std::size_t run = 0; run < runs.size(); ++run) {
        std::vector<fo4::previs::SharedGeomObjectWrite> objects;
        objects.reserve(runs[run].groupIndices.size());
        for (const auto index : runs[run].groupIndices) {
            fo4::previs::SharedGeomObjectWrite object;
            object.data_offset = prepared->csg.rows[index].data_offset;
            object.vertex_desc = models[index].mesh.vertex_desc;
            object.vertex_count = models[index].mesh.vertex_count;
            object.triangle_count = prepared->csg.rows[index].lod_triangle_count;
            object.records.reserve(plan.groups[index].instances.size());
            for (const auto& instance : plan.groups[index].instances) {
                fo4::previs::OcPlacement placement;
                placement.source_nif = models[index].path;
                placement.xform = fo4::previs::placement_transform_from_euler(
                    instance.transform.x, instance.transform.y, instance.transform.z,
                    instance.transform.rx, instance.transform.ry, instance.transform.rz, instance.scale);
                object.records.push_back(fo4::previs::record_for_placement(
                    placement, models[index].local_center, models[index].local_radius));
            }
            objects.push_back(std::move(object));
        }
        const auto firstGroup = runs[run].groupIndices.front();
        // Constraint: a shipped STAT mesh has no loose file, so the bake takes the bytes the plan measured.
        // Constraint: a template that cannot carry the placements must not fail the run, because another name for the same cell may be a measured shell.
        std::optional<std::vector<std::byte>> image;
        for (const auto& shell : shells) {
            auto attempt = fo4::previs::bake_oc_shared_objects_image(
                models[firstGroup].nif_bytes, shell.bytes, csgHash, objects);
            if (!attempt.has_value()) continue;
            image = std::move(*attempt);
            usedShell = shell.path;
            break;
        }
        if (!image.has_value()) {
            return std::unexpected(Error{
                "PrevisGenerateBake",
                "Could not bake material " + runs[run].material +
                    " into a _OC.NIF that addresses the patch's own geometry; none of the " +
                    std::to_string(shells.size()) + " templates tried for cell " + cellPrefix +
                    " is a measured _OC shell."});
        }
        baked[run] = std::move(*image);
    }

    std::filesystem::path physicsPath;
    std::vector<std::byte> physicsBytes;
    if (physicsTriangles > 0) {
        physicsPath = outputRoot / (cellPrefix + "_Physics.NIF");
        try {
            physicsBytes = fo4::nif::build_physics_nif(cellPrefix + "_Physics", physicsMeshes);
        } catch (const std::exception& error) {
            return std::unexpected(Error{
                "PrevisGeneratePhysics", std::string("the cell physics NIF could not be built: ") + error.what()});
        }
        if (physicsBytes.empty()) {
            return std::unexpected(Error{"PrevisGeneratePhysics", "the cell physics NIF came out empty"});
        }
    }
    // Constraint: the cell ships its own tome built from this run's geometry, never a copied vanilla cluster.
    auto tomePath = fo4::previs::uvd_tome_path(outputRoot, pluginFolder, cellLow24);
    // Constraint: an existing tome keeps its own casing, so a differently cased name is overwritten and no second file appears.
    if (const auto existingTome = existingFileIgnoringCase(tomePath.parent_path(), tomePath.filename().string());
        !existingTome.empty()) {
        tomePath = existingTome;
    }
    previs_prep::TomeRequest tomeRequest;
    tomeRequest.path = tomePath;
    tomeRequest.bodies = physicsMeshes;
    // Constraint: only instances whose base object may occlude contribute triangles, so a NonOccluder object culls nothing.
    tomeRequest.occluders = std::move(occluderTriangles);
    tomeRequest.copy_vanilla = copyTome;
    const auto vanillaLow24 = key.local().value() & 0xFFFFFFu;
    // Constraint: a shipped tome sits under the plugin that last generated the cell's previs, which is the winning override, or the cell owner when no override regenerated it, so the winner's Vis folder is searched first.
    std::vector<std::string> tomeFolders{std::filesystem::path(winnerName).filename().string()};
    if (const auto ownerFolder = std::filesystem::path(key.owner().filename()).string();
        support::asciiLower(ownerFolder) != support::asciiLower(tomeFolders.front())) {
        tomeFolders.push_back(ownerFolder);
    }
    tomeRequest.vanilla_tome = fo4::previs::uvd_tome_path(dataDirectory_, tomeFolders.front(), vanillaLow24);
    if (copyTome) {
        for (const auto& folder : tomeFolders) {
            auto bytes = previs_assets::readVisTome(dataDirectory_, folder, vanillaLow24, pluginLoadOrder, assetRoots,
                                                    configuredRoot);
            if (!bytes) continue;
            if (bytes->empty()) {
                return std::unexpected(Error{"PrevisGenerateTome", "copy_vanilla_tome: source tome is empty in " + folder});
            }
            tomeRequest.vanilla_tome = fo4::previs::uvd_tome_path(dataDirectory_, folder, vanillaLow24);
            tomeRequest.vanilla_tome_bytes = std::move(*bytes);
            break;
        }
    }
    // Constraint: an exterior cell covers its own 4096-unit grid square, which an interior does not have.
    if (!plan.interior && cellGrid) {
        constexpr float cellSize = 4096.0f;
        const auto minX = static_cast<float>(cellGrid->x) * cellSize;
        const auto minY = static_cast<float>(cellGrid->y) * cellSize;
        tomeRequest.cell_xy = std::array<float, 4>{minX, minY, minX + cellSize, minY + cellSize};
    }
    auto tome = previs_prep::build_tome(tomeRequest);
    if (!tome) return std::unexpected(Error{"PrevisGenerateTome", tome.error()});

    // Constraint: every file below must be written by one FileTransaction after every refusal, so a failed run leaves no file behind.
    FileTransaction files;
    for (std::size_t run = 0; run < runs.size(); ++run) files.write(outputs[run], std::move(baked[run]));
    const auto csgRows = prepared->csg.rows.size();
    const auto csgSize = prepared->csg.bytes.size();
    files.write(csgPath, std::move(prepared->csg.bytes));
    if (!physicsPath.empty()) files.write(physicsPath, std::move(physicsBytes));

    const nlohmann::json csgWritten = {{"path", csgPath.string()}, {"rows", csgRows}, {"bytes", csgSize}};
    result["materials"] = materials;
    // Constraint: the report must name the template that produced the _OC files, or a wrong-cell shell cannot be spotted after the fact.
    result["shell"] = {{"path", usedShell}, {"autoResolved", shellText.empty()}, {"candidates", shells.size()}};
    nlohmann::json physicsWritten;
    if (!physicsPath.empty()) {
        physicsWritten = {{"path", physicsPath.string()}, {"triangles", physicsTriangles}};
    }
    auto warnings = nlohmann::json::array();
    if (tome->result.copied_vanilla) warnings.push_back("copy_vanilla_tome: reused a shipped cluster tome instead of generating one");
    for (const auto& warning : tome->result.warnings) warnings.push_back(warning);
    if (!warnings.empty()) result["warnings"] = std::move(warnings);
    // Constraint: the Data patch step requires a tome this run wrote or copied, or it would ship a stale file.
    const bool tomeWritten = tome->result.copied_vanilla || tome->result.objects > 0;
    nlohmann::json uvdWritten;
    if (tome->result.objects > 0) {
        uvdWritten = {{"path", tomePath.string()},
                      {"objects", tome->result.objects},
                      {"culled", tome->result.culled},
                      {"bytes", tome->result.bytes}};
    }
    if (!tomeWritten && patchIntoData) result["warnings"].push_back("no tome was written, so the Data folder's tome for this cell was left as it was");
    nlohmann::json patchedWritten;
    std::optional<std::filesystem::path> dataTarget;
    // Constraint: one IDE action must leave the tome loadable, so the run patches it into the data root, and build_tome already validated the bytes it writes there.
    if (patchIntoData && tomeWritten && !dataDirectory_.empty()) {
        const auto dataTome = fo4::previs::uvd_tome_path(dataDirectory_, pluginFolder, cellLow24);
        const auto existing = existingFileIgnoringCase(dataTome.parent_path(), dataTome.filename().string());
        const auto target = existing.empty() ? dataTome : existing;
        // Constraint: an output root that is the data root names one file, so it must be written once.
        if (support::asciiLower(target.generic_string()) != support::asciiLower(tomePath.generic_string())) {
            dataTarget = target;
        }
        patchedWritten = {{"path", target.string()}, {"bytes", tome->bytes.size()}, {"issues", 0}};
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
    // Constraint: the ledger sits beside the patch plugin like the CDX, so it follows the patch, and a tome is recognised again only when the root that holds it is used again.
    GeneratedTomeLedger tomeLedger((pluginPath.empty() ? dataDirectory_ : pluginPath.parent_path()) /
                                   (patchStem + " - Generated tomes.json"));
    const auto tomeOrigin = tome->result.copied_vanilla ? TomeOrigin::copied : TomeOrigin::generated;
    if (const auto refused = stageGeneratedTomes(files, tomeLedger, tomePath, dataTarget, tome->bytes, tomeOrigin)) {
        return std::unexpected(Error{"PrevisGenerateTome", *refused});
    }
    try {
        auto mutated = patch->plugin;
        if (auto copied = copyCellOverrideIntoPlugin(mutated, source->plugin, key); !copied) {
            return std::unexpected(copied.error());
        }
        if (fo4::find_record(mutated, key) == nullptr) {
            return std::unexpected(
                Error{"CopyOverrideFailed", "Could not copy CELL override into patch"});
        }
        std::optional<nlohmann::json> cdxWritten;
// Constraint: XCRI reference words are file Form-IDs, so every planned REFR's owner must be a master the patch declares, in load order (plugins_ order is load order).
        const auto loadIndex = [this](const fo4::PluginKey& pluginKey) -> std::optional<std::size_t> {
            for (std::size_t index = 0; index < plugins_.size(); ++index) {
                if (plugins_[index].plugin.key == pluginKey) return index;
            }
            return std::nullopt;
        };
        std::vector<fo4::MasterDescriptor> masters;
        masters.reserve(mutated.header.masters.size() + 1);
        for (const auto& master : mutated.header.masters) masters.push_back({master.key, master.kind});
        const auto patchIndex = loadIndex(mutated.key);
        bool mastersAdded = false;
        // Constraint: XCRI words and the CDX row name the cell owner and world, so both are masters.
        std::vector<fo4::PluginKey> requiredOwners;
        requiredOwners.reserve(1 + plan.groups.size());
        if (worldspaceKey) requiredOwners.push_back(worldspaceKey->owner());
        for (const auto& group : plan.groups) {
            for (const auto& instance : group.instances) requiredOwners.push_back(instance.reference.owner());
        }
        for (const auto& owner : requiredOwners) {
            if (owner == mutated.key) continue;
            if (std::ranges::any_of(masters, [&](const auto& master) { return master.key == owner; })) {
                continue;
            }
            const auto ownerIndex = loadIndex(owner);
            const auto* loaded = findPlugin(owner.filename());
            if (!ownerIndex.has_value() || loaded == nullptr) {
                return std::unexpected(Error{
                    "PrevisGenerateMaster",
                    "Reference owner is not loaded: " + std::string(owner.filename())});
            }
            if (patchIndex.has_value() && *patchIndex <= *ownerIndex) {
                return std::unexpected(Error{
                    "MasterOrder",
                    "Master " + std::string(owner.filename()) + " does not load before " + patchName +
                        "; a master must precede the plugin that declares it"});
            }
            masters.push_back(
                {owner, fo4::classify_plugin_kind(loaded->plugin.header.tes4_flags, loaded->plugin.key)});
            mastersAdded = true;
        }
        if (mastersAdded) {
            mutated = fo4::remap_plugin_masters(std::move(mutated), std::move(masters));
            std::vector<fo4::PluginKey> loadOrder;
            loadOrder.reserve(plugins_.size());
            for (const auto& loaded : plugins_) loadOrder.push_back(loaded.plugin.key);
            const bool everyMasterLoaded =
                std::ranges::all_of(mutated.header.masters, [&](const auto& master) {
                    return std::ranges::find(loadOrder, master.key) != loadOrder.end();
                });
            if (everyMasterLoaded) mutated = fo4::sort_plugin_masters(std::move(mutated), loadOrder);
        }
        const auto xcri = fo4::previs::xcri_from_group_ids(plan, groupIds, fo4::plugin_form_id_context(mutated));
        const auto patchContext = fo4::plugin_form_id_context(mutated);
        fo4::previs::StampRequest request{
            .pcmb_stamp = stamp, .visi_stamp = stamp, .xcri = xcri, .rvis_root = std::nullopt};
        if (!plan.interior && !recordHasSubrecord(mutated, key, fo4::Signature::from_string("RVIS"))) {
            request.rvis_root = fo4::encode_plugin_form_id(key, patchContext);
        }
        const auto status = fo4::previs::stamp_cell_previs(mutated, key, request);
        if (status != fo4::previs::StampStatus::ok) {
            return std::unexpected(
                Error{"PrevisGenerateFailed", "Could not write PCMB/VISI/XCRI on the CELL override"});
        }
        setNoPreVisFlag(mutated, key, false);
        const auto patchFolder = patch->path.empty() ? dataDirectory_ : patch->path.parent_path();
        const auto cdxPath = patchFolder / (std::filesystem::path(patchName).stem().string() + ".cdx");
        // Constraint: a plugin that ships a cell's geometry must ship the CDX row that names it.
        const auto winnerStem = std::filesystem::path(winnerName).stem().string();
        if (!plan.interior && !worldspaceKey) {
            cdxNote = std::string(kNoWorldspaceCdxNote);
        } else {
            const auto& rowKey = plan.interior ? key : *worldspaceKey;
            const auto grid = plan.interior || !cellGrid
                                  ? 0u
                                  : fo4::previs::encode_cdx_grid(fo4::previs::CdxGrid{cellGrid->x, cellGrid->y});
            // Constraint: a CDX row is keyed in its own plugin's numbering, so a row copied from another index must be looked up with that index's context.
            std::string searched;
            const auto tryIndex = [&](const auto& indexPlugin, const std::string& stem) {
                if (!cdxRows.empty()) return;
                const auto file = readCdxFile(dataDirectory_ / (stem + ".cdx"));
                if (!file) return;
                // Constraint: an index whose plugin does not master the cell owner cannot hold a row for it, so it is skipped, not fatal.
                std::uint32_t formValue = 0;
                try {
                    formValue = fo4::encode_plugin_form_id(rowKey, fo4::plugin_form_id_context(indexPlugin)).value();
                } catch (const std::exception&) {
                    return;
                }
                if (!searched.empty()) searched += ", ";
                searched += stem + ".cdx";
                fo4::previs::CdxCellKey lookup;
                lookup.grid = grid;
                lookup.form = formValue;
                const auto found = fo4::previs::find_cdx_rows(*file, lookup);
                if (found.empty()) return;
                // Constraint: the copied form must be the patch's own numbering, so it is encoded only once a row exists to copy.
                const auto patchForm =
                    fo4::encode_plugin_form_id(rowKey, fo4::plugin_form_id_context(mutated)).value();
                for (const auto& row : found) {
                    cdxRows.push_back(fo4::previs::CdxRow{row.key, patchForm, row.value, row.extra});
                }
                cdxSource = stem + ".cdx";
            };
            // Constraint: the winner's index is searched first, then every other loaded index, because a base cell's row survives an override whose own plugin ships none.
            tryIndex(source->plugin, winnerStem);
            for (const auto& loaded : plugins_) {
                if (!cdxRows.empty()) break;
                if (loaded.plugin.key == source->plugin.key) continue;
                tryIndex(loaded.plugin, std::filesystem::path(loaded.plugin.key.filename()).stem().string());
            }
            if (cdxRows.empty()) {
                cdxNote = "no readable CDX under Data has a row for this cell (" +
                          (searched.empty() ? std::string("no index was readable") : searched) +
                          "); its kind and opaque words are unmeasured, so no row was written";
            }
        }
        if (!cdxRows.empty()) {
            const auto existingCdx = readCdxFile(cdxPath);
            // Constraint: an existing index that cannot be read must be refused, never replaced by a file holding only this cell's row.
            std::error_code cdxSizeError;
            if (!existingCdx && std::filesystem::file_size(cdxPath, cdxSizeError) > 0 && !cdxSizeError) {
                return std::unexpected(Error{"PrevisGenerateCdx", "the existing CDX " + cdxPath.string() +
                                                                     " could not be read, so it was not replaced"});
            }
            auto file = existingCdx.value_or(fo4::previs::CdxFile{});
            for (const auto& row : cdxRows) fo4::previs::merge_cdx_row(file, row);
            auto bytes = fo4::previs::write_cdx(file);
            const auto issues = fo4::previs::validate_cdx(bytes);
            if (!issues.empty()) {
                return std::unexpected(
                    Error{"PrevisGenerateCdx", "merged CDX failed validation: " + issues.front()});
            }
            files.write(cdxPath, std::move(bytes));
            cdxWritten = nlohmann::json{{"path", cdxPath.string()},
                                        {"rows", file.rows.size()},
                                        {"source", cdxSource}};
        }
        if (!cdxNote.empty()) result["warnings"].push_back(cdxNote);
        attachGenerateCompleteness(result, plan, !cdxRows.empty(), tomeWritten, cdxNote);
        if (result.value("complete", true) == false) {
            return result;
        }
        // Constraint: the plugin edit must be staged only after every file is in place, so a failed write never leaves a CELL pointing at missing assets.
        if (const auto failed = files.commit()) {
            return std::unexpected(Error{"PrevisGenerateWrite", "could not write the previs files: " + *failed});
        }
        stagePluginMutationLocked(*patch, std::move(mutated));
        dropBorrowedEnvironmentOnlyLocked();
        result["csg"] = csgWritten;
        if (!physicsWritten.is_null()) result["physics"] = physicsWritten;
        if (!uvdWritten.is_null()) result["uvd"] = uvdWritten;
        if (!patchedWritten.is_null()) result["patched"] = patchedWritten;
        if (cdxWritten.has_value()) result["cdx"] = *cdxWritten;
    } catch (const std::exception& error) {
        return std::unexpected(Error{"PrevisGenerateFailed", error.what()});
    }
    return result;
}

}  // namespace fo4ide::plugin

