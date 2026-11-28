#include <fo4ide/hkx/HkxService.hpp>

#include <fo4/hkx/animation.hpp>
#include <fo4/hkx/behavior_graph.hpp>
#include <fo4/hkx/channel_audit.hpp>
#include <fo4/hkx/object_graph.hpp>
#include <fo4/hkx/packfile.hpp>
#include <fo4/hkx/skeleton.hpp>
#include <fo4/hkx/symbol_search.hpp>
#include <fo4/hkx/write.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace fo4ide::hkx {
namespace {

using fo4::hkx::HkxPackfile;
using fo4::hkx::HkxStatus;

constexpr std::array<std::string_view, 10> kStatusNames{
    "Ok", "Truncated", "BadMagic", "UnsupportedLayout", "MissingSection",
    "BadSection", "FixupOutOfRange", "DuplicateFixup", "BadClassName", "MissingObject",
};

constexpr std::size_t kObjectLimitDefault = 50;
constexpr std::size_t kObjectLimitMax = 200;
constexpr std::size_t kRowLimitDefault = 200;
constexpr std::size_t kRowLimitMax = 5000;
constexpr std::size_t kScanCap = 5000;
constexpr std::size_t kSiteLimit = 100;
constexpr std::size_t kTreeDepthDefault = 32;
constexpr const char* kInspectNote =
    "objects are addressed by packfile index; a field shows its decoded value where the layout table states a type and width, every field keeps its raw hex capped at 16 bytes, and a field that is not decoded says why in reason (members whose width the tables do not state such as ulong, variant and matrix4, members the game never stores, classes with no layout); structs, vectors, matrices and arrays of them are decoded, enum values carry their name, and a half is shown as its raw 16 bits";

constexpr const char* kTreeNote =
    "objects are addressed by packfile index; summary shows the decoded string members of classes with a generated layout, and a class with no layout shows its class and offset only";

std::string statusName(HkxStatus status) {
    const auto at = static_cast<std::size_t>(status);
    return at < kStatusNames.size() ? std::string(kStatusNames[at]) : std::string("Unknown");
}

Error readError(std::string_view step, HkxStatus status) {
    return Error{"HkxRead", std::string(step) + " failed: " + statusName(status)};
}

std::size_t clampLimit(std::size_t limit, std::size_t fallback, std::size_t ceiling) {
    return limit == 0 ? fallback : std::min(limit, ceiling);
}

std::string hexSignature(std::uint32_t signature) {
    std::ostringstream text;
    text << "0x" << std::hex << std::setfill('0') << std::setw(8) << signature;
    return text.str();
}

Result<std::vector<std::byte>> readFileBytes(const std::filesystem::path& path) {
    if (path.empty()) return std::unexpected(Error{"InvalidPath", "HKX path is empty"});
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        return std::unexpected(Error{"FileNotFound", "HKX not found: " + path.string()});
    }
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) return std::unexpected(Error{"FileNotFound", "Could not open " + path.string()});
    const auto size = static_cast<std::size_t>(stream.tellg());
    std::vector<std::byte> bytes(size);
    stream.seekg(0);
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    if (!stream) return std::unexpected(Error{"ReadFailed", "Could not read " + path.string()});
    return bytes;
}

Result<void> openPackfile(const std::filesystem::path& path, HkxPackfile& packfile) {
    auto bytes = readFileBytes(path);
    if (!bytes) return std::unexpected(bytes.error());
    const auto status = fo4::hkx::read_packfile(*bytes, packfile);
    if (status == HkxStatus::Ok) return {};
    auto error = readError("packfile read", status);
    if (const auto reason = fo4::hkx::describe_packfile_refusal(*bytes); !reason.empty()) {
        error.message += " (" + reason + ")";
    }
    return std::unexpected(std::move(error));
}

std::string lowered(std::string_view text) {
    std::string out(text);
    for (char& ch : out) {
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    }
    return out;
}

