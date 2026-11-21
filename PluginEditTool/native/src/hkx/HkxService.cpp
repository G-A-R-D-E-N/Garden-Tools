#include <fo4ide/hkx/HkxService.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <limits>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <fo4/formid.hpp>
#include <fo4/hkx/animation.hpp>
#include <fo4/hkx/behavior.hpp>
#include <fo4/hkx/behavior_graph.hpp>
#include <fo4/hkx/object_view.hpp>
#include <fo4/hkx/packfile.hpp>
#include <fo4/hkx/physics.hpp>
#include <fo4/hkx/pose.hpp>
#include <fo4/hkx/project_index.hpp>
#include <fo4/hkx/root_motion.hpp>
#include <fo4/hkx/skeleton.hpp>
#include <fo4/hkx/skeleton_mapper.hpp>
#include <fo4/hkx/skeleton_document.hpp>
#include <fo4/hkx/subgraph.hpp>
#include <fo4/hkx/summary.hpp>
#include <fo4/plugin/reader.hpp>

#include <fo4/archive/assets.hpp>

namespace fo4ide::hkx {
namespace {

using fo4::hkx::HkxAnimation;
using fo4::hkx::HkxAnimationBinding;
using fo4::hkx::HkxPackfile;
using fo4::hkx::HkxQsTransform;
using fo4::hkx::HkxSkeleton;
using fo4::hkx::HkxStatus;

constexpr std::array<std::string_view, 10> kStatusNames{
    "Ok", "Truncated", "BadMagic", "UnsupportedLayout", "MissingSection",
    "BadSection", "FixupOutOfRange", "DuplicateFixup", "BadClassName", "MissingObject",
};

Result<void> requireFile(const std::filesystem::path& path, std::string_view label) {
    if (path.empty()) return std::unexpected(Error{"InvalidPath", std::string(label) + " path is empty"});
    if (!std::filesystem::is_regular_file(path)) {
        return std::unexpected(Error{"FileNotFound", std::string(label) + " not found: " + path.string()});
    }
    return {};
}

Result<void> checkStatus(HkxStatus status, std::string_view step) {
    if (status == HkxStatus::Ok) return {};
    const auto name = kStatusNames[static_cast<std::size_t>(status)];
    return std::unexpected(Error{"HkxRead", std::string(step) + " failed: " + std::string(name)});
}

Result<std::vector<std::byte>> readBytes(const std::filesystem::path& path) {
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
    auto bytes = readBytes(path);
    if (!bytes) return std::unexpected(bytes.error());
    return checkStatus(fo4::hkx::read_packfile(*bytes, packfile), "packfile read");
}

Result<HkxSkeleton> loadSkeleton(const std::filesystem::path& path) {
    HkxPackfile packfile;
    if (auto opened = openPackfile(path, packfile); !opened) return std::unexpected(opened.error());
    HkxSkeleton skeletonData;
    if (auto status = checkStatus(fo4::hkx::read_skeleton(packfile, skeletonData), "skeleton read"); !status) {
        return std::unexpected(status.error());
    }
    return skeletonData;
}

Result<void> readClip(const std::filesystem::path& path, HkxAnimation& animation, HkxAnimationBinding& binding) {
    HkxPackfile packfile;
    if (auto opened = openPackfile(path, packfile); !opened) return opened;
    if (auto status = checkStatus(fo4::hkx::read_animation(packfile, animation), "animation read"); !status) {
        return status;
    }
    if (!std::isfinite(animation.duration)) return std::unexpected(Error{"NonFiniteValue", "Animation duration is not finite"});
    return checkStatus(fo4::hkx::read_binding(packfile, binding), "binding read");
}

nlohmann::json boneJson(const std::string& name, std::int16_t parent, const HkxQsTransform& transform) {
    return {
        {"name", name},
        {"parent", parent},
        {"translation", transform.translation},
        {"rotation", transform.rotation},
        {"scale", transform.scale},
    };
}

template <typename Components>
bool allFinite(const Components& components) {
    return std::ranges::all_of(components, [](float value) { return std::isfinite(value); });
}

Result<void> requireFiniteBone(const std::string& bone, const HkxQsTransform& transform) {
    if (allFinite(transform.translation) && allFinite(transform.rotation) && allFinite(transform.scale)) return {};
    return std::unexpected(Error{"NonFiniteValue", "Bone '" + bone + "' has a translation, rotation or scale that is not finite"});
}

Result<nlohmann::json> boneArray(const HkxSkeleton& skeletonData, const std::vector<HkxQsTransform>& pose) {
    auto bones = nlohmann::json::array();
    for (std::size_t i = 0; i < pose.size(); ++i) {
        if (auto finite = requireFiniteBone(skeletonData.bone_names[i], pose[i]); !finite) return std::unexpected(finite.error());
        bones.push_back(boneJson(skeletonData.bone_names[i], skeletonData.parent_indices[i], pose[i]));
    }
    return bones;
}

Result<nlohmann::json> skeletonJson(const HkxSkeleton& skeletonData) {
    auto bones = boneArray(skeletonData, skeletonData.reference_pose);
    if (!bones) return std::unexpected(bones.error());
    // Constraint: the lock flag rides on the bone entry the panel already lists.
    for (std::size_t i = 0; i < skeletonData.bone_names.size() && i < skeletonData.lock_translation.size(); ++i) {
        (*bones)[i]["lock"] = skeletonData.lock_translation[i];
    }
    auto slots = nlohmann::json::array();
    for (const auto& slot : skeletonData.float_slots) slots.push_back({{"name", slot.name}, {"named", slot.named}});
    auto frames = nlohmann::json::array();
    for (const auto& frame : skeletonData.local_frames) {
        frames.push_back({{"bone", frame.bone_index}, {"has_frame", frame.has_frame}});
    }
    auto partitions = nlohmann::json::array();
    for (const auto& partition : skeletonData.partitions) {
        partitions.push_back({{"name", partition.name},
                              {"named", partition.named},
                              {"start_bone", partition.start_bone},
                              {"num_bones", partition.num_bones}});
    }
    return nlohmann::json{{"name", skeletonData.name},
                          {"bones", *bones},
                          {"reference_floats", skeletonData.reference_floats},
                          {"float_slots", slots},
                          {"local_frames", frames},
                          {"partitions", partitions}};
}

Result<nlohmann::json> poseJson(const HkxSkeleton& skeletonData, const HkxAnimation& animation,
                                const std::vector<HkxQsTransform>& pose) {
    auto bones = boneArray(skeletonData, pose);
    if (!bones) return std::unexpected(bones.error());
    return nlohmann::json{{"duration", animation.duration}, {"frame_count", animation.frame_count}, {"bones", *bones}};
}

struct FileStamp {
    std::filesystem::path path;
    std::uintmax_t size{0};
    std::filesystem::file_time_type written{};

