#include <fo4ide/assets/NifSkinBinding.hpp>

#include <fo4/error.hpp>
#include <fo4/hkx/packfile.hpp>
#include <fo4/hkx/skeleton.hpp>
#include <fo4/nif/nif.hpp>
#include <fo4/nif/skin_binding.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace fo4ide::assets {
namespace {

constexpr std::array<std::string_view, 10> kStatusNames{
    "Ok", "Truncated", "BadMagic", "UnsupportedLayout", "MissingSection",
    "BadSection", "FixupOutOfRange", "DuplicateFixup", "BadClassName", "MissingObject",
};

Result<std::vector<std::byte>> readBytes(const std::filesystem::path& path, std::string_view label) {
    if (path.empty()) return std::unexpected(Error{"InvalidPath", std::string(label) + " path is empty"});
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        return std::unexpected(Error{"FileNotFound", std::string(label) + " not found: " + path.string()});
    }
    std::ifstream stream(path, std::ios::binary);
    const std::vector<char> raw((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    const auto view = std::as_bytes(std::span(raw));
    return std::vector<std::byte>(view.begin(), view.end());
}

Result<void> checkStatus(fo4::hkx::HkxStatus status, std::string_view step, std::span<const std::byte> bytes) {
    if (status == fo4::hkx::HkxStatus::Ok) return {};
    auto message = std::string(step) + " failed: " + std::string(kStatusNames[static_cast<std::size_t>(status)]);
    if (const auto reason = fo4::hkx::describe_packfile_refusal(bytes); !reason.empty()) message += " (" + reason + ")";
    return std::unexpected(Error{"HkxRead", std::move(message)});
}

Result<std::vector<std::string>> skeletonBoneNames(const std::filesystem::path& path) {
    auto bytes = readBytes(path, "Skeleton HKX");
    if (!bytes) return std::unexpected(bytes.error());
    fo4::hkx::HkxPackfile packfile;
    if (auto opened = checkStatus(fo4::hkx::read_packfile(*bytes, packfile), "packfile read", *bytes); !opened) {
        return std::unexpected(opened.error());
    }
    fo4::hkx::HkxSkeleton skeleton;
    if (auto decoded = checkStatus(fo4::hkx::read_skeleton(packfile, skeleton), "skeleton read", *bytes); !decoded) {
        return std::unexpected(decoded.error());
    }
    if (skeleton.bone_names.empty()) return std::unexpected(Error{"HkxRead", "The skeleton lists no bones"});
    return skeleton.bone_names;
}

nlohmann::json bindingJson(const fo4::nif::SkinBindingReport& report, const std::vector<std::string>& skeleton) {
    auto bones = nlohmann::json::array();
    for (const auto& bone : report.bones) {
        bones.push_back({{"name", bone.name},
                         {"match", fo4::nif::skin_bone_match_name(bone.match)},
                         {"skeleton_bone", bone.skeleton_bone},
                         {"skeleton_bone_name",
                          bone.skeleton_bone >= 0 ? skeleton.at(static_cast<std::size_t>(bone.skeleton_bone)) : std::string{}},
                         {"via", bone.via},
                         {"source", bone.source},
                         {"ambiguous", bone.ambiguous},
                         {"weighted_vertices", bone.weighted_vertices}});
    }
    return {{"block", report.block},
            {"name", report.name},
            {"vertices", report.vertices},
            {"fully_bound_vertices", report.fully_bound_vertices},
            {"exact", report.exact},
            {"node_chain", report.node_chain},
            {"skin_parent", report.skin_parent},
            {"unmatched", report.unmatched},
            {"ambiguous", report.ambiguous},
            {"complete", report.complete()},
            {"warning", report.warning},
            {"bones", std::move(bones)}};
}

}

Result<std::string> readSkinBinding(const std::filesystem::path& nifPath,
                                    std::optional<std::uint32_t> block,
                                    const std::filesystem::path& skeletonPath,
                                    const std::filesystem::path& skeletonNifPath) {
    auto skeleton = skeletonBoneNames(skeletonPath);
    if (!skeleton) return std::unexpected(skeleton.error());
    auto nifBytes = readBytes(nifPath, "NIF");
    if (!nifBytes) return std::unexpected(nifBytes.error());
    std::optional<std::vector<std::byte>> treeBytes;
    if (!skeletonNifPath.empty()) {
        auto loaded = readBytes(skeletonNifPath, "Skeleton NIF");
        if (!loaded) return std::unexpected(loaded.error());
        treeBytes = std::move(*loaded);
    }

    try {
        const auto nif = fo4::nif::read_nif(*nifBytes);
        std::optional<fo4::nif::NifFile> tree;
        std::vector<fo4::nif::SkinNodeSource> sources;
        if (treeBytes) {
            tree.emplace(fo4::nif::read_nif(*treeBytes));
            sources.push_back({&*tree, skeletonNifPath.filename().string()});
        }
        sources.push_back({&nif, "body NIF"});

        const auto bound = fo4::nif::bind_skin_shapes(nif, block, *skeleton, sources);
        switch (bound.status) {
            case fo4::nif::SkinShapesStatus::ok: break;
            case fo4::nif::SkinShapesStatus::invalid_block:
            case fo4::nif::SkinShapesStatus::not_a_shape: return std::unexpected(Error{"InvalidBlock", bound.message});
            case fo4::nif::SkinShapesStatus::refused: return std::unexpected(Error{"SkinBindingRefused", bound.message});
            case fo4::nif::SkinShapesStatus::parse_error: return std::unexpected(Error{"NifReadFailed", bound.message});
            case fo4::nif::SkinShapesStatus::no_skinned_shape: return std::unexpected(Error{"NoSkinnedShape", bound.message});
        }
        auto reports = nlohmann::json::array();
        for (const auto& report : bound.shapes) reports.push_back(bindingJson(report, *skeleton));
        auto refused = nlohmann::json::array();
        for (const auto& item : bound.refused) refused.push_back({{"block", item.block}, {"name", item.name}, {"reason", item.reason}});
        return nlohmann::json{{"skeleton", {{"path", skeletonPath.string()}, {"bones", skeleton->size()}}},
                              {"shapes", std::move(reports)},
                              {"refused", std::move(refused)},
                              {"unskinned", bound.unskinned}}
            .dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    } catch (const fo4::parse_error& error) {
        return std::unexpected(Error{"NifReadFailed", error.parse_message()});
    } catch (const std::exception& ex) {
        return std::unexpected(Error{"NifReadFailed", ex.what()});
    }
}

}