// Invariant: a directory yields its .hkx files recursively in sorted path order, capped at kScanCap with truncated set.
Result<std::vector<std::filesystem::path>> hkxTargets(const std::filesystem::path& target, bool& truncated) {
    truncated = false;
    if (target.empty()) return std::unexpected(Error{"InvalidPath", "HKX path is empty"});
    std::error_code ec;
    std::vector<std::filesystem::path> files;
    if (std::filesystem::is_regular_file(target, ec)) {
        files.push_back(target);
        return files;
    }
    if (!std::filesystem::is_directory(target, ec)) {
        return std::unexpected(Error{"FileNotFound", "HKX file or folder not found: " + target.string()});
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(
             target, std::filesystem::directory_options::skip_permission_denied, ec)) {
        if (ec) break;
        std::error_code fileError;
        if (!entry.is_regular_file(fileError) || entry.path().extension() != ".hkx") continue;
        files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    if (files.size() > kScanCap) {
        files.resize(kScanCap);
        truncated = true;
    }
    return files;
}

nlohmann::json linkJson(const fo4::hkx::HkxObjectLink& link) {
    return nlohmann::json{{"source_object", link.source_object},
                          {"source_offset", link.source_offset},
                          {"member", link.member},
                          {"target_section", link.target_section},
                          {"target_offset", link.target_offset},
                          {"target_object", link.target_object},
                          {"target_class", link.target_class},
                          {"inner", link.inner}};
}

template <typename Rows, typename Emit>
nlohmann::json cappedRows(const Rows& rows, std::size_t limit, Emit emit) {
    auto out = nlohmann::json::array();
    for (std::size_t at = 0; at < rows.size() && at < limit; ++at) out.push_back(emit(rows[at]));
    return out;
}

}  // namespace

// Constraint: read-only; the object id is the packfile object index, and a field is decoded only where the layout table states its type, otherwise it keeps hex and a reason.
Result<nlohmann::json> HkxService::inspectObject(const std::filesystem::path& path, std::size_t object,
                                                 std::size_t limit) const {
    HkxPackfile packfile;
    if (auto opened = openPackfile(path, packfile); !opened) return std::unexpected(opened.error());
    fo4::hkx::HkxObjectLookup lookup;
    const auto status = fo4::hkx::lookup_object(packfile, object, lookup);
    if (status == HkxStatus::MissingObject) {
        return std::unexpected(Error{"ObjectNotFound", "object " + std::to_string(object) + " is not in the file, which has " +
                                                           std::to_string(packfile.objects.size()) + " objects"});
    }
    if (status != HkxStatus::Ok) return std::unexpected(readError("object read", status));

    const std::size_t shown = clampLimit(limit, kObjectLimitDefault, kObjectLimitMax);
    const auto& detail = lookup.detail;
    const auto fields = cappedRows(detail.fields, shown, [](const fo4::hkx::HkxObjectField& field) {
        return nlohmann::json{{"name", field.name}, {"offset", field.offset}, {"size", field.size}, {"hex", field.hex},
                              {"type", field.type},   {"value", field.value},   {"reason", field.reason}};
    });
    const bool undecoded = !detail.layout_known || std::any_of(detail.fields.begin(), detail.fields.end(),
                                                               [](const fo4::hkx::HkxObjectField& field) { return field.value.empty() || !field.reason.empty(); });
    return nlohmann::json{
        {"path", path.string()},
        {"object", detail.index},
        {"object_count", packfile.objects.size()},
        {"offset", detail.offset},
        {"signature", hexSignature(detail.signature)},
        {"class_name", detail.class_name},
        {"size", detail.size},
        {"layout_known", detail.layout_known},
        {"layout_size", detail.layout_size},
        {"fields", fields},
        {"field_total", detail.fields.size()},
        {"outgoing", cappedRows(lookup.outgoing, shown, linkJson)},
        {"outgoing_total", lookup.outgoing.size()},
        {"incoming", cappedRows(lookup.incoming, shown, linkJson)},
        {"incoming_total", lookup.incoming.size()},
        {"truncated", detail.fields.size() > shown || lookup.outgoing.size() > shown || lookup.incoming.size() > shown},
        {"partial", undecoded},
        {"note", kInspectNote},
    };
}