    friend bool operator==(const FileStamp&, const FileStamp&) = default;
};

Result<FileStamp> stampOf(const std::filesystem::path& path) {
    std::error_code ec;
    FileStamp stamp{path};
    stamp.size = std::filesystem::file_size(path, ec);
    if (ec) return std::unexpected(Error{"ReadFailed", "Could not stat " + path.string()});
    stamp.written = std::filesystem::last_write_time(path, ec);
    if (ec) return std::unexpected(Error{"ReadFailed", "Could not stat " + path.string()});
    return stamp;
}

}

struct HkxService::ClipSnapshot {
    FileStamp skeletonStamp;
    FileStamp animationStamp;
    HkxSkeleton skeleton;
    HkxAnimation animation;
    HkxAnimationBinding binding;
};

Result<std::shared_ptr<const HkxService::ClipSnapshot>> HkxService::loadedClip(
    const std::filesystem::path& skeletonPath, const std::filesystem::path& animationPath) const {
    auto skeletonStamp = stampOf(skeletonPath);
    if (!skeletonStamp) return std::unexpected(skeletonStamp.error());
    auto animationStamp = stampOf(animationPath);
    if (!animationStamp) return std::unexpected(animationStamp.error());

    std::scoped_lock lock(cacheMutex_);
    if (cache_ && cache_->skeletonStamp == *skeletonStamp && cache_->animationStamp == *animationStamp) return cache_;

    auto loaded = std::make_shared<ClipSnapshot>();
    loaded->skeletonStamp = *skeletonStamp;
    loaded->animationStamp = *animationStamp;
    auto skeletonData = loadSkeleton(skeletonPath);
    if (!skeletonData) return std::unexpected(skeletonData.error());
    loaded->skeleton = std::move(*skeletonData);
    if (auto status = readClip(animationPath, loaded->animation, loaded->binding); !status) {
        return std::unexpected(status.error());
    }
    cache_ = loaded;
    return cache_;
}

Result<nlohmann::json> HkxService::skeleton(const std::filesystem::path& skeletonPath) const {
    if (auto valid = requireFile(skeletonPath, "Skeleton"); !valid) return std::unexpected(valid.error());
    auto skeletonData = loadSkeleton(skeletonPath);
    if (!skeletonData) return std::unexpected(skeletonData.error());
    return skeletonJson(*skeletonData);
}

Result<nlohmann::json> HkxService::pose(const std::filesystem::path& skeletonPath,
                                        const std::filesystem::path& animationPath,
                                        float time,
                                        PoseSpace space) const {
    if (auto valid = requireFile(skeletonPath, "Skeleton"); !valid) return std::unexpected(valid.error());
    if (auto valid = requireFile(animationPath, "Animation"); !valid) return std::unexpected(valid.error());
    auto clip = loadedClip(skeletonPath, animationPath);
    if (!clip) return std::unexpected(clip.error());
    const auto& snapshot = **clip;

    std::vector<HkxQsTransform> local;
    if (auto sampled = checkStatus(fo4::hkx::sample_pose(snapshot.skeleton, snapshot.animation, snapshot.binding, time, local), "pose sampling");
        !sampled) {
        return std::unexpected(sampled.error());
    }
    if (space == PoseSpace::Local) return poseJson(snapshot.skeleton, snapshot.animation, local);

    std::vector<HkxQsTransform> model;
    if (auto converted = checkStatus(fo4::hkx::model_space_pose(snapshot.skeleton, local, model), "model-space pose"); !converted) {
        return std::unexpected(converted.error());
    }
    return poseJson(snapshot.skeleton, snapshot.animation, model);
}

Result<nlohmann::json> HkxService::poseBoth(const std::filesystem::path& skeletonPath,
                                            const std::filesystem::path& animationPath,
                                            float time) const {
    if (auto valid = requireFile(skeletonPath, "Skeleton"); !valid) return std::unexpected(valid.error());
    if (auto valid = requireFile(animationPath, "Animation"); !valid) return std::unexpected(valid.error());
    auto clip = loadedClip(skeletonPath, animationPath);
    if (!clip) return std::unexpected(clip.error());
    const auto& snapshot = **clip;

    std::vector<HkxQsTransform> local;
    if (auto sampled = checkStatus(fo4::hkx::sample_pose(snapshot.skeleton, snapshot.animation, snapshot.binding, time, local), "pose sampling");
        !sampled) {
        return std::unexpected(sampled.error());
    }
    std::vector<HkxQsTransform> model;
    if (auto converted = checkStatus(fo4::hkx::model_space_pose(snapshot.skeleton, local, model), "model-space pose"); !converted) {
        return std::unexpected(converted.error());
    }
    auto localBones = boneArray(snapshot.skeleton, local);
    if (!localBones) return std::unexpected(localBones.error());
    auto modelBones = boneArray(snapshot.skeleton, model);
    if (!modelBones) return std::unexpected(modelBones.error());
    return nlohmann::json{
        {"duration", snapshot.animation.duration},
        {"frame_count", snapshot.animation.frame_count},
        {"local", *localBones},
        {"model", *modelBones},
    };
}

Result<nlohmann::json> HkxService::loadClip(const std::filesystem::path& skeletonPath,
                                            const std::filesystem::path& animationPath) const {
    if (auto valid = requireFile(skeletonPath, "Skeleton"); !valid) return std::unexpected(valid.error());
    if (auto valid = requireFile(animationPath, "Animation"); !valid) return std::unexpected(valid.error());
    auto clip = loadedClip(skeletonPath, animationPath);
    if (!clip) return std::unexpected(clip.error());
    const auto& snapshot = **clip;
    const auto& skeletonName = snapshot.skeleton.name;
    const auto& originalName = snapshot.binding.original_skeleton_name;

    std::string match{"unverified"};
    if (!skeletonName.empty() && !originalName.empty()) {
        match = skeletonName == originalName ? "match" : "mismatch";
    }
    return nlohmann::json{
        {"duration", snapshot.animation.duration},
        {"frame_count", snapshot.animation.frame_count},
        {"skeleton_name", skeletonName},
        {"original_skeleton_name", originalName},
        {"skeleton_match", match},
    };
}

Result<nlohmann::json> HkxService::summary(const std::filesystem::path& path) const {
    if (auto valid = requireFile(path, "HKX"); !valid) return std::unexpected(valid.error());
    HkxPackfile packfile;
    if (auto opened = openPackfile(path, packfile); !opened) return std::unexpected(opened.error());
    const auto read = fo4::hkx::read_summary(packfile);

    auto clips = nlohmann::json::array();
    for (const auto& clip : read.clips) {
        clips.push_back({
            {"class_name", clip.class_name},
            {"kind", fo4::hkx::hkx_animation_kind_name(clip.kind)},
            {"duration", clip.duration},
            {"frame_count", clip.frame_count},
            {"track_count", clip.track_count},
            {"annotation_count", clip.annotation_count},
        });
    }
    auto undecoded = nlohmann::json::array();
    for (const auto& entry : read.undecoded) {
        undecoded.push_back({{"class_name", entry.class_name}, {"reason", entry.reason}});
    }
    auto bindings = nlohmann::json::array();
    for (const auto& name : read.binding_skeletons) bindings.push_back(name);

    nlohmann::json skeleton = nullptr;
    if (read.has_skeleton) {
        skeleton = nlohmann::json{{"name", read.skeleton_name}, {"bone_count", read.bone_names.size()}};
    }
    return nlohmann::json{
        {"object_count", read.object_count},
        {"skeleton", skeleton},
        {"clips", std::move(clips)},
        {"undecoded", std::move(undecoded)},
        {"bindings", std::move(bindings)},
    };
}

// Constraint: read_animation materializes one sample per frame per track, so this report pays that decode once.
Result<nlohmann::json> HkxService::inspectAnimation(const std::filesystem::path& animationPath,
                                                   std::optional<std::size_t> track,
                                                   std::size_t sampleLimit) const {
    if (auto valid = requireFile(animationPath, "Animation"); !valid) return std::unexpected(valid.error());
    HkxPackfile packfile;
    if (auto opened = openPackfile(animationPath, packfile); !opened) return std::unexpected(opened.error());

    HkxAnimation animation;
    if (auto status = checkStatus(fo4::hkx::read_animation(packfile, animation), "animation read"); !status) {
        return std::unexpected(status.error());
    }
    if (!std::isfinite(animation.duration)) {
        return std::unexpected(Error{"NonFiniteValue", "Animation duration is not finite"});
    }

    auto tracks = nlohmann::json::array();
    for (std::size_t index = 0; index < animation.tracks.size(); ++index) {
        const auto& found = animation.tracks[index];
        tracks.push_back({
            {"index", index},
            {"samples", found.rotations.size()},
            {"translation_animated", found.translation_animated},
            {"rotation_animated", found.rotation_animated},
            {"scale_animated", found.scale_animated},
        });
    }

    auto annotations = nlohmann::json::array();
    std::size_t annotationCount = 0;
    for (const auto& annotationTrack : animation.annotation_tracks) {
        auto entries = nlohmann::json::array();
        for (const auto& entry : annotationTrack.annotations) {
            entries.push_back({{"time", entry.time}, {"text", entry.text}});
        }
        annotationCount += entries.size();
        annotations.push_back({{"name", annotationTrack.name}, {"entries", std::move(entries)}});
    }

    nlohmann::json reply{
        {"class_name", animation.class_name},
        {"kind", fo4::hkx::hkx_animation_kind_name(animation.kind)},
        {"type", animation.type},
        {"duration", animation.duration},
        {"frame_count", animation.frame_count},
        {"frame_duration", animation.frame_duration},
        {"float_track_count", animation.float_track_count},
        {"block_count", animation.blocks.count},
        {"block_max_frames_per_block", animation.blocks.max_frames_per_block},
        {"block_duration", animation.blocks.block_duration},
        {"track_count", animation.tracks.size()},
        {"annotation_track_count", animation.annotation_tracks.size()},
        {"annotation_count", annotationCount},
        {"object_count", packfile.objects.size()},
        {"tracks", std::move(tracks)},
        {"annotations", std::move(annotations)},
    };

    if (track) {
        if (*track >= animation.tracks.size()) {
            return std::unexpected(Error{"InvalidTrack", "track index is out of range"});
        }
        const auto& found = animation.tracks[*track];
        // Constraint: a dump is one page of frames, because a long clip is about ten numbers a frame in one reply.
        const std::size_t frames =
            std::min({found.translations.size(), found.rotations.size(), found.scales.size()});
        const std::size_t shown = std::min(frames, sampleLimit);
        std::vector<float> translations;
        translations.reserve(shown * 3);
        std::vector<float> rotations;
        rotations.reserve(shown * 4);
        std::vector<float> scales;
        scales.reserve(shown * 3);
        for (std::size_t frame = 0; frame < shown; ++frame) {
            translations.insert(translations.end(), found.translations[frame].begin(), found.translations[frame].end());
            rotations.insert(rotations.end(), found.rotations[frame].begin(), found.rotations[frame].end());
            scales.insert(scales.end(), found.scales[frame].begin(), found.scales[frame].end());
        }
        reply["track"] = {
            {"index", *track},
            {"frames", frames},
            {"shown", shown},
            {"truncated", shown < frames},
            {"translations", std::move(translations)},
            {"rotations", std::move(rotations)},
            {"scales", std::move(scales)},
        };
    }
    return reply;
}

namespace {

Result<std::uint32_t> subgraphFormId(std::string_view text) {
    std::string_view body = text;
    int base = 10;
    if (body.size() > 2 && body[0] == '0' && (body[1] == 'x' || body[1] == 'X')) {
        body.remove_prefix(2);
        base = 16;
    }
    if (body.empty()) {
        return std::unexpected(Error{"InvalidFormId", "form id is empty"});
    }
    std::uint64_t value = 0;
    const char* const begin = body.data();
    const char* const end = begin + body.size();
    const auto [stop, error] = std::from_chars(begin, end, value, base);
    if (error != std::errc{} || stop != end || value > 0xFFFFFFFFull) {
        return std::unexpected(Error{"InvalidFormId",
                                     "form id must be a local form ID, decimal or 0x hex"});
    }
    if (value == 0) {
        return std::unexpected(Error{"InvalidFormId", "form id must not be zero"});
    }
    return static_cast<std::uint32_t>(value);
}

std::string hexFormId(std::uint32_t value) {
    std::ostringstream out;
    out << "0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << value;
    return out.str();
}

// Constraint: the archives must be the data folder's .ba2 files sorted by descending path, because the resolver lets the last archive win and the earliest by path must win.
fo4::archive::AssetResolver subgraphAssets(const std::filesystem::path& dataFolder) {
    std::vector<std::filesystem::path> paths;
    std::error_code error;
    const std::filesystem::directory_iterator end;
    for (std::filesystem::directory_iterator it(dataFolder, error); !error && it != end;
         it.increment(error)) {
        if (!it->is_regular_file()) continue;
        std::string extension = it->path().extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (extension == ".ba2") paths.push_back(it->path());
    }
    std::sort(paths.begin(), paths.end(), std::greater<>());
    const std::vector<std::filesystem::path> roots{dataFolder};
    return fo4::archive::AssetResolver::open_roots(roots, paths, fo4::archive::ArchiveFailurePolicy::skip)
        .resolver;
}

void addSubgraphManifest(nlohmann::json& reply, std::uint64_t manifestId,
                         const std::filesystem::path& dataFolder) {
    const std::string inner = fo4::hkx::subgraph_manifest_path(manifestId);
    const auto assets = subgraphAssets(dataFolder);

    const auto manifest = fo4::hkx::read_subgraph_manifest(assets, manifestId);
    if (!manifest) {
        reply["notes"].push_back("no " + inner + " under " + dataFolder.string() +
                                  " as a loose file or inside its .ba2 archives");
        return;
    }

    const auto parsed = fo4::hkx::parse_animation_file_data(manifest->text);
    if (!parsed) {
        reply["notes"].push_back("the manifest " + inner +
                                  " does not read as an AnimationFileData file");
        return;
    }

    std::size_t present = 0;
    std::vector<std::string> unresolved;
    for (const auto& animation : parsed->animation_paths) {
        if (fo4::hkx::animation_exists(assets, animation)) {
            ++present;
            continue;
        }
        unresolved.push_back(animation);
    }

    reply["manifest"] = {
        {"id", parsed->id},
        {"source", manifest->source},
        {"path", inner},
        {"behavior_paths", parsed->behavior_paths},
        {"animation_paths", parsed->animation_paths},
        {"present", present},
        {"unresolved", std::move(unresolved)},
    };
}
}

Result<nlohmann::json> HkxService::subgraph(const std::filesystem::path& pluginPath,
                                            std::string_view formIdText,
                                            const std::filesystem::path& dataFolder) const {
    if (auto readable = requireFile(pluginPath, "plugin"); !readable) {
        return std::unexpected(readable.error());
    }
    const auto formId = subgraphFormId(formIdText);
    if (!formId) return std::unexpected(formId.error());

    fo4::Plugin plugin;
    try {
        plugin = fo4::read_plugin_file(pluginPath);
    } catch (const std::exception& ex) {
        return std::unexpected(Error{"PluginUnreadable",
                                     "could not read " + pluginPath.string() + ": " + ex.what()});
    }

    const fo4::Record* race = fo4::find_record(plugin, fo4::RawFormId{*formId});
    if (race == nullptr) {
        return std::unexpected(Error{"RecordNotFound", "no record " + hexFormId(*formId) + " in " +
                                                           pluginPath.string()});
    }
    if (race->header.signature.view() != "RACE") {
        return std::unexpected(Error{"InvalidRecord",
                                     hexFormId(*formId) + " is a " +
                                         race->header.signature.to_string() +
                                         " record; an animation set is read from RACE"});
    }

    const auto set = fo4::hkx::race_animation_set(*race);
    nlohmann::json reply{
        {"plugin", pluginPath.string()},
        {"race", hexFormId(*formId)},
        {"behavior_graphs", set.behavior_graphs},
        {"path_prefixes", set.path_prefixes},
        {"subgraph_ids", nlohmann::json::array()},
        {"manifest", nullptr},
        {"notes", nlohmann::json::array()},
    };
    for (const auto& found : set.subgraphs) reply["subgraph_ids"].push_back(found.id);

    if (set.behavior_graphs.empty()) {
        reply["notes"].push_back("the record names no behaviour graph (SGNM)");
    } else if (set.subgraphs.empty()) {
        reply["notes"].push_back("the record names " +
                                  std::to_string(set.behavior_graphs.size()) +
                                  " behaviour graphs and does not group the animation path prefixes "
                                  "per graph, so no subgraph id is computed");
    }
    if (dataFolder.empty()) {
        reply["notes"].push_back(
            "no data folder was given, so no AnimationFileData manifest was read");
        return reply;
    }
    if (!std::filesystem::is_directory(dataFolder)) {
        return std::unexpected(Error{"DataFolderNotFound",
                                     "data folder not found: " + dataFolder.string()});
    }
    if (set.subgraphs.empty()) return reply;

    addSubgraphManifest(reply, set.subgraphs.front().id, dataFolder);
    return reply;
}

// Invariant: exactly one of id and behaviorPath names the subgraph, so the reply never mixes a given id with a computed one.
Result<nlohmann::json> HkxService::subgraphManifest(std::string_view idText, std::string_view behaviorPath,
                                                    const std::vector<std::string>& pathPrefixes,
                                                    const std::filesystem::path& dataFolder) const {
    const bool hasId = !idText.empty();
    const bool hasBehavior = !behaviorPath.empty();
    if (hasId == hasBehavior) {
        return std::unexpected(Error{"InvalidSubgraph", "give either id or behavior_path, not both and not neither"});
    }
    std::uint64_t id = 0;
    if (hasId) {
        const char* const begin = idText.data();
        const char* const end = begin + idText.size();
        const auto [stop, error] = std::from_chars(begin, end, id, 10);
        if (error != std::errc{} || stop != end) {
            return std::unexpected(Error{"InvalidSubgraph", "id must be a decimal unsigned 64-bit number"});
        }
    } else {
        if (pathPrefixes.empty()) {
            return std::unexpected(Error{"InvalidSubgraph", "behavior_path needs at least one path prefix"});
        }
        id = fo4::hkx::subgraph_id(behaviorPath, pathPrefixes);
    }

    nlohmann::json reply{
        {"id", id},
        {"id_source", hasId ? "given" : "computed"},
        {"behavior_path", std::string(behaviorPath)},
        {"path_prefixes", pathPrefixes},
        {"manifest", nullptr},
        {"notes", nlohmann::json::array()},
    };
    if (dataFolder.empty()) {
        reply["notes"].push_back("no data folder was given, so no AnimationFileData manifest was read");
        return reply;
    }
    if (!std::filesystem::is_directory(dataFolder)) {
        return std::unexpected(Error{"DataFolderNotFound", "data folder not found: " + dataFolder.string()});
    }
    addSubgraphManifest(reply, id, dataFolder);
    return reply;
}

// Invariant: the view names every class it could not read, so an object is never silently untyped.
Result<nlohmann::json> HkxService::inspectObjects(const std::filesystem::path& path,
                                                  std::string_view classPrefix,
                                                  bool knownLayoutOnly,
                                                  std::size_t limit) const {
    if (auto valid = requireFile(path, "HKX"); !valid) return std::unexpected(valid.error());
    HkxPackfile packfile;
    if (auto opened = openPackfile(path, packfile); !opened) return std::unexpected(opened.error());

    fo4::hkx::HkxObjectQuery query;
    query.class_prefix = std::string(classPrefix);
    query.known_layout_only = knownLayoutOnly;
    query.limit = limit;
    fo4::hkx::HkxObjectReport view;
    if (auto status = checkStatus(fo4::hkx::inspect_objects(packfile, query, view), "object inspection"); !status) {
        return std::unexpected(status.error());
    }

    auto objects = nlohmann::json::array();
    for (const auto& object : view.objects) {
        auto fields = nlohmann::json::array();
        for (const auto& field : object.fields) {
            fields.push_back({{"name", field.name}, {"offset", field.offset}, {"size", field.size}, {"hex", field.hex}});
        }
        auto references = nlohmann::json::array();
        for (const auto& reference : object.references) {
            references.push_back({{"source", reference.source},
                                  {"member", reference.member},
                                  {"target_section", reference.target_section},
                                  {"target_offset", reference.target_offset},
                                  {"target_object", reference.target_object},
                                  {"target_class", reference.target_class}});
        }
        objects.push_back({{"index", object.index},
                           {"offset", object.offset},
                           {"signature", object.signature},
                           {"class_name", object.class_name},
                           {"layout_size", object.layout_size},
                           {"size", object.size},
                           {"layout_known", object.layout_known},
                           {"fields", std::move(fields)},
                           {"references", std::move(references)}});
    }
    auto classes = nlohmann::json::array();
    for (const auto& name : view.classes) classes.push_back(name);
    auto diagnostics = nlohmann::json::array();
    for (const auto& line : view.diagnostics) diagnostics.push_back(line);
    return nlohmann::json{
        {"object_count", view.object_count},
        {"matched", view.matched},
        {"classes", std::move(classes)},
        {"objects", std::move(objects)},
        {"diagnostics", std::move(diagnostics)},
    };
}

// Invariant: the report names the bone it measured, so a clip without that track is refused instead of read as still.
Result<nlohmann::json> HkxService::rootMotion(const std::filesystem::path& skeletonPath,
                                              const std::filesystem::path& animationPath,
                                              std::int32_t bone,
                                              std::size_t sampleLimit) const {
    if (auto valid = requireFile(skeletonPath, "Skeleton"); !valid) return std::unexpected(valid.error());
    if (auto valid = requireFile(animationPath, "Animation"); !valid) return std::unexpected(valid.error());
    auto clip = loadedClip(skeletonPath, animationPath);
    if (!clip) return std::unexpected(clip.error());
    const auto& snapshot = **clip;
    if (bone < 0 || static_cast<std::size_t>(bone) >= snapshot.skeleton.bone_names.size()) {
        return std::unexpected(Error{"InvalidBone", "bone index is out of range for this skeleton"});
    }

    fo4::hkx::HkxRootMotion motion;
    const auto status = fo4::hkx::read_root_motion(snapshot.animation, snapshot.binding, bone, sampleLimit, motion);
    if (status == HkxStatus::MissingObject) {
        return std::unexpected(Error{"NoRootTrack", "the clip animates no translation for bone " +
                                                       snapshot.skeleton.bone_names[static_cast<std::size_t>(bone)]});
    }
    if (auto checked = checkStatus(status, "root motion read"); !checked) return std::unexpected(checked.error());

    auto samples = nlohmann::json::array();
    for (const auto& sample : motion.samples) {
        samples.push_back({{"frame", sample.frame},
                           {"time", sample.time},
                           {"translation", sample.translation},
                           {"delta", sample.delta}});
    }
    return nlohmann::json{
        {"bone", motion.bone},
        {"bone_name", snapshot.skeleton.bone_names[static_cast<std::size_t>(bone)]},
        {"track", motion.track},
        {"frame_count", motion.frame_count},
        {"duration", motion.duration},
        {"frame_duration", motion.frame_duration},
        {"total", motion.total},
        {"distance", motion.distance},
        {"yaw_degrees", motion.yaw_degrees},
        {"velocity", motion.velocity},
        {"loops", motion.loops},
        {"loop_gap", motion.loop_gap},
        {"samples", std::move(samples)},
    };
}

namespace {

nlohmann::json documentJson(const fo4::hkx::HkxSkeletonDocument& document) {
    auto bones = nlohmann::json::array();
    for (const auto& bone : document.bones) {
        auto children = nlohmann::json::array();
        for (const std::size_t child : bone.children) children.push_back(child);
        bones.push_back({{"index", bone.index},
                         {"name", bone.name},
                         {"parent", bone.parent},
                         {"depth", bone.depth},
                         {"children", std::move(children)},
                         {"opposite", bone.opposite}});
    }
    auto roots = nlohmann::json::array();
    for (const auto& root : document.roots) roots.push_back(root);
    auto diagnostics = nlohmann::json::array();
    for (const auto& line : document.diagnostics) diagnostics.push_back(line);
    return nlohmann::json{
        {"name", document.name},
        {"bone_count", document.bones.size()},
        {"has_reference_pose", document.has_reference_pose},
        {"roots", std::move(roots)},
        {"bones", std::move(bones)},
        {"diagnostics", std::move(diagnostics)},
    };
}

}  // namespace

// Invariant: the document names each bone's partner only when that bone exists, so a pair is never guessed.
Result<nlohmann::json> HkxService::skeletonDocument(const std::filesystem::path& skeletonPath) const {
    if (auto valid = requireFile(skeletonPath, "Skeleton"); !valid) return std::unexpected(valid.error());
    auto skeletonData = loadSkeleton(skeletonPath);
    if (!skeletonData) return std::unexpected(skeletonData.error());
    return documentJson(fo4::hkx::skeleton_document(*skeletonData));
}

// Constraint: this reports the edited skeleton and never writes a file, because no packfile writer rebuilds a skeleton yet.
Result<nlohmann::json> HkxService::editSkeleton(const std::filesystem::path& skeletonPath,
                                               std::string_view kind,
                                               std::size_t bone,
                                               std::string_view name,
                                               std::int32_t parent) const {
    if (auto valid = requireFile(skeletonPath, "Skeleton"); !valid) return std::unexpected(valid.error());
    auto skeletonData = loadSkeleton(skeletonPath);
    if (!skeletonData) return std::unexpected(skeletonData.error());

    fo4::hkx::HkxSkeletonEdit edit;
    edit.bone = bone;
    edit.name = std::string(name);
    edit.parent = static_cast<std::int16_t>(parent);
    if (kind == "rename") {
        edit.kind = fo4::hkx::HkxSkeletonEditKind::Rename;
    } else if (kind == "reparent") {
        edit.kind = fo4::hkx::HkxSkeletonEditKind::Reparent;
    } else if (kind == "remove") {
        edit.kind = fo4::hkx::HkxSkeletonEditKind::Remove;
    } else {
        return std::unexpected(Error{"InvalidKind", "kind must be rename, reparent or remove"});
    }
    if (parent < -1) return std::unexpected(Error{"InvalidParent", "parent must be -1 or a bone index"});

    fo4::hkx::HkxSkeleton edited;
    const auto report = fo4::hkx::apply_skeleton_edit(*skeletonData, edit, edited);
    if (!report.changed) return std::unexpected(Error{"Refused", report.refusal});
    auto reply = documentJson(fo4::hkx::skeleton_document(edited));
    reply["changed"] = true;
    return reply;
}

// Invariant: a file without behavior objects says so, because an empty graph would read as a graph with no nodes.
Result<nlohmann::json> HkxService::inspectBehavior(const std::filesystem::path& path, std::size_t limit) const {
    if (auto valid = requireFile(path, "HKX"); !valid) return std::unexpected(valid.error());
    HkxPackfile packfile;
    if (auto opened = openPackfile(path, packfile); !opened) return std::unexpected(opened.error());

    fo4::hkx::HkxBehaviorGraph graph;
    if (auto status = checkStatus(fo4::hkx::inspect_behavior(packfile, limit, graph), "behavior inspection"); !status) {
        return std::unexpected(status.error());
    }

    auto classes = nlohmann::json::array();
    for (const auto& name : graph.classes) classes.push_back(name);
    auto nodes = nlohmann::json::array();
    for (const auto& node : graph.nodes) {
        nodes.push_back({{"object", node.object},
                         {"class_name", node.class_name},
                         {"offset", node.offset},
                         {"size", node.size},
                         {"layout_known", node.layout_known},
                         {"references_out", node.references_out},
                         {"references_in", node.references_in}});
    }
    auto links = nlohmann::json::array();
    for (const auto& link : graph.links) {
        links.push_back({{"from", link.from}, {"to", link.to}, {"member", link.member}});
    }
    auto diagnostics = nlohmann::json::array();
    for (const auto& line : graph.diagnostics) diagnostics.push_back(line);
    return nlohmann::json{
        {"object_count", graph.object_count},
        {"matched", graph.matched},
        {"classes", std::move(classes)},
        {"nodes", std::move(nodes)},
        {"links", std::move(links)},
        {"diagnostics", std::move(diagnostics)},
    };
}

namespace {

constexpr std::array<std::string_view, 17> kGraphKindNames{
    "other", "state_machine", "blender", "clip", "selector", "layer", "pose_matching", "modifier_generator",
    "state_tagging", "cyclic_blend_transition", "offset_animation", "behavior_graph_swap", "bone_switch",
    "behavior_reference", "gamebryo_sequence", "dynamic_animation_tagging", "reference_pose",
};

constexpr std::array<std::string_view, 7> kGraphEdgeRoles{
    "unknown", "wrapped", "offset_clip", "selected", "layer", "bone_switch_child", "blend_child",
};

template <typename Row, typename Emit>
nlohmann::json limitedRows(const std::vector<Row>& rows, std::size_t limit, Emit emit) {
    auto out = nlohmann::json::array();
    for (std::size_t i = 0; i < rows.size() && i < limit; ++i) out.push_back(emit(rows[i]));
    return out;
}

}  // namespace

// Invariant: each list stops at `limit` rows and the *_total counts keep the full size.
Result<nlohmann::json> HkxService::behaviorModel(const std::filesystem::path& path, std::size_t limit) const {
    if (auto valid = requireFile(path, "HKX"); !valid) return std::unexpected(valid.error());
    HkxPackfile packfile;
    if (auto opened = openPackfile(path, packfile); !opened) return std::unexpected(opened.error());

    fo4::hkx::HkxGraphModel model;
    if (auto status = checkStatus(fo4::hkx::read_behavior_model(packfile, model), "behavior model read"); !status) {
        return std::unexpected(status.error());
    }

    const auto events = limitedRows(model.events, limit, [](const fo4::hkx::HkxGraphEvent& event) {
        return nlohmann::json{{"name", event.name}, {"flags", event.flags}};
    });
    const auto variables = limitedRows(model.variables, limit, [](const fo4::hkx::HkxGraphVariable& variable) {
        nlohmann::json row{{"name", variable.name},
                           {"type", variable.type},
                           {"role", variable.role},
                           {"word", variable.word},
                           {"int_value", variable.int_value},
                           {"real_value", variable.real_value}};
        if (variable.has_vector) row["vector"] = variable.vector;
        return row;
    });
    const auto nodes = limitedRows(model.nodes, limit, [](const fo4::hkx::HkxGraphNode& node) {
        return nlohmann::json{{"object", node.object},
                              {"class_name", node.class_name},
                              {"name", node.name},
                              {"kind", std::string(kGraphKindNames[std::min(static_cast<std::size_t>(node.kind), kGraphKindNames.size() - 1)])},
                              {"kind_index", node.kind_index}};
    });
    const auto edges = limitedRows(model.edges, limit, [](const fo4::hkx::HkxGraphEdge& edge) {
        const auto role = static_cast<std::size_t>(edge.role);
        return nlohmann::json{{"parent", edge.parent},
                              {"child", edge.child},
                              {"role", std::string(kGraphEdgeRoles[role < kGraphEdgeRoles.size() ? role : 0])},
                              {"slot", edge.slot},
                              {"weight", edge.weight},
                              {"has_bone_weights", edge.has_bone_weights}};
    });
    const auto states = limitedRows(model.states, limit, [](const fo4::hkx::HkxGraphState& state) {
        return nlohmann::json{{"state_machine", state.state_machine},
                              {"name", state.name},
                              {"state_id", state.state_id},
                              {"generator_node", state.generator_node}};
    });
    auto diagnostics = nlohmann::json::array();
    for (const auto& line : model.diagnostics) diagnostics.push_back(line);
    return nlohmann::json{
        {"graph_name", model.graph_name},
        {"root_node", model.root_node},
        {"events", events},
        {"variables", variables},
        {"nodes", nodes},
        {"edges", edges},
        {"states", states},
        {"event_total", model.events.size()},
        {"variable_total", model.variables.size()},
        {"node_total", model.nodes.size()},
        {"edge_total", model.edges.size()},
        {"state_total", model.states.size()},
        {"diagnostics", std::move(diagnostics)},
    };
}

namespace {

nlohmann::json projectFileJson(const fo4::hkx::HkxProjectFile& file) {
    auto classes = nlohmann::json::array();
    for (const auto& name : file.classes) classes.push_back(name);
    return nlohmann::json{
        {"name", file.name},
        {"object_count", file.object_count},
        {"behavior_objects", file.behavior_objects},
        {"animation_objects", file.animation_objects},
        {"skeleton_objects", file.skeleton_objects},
        {"behavior_hash", std::to_string(file.behavior_hash)},
        {"classes", std::move(classes)},
    };
}

}  // namespace

// Constraint: the walk stays in this layer, because the Radicle index reads bytes and never touches a filesystem.
Result<nlohmann::json> HkxService::projectIndex(const std::filesystem::path& rootPath,
                                                std::string_view query,
                                                std::size_t limit) const {
    std::error_code ec;
    if (rootPath.empty() || !std::filesystem::is_directory(rootPath, ec)) {
        return std::unexpected(Error{"InvalidPath", "project root is not a directory: " + rootPath.string()});
    }

    fo4::hkx::HkxProjectIndex index;
    std::size_t scanned = 0;
    bool truncated = false;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(
             rootPath, std::filesystem::directory_options::skip_permission_denied, ec)) {
        if (ec) break;
        std::error_code fileError;
        if (!entry.is_regular_file(fileError) || entry.path().extension() != ".hkx") continue;
        if (scanned >= limit) {
            truncated = true;
            break;
        }
        ++scanned;
        ++index.file_count;
        auto bytes = readBytes(entry.path());
        if (!bytes) {
            fo4::hkx::HkxProjectFile failed;
            failed.name = entry.path().string();
            failed.error = bytes.error().message;
            index.unreadable.push_back(std::move(failed));
            continue;
        }
        auto file = fo4::hkx::index_project_file(entry.path().string(), *bytes);
        if (file.read_ok) {
            index.files.push_back(std::move(file));
            ++index.read_count;
        } else {
            index.unreadable.push_back(std::move(file));
        }
    }
    // Invariant: a truncated scan says so, because a short file list would otherwise read as a small project.
    if (truncated) index.diagnostics.push_back("scan stopped after " + std::to_string(scanned) + " files");

