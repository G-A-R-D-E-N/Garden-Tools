#include <fo4ide/hkx/HkxService.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <fo4/formid.hpp>
#include <fo4/hash.hpp>
#include <fo4/io/atomic_write.hpp>
#include <fo4/hkx/animation.hpp>
#include <fo4/hkx/animation_index.hpp>
#include <fo4/hkx/animation_sources.hpp>
#include <fo4/hkx/behavior.hpp>
#include <fo4/hkx/behavior_graph.hpp>
#include <fo4/hkx/gltf_animation_save.hpp>
#include <fo4/hkx/motion_preview.hpp>
#include <fo4/hkx/object_view.hpp>
#include <fo4/hkx/packfile.hpp>
#include <fo4/hkx/physics.hpp>
#include <fo4/hkx/pose.hpp>
#include <fo4/hkx/project_chain.hpp>
#include <fo4/hkx/project_index.hpp>
#include <fo4/hkx/ragdoll_edit.hpp>
#include <fo4/hkx/root_motion.hpp>
#include <fo4/hkx/scene.hpp>
#include <fo4/hkx/skeleton.hpp>
#include <fo4/hkx/skeleton_mapper.hpp>
#include <fo4/hkx/skeleton_document.hpp>
#include <fo4/hkx/skeleton_write.hpp>
#include <fo4/hkx/subgraph.hpp>
#include <fo4/hkx/summary.hpp>
#include <fo4/hkx/track_names.hpp>
#include <fo4/io/atomic_write.hpp>
#include <fo4/io/path_text.hpp>
#include <fo4/plugin/reader.hpp>

#include <fo4/archive/assets.hpp>
#include <fo4/io/sha256.hpp>
#include <fo4ide/core/ProtectedWriteName.hpp>

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

Result<void> checkAnimationRead(HkxStatus status, const HkxAnimation& animation) {
    auto checked = checkStatus(status, "animation read");
    if (checked || status != HkxStatus::UnsupportedLayout) return checked;
    std::string detail = animation.refusal.class_name;
    if (!animation.refusal.feature.empty()) detail += ": " + animation.refusal.feature;
    if (detail.empty()) return checked;
    return std::unexpected(Error{checked.error().type, checked.error().message + " (" + detail + ")"});
}

}  // namespace

Result<std::vector<std::byte>> readHkxBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) return std::unexpected(Error{"FileNotFound", "Could not open " + path.string()});
    const auto size = static_cast<std::size_t>(stream.tellg());
    std::vector<std::byte> bytes(size);
    stream.seekg(0);
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    if (!stream) return std::unexpected(Error{"ReadFailed", "Could not read " + path.string()});
    return bytes;
}