// Constraint: node names and clip animations come from the typed behavior model when the file has one; summary carries the decoded string members of any class with a layout.
Result<nlohmann::json> HkxService::objectTree(const std::filesystem::path& path, std::size_t depth,
                                              std::size_t limit) const {
    HkxPackfile packfile;
    if (auto opened = openPackfile(path, packfile); !opened) return std::unexpected(opened.error());
    fo4::hkx::HkxTreeQuery query;
    query.max_depth = depth == 0 ? kTreeDepthDefault : depth;
    query.max_nodes = clampLimit(limit, kRowLimitDefault, kRowLimitMax);
    fo4::hkx::HkxObjectTree tree;
    if (const auto status = fo4::hkx::build_object_tree(packfile, query, tree); status != HkxStatus::Ok) {
        return std::unexpected(readError("object tree", status));
    }

    std::map<std::size_t, std::string> names;
    std::map<std::size_t, std::string> animations;
    fo4::hkx::HkxGraphModel model;
    if (fo4::hkx::read_behavior_model(packfile, model) == HkxStatus::Ok) {
        for (const auto& node : model.nodes) names[node.object] = node.name;
        for (const auto& state : model.states) names[state.object] = state.name;
        for (const auto& clip : model.clips) {
            if (clip.node < model.nodes.size()) animations[model.nodes[clip.node].object] = clip.animation;
        }
    }

    auto nodes = nlohmann::json::array();
    for (std::size_t at = 0; at < tree.nodes.size(); ++at) {
        const auto& node = tree.nodes[at];
        const auto& object = packfile.objects[node.object];
        const auto name = names.find(node.object);
        const auto animation = animations.find(node.object);
        nodes.push_back({{"node", at},
                         {"object", node.object},
                         {"parent", node.parent},
                         {"depth", node.depth},
                         {"class_name", std::string(object.class_name)},
                         {"name", name == names.end() ? std::string{} : name->second},
                         {"animation", animation == animations.end() ? std::string{} : animation->second},
                         {"offset", object.offset},
                         {"member", node.member},
                         {"summary", node.summary},
                         {"repeated", node.repeated},
                         {"cut", node.cut}});
    }
    auto roots = nlohmann::json::array();
    for (const std::size_t root : tree.roots) roots.push_back(root);
    return nlohmann::json{
        {"path", path.string()},
        {"object_count", tree.object_count},
        {"roots", std::move(roots)},
        {"nodes", std::move(nodes)},
        {"unreachable", tree.unreachable},
        {"truncated", tree.truncated},
        {"partial", true},
        {"note", kTreeNote},
    };
}

// Constraint: a header the packfile reader refuses still gets a reply, because diagnosing such a file is the point.
Result<nlohmann::json> HkxService::packfileDiagnostics(const std::filesystem::path& path) const {
    auto bytes = readFileBytes(path);
    if (!bytes) return std::unexpected(bytes.error());
    HkxPackfile packfile;
    const auto status = fo4::hkx::read_packfile(*bytes, packfile);
    if (status != HkxStatus::Ok) {
        auto reason = fo4::hkx::describe_packfile_refusal(*bytes);
        if (reason.empty()) reason = "the packfile reader refused the file: " + statusName(status);
        return nlohmann::json{{"path", path.string()},
                              {"readable", false},
                              {"status", statusName(status)},
                              {"issues", nlohmann::json::array({reason})}};
    }
    fo4::hkx::HkxPackfileDiagnostics report;
    if (const auto diagnosed = fo4::hkx::diagnose_packfile(packfile, report); diagnosed != HkxStatus::Ok) {
        return std::unexpected(readError("packfile diagnostics", diagnosed));
    }
    static constexpr std::array<std::string_view, 3> kLayoutStatus{"match", "signature_mismatch", "no_layout"};
    auto sections = nlohmann::json::array();
    for (const auto& section : report.sections) {
        sections.push_back({{"tag", section.tag},
                            {"size", section.size},
                            {"local_fixups", section.local_fixups},
                            {"global_fixups", section.global_fixups},
                            {"virtual_fixups", section.virtual_fixups}});
    }
    auto classes = nlohmann::json::array();
    for (const auto& row : report.classes) {
        classes.push_back({{"name", row.name},
                           {"signature", hexSignature(row.signature)},
                           {"count", row.count},
                           {"layout_status", std::string(kLayoutStatus[static_cast<std::size_t>(row.status)])},
                           {"layout_size", row.layout_size},
                           {"shortest_span", row.shortest_span}});
    }
    auto issues = nlohmann::json::array();
    for (const auto& issue : report.issues) issues.push_back(issue);
    return nlohmann::json{
        {"path", path.string()},
        {"readable", true},
        {"file_version", report.file_version},
        {"user_tag", report.user_tag},
        {"pointer_size", report.pointer_size},
        {"object_count", report.object_count},
        {"object_links", report.object_links},
        {"inner_links", report.inner_links},
        {"external_links", report.external_links},
        {"stray_links", report.stray_links},
        {"unreferenced_objects", report.unreferenced_objects},
        {"sections", std::move(sections)},
        {"classes", std::move(classes)},
        {"issues", std::move(issues)},
    };
}