    auto files = nlohmann::json::array();
    for (const auto& file : index.files) files.push_back(projectFileJson(file));
    auto unreadable = nlohmann::json::array();
    for (const auto& file : index.unreadable) unreadable.push_back({{"name", file.name}, {"error", file.error}});
    auto duplicates = nlohmann::json::array();
    for (const auto& group : fo4::hkx::duplicate_behavior_groups(index)) {
        auto names = nlohmann::json::array();
        for (const std::size_t at : group) names.push_back(index.files[at].name);
        duplicates.push_back(std::move(names));
    }
    auto matches = nlohmann::json::array();
    for (const std::size_t at : fo4::hkx::search_project_index(index, query)) {
        matches.push_back(index.files[at].name);
    }
    auto diagnostics = nlohmann::json::array();
    for (const auto& line : index.diagnostics) diagnostics.push_back(line);
    return nlohmann::json{
        {"root", rootPath.string()},
        {"query", std::string(query)},
        {"file_count", index.file_count},
        {"read_count", index.read_count},
        {"matches", std::move(matches)},
        {"files", std::move(files)},
        {"unreadable", std::move(unreadable)},
        {"duplicates", std::move(duplicates)},
        {"diagnostics", std::move(diagnostics)},
    };
}


namespace {

constexpr std::array<std::string_view, 7> kShapeKindNames{
    "unknown", "sphere", "capsule", "box", "convex_hull", "mesh", "compound",
};

constexpr std::array<std::string_view, 6> kConstraintKindNames{
    "unknown", "fixed", "hinge", "limited_hinge", "ball_and_socket", "ragdoll",
};

constexpr std::array<std::string_view, 3> kLoadSeverityNames{"info", "warning", "error"};

template <typename Value>
nlohmann::json optionalJson(const std::optional<Value>& value) {
    return value ? nlohmann::json(*value) : nlohmann::json(nullptr);
}

nlohmann::json classCountsJson(const fo4::hkx::HkxClassCounts& counts) {
    auto out = nlohmann::json::object();
    for (const auto& [name, count] : counts) out[name] = count;
    return out;
}

nlohmann::json loadReportJson(const fo4::hkx::HkxPhysicsLoadReport& report) {
    auto findings = nlohmann::json::array();
    for (const auto& finding : report.findings) {
        findings.push_back({{"severity", std::string(kLoadSeverityNames[static_cast<std::size_t>(finding.severity)])},
                            {"message", finding.message},
                            {"hint", finding.hint}});
    }
    return nlohmann::json{
        {"status", report.status_slug()},
        {"status_label", report.status_label()},
        {"headline", report.headline()},
        {"hint", report.hint()},
        {"findings", std::move(findings)},
        {"file_version", optionalJson(report.file_version)},
        {"pointer_size", optionalJson(report.pointer_size)},
        {"failing_stage", report.failing_stage},
        {"body_count", report.body_count},
        {"usable_bodies", report.usable_bodies},
        {"skipped_bodies", report.skipped_bodies},
        {"skipped_constraints", report.skipped_constraints},
        {"shape_count", report.shape_count},
        {"constraint_count", report.constraint_count},
        {"mappings", report.mappings},
        {"class_counts", classCountsJson(report.class_counts)},
    };
}

nlohmann::json skeletonBriefJson(const std::optional<HkxSkeleton>& skeleton) {
    if (!skeleton) return nullptr;
    return nlohmann::json{{"name", skeleton->name}, {"bone_count", skeleton->bone_names.size()}};
}

}  // namespace

