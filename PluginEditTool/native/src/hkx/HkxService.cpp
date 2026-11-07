#include <fo4ide/hkx/HkxService.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <fo4/hkx/animation.hpp>
#include <fo4/hkx/packfile.hpp>
#include <fo4/hkx/pose.hpp>
#include <fo4/hkx/skeleton.hpp>

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

Result<void> loadClip(const std::filesystem::path& path, HkxAnimation& animation, HkxAnimationBinding& binding) {
    HkxPackfile packfile;
    if (auto opened = openPackfile(path, packfile); !opened) return opened;
    if (auto status = checkStatus(fo4::hkx::read_animation(packfile, animation), "animation read"); !status) {
        return status;
    }
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

nlohmann::json skeletonJson(const HkxSkeleton& skeletonData) {
    auto bones = nlohmann::json::array();
    for (std::size_t i = 0; i < skeletonData.bone_names.size(); ++i) {
        bones.push_back(boneJson(skeletonData.bone_names[i], skeletonData.parent_indices[i], skeletonData.reference_pose[i]));
    }
    return {{"name", skeletonData.name}, {"bones", bones}};
}

nlohmann::json poseJson(const HkxSkeleton& skeletonData, const HkxAnimation& animation,
                        const std::vector<HkxQsTransform>& pose) {
    auto bones = nlohmann::json::array();
    for (std::size_t i = 0; i < pose.size(); ++i) {
        bones.push_back(boneJson(skeletonData.bone_names[i], skeletonData.parent_indices[i], pose[i]));
    }
    return {{"duration", animation.duration}, {"frame_count", animation.frame_count}, {"bones", bones}};
}

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
    auto skeletonData = loadSkeleton(skeletonPath);
    if (!skeletonData) return std::unexpected(skeletonData.error());
    HkxAnimation animation;
    HkxAnimationBinding binding;
    if (auto loaded = loadClip(animationPath, animation, binding); !loaded) return std::unexpected(loaded.error());

    std::vector<HkxQsTransform> local;
    if (auto sampled = checkStatus(fo4::hkx::sample_pose(*skeletonData, animation, binding, time, local), "pose sampling");
        !sampled) {
        return std::unexpected(sampled.error());
    }
    if (space == PoseSpace::Local) return poseJson(*skeletonData, animation, local);

    std::vector<HkxQsTransform> model;
    if (auto converted = checkStatus(fo4::hkx::model_space_pose(*skeletonData, local, model), "model-space pose"); !converted) {
        return std::unexpected(converted.error());
    }
    return poseJson(*skeletonData, animation, model);
}

}