// Constraint: read-only; verdict order must match fo4::hkx::HkxRepackVerdict, and refused is a layout the writer declines, not corruption.
Result<nlohmann::json> HkxService::repackCheck(const std::filesystem::path& path) const {
    auto bytes = readFileBytes(path);
    if (!bytes) return std::unexpected(bytes.error());
    static constexpr std::array<std::string_view, 6> kVerdicts{"equal", "read_failed", "refused", "rewrite_failed",
                                                                "reread_failed", "differs"};
    const auto report = fo4::hkx::repack_check(*bytes);
    const auto verdict = static_cast<std::size_t>(report.verdict);
    nlohmann::json reply{
        {"path", path.string()},
        {"verdict", std::string(kVerdicts[verdict])},
        {"model_equal", report.verdict == fo4::hkx::HkxRepackVerdict::Equal},
        {"status", statusName(report.status)},
        {"bytes_identical", report.bytes_identical},
        {"source_size", report.source_size},
        {"rewritten_size", report.rewritten_size},
        {"first_difference", report.difference},
    };
    if (report.verdict == fo4::hkx::HkxRepackVerdict::ReadFailed) {
        reply["reason"] = fo4::hkx::describe_packfile_refusal(*bytes);
    }
    return reply;
}

// Constraint: the walk stays in this layer; one global hit limit, and a file that does not read is counted in skipped, never dropped silently.
Result<nlohmann::json> HkxService::searchHkx(const std::filesystem::path& target, std::string_view query,
                                             std::size_t limit) const {
    while (!query.empty() && (query.front() == ' ' || query.front() == '\t')) query.remove_prefix(1);
    while (!query.empty() && (query.back() == ' ' || query.back() == '\t')) query.remove_suffix(1);
    bool scanTruncated = false;
    auto files = hkxTargets(target, scanTruncated);
    if (!files) return std::unexpected(files.error());
    const std::size_t cap = clampLimit(limit, kRowLimitDefault, kRowLimitMax);

    auto hits = nlohmann::json::array();
    std::size_t kept = 0;
    std::size_t skipped = 0;
    bool truncated = scanTruncated;
    const std::string needle = lowered(query);
    for (const auto& file : *files) {
        if (query.empty()) break;
        if (kept >= cap) {
            truncated = true;
            break;
        }
        const auto name = file.filename().string();
        if (lowered(name).find(needle) != std::string::npos) {
            hits.push_back({{"file", file.string()}, {"kind", "file"}, {"text", name}, {"object", -1}});
            ++kept;
        }
        auto bytes = readFileBytes(file);
        HkxPackfile packfile;
        if (!bytes || fo4::hkx::read_packfile(*bytes, packfile) != HkxStatus::Ok) {
            ++skipped;
            continue;
        }
        fo4::hkx::HkxSearchResult result;
        if (fo4::hkx::search_symbols(packfile, query, cap - kept, result) != HkxStatus::Ok) {
            ++skipped;
            continue;
        }
        for (const auto& hit : result.hits) {
            hits.push_back({{"file", file.string()},
                            {"kind", fo4::hkx::hkx_hit_kind_name(hit.kind)},
                            {"text", hit.text},
                            {"object", hit.object}});
            ++kept;
        }
        if (result.truncated) truncated = true;
    }
    return nlohmann::json{
        {"path", target.string()},
        {"query", std::string(query)},
        {"file_count", files->size()},
        {"skipped", skipped},
        {"hits", std::move(hits)},
        {"hit_count", kept},
        {"truncated", truncated},
    };
}