// Invariant: a file with no usable ragdoll is a reply with loaded false and the classifier verdict, not an error.
Result<nlohmann::json> HkxService::ragdoll(const std::filesystem::path& path, std::size_t limit) const {
    if (auto valid = requireFile(path, "HKX"); !valid) return std::unexpected(valid.error());
    auto bytes = readBytes(path);
    if (!bytes) return std::unexpected(bytes.error());

    fo4::hkx::HkxRagdollModel model;
    fo4::hkx::HkxPhysicsLoadReport report;
    const auto status = fo4::hkx::read_ragdoll(std::span<const std::byte>(*bytes), model, report);
    auto reply = loadReportJson(report);
    reply["loaded"] = status == HkxStatus::Ok;
    reply["read_status"] = std::string(kStatusNames[static_cast<std::size_t>(status)]);
    if (status != HkxStatus::Ok) return reply;

    reply["shapes"] = limitedRows(model.shapes, limit, [](const fo4::hkx::HkxPhysicsShape& shape) {
        return nlohmann::json{{"id", shape.id},
                              {"kind", std::string(kShapeKindNames[static_cast<std::size_t>(shape.kind)])},
                              {"radius", shape.radius},
                              {"half_extents", shape.half_extents},
                              {"children", shape.children},
                              {"vertex_count", shape.vertices.size()},
                              {"face_count", shape.face_rings.size()}};
    });
    reply["bodies"] = limitedRows(model.bodies, limit, [](const fo4::hkx::HkxRigidBody& body) {
        return nlohmann::json{{"id", body.id},
                              {"name", body.name},
                              {"shape_id", body.shape_id},
                              {"bone_index", body.bone_index},
                              {"mass", body.mass},
                              {"center_of_mass", body.center_of_mass},
                              {"position", body.position},
                              {"rotation", body.rotation}};
    });
    reply["constraints"] = limitedRows(model.constraints, limit, [](const fo4::hkx::HkxPhysicsConstraint& constraint) {
        return nlohmann::json{
            {"id", constraint.id},
            {"kind", std::string(kConstraintKindNames[static_cast<std::size_t>(constraint.kind)])},
            {"body_a", constraint.body_a},
            {"body_b", constraint.body_b},
            {"min_angle", constraint.min_angle},
            {"max_angle", constraint.max_angle},
            {"twist_min_angle", optionalJson(constraint.twist_min_angle)},
            {"twist_max_angle", optionalJson(constraint.twist_max_angle)},
            {"cone_max_angle", optionalJson(constraint.cone_max_angle)},
            {"has_preview_limits", constraint.has_preview_limits}};
    });
    reply["bone_bindings"] = limitedRows(model.bone_bindings, limit, [](const fo4::hkx::HkxRagdollBoneBinding& binding) {
        return nlohmann::json{{"bone_index", binding.bone_index}, {"body_id", binding.body_id}};
    });
    reply["shape_total"] = model.shapes.size();
    reply["body_total"] = model.bodies.size();
    reply["constraint_total"] = model.constraints.size();
    reply["bone_binding_total"] = model.bone_bindings.size();
    reply["mapping_total"] = model.mappings.size();
    reply["skeleton"] = skeletonBriefJson(model.skeleton);
    reply["animation_skeleton"] = skeletonBriefJson(model.animation_skeleton);

    std::optional<std::int32_t> boneCount;
    if (model.skeleton) boneCount = static_cast<std::int32_t>(model.skeleton->bone_names.size());
    auto validation = nlohmann::json::array();
    for (const auto& finding : fo4::hkx::check_physics_model(model, boneCount)) {
        const bool isError = finding.level == fo4::hkx::HkxPhysicsValidationLevel::Error;
        validation.push_back({{"level", isError ? "error" : "warning"},
                              {"where", finding.where},
                              {"message", finding.message}});
    }
    reply["validation"] = std::move(validation);
    return reply;
}