namespace {

Result<std::vector<std::byte>> readBytes(const std::filesystem::path& path) { return readHkxBytes(path); }

Result<void> openPackfile(const std::filesystem::path& path, HkxPackfile& packfile) {
    auto bytes = readBytes(path);
    if (!bytes) return std::unexpected(bytes.error());
    auto status = checkStatus(fo4::hkx::read_packfile(*bytes, packfile), "packfile read");
    if (status) return status;
    if (const auto reason = fo4::hkx::describe_packfile_refusal(*bytes); !reason.empty()) {
        return std::unexpected(Error{status.error().type, status.error().message + " (" + reason + ")"});
    }
    return status;
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
    if (auto status = checkAnimationRead(fo4::hkx::read_animation(packfile, animation), animation); !status) {
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

void HkxService::setImplicitReadFilter(std::function<bool(const std::filesystem::path&)> filter) {
    implicitReadFilter_ = std::move(filter);
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
        undecoded.push_back({{"class_name", entry.class_name}, {"reason", entry.reason}, {"detail", entry.detail}});
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
                                                   std::size_t sampleLimit,
                                                   const std::filesystem::path& skeletonPath) const {
    if (auto valid = requireFile(animationPath, "Animation"); !valid) return std::unexpected(valid.error());
    HkxPackfile packfile;
    if (auto opened = openPackfile(animationPath, packfile); !opened) return std::unexpected(opened.error());

    HkxAnimation animation;
    if (auto status = checkAnimationRead(fo4::hkx::read_animation(packfile, animation), animation); !status) {
        return std::unexpected(status.error());
    }
    if (!std::isfinite(animation.duration)) {
        return std::unexpected(Error{"NonFiniteValue", "Animation duration is not finite"});
    }

    fo4::hkx::HkxAnimationBinding binding;
    if (fo4::hkx::read_binding(packfile, binding) != fo4::hkx::HkxStatus::Ok) binding = {};
    const auto named = fo4::hkx::name_tracks(animationPath, packfile, binding, animation.tracks.size(), skeletonPath,
                                           implicitReadFilter_);

    auto tracks = nlohmann::json::array();
    for (std::size_t index = 0; index < animation.tracks.size(); ++index) {
        const auto& found = animation.tracks[index];
        tracks.push_back({
            {"index", index},
            {"name", named.names[index]},
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
        {"track_name_source", fo4::hkx::hkx_track_name_source_name(named.source)},
        {"track_name_skeleton", named.skeleton.empty() ? std::string{} : named.skeleton.string()},
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

Result<fo4::hkx::AnimationSources> openAssets(const std::filesystem::path& dataFolder,
                                              const std::filesystem::path& mo2InstanceDir,
                                              std::string_view mo2Profile) {
    if (mo2InstanceDir.empty()) {
        if (!mo2Profile.empty()) {
            return std::unexpected(Error{"InvalidMo2", "mo2_profile needs mo2_instance_dir"});
        }
        if (!std::filesystem::is_directory(dataFolder)) {
            return std::unexpected(Error{"DataFolderNotFound", "data folder not found: " + dataFolder.string()});
        }
    }
    try {
        fo4::hkx::AnimationSourceRequest request;
        request.data_dir = dataFolder;
        request.mo2_instance = mo2InstanceDir;
        request.profile = std::string(mo2Profile);
        return fo4::hkx::open_animation_sources(request);
    } catch (const fo4::hkx::AnimationSourceError& error) {
        return std::unexpected(Error{"InvalidSources", error.what()});
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InvalidSources", error.what()});
    }
}

void addSourceNotes(nlohmann::json& reply, const std::vector<std::string>& notes) {
    for (const auto& note : notes) reply["notes"].push_back(note);
}

void addSubgraphManifest(nlohmann::json& reply, std::uint64_t manifestId,
                         const fo4::archive::AssetResolver& assets, const std::string& where) {
    const std::string inner = fo4::hkx::subgraph_manifest_path(manifestId);

    const auto manifest = fo4::hkx::read_subgraph_manifest(assets, manifestId);
    if (!manifest) {
        reply["notes"].push_back("no " + inner + " under " + where +
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

    const auto assets = openAssets(dataFolder, {}, {});
    if (!assets) return std::unexpected(assets.error());
    addSourceNotes(reply, assets->notes);
    addSubgraphManifest(reply, set.subgraphs.front().id, assets->resolver, dataFolder.string());
    return reply;
}

// Invariant: exactly one of id and behaviorPath names the subgraph, so the reply never mixes a given id with a computed one.
Result<nlohmann::json> HkxService::subgraphManifest(std::string_view idText, std::string_view behaviorPath,
                                                    const std::vector<std::string>& pathPrefixes,
                                                    const std::filesystem::path& dataFolder,
                                                    const std::filesystem::path& mo2InstanceDir,
                                                    std::string_view mo2Profile) const {
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
    if (dataFolder.empty() && mo2InstanceDir.empty()) {
        reply["notes"].push_back("no data folder was given, so no AnimationFileData manifest was read");
        return reply;
    }
    const auto assets = openAssets(dataFolder, mo2InstanceDir, mo2Profile);
    if (!assets) return std::unexpected(assets.error());
    addSourceNotes(reply, assets->notes);
    addSubgraphManifest(reply, id, assets->resolver,
                        mo2InstanceDir.empty() ? dataFolder.string() : "the MO2 instance " + mo2InstanceDir.string());
    return reply;
}

namespace {

nlohmann::json chainLinkJson(const fo4::hkx::HkxChainLink& link) {
    return {
        {"role", link.role},
        {"declared", link.declared},
        {"resolved", link.resolved},
        {"source", link.source},
        {"exists", link.exists},
        {"blocked", link.blocked},
        {"note", link.note},
    };
}

nlohmann::json chainJson(const fo4::hkx::HkxProjectChain& chain) {
    nlohmann::json links = nlohmann::json::array();
    for (const auto& link : chain.links) links.push_back(chainLinkJson(link));
    nlohmann::json animations = nlohmann::json::array();
    for (const auto& animation : chain.animations) animations.push_back(chainLinkJson(animation));
    return {
        {"root", chain.root},
        {"links", std::move(links)},
        {"animations", std::move(animations)},
        {"problems", chain.problems},
        {"warnings", chain.warnings},
    };
}

}

Result<nlohmann::json> HkxService::projectChain(std::string_view hkxPath, const std::filesystem::path& dataFolder,
                                                const std::filesystem::path& mo2InstanceDir,
                                                std::string_view mo2Profile) const {
    const auto assets = openAssets(dataFolder, mo2InstanceDir, mo2Profile);
    if (!assets) return std::unexpected(assets.error());
    auto reply = chainJson(fo4::hkx::resolve_project_chain(assets->resolver, hkxPath));
    reply["ok"] = reply["problems"].empty();
    reply["notes"] = nlohmann::json::array();
    addSourceNotes(reply, assets->notes);
    return reply;
}

// Invariant: a check that stops at fileLimit says so through truncated, so a short list never reads as a small project.
Result<nlohmann::json> HkxService::projectCheck(std::string_view hkxPath, const std::filesystem::path& dataFolder,
                                                const std::filesystem::path& mo2InstanceDir,
                                                std::string_view mo2Profile, std::size_t fileLimit,
                                                std::string_view reportFormat) const {
    if (!reportFormat.empty() && reportFormat != "csv" && reportFormat != "html") {
        return std::unexpected(Error{"InvalidFormat", "format must be csv or html, or empty for none"});
    }
    const auto assets = openAssets(dataFolder, mo2InstanceDir, mo2Profile);
    if (!assets) return std::unexpected(assets.error());
    const auto chain = fo4::hkx::resolve_project_chain(assets->resolver, hkxPath);
    const auto check = fo4::hkx::check_project_behaviors(assets->resolver, chain, fileLimit);

    std::size_t errors = 0;
    std::size_t warnings = 0;
    nlohmann::json findings = nlohmann::json::array();
    for (const auto& finding : check.findings) {
        (finding.level == "error" ? errors : warnings) += 1;
        findings.push_back({{"file", finding.file}, {"level", finding.level}, {"code", finding.code}, {"message", finding.message}});
    }
    const auto findingJson = [&check](std::size_t at) {
        const auto& finding = check.findings[at];
        return nlohmann::json{{"severity", finding.level}, {"code", finding.code}, {"message", finding.message}};
    };
    std::size_t reportErrors = 0;
    std::size_t reportWarnings = 0;
    nlohmann::json reportFiles = nlohmann::json::array();
    for (const auto& file : check.per_file) {
        reportErrors += file.errors;
        reportWarnings += file.warnings;
        nlohmann::json losses = nlohmann::json::array();
        for (const auto& loss : file.round_trip) {
            losses.push_back({{"kind", loss.kind}, {"object_id", nullptr}, {"member", nullptr}, {"message", loss.message}, {"blocks_save", true}});
        }
        nlohmann::json fileFindings = nlohmann::json::array();
        if (file.error.empty()) {
            for (std::size_t at = file.first_finding; at < file.first_finding + file.finding_count; ++at) {
                fileFindings.push_back(findingJson(at));
            }
        }
        reportFiles.push_back({{"path", file.path},
                               {"name", file.name},
                               {"unreadable", file.error.empty() ? nlohmann::json(nullptr) : nlohmann::json(file.error)},
                               {"errors", file.errors},
                               {"warnings", file.warnings},
                               {"round_trip_losses", std::move(losses)},
                               {"findings", std::move(fileFindings)}});
    }
    nlohmann::json projectFindings = nlohmann::json::array();
    for (std::size_t at = 0; at < fo4::hkx::project_finding_count(check); ++at) {
        auto entry = findingJson(at);
        entry["file"] = check.findings[at].file;
        projectFindings.push_back(std::move(entry));
    }
    const std::size_t unreadable = fo4::hkx::unreadable_files(check);
    const std::size_t losses = fo4::hkx::round_trip_losses(check);
    const std::size_t lossyFiles = fo4::hkx::files_with_round_trip_losses(check);
    const std::string summary = fo4::hkx::describe_project_check(check);
    nlohmann::json report{
        {"project", chain.root},
        {"summary", summary},
        {"truncated", check.truncated},
        {"totals", {{"files", check.per_file.size()},
                    {"readable", check.per_file.size() - unreadable},
                    {"unreadable", unreadable},
                    {"errors", reportErrors},
                    {"warnings", reportWarnings},
                    {"round_trip_losses", losses},
                    {"files_with_round_trip_losses", lossyFiles}}},
        {"files", std::move(reportFiles)},
        {"project_findings", std::move(projectFindings)},
    };
    nlohmann::json reply{
        {"root", chain.root},
        {"chain_problems", chain.problems},
        {"chain_warnings", chain.warnings},
        {"files", check.files},
        {"read", check.read},
        {"truncated", check.truncated},
        {"complete", !check.truncated},
        {"errors", errors},
        {"warnings", warnings},
        {"findings", std::move(findings)},
        {"unreadable", unreadable},
        {"round_trip_losses", losses},
        {"files_with_round_trip_losses", lossyFiles},
        {"summary", summary},
        {"report", std::move(report)},
        {"ok", chain.problems.empty() && errors == 0 && losses == 0 && !check.truncated},
        {"notes", nlohmann::json::array()},
    };
    if (!reportFormat.empty()) {
        reply["format"] = std::string(reportFormat);
        reply["rendered"] = fo4::hkx::render_project_report(
            chain, check, reportFormat == "csv" ? fo4::hkx::HkxReportFormat::Csv : fo4::hkx::HkxReportFormat::Html);
    }
    addSourceNotes(reply, assets->notes);
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
    query.decode_values = false;
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

namespace {

nlohmann::json motionPoseJson(const fo4::hkx::HkxMotionPose& pose) {
    return nlohmann::json::array({pose.position[0], pose.position[1], pose.position[2], pose.turn_radians});
}

const char* motionKindName(fo4::hkx::HkxMotionKind kind) {
    switch (kind) {
        case fo4::hkx::HkxMotionKind::Default: return "default";
        case fo4::hkx::HkxMotionKind::Unsupported: return "unsupported";
        case fo4::hkx::HkxMotionKind::None: break;
    }
    return "none";
}

}  // namespace

struct HkxService::MotionSnapshot {
    std::filesystem::path animationPath;
    FileStamp stamp;
    fo4::hkx::HkxExtractedMotion motion;
};

// Constraint: playback asks for the pose of one clip every frame, so the decoded motion is kept until the animation path or stamp changes.
// Constraint: every number comes from Radicle motion_at_time, summarize_motion and the motion_preview helpers; this only shapes the reply.
Result<nlohmann::json> HkxService::extractedMotion(const std::filesystem::path& animationPath, float time, bool loop,
                                                   bool includePath, std::size_t pointLimit) const {
    if (auto valid = requireFile(animationPath, "Animation"); !valid) return std::unexpected(valid.error());
    auto stamp = stampOf(animationPath);
    if (!stamp) return std::unexpected(stamp.error());
    std::shared_ptr<const MotionSnapshot> snapshot;
    {
        std::scoped_lock lock(motionMutex_);
        if (motionCache_ && motionCache_->animationPath == animationPath && motionCache_->stamp == *stamp) {
            snapshot = motionCache_;
        }
    }
    if (!snapshot) {
        HkxPackfile packfile;
        if (auto opened = openPackfile(animationPath, packfile); !opened) return std::unexpected(opened.error());
        auto loaded = std::make_shared<MotionSnapshot>();
        loaded->animationPath = animationPath;
        loaded->stamp = *stamp;
        if (auto checked = checkStatus(fo4::hkx::read_extracted_motion(packfile, loaded->motion), "extracted motion read");
            !checked) {
            return std::unexpected(checked.error());
        }
        std::scoped_lock lock(motionMutex_);
        motionCache_ = loaded;
        snapshot = loaded;
    }
    const auto& motion = snapshot->motion;
    const bool previewable = fo4::hkx::motion_previewable(motion);
    nlohmann::json reply{
        {"class_name", motion.class_name},
        {"kind", motionKindName(motion.kind)},
        {"previewable", previewable},
        {"duration", motion.duration},
        {"up", motion.up},
        {"forward", motion.forward},
        {"sample_count", motion.samples.size()},
    };
    if (!previewable) return reply;

    const auto summary = fo4::hkx::summarize_motion(motion);
    reply["origin"] = motionPoseJson(fo4::hkx::motion_at_fraction(motion, 0.0f));
    reply["at"] = motionPoseJson(fo4::hkx::motion_at_time(motion, time, loop));
    reply["time"] = time;
    reply["loop"] = loop;
    reply["summary"] = {{"net_translation", summary.net_translation},
                        {"net_turn_radians", summary.net_turn_radians},
                        {"mean_planar_speed", summary.mean_planar_speed},
                        {"mean_turn_rate", summary.mean_turn_rate}};
    if (includePath) {
        auto path = nlohmann::json::array();
        for (const auto& point : fo4::hkx::motion_path_points(motion, pointLimit)) path.push_back(motionPoseJson(point));
        auto marks = nlohmann::json::array();
        for (const auto& mark : fo4::hkx::motion_second_marks(motion)) marks.push_back(motionPoseJson(mark));
        reply["path"] = std::move(path);
        reply["path_truncated"] = motion.samples.size() > pointLimit;
        reply["marks"] = std::move(marks);
    }
    return reply;
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

Result<HkxSkeleton> skeletonFromBytes(const std::vector<std::byte>& bytes) {
    HkxPackfile packfile;
    if (auto status = checkStatus(fo4::hkx::read_packfile(bytes, packfile), "packfile read"); !status) {
        return std::unexpected(status.error());
    }
    HkxSkeleton skeletonData;
    if (auto status = checkStatus(fo4::hkx::read_skeleton(packfile, skeletonData), "skeleton read"); !status) {
        return std::unexpected(status.error());
    }
    return skeletonData;
}

std::string sourceHash(const std::vector<std::byte>& bytes) {
    std::ostringstream text;
    text << std::hex << std::setfill('0') << std::setw(16) << fo4::fnv1a64_bytes(bytes);
    return text.str();
}

// Invariant: a bone entry gets its pose and lock only when the skeleton holds one of each per bone, so an index never misaligns.
void addPoseDetails(nlohmann::json& reply, const HkxSkeleton& skeletonData) {
    auto& bones = reply["bones"];
    const auto count = skeletonData.bone_names.size();
    if (!bones.is_array() || bones.size() != count || skeletonData.reference_pose.size() != count ||
        skeletonData.lock_translation.size() != count) {
        return;
    }
    for (std::size_t i = 0; i < count; ++i) {
        bones[i]["lock"] = static_cast<bool>(skeletonData.lock_translation[i]);
        bones[i]["translation"] = skeletonData.reference_pose[i].translation;
        bones[i]["rotation"] = skeletonData.reference_pose[i].rotation;
        bones[i]["scale"] = skeletonData.reference_pose[i].scale;
    }
}

}  // namespace

// Invariant: the document names each bone's partner only when that bone exists, so a pair is never guessed.
Result<nlohmann::json> HkxService::skeletonDocument(const std::filesystem::path& skeletonPath) const {
    if (auto valid = requireFile(skeletonPath, "Skeleton"); !valid) return std::unexpected(valid.error());
    auto bytes = readBytes(skeletonPath);
    if (!bytes) return std::unexpected(bytes.error());
    auto skeletonData = skeletonFromBytes(*bytes);
    if (!skeletonData) return std::unexpected(skeletonData.error());
    auto reply = documentJson(fo4::hkx::skeleton_document(*skeletonData));
    addPoseDetails(reply, *skeletonData);
    reply["source_hash"] = sourceHash(*bytes);
    return reply;
}

// Constraint: this reports the edited skeleton and never writes a file; HkxService::saveSkeleton and writeSkeleton are the only writers.
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

namespace {

Result<std::vector<fo4::hkx::HkxSkeletonEdit>> parseSkeletonEdits(const nlohmann::json& edits) {
    if (!edits.is_array()) return std::unexpected(Error{"InvalidEdits", "edits must be a JSON array"});
    std::vector<fo4::hkx::HkxSkeletonEdit> parsed;
    for (std::size_t i = 0; i < edits.size(); ++i) {
        const auto& item = edits[i];
        const std::string where = "edit " + std::to_string(i + 1) + ": ";
        if (!item.is_object()) return std::unexpected(Error{"InvalidEdits", where + "must be an object"});
        fo4::hkx::HkxSkeletonEdit edit;
        const auto kind = item.find("kind");
        if (kind == item.end() || !kind->is_string()) return std::unexpected(Error{"InvalidEdits", where + "kind is missing"});
        const auto kindText = kind->get<std::string>();
        if (kindText == "rename") {
            edit.kind = fo4::hkx::HkxSkeletonEditKind::Rename;
        } else if (kindText == "reparent") {
            edit.kind = fo4::hkx::HkxSkeletonEditKind::Reparent;
        } else if (kindText == "remove") {
            edit.kind = fo4::hkx::HkxSkeletonEditKind::Remove;
        } else if (kindText == "set_pose") {
            edit.kind = fo4::hkx::HkxSkeletonEditKind::SetPose;
        } else if (kindText == "set_lock") {
            edit.kind = fo4::hkx::HkxSkeletonEditKind::SetLock;
        } else {
            return std::unexpected(Error{"InvalidEdits", where + "kind must be rename, reparent, remove, set_pose or set_lock"});
        }
        const auto bone = item.find("bone");
        if (bone == item.end() || !bone->is_number_integer() || bone->get<std::int64_t>() < 0) {
            return std::unexpected(Error{"InvalidEdits", where + "bone must be zero or more"});
        }
        edit.bone = static_cast<std::size_t>(bone->get<std::int64_t>());
        if (const auto name = item.find("name"); name != item.end()) {
            if (!name->is_string()) return std::unexpected(Error{"InvalidEdits", where + "name must be text"});
            edit.name = name->get<std::string>();
        }
        if (const auto parent = item.find("parent"); parent != item.end()) {
            if (!parent->is_number_integer() || parent->get<std::int64_t>() < -1 ||
                parent->get<std::int64_t>() > std::numeric_limits<std::int16_t>::max()) {
                return std::unexpected(Error{"InvalidEdits", where + "parent must be -1 or a bone index"});
            }
            edit.parent = static_cast<std::int16_t>(parent->get<std::int64_t>());
        } else if (edit.kind == fo4::hkx::HkxSkeletonEditKind::Reparent) {
            return std::unexpected(Error{"InvalidEdits", where + "reparent needs a parent"});
        }
        if (edit.kind == fo4::hkx::HkxSkeletonEditKind::SetPose) {
            const auto fill = [&](const char* key, float* lanes, std::size_t size) -> std::string {
                const auto found = item.find(key);
                if (found == item.end() || !found->is_array() || found->size() != size) {
                    return std::string(key) + " must be a list of " + std::to_string(size) + " numbers";
                }
                for (std::size_t k = 0; k < size; ++k) {
                    if (!(*found)[k].is_number()) return std::string(key) + " must be a list of " + std::to_string(size) + " numbers";
                    const double wide = (*found)[k].get<double>();
                    if (!std::isfinite(wide) || std::abs(wide) > static_cast<double>(std::numeric_limits<float>::max())) {
                        return std::string(key) + " must hold finite numbers";
                    }
                    lanes[k] = static_cast<float>(wide);
                }
                return {};
            };
            for (const auto& why : {fill("translation", edit.pose.translation.data(), 3),
                                    fill("rotation", edit.pose.rotation.data(), 4),
                                    fill("scale", edit.pose.scale.data(), 3)}) {
                if (!why.empty()) return std::unexpected(Error{"InvalidEdits", where + "set_pose: " + why});
            }
        } else if (edit.kind == fo4::hkx::HkxSkeletonEditKind::SetLock) {
            const auto locked = item.find("locked");
            if (locked == item.end() || !locked->is_boolean()) {
                return std::unexpected(Error{"InvalidEdits", where + "set_lock needs locked true or false"});
            }
            edit.locked = locked->get<bool>();
        }
        parsed.push_back(std::move(edit));
    }
    return parsed;
}

std::string lowerText(std::string text) {
    std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

}  // namespace

// Constraint: a document is saved only to an existing folder, as a plain .hkx file name, never over a directory or a protected name.
Result<void> checkSaveTarget(const std::filesystem::path& target, std::string_view what) {
    if (lowerText(target.extension().string()) != ".hkx") {
        return std::unexpected(Error{"InvalidPath", std::string(what) + " is saved to a .hkx file"});
    }
    if (const auto why = core::protectedWriteName(target.filename().string())) {
        return std::unexpected(Error{"ProtectedFile", target.filename().string() + " " + *why});
    }
    std::error_code ec;
    const auto folder = target.has_parent_path() ? target.parent_path() : std::filesystem::path(".");
    if (!std::filesystem::is_directory(folder, ec)) {
        return std::unexpected(Error{"OutputFolderMissing", "Not saved: folder does not exist: " + folder.string()});
    }
    if (std::filesystem::exists(target, ec) && !std::filesystem::is_regular_file(target, ec)) {
        return std::unexpected(Error{"InvalidPath", "The destination is not a file: " + target.string()});
    }
    return {};
}

namespace {

Result<void> checkSkeletonDestination(const std::filesystem::path& source, const std::filesystem::path& target,
                                      bool overwrite) {
    if (auto allowed = checkSaveTarget(target, "A skeleton"); !allowed) return allowed;
    std::error_code ec;
    if (!overwrite && std::filesystem::exists(target, ec) && !std::filesystem::equivalent(source, target, ec)) {
        return std::unexpected(Error{"DestinationExists",
                                     "A different file already exists at " + target.string() + "; choose a new file name"});
    }
    return {};
}

}  // namespace

// Constraint: without overwrite the destination is claimed with commit_if_absent, so a file that appears after the check is never replaced.
Result<void> publishHkxBytes(const std::filesystem::path& target, const std::vector<std::byte>& bytes,
                             bool overwrite, std::string_view failure) {
    try {
        auto staged = fo4::io::StagedFile::create(target);
        staged.write(bytes);
        staged.finish();
        staged.verify(bytes.size());
        if (overwrite) {
            std::move(staged).commit();
        } else if (!std::move(staged).commit_if_absent()) {
            return std::unexpected(Error{"OutputExists", "Not saved: " + target.string() + " already exists"});
        }
        return {};
    } catch (const std::exception& error) {
        return std::unexpected(Error{std::string(failure), std::string("Not saved: ") + error.what()});
    }
}

namespace {

std::string writeRefusal(HkxStatus status,const fo4::hkx::HkxSkeletonWrite& written) {
    if (!written.refusal.empty()) return written.refusal;
    return "the skeleton writer failed: " + std::string(kStatusNames[static_cast<std::size_t>(status)]);
}

}  // namespace

// Invariant: saveable is the verdict of the real writer on this list, so a preview never promises a save that would be refused.
Result<nlohmann::json> HkxService::previewSkeleton(const std::filesystem::path& skeletonPath,
                                                   const nlohmann::json& edits) const {
    if (auto valid = requireFile(skeletonPath, "Skeleton"); !valid) return std::unexpected(valid.error());
    auto parsed = parseSkeletonEdits(edits);
    if (!parsed) return std::unexpected(parsed.error());
    auto bytes = readBytes(skeletonPath);
    if (!bytes) return std::unexpected(bytes.error());
    auto skeletonData = skeletonFromBytes(*bytes);
    if (!skeletonData) return std::unexpected(skeletonData.error());

    fo4::hkx::HkxSkeleton edited;
    const auto report = fo4::hkx::apply_skeleton_edits(*skeletonData, *parsed, edited);
    if (!report.refusal.empty()) return std::unexpected(Error{"Refused", report.refusal});
    auto reply = documentJson(fo4::hkx::skeleton_document(edited));
    addPoseDetails(reply, edited);
    reply["source_hash"] = sourceHash(*bytes);
    reply["edit_count"] = parsed->size();
    reply["changed"] = report.changed;

    fo4::hkx::HkxSkeletonWrite written;
    const auto status = fo4::hkx::write_skeleton(*bytes, *parsed, written);
    const bool saveable = status == HkxStatus::Ok && written.changed;
    reply["saveable"] = saveable;
    if (status != HkxStatus::Ok) {
        reply["save_refusal"] = writeRefusal(status, written);
    } else if (!written.changed) {
        reply["save_refusal"] = "the edits change nothing";
    } else {
        reply["save_refusal"] = nullptr;
    }
    return reply;
}

// Invariant: a refusal or a failed write leaves the source and the destination as they were; saved is reported only after a reread.
Result<nlohmann::json> HkxService::saveSkeleton(const std::filesystem::path& skeletonPath,
                                                const nlohmann::json& edits,
                                                const std::filesystem::path& outputPath,
                                                std::string_view expectedHash,
                                                bool overwrite) const {
    if (auto valid = requireFile(skeletonPath, "Skeleton"); !valid) return std::unexpected(valid.error());
    auto parsed = parseSkeletonEdits(edits);
    if (!parsed) return std::unexpected(parsed.error());
    if (parsed->empty()) return std::unexpected(Error{"NothingToSave", "There are no edits to save"});
    const auto& target = outputPath.empty() ? skeletonPath : outputPath;
    if (auto allowed = checkSkeletonDestination(skeletonPath, target, overwrite); !allowed) {
        return std::unexpected(allowed.error());
    }

    auto bytes = readBytes(skeletonPath);
    if (!bytes) return std::unexpected(bytes.error());
    const auto hash = sourceHash(*bytes);
    if (!expectedHash.empty() && expectedHash != hash) {
        return std::unexpected(Error{"SourceChanged", "The skeleton file changed on disk since it was opened; reopen it before saving"});
    }

    fo4::hkx::HkxSkeletonWrite written;
    const auto status = fo4::hkx::write_skeleton(*bytes, *parsed, written);
    if (status != HkxStatus::Ok) return std::unexpected(Error{"Refused", writeRefusal(status, written)});
    if (!written.changed) {
        return nlohmann::json{{"saved", false}, {"changed", false}, {"path", target.string()}, {"source_hash", hash}};
    }

    std::error_code equivalence;
    const bool inPlace = std::filesystem::equivalent(skeletonPath, target, equivalence);
    if (inPlace) {
        auto again = readBytes(skeletonPath);
        if (!again || sourceHash(*again) != hash) {
            return std::unexpected(Error{"SourceChanged", "The skeleton file changed on disk while it was being saved"});
        }
    }
    if (auto published = publishHkxBytes(target, written.bytes, inPlace || overwrite, "WriteFailed"); !published) {
        return std::unexpected(published.error());
    }

    auto reopened = readBytes(target);
    if (!reopened) return std::unexpected(Error{"VerifyFailed", "The saved file could not be read back: " + reopened.error().message});
    if (*reopened != written.bytes) {
        return std::unexpected(Error{"VerifyFailed", "The saved file does not match what was written: " + target.string()});
    }
    auto skeletonData = skeletonFromBytes(*reopened);
    if (!skeletonData) {
        return std::unexpected(Error{"VerifyFailed", "The saved file does not read back: " + skeletonData.error().message});
    }
    auto reply = documentJson(fo4::hkx::skeleton_document(*skeletonData));
    addPoseDetails(reply, *skeletonData);
    reply["saved"] = true;
    reply["changed"] = true;
    reply["path"] = target.string();
    reply["source_hash"] = sourceHash(*reopened);
    return reply;
}

// Invariant: the file at skeletonPath changes only when outPath names it and overwrite is true; an empty outPath is a preview.
Result<nlohmann::json> HkxService::writeSkeleton(const std::filesystem::path& skeletonPath,
                                                 const nlohmann::json& edits,
                                                 const std::filesystem::path& outPath,
                                                 bool overwrite,
                                                 std::string_view expectedHash) const {
    if (outPath.empty()) {
        auto reply = previewSkeleton(skeletonPath, edits);
        if (!reply) return reply;
        (*reply)["preview"] = true;
        (*reply)["saved"] = false;
        return reply;
    }
    if (auto allowed = checkSaveTarget(outPath, "A skeleton"); !allowed) return std::unexpected(allowed.error());
    std::error_code ec;
    if (std::filesystem::exists(outPath, ec) && !overwrite) {
        return std::unexpected(Error{"OutputExists", "Not saved: " + outPath.string() +
                                                        " already exists; choose another file or allow overwrite"});
    }
    auto saved = saveSkeleton(skeletonPath, edits, outPath, expectedHash, overwrite);
    if (saved) (*saved)["preview"] = false;
    return saved;
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
            {"plane_min_angle", optionalJson(constraint.plane_min_angle)},
            {"plane_max_angle", optionalJson(constraint.plane_max_angle)},
            {"friction", optionalJson(constraint.friction)},
            {"cone_angle_mode", optionalJson(constraint.cone_angle_mode)},
            {"plane_angle_mode", optionalJson(constraint.plane_angle_mode)},
            {"has_preview_limits", constraint.has_preview_limits}};
    });
    reply["bone_bindings"] = limitedRows(model.bone_bindings, limit, [](const fo4::hkx::HkxRagdollBoneBinding& binding) {
        return nlohmann::json{{"bone_index", binding.bone_index}, {"body_id", binding.body_id}};
    });
    fo4::hkx::HkxPackfile packfile;
    fo4::hkx::HkxRagdollMeasures measures;
    if (fo4::hkx::read_packfile(std::span<const std::byte>(*bytes), packfile) != HkxStatus::Ok ||
        fo4::hkx::read_ragdoll_measures(packfile, measures) != HkxStatus::Ok) {
        measures = {};
    }
    reply["capsules"] = limitedRows(measures.capsules, limit, [](const fo4::hkx::HkxRagdollCapsuleMeasure& capsule) {
        return nlohmann::json{{"shape", capsule.shape}, {"a", capsule.a}, {"b", capsule.b}, {"radius", capsule.radius},
                              {"editable", capsule.editable}, {"reason", capsule.reason}};
    });
    reply["materials"] = limitedRows(measures.materials, limit, [](const fo4::hkx::HkxRagdollMaterialMeasure& material) {
        return nlohmann::json{{"material", material.material}, {"dynamic_friction", material.dynamic_friction},
                              {"static_friction", material.static_friction}, {"restitution", material.restitution},
                              {"shared", material.shared}};
    });
    reply["capsule_total"] = measures.capsules.size();
    reply["material_total"] = measures.materials.size();
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
    auto bytes = readBytes(path);
    if (!bytes) return std::unexpected(bytes.error());
    HkxPackfile packfile;
    if (auto opened = checkStatus(fo4::hkx::read_packfile(*bytes, packfile), "packfile read"); !opened) {
        return std::unexpected(opened.error());
    }

    std::vector<fo4::hkx::HkxSkeletonMapper> found;
    if (auto status = checkStatus(fo4::hkx::read_skeleton_mappers(packfile, found), "skeleton mapper read"); !status) {
        return std::unexpected(status.error());
    }
    auto rows = nlohmann::json::array();
    for (const auto& mapper : found) rows.push_back(mapperJson(mapper, recompute, limit));
    return nlohmann::json{{"mapper_count", found.size()},
                          {"recompute", recompute},
                          {"source_sha256", fo4::io::sha256_hex(*bytes)},
                          {"mappers", std::move(rows)}};
}

namespace {

Error invalidMapperEdit(std::size_t index, const std::string& why) {
    return Error{"InvalidMapperEdit", "edit " + std::to_string(index) + ": " + why};
}

std::string mapperEditKind(const nlohmann::json& item) {
    const auto found = item.find("kind");
    return found != item.end() && found->is_string() ? found->get<std::string>() : std::string{};
}

std::optional<std::int16_t> mapperInt16Field(const nlohmann::json& object, const char* key) {
    const auto found = object.find(key);
    if (found == object.end() || !found->is_number_integer()) return std::nullopt;
    if (found->is_number_unsigned()) {
        const auto value = found->get<std::uint64_t>();
        if (value > 32767) return std::nullopt;
        return static_cast<std::int16_t>(value);
    }
    const auto value = found->get<std::int64_t>();
    if (value < -32768 || value > 32767) return std::nullopt;
    return static_cast<std::int16_t>(value);
}

std::string overlayLanes(const nlohmann::json& pose, const char* key, std::size_t count, std::size_t first,
                         fo4::hkx::HkxMapperTransform& transform) {
    const auto found = pose.find(key);
    if (found == pose.end()) return {};
    if (!found->is_array() || found->size() != count) {
        return std::string(key) + " must be a list of " + std::to_string(count) + " numbers";
    }
    for (std::size_t i = 0; i < count; ++i) {
        if (!(*found)[i].is_number()) return std::string(key) + " must hold only numbers";
        const auto value = (*found)[i].get<float>();
        if (!std::isfinite(value)) return std::string(key) + " must hold finite numbers";
        transform.lanes[first + i] = value;
    }
    return {};
}

// Invariant: only the members the pose lists replace lanes, so padding lanes 3 and 11 and an unlisted scale keep their value.
std::string overlayPose(const nlohmann::json& edit, const char* key, bool needsTranslationAndRotation,
                        fo4::hkx::HkxMapperTransform& transform) {
    const auto found = edit.find(key);
    if (found == edit.end() || !found->is_object()) return std::string(key) + " must be an object";
    if (needsTranslationAndRotation && (!found->contains("translation") || !found->contains("rotation"))) {
        return std::string(key) + " needs translation and rotation";
    }
    if (auto why = overlayLanes(*found, "translation", 3, 0, transform); !why.empty()) return why;
    if (auto why = overlayLanes(*found, "rotation", 4, 4, transform); !why.empty()) return why;
    return overlayLanes(*found, "scale", 3, 8, transform);
}

Result<fo4::hkx::HkxMapperEdit> parseMapperEdit(std::size_t index, const nlohmann::json& item,
                                                const fo4::hkx::HkxSkeletonMapper& current) {
    using Kind = fo4::hkx::HkxMapperEditKind;
    const auto kindIt = item.find("kind");
    if (kindIt == item.end() || !kindIt->is_string()) return std::unexpected(invalidMapperEdit(index, "kind is missing"));
    const auto kind = kindIt->get<std::string>();

    fo4::hkx::HkxMapperEdit edit;
    const auto needRow = [&]() -> std::optional<std::string> {
        const auto row = item.find("row");
        if (row == item.end() || !row->is_number_integer() || row->get<std::int64_t>() < 0) {
            return "row must be a whole number of zero or more";
        }
        edit.row = static_cast<std::size_t>(row->get<std::int64_t>());
        return std::nullopt;
    };
    const auto needBones = [&](const char* a, const char* b, std::int16_t& outA, std::int16_t& outB)
        -> std::optional<std::string> {
        const auto boneA = mapperInt16Field(item, a);
        const auto boneB = mapperInt16Field(item, b);
        if (!boneA || !boneB) return std::string(a) + " and " + b + " must be whole numbers in the 16 bit range";
        outA = *boneA;
        outB = *boneB;
        return std::nullopt;
    };
    const auto fail = [&](const std::string& why) { return std::unexpected(invalidMapperEdit(index, kind + ": " + why)); };

    if (kind == "set_row_bones" || kind == "insert_row" || kind == "insert_row_with_transform") {
        edit.kind = kind == "set_row_bones" ? Kind::SetRowBones
                    : kind == "insert_row"  ? Kind::InsertRow
                                            : Kind::InsertRowWithTransform;
        if (kind == "insert_row" || kind == "insert_row_with_transform") {
            edit.row = current.rows.size();
            if (item.contains("row")) {
                if (auto why = needRow()) return fail(*why);
            }
            const auto duplicate = item.find("allow_duplicate_pair");
            edit.allow_duplicate_pair = duplicate != item.end() && duplicate->is_boolean() && duplicate->get<bool>();
        } else if (auto why = needRow()) {
            return fail(*why);
        }
        if (auto why = needBones("bone_a", "bone_b", edit.bone_a, edit.bone_b)) return fail(*why);
        if (kind == "insert_row_with_transform") {
            if (auto why = overlayPose(item, "transform", true, edit.transform); !why.empty()) return fail(why);
        }
    } else if (kind == "set_row_transform" || kind == "set_row_pose") {
        edit.kind = kind == "set_row_pose" ? Kind::SetRowPose : Kind::SetRowTransform;
        if (auto why = needRow()) return fail(*why);
        if (const auto pose = item.find("transform");
            kind == "set_row_pose" && pose != item.end() && pose->is_object() && pose->contains("scale")) {
            return fail("set_row_pose keeps the stored scale; use set_row_transform to change it");
        }
        if (kind == "set_row_transform" && edit.row < current.rows.size()) edit.transform = current.rows[edit.row].a_from_b;
        if (auto why = overlayPose(item, "transform", kind == "set_row_pose", edit.transform); !why.empty()) {
            return fail(why);
        }
    } else if (kind == "remove_row" || kind == "recompute_row") {
        edit.kind = kind == "remove_row" ? Kind::RemoveRow : Kind::RecomputeRow;
        if (auto why = needRow()) return fail(*why);
    } else if (kind == "set_chain_bones") {
        edit.kind = Kind::SetChainBones;
        if (auto why = needRow()) return fail(*why);
        if (auto why = needBones("start_bone_a", "start_bone_b", edit.bone_a, edit.bone_b)) return fail(*why);
        if (auto why = needBones("end_bone_a", "end_bone_b", edit.end_bone_a, edit.end_bone_b)) return fail(*why);
    } else if (kind == "set_chain_transforms") {
        edit.kind = Kind::SetChainTransforms;
        if (auto why = needRow()) return fail(*why);
        if (edit.row < current.chains.size()) {
            edit.transform = current.chains[edit.row].start_a_from_b;
            edit.end_transform = current.chains[edit.row].end_a_from_b;
        }
        if (auto why = overlayPose(item, "start_transform", false, edit.transform); !why.empty()) return fail(why);
        if (auto why = overlayPose(item, "end_transform", false, edit.end_transform); !why.empty()) return fail(why);
    } else if (kind == "set_mapping_type") {
        const auto type = item.find("mapping_type");
        if (type == item.end() || !type->is_number_integer()) return fail("mapping_type must be a whole number");
        const auto value = type->get<std::int64_t>();
        edit.kind = Kind::SetMappingType;
        edit.mapping_type = static_cast<std::int32_t>(std::clamp<std::int64_t>(value, -1, 1000));
    } else if (kind == "set_keep_unmapped_local") {
        const auto flag = item.find("value");
        if (flag == item.end() || !flag->is_boolean()) return fail("value must be true or false");
        edit.kind = Kind::SetKeepUnmappedLocal;
        edit.flag = flag->get<bool>();
    } else if (kind == "insert_chain" || kind == "remove_chain" || kind == "build_chains") {
        return fail("chain rows are edited in place only; Radicle never resizes chain rows and has no chain builder");
    } else {
        return fail("unknown edit kind");
    }
    return edit;
}

// Constraint: pairs default to the first unused bone of B with the same name, so an unnamed or unmatched bone gets no row.
std::string buildMapperRows(const nlohmann::json& item, fo4::hkx::HkxSkeletonMapper& mapper) {
    std::vector<fo4::hkx::HkxMappingPair> pairs;
    if (const auto given = item.find("pairs"); given != item.end()) {
        if (!given->is_array()) return "pairs must be a list of {bone_a, bone_b}";
        for (const auto& pair : *given) {
            const auto boneA = pair.is_object() ? mapperInt16Field(pair, "bone_a") : std::nullopt;
            const auto boneB = pair.is_object() ? mapperInt16Field(pair, "bone_b") : std::nullopt;
            if (!boneA || !boneB) return "each pair needs bone_a and bone_b as whole numbers in the 16 bit range";
            pairs.push_back({*boneA, *boneB});
        }
    } else {
        std::vector<bool> used(mapper.skeleton_b.bone_names.size(), false);
        for (std::size_t a = 0; a < mapper.skeleton_a.bone_names.size(); ++a) {
            if (mapper.skeleton_a.bone_names[a].empty()) continue;
            for (std::size_t b = 0; b < used.size(); ++b) {
                if (used[b] || mapper.skeleton_b.bone_names[b] != mapper.skeleton_a.bone_names[a]) continue;
                used[b] = true;
                pairs.push_back({static_cast<std::int16_t>(a), static_cast<std::int16_t>(b)});
                break;
            }
        }
    }
    if (pairs.empty()) return "no bone pairs: give pairs, or the two skeletons share no bone name";
    std::vector<fo4::hkx::HkxMapperSimpleRow> rows;
    if (auto why = fo4::hkx::build_skeleton_mapping(mapper.skeleton_a, mapper.skeleton_b, pairs, rows); !why.empty()) {
        return why;
    }
    mapper.rows = std::move(rows);
    return {};
}

}  // namespace

// Invariant: the writer always runs, in memory, so a preview never reports a resize the writer would refuse as writable.
Result<nlohmann::json> HkxService::editMapper(const std::filesystem::path& path,
                                              std::size_t mapper,
                                              const nlohmann::json& edits,
                                              const std::filesystem::path& outPath,
                                              bool overwrite,
                                              std::string_view expectedSource,
                                              std::size_t limit) const {
    if (auto valid = requireFile(path, "HKX"); !valid) return std::unexpected(valid.error());
    if (!edits.is_array() || edits.empty()) {
        return std::unexpected(Error{"InvalidMapperEdit", "edits must be a non-empty list"});
    }
    auto bytes = readBytes(path);
    if (!bytes) return std::unexpected(bytes.error());
    const auto sourceDigest = fo4::io::sha256_hex(*bytes);
    if (!expectedSource.empty() && expectedSource != sourceDigest) {
        return std::unexpected(Error{"MapperSourceChanged", "Not edited: " + path.string() +
                                                               " changed on disk after it was read -- read it again"});
    }
    HkxPackfile packfile;
    if (auto opened = checkStatus(fo4::hkx::read_packfile(*bytes, packfile), "packfile read"); !opened) {
        return std::unexpected(opened.error());
    }
    std::vector<fo4::hkx::HkxSkeletonMapper> edited;
    if (auto status = checkStatus(fo4::hkx::read_skeleton_mappers(packfile, edited), "skeleton mapper read"); !status) {
        return std::unexpected(status.error());
    }
    if (mapper >= edited.size()) {
        return std::unexpected(Error{"InvalidMapper", "file holds " + std::to_string(edited.size()) +
                                                         " mapper(s), so there is no mapper " + std::to_string(mapper)});
    }

    auto current = edited[mapper];
    for (std::size_t i = 0; i < edits.size(); ++i) {
        const auto& item = edits[i];
        if (!item.is_object()) return std::unexpected(invalidMapperEdit(i, "an edit must be an object"));
        if (mapperEditKind(item) == "build_rows") {
            if (auto why = buildMapperRows(item, current); !why.empty()) {
                return std::unexpected(Error{"MapperEditRefused", "edit " + std::to_string(i) + " (build_rows): " + why});
            }
            continue;
        }
        auto edit = parseMapperEdit(i, item, current);
        if (!edit) return std::unexpected(edit.error());
        fo4::hkx::HkxSkeletonMapper next;
        const auto report = fo4::hkx::apply_mapper_edit(current, *edit, next);
        if (!report.changed) {
            return std::unexpected(Error{"MapperEditRefused", "edit " + std::to_string(i) + " (" +
                                                                 mapperEditKind(item) + "): " + report.refusal});
        }
        current = std::move(next);
    }
    edited[mapper] = std::move(current);

    std::vector<std::byte> produced;
    std::string refusal;
    if (const auto status = fo4::hkx::write_skeleton_mappers(*bytes, edited, produced, &refusal);
        status != HkxStatus::Ok) {
        if (refusal.empty()) refusal = kStatusNames[static_cast<std::size_t>(status)];
        return std::unexpected(Error{"MapperWriteRefused", "Not writable: " + refusal});
    }
    HkxPackfile reopened;
    std::vector<fo4::hkx::HkxSkeletonMapper> reread;
    if (auto status = checkStatus(fo4::hkx::read_packfile(produced, reopened), "output packfile read"); !status) {
        return std::unexpected(status.error());
    }
    if (auto status = checkStatus(fo4::hkx::read_skeleton_mappers(reopened, reread), "output mapper read"); !status) {
        return std::unexpected(status.error());
    }

    const bool changed = produced != *bytes;
    bool saved = false;
    if (!outPath.empty()) {
        if (auto allowed = checkSaveTarget(outPath, "A mapper"); !allowed) return std::unexpected(allowed.error());
        std::error_code ec;
        const bool exists = std::filesystem::exists(outPath, ec);
        if (exists && !overwrite) {
            return std::unexpected(Error{"OutputExists", "Not saved: " + outPath.string() +
                                                            " already exists; choose another file or allow overwrite"});
        }
        const bool inPlace = exists && std::filesystem::equivalent(outPath, path, ec);
        if (inPlace) {
            auto again = readBytes(path);
            if (!again) return std::unexpected(again.error());
            if (*again != *bytes) {
                return std::unexpected(Error{"MapperSourceChanged", "Not saved: " + path.string() +
                                                                       " changed on disk while it was edited"});
            }
        }
        if (!inPlace || changed) {
            if (auto published = publishHkxBytes(outPath, produced, overwrite, "MapperWriteFailed"); !published) {
                return std::unexpected(published.error());
            }
            saved = true;
        }
    }

    auto rows = nlohmann::json::array();
    for (const auto& entry : reread) rows.push_back(mapperJson(entry, false, limit));
    return nlohmann::json{{"preview", outPath.empty()},
                          {"saved", saved},
                          {"changed", changed},
                          {"out_path", outPath.string()},
                          {"mapper", mapper},
                          {"edits_applied", edits.size()},
                          {"source_sha256", sourceDigest},
                          {"output_sha256", fo4::io::sha256_hex(produced)},
                          {"mapper_count", reread.size()},
                          {"mappers", std::move(rows)}};
}

// Invariant: units, up axis and environment are reported as not decoded, because the scene classes have no verified layout.
Result<nlohmann::json> HkxService::readScene(const std::filesystem::path& path) const {
    if (auto valid = requireFile(path, "HKX"); !valid) return std::unexpected(valid.error());
    HkxPackfile packfile;
    if (auto opened = openPackfile(path, packfile); !opened) return std::unexpected(opened.error());

    fo4::hkx::HkxSceneStructure scene;
    if (auto status = checkStatus(fo4::hkx::read_scene_structure(packfile, scene), "scene structure read"); !status) {
        return std::unexpected(status.error());
    }
    auto variants = nlohmann::json::array();
    for (const auto& variant : scene.variants) {
        variants.push_back({{"name", variant.name},
                            {"declared_class", variant.declared_class},
                            {"class_name", variant.class_name},
                            {"object", variant.object},
                            {"signature", variant.signature},
                            {"layout_known", variant.layout_known}});
    }
    auto sceneObjects = nlohmann::json::array();
    for (const auto& object : scene.scene_objects) {
        sceneObjects.push_back({{"class_name", object.class_name},
                                {"object", object.object},
                                {"signature", object.signature},
                                {"size", object.size},
                                {"layout_known", object.layout_known}});
    }
    return nlohmann::json{
        {"object_count", scene.object_count},
        {"has_root", scene.has_root},
        {"variants", std::move(variants)},
        {"scene_objects", std::move(sceneObjects)},
        {"unknown_classes", scene.unknown_classes},
        {"diagnostics", scene.diagnostics},
        {"metadata", {{"decoded", false},
                      {"reason", "units, up axis and environment variables have no verified layout, so they are not read"}}},
    };
}

namespace {

constexpr std::size_t kAnimationFacetCap = 400;

nlohmann::json animationProviderJson(const fo4::hkx::AnimationProvider& provider) {
    return nlohmann::json{{"label", provider.label},
                          {"origin", provider.archive ? "archive" : "loose"},
                          {"source", fo4::io::utf8(provider.source)},
                          {"file", provider.archive ? std::string() : fo4::io::utf8(provider.file)}};
}

nlohmann::json optionalProviderJson(const std::optional<fo4::hkx::AnimationProvider>& provider) {
    return provider ? animationProviderJson(*provider) : nlohmann::json(nullptr);
}

nlohmann::json optionalTextJson(const std::optional<std::string>& text) {
    return text ? nlohmann::json(*text) : nlohmann::json(nullptr);
}

nlohmann::json textArrayJson(const std::vector<std::string>& values) {
    auto out = nlohmann::json::array();
    for (const auto& value : values) out.push_back(value);
    return out;
}

nlohmann::json animationEntryJson(const fo4::hkx::AnimationIndexEntry& entry) {
    auto shadowed = nlohmann::json::array();
    for (const auto& provider : entry.shadowed) shadowed.push_back(animationProviderJson(provider));
    return nlohmann::json{{"path", entry.path},
                          {"kind", std::string(fo4::hkx::animation_kind_name(entry.kind))},
                          {"project", entry.project},
                          {"race", entry.race},
                          {"character", entry.character},
                          {"folder", entry.folder},
                          {"size", entry.size},
                          {"provider", animationProviderJson(entry.winner)},
                          {"shadowed", std::move(shadowed)}};
}

nlohmann::json animationFacetJson(const std::vector<std::string>& values) {
    auto shown = nlohmann::json::array();
    for (std::size_t i = 0; i < values.size() && i < kAnimationFacetCap; ++i) shown.push_back(values[i]);
    const std::size_t hidden = values.size() > kAnimationFacetCap ? values.size() - kAnimationFacetCap : 0;
    return nlohmann::json{{"values", std::move(shown)}, {"hidden", hidden}};
}

nlohmann::json animationActorJson(const fo4::hkx::AnimationActorAssets& actor) {
    return nlohmann::json{{"actor_root", actor.actor_root},
                          {"skeleton", optionalTextJson(actor.skeleton)},
                          {"skeleton_provider", optionalProviderJson(actor.skeleton_provider)},
                          {"skeleton_candidates", textArrayJson(actor.skeleton_candidates)},
                          {"skeleton_nif", optionalTextJson(actor.skeleton_nif)},
                          {"skeleton_nif_provider", optionalProviderJson(actor.skeleton_nif_provider)},
                          {"body", optionalTextJson(actor.body)},
                          {"body_provider", optionalProviderJson(actor.body_provider)},
                          {"body_candidates", textArrayJson(actor.body_candidates)},
                          {"body_candidate_total", actor.body_candidate_total},
                          {"notes", textArrayJson(actor.notes)}};
}

using fo4::hkx::HkxGltfSaveStatus;

bool sameFile(const std::filesystem::path& left, const std::filesystem::path& right) {
    std::error_code ec;
    if (std::filesystem::exists(right, ec) && std::filesystem::equivalent(left, right, ec)) return true;
    const auto a = std::filesystem::weakly_canonical(left, ec);
    const auto b = std::filesystem::weakly_canonical(right, ec);
    return !a.empty() && a == b;
}

// Requires every external buffer to resolve below the glTF's own folder, symlinks included.
fo4::hkx::HkxGltfUriLoader loadBesideGltf(const std::filesystem::path& gltfPath) {
    std::error_code ec;
    const auto folder = std::filesystem::weakly_canonical(gltfPath.parent_path(), ec);
    return [folder, ec](std::string_view uri) {
        if (ec || folder.empty()) throw std::runtime_error("the glTF folder could not be resolved");
        std::error_code inner;
        const std::filesystem::path named(std::u8string(reinterpret_cast<const char8_t*>(uri.data()), uri.size()));
        const auto target = std::filesystem::weakly_canonical(folder / named, inner);
        const auto relative = target.lexically_relative(folder);
        if (inner || relative.empty() || *relative.begin() == "..") {
            throw std::runtime_error("glTF buffer '" + std::string(uri) + "' is outside the folder of the glTF");
        }
        auto bytes = readBytes(target);
        if (!bytes) throw std::runtime_error(bytes.error().message);
        return std::move(*bytes);
    };
}

const char* saveHint(HkxGltfSaveStatus status) {
    switch (status) {
        case HkxGltfSaveStatus::BadImport: return " Re-export the glTF against this skeleton.";
        case HkxGltfSaveStatus::BadTemplate: return " Pick a template .hkx that holds one animation and its binding.";
        case HkxGltfSaveStatus::TemplateNoClip: return " Pick a template with exactly one animation clip.";
        case HkxGltfSaveStatus::TemplateNoBinding: return " Pick a template with one animation binding that names its clip.";
        case HkxGltfSaveStatus::TemplateFloatTracks: return " Pick a template with transform tracks only.";
        case HkxGltfSaveStatus::TemplateTrackMap: return " Pick a template whose binding maps each track to one bone of the skeleton.";
        case HkxGltfSaveStatus::UnmappedBone: return " Pick a template whose binding drives every bone the glTF animates, or remove that animation from the glTF.";
        case HkxGltfSaveStatus::AnnotationsReplaced: return " Set replace_annotations to true to replace the template's notes, or pick a template without any.";
        case HkxGltfSaveStatus::MotionReplaced: return " Set replace_motion to true to replace the template's root motion, or pick a template without any.";
        case HkxGltfSaveStatus::UnsupportedMotion: return " Pick a template whose root-motion frame is a Z-up default frame, or keep the glTF's root still.";
        case HkxGltfSaveStatus::SharedData: return " Pick a template whose clip is not shared with another object.";
        case HkxGltfSaveStatus::OverBudget: return " The saved clip would not match the glTF within the limits, so it was not written; check for a very large movement range or a curve the clip format cannot hold, and simplify or split the glTF animation.";
        case HkxGltfSaveStatus::SkeletonMismatch: return " Pick a template made for this skeleton, or load the skeleton the template names.";
        case HkxGltfSaveStatus::Ok:
        case HkxGltfSaveStatus::WriteFailed:
        case HkxGltfSaveStatus::ReadBack: break;
    }
    return "";
}

nlohmann::json gltfSaveReportJson(const fo4::hkx::HkxGltfSaveReport& report) {
    return {
        {"tracks", report.tracks},
        {"frames", report.frames},
        {"frame_rate", report.frame_rate},
        {"duration", report.duration},
        {"animated", report.animated},
        {"reference_tracks", report.reference_tracks},
        {"untracked_bones", report.untracked_bones},
        {"unmapped_joints", report.unmapped_joints},
        {"rest_mismatch", report.rest_mismatch},
        {"weight_channels", report.weight_channels},
        {"non_bone_channels", report.non_bone_channels},
        {"annotations_replaced", report.annotations_replaced},
        {"annotations_written", report.annotations_written},
        {"motion_replaced", report.motion_replaced},
        {"motion_samples", report.motion_samples},
        {"binding_linked", report.binding_linked},
        {"binding_skeleton_name", report.binding_skeleton_name},
        {"skeleton_match", report.skeleton_match},
        {"sampling_error", {{"position", report.sampling_position}, {"rotation", report.sampling_rotation}, {"scale", report.sampling_scale}}},
        {"readback_error", {{"position", report.readback_position}, {"rotation", report.readback_rotation}, {"scale", report.readback_scale}}},
    };
}

}  // namespace

namespace {

bool fileHoldsBytes(const std::filesystem::path& file, const std::vector<std::byte>& expected) {
    std::ifstream stream(file, std::ios::binary | std::ios::ate);
    if (!stream || static_cast<std::uint64_t>(stream.tellg()) != expected.size()) return false;
    stream.seekg(0, std::ios::beg);
    std::vector<std::byte> actual(expected.size());
    if (!actual.empty()) stream.read(reinterpret_cast<char*>(actual.data()), static_cast<std::streamsize>(actual.size()));
    return stream && actual == expected;
}

}  // namespace

struct HkxService::IndexSnapshot {
    std::string key;
    fo4::hkx::AnimationSources sources;
    fo4::hkx::AnimationIndex index;
};

Result<std::shared_ptr<const HkxService::IndexSnapshot>> HkxService::indexSnapshot(const AnimationIndexQuery& request) const {
    const auto key = fo4::io::utf8(request.dataFolder) + "|" + fo4::io::utf8(request.mo2Folder) + "|" + request.profile;
    {
        std::lock_guard lock(indexMutex_);
        if (!request.refresh && indexCache_ && indexCache_->key == key) return indexCache_;
    }
    auto built = std::make_shared<IndexSnapshot>();
    built->key = key;
    try {
        fo4::hkx::AnimationSourceRequest sourceRequest;
        sourceRequest.data_dir = request.dataFolder;
        sourceRequest.mo2_instance = request.mo2Folder;
        sourceRequest.profile = request.profile;
        built->sources = fo4::hkx::open_animation_sources(sourceRequest);
        built->index = fo4::hkx::build_animation_index(built->sources.resolver);
    } catch (const fo4::hkx::AnimationSourceError& error) {
        return std::unexpected(Error{"InvalidSources", error.what()});
    } catch (const std::exception& error) {
        return std::unexpected(Error{"AnimationIndex", error.what()});
    }
    std::shared_ptr<const IndexSnapshot> snapshot = built;
    std::lock_guard lock(indexMutex_);
    indexCache_ = snapshot;
    return snapshot;
}

Result<nlohmann::json> HkxService::extractAnimationAsset(const AnimationIndexQuery& sources, std::string_view path) const {
    auto shared = indexSnapshot(sources);
    if (!shared) return std::unexpected(shared.error());
    const auto snapshot = std::move(*shared);

    std::optional<fo4::hkx::AnimationAssetBytes> asset;
    try {
        asset = fo4::hkx::read_animation_asset(snapshot->sources.resolver, path);
    } catch (const fo4::archive::AssetPathError& error) {
        return std::unexpected(Error{"InvalidAssetPath", error.what()});
    } catch (const std::exception& error) {
        return std::unexpected(Error{"ExtractFailed", error.what()});
    }
    if (!asset) return std::unexpected(Error{"AssetNotFound", "no source holds " + std::string(path)});

    std::filesystem::path file = asset->loose_path;
    const bool extracted = asset->provider.archive;
    if (extracted) {
        try {
            const auto folder = std::filesystem::temp_directory_path() / "FO4RE_Hkx";
            std::error_code ec;
            if (std::filesystem::is_symlink(std::filesystem::symlink_status(folder, ec))) {
                return std::unexpected(Error{"ExtractFailed", "refusing to write through the symlink " + fo4::io::utf8(folder)});
            }
            std::filesystem::create_directories(folder);
            file = folder / (fo4::io::sha256_hex(asset->bytes).substr(0, 24) +
                             std::filesystem::path(asset->path).extension().string());
            const auto status = std::filesystem::symlink_status(file, ec);
            if (std::filesystem::is_symlink(status)) {
                return std::unexpected(Error{"ExtractFailed", "refusing to write through the symlink " + fo4::io::utf8(file)});
            }
            if (!std::filesystem::is_regular_file(status) || !fileHoldsBytes(file, asset->bytes)) {
                auto staged = fo4::io::StagedFile::create(file);
                staged.write(asset->bytes);
                staged.finish();
                staged.verify(asset->bytes.size());
                std::move(staged).commit();
            }
        } catch (const std::exception& error) {
            return std::unexpected(Error{"ExtractFailed", error.what()});
        }
    }
    return nlohmann::json{{"path", fo4::io::utf8(file)},
                          {"extracted", extracted},
                          {"asset", asset->path},
                          {"provider", animationProviderJson(asset->provider)},
                          {"size", asset->size}};
}

Result<nlohmann::json> HkxService::animationIndex(const AnimationIndexQuery& request) const {
    std::optional<fo4::hkx::AnimationKind> kind;
    if (!request.kind.empty()) {
        kind = fo4::hkx::animation_kind_from_name(request.kind);
        if (!kind) {
            return std::unexpected(Error{"InvalidKind", "kind must be animation, behaviour, project, character, skeleton or other: " + request.kind});
        }
    }

    auto shared = indexSnapshot(request);
    if (!shared) return std::unexpected(shared.error());
    const auto snapshot = std::move(*shared);

    const auto& index = snapshot->index;
    fo4::hkx::AnimationIndexFilter filter;
    filter.kind = kind;
    filter.project = request.project;
    filter.race = request.race;
    filter.character = request.character;
    filter.query = request.query;
    const auto matched = fo4::hkx::filter_animation_index(index, filter);
    const std::size_t offset = std::min(request.offset, matched.size());
    const std::size_t end = request.limit >= matched.size() - offset ? matched.size() : offset + request.limit;

    auto entries = nlohmann::json::array();
    for (std::size_t i = offset; i < end; ++i) entries.push_back(animationEntryJson(index.entries[matched[i]]));

    std::array<std::size_t, 6> kindCounts{};
    for (const auto& entry : index.entries) ++kindCounts[static_cast<std::size_t>(entry.kind)];
    nlohmann::json counts = nlohmann::json::object();
    for (const auto named : {fo4::hkx::AnimationKind::animation, fo4::hkx::AnimationKind::behaviour,
                             fo4::hkx::AnimationKind::project, fo4::hkx::AnimationKind::character,
                             fo4::hkx::AnimationKind::skeleton, fo4::hkx::AnimationKind::other}) {
        counts[std::string(fo4::hkx::animation_kind_name(named))] = kindCounts[static_cast<std::size_t>(named)];
    }

    auto sources = nlohmann::json::array();
    for (const auto& provider : fo4::hkx::animation_index_sources(snapshot->sources.resolver)) {
        sources.push_back(animationProviderJson(provider));
    }
    auto notes = textArrayJson(snapshot->sources.notes);
    for (const auto& note : index.notes) notes.push_back(note);

    const auto facets = fo4::hkx::animation_index_facets(index);
    nlohmann::json out{{"manager", snapshot->sources.manager},
                       {"profile", snapshot->sources.profile},
                       {"data_folder", fo4::io::utf8(snapshot->sources.data_dir)},
                       {"sources", std::move(sources)},
                       {"notes", std::move(notes)},
                       {"total", index.entries.size()},
                       {"counts", std::move(counts)},
                       {"matched", matched.size()},
                       {"offset", offset},
                       {"limit", request.limit},
                       {"truncated", end < matched.size()},
                       {"entries", std::move(entries)},
                       {"facets", {{"projects", animationFacetJson(facets.projects)},
                                   {"races", animationFacetJson(facets.races)},
                                   {"characters", animationFacetJson(facets.characters)}}}};
    if (!request.resolvePath.empty()) {
        out["actor"] = animationActorJson(fo4::hkx::resolve_animation_actor(index, request.resolvePath));
    }
    return out;
}

Result<nlohmann::json> HkxService::saveGltfAnimation(const GltfSaveRequest& request) const {
    if (auto valid = requireFile(request.templatePath, "Template HKX"); !valid) return std::unexpected(valid.error());
    if (auto valid = requireFile(request.skeletonPath, "Skeleton HKX"); !valid) return std::unexpected(valid.error());
    if (auto valid = requireFile(request.gltfPath, "glTF"); !valid) return std::unexpected(valid.error());
    if (request.outputPath.empty()) return std::unexpected(Error{"InvalidPath", "Output HKX path is empty"});
    // Invariant: the output target passes the mapper save checks (.hkx name, existing folder, regular file) before any read.
    if (auto allowed = checkSaveTarget(request.outputPath, "A glTF animation"); !allowed) return std::unexpected(allowed.error());
    for (const auto* input : {&request.templatePath, &request.skeletonPath, &request.gltfPath}) {
        if (sameFile(*input, request.outputPath)) {
            return std::unexpected(Error{"HkxGltfRefused", "Refused: the output path is an input file (" + input->string() +
                                                                 "); choose a new path so the template is never overwritten"});
        }
    }
    std::error_code ec;
    const bool exists = std::filesystem::exists(std::filesystem::symlink_status(request.outputPath, ec));
    if (exists && !request.overwrite) {
        return std::unexpected(Error{"OutputExists", "Refusing to replace an existing file without overwrite: " +
                                                         request.outputPath.string()});
    }

    auto templateBytes = readBytes(request.templatePath);
    if (!templateBytes) return std::unexpected(templateBytes.error());
    auto gltfBytes = readBytes(request.gltfPath);
    if (!gltfBytes) return std::unexpected(gltfBytes.error());
    auto skeletonData = loadSkeleton(request.skeletonPath);
    if (!skeletonData) return std::unexpected(skeletonData.error());

    fo4::hkx::HkxGltfOptions options;
    if (!request.animation.empty()) options.animation = request.animation;
    if (!request.skin.empty()) options.skin = request.skin;
    options.load_uri = loadBesideGltf(request.gltfPath);
    fo4::hkx::HkxGltfImport imported;
    if (const auto status = fo4::hkx::import_gltf_animation(*gltfBytes, *skeletonData, options, imported);
        status != fo4::hkx::HkxGltfStatus::Ok) {
        return std::unexpected(Error{"HkxGltfImport", std::string(fo4::hkx::hkx_gltf_status_name(status)) + ": " +
                                                          imported.message});
    }

    fo4::hkx::HkxGltfSaveOptions saveOptions;
    saveOptions.replace_annotations = request.replaceAnnotations;
    saveOptions.replace_motion = request.replaceMotion;
    fo4::hkx::HkxGltfSave saved;
    if (const auto status = fo4::hkx::save_gltf_animation(*templateBytes, *skeletonData, imported, saveOptions, saved);
        status != HkxGltfSaveStatus::Ok) {
        return std::unexpected(Error{"HkxGltfSave", std::string(fo4::hkx::hkx_gltf_save_status_name(status)) + ": " +
                                                        saved.report.message + saveHint(status) +
                                                        " Nothing was written."});
    }

    if (auto published = publishHkxBytes(request.outputPath, saved.bytes, request.overwrite, "HkxGltfWrite"); !published) {
        return std::unexpected(published.error());
    }
    return nlohmann::json{{"saved", true},
                          {"output", request.outputPath.string()},
                          {"overwrote", exists},
                          {"bytes", saved.bytes.size()},
                          {"report", gltfSaveReportJson(saved.report)}};
}

}