// Constraint: needs one skeleton; each animation is audited by Radicle and a clip it refuses is listed under skipped with the reason.
Result<nlohmann::json> HkxService::auditChannels(const std::filesystem::path& skeletonPath,
                                                 const std::filesystem::path& animations, std::size_t limit) const {
    HkxPackfile skeletonPack;
    if (auto opened = openPackfile(skeletonPath, skeletonPack); !opened) return std::unexpected(opened.error());
    fo4::hkx::HkxSkeleton skeletonData;
    if (const auto status = fo4::hkx::read_skeleton(skeletonPack, skeletonData); status != HkxStatus::Ok) {
        return std::unexpected(readError("skeleton read", status));
    }
    bool scanTruncated = false;
    auto files = hkxTargets(animations, scanTruncated);
    if (!files) return std::unexpected(files.error());
    const std::size_t cap = clampLimit(limit, kRowLimitDefault, kRowLimitMax);

    auto rows = nlohmann::json::array();
    auto skippedRows = nlohmann::json::array();
    std::size_t audited = 0;
    std::size_t withOffsets = 0;
    std::size_t skipped = 0;
    bool dropped = scanTruncated;
    for (const auto& file : *files) {
        const auto skip = [&](const std::string& reason) {
            ++skipped;
            if (skippedRows.size() < cap) {
                skippedRows.push_back({{"file", file.string()}, {"reason", reason}});
            } else {
                dropped = true;
            }
        };
        auto bytes = readFileBytes(file);
        HkxPackfile packfile;
        if (!bytes) {
            skip(bytes.error().message);
            continue;
        }
        if (const auto status = fo4::hkx::read_packfile(*bytes, packfile); status != HkxStatus::Ok) {
            skip("packfile read failed: " + statusName(status));
            continue;
        }
        fo4::hkx::HkxAnimation animation;
        if (const auto status = fo4::hkx::read_animation(packfile, animation); status != HkxStatus::Ok) {
            std::string refused;
            if (status == HkxStatus::UnsupportedLayout && !animation.refusal.class_name.empty()) {
                refused = " (" + animation.refusal.class_name +
                          (animation.refusal.feature.empty() ? std::string{} : ": " + animation.refusal.feature) + ")";
            }
            skip("animation read failed: " + statusName(status) + refused);
            continue;
        }
        if (animation.tracks.empty()) {
            skip("the clip has no transform tracks");
            continue;
        }
        fo4::hkx::HkxAnimationBinding binding;
        if (const auto status = fo4::hkx::read_binding(packfile, binding); status != HkxStatus::Ok) {
            skip("binding read failed: " + statusName(status));
            continue;
        }
        fo4::hkx::HkxChannelAudit audit;
        if (const auto status = fo4::hkx::audit_channels(skeletonData, animation, binding, audit); status != HkxStatus::Ok) {
            skip("the clip cannot be posed on this skeleton: " + statusName(status));
            continue;
        }
        ++audited;
        const bool offset = audit.offset_translation != 0 || audit.offset_rotation != 0 || audit.offset_scale != 0;
        if (offset) ++withOffsets;
        if (rows.size() >= cap) {
            dropped = true;
            continue;
        }
        auto offsets = nlohmann::json::array();
        for (std::size_t at = 0; at < audit.offsets.size() && at < kSiteLimit; ++at) {
            const auto& track = audit.offsets[at];
            offsets.push_back({{"track", track.track},
                               {"bone", track.bone},
                               {"bone_name", track.bone_name},
                               {"translation_undriven", track.translation_undriven},
                               {"rotation_undriven", track.rotation_undriven},
                               {"scale_undriven", track.scale_undriven},
                               {"rest_translation_length", track.rest_translation_length},
                               {"rest_rotation_degrees", track.rest_rotation_degrees},
                               {"rest_scale_deviation", track.rest_scale_deviation}});
        }
        rows.push_back({{"file", file.string()},
                        {"tracks", audit.tracks},
                        {"no_translation", audit.no_translation},
                        {"no_rotation", audit.no_rotation},
                        {"no_scale", audit.no_scale},
                        {"offset_translation", audit.offset_translation},
                        {"offset_rotation", audit.offset_rotation},
                        {"offset_scale", audit.offset_scale},
                        {"max_translation_offset", audit.max_translation_offset},
                        {"max_rotation_degrees", audit.max_rotation_degrees},
                        {"offsets", std::move(offsets)},
                        {"offset_total", audit.offsets.size()}});
    }
    return nlohmann::json{
        {"skeleton", skeletonData.name},
        {"bone_count", skeletonData.bone_names.size()},
        {"path", animations.string()},
        {"file_count", files->size()},
        {"audited", audited},
        {"with_offsets", withOffsets},
        {"skipped", skipped},
        {"files", std::move(rows)},
        {"skipped_files", std::move(skippedRows)},
        {"truncated", dropped},
    };
}