namespace {

std::string boneNameAt(const HkxSkeleton& skeleton, std::int16_t bone) {
    if (bone < 0 || static_cast<std::size_t>(bone) >= skeleton.bone_names.size()) return {};
    return skeleton.bone_names[static_cast<std::size_t>(bone)];
}

nlohmann::json mapperPoseJson(const fo4::hkx::HkxMapperTransform& transform) {
    return nlohmann::json{{"translation", transform.translation()},
                          {"rotation", transform.rotation()},
                          {"scale", transform.scale()}};
}

float rotationGap(const std::array<float, 4>& left, const std::array<float, 4>& right) {
    double dot = 0.0;
    for (std::size_t i = 0; i < 4; ++i) dot += static_cast<double>(left[i]) * right[i];
    if (!std::isfinite(dot)) return std::numeric_limits<float>::quiet_NaN();
    return static_cast<float>(2.0 * std::acos(std::min(1.0, std::fabs(dot))));
}

nlohmann::json mapperRowJson(const fo4::hkx::HkxSkeletonMapper& mapper, const fo4::hkx::HkxMapperSimpleRow& row,
                             bool recompute) {
    auto out = nlohmann::json{{"bone_a", row.bone_a},
                              {"bone_b", row.bone_b},
                              {"bone_a_name", boneNameAt(mapper.skeleton_a, row.bone_a)},
                              {"bone_b_name", boneNameAt(mapper.skeleton_b, row.bone_b)},
                              {"transform", mapperPoseJson(row.a_from_b)}};
    if (!recompute) return out;
    auto derived = row.a_from_b;
    const auto refusal = fo4::hkx::recompute_mapping_row(mapper.skeleton_a, row.bone_a, mapper.skeleton_b,
                                                         row.bone_b, derived);
    if (!refusal.empty()) {
        out["recompute_error"] = refusal;
        return out;
    }
    const auto stored = row.a_from_b.translation();
    const auto fresh = derived.translation();
    float squared = 0.0f;
    for (std::size_t i = 0; i < 3; ++i) squared += (stored[i] - fresh[i]) * (stored[i] - fresh[i]);
    out["recomputed"] = mapperPoseJson(derived);
    out["translation_delta"] = std::sqrt(squared);
    out["rotation_delta"] = rotationGap(row.a_from_b.rotation(), derived.rotation());
    return out;
}

nlohmann::json mapperJson(const fo4::hkx::HkxSkeletonMapper& mapper, bool recompute, std::size_t limit) {
    return nlohmann::json{
        {"object_index", mapper.object_index},
        {"skeleton_a", {{"name", mapper.skeleton_a.name}, {"bone_count", mapper.skeleton_a.bone_names.size()}}},
        {"skeleton_b", {{"name", mapper.skeleton_b.name}, {"bone_count", mapper.skeleton_b.bone_names.size()}}},
        {"mapping_type", mapper.mapping_type},
        {"keep_unmapped_local", mapper.keep_unmapped_local},
        {"unmapped_bones", mapper.unmapped_bones},
        {"extracted_motion", mapperPoseJson(mapper.extracted_motion)},
        {"rows", limitedRows(mapper.rows, limit, [&](const fo4::hkx::HkxMapperSimpleRow& row) {
             return mapperRowJson(mapper, row, recompute);
         })},
        {"chains", limitedRows(mapper.chains, limit, [&](const fo4::hkx::HkxMapperChainRow& chain) {
             return nlohmann::json{{"start_bone_a", chain.start_bone_a},
                                   {"end_bone_a", chain.end_bone_a},
                                   {"start_bone_b", chain.start_bone_b},
                                   {"end_bone_b", chain.end_bone_b},
                                   {"start_a_from_b", mapperPoseJson(chain.start_a_from_b)},
                                   {"end_a_from_b", mapperPoseJson(chain.end_a_from_b)}};
         })},
        {"row_total", mapper.rows.size()},
        {"chain_total", mapper.chains.size()},
    };
}

}  // namespace

// Invariant: recompute works on a copy of each stored transform, so the read never changes what it reports.
Result<nlohmann::json> HkxService::mappers(const std::filesystem::path& path, bool recompute, std::size_t limit) const {
    if (auto valid = requireFile(path, "HKX"); !valid) return std::unexpected(valid.error());
    HkxPackfile packfile;
    if (auto opened = openPackfile(path, packfile); !opened) return std::unexpected(opened.error());

    std::vector<fo4::hkx::HkxSkeletonMapper> found;
    if (auto status = checkStatus(fo4::hkx::read_skeleton_mappers(packfile, found), "skeleton mapper read"); !status) {
        return std::unexpected(status.error());
    }
    auto rows = nlohmann::json::array();
    for (const auto& mapper : found) rows.push_back(mapperJson(mapper, recompute, limit));
    return nlohmann::json{{"mapper_count", found.size()}, {"recompute", recompute}, {"mappers", std::move(rows)}};
}

}