// Constraint: partial by design; only event sites the typed model and the layout table decode are listed, so a missing site is not proof of an unused event.
Result<nlohmann::json> HkxService::symbolUsage(const std::filesystem::path& path, std::size_t limit) const {
    HkxPackfile packfile;
    if (auto opened = openPackfile(path, packfile); !opened) return std::unexpected(opened.error());
    fo4::hkx::HkxGraphModel model;
    if (const auto status = fo4::hkx::read_behavior_model(packfile, model); status != HkxStatus::Ok) {
        return std::unexpected(readError("behavior model read", status));
    }
    fo4::hkx::HkxSymbolUsage usage;
    fo4::hkx::collect_symbol_usage(model, usage);
    fo4::hkx::collect_object_event_sites(packfile, usage);
    const std::size_t cap = clampLimit(limit, kRowLimitDefault, kRowLimitMax);

    const auto events = cappedRows(usage.events, cap, [](const fo4::hkx::HkxEventUsage& row) {
        const auto sites = cappedRows(row.sites, kSiteLimit, [](const fo4::hkx::HkxEventSiteRow& site) {
            return nlohmann::json{{"site", fo4::hkx::hkx_event_site_name(site.site)},
                                  {"node", site.node},
                                  {"state", site.state},
                                  {"owner", site.owner},
                                  {"object", site.object},
                                  {"member", site.member}};
        });
        return nlohmann::json{{"event_id", row.event_id},
                              {"name", row.name},
                              {"in_range", row.in_range},
                              {"no_decoded_site", row.in_range && row.sites.empty()},
                              {"site_total", row.sites.size()},
                              {"sites", sites}};
    });
    static constexpr std::array<std::string_view, 6> kOwnerKinds{"", "state_machine", "state", "blender", "blend_child", "clip"};
    const auto variables = cappedRows(usage.variables, cap, [](const fo4::hkx::HkxVariableUsage& row) {
        const auto sites = cappedRows(row.sites, kSiteLimit, [](const fo4::hkx::HkxVariableSiteRow& site) {
            const auto kind = static_cast<std::size_t>(site.owner_kind);
            return nlohmann::json{{"owner_kind", std::string(kind < kOwnerKinds.size() ? kOwnerKinds[kind] : "")},
                                  {"owner_index", site.owner_index},
                                  {"member_path", site.member_path}};
        });
        return nlohmann::json{{"variable_index", row.variable_index},
                              {"name", row.name},
                              {"in_range", row.in_range},
                              {"no_decoded_site", row.in_range && row.sites.empty()},
                              {"site_total", row.sites.size()},
                              {"sites", sites}};
    });
    const auto states = cappedRows(usage.states, cap, [](const fo4::hkx::HkxStateUsage& row) {
        return nlohmann::json{{"state", row.state},
                              {"name", row.name},
                              {"state_id", row.state_id},
                              {"state_machine", row.state_machine},
                              {"incoming", row.incoming},
                              {"outgoing", row.outgoing},
                              {"is_start", row.is_start}};
    });
    return nlohmann::json{
        {"path", path.string()},
        {"graph_name", model.graph_name},
        {"events", events},
        {"event_total", usage.events.size()},
        {"variables", variables},
        {"variable_total", usage.variables.size()},
        {"states", states},
        {"state_total", usage.states.size()},
        {"partial", true},
        {"note", "event sites are limited to clip triggers, state machine events, transitions and the object event slots the class "
                 "table types as events (object_member); a class or slot the table does not type is not decoded. "
                 "Use hkx_behavior_validate for dangling reference errors."},
    };
}

}  // namespace fo4ide::hkx
