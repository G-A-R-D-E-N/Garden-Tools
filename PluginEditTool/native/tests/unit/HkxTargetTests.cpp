#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <fo4/archive/ba2.hpp>
#include <fo4ide/app/ServiceTargets.hpp>
#include <fo4ide/hkx/HkxService.hpp>

#include "fo4/hkx/layout.hpp"
#include "fo4/hkx/packfile.hpp"
#include "fo4/hkx/skeleton.hpp"
#include "hkx_animation_fixture.hpp"
#include "hkx_behavior_graph_fixture.hpp"
#include "hkx_edit_fixture.hpp"
#include "hkx_motion_fixture.hpp"
#include "hkx_physics_fixture.hpp"
#include "hkx_test_fixture.hpp"
#include "hkx_turret_glb.hpp"
#include "plugin_bytes.hpp"

using fo4ide::app::HkxTarget;
using fo4ide::hkx::HkxService;
using fo4ide::hkx::PoseSpace;

namespace {

using namespace fo4::test::hkx;

constexpr std::size_t kTracks = 2;
constexpr std::size_t kFrames = 2;

std::filesystem::path tempRoot() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / ("fo4ide_hkx_pose_" + std::to_string(id));
}

struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

void writeBytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

std::size_t appendString(std::vector<std::byte>& data, const std::string& text) {
    const std::size_t at = data.size();
    for (char ch : text) data.push_back(static_cast<std::byte>(ch));
    data.push_back(std::byte{0});
    return at;
}

void putTransform(std::vector<std::byte>& data, const std::array<float, 3>& translation,
                  const std::array<float, 4>& rotation = {0.0f, 0.0f, 0.0f, 1.0f},
                  const std::array<float, 3>& scale = {1.0f, 1.0f, 1.0f}) {
    for (float value : translation) put_float(data, value);
    put_float(data, 0.0f);
    for (float value : rotation) put_float(data, value);
    for (float value : scale) put_float(data, value);
    put_float(data, 0.0f);
}

std::vector<std::byte> skeletonPackfile(const std::string& skeletonName = "Rig",
                                        const std::vector<std::string>& names = {"Root", "Spine", "Head"},
                                        bool ragdollSkeleton = false, float rootTranslationX = 0.0f) {
    const std::vector<std::int16_t> parents{-1, 0, 1};
    const std::vector<std::array<float, 3>> pose{{rootTranslationX, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}};

    const std::size_t boneSize = fo4::hkx::find_layout("hkaBone")->size;
    const std::size_t boneName = member_at("hkaBone", "name");
    std::vector<std::byte> data(fo4::hkx::find_layout("hkaSkeleton")->size, std::byte{0});
    std::vector<std::uint32_t> local;

    const std::size_t bonesAt = data.size();
    data.resize(bonesAt + names.size() * boneSize, std::byte{0});
    for (std::size_t i = 0; i < names.size(); ++i) {
        link(local, bonesAt + i * boneSize + boneName, appendString(data, names[i]));
    }

    const std::size_t parentsAt = data.size();
    for (std::int16_t parent : parents) {
        const auto bits = static_cast<std::uint16_t>(parent);
        data.push_back(static_cast<std::byte>(bits & 0xFF));
        data.push_back(static_cast<std::byte>(bits >> 8));
    }
    const std::size_t poseAt = data.size();
    for (const auto& translation : pose) putTransform(data, translation);

    link(local, member_at("hkaSkeleton", "name"), appendString(data, skeletonName));
    link(local, member_at("hkaSkeleton", "parentIndices"), parentsAt);
    set_count(data, member_at("hkaSkeleton", "parentIndices"), parents.size());
    link(local, member_at("hkaSkeleton", "bones"), bonesAt);
    set_count(data, member_at("hkaSkeleton", "bones"), names.size());
    link(local, member_at("hkaSkeleton", "referencePose"), poseAt);
    set_count(data, member_at("hkaSkeleton", "referencePose"), pose.size());

    // Constraint: only bone 2 is locked, so the reply carries the flag per bone.
    data[bonesAt + 2 * boneSize + member_at("hkaBone", "lockTranslation")] = std::byte{1};

    const std::size_t floatsAt = data.size();
    put_float(data, 2.0f);
    link(local, member_at("hkaSkeleton", "referenceFloats"), floatsAt);
    set_count(data, member_at("hkaSkeleton", "referenceFloats"), 1);

    const std::size_t slotsAt = data.size();
    data.resize(slotsAt + 2 * 8, std::byte{0});
    link(local, slotsAt, appendString(data, "speed"));
    // Constraint: slot 1 is a null pointer, so an unnamed slot keeps its index.
    link(local, member_at("hkaSkeleton", "floatSlots"), slotsAt);
    set_count(data, member_at("hkaSkeleton", "floatSlots"), 2);

    const std::size_t frameSize = fo4::hkx::find_layout("hkaSkeletonLocalFrameOnBone")->size;
    const std::size_t framesAt = data.size();
    data.resize(framesAt + frameSize, std::byte{0});
    data[framesAt + member_at("hkaSkeletonLocalFrameOnBone", "boneIndex")] = std::byte{2};
    link(local, framesAt + member_at("hkaSkeletonLocalFrameOnBone", "localFrame"), appendString(data, "frame"));
    link(local, member_at("hkaSkeleton", "localFrames"), framesAt);
    set_count(data, member_at("hkaSkeleton", "localFrames"), 1);

    const std::size_t partitionSize = fo4::hkx::find_layout("hkaSkeletonPartition")->size;
    const std::size_t partitionsAt = data.size();
    data.resize(partitionsAt + partitionSize, std::byte{0});
    link(local, partitionsAt + member_at("hkaSkeletonPartition", "name"), appendString(data, "upper"));
    data[partitionsAt + member_at("hkaSkeletonPartition", "startBoneIndex")] = std::byte{1};
    data[partitionsAt + member_at("hkaSkeletonPartition", "numBones")] = std::byte{2};
    link(local, member_at("hkaSkeleton", "partitions"), partitionsAt);
    set_count(data, member_at("hkaSkeleton", "partitions"), 1);

    std::vector<std::byte> classNames;
    const auto skeletonClass = add_class_name(classNames, signature_of("hkaSkeleton"), "hkaSkeleton");
    std::vector<std::uint32_t> objects{0, 0, skeletonClass};
    if (ragdollSkeleton) {
        const std::size_t ragdollAt = data.size();
        data.resize(ragdollAt + fo4::hkx::find_layout("hkaSkeleton")->size, std::byte{0});
        objects.insert(objects.end(), {static_cast<std::uint32_t>(ragdollAt), 0, skeletonClass});
    }
    return build_packfile({
        TestSection{"__classnames__", classNames, {}, {}, {}},
        TestSection{"__types__", {}, {}, {}, {}},
        TestSection{"__data__", std::move(data), std::move(local), {}, objects},
    });
}

std::vector<std::byte> clipPackfile(float shift = 0.0f, const std::string& originalSkeleton = "Rig",
                                    bool extraBinding = false, float duration = 1.0f,
                                    const std::array<float, 4>& rotation = {0.0f, 0.0f, 0.0f, 1.0f},
                                    const std::array<float, 3>& scale = {1.0f, 1.0f, 1.0f},
                                    const std::vector<std::pair<float, std::string>>& annotations = {},
                                    const std::vector<std::array<float, 3>>& transforms = {{1.0f, 0.0f, 0.0f},
                                                                                          {1.0f, 0.0f, 0.0f},
                                                                                          {2.0f, 0.0f, 0.0f},
                                                                                          {3.0f, 0.0f, 0.0f}}) {
    std::vector<std::byte> names;
    const auto clipName = add_class_name(names, signature_of("hkaInterleavedUncompressedAnimation"),
                                         "hkaInterleavedUncompressedAnimation");
    const auto bindingName = add_class_name(names, signature_of("hkaAnimationBinding"), "hkaAnimationBinding");

    std::vector<std::byte> data(fo4::hkx::find_layout("hkaInterleavedUncompressedAnimation")->size, std::byte{0});
    const std::size_t bindingAt = data.size();
    data.resize(bindingAt + fo4::hkx::find_layout("hkaAnimationBinding")->size, std::byte{0});
    std::vector<std::uint32_t> local;

    set_float_member(data, "hkaAnimation", "duration", duration);
    set_int_member(data, "hkaAnimation", "numberOfTransformTracks", static_cast<std::uint32_t>(kTracks));
    const std::size_t transformsAt = data.size();
    for (auto translation : transforms) {
        translation[0] += shift;
        putTransform(data, translation, rotation, scale);
    }
    link(local, member_at("hkaInterleavedUncompressedAnimation", "transforms"), transformsAt);
    set_count(data, member_at("hkaInterleavedUncompressedAnimation", "transforms"), transforms.size());

    const std::size_t bonesAt = data.size();
    for (std::int16_t bone : {std::int16_t{1}, std::int16_t{2}}) {
        const auto bits = static_cast<std::uint16_t>(bone);
        data.push_back(static_cast<std::byte>(bits & 0xFF));
        data.push_back(static_cast<std::byte>(bits >> 8));
    }
    link(local, bindingAt + member_at("hkaAnimationBinding", "transformTrackToBoneIndices"), bonesAt);
    set_count(data, bindingAt + member_at("hkaAnimationBinding", "transformTrackToBoneIndices"), kTracks);
    link(local, bindingAt + member_at("hkaAnimationBinding", "originalSkeletonName"), appendString(data, originalSkeleton));

    if (!annotations.empty()) {
        const std::size_t trackSize = fo4::hkx::find_layout("hkaAnnotationTrack")->size;
        const std::size_t eventSize = fo4::hkx::find_layout("hkaAnnotationTrackAnnotation")->size;
        const std::size_t trackAt = data.size();
        data.resize(trackAt + trackSize, std::byte{0});
        const std::size_t eventsAt = data.size();
        data.resize(eventsAt + annotations.size() * eventSize, std::byte{0});
        link(local, trackAt + member_at("hkaAnnotationTrack", "trackName"), appendString(data, "Events"));
        link(local, trackAt + member_at("hkaAnnotationTrack", "annotations"), eventsAt);
        set_count(data, trackAt + member_at("hkaAnnotationTrack", "annotations"), annotations.size());
        for (std::size_t i = 0; i < annotations.size(); ++i) {
            const std::size_t eventAt = eventsAt + i * eventSize;
            patch_float(data, eventAt + member_at("hkaAnnotationTrackAnnotation", "time"), annotations[i].first);
            link(local, eventAt + member_at("hkaAnnotationTrackAnnotation", "text"),
                 appendString(data, annotations[i].second));
        }
        const std::size_t field = member_at("hkaAnimation", "annotationTracks");
        link(local, field, trackAt);
        set_count(data, field, 1);
    }

    std::vector<std::uint32_t> objects{0, 0, clipName, static_cast<std::uint32_t>(bindingAt), 0, bindingName};
    if (extraBinding) {
        const std::size_t secondBindingAt = data.size();
        data.resize(secondBindingAt + fo4::hkx::find_layout("hkaAnimationBinding")->size, std::byte{0});
        objects.insert(objects.end(), {static_cast<std::uint32_t>(secondBindingAt), 0, bindingName});
    }

    return build_packfile({
        TestSection{"__classnames__", names, {}, {}, {}},
        TestSection{"__types__", {}, {}, {}, {}},
        TestSection{"__data__", std::move(data), std::move(local), {}, objects},
    });
}

// Requires clipPackfile's two-track, two-frame shape so a half-duration pose interpolates the same numbers.
std::vector<std::byte> losslessClipPackfile() {
    std::vector<std::byte> names;
    const auto clipName = add_class_name(names, signature_of("hkaLosslessCompressedAnimation"),
                                         "hkaLosslessCompressedAnimation");
    const auto bindingName = add_class_name(names, signature_of("hkaAnimationBinding"), "hkaAnimationBinding");

    std::vector<std::byte> data(fo4::hkx::find_layout("hkaLosslessCompressedAnimation")->size, std::byte{0});
    const std::size_t bindingAt = data.size();
    data.resize(bindingAt + fo4::hkx::find_layout("hkaAnimationBinding")->size, std::byte{0});
    std::vector<std::uint32_t> local;

    set_float_member(data, "hkaAnimation", "duration", 1.0f);
    set_int_member(data, "hkaAnimation", "numberOfTransformTracks", static_cast<std::uint32_t>(kTracks));
    set_int_member(data, "hkaAnimation", "numberOfFloatTracks", 0);
    set_int_member(data, "hkaLosslessCompressedAnimation", "numFrames", static_cast<std::uint32_t>(kFrames));

    const std::size_t translationsAt = data.size();
    for (const float value : {1.0f, 1.0f, 2.0f, 3.0f}) put_float(data, value);
    // Constraint: track 0 names a static translation, and BGS refuses a static offset with no static array.
    const std::size_t staticTranslationsAt = data.size();
    put_float(data, 1.0f);
    const std::size_t translationWordsAt = data.size();
    for (const std::uint64_t word : {std::uint64_t{1} << 16 | 2u, std::uint64_t{6}}) {
        for (int shift = 0; shift < 64; shift += 8) data.push_back(static_cast<std::byte>((word >> shift) & 0xFF));
    }

    const std::size_t rotationsAt = data.size();
    for (const float value : {0.0f, 0.0f, 0.0f, 1.0f}) put_float(data, value);
    const std::size_t rotationWordsAt = data.size();
    for (const std::uint16_t word : {std::uint16_t{1}, std::uint16_t{1}}) {
        data.push_back(static_cast<std::byte>(word & 0xFF));
        data.push_back(static_cast<std::byte>(word >> 8));
    }

    const std::size_t scalesAt = data.size();
    put_float(data, 1.0f);
    const std::size_t scaleWordsAt = data.size();
    const std::uint64_t staticScaleWord = std::uint64_t{1} | (std::uint64_t{1} << 16) | (std::uint64_t{1} << 32);
    for (const std::uint64_t word : {staticScaleWord, staticScaleWord}) {
        for (int shift = 0; shift < 64; shift += 8) data.push_back(static_cast<std::byte>((word >> shift) & 0xFF));
    }

    link(local, member_at("hkaLosslessCompressedAnimation", "dynamicTranslations"), translationsAt);
    set_count(data, member_at("hkaLosslessCompressedAnimation", "dynamicTranslations"), kTracks * kFrames);
    link(local, member_at("hkaLosslessCompressedAnimation", "staticTranslations"), staticTranslationsAt);
    set_count(data, member_at("hkaLosslessCompressedAnimation", "staticTranslations"), 1);
    link(local, member_at("hkaLosslessCompressedAnimation", "translationTypeAndOffsets"), translationWordsAt);
    set_count(data, member_at("hkaLosslessCompressedAnimation", "translationTypeAndOffsets"), kTracks);
    link(local, member_at("hkaLosslessCompressedAnimation", "staticRotations"), rotationsAt);
    set_count(data, member_at("hkaLosslessCompressedAnimation", "staticRotations"), 1);
    link(local, member_at("hkaLosslessCompressedAnimation", "rotationTypeAndOffsets"), rotationWordsAt);
    set_count(data, member_at("hkaLosslessCompressedAnimation", "rotationTypeAndOffsets"), kTracks);
    link(local, member_at("hkaLosslessCompressedAnimation", "staticScales"), scalesAt);
    set_count(data, member_at("hkaLosslessCompressedAnimation", "staticScales"), 1);
    link(local, member_at("hkaLosslessCompressedAnimation", "scaleTypeAndOffsets"), scaleWordsAt);
    set_count(data, member_at("hkaLosslessCompressedAnimation", "scaleTypeAndOffsets"), kTracks);

    const std::size_t bonesAt = data.size();
    for (std::int16_t bone : {std::int16_t{1}, std::int16_t{2}}) {
        const auto bits = static_cast<std::uint16_t>(bone);
        data.push_back(static_cast<std::byte>(bits & 0xFF));
        data.push_back(static_cast<std::byte>(bits >> 8));
    }
    link(local, bindingAt + member_at("hkaAnimationBinding", "transformTrackToBoneIndices"), bonesAt);
    set_count(data, bindingAt + member_at("hkaAnimationBinding", "transformTrackToBoneIndices"), kTracks);
    link(local, bindingAt + member_at("hkaAnimationBinding", "originalSkeletonName"), appendString(data, "Rig"));

    std::vector<std::uint32_t> objects{0, 0, clipName, static_cast<std::uint32_t>(bindingAt), 0, bindingName};
    return build_packfile({
        TestSection{"__classnames__", names, {}, {}, {}},
        TestSection{"__types__", {}, {}, {}, {}},
        TestSection{"__data__", std::move(data), std::move(local), {}, objects},
    });
}

}

TEST_CASE("HkxTarget rejects an unknown method") {
    HkxService service;
    HkxTarget target(service);
    const auto result = target.invoke("Nope", nlohmann::json::array());
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "UnknownMethod");
}

TEST_CASE("HkxTarget refuses a pose time that is negative or not a number") {
    HkxService service;
    HkxTarget target(service);

    const auto negative = target.invoke("Pose", nlohmann::json::array({"skeleton.hkx", "clip.hkx", -0.5, "local"}));
    REQUIRE_FALSE(negative.has_value());
    CHECK(negative.error().type == "InvalidTime");

    const auto missing = target.invoke("Pose", nlohmann::json::array({"skeleton.hkx", "clip.hkx", nullptr, nullptr}));
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().type == "InvalidTime");

    const auto text = target.invoke("Pose", nlohmann::json::array({"skeleton.hkx", "clip.hkx", "1.0", "local"}));
    REQUIRE_FALSE(text.has_value());
    CHECK(text.error().type == "InvalidTime");
}

TEST_CASE("HkxTarget PoseBoth refuses a negative time and an absent clip") {
    HkxService service;
    HkxTarget target(service);
    const auto negative = target.invoke("PoseBoth", nlohmann::json::array({"skeleton.hkx", "clip.hkx", -0.5}));
    REQUIRE_FALSE(negative.has_value());
    CHECK(negative.error().type == "InvalidTime");

    const auto absent = target.invoke("PoseBoth", nlohmann::json::array({"missing_skeleton.hkx", "missing_clip.hkx", 0.5}));
    REQUIRE_FALSE(absent.has_value());
    CHECK(absent.error().type == "FileNotFound");
}

TEST_CASE("HkxTarget LoadClip refuses an absent clip") {
    HkxService service;
    HkxTarget target(service);
    const auto absent = target.invoke("LoadClip", nlohmann::json::array({"missing_skeleton.hkx", "missing_clip.hkx"}));
    REQUIRE_FALSE(absent.has_value());
    CHECK(absent.error().type == "FileNotFound");
}

TEST_CASE("HkxTarget refuses a pose space other than local or model") {
    HkxService service;
    HkxTarget target(service);
    const auto result = target.invoke("Pose", nlohmann::json::array({"skeleton.hkx", "clip.hkx", 0.5, "sideways"}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "InvalidSpace");
}

TEST_CASE("HkxTarget accepts local, model, and an absent space, which defaults to local") {
    HkxService service;
    HkxTarget target(service);
    for (const auto& space : {nlohmann::json("local"), nlohmann::json("model"), nlohmann::json(nullptr)}) {
        const auto result = target.invoke("Pose", nlohmann::json::array({"missing_skeleton.hkx", "missing_clip.hkx", 0.5, space}));
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().type == "FileNotFound");
    }
}

TEST_CASE("HkxTarget Skeleton reports the bone lock, float slots, local frames and partitions") {
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    const auto skeletonPath = cleanup.root / "rig.hkx";
    writeBytes(skeletonPath, skeletonPackfile());

    HkxService service;
    HkxTarget target(service);
    const auto reply = target.invoke("Skeleton", nlohmann::json::array({skeletonPath.string()}));
    REQUIRE(reply.has_value());
    // Regression: the reply must carry each partition, slot and lock, or the panel cannot name them.
    CHECK((*reply)["name"].get<std::string>() == "Rig");
    REQUIRE((*reply)["bones"].size() == 3u);
    CHECK((*reply)["bones"][0]["lock"].get<bool>() == false);
    CHECK((*reply)["bones"][2]["lock"].get<bool>() == true);
    REQUIRE((*reply)["reference_floats"].size() == 1u);
    CHECK((*reply)["reference_floats"][0].get<double>() == 2.0);
    REQUIRE((*reply)["float_slots"].size() == 2u);
    CHECK((*reply)["float_slots"][0]["name"].get<std::string>() == "speed");
    CHECK((*reply)["float_slots"][0]["named"].get<bool>() == true);
    CHECK((*reply)["float_slots"][1]["named"].get<bool>() == false);
    REQUIRE((*reply)["local_frames"].size() == 1u);
    CHECK((*reply)["local_frames"][0]["bone"].get<int>() == 2);
    REQUIRE((*reply)["partitions"].size() == 1u);
    CHECK((*reply)["partitions"][0]["name"].get<std::string>() == "upper");
    CHECK((*reply)["partitions"][0]["start_bone"].get<int>() == 1);
    CHECK((*reply)["partitions"][0]["num_bones"].get<int>() == 2);
}

TEST_CASE("HkxTarget refuses an empty or missing skeleton path") {
    HkxService service;
    HkxTarget target(service);

    const auto empty = target.invoke("Skeleton", nlohmann::json::array({""}));
    REQUIRE_FALSE(empty.has_value());
    CHECK(empty.error().type == "InvalidPath");

    const auto missing = target.invoke("Pose", nlohmann::json::array({"", "clip.hkx", 0.5, "local"}));
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().type == "InvalidPath");

    const auto absent = target.invoke("Skeleton", nlohmann::json::array({"missing_skeleton.hkx"}));
    REQUIRE_FALSE(absent.has_value());
    CHECK(absent.error().type == "FileNotFound");
}

TEST_CASE("HkxTarget summary reports a clip's kind, counts and binding skeleton") {
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    const auto clipPath = cleanup.root / "clip.hkx";
    writeBytes(clipPath, clipPackfile());

    HkxService service;
    HkxTarget target(service);
    const auto reply = target.invoke("Summary", nlohmann::json::array({clipPath.string()}));
    REQUIRE(reply.has_value());
    // Regression: the report dropped the binding skeleton or counted the packfile's objects as clips.
    CHECK((*reply)["object_count"].get<std::size_t>() == 2u);
    CHECK((*reply)["skeleton"].is_null());
    REQUIRE((*reply)["clips"].size() == 1u);
    CHECK((*reply)["clips"][0]["class_name"].get<std::string>() == "hkaInterleavedUncompressedAnimation");
    CHECK((*reply)["clips"][0]["kind"].get<std::string>() == "interleaved");
    CHECK((*reply)["clips"][0]["frame_count"].get<std::size_t>() == kFrames);
    CHECK((*reply)["clips"][0]["track_count"].get<std::size_t>() == kTracks);
    REQUIRE((*reply)["bindings"].size() == 1u);
    CHECK((*reply)["bindings"][0].get<std::string>() == "Rig");

    const auto skeletonPath = cleanup.root / "rig.hkx";
    writeBytes(skeletonPath, skeletonPackfile());
    const auto rig = target.invoke("Summary", nlohmann::json::array({skeletonPath.string()}));
    REQUIRE(rig.has_value());
    // Regression: the report dropped the skeleton section, so a rig file read as an empty packfile.
    REQUIRE((*rig)["skeleton"].is_object());
    CHECK((*rig)["skeleton"]["name"].get<std::string>() == "Rig");
    CHECK((*rig)["skeleton"]["bone_count"].get<std::size_t>() == 3u);
    CHECK((*rig)["clips"].empty());
}

TEST_CASE("HkxTarget InspectAnimation reports track flags and annotations, and refuses a bad track index") {
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    const auto clipPath = cleanup.root / "clip.hkx";
    writeBytes(clipPath, clipPackfile(0.0f, "Rig", false, 1.0f, {0.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f},
                                      {{0.5f, "Footstep"}, {0.75f, "Land"}}));

    HkxService service;
    HkxTarget target(service);
    const auto reply = target.invoke("InspectAnimation", nlohmann::json::array({clipPath.string(), 1}));
    REQUIRE(reply.has_value());
    // Regression: the annotation tracks were counted but their entries dropped, and the track index was not checked.
    CHECK((*reply)["kind"].get<std::string>() == "interleaved");
    CHECK((*reply)["frame_count"].get<std::size_t>() == kFrames);
    CHECK((*reply)["track_count"].get<std::size_t>() == kTracks);
    REQUIRE((*reply)["tracks"].size() == kTracks);
    CHECK((*reply)["tracks"][1]["samples"].get<std::size_t>() == kFrames);
    CHECK((*reply)["tracks"][1]["rotation_animated"].get<bool>());
    REQUIRE((*reply)["annotations"].size() == 1u);
    CHECK((*reply)["annotations"][0]["name"].get<std::string>() == "Events");
    REQUIRE((*reply)["annotations"][0]["entries"].size() == 2u);
    CHECK((*reply)["annotations"][0]["entries"][1]["text"].get<std::string>() == "Land");
    CHECK((*reply)["annotation_count"].get<std::size_t>() == 2u);
    REQUIRE((*reply)["track"].is_object());
    // The fixture stores its samples frame-major, so track 1's two frames are x = 1 and x = 3.
    CHECK((*reply)["track"]["translations"][0].get<double>() == 1.0);
    CHECK((*reply)["track"]["translations"][3].get<double>() == 3.0);
    CHECK((*reply)["track"]["translations"].size() == kFrames * 3u);
    CHECK((*reply)["track"]["rotations"].size() == kFrames * 4u);
    CHECK((*reply)["track"]["frames"].get<std::size_t>() == kFrames);
    CHECK((*reply)["track"]["shown"].get<std::size_t>() == kFrames);
    CHECK_FALSE((*reply)["track"]["truncated"].get<bool>());

    // Regression: a long clip used to return every frame, filling one reply with millions of numbers.
    const auto capped = target.invoke("InspectAnimation", nlohmann::json::array({clipPath.string(), 1, 1}));
    REQUIRE(capped.has_value());
    CHECK((*capped)["track"]["frames"].get<std::size_t>() == kFrames);
    CHECK((*capped)["track"]["shown"].get<std::size_t>() == 1u);
    CHECK((*capped)["track"]["truncated"].get<bool>());
    CHECK((*capped)["track"]["translations"].size() == 3u);
    CHECK((*capped)["track"]["rotations"].size() == 4u);
    CHECK((*capped)["track"]["scales"].size() == 3u);
    CHECK((*capped)["track"]["translations"][0].get<double>() == 1.0);

    const auto beyond = target.invoke("InspectAnimation", nlohmann::json::array({clipPath.string(), kTracks}));
    REQUIRE_FALSE(beyond.has_value());
    CHECK(beyond.error().type == "InvalidTrack");
    const auto negative = target.invoke("InspectAnimation", nlohmann::json::array({clipPath.string(), -1}));
    REQUIRE_FALSE(negative.has_value());
    CHECK(negative.error().type == "InvalidTrack");
    const auto fractional = target.invoke("InspectAnimation", nlohmann::json::array({clipPath.string(), 1.5}));
    REQUIRE_FALSE(fractional.has_value());
    CHECK(fractional.error().type == "InvalidTrack");
}

TEST_CASE("HkxTarget InspectAnimation names each track from an explicit, a sibling or no skeleton and skips an unreadable one") {
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    const auto loose = cleanup.root / "loose.hkx";
    writeBytes(loose, clipPackfile());

    HkxService service;
    HkxTarget target(service);
    const auto bare = target.invoke("InspectAnimation", nlohmann::json::array({loose.string()}));
    REQUIRE(bare.has_value());
    CHECK((*bare)["track_name_source"].get<std::string>() == "none");
    CHECK((*bare)["tracks"][0]["name"].get<std::string>() == "track 0");
    CHECK((*bare)["tracks"][1]["name"].get<std::string>() == "track 1");

    const auto rig = cleanup.root / "rig.hkx";
    writeBytes(rig, skeletonPackfile());
    const auto named = target.invoke("InspectAnimation",
                                     nlohmann::json::array({loose.string(), nullptr, nullptr, rig.string()}));
    REQUIRE(named.has_value());
    CHECK((*named)["track_name_source"].get<std::string>() == "explicit");
    CHECK((*named)["tracks"][0]["name"].get<std::string>() == "Spine");
    CHECK((*named)["tracks"][1]["name"].get<std::string>() == "Head");

    const auto broken = cleanup.root / "broken.hkx";
    writeBytes(broken, std::vector<std::byte>(8, std::byte{1}));
    const auto fallback = target.invoke("InspectAnimation",
                                        nlohmann::json::array({loose.string(), nullptr, nullptr, broken.string()}));
    REQUIRE(fallback.has_value());
    CHECK((*fallback)["track_name_source"].get<std::string>() == "sibling");
    CHECK((*fallback)["tracks"][0]["name"].get<std::string>() == "Spine");

    const auto animation = cleanup.root / "Dog" / "Animations" / "clip.hkx";
    std::filesystem::create_directories(animation.parent_path());
    std::filesystem::create_directories(cleanup.root / "Dog" / "CharacterAssets");
    writeBytes(animation, clipPackfile());
    writeBytes(cleanup.root / "Dog" / "CharacterAssets" / "skeleton.hkx", skeletonPackfile());
    const auto sibling = target.invoke("InspectAnimation", nlohmann::json::array({animation.string()}));
    REQUIRE(sibling.has_value());
    CHECK((*sibling)["track_name_source"].get<std::string>() == "sibling");
    CHECK((*sibling)["tracks"][1]["name"].get<std::string>() == "Head");
}

TEST_CASE("HkxService reads a skeleton and samples a clip in local and model space") {
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    const auto skeletonPath = cleanup.root / "rig.hkx";
    const auto clipPath = cleanup.root / "clip.hkx";
    writeBytes(skeletonPath, skeletonPackfile());
    writeBytes(clipPath, clipPackfile());

    HkxService service;

    const auto skeleton = service.skeleton(skeletonPath);
    REQUIRE(skeleton.has_value());
    CHECK((*skeleton)["name"].get<std::string>() == "Rig");
    REQUIRE((*skeleton)["bones"].size() == 3u);
    CHECK((*skeleton)["bones"][0]["name"].get<std::string>() == "Root");
    CHECK((*skeleton)["bones"][0]["parent"].get<int>() == -1);
    CHECK((*skeleton)["bones"][2]["parent"].get<int>() == 1);

    const auto local = service.pose(skeletonPath, clipPath, 0.5f, PoseSpace::Local);
    REQUIRE(local.has_value());
    CHECK((*local)["duration"].get<double>() == 1.0);
    CHECK((*local)["frame_count"].get<std::size_t>() == 2u);
    REQUIRE((*local)["bones"].size() == 3u);
    CHECK((*local)["bones"][1]["translation"][0].get<double>() == 1.5);
    CHECK((*local)["bones"][2]["translation"][0].get<double>() == 2.0);

    const auto model = service.pose(skeletonPath, clipPath, 0.5f, PoseSpace::Model);
    REQUIRE(model.has_value());
    CHECK((*model)["bones"][1]["translation"][0].get<double>() == 1.5);
    CHECK((*model)["bones"][2]["translation"][0].get<double>() == 3.5);
}

TEST_CASE("HkxService poseBoth takes both spaces from one sample and re-reads a clip whose file changed") {
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    const auto skeletonPath = cleanup.root / "rig.hkx";
    const auto clipPath = cleanup.root / "clip.hkx";
    writeBytes(skeletonPath, skeletonPackfile());
    writeBytes(clipPath, clipPackfile());

    HkxService service;
    const auto first = service.poseBoth(skeletonPath, clipPath, 0.5f);
    REQUIRE(first.has_value());
    CHECK((*first)["frame_count"].get<std::size_t>() == 2u);
    CHECK((*first)["local"][1]["translation"][0].get<double>() == 1.5);
    CHECK((*first)["local"][2]["translation"][0].get<double>() == 2.0);
    CHECK((*first)["model"][1]["translation"][0].get<double>() == 1.5);
    CHECK((*first)["model"][2]["translation"][0].get<double>() == 3.5);

    writeBytes(clipPath, clipPackfile(10.0f));
    std::filesystem::last_write_time(clipPath, std::filesystem::last_write_time(clipPath) + std::chrono::seconds(2));
    const auto second = service.poseBoth(skeletonPath, clipPath, 0.5f);
    REQUIRE(second.has_value());
    CHECK((*second)["local"][1]["translation"][0].get<double>() == 11.5);
    CHECK((*second)["model"][1]["translation"][0].get<double>() == 11.5);
}

TEST_CASE("HkxService loadClip reports a match when the clip names the selected skeleton") {
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    const auto skeletonPath = cleanup.root / "rig.hkx";
    const auto clipPath = cleanup.root / "clip.hkx";
    writeBytes(skeletonPath, skeletonPackfile());
    writeBytes(clipPath, clipPackfile());

    HkxService service;
    const auto reply = service.loadClip(skeletonPath, clipPath);
    REQUIRE(reply.has_value());
    CHECK((*reply)["skeleton_name"].get<std::string>() == "Rig");
    CHECK((*reply)["original_skeleton_name"].get<std::string>() == "Rig");
    CHECK((*reply)["skeleton_match"].get<std::string>() == "match");
    CHECK((*reply)["frame_count"].get<std::size_t>() == 2u);
}

TEST_CASE("HkxService loadClip keeps a match when the skeleton file also holds a ragdoll skeleton object") {
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    const auto skeletonPath = cleanup.root / "rig.hkx";
    const auto clipPath = cleanup.root / "clip.hkx";
    writeBytes(skeletonPath, skeletonPackfile("Rig", {"Root", "Spine", "Head"}, true));
    writeBytes(clipPath, clipPackfile());

    HkxService service;
    const auto reply = service.loadClip(skeletonPath, clipPath);
    REQUIRE(reply.has_value());
    CHECK((*reply)["skeleton_name"].get<std::string>() == "Rig");
    CHECK((*reply)["skeleton_match"].get<std::string>() == "match");
}

TEST_CASE("HkxService loadClip reports a mismatch for a same-sized skeleton with another name and bone order") {
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    const auto skeletonPath = cleanup.root / "biped.hkx";
    const auto clipPath = cleanup.root / "clip.hkx";
    writeBytes(skeletonPath, skeletonPackfile("Biped", {"Root", "Head", "Spine"}));
    writeBytes(clipPath, clipPackfile());

    HkxService service;
    const auto reply = service.loadClip(skeletonPath, clipPath);
    REQUIRE(reply.has_value());
    CHECK((*reply)["skeleton_name"].get<std::string>() == "Biped");
    CHECK((*reply)["original_skeleton_name"].get<std::string>() == "Rig");
    CHECK((*reply)["skeleton_match"].get<std::string>() == "mismatch");
    CHECK(service.pose(skeletonPath, clipPath, 0.5f, PoseSpace::Local).has_value());
}

TEST_CASE("HkxService loadClip reports unverified when the clip names no skeleton and refuses a clip holding two bindings") {
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    const auto skeletonPath = cleanup.root / "rig.hkx";
    const auto unnamedPath = cleanup.root / "unnamed.hkx";
    const auto doubledPath = cleanup.root / "doubled.hkx";
    writeBytes(skeletonPath, skeletonPackfile());
    writeBytes(unnamedPath, clipPackfile(0.0f, ""));
    writeBytes(doubledPath, clipPackfile(0.0f, "Rig", true));

    HkxService service;
    const auto unnamed = service.loadClip(skeletonPath, unnamedPath);
    REQUIRE(unnamed.has_value());
    CHECK((*unnamed)["skeleton_match"].get<std::string>() == "unverified");

    const auto doubled = service.loadClip(skeletonPath, doubledPath);
    REQUIRE_FALSE(doubled.has_value());
    CHECK(doubled.error().type == "HkxRead");
}

TEST_CASE("HkxService names the unsupported container field when a packfile uses 4-byte pointers") {
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    const auto path = cleanup.root / "pointer4.hkx";
    auto bytes = skeletonPackfile();
    bytes[0x10] = std::byte{4};
    writeBytes(path, bytes);

    HkxService service;
    const auto summary = service.summary(path);
    REQUIRE_FALSE(summary.has_value());
    CHECK(summary.error().type == "HkxRead");
    CHECK(summary.error().message.find("pointer size 4") != std::string::npos);
}

TEST_CASE("HkxService refuses a clip whose duration is NaN or infinite at the Radicle read instead of reporting it as null") {
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    const auto skeletonPath = cleanup.root / "rig.hkx";
    const auto clipPath = cleanup.root / "clip.hkx";
    writeBytes(skeletonPath, skeletonPackfile());

    for (float duration : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
        writeBytes(clipPath, clipPackfile(0.0f, "Rig", false, duration));
        HkxService service;

        const auto clip = service.loadClip(skeletonPath, clipPath);
        REQUIRE_FALSE(clip.has_value());
        CHECK(clip.error().type == "HkxRead");

        const auto both = service.poseBoth(skeletonPath, clipPath, 0.5f);
        REQUIRE_FALSE(both.has_value());
        CHECK(both.error().type == "HkxRead");
    }
}

TEST_CASE("HkxService refuses a clip whose bone translation, rotation or scale is not finite at the Radicle read") {
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    const auto skeletonPath = cleanup.root / "rig.hkx";
    const auto clipPath = cleanup.root / "clip.hkx";
    writeBytes(skeletonPath, skeletonPackfile());
    const float nonFinite = std::numeric_limits<float>::quiet_NaN();

    SECTION("translation") { writeBytes(clipPath, clipPackfile(nonFinite)); }
    SECTION("rotation") { writeBytes(clipPath, clipPackfile(0.0f, "Rig", false, 1.0f, {nonFinite, 0.0f, 0.0f, 1.0f})); }
    SECTION("scale") { writeBytes(clipPath, clipPackfile(0.0f, "Rig", false, 1.0f, {0.0f, 0.0f, 0.0f, 1.0f}, {nonFinite, 1.0f, 1.0f})); }

    HkxService service;
    const auto local = service.pose(skeletonPath, clipPath, 0.5f, PoseSpace::Local);
    REQUIRE_FALSE(local.has_value());
    CHECK(local.error().type == "HkxRead");

    const auto both = service.poseBoth(skeletonPath, clipPath, 0.5f);
    REQUIRE_FALSE(both.has_value());
    CHECK(both.error().type == "HkxRead");
}

TEST_CASE("HkxService poses a lossless clip that BGS decoded and Radicle reported as a view") {
    // Constraint: detects a lossless clip still refused as a View, or a pose read at the wrong channel stride.
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    const auto skeletonPath = cleanup.root / "rig.hkx";
    const auto clipPath = cleanup.root / "lossless.hkx";
    writeBytes(skeletonPath, skeletonPackfile());
    writeBytes(clipPath, losslessClipPackfile());

    HkxService service;
    const auto local = service.pose(skeletonPath, clipPath, 0.5f, PoseSpace::Local);
    REQUIRE(local.has_value());
    CHECK((*local)["frame_count"].get<std::size_t>() == 2u);
    CHECK((*local)["bones"][1]["translation"][0].get<double>() == 1.5);
    // Constraint: track 0's second channel is static, so the pose must carry staticTranslations[0].
    CHECK((*local)["bones"][1]["translation"][1].get<double>() == 1.0);
    CHECK((*local)["bones"][2]["translation"][0].get<double>() == 2.0);

    const auto model = service.pose(skeletonPath, clipPath, 0.5f, PoseSpace::Model);
    REQUIRE(model.has_value());
    CHECK((*model)["bones"][2]["translation"][0].get<double>() == 3.5);
}

TEST_CASE("HkxService refuses a skeleton whose reference translation is NaN at the Radicle read, including for an unanimated bone") {
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    const auto skeletonPath = cleanup.root / "rig.hkx";
    const auto clipPath = cleanup.root / "clip.hkx";
    writeBytes(skeletonPath, skeletonPackfile("Rig", {"Root", "Spine", "Head"}, false,
                                              std::numeric_limits<float>::quiet_NaN()));
    writeBytes(clipPath, clipPackfile());

    HkxService service;
    const auto skeleton = service.skeleton(skeletonPath);
    REQUIRE_FALSE(skeleton.has_value());
    CHECK(skeleton.error().type == "HkxRead");

    const auto both = service.poseBoth(skeletonPath, clipPath, 0.5f);
    REQUIRE_FALSE(both.has_value());
    CHECK(both.error().type == "HkxRead");
}

namespace {

using namespace fo4::test::hkx;

std::vector<std::byte> twoAnimationObjectPackfile() {
    std::vector<std::byte> names;
    const auto name_at = add_class_name(names, signature_of("hkaInterleavedUncompressedAnimation"),
                                        "hkaInterleavedUncompressedAnimation");
    const std::size_t size = fo4::hkx::find_layout("hkaInterleavedUncompressedAnimation")->size;
    const auto second_at = static_cast<std::uint32_t>(size);
    return build_packfile({
        TestSection{"__classnames__", names, {}, {}, {}},
        TestSection{"__types__", {}, {}, {}, {}},
        TestSection{"__data__", filled(2 * size, std::byte{0}), {}, {}, {0, 0, name_at, second_at, 0, name_at}},
    });
}

std::vector<std::byte> refusedPredictivePackfile() {
    const char* const predictive = "hkaPredictiveCompressedAnimation";
    std::vector<std::byte> names;
    const auto name_at = add_class_name(names, signature_of(predictive), predictive);
    std::vector<std::byte> data = filled(fo4::hkx::find_layout(predictive)->size, std::byte{0});
    set_int_member(data, predictive, "numFrames", 1);
    set_int_member(data, predictive, "numFloatSlots", 1);
    set_int_member(data, "hkaAnimation", "numberOfFloatTracks", 1);
    return build_packfile({
        TestSection{"__classnames__", names, {}, {}, {}},
        TestSection{"__types__", {}, {}, {}, {}},
        TestSection{"__data__", std::move(data), {}, {}, {0, 0, name_at}},
    });
}

}  // namespace

TEST_CASE("HkxTarget Summary and InspectAnimation name the case a refused predictive clip declined") {
    // Constraint: detects a refusal reported as a bare UnsupportedLayout, which hides which predictive layout to decode next.
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    HkxService service;
    HkxTarget target(service);
    const auto clipPath = cleanup.root / "predictive.hkx";
    writeBytes(clipPath, refusedPredictivePackfile());

    const auto summary = target.invoke("Summary", nlohmann::json::array({clipPath.string()}));
    REQUIRE(summary.has_value());
    REQUIRE((*summary)["undecoded"].size() == 1u);
    CHECK((*summary)["undecoded"][0]["class_name"].get<std::string>() == "hkaPredictiveCompressedAnimation");
    CHECK((*summary)["undecoded"][0]["reason"].get<std::string>() == "unsupported-layout");
    CHECK((*summary)["undecoded"][0]["detail"].get<std::string>() == "float-track and float/bone boundary sampling");

    const auto inspect = target.invoke("InspectAnimation", nlohmann::json::array({clipPath.string()}));
    REQUIRE_FALSE(inspect.has_value());
    CHECK(inspect.error().message.find("UnsupportedLayout") != std::string::npos);
    CHECK(inspect.error().message.find("hkaPredictiveCompressedAnimation: float-track and float/bone boundary sampling") !=
          std::string::npos);
}

TEST_CASE("HkxTarget summary and InspectAnimation name a lossless clip's kind and keep its counts") {
    // Constraint: detects a kind switch without a lossless case, which reported the clip as a view in both reports.
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    const auto clipPath = cleanup.root / "lossless.hkx";
    writeBytes(clipPath, fo4::test::hkx::lossless_packfile());

    HkxService service;
    HkxTarget target(service);
    const auto summary = target.invoke("Summary", nlohmann::json::array({clipPath.string()}));
    REQUIRE(summary.has_value());
    CHECK((*summary)["undecoded"].empty());
    REQUIRE((*summary)["clips"].size() == 1u);
    CHECK((*summary)["clips"][0]["class_name"].get<std::string>() == "hkaLosslessCompressedAnimation");
    CHECK((*summary)["clips"][0]["kind"].get<std::string>() == "lossless");
    CHECK((*summary)["clips"][0]["frame_count"].get<std::size_t>() == 3u);
    CHECK((*summary)["clips"][0]["track_count"].get<std::size_t>() == 2u);
    CHECK((*summary)["clips"][0]["annotation_count"].get<std::size_t>() == 2u);

    const auto inspect = target.invoke("InspectAnimation", nlohmann::json::array({clipPath.string()}));
    REQUIRE(inspect.has_value());
    CHECK((*inspect)["kind"].get<std::string>() == "lossless");
    CHECK((*inspect)["frame_count"].get<std::size_t>() == 3u);
    CHECK((*inspect)["track_count"].get<std::size_t>() == 2u);
    CHECK((*inspect)["annotation_count"].get<std::size_t>() == 2u);
}

TEST_CASE("HkxTarget summary and InspectAnimation keep a float-only lossless clip's declared frames") {
    // Constraint: detects a zero-track clip reported with frame_count 0 and no float samples.
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    const auto clipPath = cleanup.root / "floatonly.hkx";
    writeBytes(clipPath, fo4::test::hkx::lossless_shape_packfile(4, 0, 0, 0, 2, 8, 0, 0.0f, true, 2.5f));

    HkxService service;
    HkxTarget target(service);
    const auto summary = target.invoke("Summary", nlohmann::json::array({clipPath.string()}));
    REQUIRE(summary.has_value());
    REQUIRE((*summary)["clips"].size() == 1u);
    CHECK((*summary)["clips"][0]["kind"].get<std::string>() == "lossless");
    CHECK((*summary)["clips"][0]["frame_count"].get<std::size_t>() == 4u);
    CHECK((*summary)["clips"][0]["track_count"].get<std::size_t>() == 0u);

    const auto inspect = target.invoke("InspectAnimation", nlohmann::json::array({clipPath.string()}));
    REQUIRE(inspect.has_value());
    CHECK((*inspect)["frame_count"].get<std::size_t>() == 4u);
    CHECK((*inspect)["float_track_count"].get<int>() == 2);
    CHECK((*inspect)["track_count"].get<std::size_t>() == 0u);
}

TEST_CASE("HkxTarget summary names an undecoded clip instead of succeeding with no clips") {
    // Constraint: detects a decode failure returned as success with clips [], which hid a broken clip from the caller.
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    HkxService service;
    HkxTarget target(service);

    const auto twoClips = cleanup.root / "twoclips.hkx";
    writeBytes(twoClips, twoAnimationObjectPackfile());
    const auto both = target.invoke("Summary", nlohmann::json::array({twoClips.string()}));
    REQUIRE(both.has_value());
    CHECK((*both)["clips"].empty());
    REQUIRE((*both)["undecoded"].size() == 1u);
    CHECK((*both)["undecoded"][0]["class_name"].get<std::string>() == "hkaInterleavedUncompressedAnimation");
    CHECK((*both)["undecoded"][0]["reason"].get<std::string>() == "more than one animation object: 2");

    const std::uint64_t dynamic_offset_two = std::uint64_t{2} | (std::uint64_t{2} << 2);
    const auto malformed = cleanup.root / "malformed.hkx";
    writeBytes(malformed, fo4::test::hkx::lossless_shape_packfile(3, 1, 3, 1, 0, 0, dynamic_offset_two));
    const auto broken = target.invoke("Summary", nlohmann::json::array({malformed.string()}));
    REQUIRE(broken.has_value());
    CHECK((*broken)["clips"].empty());
    REQUIRE((*broken)["undecoded"].size() == 1u);
    CHECK((*broken)["undecoded"][0]["class_name"].get<std::string>() == "hkaLosslessCompressedAnimation");
    CHECK((*broken)["undecoded"][0]["reason"].get<std::string>() == "bad-section");
}


namespace {

// Constraint: MTBehavior and its five prefixes must reproduce this shipped id, or the hash no longer matches the game's manifest name.
constexpr std::uint64_t kMtBehaviorSubgraph = 10121085405136975821ull;
const std::string kMtBehavior = "Actors\\Character\\Behaviors\\MTBehavior.hkx";
const std::vector<std::string> kMtPrefixes{
    "Actors\\Character\\Animations\\MT\\Injured\\Right",
    "Actors\\Character\\Animations\\MT\\Player",
    "Actors\\Character\\Animations\\MT\\Neutral",
    "Actors\\Character\\Animations",
    "actors\\Character\\Animations\\Furniture\\Water"};

void writeSubgraphPlugin(const std::filesystem::path& path, std::string_view signature,
                         std::string_view editorId, const std::string& graph,
                         const std::vector<std::string>& prefixes) {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(0x800);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto group = fo4::test::begin_group(out, std::string(signature), 0);
    const auto record = fo4::test::begin_record(out, signature, 0, 0x800u, 131);
    fo4::test::write_subrecord(out, "EDID", std::string_view(editorId.data(), editorId.size() + 1));
    if (!graph.empty()) {
        fo4::test::write_subrecord(out, "SGNM", std::string_view(graph.data(), graph.size() + 1));
    }
    for (const auto& prefix : prefixes) {
        fo4::test::write_subrecord(out, "SAPT", std::string_view(prefix.data(), prefix.size() + 1));
    }
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);

    std::ofstream stream(path, std::ios::binary);
    REQUIRE(stream.good());
    stream.write(reinterpret_cast<const char*>(out.bytes.data()),
                 static_cast<std::streamsize>(out.bytes.size()));
    REQUIRE(stream.good());
}

}

TEST_CASE("HkxTarget Subgraph resolves a race to the id the game names its manifest by") {
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    const auto pluginPath = cleanup.root / "Races.esp";
    writeSubgraphPlugin(pluginPath, "RACE", "TestRace", kMtBehavior, kMtPrefixes);

    const auto dataFolder = cleanup.root / "Data";
    const auto manifestDir = dataFolder / "Meshes" / "AnimTextData" / "AnimationFileData";
    std::filesystem::create_directories(manifestDir);
    const auto manifestPath = manifestDir / (std::to_string(kMtBehaviorSubgraph) + ".txt");
    {
        std::ofstream stream(manifestPath, std::ios::binary | std::ios::trunc);
        stream << "V4\n0\n" << kMtBehaviorSubgraph << "\n0\n" << kMtBehavior << "\n"
               << "Actors\\Character\\Animations\\MT\\Neutral\n"
               << "Actors\\Character\\Animations\\MT\\Missing\n";
        REQUIRE(stream.good());
    }
    const auto animationDir =
        dataFolder / "Meshes" / "Actors" / "Character" / "Animations" / "MT";
    std::filesystem::create_directories(animationDir);
    {
        std::ofstream stream(animationDir / "Neutral.hkx", std::ios::binary | std::ios::trunc);
        stream << "x";
        REQUIRE(stream.good());
    }

    HkxService service;
    HkxTarget target(service);
    const auto reply = target.invoke(
        "Subgraph", nlohmann::json::array({pluginPath.string(), "0x800", dataFolder.string()}));
    REQUIRE(reply.has_value());

    // Constraint: the id must hash the record's whole SAPT list, or the race resolves to another race's manifest.
    REQUIRE((*reply)["subgraph_ids"].size() == 1u);
    CHECK((*reply)["subgraph_ids"][0].get<std::uint64_t>() == kMtBehaviorSubgraph);
    CHECK((*reply)["behavior_graphs"][0].get<std::string>() == kMtBehavior);
    CHECK((*reply)["path_prefixes"].size() == kMtPrefixes.size());

    REQUIRE_FALSE((*reply)["manifest"].is_null());
    CHECK((*reply)["manifest"]["id"].get<std::uint64_t>() == kMtBehaviorSubgraph);
    CHECK((*reply)["manifest"]["source"].get<std::string>() == "loose");
    CHECK((*reply)["manifest"]["behavior_paths"][0].get<std::string>() == kMtBehavior);

    // Constraint: an animation the Data folder does not carry must land in unresolved, or a gap reads as clean.
    CHECK((*reply)["manifest"]["present"].get<std::size_t>() == 1u);
    REQUIRE((*reply)["manifest"]["unresolved"].size() == 1u);
    CHECK((*reply)["manifest"]["unresolved"][0].get<std::string>() ==
          "Actors\\Character\\Animations\\MT\\Missing");
}

TEST_CASE("HkxTarget Subgraph refuses a non-race record, a bad form id and a missing record") {
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    const auto pluginPath = cleanup.root / "Plain.esp";
    writeSubgraphPlugin(pluginPath, "MISC", "TestMisc", "", {});
    const auto dataFolder = cleanup.root / "Data";
    std::filesystem::create_directories(dataFolder);

    HkxService service;
    HkxTarget target(service);

    // Constraint: a non-race record must be refused, so a weapon or cell id cannot produce a manifest.
    const auto notARace = target.invoke(
        "Subgraph", nlohmann::json::array({pluginPath.string(), "0x800", dataFolder.string()}));
    REQUIRE_FALSE(notARace.has_value());
    CHECK(notARace.error().type == "InvalidRecord");

    const auto badId = target.invoke(
        "Subgraph", nlohmann::json::array({pluginPath.string(), "not-a-number", dataFolder.string()}));
    REQUIRE_FALSE(badId.has_value());
    CHECK(badId.error().type == "InvalidFormId");

    const auto missing = target.invoke(
        "Subgraph", nlohmann::json::array({pluginPath.string(), "0x900", dataFolder.string()}));
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().type == "RecordNotFound");
}

namespace {

std::vector<std::byte> objectGraphPackfile() {
    const auto skeleton = fo4::hkx::find_layout("hkaSkeleton");
    const auto bone = fo4::hkx::find_layout("hkaBone");
    std::vector<std::byte> names;
    const auto skeletonName = add_class_name(names, skeleton->signature, "hkaSkeleton");
    const auto boneName = add_class_name(names, bone->signature, "hkaBone");
    const std::size_t bonesField = member_at("hkaSkeleton", "bones");
    std::vector<std::byte> data(skeleton->size, std::byte{0});
    std::vector<std::uint32_t> local;
    const std::size_t boneAt = data.size();
    data.resize(boneAt + bone->size, std::byte{0});
    patch_u32(data, bonesField + 8, 1);
    link(local, bonesField, boneAt);
    return build_packfile({
        TestSection{"__classnames__", names, {}, {}, {}},
        TestSection{"__types__", {}, {}, {}, {}},
        TestSection{"__data__", std::move(data), std::move(local), {},
                    {0, 0, skeletonName, static_cast<std::uint32_t>(boneAt), 0, boneName}},
    });
}

}  // namespace

TEST_CASE("HkxTarget InspectObjects resolves the skeleton's bone pointer for the object panel") {
    // Constraint: detects a view that lists objects but leaves every pointer unresolved, which no panel can navigate.
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    HkxService service;
    HkxTarget target(service);

    const auto file = cleanup.root / "rig.hkx";
    writeBytes(file, objectGraphPackfile());

    const auto all = target.invoke("InspectObjects", nlohmann::json::array({file.string()}));
    REQUIRE(all.has_value());
    CHECK((*all)["object_count"].get<std::size_t>() == 2u);
    REQUIRE((*all)["objects"].size() == 2u);
    const auto& skeleton = (*all)["objects"][0];
    CHECK(skeleton["class_name"].get<std::string>() == "hkaSkeleton");
    CHECK(skeleton["layout_known"].get<bool>());
    CHECK(skeleton["fields"].size() > 0u);
    REQUIRE(skeleton["references"].size() == 1u);
    CHECK(skeleton["references"][0]["member"].get<std::string>() == "bones");
    CHECK(skeleton["references"][0]["target_object"].get<int>() == 1);
    CHECK(skeleton["references"][0]["target_class"].get<std::string>() == "hkaBone");

    const auto only = target.invoke("InspectObjects", nlohmann::json::array({file.string(), "hkaBone"}));
    REQUIRE(only.has_value());
    CHECK((*only)["matched"].get<std::size_t>() == 1u);
    CHECK((*only)["objects"].size() == 1u);
    CHECK((*only)["objects"][0]["layout_known"].get<bool>());

    const auto missing = target.invoke("InspectObjects",
                                       nlohmann::json::array({(cleanup.root / "missing.hkx").string()}));
    CHECK_FALSE(missing.has_value());
}

TEST_CASE("HkxTarget ReadScene names the root variants and says the scene metadata is not decoded") {
    // Constraint: detects a scene wrapper hidden from the asset tree, or unit and axis shown for a class with no verified layout.
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    HkxService service;
    HkxTarget target(service);

    const auto root = fo4::hkx::find_layout("hkRootLevelContainer");
    const auto variant = fo4::hkx::find_layout("hkRootLevelContainerNamedVariant");
    std::vector<std::byte> names;
    const auto rootName = add_class_name(names, root->signature, "hkRootLevelContainer");
    const auto sceneName = add_class_name(names, 0x5C3E0001u, "hkxScene");
    const std::size_t arrayAt = member_at("hkRootLevelContainer", "namedVariants");
    std::vector<std::byte> data(root->size, std::byte{0});
    std::vector<std::uint32_t> local;
    patch_u32(data, arrayAt + 8, 1);
    const std::size_t entryAt = data.size();
    data.resize(entryAt + variant->size, std::byte{0});
    link(local, arrayAt, entryAt);
    link(local, entryAt + member_at("hkRootLevelContainerNamedVariant", "name"), appendString(data, "Scene Data"));
    data.resize((data.size() + 15) / 16 * 16, std::byte{0});
    const std::size_t sceneAt = data.size();
    data.resize(sceneAt + 16, std::byte{0x5A});
    const auto file = cleanup.root / "scene.hkx";
    writeBytes(file, build_packfile({
        TestSection{"__classnames__", names, {}, {}, {}},
        TestSection{"__types__", {}, {}, {}, {}},
        TestSection{"__data__", std::move(data), std::move(local),
                    {static_cast<std::uint32_t>(entryAt + member_at("hkRootLevelContainerNamedVariant", "variant")), 2,
                     static_cast<std::uint32_t>(sceneAt)},
                    {0, 0, rootName, static_cast<std::uint32_t>(sceneAt), 0, sceneName}},
    }));

    const auto reply = target.invoke("ReadScene", nlohmann::json::array({file.string()}));
    REQUIRE(reply.has_value());
    CHECK((*reply)["has_root"].get<bool>());
    REQUIRE((*reply)["variants"].size() == 1u);
    CHECK((*reply)["variants"][0]["name"].get<std::string>() == "Scene Data");
    CHECK((*reply)["variants"][0]["class_name"].get<std::string>() == "hkxScene");
    CHECK_FALSE((*reply)["variants"][0]["layout_known"].get<bool>());
    REQUIRE((*reply)["scene_objects"].size() == 1u);
    CHECK_FALSE((*reply)["metadata"]["decoded"].get<bool>());
    CHECK((*reply)["unknown_classes"][0].get<std::string>() == "hkxScene");

    CHECK_FALSE(target.invoke("ReadScene", nlohmann::json::array({(cleanup.root / "missing.hkx").string()})).has_value());
}

TEST_CASE("HkxTarget ExtractedMotion reports the pose, summary, path and marks Radicle computes") {
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    HkxService service;
    HkxTarget target(service);

    MotionSpec spec;
    spec.duration = 2.0f;
    spec.samples = {{0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 4.0f, 0.0f, 1.0f}};
    const auto walk = cleanup.root / "walk.hkx";
    writeBytes(walk, motion_packfile(spec));

    const auto basic = target.invoke("ExtractedMotion", nlohmann::json::array({walk.string(), 1.0}));
    REQUIRE(basic.has_value());
    CHECK((*basic)["kind"].get<std::string>() == "default");
    CHECK((*basic)["previewable"].get<bool>());
    CHECK((*basic)["at"][1].get<double>() == Catch::Approx(2.0));
    CHECK((*basic)["origin"][1].get<double>() == Catch::Approx(0.0));
    CHECK((*basic)["summary"]["net_translation"][1].get<double>() == Catch::Approx(4.0));
    CHECK_FALSE(basic->contains("path"));
    CHECK_FALSE(basic->contains("marks"));

    const auto full = target.invoke("ExtractedMotion", nlohmann::json::array({walk.string(), 0.0, false, true}));
    REQUIRE(full.has_value());
    CHECK((*full)["path"].size() == 2u);
    CHECK_FALSE((*full)["path_truncated"].get<bool>());
    REQUIRE((*full)["marks"].size() == 3u);
    CHECK((*full)["marks"][1][1].get<double>() == Catch::Approx(2.0));

    const auto thinned = target.invoke("ExtractedMotion", nlohmann::json::array({walk.string(), 0.0, false, true, 1}));
    REQUIRE(thinned.has_value());
    CHECK((*thinned)["path"].size() == 1u);
    CHECK((*thinned)["path_truncated"].get<bool>());

    const auto clamped = target.invoke("ExtractedMotion", nlohmann::json::array({walk.string(), 3.0}));
    REQUIRE(clamped.has_value());
    CHECK((*clamped)["at"][1].get<double>() == Catch::Approx(4.0));
    const auto looped = target.invoke("ExtractedMotion", nlohmann::json::array({walk.string(), 3.0, true}));
    REQUIRE(looped.has_value());
    CHECK((*looped)["at"][1].get<double>() == Catch::Approx(2.0));

    const auto negative = target.invoke("ExtractedMotion", nlohmann::json::array({walk.string(), -1.0}));
    REQUIRE_FALSE(negative.has_value());
    CHECK(negative.error().type == "InvalidTime");

    const auto still = cleanup.root / "still.hkx";
    writeBytes(still, motion_packfile(std::nullopt));
    const auto none = target.invoke("ExtractedMotion", nlohmann::json::array({still.string()}));
    REQUIRE(none.has_value());
    CHECK((*none)["kind"].get<std::string>() == "none");
    CHECK_FALSE((*none)["previewable"].get<bool>());
    CHECK_FALSE(none->contains("at"));

    MotionSpec other;
    other.frame_class = "hkaAnimatedReferenceFrame";
    other.samples = {{1.0f, 2.0f, 3.0f, 4.0f}};
    const auto odd = cleanup.root / "odd.hkx";
    writeBytes(odd, motion_packfile(other));
    const auto unsupported = target.invoke("ExtractedMotion", nlohmann::json::array({odd.string()}));
    REQUIRE(unsupported.has_value());
    CHECK((*unsupported)["kind"].get<std::string>() == "unsupported");
    CHECK_FALSE((*unsupported)["previewable"].get<bool>());

    CHECK_FALSE(target.invoke("ExtractedMotion", nlohmann::json::array({(cleanup.root / "gone.hkx").string()})).has_value());
}

TEST_CASE("HkxTarget RootMotion reports the clip's travel and loop closure per bone") {
    // Constraint: detects a report that reads the first track for every bone, which shows a still spine as the walk.
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    HkxService service;
    HkxTarget target(service);

    const auto skeleton = cleanup.root / "rig.hkx";
    writeBytes(skeleton, skeletonPackfile());
    const auto clip = cleanup.root / "walk.hkx";
    // Constraint: an interleaved transform array is frame-major, so track 0 holds elements 0 and 2 and track 1 holds 1 and 3.
    writeBytes(clip, clipPackfile(0.0f, "Rig", false, 1.0f, {0.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {},
                                  {{1.0f, 0.0f, 0.0f}, {2.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {3.0f, 0.0f, 0.0f}}));

    const auto moved = target.invoke("RootMotion", nlohmann::json::array({skeleton.string(), clip.string(), 2}));
    REQUIRE(moved.has_value());
    CHECK((*moved)["bone_name"].get<std::string>() == "Head");
    CHECK((*moved)["frame_count"].get<std::size_t>() == 2u);
    CHECK(std::fabs((*moved)["total"][0].get<float>() - 1.0f) < 1e-4f);
    CHECK(std::fabs((*moved)["distance"].get<float>() - 1.0f) < 1e-4f);
    CHECK_FALSE((*moved)["loops"].get<bool>());

    const auto still = target.invoke("RootMotion", nlohmann::json::array({skeleton.string(), clip.string(), 1}));
    REQUIRE(still.has_value());
    CHECK((*still)["loops"].get<bool>());
    CHECK(std::fabs((*still)["distance"].get<float>()) < 1e-4f);

    const auto missing = target.invoke("RootMotion", nlohmann::json::array({skeleton.string(), clip.string(), 9}));
    CHECK_FALSE(missing.has_value());
}

TEST_CASE("HkxTarget SkeletonDocument and SkeletonEdit report depths and refuse a colliding rename") {
    // Constraint: detects a document without depth or a remove that orphans children, which the panel would show as a valid rig.
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    HkxService service;
    HkxTarget target(service);
    const auto skeleton = cleanup.root / "rig.hkx";
    writeBytes(skeleton, skeletonPackfile());

    const auto document = target.invoke("SkeletonDocument", nlohmann::json::array({skeleton.string()}));
    REQUIRE(document.has_value());
    CHECK((*document)["bone_count"].get<std::size_t>() == 3u);
    CHECK((*document)["bones"][2]["depth"].get<std::size_t>() == 2u);
    REQUIRE((*document)["roots"].size() == 1u);
    CHECK((*document)["roots"][0].get<std::string>() == "Root");

    const auto renamed = target.invoke("SkeletonEdit",
                                       nlohmann::json::array({skeleton.string(), "rename", 2, "Skull", -1}));
    REQUIRE(renamed.has_value());
    CHECK((*renamed)["bones"][2]["name"].get<std::string>() == "Skull");

    const auto clash = target.invoke("SkeletonEdit",
                                     nlohmann::json::array({skeleton.string(), "rename", 1, "root", -1}));
    CHECK_FALSE(clash.has_value());

    const auto orphan = target.invoke("SkeletonEdit", nlohmann::json::array({skeleton.string(), "remove", 0, "", -1}));
    CHECK_FALSE(orphan.has_value());

    const auto leaf = target.invoke("SkeletonEdit", nlohmann::json::array({skeleton.string(), "remove", 2, "", -1}));
    REQUIRE(leaf.has_value());
    CHECK((*leaf)["bone_count"].get<std::size_t>() == 2u);
}

namespace {

std::vector<std::byte> fileBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    std::vector<std::byte> bytes(static_cast<std::size_t>(stream.tellg()));
    stream.seekg(0);
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return bytes;
}

}  // namespace

TEST_CASE("HkxTarget SkeletonSave writes an edited skeleton and reads it back") {
    // Constraint: detects a save that skips the reread, overwrites the source on a save as, or drops the locks, slots, frames and partitions.
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    HkxService service;
    HkxTarget target(service);
    const auto source = cleanup.root / "rig.hkx";
    const auto copy = cleanup.root / "rig_edited.hkx";
    writeBytes(source, skeletonPackfile());
    const auto original = fileBytes(source);

    const auto opened = target.invoke("SkeletonDocument", nlohmann::json::array({source.string()}));
    REQUIRE(opened.has_value());
    const auto hash = (*opened)["source_hash"].get<std::string>();
    const std::string edits =
        R"([{"kind":"rename","bone":2,"name":"Skull"},{"kind":"reparent","bone":2,"parent":0}])";

    const auto preview = target.invoke("SkeletonPreview", nlohmann::json::array({source.string(), edits}));
    REQUIRE(preview.has_value());
    CHECK((*preview)["saveable"].get<bool>() == true);
    CHECK((*preview)["bones"][2]["name"].get<std::string>() == "Skull");
    CHECK((*preview)["bones"][2]["parent"].get<int>() == 0);
    CHECK(fileBytes(source) == original);

    const auto saved = target.invoke("SkeletonSave", nlohmann::json::array({source.string(), edits, copy.string(), hash}));
    REQUIRE(saved.has_value());
    CHECK((*saved)["saved"].get<bool>() == true);
    CHECK((*saved)["bones"][2]["name"].get<std::string>() == "Skull");
    CHECK(fileBytes(source) == original);

    const auto reread = target.invoke("Skeleton", nlohmann::json::array({copy.string()}));
    REQUIRE(reread.has_value());
    CHECK((*reread)["bones"][2]["name"].get<std::string>() == "Skull");
    CHECK((*reread)["bones"][2]["parent"].get<int>() == 0);
    CHECK((*reread)["bones"][2]["lock"].get<bool>() == true);
    CHECK((*reread)["float_slots"].size() == 2u);
    CHECK((*reread)["local_frames"].size() == 1u);
    CHECK((*reread)["partitions"].size() == 1u);

    const std::string again = R"([{"kind":"rename","bone":1,"name":"Chest"}])";
    const auto inPlace = target.invoke("SkeletonSave", nlohmann::json::array({source.string(), again, "", hash}));
    REQUIRE(inPlace.has_value());
    CHECK((*inPlace)["source_hash"].get<std::string>() != hash);
    const auto changed = target.invoke("SkeletonDocument", nlohmann::json::array({source.string()}));
    REQUIRE(changed.has_value());
    CHECK((*changed)["bones"][1]["name"].get<std::string>() == "Chest");
}

TEST_CASE("HkxTarget SkeletonSave refuses without touching the source or the destination") {
    // Constraint: detects a refused save that still replaces a file, a stale hash that overwrites an external change, or a save as over another file.
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    HkxService service;
    HkxTarget target(service);
    const auto source = cleanup.root / "rig.hkx";
    const auto other = cleanup.root / "other.hkx";
    writeBytes(source, skeletonPackfile());
    writeBytes(other, skeletonPackfile("Other"));
    const auto original = fileBytes(source);
    const auto otherOriginal = fileBytes(other);
    const auto opened = target.invoke("SkeletonDocument", nlohmann::json::array({source.string()}));
    REQUIRE(opened.has_value());
    const auto hash = (*opened)["source_hash"].get<std::string>();

    const std::string rename = R"([{"kind":"rename","bone":2,"name":"Skull"}])";
    const std::string remove = R"([{"kind":"remove","bone":2}])";

    const auto partitioned = target.invoke("SkeletonPreview", nlohmann::json::array({source.string(), remove}));
    REQUIRE(partitioned.has_value());
    CHECK((*partitioned)["saveable"].get<bool>() == false);
    CHECK((*partitioned)["save_refusal"].get<std::string>().find("partitions") != std::string::npos);
    CHECK_FALSE(target.invoke("SkeletonSave", nlohmann::json::array({source.string(), remove, "", hash})).has_value());
    CHECK(fileBytes(source) == original);

    CHECK_FALSE(target.invoke("SkeletonSave", nlohmann::json::array({source.string(), rename, "", "0000000000000000"})).has_value());
    CHECK(fileBytes(source) == original);

    CHECK_FALSE(target.invoke("SkeletonSave", nlohmann::json::array({source.string(), rename, other.string(), hash})).has_value());
    CHECK(fileBytes(other) == otherOriginal);

    CHECK_FALSE(target.invoke("SkeletonSave", nlohmann::json::array({source.string(), rename, (cleanup.root / "rig.txt").string(), hash})).has_value());
    CHECK_FALSE(std::filesystem::exists(cleanup.root / "rig.txt"));

    CHECK_FALSE(target.invoke("SkeletonSave", nlohmann::json::array({source.string(), "[]", "", hash})).has_value());
    CHECK_FALSE(target.invoke("SkeletonSave", nlohmann::json::array({source.string(), "not json", "", hash})).has_value());
    CHECK_FALSE(target.invoke("SkeletonSave", nlohmann::json::array({source.string(), R"([{"kind":"rename","bone":-1}])", "", hash})).has_value());
    const auto noParent = target.invoke("SkeletonPreview", nlohmann::json::array({source.string(), R"([{"kind":"reparent","bone":2}])"}));
    REQUIRE_FALSE(noParent.has_value());
    CHECK(noParent.error().message.find("reparent needs a parent") != std::string::npos);
    CHECK(fileBytes(source) == original);
}

TEST_CASE("HkxTarget SkeletonWrite previews, saves a new file and replaces only with overwrite") {
    // Constraint: detects an MCP skeleton save that writes without out_path, replaces a file without overwrite, or loses pose and lock edits.
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    HkxService service;
    HkxTarget target(service);
    const auto source = cleanup.root / "rig.hkx";
    const auto copy = cleanup.root / "rig_posed.hkx";
    writeBytes(source, skeletonPackfile());
    const auto original = fileBytes(source);

    const auto opened = target.invoke("SkeletonDocument", nlohmann::json::array({source.string()}));
    REQUIRE(opened.has_value());
    const auto hash = (*opened)["source_hash"].get<std::string>();
    CHECK((*opened)["bones"][1]["translation"][0].get<float>() == 1.0f);
    CHECK((*opened)["bones"][1]["lock"].get<bool>() == false);
    CHECK((*opened)["bones"][2]["lock"].get<bool>() == true);

    const auto edits = nlohmann::json::parse(
        R"([{"kind":"set_pose","bone":0,"translation":[12.5,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},)"
        R"({"kind":"set_lock","bone":1,"locked":true}])");

    const auto preview = target.invoke("SkeletonWrite", nlohmann::json::array({source.string(), edits, "", false, hash}));
    REQUIRE(preview.has_value());
    CHECK((*preview)["preview"].get<bool>() == true);
    CHECK((*preview)["saved"].get<bool>() == false);
    CHECK((*preview)["saveable"].get<bool>() == true);
    CHECK((*preview)["bones"][0]["translation"][0].get<float>() == 12.5f);
    CHECK((*preview)["bones"][1]["lock"].get<bool>() == true);
    CHECK(fileBytes(source) == original);

    const auto saved = target.invoke("SkeletonWrite", nlohmann::json::array({source.string(), edits, copy.string(), false, hash}));
    REQUIRE(saved.has_value());
    CHECK((*saved)["saved"].get<bool>() == true);
    CHECK(fileBytes(source) == original);
    const auto reread = target.invoke("Skeleton", nlohmann::json::array({copy.string()}));
    REQUIRE(reread.has_value());
    CHECK((*reread)["bones"][0]["translation"][0].get<float>() == 12.5f);
    CHECK((*reread)["bones"][1]["translation"][0].get<float>() == 1.0f);
    CHECK((*reread)["bones"][1]["lock"].get<bool>() == true);
    CHECK((*reread)["bones"][2]["lock"].get<bool>() == true);
    CHECK((*reread)["partitions"].size() == 1u);
    const auto savedBytes = fileBytes(copy);

    CHECK_FALSE(target.invoke("SkeletonWrite", nlohmann::json::array({source.string(), edits, copy.string(), false, hash})).has_value());
    CHECK(fileBytes(copy) == savedBytes);
    CHECK_FALSE(target.invoke("SkeletonWrite", nlohmann::json::array({source.string(), edits, source.string(), false, hash})).has_value());
    CHECK(fileBytes(source) == original);

    const auto inPlace = target.invoke("SkeletonWrite", nlohmann::json::array({source.string(), edits.dump(), source.string(), true, hash}));
    REQUIRE(inPlace.has_value());
    CHECK((*inPlace)["saved"].get<bool>() == true);
    CHECK(fileBytes(source) == savedBytes);
}

TEST_CASE("HkxTarget SkeletonWrite refuses unusable poses and unsafe saves without writing a file") {
    // Constraint: detects a non-unit or overflowing pose, a stale expected_source or a bad destination that still produces a file.
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    HkxService service;
    HkxTarget target(service);
    const auto source = cleanup.root / "rig.hkx";
    const auto out = cleanup.root / "never.hkx";
    writeBytes(source, skeletonPackfile());
    const auto original = fileBytes(source);
    const auto opened = target.invoke("SkeletonDocument", nlohmann::json::array({source.string()}));
    REQUIRE(opened.has_value());
    const auto hash = (*opened)["source_hash"].get<std::string>();

    const auto refused = [&](const std::string& edits, const std::string& to, const std::string& expected) {
        const auto reply = target.invoke("SkeletonWrite", nlohmann::json::array({source.string(), nlohmann::json::parse(edits), to, false, expected}));
        return reply.has_value() ? std::string() : reply.error().message;
    };
    const std::string tail = R"("rotation":[0,0,0,1],"scale":[1,1,1]})";
    CHECK(refused(R"([{"kind":"set_pose","bone":0,"translation":[1e39,0,0],)" + tail + "]", out.string(), hash).find("finite") != std::string::npos);
    CHECK(refused(R"([{"kind":"set_pose","bone":0,"translation":[0,0,0],"rotation":[0,0,0,2],"scale":[1,1,1]}])", out.string(), hash)
              .find("unit") != std::string::npos);
    CHECK_FALSE(refused(R"([{"kind":"set_pose","bone":0,"translation":[0,0,0],"rotation":[0,0,0,1]}])", out.string(), hash).empty());
    CHECK_FALSE(refused(R"([{"kind":"set_pose","bone":9,"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]}])", out.string(), hash).empty());
    CHECK_FALSE(refused(R"([{"kind":"set_lock","bone":1,"locked":"yes"}])", out.string(), hash).empty());
    CHECK_FALSE(refused(R"([{"kind":"set_lock","bone":1,"locked":true}])", out.string(), "0000000000000000").empty());
    CHECK_FALSE(refused(R"([{"kind":"set_lock","bone":1,"locked":true}])", (cleanup.root / "rig.txt").string(), hash).empty());
    CHECK_FALSE(std::filesystem::exists(out));
    CHECK_FALSE(std::filesystem::exists(cleanup.root / "rig.txt"));
    CHECK(fileBytes(source) == original);
}

namespace {

std::vector<std::byte> behaviorPackfile() {
    const auto graph = fo4::hkx::find_layout("hkbAssetBundle");
    const auto generator = fo4::hkx::find_layout("hkbAnimatedSkeletonGenerator");
    std::vector<std::byte> names;
    const auto graphName = add_class_name(names, graph->signature, "hkbAssetBundle");
    const auto generatorName = add_class_name(names, generator->signature, "hkbAnimatedSkeletonGenerator");
    std::vector<std::byte> data(graph->size, std::byte{0});
    std::vector<std::uint32_t> local;
    const std::size_t generatorAt = data.size();
    data.resize(generatorAt + generator->size, std::byte{0});
    link(local, 0, generatorAt);
    return build_packfile({
        TestSection{"__classnames__", names, {}, {}, {}},
        TestSection{"__types__", {}, {}, {}, {}},
        TestSection{"__data__", std::move(data), std::move(local), {},
                    {0, 0, graphName, static_cast<std::uint32_t>(generatorAt), 0, generatorName}},
    });
}

}  // namespace

TEST_CASE("HkxTarget InspectBehavior graphs the behavior objects and names a file without any") {
    // Constraint: detects a behavior view that drops the link between two hkb objects, which hides the graph shape.
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    HkxService service;
    HkxTarget target(service);

    const auto behavior = cleanup.root / "behavior.hkx";
    writeBytes(behavior, behaviorPackfile());
    const auto graph = target.invoke("InspectBehavior", nlohmann::json::array({behavior.string()}));
    REQUIRE(graph.has_value());
    CHECK((*graph)["matched"].get<std::size_t>() == 2u);
    REQUIRE((*graph)["nodes"].size() == 2u);
    CHECK((*graph)["nodes"][0]["class_name"].get<std::string>() == "hkbAssetBundle");
    REQUIRE((*graph)["links"].size() == 1u);
    CHECK((*graph)["links"][0]["to"].get<std::size_t>() == 1u);
    CHECK((*graph)["nodes"][1]["references_in"].get<std::size_t>() == 1u);

    const auto rig = cleanup.root / "rig.hkx";
    writeBytes(rig, skeletonPackfile());
    const auto none = target.invoke("InspectBehavior", nlohmann::json::array({rig.string()}));
    REQUIRE(none.has_value());
    CHECK((*none)["nodes"].empty());
    REQUIRE((*none)["diagnostics"].size() == 1u);
    CHECK((*none)["diagnostics"][0].get<std::string>().find("no behavior objects") != std::string::npos);
}

TEST_CASE("HkxTarget ReadBehaviorModel returns the typed graph with child edges, states, events and variables") {
    // Constraint: detects a typed view that drops the child edges or the state generators, which leaves a state machine looking childless.
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    HkxService service;
    HkxTarget target(service);

    const auto generators = cleanup.root / "generators.hkx";
    writeBytes(generators, generator_graph_packfile());
    const auto reply = target.invoke("ReadBehaviorModel", nlohmann::json::array({generators.string()}));
    REQUIRE(reply.has_value());
    CHECK((*reply)["graph_name"].get<std::string>() == "GeneratorGraph");
    CHECK((*reply)["root_node"].get<int>() == 3);
    REQUIRE((*reply)["nodes"].size() == 15u);
    CHECK((*reply)["nodes"][2]["kind"].get<std::string>() == "selector");
    CHECK((*reply)["nodes"][4]["kind"].get<std::string>() == "layer");
    CHECK((*reply)["nodes"][4]["name"].get<std::string>() == "Layers");
    REQUIRE((*reply)["edges"].size() == 15u);
    CHECK((*reply)["edges"][3]["parent"].get<std::size_t>() == 4u);
    CHECK((*reply)["edges"][3]["child"].get<int>() == 0);
    CHECK((*reply)["edges"][3]["role"].get<std::string>() == "layer");
    CHECK((*reply)["edges"][3]["weight"].get<float>() == 0.25f);
    CHECK((*reply)["edges"][8]["role"].get<std::string>() == "bone_switch_child");
    CHECK((*reply)["edges"][8]["has_bone_weights"].get<bool>());

    const auto cut = target.invoke("ReadBehaviorModel", nlohmann::json::array({generators.string(), 2}));
    REQUIRE(cut.has_value());
    CHECK((*cut)["nodes"].size() == 2u);
    CHECK((*cut)["edges"].size() == 2u);
    CHECK((*cut)["node_total"].get<std::size_t>() == 15u);
    CHECK((*cut)["edge_total"].get<std::size_t>() == 15u);

    const auto machine = cleanup.root / "machine.hkx";
    writeBytes(machine, behavior_graph_packfile());
    const auto states = target.invoke("ReadBehaviorModel", nlohmann::json::array({machine.string()}));
    REQUIRE(states.has_value());
    CHECK((*states)["graph_name"].get<std::string>() == "TestGraph");
    REQUIRE((*states)["events"].size() == 3u);
    CHECK((*states)["events"][2]["name"].get<std::string>() == "Wave");
    REQUIRE((*states)["variables"].size() == 3u);
    CHECK((*states)["variables"][1]["name"].get<std::string>() == "fSpeed");
    CHECK((*states)["variables"][2]["vector"].size() == 4u);
    REQUIRE((*states)["states"].size() == 2u);
    CHECK((*states)["states"][1]["name"].get<std::string>() == "Mix");
    CHECK((*states)["states"][1]["state_id"].get<int>() == 9);
    const auto mix = (*states)["states"][1]["generator_node"].get<std::size_t>();
    CHECK((*states)["nodes"][mix]["class_name"].get<std::string>() == "hkbBlenderGenerator");
}

TEST_CASE("HkxTarget ReadBehaviorModel refuses a file without a behavior graph and a missing file") {
    // Constraint: detects an unreadable or graph-less file reported as an empty graph instead of an error.
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    HkxService service;
    HkxTarget target(service);

    const auto rig = cleanup.root / "rig.hkx";
    writeBytes(rig, skeletonPackfile());
    const auto none = target.invoke("ReadBehaviorModel", nlohmann::json::array({rig.string()}));
    REQUIRE_FALSE(none.has_value());
    CHECK(none.error().type == "HkxRead");
    CHECK(none.error().message.find("MissingObject") != std::string::npos);

    const auto missing = target.invoke("ReadBehaviorModel", nlohmann::json::array({(cleanup.root / "nope.hkx").string()}));
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().type == "FileNotFound");
}

TEST_CASE("HkxTarget ReadBehaviorAuthoring returns every transition with its fields and the validation") {
    // Constraint: detects an authoring view that drops a transition's event, target, flags, effect or condition, or that cuts a list, which would make an editor write rows it never saw.
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    HkxService service;
    HkxTarget target(service);

    const auto machine = cleanup.root / "machine.hkx";
    writeBytes(machine, behavior_graph_packfile());
    const auto reply = target.invoke("ReadBehaviorAuthoring", nlohmann::json::array({machine.string()}));
    REQUIRE(reply.has_value());
    REQUIRE((*reply)["transitions"].size() == 2u);
    const auto& first = (*reply)["transitions"][0];
    CHECK(first["state"].get<int>() == 0);
    CHECK(first["event_id"].get<int>() == 1);
    CHECK(first["to_state_id"].get<int>() == 9);
    CHECK(first["priority"].get<int>() == 3);
    CHECK(first["flags"].get<int>() == 0x0A00);
    CHECK(first["condition_kind"].get<std::string>() == "string");
    CHECK(first["condition"].get<std::string>() == "x > 1");
    CHECK(first["effect_node"].get<int>() >= 0);
    CHECK((*reply)["transitions"][1]["state"].get<int>() == -1);
    REQUIRE((*reply)["state_machines"].size() == 1u);
    CHECK((*reply)["state_machines"][0]["wildcard_count"].get<std::size_t>() == 1u);
    CHECK((*reply)["events"].size() == 3u);
    CHECK((*reply)["states"][1]["name"].get<std::string>() == "Mix");
    CHECK((*reply)["validation"]["errors"].get<std::size_t>() == 0u);

    const auto checked = target.invoke("ValidateBehaviorGraph", nlohmann::json::array({machine.string()}));
    REQUIRE(checked.has_value());
    CHECK((*checked)["errors"].get<std::size_t>() == 0u);
    CHECK((*checked)["issue_total"].get<std::size_t>() == (*checked)["issues"].size());

    const auto rig = cleanup.root / "rig.hkx";
    writeBytes(rig, skeletonPackfile());
    const auto none = target.invoke("ValidateBehaviorGraph", nlohmann::json::array({rig.string()}));
    REQUIRE_FALSE(none.has_value());
    CHECK(none.error().type == "HkxRead");
}

TEST_CASE("HkxTarget EditBehaviorGraph previews, writes only with an out path, and a refusal writes nothing") {
    // Constraint: detects a save that writes on preview, replaces a file without overwrite, leaves a file behind after a refused edit, or changes the source.
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    HkxService service;
    HkxTarget target(service);

    const auto fileBytes = [](const std::filesystem::path& path) {
        std::ifstream stream(path, std::ios::binary);
        return std::vector<char>((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    };
    const auto source = cleanup.root / "source.hkx";
    writeBytes(source, behavior_graph_packfile());
    const auto original = fileBytes(source);
    const auto add = nlohmann::json::parse(
        R"([{"kind":"add_transition","state_machine":0,"state":1,"index":0,"transition":{"event_id":2,"to_state_id":7}}])");

    const auto preview = target.invoke("EditBehaviorGraph", nlohmann::json::array({source.string(), add}));
    REQUIRE(preview.has_value());
    CHECK((*preview)["ok"].get<bool>());
    CHECK_FALSE((*preview)["written"].get<bool>());
    CHECK((*preview)["after"]["errors"].get<std::size_t>() == 0u);
    CHECK(fileBytes(source) == original);

    const auto asText = target.invoke("EditBehaviorGraph", nlohmann::json::array({source.string(), add.dump()}));
    REQUIRE(asText.has_value());
    CHECK((*asText)["applied"].get<std::size_t>() == 1u);
    const auto notJson = target.invoke("EditBehaviorGraph", nlohmann::json::array({source.string(), "[{"}));
    REQUIRE_FALSE(notJson.has_value());
    CHECK(notJson.error().type == "InvalidEdits");

    const auto out = cleanup.root / "out.hkx";
    const auto saved = target.invoke("EditBehaviorGraph", nlohmann::json::array({source.string(), add, out.string(), false}));
    REQUIRE(saved.has_value());
    CHECK((*saved)["written"].get<bool>());
    REQUIRE(std::filesystem::exists(out));
    const auto reopened = target.invoke("ReadBehaviorAuthoring", nlohmann::json::array({out.string()}));
    REQUIRE(reopened.has_value());
    CHECK((*reopened)["transitions"].size() == 3u);
    CHECK((*reopened)["states"][1]["transition_count"].get<std::size_t>() == 1u);
    CHECK((*reopened)["transitions"][0]["condition"].get<std::string>() == "x > 1");

    const auto again = target.invoke("EditBehaviorGraph", nlohmann::json::array({source.string(), add, out.string(), false}));
    REQUIRE_FALSE(again.has_value());
    CHECK(again.error().type == "OutputExists");
    const auto replaced = target.invoke("EditBehaviorGraph", nlohmann::json::array({source.string(), add, out.string(), true}));
    CHECK(replaced.has_value());

    const auto refusedOut = cleanup.root / "refused.hkx";
    const auto dangling = nlohmann::json::parse(
        R"([{"kind":"add_transition","state_machine":0,"state":1,"index":0,"transition":{"event_id":99,"to_state_id":7}}])");
    const auto removeEvent = nlohmann::json::parse(R"([{"kind":"remove_event","index":0}])");
    for (const auto& edits : {dangling, removeEvent}) {
        const auto refused = target.invoke("EditBehaviorGraph", nlohmann::json::array({source.string(), edits, refusedOut.string(), true}));
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().type == "BehaviorEditRefused");
        CHECK(refused.error().message.find("edit 0") != std::string::npos);
        CHECK_FALSE(std::filesystem::exists(refusedOut));
    }

    for (const auto* text : {R"([{"kind":"add_transition"}])", R"([{"kind":"nope","index":0}])", R"([])", R"("x")",
                             R"([{"kind":"set_transition","state_machine":0,"state":0,"index":0,"transition":{"event_id":1,"to_state_id":9}}])"}) {
        const auto parsed = nlohmann::json::parse(text);
        const auto bad = target.invoke("EditBehaviorGraph", nlohmann::json::array({source.string(), parsed, refusedOut.string(), true}));
        REQUIRE_FALSE(bad.has_value());
        CHECK(bad.error().type == "InvalidEdits");
        CHECK_FALSE(std::filesystem::exists(refusedOut));
    }
    CHECK(fileBytes(source) == original);

    const auto notHkx = cleanup.root / "out.txt";
    const auto wrongName = target.invoke("EditBehaviorGraph", nlohmann::json::array({source.string(), add, notHkx.string(), true}));
    REQUIRE_FALSE(wrongName.has_value());
    CHECK(wrongName.error().type == "InvalidPath");
    CHECK_FALSE(std::filesystem::exists(notHkx));
    const auto missingFolder = cleanup.root / "nowhere" / "out.hkx";
    const auto noFolder = target.invoke("EditBehaviorGraph", nlohmann::json::array({source.string(), add, missingFolder.string(), true}));
    REQUIRE_FALSE(noFolder.has_value());
    CHECK(noFolder.error().type == "OutputFolderMissing");
    CHECK_FALSE(std::filesystem::exists(missingFolder.parent_path()));
    CHECK(fileBytes(source) == original);
}

TEST_CASE("HkxTarget EditBehaviorGraph saves a clip, variable and state edit and ReadBehaviorAuthoring shows the generator view") {
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    HkxService service;
    HkxTarget target(service);

    const auto source = cleanup.root / "source.hkx";
    writeBytes(source, behavior_graph_packfile());
    const auto before = target.invoke("ReadBehaviorAuthoring", nlohmann::json::array({source.string()}));
    REQUIRE(before.has_value());
    CHECK((*before)["clips"].size() == 3u);
    CHECK((*before)["clips"][0]["animation"].get<std::string>() == "Animations\\A.hkt");
    CHECK((*before)["blenders"].size() == 3u);
    CHECK((*before)["blend_children"].size() > 0u);
    CHECK((*before)["bindings"].size() > 0u);
    CHECK((*before)["states"][0]["enable"].get<bool>());
    const auto clipNode = (*before)["clips"][2]["node"].get<int>();

    const auto edits = nlohmann::json::parse(R"([
        {"kind":"set_clip_animation","index":1,"name":"Animations\\Other.hkt"},
        {"kind":"add_variable","name":"iCount","flags":3},
        {"kind":"rename_variable","index":0,"name":"bActive"},
        {"kind":"add_state","state_machine":0,"name":"Walk","value":11,"node":0},
        {"kind":"set_start_state","state_machine":0,"value":9}])");
    auto withNode = edits;
    withNode[3]["node"] = clipNode;
    const auto out = cleanup.root / "out.hkx";
    const auto saved = target.invoke("EditBehaviorGraph", nlohmann::json::array({source.string(), withNode, out.string(), false}));
    REQUIRE(saved.has_value());
    CHECK((*saved)["applied"].get<std::size_t>() == 5u);
    CHECK((*saved)["after"]["errors"].get<std::size_t>() == 0u);
    const auto after = target.invoke("ReadBehaviorAuthoring", nlohmann::json::array({out.string()}));
    REQUIRE(after.has_value());
    CHECK((*after)["clips"][1]["animation"].get<std::string>() == "Animations\\Other.hkt");
    REQUIRE((*after)["variables"].size() == 4u);
    CHECK((*after)["variables"][3]["name"].get<std::string>() == "iCount");
    CHECK((*after)["variables"][0]["name"].get<std::string>() == "bActive");
    REQUIRE((*after)["states"].size() == 3u);
    CHECK((*after)["states"][2]["name"].get<std::string>() == "Walk");
    CHECK((*after)["state_machines"][0]["start_state_id"].get<int>() == 9);

    for (const auto* text : {R"([{"kind":"add_state","state_machine":0,"name":"S","value":11}])",
                             R"([{"kind":"add_variable","name":"x"}])",
                             R"([{"kind":"set_start_state","state_machine":0}])",
                             R"([{"kind":"remove_state","state_machine":0}])"}) {
        const auto bad = target.invoke("EditBehaviorGraph", nlohmann::json::array({source.string(), nlohmann::json::parse(text)}));
        REQUIRE_FALSE(bad.has_value());
        CHECK(bad.error().type == "InvalidEdits");
    }
    const auto vector = nlohmann::json::parse(R"([{"kind":"add_variable","name":"vX","flags":6}])");
    const auto refused = target.invoke("EditBehaviorGraph", nlohmann::json::array({source.string(), vector}));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "BehaviorEditRefused");

    // Constraint, regression caught: set_generator_field must reach the host, change only the addressed clip or blend child in the saved file, and report a malformed request as InvalidEdits and an out-of-range mode as BehaviorEditRefused, not as a silent write of 0.
    CHECK((*before)["clips"][0]["playback_speed"].get<float>() == 1.5f);
    CHECK((*before)["blenders"][0].contains("min_cyclic_blend_parameter"));
    CHECK((*before)["blend_children"][1]["weight"].get<float>() == 3.0f);
    const auto fieldEdits = nlohmann::json::parse(R"([
        {"kind":"set_generator_field","flags":5,"index":0,"name":"playbackSpeed","real":2.5},
        {"kind":"set_generator_field","flags":5,"index":2,"name":"mode","value":3},
        {"kind":"set_generator_field","flags":4,"index":1,"name":"weight","real":2.0}])");
    const auto fieldOut = cleanup.root / "fields.hkx";
    const auto fieldSaved = target.invoke("EditBehaviorGraph", nlohmann::json::array({source.string(), fieldEdits, fieldOut.string(), false}));
    REQUIRE(fieldSaved.has_value());
    CHECK((*fieldSaved)["applied"].get<std::size_t>() == 3u);
    const auto fieldAfter = target.invoke("ReadBehaviorAuthoring", nlohmann::json::array({fieldOut.string()}));
    REQUIRE(fieldAfter.has_value());
    CHECK((*fieldAfter)["clips"][0]["playback_speed"].get<float>() == 2.5f);
    CHECK((*fieldAfter)["clips"][1]["playback_speed"].get<float>() == 1.0f);
    CHECK((*fieldAfter)["clips"][2]["mode"].get<int>() == 3);
    CHECK((*fieldAfter)["blend_children"][1]["weight"].get<float>() == 2.0f);
    CHECK((*fieldAfter)["blend_children"][0]["weight"].get<float>() == 1.0f);

    for (const auto* text : {R"([{"kind":"set_generator_field","flags":5,"index":0,"name":"mode"}])",
                             R"([{"kind":"set_generator_field","index":0,"name":"mode","value":1}])",
                             R"([{"kind":"set_generator_field","flags":5,"index":0,"name":"playbackSpeed","real":"fast"}])",
                             R"([{"kind":"set_generator_field","flags":5,"index":0,"value":1}])"}) {
        const auto bad = target.invoke("EditBehaviorGraph", nlohmann::json::array({source.string(), nlohmann::json::parse(text)}));
        REQUIRE_FALSE(bad.has_value());
        CHECK(bad.error().type == "InvalidEdits");
    }
    const auto badMode = nlohmann::json::parse(R"([{"kind":"set_generator_field","flags":5,"index":0,"name":"mode","value":4}])");
    const auto modeRefused = target.invoke("EditBehaviorGraph", nlohmann::json::array({source.string(), badMode}));
    REQUIRE_FALSE(modeRefused.has_value());
    CHECK(modeRefused.error().type == "BehaviorEditRefused");
}

TEST_CASE("HkxTarget EditBehaviorGraph saves blend child edits and refuses a malformed or unsafe child edit") {
    // Constraint: detects an RPC that drops the blender index, node or weight of a blend child edit, or maps remove_blend_child to another row, which would add the child to the wrong blender or remove the wrong one; a missing required field must be InvalidEdits and an engine-unsafe removal BehaviorEditRefused.
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    HkxService service;
    HkxTarget target(service);

    const auto source = cleanup.root / "source.hkx";
    writeBytes(source, behavior_graph_packfile());
    const auto before = target.invoke("ReadBehaviorAuthoring", nlohmann::json::array({source.string()}));
    REQUIRE(before.has_value());
    const auto clipNode = (*before)["clips"][2]["node"].get<int>();
    const auto childRows = (*before)["blend_children"].size();
    const auto firstOfThird = (*before)["blenders"][2]["first_child"].get<std::size_t>();

    const auto edits = nlohmann::json::parse(R"([
        {"kind":"add_blend_child","index":1,"node":0,"real":0.5},
        {"kind":"remove_blend_child","index":0}])");
    auto withNode = edits;
    withNode[0]["node"] = clipNode;
    withNode[1]["index"] = firstOfThird + 2;
    const auto out = cleanup.root / "children.hkx";
    const auto saved = target.invoke("EditBehaviorGraph", nlohmann::json::array({source.string(), withNode, out.string(), false}));
    REQUIRE(saved.has_value());
    CHECK((*saved)["applied"].get<std::size_t>() == 2u);
    const auto after = target.invoke("ReadBehaviorAuthoring", nlohmann::json::array({out.string()}));
    REQUIRE(after.has_value());
    CHECK((*after)["blend_children"].size() == childRows);
    CHECK((*after)["blenders"][1]["child_count"].get<int>() == 4);
    CHECK((*after)["blenders"][2]["child_count"].get<int>() == 1);

    for (const auto* text : {R"([{"kind":"add_blend_child","index":0,"node":1}])",
                             R"([{"kind":"add_blend_child","index":0,"real":0.5}])",
                             R"([{"kind":"add_blend_child","node":1,"real":0.5}])",
                             R"([{"kind":"remove_blend_child"}])",
                             R"([{"kind":"add_selector_child","node":1}])",
                             R"([{"kind":"remove_selector_child"}])",
                             R"([{"kind":"remove_subtree"}])"}) {
        const auto bad = target.invoke("EditBehaviorGraph", nlohmann::json::array({source.string(), nlohmann::json::parse(text)}));
        REQUIRE_FALSE(bad.has_value());
        CHECK(bad.error().type == "InvalidEdits");
    }
    const auto refusedOut = cleanup.root / "refused.hkx";
    for (const auto* text : {R"([{"kind":"remove_blend_child","index":0}])", R"([{"kind":"remove_subtree","node":0}])",
                             R"([{"kind":"add_blend_child","index":0,"node":0,"real":4.0}])"}) {
        const auto refused = target.invoke("EditBehaviorGraph", nlohmann::json::array({source.string(), nlohmann::json::parse(text), refusedOut.string(), true}));
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().type == "BehaviorEditRefused");
        CHECK_FALSE(std::filesystem::exists(refusedOut));
    }
}

TEST_CASE("HkxTarget EditBehaviorGraph saves binding edits addressed by row and by owner") {
    // Constraint: detects an RPC that drops the binding row index, the owner kind or the variable, which would retarget or remove another owner's binding.
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    HkxService service;
    HkxTarget target(service);

    const auto source = cleanup.root / "source.hkx";
    writeBytes(source, behavior_graph_packfile());
    const auto edits = nlohmann::json::parse(R"([
        {"kind":"set_binding","index":1,"value":0},
        {"kind":"add_binding","index":2,"flags":3,"name":"minCyclicBlendParameter","value":1},
        {"kind":"remove_binding","index":0}])");
    const auto out = cleanup.root / "out.hkx";
    const auto saved = target.invoke("EditBehaviorGraph", nlohmann::json::array({source.string(), edits, out.string(), false}));
    REQUIRE(saved.has_value());
    CHECK((*saved)["applied"].get<std::size_t>() == 3u);
    const auto after = target.invoke("ReadBehaviorAuthoring", nlohmann::json::array({out.string()}));
    REQUIRE(after.has_value());
    REQUIRE((*after)["bindings"].size() == 2u);
    CHECK((*after)["bindings"][0]["member_path"].get<std::string>() == "blendParameter");
    CHECK((*after)["bindings"][0]["variable_index"].get<int>() == 0);
    CHECK((*after)["bindings"][1]["member_path"].get<std::string>() == "minCyclicBlendParameter");
    CHECK((*after)["bindings"][1]["variable_index"].get<int>() == 1);
    CHECK((*after)["bindings"][1]["owner_kind"].get<int>() == 3);

    for (const auto* text : {R"([{"kind":"add_binding","index":2,"name":"x","value":1}])",
                             R"([{"kind":"add_binding","index":2,"flags":3,"value":1}])",
                             R"([{"kind":"set_binding","index":1}])",
                             R"([{"kind":"remove_binding"}])"}) {
        const auto bad = target.invoke("EditBehaviorGraph", nlohmann::json::array({source.string(), nlohmann::json::parse(text)}));
        REQUIRE_FALSE(bad.has_value());
        CHECK(bad.error().type == "InvalidEdits");
    }
    const auto noSet = nlohmann::json::parse(R"([{"kind":"add_binding","index":0,"flags":5,"name":"x","value":0}])");
    const auto refused = target.invoke("EditBehaviorGraph", nlohmann::json::array({source.string(), noSet}));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "BehaviorEditRefused");
}

TEST_CASE("HkxTarget ReadRagdoll returns bodies, shapes, constraints, bindings and the load report") {
    // Constraint: detects a ragdoll view that drops a constraint's limits, a body's shape link or the skeleton pair.
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    HkxService service;
    HkxTarget target(service);

    const auto file = cleanup.root / "ragdoll.hkx";
    writeBytes(file, physics_packfile(default_physics_spec()));
    const auto reply = target.invoke("ReadRagdoll", nlohmann::json::array({file.string()}));
    REQUIRE(reply.has_value());
    CHECK((*reply)["loaded"].get<bool>());
    CHECK((*reply)["status"].get<std::string>() == "ok");
    CHECK((*reply)["body_total"].get<std::size_t>() == 2u);
    CHECK((*reply)["shape_total"].get<std::size_t>() == 2u);
    CHECK((*reply)["constraint_total"].get<std::size_t>() == 1u);
    CHECK((*reply)["mapping_total"].get<std::size_t>() == 2u);
    CHECK((*reply)["usable_bodies"].get<int>() == 2);
    CHECK((*reply)["file_version"].get<int>() == 11);
    CHECK((*reply)["pointer_size"].get<int>() == 8);

    REQUIRE((*reply)["bodies"].size() == 2u);
    CHECK((*reply)["bodies"][0]["name"].get<std::string>() == "Root Body");
    CHECK((*reply)["bodies"][0]["shape_id"].get<int>() == 1);
    CHECK((*reply)["bodies"][0]["position"][2].get<float>() == 3.0f);
    CHECK((*reply)["bodies"][0]["center_of_mass"][0].get<float>() == 20.0f);
    CHECK((*reply)["bodies"][1]["position"][0].get<float>() == 4.0f);

    REQUIRE((*reply)["shapes"].size() == 2u);
    CHECK((*reply)["shapes"][0]["kind"].get<std::string>() == "capsule");
    CHECK((*reply)["shapes"][0]["vertex_count"].get<std::size_t>() == 8u);
    CHECK((*reply)["shapes"][0]["face_count"].get<std::size_t>() == 2u);

    REQUIRE((*reply)["constraints"].size() == 1u);
    const auto& constraint = (*reply)["constraints"][0];
    CHECK(constraint["kind"].get<std::string>() == "ragdoll");
    CHECK(constraint["body_a"].get<int>() == 1);
    CHECK(constraint["body_b"].get<int>() == 0);
    CHECK(constraint["twist_min_angle"].get<float>() == -0.5f);
    CHECK(constraint["twist_max_angle"].get<float>() == 0.75f);
    CHECK(constraint["cone_max_angle"].get<float>() == 0.625f);

    REQUIRE((*reply)["bone_bindings"].size() == 2u);
    CHECK((*reply)["bone_bindings"][1]["bone_index"].get<int>() == 1);
    CHECK((*reply)["bone_bindings"][1]["body_id"].get<int>() == 1);
    CHECK((*reply)["skeleton"]["name"].get<std::string>() == "Physics");
    CHECK((*reply)["skeleton"]["bone_count"].get<std::size_t>() == 2u);
    CHECK((*reply)["animation_skeleton"]["name"].get<std::string>() == "Animation");
    CHECK((*reply)["animation_skeleton"]["bone_count"].get<std::size_t>() == 3u);
    for (const auto& finding : (*reply)["validation"]) CHECK(finding["level"].get<std::string>() != "error");

    CHECK((*reply)["capsules"].is_array());
    CHECK((*reply)["materials"].is_array());
    for (const auto& capsule : (*reply)["capsules"]) {
        CHECK_FALSE(capsule["editable"].get<bool>());
        CHECK_FALSE(capsule["reason"].get<std::string>().empty());
    }

    const auto cut = target.invoke("ReadRagdoll", nlohmann::json::array({file.string(), 1}));
    REQUIRE(cut.has_value());
    CHECK((*cut)["bodies"].size() == 1u);
    CHECK((*cut)["body_total"].get<std::size_t>() == 2u);
    CHECK((*cut)["capsules"].size() == 1u);
    CHECK((*cut)["capsule_total"].get<std::size_t>() == (*reply)["capsules"].size());
    CHECK((*cut)["material_total"].get<std::size_t>() == (*reply)["materials"].size());
}

TEST_CASE("HkxTarget ReadRagdoll answers a file with no ragdoll with a verdict and refuses a missing file") {
    // Constraint: detects a file that holds no usable ragdoll turned into an error or an empty loaded reply instead of the classifier verdict.
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    HkxService service;
    HkxTarget target(service);

    const auto rig = cleanup.root / "rig.hkx";
    writeBytes(rig, skeletonPackfile());
    const auto none = target.invoke("ReadRagdoll", nlohmann::json::array({rig.string()}));
    REQUIRE(none.has_value());
    CHECK_FALSE((*none)["loaded"].get<bool>());
    CHECK((*none)["read_status"].get<std::string>() == "MissingObject");
    CHECK((*none)["status"].get<std::string>() != "ok");
    CHECK_FALSE((*none)["headline"].get<std::string>().empty());
    CHECK_FALSE((*none).contains("bodies"));

    const auto tiny = cleanup.root / "tiny.hkx";
    writeBytes(tiny, std::vector<std::byte>(8, std::byte{1}));
    const auto wrong = target.invoke("ReadRagdoll", nlohmann::json::array({tiny.string()}));
    REQUIRE(wrong.has_value());
    CHECK_FALSE((*wrong)["loaded"].get<bool>());
    CHECK((*wrong)["status"].get<std::string>() == "wrong-container");

    const auto missing = target.invoke("ReadRagdoll", nlohmann::json::array({(cleanup.root / "nope.hkx").string()}));
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().type == "FileNotFound");
}

namespace {

std::vector<std::byte> readAll(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    const std::vector<char> raw((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    std::vector<std::byte> bytes(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i) bytes[i] = static_cast<std::byte>(raw[i]);
    return bytes;
}

}  // namespace

TEST_CASE("HkxTarget SaveRagdoll writes the edited file and the reopened ragdoll shows only the edit") {
    // Constraint: detects a save that loses an edited pose or limit, or that changes a body or constraint nobody edited.
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    HkxService service;
    HkxTarget target(service);

    const auto source = cleanup.root / "ragdoll.hkx";
    const auto out = cleanup.root / "edited.hkx";
    const auto original = physics_packfile(default_physics_spec());
    writeBytes(source, original);

    const nlohmann::json edits = {
        {"bodies", nlohmann::json::array({{{"body", 0}, {"position", {1.0, 2.0, 5.0}}}})},
        {"constraints", nlohmann::json::array({{{"constraint", 0}, {"twist_min", -0.25}}})}};
    const auto reply = target.invoke("SaveRagdoll", nlohmann::json::array({source.string(), out.string(), edits}));
    REQUIRE(reply.has_value());
    CHECK((*reply)["saved"].get<bool>());
    CHECK((*reply)["bytes"].get<std::size_t>() == original.size());
    CHECK((*reply)["sha256"].get<std::string>().size() == 64u);
    CHECK(readAll(source) == original);

    const auto reopened = target.invoke("ReadRagdoll", nlohmann::json::array({out.string()}));
    REQUIRE(reopened.has_value());
    CHECK((*reopened)["bodies"][0]["position"][0].get<float>() == 1.0f);
    CHECK((*reopened)["bodies"][0]["position"][2].get<float>() == 5.0f);
    CHECK((*reopened)["bodies"][1]["position"][0].get<float>() == 4.0f);
    CHECK((*reopened)["constraints"][0]["twist_min_angle"].get<float>() == -0.25f);
    CHECK((*reopened)["constraints"][0]["twist_max_angle"].get<float>() == 0.75f);
    CHECK((*reopened)["constraints"][0]["cone_max_angle"].get<float>() == 0.625f);

    const auto again = target.invoke("SaveRagdoll", nlohmann::json::array({source.string(), out.string(), edits}));
    REQUIRE_FALSE(again.has_value());
    CHECK(again.error().type == "OutputExists");

    const auto firstSave = readAll(out);
    const nlohmann::json second = {{"bodies", nlohmann::json::array({{{"body", 0}, {"position", {1.0, 2.0, 6.0}}}})}};
    const auto replaced = target.invoke("SaveRagdoll", nlohmann::json::array({source.string(), out.string(), second, true}));
    REQUIRE(replaced.has_value());
    CHECK(readAll(cleanup.root / "edited.hkx.bak") == firstSave);
    CHECK(readAll(out) != firstSave);
    const auto rewritten = target.invoke("ReadRagdoll", nlohmann::json::array({out.string()}));
    REQUIRE(rewritten.has_value());
    CHECK((*rewritten)["bodies"][0]["position"][2].get<float>() == 6.0f);
}

TEST_CASE("HkxTarget SaveRagdoll refuses without touching the source or the destination") {
    // Constraint: detects a refused save that still writes, truncates or replaces a file, or that is accepted on a stale source digest.
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    HkxService service;
    HkxTarget target(service);

    const auto source = cleanup.root / "ragdoll.hkx";
    const auto out = cleanup.root / "edited.hkx";
    const auto original = physics_packfile(default_physics_spec());
    writeBytes(source, original);
    const nlohmann::json good = {{"bodies", nlohmann::json::array({{{"body", 0}, {"position", {1.0, 2.0, 5.0}}}})}};

    const auto sameFile = target.invoke("SaveRagdoll", nlohmann::json::array({source.string(), source.string(), good, true}));
    REQUIRE_FALSE(sameFile.has_value());
    CHECK(sameFile.error().type == "RagdollRefused");
    CHECK(readAll(source) == original);

    const auto notJson = target.invoke("SaveRagdoll", nlohmann::json::array({source.string(), out.string(), "{not json"}));
    REQUIRE_FALSE(notJson.has_value());
    CHECK(notJson.error().type == "InvalidEdits");

    const auto badRow = target.invoke("SaveRagdoll", nlohmann::json::array({source.string(), out.string(),
        nlohmann::json{{"bodies", nlohmann::json::array({{{"body", 0}, {"position", {1.0, 2.0}}}})}}}));
    REQUIRE_FALSE(badRow.has_value());
    CHECK(badRow.error().type == "InvalidEdits");

    const auto stale = target.invoke("SaveRagdoll", nlohmann::json::array({source.string(), out.string(), good, false, std::string(64, '0')}));
    REQUIRE_FALSE(stale.has_value());
    CHECK(stale.error().type == "SourceChanged");

    const auto outOfRange = target.invoke("SaveRagdoll", nlohmann::json::array({source.string(), out.string(),
        nlohmann::json{{"bodies", nlohmann::json::array({{{"body", 99}, {"position", {1.0, 2.0, 5.0}}}})}}}));
    REQUIRE_FALSE(outOfRange.has_value());
    CHECK(outOfRange.error().type == "RagdollRefused");

    const auto shape = target.invoke("SaveRagdoll", nlohmann::json::array({source.string(), out.string(),
        nlohmann::json{{"shapes", nlohmann::json::array({{{"shape", 1}, {"radius", 0.5}}})}}}));
    REQUIRE_FALSE(shape.has_value());
    CHECK(shape.error().type == "RagdollRefused");
    CHECK(shape.error().message.find("shape_geometry_unsupported") != std::string::npos);

    const auto material = target.invoke("SaveRagdoll", nlohmann::json::array({source.string(), out.string(),
        nlohmann::json{{"materials", nlohmann::json::array({{{"material", 0}, {"restitution", 0.25}}})}}}));
    REQUIRE_FALSE(material.has_value());
    CHECK(material.error().type == "RagdollRefused");
    CHECK(material.error().message.find("bad_target") != std::string::npos);

    const auto badShared = target.invoke("SaveRagdoll", nlohmann::json::array({source.string(), out.string(),
        nlohmann::json{{"materials", nlohmann::json::array({{{"material", 0}, {"restitution", 0.25}, {"allow_shared", "yes"}}})}}}));
    REQUIRE_FALSE(badShared.has_value());
    CHECK(badShared.error().type == "InvalidEdits");

    const auto wrongExtension = target.invoke("SaveRagdoll", nlohmann::json::array({source.string(), (cleanup.root / "edited.txt").string(), good}));
    REQUIRE_FALSE(wrongExtension.has_value());
    CHECK(wrongExtension.error().type == "InvalidPath");
    CHECK_FALSE(std::filesystem::exists(cleanup.root / "edited.txt"));

    const auto missingFolder = target.invoke("SaveRagdoll", nlohmann::json::array({source.string(), (cleanup.root / "nope" / "edited.hkx").string(), good}));
    REQUIRE_FALSE(missingFolder.has_value());
    CHECK(missingFolder.error().type == "OutputFolderMissing");
    CHECK_FALSE(std::filesystem::exists(cleanup.root / "nope"));

    std::filesystem::create_directories(cleanup.root / "folder.hkx");
    const auto directoryTarget = target.invoke("SaveRagdoll", nlohmann::json::array({source.string(), (cleanup.root / "folder.hkx").string(), good, true}));
    REQUIRE_FALSE(directoryTarget.has_value());
    CHECK(directoryTarget.error().type == "InvalidPath");

    const auto hugeId = target.invoke("SaveRagdoll", nlohmann::json::array({source.string(), out.string(),
        nlohmann::json{{"bodies", nlohmann::json::array({{{"body", 4294967296LL}, {"position", {1.0, 2.0, 5.0}}}})}}}));
    REQUIRE_FALSE(hugeId.has_value());
    CHECK(hugeId.error().type == "InvalidEdits");
    const auto hugeUnsigned = target.invoke("SaveRagdoll", nlohmann::json::array({source.string(), out.string(),
        nlohmann::json{{"bodies", nlohmann::json::array({{{"body", 18446744073709551615ULL}, {"position", {1.0, 2.0, 5.0}}}})}}}));
    REQUIRE_FALSE(hugeUnsigned.has_value());
    CHECK(hugeUnsigned.error().type == "InvalidEdits");

    CHECK_FALSE(std::filesystem::exists(out));
    CHECK(readAll(source) == original);

    writeBytes(out, std::vector<std::byte>(4, std::byte{7}));
    const auto refused = target.invoke("SaveRagdoll", nlohmann::json::array({source.string(), out.string(),
        nlohmann::json{{"shapes", nlohmann::json::array({{{"shape", 1}, {"radius", 0.5}}})}}, true}));
    REQUIRE_FALSE(refused.has_value());
    CHECK(readAll(out) == std::vector<std::byte>(4, std::byte{7}));
    CHECK_FALSE(std::filesystem::exists(cleanup.root / "edited.hkx.bak"));

    std::filesystem::create_directories(cleanup.root / "edited.hkx.bak");
    const auto noBackup = target.invoke("SaveRagdoll", nlohmann::json::array({source.string(), out.string(), good, true}));
    REQUIRE_FALSE(noBackup.has_value());
    CHECK(noBackup.error().type == "BackupFailed");
    CHECK(readAll(out) == std::vector<std::byte>(4, std::byte{7}));
}

TEST_CASE("HkxTarget SaveRagdoll resizes a vanilla capsule and edits a material and the reopened file shows only those") {
    // Constraint: detects an RPC save that drops the shapes or materials rows, writes them to the wrong capsule or material, or reports saved without the file holding the edit; the committed vanilla skeleton is needed because synthetic capsules are never editable.
    const std::filesystem::path samples{FO4IDE_HKX_SAMPLES_DIR};
    const auto skeleton = samples.parent_path() / "Meshes" / "Actors" / "Character" / "CharacterAssets" / "skeleton.hkx";
    if (!std::filesystem::exists(skeleton)) {
        if (std::getenv("CI") != nullptr) FAIL("CI is missing the committed vanilla skeleton fixture");
        SKIP("no committed vanilla skeleton fixture");
    }
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    HkxService service;
    HkxTarget target(service);

    const auto source = cleanup.root / "skeleton.hkx";
    const auto out = cleanup.root / "edited.hkx";
    const auto original = fileBytes(skeleton);
    writeBytes(source, original);

    const auto before = target.invoke("ReadRagdoll", nlohmann::json::array({source.string()}));
    REQUIRE(before.has_value());
    REQUIRE((*before)["capsules"].size() == 19u);
    REQUIRE((*before)["materials"].size() == 18u);
    const auto capsule = (*before)["capsules"][1];
    REQUIRE(capsule["editable"].get<bool>());
    const auto a = capsule["a"].get<std::array<float, 3>>();
    const auto b = capsule["b"].get<std::array<float, 3>>();
    std::array<float, 3> newA{};
    std::array<float, 3> newB{};
    for (std::size_t i = 0; i < 3; ++i) {
        const double centre = (static_cast<double>(a[i]) + b[i]) / 2.0;
        newA[i] = static_cast<float>(centre - 0.4 * (static_cast<double>(b[i]) - a[i]));
        newB[i] = static_cast<float>(centre + 0.4 * (static_cast<double>(b[i]) - a[i]));
    }
    const float newRadius = capsule["radius"].get<float>() * 1.25f;

    const nlohmann::json edits = {
        {"shapes", nlohmann::json::array({{{"shape", capsule["shape"]}, {"a", newA}, {"b", newB}, {"radius", newRadius}}})},
        {"materials", nlohmann::json::array({{{"material", 3}, {"restitution", 0.25}, {"dynamic_friction", 0.5}}})}};
    const auto reply = target.invoke("SaveRagdoll", nlohmann::json::array({source.string(), out.string(), edits}));
    REQUIRE(reply.has_value());
    CHECK((*reply)["saved"].get<bool>());
    CHECK((*reply)["bytes"].get<std::size_t>() == original.size());
    CHECK(readAll(source) == original);
    CHECK(readAll(out) != original);

    const auto after = target.invoke("ReadRagdoll", nlohmann::json::array({out.string()}));
    REQUIRE(after.has_value());
    CHECK((*after)["status"].get<std::string>() == "ok");
    REQUIRE((*after)["capsules"].size() == 19u);
    CHECK((*after)["capsules"][1]["editable"].get<bool>());
    CHECK((*after)["capsules"][1]["a"].get<std::array<float, 3>>() == newA);
    CHECK((*after)["capsules"][1]["b"].get<std::array<float, 3>>() == newB);
    CHECK((*after)["capsules"][1]["radius"].get<float>() == newRadius);
    for (std::size_t i = 0; i < 19u; ++i) {
        if (i != 1u) CHECK((*after)["capsules"][i].dump() == (*before)["capsules"][i].dump());
    }
    REQUIRE((*after)["materials"].size() == 18u);
    CHECK((*after)["materials"][3]["restitution"].get<float>() == 0.25f);
    CHECK((*after)["materials"][3]["dynamic_friction"].get<float>() == 0.5f);
    CHECK((*after)["materials"][3]["static_friction"].dump() == (*before)["materials"][3]["static_friction"].dump());
    for (std::size_t i = 0; i < 18u; ++i) {
        if (i != 3u) CHECK((*after)["materials"][i].dump() == (*before)["materials"][i].dump());
    }
    CHECK((*after)["bodies"].dump() == (*before)["bodies"].dump());
    CHECK((*after)["constraints"].dump() == (*before)["constraints"].dump());

    const auto refusedOut = cleanup.root / "turned.hkx";
    auto turned = b;
    turned[1] += 5.0f;
    const nlohmann::json turnEdit = {
        {"shapes", nlohmann::json::array({{{"shape", capsule["shape"]}, {"b", turned}}})}};
    const auto refused = target.invoke("SaveRagdoll", nlohmann::json::array({source.string(), refusedOut.string(), turnEdit}));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "RagdollRefused");
    CHECK(refused.error().message.find("shape_geometry_unsupported") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(refusedOut));
}

TEST_CASE("HkxTarget ragdoll plane limit, friction and angle mode round trip and a mode refusal names its reason") {
    // Constraint: detects a plane or friction edit lost between the panel rows and the file, a read that hides the stored angle measurement mode, or a cone edit on a foreign-mode atom that is saved instead of refused with its reason.
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    HkxService service;
    HkxTarget target(service);

    PhysicsSpec spec = default_physics_spec();
    spec.constraints[0].plane_min = -0.3f;
    spec.constraints[0].plane_max = 0.3f;
    spec.constraints[0].friction = 1.0f;
    const auto source = cleanup.root / "ragdoll.hkx";
    const auto out = cleanup.root / "edited.hkx";
    const auto original = physics_packfile(spec);
    writeBytes(source, original);

    const auto before = target.invoke("ReadRagdoll", nlohmann::json::array({source.string()}));
    REQUIRE(before.has_value());
    const auto& row = (*before)["constraints"][0];
    CHECK(row["plane_min_angle"].get<float>() == -0.3f);
    CHECK(row["plane_max_angle"].get<float>() == 0.3f);
    CHECK(row["friction"].get<float>() == 1.0f);
    CHECK(row["cone_angle_mode"].get<int>() == 0);
    CHECK(row["plane_angle_mode"].get<int>() == 1);

    const nlohmann::json edits = {{"constraints", nlohmann::json::array({{{"constraint", 0}, {"plane_min", -0.1}, {"plane_max", 0.1}, {"friction", 2.5}}})}};
    const auto saved = target.invoke("SaveRagdoll", nlohmann::json::array({source.string(), out.string(), edits}));
    REQUIRE(saved.has_value());
    CHECK((*saved)["saved"].get<bool>());
    CHECK(readAll(source) == original);

    const auto after = target.invoke("ReadRagdoll", nlohmann::json::array({out.string()}));
    REQUIRE(after.has_value());
    const auto& edited = (*after)["constraints"][0];
    CHECK(edited["plane_min_angle"].get<float>() == -0.1f);
    CHECK(edited["plane_max_angle"].get<float>() == 0.1f);
    CHECK(edited["friction"].get<float>() == 2.5f);
    CHECK(edited["twist_min_angle"].get<float>() == -0.5f);
    CHECK(edited["cone_max_angle"].get<float>() == 0.625f);

    const nlohmann::json negative = {{"constraints", nlohmann::json::array({{{"constraint", 0}, {"friction", -1.0}}})}};
    const auto badFriction = target.invoke("SaveRagdoll", nlohmann::json::array({source.string(), (cleanup.root / "bad.hkx").string(), negative}));
    REQUIRE_FALSE(badFriction.has_value());
    CHECK(badFriction.error().type == "RagdollRefused");
    CHECK(badFriction.error().message.find("nothing was written") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(cleanup.root / "bad.hkx"));

    PhysicsSpec foreign = spec;
    foreign.constraints[0].cone_mode = 3;
    const auto foreignSource = cleanup.root / "foreign.hkx";
    writeBytes(foreignSource, physics_packfile(foreign));
    const auto foreignRead = target.invoke("ReadRagdoll", nlohmann::json::array({foreignSource.string()}));
    REQUIRE(foreignRead.has_value());
    CHECK((*foreignRead)["constraints"][0]["cone_angle_mode"].get<int>() == 3);

    const nlohmann::json cone = {{"constraints", nlohmann::json::array({{{"constraint", 0}, {"cone_max", 0.5}}})}};
    const auto refused = target.invoke("SaveRagdoll", nlohmann::json::array({foreignSource.string(), (cleanup.root / "cone.hkx").string(), cone}));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "RagdollRefused");
    CHECK(refused.error().message.find("angle_mode_unsupported") != std::string::npos);
    CHECK(refused.error().message.find("nothing was written") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(cleanup.root / "cone.hkx"));

    const nlohmann::json twist = {{"constraints", nlohmann::json::array({{{"constraint", 0}, {"twist_min", -0.25}}})}};
    const auto allowed = target.invoke("SaveRagdoll", nlohmann::json::array({foreignSource.string(), (cleanup.root / "twist.hkx").string(), twist}));
    REQUIRE(allowed.has_value());
}

namespace {

PhysicsSpec previewSpec() {
    PhysicsSpec spec = default_physics_spec();
    spec.inverse_masses = {0.5f, 0.25f};
    spec.inverse_inertias = {{1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}};
    for (auto& shape : spec.shapes) {
        shape.a = {-0.4f, 0.0f, 0.0f};
        shape.b = {0.4f, 0.0f, 0.0f};
        shape.radius = 0.1f;
    }
    auto& joint = spec.constraints[0];
    joint.frame_a = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    joint.frame_b = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 3, 3, 3, 1};
    joint.twist_axis = 0;
    joint.twist_ref_axis = 1;
    joint.cone_twist_axis = 0;
    joint.cone_ref_axis = 0;
    return spec;
}

}  // namespace

TEST_CASE("HkxTarget PreviewRagdoll samples a bounded run, reports measured joints and never writes") {
    // Constraint: detects a preview that writes the file, ignores its step and frame caps, or lets a limit override reach the stored limits.
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    HkxService service;
    HkxTarget target(service);

    const auto file = cleanup.root / "ragdoll.hkx";
    const auto original = physics_packfile(previewSpec());
    writeBytes(file, original);

    const nlohmann::json options = {{"gravity", 0.0}, {"ground", false}, {"steps", 24}, {"frames", 4}};
    const auto reply = target.invoke("PreviewRagdoll", nlohmann::json::array({file.string(), options}));
    REQUIRE(reply.has_value());
    CHECK((*reply)["previewed"].get<bool>());
    CHECK_FALSE((*reply)["saved"].get<bool>());
    CHECK((*reply)["steps"].get<int>() == 24);
    REQUIRE((*reply)["frames"].size() == 5u);
    CHECK((*reply)["frames"][0]["step"].get<int>() == 0);
    CHECK((*reply)["frames"][4]["step"].get<int>() == 24);
    REQUIRE((*reply)["bodies"].size() == 2u);
    CHECK((*reply)["bodies"][0]["mass"].get<float>() == 4.0f);
    REQUIRE((*reply)["constraints"].size() == 1u);
    CHECK((*reply)["constraints"][0]["kind"].get<std::string>() == "ragdoll");
    CHECK((*reply)["constraints"][0]["anchor_error"].get<float>() < 0.05f);
    CHECK_FALSE((*reply)["constraints"][0]["overridden"].get<bool>());

    const nlohmann::json tighter = {{"gravity", 0.0}, {"ground", false}, {"steps", 4},
                                    {"limit_overrides", nlohmann::json::array({{{"constraint", 0}, {"twist_max", 0.1}}})}};
    const auto overridden = target.invoke("PreviewRagdoll", nlohmann::json::array({file.string(), tighter}));
    REQUIRE(overridden.has_value());
    CHECK((*overridden)["constraints"][0]["overridden"].get<bool>());
    CHECK((*overridden)["constraints"][0]["twist_max"].get<float>() == 0.1f);

    const auto stored = target.invoke("ReadRagdoll", nlohmann::json::array({file.string()}));
    REQUIRE(stored.has_value());
    CHECK((*stored)["constraints"][0]["twist_max_angle"].get<float>() == 0.75f);
    CHECK(readAll(file) == original);

    const auto text = target.invoke("PreviewRagdoll", nlohmann::json::array({file.string(), "{\"steps\": 2, \"ground\": false}"}));
    REQUIRE(text.has_value());
    CHECK((*text)["steps"].get<int>() == 2);
}

TEST_CASE("HkxTarget PreviewRagdoll refuses unproven input with a named reason") {
    // Constraint: detects a preview that simulates a ragdoll with no mass, accepts unbounded options, or answers a non-ragdoll as if it loaded.
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    HkxService service;
    HkxTarget target(service);

    const auto massless = cleanup.root / "massless.hkx";
    PhysicsSpec noInverseMass = previewSpec();
    noInverseMass.inverse_masses = {0.0f, 0.0f};
    writeBytes(massless, physics_packfile(noInverseMass));
    const auto noMass = target.invoke("PreviewRagdoll", nlohmann::json::array({massless.string(), nlohmann::json::object()}));
    REQUIRE_FALSE(noMass.has_value());
    CHECK(noMass.error().type == "PreviewRefused");
    CHECK(noMass.error().message.find("BadMass") != std::string::npos);

    const auto file = cleanup.root / "ragdoll.hkx";
    writeBytes(file, physics_packfile(previewSpec()));
    const auto tooLong = target.invoke("PreviewRagdoll", nlohmann::json::array({file.string(), nlohmann::json{{"steps", 999999}}}));
    REQUIRE_FALSE(tooLong.has_value());
    CHECK(tooLong.error().type == "InvalidOptions");
    const auto notJson = target.invoke("PreviewRagdoll", nlohmann::json::array({file.string(), "{oops"}));
    REQUIRE_FALSE(notJson.has_value());
    CHECK(notJson.error().type == "InvalidOptions");
    const auto badBody = target.invoke("PreviewRagdoll", nlohmann::json::array({file.string(),
        nlohmann::json{{"impulses", nlohmann::json::array({{{"body", 9}, {"linear", {1.0, 0.0, 0.0}}}})}}}));
    REQUIRE_FALSE(badBody.has_value());
    CHECK(badBody.error().type == "PreviewRefused");
    const auto badLimit = target.invoke("PreviewRagdoll", nlohmann::json::array({file.string(),
        nlohmann::json{{"limit_overrides", nlohmann::json::array({{{"constraint", 0}, {"twist_min", 1.0}, {"twist_max", -1.0}}})}}}));
    REQUIRE_FALSE(badLimit.has_value());
    CHECK(badLimit.error().type == "PreviewRefused");
    const auto wrongPose = target.invoke("PreviewRagdoll", nlohmann::json::array({file.string(),
        nlohmann::json{{"pose", nlohmann::json::array({{{"position", {0.0, 0.0, 0.0}}, {"rotation", {0.0, 0.0, 0.0, 1.0}}}})}}}));
    REQUIRE_FALSE(wrongPose.has_value());
    CHECK(wrongPose.error().type == "PreviewRefused");
    CHECK(wrongPose.error().message.find("1 bones") != std::string::npos);
    const auto hugeBody = target.invoke("PreviewRagdoll", nlohmann::json::array({file.string(),
        nlohmann::json{{"impulses", nlohmann::json::array({{{"body", 4294967296LL}, {"linear", {1.0, 0.0, 0.0}}}})}}}));
    REQUIRE_FALSE(hugeBody.has_value());
    CHECK(hugeBody.error().type == "InvalidOptions");

    const auto rig =cleanup.root / "rig.hkx";
    writeBytes(rig, skeletonPackfile());
    const auto none = target.invoke("PreviewRagdoll", nlohmann::json::array({rig.string(), nlohmann::json::object()}));
    REQUIRE_FALSE(none.has_value());
    CHECK(none.error().type == "NotARagdoll");

    const auto missing = target.invoke("PreviewRagdoll", nlohmann::json::array({(cleanup.root / "nope.hkx").string(), nlohmann::json::object()}));
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().type == "FileNotFound");
}

TEST_CASE("HkxTarget ReadMappers returns both skeletons, rows, chains and the unmapped bones") {
    // Constraint: detects a mapper view that drops the chain rows, the unmapped bones or the bone names a row points at.
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    HkxService service;
    HkxTarget target(service);

    const auto file = cleanup.root / "mapper.hkx";
    writeBytes(file, mapper_packfile(default_mapper_spec()));
    const auto reply = target.invoke("ReadMappers", nlohmann::json::array({file.string()}));
    REQUIRE(reply.has_value());
    CHECK((*reply)["mapper_count"].get<std::size_t>() == 1u);
    CHECK_FALSE((*reply)["recompute"].get<bool>());
    const auto& mapper = (*reply)["mappers"][0];
    CHECK(mapper["skeleton_a"]["name"].get<std::string>() == "RigA");
    CHECK(mapper["skeleton_a"]["bone_count"].get<std::size_t>() == 3u);
    CHECK(mapper["skeleton_b"]["name"].get<std::string>() == "RigB");
    CHECK(mapper["mapping_type"].get<int>() == 1);
    CHECK(mapper["keep_unmapped_local"].get<bool>());
    REQUIRE(mapper["unmapped_bones"].size() == 1u);
    CHECK(mapper["unmapped_bones"][0].get<int>() == 2);
    CHECK(mapper["extracted_motion"]["translation"][0].get<float>() == 0.25f);

    CHECK(mapper["row_total"].get<std::size_t>() == 2u);
    REQUIRE(mapper["rows"].size() == 2u);
    CHECK(mapper["rows"][1]["bone_a"].get<int>() == 1);
    CHECK(mapper["rows"][1]["bone_a_name"].get<std::string>() == "Child");
    CHECK(mapper["rows"][1]["bone_b_name"].get<std::string>() == "Child");
    CHECK(mapper["rows"][1]["transform"]["translation"][1].get<float>() == 1.0f);
    CHECK_FALSE(mapper["rows"][1].contains("recomputed"));

    CHECK(mapper["chain_total"].get<std::size_t>() == 1u);
    REQUIRE(mapper["chains"].size() == 1u);
    CHECK(mapper["chains"][0]["end_bone_a"].get<int>() == 2);
    CHECK(mapper["chains"][0]["end_a_from_b"]["translation"][0].get<float>() == 0.5f);

    const auto cut = target.invoke("ReadMappers", nlohmann::json::array({file.string(), false, 1}));
    REQUIRE(cut.has_value());
    CHECK((*cut)["mappers"][0]["rows"].size() == 1u);
    CHECK((*cut)["mappers"][0]["row_total"].get<std::size_t>() == 2u);
}

TEST_CASE("HkxTarget ReadMappers recompute reports a row that no longer matches its skeletons") {
    // Constraint: detects a recompute that reports no gap for a stored transform the two reference poses do not produce.
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    HkxService service;
    HkxTarget target(service);

    const auto matching = cleanup.root / "matching.hkx";
    writeBytes(matching, mapper_packfile(default_mapper_spec()));
    const auto same = target.invoke("ReadMappers", nlohmann::json::array({matching.string(), true}));
    REQUIRE(same.has_value());
    CHECK((*same)["recompute"].get<bool>());
    const auto& rows = (*same)["mappers"][0]["rows"];
    REQUIRE(rows.size() == 2u);
    for (const auto& row : rows) {
        REQUIRE(row.contains("translation_delta"));
        CHECK(row["translation_delta"].get<float>() < 1e-4f);
        CHECK(row["rotation_delta"].get<float>() < 1e-3f);
    }

    auto drifted = default_mapper_spec();
    drifted.rows[1].lanes = mapper_lanes(0, 5, 0, 0, 0, 0, 1);
    const auto moved = cleanup.root / "moved.hkx";
    writeBytes(moved, mapper_packfile(drifted));
    const auto drift = target.invoke("ReadMappers", nlohmann::json::array({moved.string(), true}));
    REQUIRE(drift.has_value());
    const auto& row = (*drift)["mappers"][0]["rows"][1];
    CHECK(std::fabs(row["translation_delta"].get<float>() - 4.0f) < 1e-3f);
    CHECK(row["transform"]["translation"][1].get<float>() == 5.0f);
    CHECK(std::fabs(row["recomputed"]["translation"][1].get<float>() - 1.0f) < 1e-4f);
    CHECK((*drift)["mappers"][0]["rows"][0]["translation_delta"].get<float>() < 1e-4f);
}

TEST_CASE("HkxTarget ReadMappers refuses a file without a mapper and a missing file") {
    // Constraint: detects a mapper-less file reported as an empty mapper list instead of an error.
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    HkxService service;
    HkxTarget target(service);

    const auto rig = cleanup.root / "rig.hkx";
    writeBytes(rig, skeletonPackfile());
    const auto none = target.invoke("ReadMappers", nlohmann::json::array({rig.string()}));
    REQUIRE_FALSE(none.has_value());
    CHECK(none.error().type == "HkxRead");
    CHECK(none.error().message.find("MissingObject") != std::string::npos);

    const auto missing = target.invoke("ReadMappers", nlohmann::json::array({(cleanup.root / "nope.hkx").string()}));
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().type == "FileNotFound");
}

namespace {

std::vector<std::byte> mapperFileBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    const std::string raw((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    std::vector<std::byte> bytes(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i) bytes[i] = static_cast<std::byte>(raw[i]);
    return bytes;
}

std::size_t mapperEntryCount(const std::filesystem::path& folder) {
    return static_cast<std::size_t>(std::distance(std::filesystem::directory_iterator(folder),
                                                  std::filesystem::directory_iterator{}));
}

auto editMapperFile(HkxTarget& target, const std::filesystem::path& file, const std::string& edits,
                const std::filesystem::path& out = {}, bool overwrite = false, const std::string& expected = {}) {
    return target.invoke("EditMapper", nlohmann::json::array({file.string(), 0, nlohmann::json::parse(edits),
                                                              out.string(), overwrite, expected}));
}

}  // namespace

TEST_CASE("HkxTarget EditMapper builds rows, saves them and the output reopens with chains, unmapped bones and motion kept") {
    // Constraint: detects a built or edited mapper that is saved wrong, reopens with other rows, or loses chains, unmapped bones or the source file.
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    HkxService service;
    HkxTarget target(service);

    const auto file = cleanup.root / "mapper.hkx";
    writeBytes(file, mapper_packfile(default_mapper_spec()));
    const auto before = mapperFileBytes(file);
    const auto read = target.invoke("ReadMappers", nlohmann::json::array({file.string()}));
    REQUIRE(read.has_value());
    const auto sourceSha = (*read)["source_sha256"].get<std::string>();
    CHECK(sourceSha.size() == 64u);

    const std::string edits = R"([
        {"kind": "build_rows", "pairs": [{"bone_a": 0, "bone_b": 0}, {"bone_a": 1, "bone_b": 1}, {"bone_a": 2, "bone_b": 2}]},
        {"kind": "set_mapping_type", "mapping_type": 0},
        {"kind": "set_row_pose", "row": 1, "transform": {"translation": [0, 3, 0], "rotation": [0, 0, 0, 1]}}])";

    const auto preview = editMapperFile(target, file, edits, {}, false, sourceSha);
    REQUIRE(preview.has_value());
    CHECK((*preview)["preview"].get<bool>());
    CHECK_FALSE((*preview)["saved"].get<bool>());
    CHECK((*preview)["changed"].get<bool>());
    CHECK((*preview)["mappers"][0]["row_total"].get<std::size_t>() == 3u);
    CHECK(mapperFileBytes(file) == before);

    const auto out = cleanup.root / "built.hkx";
    const auto saved = editMapperFile(target, file, edits, out, false, sourceSha);
    REQUIRE(saved.has_value());
    CHECK((*saved)["saved"].get<bool>());
    CHECK((*saved)["source_sha256"].get<std::string>() == sourceSha);
    CHECK((*saved)["output_sha256"].get<std::string>() != sourceSha);
    CHECK(mapperFileBytes(file) == before);
    REQUIRE(std::filesystem::exists(out));

    const auto reopened = target.invoke("ReadMappers", nlohmann::json::array({out.string()}));
    REQUIRE(reopened.has_value());
    CHECK((*reopened)["source_sha256"] == (*saved)["output_sha256"]);
    const auto& mapper = (*reopened)["mappers"][0];
    CHECK(mapper["mapping_type"].get<int>() == 0);
    REQUIRE(mapper["rows"].size() == 3u);
    CHECK(mapper["rows"][1]["transform"]["translation"][1].get<float>() == 3.0f);
    CHECK(std::fabs(mapper["rows"][2]["transform"]["translation"][2].get<float>() - 3.0f) < 1e-4f);
    CHECK(mapper["keep_unmapped_local"].get<bool>());
    CHECK(mapper["unmapped_bones"][0].get<int>() == 2);
    CHECK(mapper["extracted_motion"]["translation"][0].get<float>() == 0.25f);
    REQUIRE(mapper["chains"].size() == 1u);
    CHECK(mapper["chains"][0]["end_a_from_b"]["translation"][0].get<float>() == 0.5f);

    const auto same = editMapperFile(target, file, R"([{"kind": "set_mapping_type", "mapping_type": 1}])");
    REQUIRE(same.has_value());
    CHECK_FALSE((*same)["changed"].get<bool>());
    CHECK((*same)["output_sha256"].get<std::string>() == sourceSha);

    const auto byName = editMapperFile(target, file, R"([{"kind": "build_rows"}])");
    REQUIRE(byName.has_value());
    const auto& named = (*byName)["mappers"][0]["rows"];
    REQUIRE(named.size() == 2u);
    CHECK(named[0]["bone_a_name"].get<std::string>() == "Root");
    CHECK(named[1]["bone_a_name"].get<std::string>() == "Child");
    CHECK(named[1]["bone_b_name"].get<std::string>() == "Child");
}

TEST_CASE("HkxTarget EditMapper refuses a resize the writer cannot place and leaves source and output alone") {
    // Constraint: detects a preview or save that reports a row resize as writable when the mapper has partition ranges.
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    HkxService service;
    HkxTarget target(service);

    auto spec = default_mapper_spec();
    spec.partition_ranges = true;
    const auto file = cleanup.root / "ranges.hkx";
    writeBytes(file, mapper_packfile(spec));
    const auto before = mapperFileBytes(file);
    const auto out = cleanup.root / "out.hkx";

    for (const auto& target_out : {std::filesystem::path{}, out}) {
        const auto refused = editMapperFile(target, file, R"([{"kind": "remove_row", "row": 1}])", target_out);
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().type == "MapperWriteRefused");
        CHECK(refused.error().message.find("simpleMappingPartitionRanges") != std::string::npos);
        CHECK_FALSE(std::filesystem::exists(out));
        CHECK(mapperFileBytes(file) == before);
    }
    CHECK(mapperEntryCount(cleanup.root) == 1u);

    const auto value = editMapperFile(target, file, R"([{"kind": "set_row_pose", "row": 1,
        "transform": {"translation": [0, 2, 0], "rotation": [0, 0, 0, 1]}}])", out);
    REQUIRE(value.has_value());
    CHECK((*value)["saved"].get<bool>());
    CHECK((*value)["mappers"][0]["rows"][1]["transform"]["translation"][1].get<float>() == 2.0f);
    const auto saved = mapperFileBytes(out);
    REQUIRE(saved.size() == before.size());
    std::size_t differing = 0;
    for (std::size_t i = 0; i < saved.size(); ++i) differing += saved[i] != before[i] ? 1u : 0u;
    CHECK(differing > 0u);
    CHECK(differing <= 4u);
}

TEST_CASE("HkxTarget EditMapper refuses an existing output, an in-place save and a changed source without touching them") {
    // Constraint: detects a save that overwrites the source or another file without overwrite, ignores a changed source, or leaves staging litter.
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    HkxService service;
    HkxTarget target(service);

    const auto file = cleanup.root / "mapper.hkx";
    writeBytes(file, mapper_packfile(default_mapper_spec()));
    const auto before = mapperFileBytes(file);
    const auto other = cleanup.root / "other.hkx";
    const std::vector<std::byte> keep(5, std::byte{7});
    writeBytes(other, keep);
    const std::string edits = R"([{"kind": "set_mapping_type", "mapping_type": 0}])";

    const auto existing = editMapperFile(target, file, edits, other);
    REQUIRE_FALSE(existing.has_value());
    CHECK(existing.error().type == "OutputExists");
    CHECK(mapperFileBytes(other) == keep);

    const auto inPlace = editMapperFile(target, file, edits, file);
    REQUIRE_FALSE(inPlace.has_value());
    CHECK(inPlace.error().type == "OutputExists");
    CHECK(mapperFileBytes(file) == before);

    const auto stale = editMapperFile(target, file, edits, file, true, "deadbeef");
    REQUIRE_FALSE(stale.has_value());
    CHECK(stale.error().type == "MapperSourceChanged");
    CHECK(mapperFileBytes(file) == before);

    const auto noFolder = editMapperFile(target, file, edits, cleanup.root / "missing" / "out.hkx");
    REQUIRE_FALSE(noFolder.has_value());
    CHECK(noFolder.error().type == "OutputFolderMissing");
    CHECK(mapperEntryCount(cleanup.root) == 2u);

    const auto read = target.invoke("ReadMappers", nlohmann::json::array({file.string()}));
    REQUIRE(read.has_value());
    const auto replaced = editMapperFile(target, file, edits, file, true, (*read)["source_sha256"].get<std::string>());
    REQUIRE(replaced.has_value());
    CHECK((*replaced)["saved"].get<bool>());
    CHECK(mapperFileBytes(file) != before);
    CHECK(mapperEntryCount(cleanup.root) == 2u);
    const auto after = target.invoke("ReadMappers", nlohmann::json::array({file.string()}));
    REQUIRE(after.has_value());
    CHECK((*after)["mappers"][0]["mapping_type"].get<int>() == 0);

    std::filesystem::create_directories(cleanup.root / "folder.hkx");
    const auto asDirectory = editMapperFile(target, file, edits, cleanup.root / "folder.hkx", true);
    REQUIRE_FALSE(asDirectory.has_value());
    CHECK(asDirectory.error().type == "InvalidPath");
    const auto wrongType = editMapperFile(target, file, edits, cleanup.root / "mapper.txt");
    REQUIRE_FALSE(wrongType.has_value());
    CHECK(wrongType.error().type == "InvalidPath");
    CHECK_FALSE(std::filesystem::exists(cleanup.root / "mapper.txt"));
    CHECK(mapperEntryCount(cleanup.root) == 3u);
}

TEST_CASE("HkxTarget EditMapper refuses chain resizes, bad bones and malformed edits") {
    // Constraint: detects a chain add or remove, an out of range bone or mapping type, a duplicate pairing or a half-applied edit list that is accepted or written.
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    HkxService service;
    HkxTarget target(service);

    const auto file = cleanup.root / "mapper.hkx";
    writeBytes(file, mapper_packfile(default_mapper_spec()));
    const auto before = mapperFileBytes(file);
    const auto out = cleanup.root / "out.hkx";

    const auto refuse = [&](const std::string& edits, const std::string& type, const std::string& text) {
        const auto result = editMapperFile(target, file, edits, out);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().type == type);
        CHECK(result.error().message.find(text) != std::string::npos);
        CHECK_FALSE(std::filesystem::exists(out));
        CHECK(mapperFileBytes(file) == before);
    };
    refuse(R"([{"kind": "insert_chain"}])", "InvalidMapperEdit", "chain");
    refuse(R"([{"kind": "remove_chain", "row": 0}])", "InvalidMapperEdit", "chain");
    refuse(R"([{"kind": "build_chains"}])", "InvalidMapperEdit", "chain");
    refuse(R"([{"kind": "set_row_bones", "row": 0, "bone_a": 99, "bone_b": 0}])", "MapperEditRefused", "edit 0");
    refuse(R"([{"kind": "set_row_bones", "row": 0, "bone_a": 40000, "bone_b": 0}])", "InvalidMapperEdit", "16 bit");
    refuse(R"([{"kind": "set_mapping_type", "mapping_type": 2}])", "MapperEditRefused", "mapping type");
    refuse(R"([{"kind": "remove_row", "row": 9}])", "MapperEditRefused", "no row 9");
    refuse(R"([{"kind": "insert_row", "bone_a": 1, "bone_b": 1}])", "MapperEditRefused", "already mapped");
    refuse(R"([{"kind": "set_row_pose", "row": 0, "transform": {"translation": [0, 0, 0]}}])", "InvalidMapperEdit",
           "translation and rotation");
    refuse(R"([{"kind": "set_row_pose", "row": 0, "transform": {"translation": [0, 0, 0], "rotation": [0, 0, 0, 1], "scale": [2, 2, 2]}}])",
           "InvalidMapperEdit", "scale");
    refuse(R"([{"kind": 5}])", "InvalidMapperEdit", "kind");
    refuse(R"([{"kind": "set_keep_unmapped_local", "value": false}, {"kind": "remove_row", "row": 9}])",
           "MapperEditRefused", "edit 1");
    refuse(R"([])", "InvalidMapperEdit", "non-empty");

    const auto badMapper = target.invoke("EditMapper", nlohmann::json::array({file.string(), 5, nlohmann::json::array(
        {nlohmann::json{{"kind", "remove_row"}, {"row", 0}}}), out.string()}));
    REQUIRE_FALSE(badMapper.has_value());
    CHECK(badMapper.error().type == "InvalidMapper");
    CHECK_FALSE(std::filesystem::exists(out));

    const auto text = target.invoke("EditMapper", nlohmann::json::array({file.string(), 0, "[{\"kind\": \"remove_row\", \"row\": 1}]"}));
    REQUIRE(text.has_value());
    CHECK((*text)["mappers"][0]["row_total"].get<std::size_t>() == 1u);
    const auto notJson = target.invoke("EditMapper", nlohmann::json::array({file.string(), 0, "[{"}));
    REQUIRE_FALSE(notJson.has_value());
    CHECK(notJson.error().type == "InvalidMapperEdit");
}

TEST_CASE("HkxTarget SubgraphManifest names the same manifest from an id and from a behaviour path") {
    // Constraint: detects an id computed from a path that differs from the one the game names its manifest by, or a given id that skips the manifest.
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    const auto dataFolder = cleanup.root / "Data";
    const auto manifestDir = dataFolder / "Meshes" / "AnimTextData" / "AnimationFileData";
    std::filesystem::create_directories(manifestDir);
    {
        std::ofstream stream(manifestDir / (std::to_string(kMtBehaviorSubgraph) + ".txt"), std::ios::binary | std::ios::trunc);
        stream << "V4\n0\n" << kMtBehaviorSubgraph << "\n0\n" << kMtBehavior << "\n"
               << "Actors\\Character\\Animations\\MT\\Neutral\n"
               << "Actors\\Character\\Animations\\MT\\Missing\n";
        REQUIRE(stream.good());
    }
    const auto animationDir = dataFolder / "Meshes" / "Actors" / "Character" / "Animations" / "MT";
    std::filesystem::create_directories(animationDir);
    {
        std::ofstream stream(animationDir / "Neutral.hkx", std::ios::binary | std::ios::trunc);
        stream << "x";
        REQUIRE(stream.good());
    }

    HkxService service;
    HkxTarget target(service);
    const auto byId = target.invoke("SubgraphManifest", nlohmann::json::array(
        {std::to_string(kMtBehaviorSubgraph), "", nlohmann::json::array(), dataFolder.string()}));
    REQUIRE(byId.has_value());
    CHECK((*byId)["id"].get<std::uint64_t>() == kMtBehaviorSubgraph);
    CHECK((*byId)["id_source"].get<std::string>() == "given");
    REQUIRE_FALSE((*byId)["manifest"].is_null());
    CHECK((*byId)["manifest"]["source"].get<std::string>() == "loose");
    CHECK((*byId)["manifest"]["present"].get<std::size_t>() == 1u);
    REQUIRE((*byId)["manifest"]["unresolved"].size() == 1u);
    CHECK((*byId)["manifest"]["unresolved"][0].get<std::string>() == "Actors\\Character\\Animations\\MT\\Missing");

    nlohmann::json prefixes = nlohmann::json::array();
    for (const auto& prefix : kMtPrefixes) prefixes.push_back(prefix);
    const auto byPath = target.invoke("SubgraphManifest", nlohmann::json::array({"", kMtBehavior, prefixes, dataFolder.string()}));
    REQUIRE(byPath.has_value());
    CHECK((*byPath)["id"].get<std::uint64_t>() == kMtBehaviorSubgraph);
    CHECK((*byPath)["id_source"].get<std::string>() == "computed");
    CHECK((*byPath)["manifest"] == (*byId)["manifest"]);

    const auto noFolder = target.invoke("SubgraphManifest", nlohmann::json::array({"", kMtBehavior, prefixes, ""}));
    REQUIRE(noFolder.has_value());
    CHECK((*noFolder)["id"].get<std::uint64_t>() == kMtBehaviorSubgraph);
    CHECK((*noFolder)["manifest"].is_null());
    CHECK((*noFolder)["notes"].size() == 1u);

    const auto lost = target.invoke("SubgraphManifest", nlohmann::json::array({"7", "", nlohmann::json::array(), dataFolder.string()}));
    REQUIRE(lost.has_value());
    CHECK((*lost)["manifest"].is_null());
    CHECK((*lost)["notes"].size() == 1u);
}

TEST_CASE("HkxTarget SubgraphManifest refuses a missing, a doubled and a malformed identity") {
    // Constraint: detects an empty prefix list hashed to a number that names nothing, or two identities accepted at once.
    HkxService service;
    HkxTarget target(service);
    const auto prefixes = nlohmann::json::array({"Actors\\Character\\Animations"});
    const std::vector<nlohmann::json> refused{
        nlohmann::json::array({"", "", nlohmann::json::array(), ""}),
        nlohmann::json::array({"5", kMtBehavior, prefixes, ""}),
        nlohmann::json::array({"12x", "", nlohmann::json::array(), ""}),
        nlohmann::json::array({"-1", "", nlohmann::json::array(), ""}),
        nlohmann::json::array({"99999999999999999999999", "", nlohmann::json::array(), ""}),
        nlohmann::json::array({"", kMtBehavior, nlohmann::json::array(), ""})};
    for (const auto& args : refused) {
        const auto reply = target.invoke("SubgraphManifest", args);
        REQUIRE_FALSE(reply.has_value());
        CHECK(reply.error().type == "InvalidSubgraph");
    }

    const auto badFolder = target.invoke("SubgraphManifest", nlohmann::json::array({"5", "", nlohmann::json::array(), "Z:\\no\\such\\folder"}));
    REQUIRE_FALSE(badFolder.has_value());
    CHECK(badFolder.error().type == "DataFolderNotFound");
}

TEST_CASE("HkxTarget ProjectIndex scans a folder, groups identical graphs and keeps a broken file") {
    // Constraint: detects a scan that drops an unreadable file or fails to group two copies of one graph.
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root / "anim");
    HkxService service;
    HkxTarget target(service);

    writeBytes(cleanup.root / "anim" / "walk.hkx", behaviorPackfile());
    writeBytes(cleanup.root / "rig.hkx", behaviorPackfile());
    writeBytes(cleanup.root / "skeleton.hkx", skeletonPackfile());
    const std::vector<std::byte> junk{std::byte{1}, std::byte{2}, std::byte{3}};
    writeBytes(cleanup.root / "broken.hkx", junk);

    const auto index = target.invoke("ProjectIndex", nlohmann::json::array({cleanup.root.string(), "", 64}));
    REQUIRE(index.has_value());
    CHECK((*index)["file_count"].get<std::size_t>() == 4u);
    CHECK((*index)["read_count"].get<std::size_t>() == 3u);
    CHECK((*index)["files"].size() == 3u);
    REQUIRE((*index)["unreadable"].size() == 1u);
    CHECK((*index)["unreadable"][0]["name"].get<std::string>().find("broken.hkx") != std::string::npos);
    REQUIRE((*index)["duplicates"].size() == 1u);
    CHECK((*index)["duplicates"][0].size() == 2u);

    const auto matched = target.invoke("ProjectIndex", nlohmann::json::array({cleanup.root.string(), "walk", 64}));
    REQUIRE(matched.has_value());
    REQUIRE((*matched)["matches"].size() == 1u);
    CHECK((*matched)["matches"][0].get<std::string>().find("walk.hkx") != std::string::npos);

    const auto absent = target.invoke("ProjectIndex", nlohmann::json::array({(cleanup.root / "nope").string(), "", 64}));
    CHECK_FALSE(absent.has_value());
}

namespace {

void touchFile(const std::filesystem::path& path, const std::string& text = "x") {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << text;
}

nlohmann::json animationIndexArgs(const std::filesystem::path& data, const std::filesystem::path& mo2,
                                  const std::string& resolvePath, int limit, bool refresh) {
    return nlohmann::json::array({data.string(), mo2.string(), "", "", "", "", "", "", resolvePath, 0, limit, refresh});
}

void writeText(const std::filesystem::path& path, const std::string& text) {
    std::ofstream stream(path, std::ios::binary);
    stream << text;
}

std::string readText(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

nlohmann::json saveArguments(const std::filesystem::path& templateFile, const std::filesystem::path& skeleton,
                             const std::filesystem::path& gltf, const std::filesystem::path& output, bool overwrite = false) {
    return nlohmann::json::array({templateFile.string(), skeleton.string(), gltf.string(), output.string(), "", "", false, false, overwrite});
}

}  // namespace

TEST_CASE("HkxTarget AnimationIndex pages the listing, matches the actor assets and rescans only on refresh") {
    // Constraint: detects a reply that ignores paging, drops the skeleton match, or keeps serving a stale index after refresh.
    const Cleanup cleanup{tempRoot()};
    const auto data = cleanup.root / "Data";
    const std::string walk = "meshes/actors/character/animations/walk.hkx";
    touchFile(data / walk);
    touchFile(data / "meshes/actors/character/characterassets/skeleton.hkx");
    touchFile(data / "meshes/actors/character/characterassets/skeleton.nif");
    touchFile(data / "meshes/actors/character/bodyparts/body.nif");
    HkxService service;
    HkxTarget target(service);

    const auto first = target.invoke("AnimationIndex", animationIndexArgs(data, {}, walk, 1, false));
    REQUIRE(first.has_value());
    CHECK((*first)["manager"].get<std::string>() == "none");
    CHECK((*first)["total"].get<std::size_t>() == 2u);
    CHECK((*first)["matched"].get<std::size_t>() == 2u);
    CHECK((*first)["entries"].size() == 1u);
    CHECK((*first)["truncated"].get<bool>());
    CHECK((*first)["counts"]["animation"].get<std::size_t>() == 1u);
    CHECK((*first)["counts"]["skeleton"].get<std::size_t>() == 1u);
    CHECK((*first)["entries"][0]["provider"]["label"].get<std::string>() == "Data");
    CHECK((*first)["entries"][0]["provider"]["origin"].get<std::string>() == "loose");
    REQUIRE_FALSE((*first)["actor"]["skeleton"].is_null());
    CHECK((*first)["actor"]["skeleton"].get<std::string>() == "meshes/actors/character/characterassets/skeleton.hkx");
    CHECK((*first)["actor"]["skeleton_nif"].get<std::string>() == "meshes/actors/character/characterassets/skeleton.nif");
    CHECK((*first)["actor"]["body"].get<std::string>() == "meshes/actors/character/bodyparts/body.nif");

    touchFile(data / "meshes/actors/character/animations/run.hkx");
    const auto stale = target.invoke("AnimationIndex", animationIndexArgs(data, {}, "", 200, false));
    REQUIRE(stale.has_value());
    CHECK((*stale)["total"].get<std::size_t>() == 2u);
    CHECK_FALSE((*stale)["truncated"].get<bool>());
    CHECK_FALSE((*stale).contains("actor"));

    const auto fresh = target.invoke("AnimationIndex", animationIndexArgs(data, {}, "", 200, true));
    REQUIRE(fresh.has_value());
    CHECK((*fresh)["total"].get<std::size_t>() == 3u);
}

TEST_CASE("HkxTarget AnimationIndex reports the MO2 winner and the provider it shadows") {
    // Constraint: detects an MO2 mod that fails to beat the Data file, or a shadowed provider that goes unreported.
    const Cleanup cleanup{tempRoot()};
    const auto game = cleanup.root / "Game";
    touchFile(cleanup.root / "ModOrganizer.ini", "selected_profile=Test\ngamePath=" + game.string() + "\n");
    touchFile(game / "Data" / "Fallout4.esm", "");
    touchFile(cleanup.root / "profiles" / "Test" / "modlist.txt", "+High\n");
    touchFile(cleanup.root / "profiles" / "Test" / "plugins.txt", "");
    const std::string walk = "meshes/actors/character/animations/walk.hkx";
    touchFile(game / "Data" / walk, "data");
    touchFile(cleanup.root / "mods" / "High" / walk, "high!");
    HkxService service;
    HkxTarget target(service);

    const auto reply = target.invoke("AnimationIndex", animationIndexArgs({}, cleanup.root, "", 200, false));
    REQUIRE(reply.has_value());
    CHECK((*reply)["manager"].get<std::string>() == "mo2");
    CHECK((*reply)["profile"].get<std::string>() == "Test");
    REQUIRE((*reply)["entries"].size() == 1u);
    const auto& entry = (*reply)["entries"][0];
    CHECK(entry["provider"]["label"].get<std::string>() == "High");
    CHECK(entry["size"].get<std::size_t>() == 5u);
    CHECK(entry["provider"]["file"].get<std::string>().find("walk.hkx") != std::string::npos);
    REQUIRE(entry["shadowed"].size() == 1u);
    CHECK(entry["shadowed"][0]["label"].get<std::string>() == "Data");
}

TEST_CASE("HkxTarget AnimationIndex refuses a bad kind, a non-MO2 folder and a missing Data folder") {
    // Constraint: detects an unreadable source answering with an empty index that reads as an unmodded game.
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root / "Data");
    HkxService service;
    HkxTarget target(service);

    auto badKind = animationIndexArgs(cleanup.root / "Data", {}, "", 200, false);
    badKind[3] = "bogus";
    const auto kindReply = target.invoke("AnimationIndex", badKind);
    REQUIRE_FALSE(kindReply.has_value());
    CHECK(kindReply.error().type == "InvalidKind");

    const auto notMo2 = target.invoke("AnimationIndex", animationIndexArgs({}, cleanup.root, "", 200, false));
    REQUIRE_FALSE(notMo2.has_value());
    CHECK(notMo2.error().type == "InvalidSources");
    CHECK(notMo2.error().message.find("Vortex") != std::string::npos);

    const auto missing = target.invoke("AnimationIndex", animationIndexArgs(cleanup.root / "nope", {}, "", 200, false));
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().type == "InvalidSources");
}

TEST_CASE("HkxTarget SaveGltfAnimation never overwrites its inputs or an existing output and names the refusal") {
    // Constraint: detects a save that replaces the template, replaces an existing file without overwrite, or hides why an import was refused.
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    HkxService service;
    HkxTarget target(service);
    const auto templateFile = cleanup.root / "template.hkx";
    const auto skeleton = cleanup.root / "skeleton.hkx";
    const auto gltf = cleanup.root / "walk.gltf";
    const auto existing = cleanup.root / "existing.hkx";
    writeBytes(templateFile, losslessClipPackfile());
    writeBytes(skeleton, skeletonPackfile());
    writeText(gltf, "not json");
    writeText(existing, "keep me");

    const auto missing = target.invoke("SaveGltfAnimation", saveArguments(cleanup.root / "absent.hkx", skeleton, gltf, cleanup.root / "out.hkx"));
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().type == "FileNotFound");

    const auto overTemplate = target.invoke("SaveGltfAnimation", saveArguments(templateFile, skeleton, gltf, templateFile, true));
    REQUIRE_FALSE(overTemplate.has_value());
    CHECK(overTemplate.error().type == "HkxGltfRefused");

    const auto overExisting = target.invoke("SaveGltfAnimation", saveArguments(templateFile, skeleton, gltf, existing));
    REQUIRE_FALSE(overExisting.has_value());
    CHECK(overExisting.error().type == "OutputExists");
    CHECK(readText(existing) == "keep me");

    const auto output = cleanup.root / "out.hkx";
    const auto broken = target.invoke("SaveGltfAnimation", saveArguments(templateFile, skeleton, gltf, output));
    REQUIRE_FALSE(broken.has_value());
    CHECK(broken.error().type == "HkxGltfImport");
    CHECK(broken.error().message.starts_with("Malformed"));
    CHECK_FALSE(std::filesystem::exists(output));
}

TEST_CASE("HkxTarget SaveGltfAnimation saves only to a .hkx file in an existing folder") {
    // Constraint: detects a save that writes a non-.hkx name or creates a missing folder tree instead of naming the mistake.
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    HkxService service;
    HkxTarget target(service);
    writeBytes(cleanup.root / "template.hkx", losslessClipPackfile());
    writeBytes(cleanup.root / "skeleton.hkx", skeletonPackfile());
    writeText(cleanup.root / "walk.gltf", "not json");

    const auto wrongName = cleanup.root / "out.txt";
    const auto notHkx = target.invoke("SaveGltfAnimation", saveArguments(cleanup.root / "template.hkx", cleanup.root / "skeleton.hkx",
                                                                         cleanup.root / "walk.gltf", wrongName));
    REQUIRE_FALSE(notHkx.has_value());
    CHECK(notHkx.error().type == "InvalidPath");
    CHECK_FALSE(std::filesystem::exists(wrongName));

    const auto nowhere = cleanup.root / "absent" / "out.hkx";
    const auto noFolder = target.invoke("SaveGltfAnimation", saveArguments(cleanup.root / "template.hkx", cleanup.root / "skeleton.hkx",
                                                                           cleanup.root / "walk.gltf", nowhere));
    REQUIRE_FALSE(noFolder.has_value());
    CHECK(noFolder.error().type == "OutputFolderMissing");
    CHECK_FALSE(std::filesystem::exists(cleanup.root / "absent"));
}

TEST_CASE("HkxTarget SaveGltfAnimation refuses a glTF buffer outside the glTF folder") {
    // Constraint: detects an external buffer path that climbs out of the glTF folder and reads another file.
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root / "models");
    HkxService service;
    HkxTarget target(service);
    writeBytes(cleanup.root / "template.hkx", losslessClipPackfile());
    writeBytes(cleanup.root / "skeleton.hkx", skeletonPackfile());
    writeText(cleanup.root / "secret.bin", "abcd");
    writeText(cleanup.root / "models" / "walk.gltf",
              R"({"asset":{"version":"2.0"},"buffers":[{"uri":"../secret.bin","byteLength":4}]})");

    const auto output = cleanup.root / "out.hkx";
    const auto reply = target.invoke("SaveGltfAnimation", saveArguments(cleanup.root / "template.hkx", cleanup.root / "skeleton.hkx",
                                                                        cleanup.root / "models" / "walk.gltf", output));
    REQUIRE_FALSE(reply.has_value());
    CHECK(reply.error().type == "HkxGltfImport");
    CHECK(reply.error().message.find("climbs out of its folder") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(output));
}

TEST_CASE("HkxTarget SaveGltfAnimation saves the committed Turret clip from a glb and the output reopens as one clip") {
    // Constraint: detects a glTF save that only succeeds on synthetic templates, or an RPC reply that says saved while the file does not reopen on the same skeleton.
    const std::filesystem::path samples{FO4IDE_HKX_SAMPLES_DIR};
    const auto templateFile = samples / "TurretIdleWeapReady.hkx";
    const auto skeletonFile = samples / "TurretMountedSkeleton.hkx";
    if (!std::filesystem::exists(templateFile) || !std::filesystem::exists(skeletonFile)) {
        if (std::getenv("CI") != nullptr) FAIL("CI is missing the committed Turret samples");
        SKIP("no committed Turret samples");
    }
    const auto skeletonBytes = fileBytes(skeletonFile);
    fo4::hkx::HkxPackfile pack;
    REQUIRE(fo4::hkx::read_packfile(skeletonBytes, pack) == fo4::hkx::HkxStatus::Ok);
    fo4::hkx::HkxSkeleton skeleton;
    REQUIRE(fo4::hkx::read_skeleton(pack, skeleton) == fo4::hkx::HkxStatus::Ok);
    const auto lid = std::find(skeleton.bone_names.begin(), skeleton.bone_names.end(), "Lid");
    REQUIRE(lid != skeleton.bone_names.end());

    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    const auto gltf = cleanup.root / "turret.glb";
    writeBytes(gltf, fo4::test::hkx::turret_animation_glb(skeleton, static_cast<std::size_t>(lid - skeleton.bone_names.begin())));
    const auto output = cleanup.root / "turret_out.hkx";
    const auto templateBefore = fileBytes(templateFile);

    HkxService service;
    HkxTarget target(service);
    const auto saved = target.invoke("SaveGltfAnimation", saveArguments(templateFile, skeletonFile, gltf, output));
    REQUIRE(saved.has_value());
    CHECK((*saved)["saved"].get<bool>() == true);
    CHECK((*saved)["overwrote"].get<bool>() == false);
    CHECK((*saved)["report"]["tracks"].get<std::size_t>() == 8u);
    CHECK((*saved)["report"]["animated"] == nlohmann::json::array({"Lid"}));
    CHECK((*saved)["report"]["skeleton_match"].get<std::string>() == "match");
    CHECK((*saved)["report"]["binding_linked"].get<bool>() == true);
    CHECK(fileBytes(templateFile) == templateBefore);
    REQUIRE(std::filesystem::exists(output));
    CHECK((*saved)["bytes"].get<std::size_t>() == fileBytes(output).size());

    const auto reopened = target.invoke("Summary", nlohmann::json::array({output.string()}));
    REQUIRE(reopened.has_value());
    REQUIRE((*reopened)["clips"].size() == 1u);
    CHECK((*reopened)["clips"][0]["kind"].get<std::string>() == "spline");
    CHECK((*reopened)["clips"][0]["track_count"].get<std::size_t>() == 8u);
    CHECK((*reopened)["bindings"].size() == 1u);
}

// Regression: a ClipEdit that previews, saves and reopens must keep the trimmed frames, annotations and root motion.
TEST_CASE("HkxTarget ClipEdit previews, saves and reopens a trimmed clip") {
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    const auto clip = cleanup.root / "clip.hkx";
    const auto edited = cleanup.root / "edited.hkx";
    writeBytes(clip, fo4::test::hkx::editable_packfile());
    HkxService service;
    HkxTarget target(service);
    const auto trim = nlohmann::json::parse(R"([{"op":"trim","first":1,"last":3}])");

    const auto preview = target.invoke("ClipEdit", nlohmann::json::array({clip.string(), trim, "", "", false}));
    REQUIRE(preview.has_value());
    CHECK((*preview)["saved"].get<bool>() == false);
    CHECK((*preview)["frame_count"].get<std::size_t>() == 3u);
    CHECK((*preview)["report"]["annotations_dropped"].get<std::size_t>() == 2u);
    CHECK_FALSE(std::filesystem::exists(edited));

    const auto saved = target.invoke(
        "ClipEdit", nlohmann::json::array({clip.string(), trim, edited.string(), (*preview)["source"], false}));
    REQUIRE(saved.has_value());
    CHECK((*saved)["saved"].get<bool>() == true);
    REQUIRE(std::filesystem::exists(edited));

    const auto again = nlohmann::json::parse(R"([{"op":"set_key","track":0,"frame":0,"translation":[1,2,3]}])");
    const auto reopened = target.invoke("ClipEdit", nlohmann::json::array({edited.string(), again, "", "", false}));
    REQUIRE(reopened.has_value());
    CHECK((*reopened)["frame_count"].get<std::size_t>() == 3u);
    CHECK((*reopened)["motion"].get<std::string>() == "default");
    REQUIRE((*reopened)["annotation_tracks"].size() == 1u);
    CHECK((*reopened)["annotation_tracks"][0]["annotations"].get<std::size_t>() == 1u);

    const auto kinked = nlohmann::json::parse(
        R"([{"op":"set_key","track":0,"frame":2,"translation":[100,0,0]},{"op":"retime","scale":0.75,"keep_frame_rate":true}])");
    const auto resampled = target.invoke("ClipEdit", nlohmann::json::array({clip.string(), kinked, "", "", false}));
    REQUIRE(resampled.has_value());
    CHECK((*resampled)["report"]["resampled"].get<bool>() == true);
    CHECK((*resampled)["report"]["position_error"].get<float>() > 1.0f);
    CHECK((*resampled)["save_report"]["position_error"].get<float>() < 0.02f);
    CHECK((*resampled)["spline"].get<bool>() == false);
}

// Regression: a refused op list, a stale source or an existing target must never leave or change a file.
TEST_CASE("HkxTarget ClipEdit refuses a bad op list and an unsafe save without writing a file") {
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    const auto clip = cleanup.root / "clip.hkx";
    const auto floats = cleanup.root / "floats.hkx";
    const auto out = cleanup.root / "out.hkx";
    writeBytes(clip, fo4::test::hkx::editable_packfile());
    fo4::test::hkx::EditSpec withFloats;
    withFloats.float_track = true;
    writeBytes(floats, fo4::test::hkx::editable_packfile(withFloats));
    HkxService service;
    HkxTarget target(service);
    const auto run = [&](const std::filesystem::path& path, const char* ops, const std::string& expected = "",
                         bool overwrite = false) {
        return target.invoke("ClipEdit", nlohmann::json::array({path.string(), nlohmann::json::parse(ops),
                                                                out.string(), expected, overwrite}));
    };

    const auto unknownOp = run(clip, R"([{"op":"trim","first":0,"last":2},{"op":"explode"}])");
    REQUIRE_FALSE(unknownOp.has_value());
    CHECK(unknownOp.error().type == "InvalidClipEdit");
    CHECK(unknownOp.error().message.find("op 1") != std::string::npos);

    const auto unknownField = run(clip, R"([{"op":"trim","first":0,"last":2,"extra":1}])");
    REQUIRE_FALSE(unknownField.has_value());
    CHECK(unknownField.error().type == "InvalidClipEdit");

    const auto badRange = run(clip, R"([{"op":"trim","first":3,"last":1}])");
    REQUIRE_FALSE(badRange.has_value());
    CHECK(badRange.error().message.find("BadRange") != std::string::npos);

    const auto unsupported = run(floats, R"([{"op":"trim","first":0,"last":2}])");
    REQUIRE_FALSE(unsupported.has_value());
    CHECK(unsupported.error().message.find("UnsupportedClip") != std::string::npos);

    const auto stale = run(clip, R"([{"op":"trim","first":0,"last":2}])", "00");
    REQUIRE_FALSE(stale.has_value());
    CHECK(stale.error().type == "HkxChanged");
    CHECK_FALSE(std::filesystem::exists(out));

    const auto first = run(clip, R"([{"op":"trim","first":0,"last":2}])");
    REQUIRE(first.has_value());
    const auto size = std::filesystem::file_size(out);
    const auto refused = run(clip, R"([{"op":"trim","first":0,"last":3}])");
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "OutputExists");
    CHECK(std::filesystem::file_size(out) == size);
    const auto replaced = run(clip, R"([{"op":"trim","first":0,"last":3}])", "", true);
    REQUIRE(replaced.has_value());
    CHECK(std::filesystem::file_size(out) != size);

    const auto trim = nlohmann::json::parse(R"([{"op":"trim","first":0,"last":2}])");
    const auto esp = cleanup.root / "plugin.esp";
    writeBytes(esp, fo4::test::hkx::editable_packfile());
    const auto espSize = std::filesystem::file_size(esp);
    const auto wrongKind = target.invoke("ClipEdit", nlohmann::json::array({clip.string(), trim, esp.string(), "", true}));
    REQUIRE_FALSE(wrongKind.has_value());
    CHECK(wrongKind.error().type == "InvalidPath");
    CHECK(std::filesystem::file_size(esp) == espSize);

    const auto missingFolder = target.invoke(
        "ClipEdit", nlohmann::json::array({clip.string(), trim, (cleanup.root / "none" / "a.hkx").string(), "", false}));
    REQUIRE_FALSE(missingFolder.has_value());
    CHECK(missingFolder.error().type == "OutputFolderMissing");

    const auto clipSize = std::filesystem::file_size(clip);
    const auto inPlaceRefused = target.invoke("ClipEdit", nlohmann::json::array({clip.string(), trim, clip.string(), "", false}));
    REQUIRE_FALSE(inPlaceRefused.has_value());
    CHECK(inPlaceRefused.error().type == "OutputExists");
    CHECK(std::filesystem::file_size(clip) == clipSize);

    const auto negative = target.invoke(
        "ClipEdit", nlohmann::json::array({clip.string(), trim, "", "", false, nullptr, -1.0}));
    REQUIRE_FALSE(negative.has_value());
    CHECK(negative.error().type == "InvalidClipEdit");

    const auto inPlace = target.invoke("ClipEdit", nlohmann::json::array({clip.string(), trim, clip.string(), "", true}));
    REQUIRE(inPlace.has_value());
    CHECK(std::filesystem::file_size(clip) != clipSize);
}

TEST_CASE("HkxTarget ClipEdit in place refuses a replayed source digest after its own save") {
    // Constraint: detects an in-place save that still accepts the digest from before it, which lets a stale editor overwrite the file a second time.
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    const auto clip = cleanup.root / "clip.hkx";
    writeBytes(clip, fo4::test::hkx::editable_packfile());
    HkxService service;
    HkxTarget target(service);
    const auto trim = nlohmann::json::parse(R"([{"op":"trim","first":0,"last":2}])");
    const auto shorter = nlohmann::json::parse(R"([{"op":"trim","first":0,"last":1}])");

    const auto preview = target.invoke("ClipEdit", nlohmann::json::array({clip.string(), trim, "", "", false}));
    REQUIRE(preview.has_value());
    const std::string digest = (*preview)["source"].get<std::string>();
    const auto original = fileBytes(clip);

    const auto saved = target.invoke("ClipEdit", nlohmann::json::array({clip.string(), trim, clip.string(), digest, true}));
    REQUIRE(saved.has_value());
    CHECK((*saved)["saved"].get<bool>() == true);
    const auto written = fileBytes(clip);
    CHECK(written != original);

    const auto replayed = target.invoke("ClipEdit", nlohmann::json::array({clip.string(), shorter, clip.string(), digest, true}));
    REQUIRE_FALSE(replayed.has_value());
    CHECK(replayed.error().type == "HkxChanged");
    CHECK(fileBytes(clip) == written);

    const auto fresh = target.invoke("ClipEdit", nlohmann::json::array({clip.string(), shorter, "", "", false}));
    REQUIRE(fresh.has_value());
    CHECK((*fresh)["source"].get<std::string>() != digest);
    const auto next = target.invoke(
        "ClipEdit", nlohmann::json::array({clip.string(), shorter, clip.string(), (*fresh)["source"], true}));
    REQUIRE(next.has_value());
    CHECK(fileBytes(clip) != written);
}

TEST_CASE("HkxTarget ClipEdit in place keeps a rotating backup of the replaced file") {
    // Constraint: detects an in-place save that replaces the clip with no .bak, a rotation that loses the older copy, or a backup written beside a different file.
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    const auto clip = cleanup.root / "clip.hkx";
    writeBytes(clip, fo4::test::hkx::editable_packfile());
    HkxService service;
    HkxTarget target(service);
    const auto trim = nlohmann::json::parse(R"([{"op":"trim","first":0,"last":2}])");
    const auto shorter = nlohmann::json::parse(R"([{"op":"trim","first":0,"last":1}])");
    const auto original = fileBytes(clip);
    const auto backup = std::filesystem::path(clip.string() + ".bak");
    const auto older = std::filesystem::path(clip.string() + ".bak.1");

    const auto preview = target.invoke("ClipEdit", nlohmann::json::array({clip.string(), trim, "", "", false}));
    REQUIRE(preview.has_value());
    CHECK_FALSE((*preview).contains("backup"));
    CHECK_FALSE(std::filesystem::exists(backup));

    const auto first = target.invoke(
        "ClipEdit", nlohmann::json::array({clip.string(), trim, clip.string(), (*preview)["source"], true}));
    REQUIRE(first.has_value());
    CHECK((*first)["backup"].get<std::string>() == backup.string());
    CHECK(fileBytes(backup) == original);
    const auto afterFirst = fileBytes(clip);
    CHECK(afterFirst != original);

    const auto fresh = target.invoke("ClipEdit", nlohmann::json::array({clip.string(), shorter, "", "", false}));
    REQUIRE(fresh.has_value());
    const auto second = target.invoke(
        "ClipEdit", nlohmann::json::array({clip.string(), shorter, clip.string(), (*fresh)["source"], true}));
    REQUIRE(second.has_value());
    CHECK(fileBytes(backup) == afterFirst);
    CHECK(fileBytes(older) == original);
}

TEST_CASE("HkxTarget ClipEdit in place refuses with BackupFailed and leaves the clip untouched when the backup cannot rotate") {
    // Constraint: detects an in-place save that goes ahead after its backup failed, which would lose the only copy of the original.
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    const auto clip = cleanup.root / "clip.hkx";
    writeBytes(clip, fo4::test::hkx::editable_packfile());
    HkxService service;
    HkxTarget target(service);
    const auto trim = nlohmann::json::parse(R"([{"op":"trim","first":0,"last":2}])");
    const auto original = fileBytes(clip);

    const auto preview = target.invoke("ClipEdit", nlohmann::json::array({clip.string(), trim, "", "", false}));
    REQUIRE(preview.has_value());

    writeBytes(std::filesystem::path(clip.string() + ".bak"), std::vector<std::byte>(4, std::byte{1}));
    writeBytes(std::filesystem::path(clip.string() + ".bak.1"), std::vector<std::byte>(4, std::byte{2}));
    const auto oldest = std::filesystem::path(clip.string() + ".bak.2");
    std::filesystem::create_directories(oldest);
    writeBytes(oldest / "keep.txt", std::vector<std::byte>(1, std::byte{3}));

    const auto refused = target.invoke(
        "ClipEdit", nlohmann::json::array({clip.string(), trim, clip.string(), (*preview)["source"], true}));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "BackupFailed");
    CHECK(fileBytes(clip) == original);
    CHECK(fileBytes(std::filesystem::path(clip.string() + ".bak")) == std::vector<std::byte>(4, std::byte{1}));
}

TEST_CASE("HkxTarget ClipEdit retimes to a length in seconds and refuses both or neither of scale and duration and a bad length") {
    // Constraint: detects a duration retime that ignores the length, accepts scale and duration together, or accepts an empty retime.
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    const auto clip = cleanup.root / "clip.hkx";
    writeBytes(clip, fo4::test::hkx::editable_packfile());
    HkxService service;
    HkxTarget target(service);
    const auto run = [&](const char* ops) {
        return target.invoke("ClipEdit", nlohmann::json::array({clip.string(), nlohmann::json::parse(ops), "", "", false}));
    };

    const auto timed = run(R"([{"op":"retime","duration":2.0}])");
    REQUIRE(timed.has_value());
    CHECK((*timed)["frame_count"].get<std::size_t>() == 5u);
    CHECK(std::abs((*timed)["duration"].get<float>() - 2.0f) < 1e-4f);

    const auto both = run(R"([{"op":"retime","scale":0.5,"duration":2.0}])");
    REQUIRE_FALSE(both.has_value());
    CHECK(both.error().type == "InvalidClipEdit");
    const auto neither = run(R"([{"op":"retime","keep_frame_rate":true}])");
    REQUIRE_FALSE(neither.has_value());
    CHECK(neither.error().type == "InvalidClipEdit");
    const auto negative = run(R"([{"op":"retime","duration":-1}])");
    REQUIRE_FALSE(negative.has_value());
    CHECK(negative.error().type == "HkxClipEdit");
}

TEST_CASE("HkxTarget ExtractAnimationAsset returns a loose winner in place, writes an archive winner outside the game folders and refuses other files") {
    // Constraint: detects a BA2 winner the panel cannot load, an extraction written under the Data folder, a loose mod file replaced by an archive copy, a same-size corrupted cache file served as the archive's bytes, or a lookup that serves arbitrary files.
    const Cleanup cleanup{tempRoot()};
    const auto data = cleanup.root / "Data";
    const std::string walk = "meshes/actors/character/animations/walk.hkx";
    const std::string howl = "meshes/actors/wolf/animations/howl.hkx";
    touchFile(data / walk, "loose-walk");
    fo4::archive::Ba2GeneralArchive archive;
    for (const std::string& entryPath : {walk, howl, std::string("meshes/actors/wolf/body.nif"), std::string("meshes/actors/wolf/notes.txt")}) {
        fo4::archive::Ba2GeneralEntry entry;
        entry.path = entryPath;
        std::vector<std::byte> payload;
        for (const char ch : "ba2-" + entryPath) payload.push_back(static_cast<std::byte>(ch));
        entry.chunks.push_back(fo4::archive::make_stored_ba2_chunk(payload));
        archive.entries.push_back(std::move(entry));
    }
    fo4::archive::write_ba2_general_file(archive, data / "Pack.ba2");
    HkxService service;
    HkxTarget target(service);
    const auto ask = [&](const std::string& path) {
        return target.invoke("ExtractAnimationAsset", nlohmann::json::array({data.string(), "", "", path}));
    };
    const auto read = [](const std::string& file) {
        std::ifstream stream(std::filesystem::path(file), std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    };

    const auto loose = ask(walk);
    REQUIRE(loose.has_value());
    CHECK_FALSE((*loose)["extracted"].get<bool>());
    CHECK(std::filesystem::equivalent(std::filesystem::path((*loose)["path"].get<std::string>()), data / walk));
    CHECK(read((*loose)["path"].get<std::string>()) == "loose-walk");

    const auto packed = ask(howl);
    REQUIRE(packed.has_value());
    CHECK((*packed)["extracted"].get<bool>());
    CHECK((*packed)["provider"]["origin"].get<std::string>() == "archive");
    const auto packedPath = (*packed)["path"].get<std::string>();
    CHECK(read(packedPath) == "ba2-" + howl);
    CHECK(packedPath.ends_with(".hkx"));
    CHECK(packedPath.find("FO4RE_Hkx") != std::string::npos);
    CHECK(packedPath.find(data.string()) == std::string::npos);
    CHECK(packedPath.find("howl") == std::string::npos);

    const auto again = ask(howl);
    REQUIRE(again.has_value());
    CHECK((*again)["path"].get<std::string>() == packedPath);

    {
        std::ofstream corrupt(std::filesystem::path(packedPath), std::ios::binary | std::ios::trunc);
        corrupt << std::string(("ba2-" + howl).size(), 'x');
    }
    const auto repaired = ask(howl);
    REQUIRE(repaired.has_value());
    CHECK(read((*repaired)["path"].get<std::string>()) == "ba2-" + howl);

    const auto body = ask("meshes/actors/wolf/body.nif");
    REQUIRE(body.has_value());
    CHECK((*body)["path"].get<std::string>().ends_with(".nif"));

    const auto text = ask("meshes/actors/wolf/notes.txt");
    REQUIRE_FALSE(text.has_value());
    CHECK(text.error().type == "InvalidAssetPath");
    const auto escape = ask("meshes/../outside.hkx");
    REQUIRE_FALSE(escape.has_value());
    CHECK(escape.error().type == "InvalidAssetPath");
    const auto missing = ask("meshes/actors/wolf/missing.hkx");
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().type == "AssetNotFound");

    std::error_code ec;
    std::filesystem::remove(packedPath, ec);
    std::filesystem::remove((*body)["path"].get<std::string>(), ec);
}

TEST_CASE("HkxTarget ProjectChain and ProjectCheck read the Data folder, and an MO2 mod replaces a broken Data behaviour") {
    // Constraint: detects a chain or check that reads only the Data folder when an MO2 instance is given, so a mod's replacement is never the file checked, or a lost problem for a missing animation.
    const Cleanup cleanup{tempRoot()};
    const auto data = cleanup.root / "Game" / "Data";
    const auto foo = data / "Meshes" / "Actors" / "Foo";
    for (const char* folder : {"Characters", "Behaviors", "CharacterAssets"}) std::filesystem::create_directories(foo / folder);
    writeBytes(foo / "Foo.hkx", project_packfile({"Characters\\Foo.hkx"}));
    writeBytes(foo / "Characters" / "Foo.hkx",
               character_packfile("CharacterAssets\\skeleton.hkx", "Behaviors\\FooBehavior.hkx", {{"Animations\\walk.hkx"}}));
    writeBytes(foo / "CharacterAssets" / "skeleton.hkx", skeletonPackfile());
    writeBytes(foo / "Behaviors" / "FooBehavior.hkx", {std::byte{1}, std::byte{2}, std::byte{3}});
    writeBytes(data / "Bad.ba2", {std::byte{1}, std::byte{2}, std::byte{3}});

    HkxService service;
    HkxTarget target(service);
    const std::string project = "Meshes/Actors/Foo/Foo.hkx";

    const auto chain = target.invoke("ProjectChain", nlohmann::json::array({project, data.string(), "", ""}));
    REQUIRE(chain.has_value());
    REQUIRE((*chain)["notes"].size() == 1u);
    CHECK((*chain)["notes"][0].get<std::string>().find("Bad.ba2") != std::string::npos);
    CHECK((*chain)["root"].get<std::string>() == "meshes/actors/foo");
    CHECK_FALSE((*chain)["ok"].get<bool>());
    REQUIRE((*chain)["problems"].size() == 1u);
    CHECK((*chain)["problems"][0].get<std::string>().find("missing animation: Animations\\walk.hkx") != std::string::npos);
    bool sawSkeleton = false;
    for (const auto& link : (*chain)["links"]) {
        if (link["role"].get<std::string>() == "skeleton") {
            sawSkeleton = true;
            CHECK(link["note"].get<std::string>() == "3 bones");
        }
    }
    CHECK(sawSkeleton);

    const auto broken = target.invoke("ProjectCheck", nlohmann::json::array({project, data.string(), "", "", 100}));
    REQUIRE(broken.has_value());
    CHECK((*broken)["errors"].get<std::size_t>() == 1u);
    CHECK((*broken)["files"].get<std::size_t>() == 1u);
    CHECK((*broken)["unreadable"].get<std::size_t>() == 1u);
    CHECK((*broken)["round_trip_losses"].get<std::size_t>() == 0u);
    REQUIRE((*broken)["report"]["files"].size() == 1u);
    CHECK_FALSE((*broken)["report"]["files"][0]["unreadable"].is_null());
    CHECK((*broken)["report"]["totals"]["unreadable"].get<std::size_t>() == 1u);
    CHECK((*broken)["summary"].get<std::string>().find("1 could not be unpacked") != std::string::npos);
    CHECK_FALSE(broken->contains("rendered"));

    const auto high = cleanup.root / "mods" / "High" / "Meshes" / "Actors" / "Foo";
    std::filesystem::create_directories(high / "Behaviors");
    std::filesystem::create_directories(high / "Animations");
    std::filesystem::create_directories(cleanup.root / "overwrite");
    writeBytes(high / "Behaviors" / "FooBehavior.hkx", behavior_graph_packfile());
    writeBytes(high / "Animations" / "walk.hkx", {std::byte{1}});
    for (const char* clip : {"A", "B", "C"}) writeBytes(high / "Animations" / (std::string(clip) + ".hkx"), {std::byte{1}});
    std::filesystem::create_directories(cleanup.root / "profiles" / "Test");
    {
        std::ofstream ini(cleanup.root / "ModOrganizer.ini", std::ios::binary);
        ini << "selected_profile=Test\ngamePath=" << (cleanup.root / "Game").string() << "\n";
        std::ofstream modlist(cleanup.root / "profiles" / "Test" / "modlist.txt", std::ios::binary);
        modlist << "+High\n";
        std::ofstream plugins(cleanup.root / "profiles" / "Test" / "plugins.txt", std::ios::binary);
        REQUIRE(ini.good());
        REQUIRE(modlist.good());
        REQUIRE(plugins.good());
    }

    const auto layered = target.invoke("ProjectChain", nlohmann::json::array({project, "", cleanup.root.string(), "Test"}));
    REQUIRE(layered.has_value());
    CHECK((*layered)["problems"].empty());
    CHECK((*layered)["ok"].get<bool>());
    const auto checked = target.invoke("ProjectCheck", nlohmann::json::array({project, "", cleanup.root.string(), "", 100}));
    REQUIRE(checked.has_value());
    CHECK((*checked)["errors"].get<std::size_t>() == 0u);
    CHECK((*checked)["ok"].get<bool>());
    CHECK((*checked)["complete"].get<bool>());
    bool sawUndeclared = false;
    for (const auto& finding : (*checked)["findings"]) {
        sawUndeclared = sawUndeclared || finding["code"].get<std::string>() == "chain.clip_animation_undeclared";
    }
    CHECK(sawUndeclared);
    CHECK((*checked)["round_trip_losses"].get<std::size_t>() == 0u);
    CHECK((*checked)["report"]["files"][0]["round_trip_losses"].empty());

    const auto csv = target.invoke("ProjectCheck", nlohmann::json::array({project, "", cleanup.root.string(), "", 100, "csv"}));
    REQUIRE(csv.has_value());
    CHECK((*csv)["format"].get<std::string>() == "csv");
    CHECK((*csv)["rendered"].get<std::string>().rfind("project,file,path,status,severity,object_id,where,message,blocks_save\n", 0) == 0u);
    const auto html = target.invoke("ProjectCheck", nlohmann::json::array({project, "", cleanup.root.string(), "", 100, "html"}));
    REQUIRE(html.has_value());
    CHECK((*html)["rendered"].get<std::string>().rfind("<!doctype html>", 0) == 0u);
    const auto badFormat = target.invoke("ProjectCheck", nlohmann::json::array({project, "", cleanup.root.string(), "", 100, "xml"}));
    REQUIRE_FALSE(badFormat.has_value());
    CHECK(badFormat.error().type == "InvalidFormat");

    writeBytes(high / "Behaviors" / "Extra.hkx", behavior_graph_packfile());
    const auto limited = target.invoke("ProjectCheck", nlohmann::json::array({project, "", cleanup.root.string(), "", 1}));
    REQUIRE(limited.has_value());
    CHECK((*limited)["truncated"].get<bool>());
    CHECK_FALSE((*limited)["complete"].get<bool>());
    CHECK((*limited)["errors"].get<std::size_t>() == 0u);
    CHECK_FALSE((*limited)["ok"].get<bool>());

    const auto stray = target.invoke("ProjectChain", nlohmann::json::array({project, data.string(), "", "Test"}));
    REQUIRE_FALSE(stray.has_value());
    CHECK(stray.error().type == "InvalidMo2");
    const auto wrongProfile = target.invoke("ProjectChain", nlohmann::json::array({project, "", cleanup.root.string(), "Nope"}));
    REQUIRE_FALSE(wrongProfile.has_value());
    CHECK(wrongProfile.error().type == "InvalidSources");
    const auto noRoot = target.invoke("ProjectChain", nlohmann::json::array({project, "", "", ""}));
    CHECK_FALSE(noRoot.has_value());
    const auto climbing = target.invoke("ProjectChain", nlohmann::json::array({project, "", cleanup.root.string(), "..\\Test"}));
    REQUIRE_FALSE(climbing.has_value());
    CHECK(climbing.error().type == "InvalidSources");

    const Cleanup bare{tempRoot()};
    std::filesystem::create_directories(bare.root / "profiles" / "Test");
    {
        std::ofstream ini(bare.root / "ModOrganizer.ini", std::ios::binary);
        ini << "selected_profile=Test\ngamePath=" << (bare.root / "Game").string() << "\n";
        std::ofstream modlist(bare.root / "profiles" / "Test" / "modlist.txt", std::ios::binary);
        modlist << "+High\n";
        std::ofstream plugins(bare.root / "profiles" / "Test" / "plugins.txt", std::ios::binary);
        REQUIRE(ini.good());
        REQUIRE(plugins.good());
    }
    const auto noData = target.invoke("ProjectChain", nlohmann::json::array({project, "", bare.root.string(), ""}));
    REQUIRE_FALSE(noData.has_value());
    CHECK(noData.error().type == "InvalidSources");
}

TEST_CASE("HkxTarget SubgraphManifest reads a manifest from an MO2 mod over the Data folder") {
    // Constraint: detects the crash lookup with an MO2 profile ignoring the mods, so a modded manifest reads as absent.
    const Cleanup cleanup{tempRoot()};
    const auto manifestFolder = std::filesystem::path("Meshes") / "AnimTextData" / "AnimationFileData";
    const auto high = cleanup.root / "mods" / "High" / manifestFolder;
    std::filesystem::create_directories(high);
    std::filesystem::create_directories(cleanup.root / "Game" / "Data");
    std::filesystem::create_directories(cleanup.root / "overwrite");
    std::filesystem::create_directories(cleanup.root / "profiles" / "Test");
    {
        std::ofstream manifest(high / (std::to_string(kMtBehaviorSubgraph) + ".txt"), std::ios::binary);
        manifest << "V4\n0\n" << kMtBehaviorSubgraph << "\n0\n" << kMtBehavior << "\n";
        std::ofstream ini(cleanup.root / "ModOrganizer.ini", std::ios::binary);
        ini << "selected_profile=Test\ngamePath=" << (cleanup.root / "Game").string() << "\n";
        std::ofstream modlist(cleanup.root / "profiles" / "Test" / "modlist.txt", std::ios::binary);
        modlist << "+High\n";
        std::ofstream plugins(cleanup.root / "profiles" / "Test" / "plugins.txt", std::ios::binary);
        REQUIRE(manifest.good());
        REQUIRE(plugins.good());
    }

    HkxService service;
    HkxTarget target(service);
    const auto found = target.invoke("SubgraphManifest", nlohmann::json::array(
        {std::to_string(kMtBehaviorSubgraph), "", nlohmann::json::array(), "", cleanup.root.string(), "Test"}));
    REQUIRE(found.has_value());
    REQUIRE_FALSE((*found)["manifest"].is_null());
    CHECK((*found)["manifest"]["source"].get<std::string>() == "loose");

    const auto dataOnly = target.invoke("SubgraphManifest", nlohmann::json::array(
        {std::to_string(kMtBehaviorSubgraph), "", nlohmann::json::array(), (cleanup.root / "Game" / "Data").string()}));
    REQUIRE(dataOnly.has_value());
    CHECK((*dataOnly)["manifest"].is_null());
}

TEST_CASE("HkxTarget SubgraphManifest with an MO2 instance reads a given Data folder instead of the instance's") {
    // Constraint: detects data_folder being ignored or merged when an MO2 instance is also given, or a missing data_folder being skipped.
    const Cleanup cleanup{tempRoot()};
    const auto manifestFolder = std::filesystem::path("Meshes") / "AnimTextData" / "AnimationFileData";
    const auto altData = cleanup.root / "Alt" / "Data";
    std::filesystem::create_directories(altData / manifestFolder);
    std::filesystem::create_directories(cleanup.root / "Game" / "Data");
    std::filesystem::create_directories(cleanup.root / "mods" / "High");
    std::filesystem::create_directories(cleanup.root / "overwrite");
    std::filesystem::create_directories(cleanup.root / "profiles" / "Test");
    {
        std::ofstream manifest(altData / manifestFolder / (std::to_string(kMtBehaviorSubgraph) + ".txt"), std::ios::binary);
        manifest << "V4\n0\n" << kMtBehaviorSubgraph << "\n0\n" << kMtBehavior << "\n";
        std::ofstream ini(cleanup.root / "ModOrganizer.ini", std::ios::binary);
        ini << "selected_profile=Test\ngamePath=" << (cleanup.root / "Game").string() << "\n";
        std::ofstream modlist(cleanup.root / "profiles" / "Test" / "modlist.txt", std::ios::binary);
        modlist << "+High\n";
        std::ofstream plugins(cleanup.root / "profiles" / "Test" / "plugins.txt", std::ios::binary);
        REQUIRE(manifest.good());
        REQUIRE(plugins.good());
    }

    HkxService service;
    HkxTarget target(service);
    const auto id = std::to_string(kMtBehaviorSubgraph);
    const auto instanceOnly = target.invoke("SubgraphManifest", nlohmann::json::array(
        {id, "", nlohmann::json::array(), "", cleanup.root.string(), "Test"}));
    REQUIRE(instanceOnly.has_value());
    CHECK((*instanceOnly)["manifest"].is_null());

    const auto replaced = target.invoke("SubgraphManifest", nlohmann::json::array(
        {id, "", nlohmann::json::array(), altData.string(), cleanup.root.string(), "Test"}));
    REQUIRE(replaced.has_value());
    REQUIRE_FALSE((*replaced)["manifest"].is_null());
    CHECK((*replaced)["manifest"]["source"].get<std::string>() == "loose");

    const auto missing = target.invoke("SubgraphManifest", nlohmann::json::array(
        {id, "", nlohmann::json::array(), (cleanup.root / "NoSuchData").string(), cleanup.root.string(), "Test"}));
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().type == "InvalidSources");
}

TEST_CASE("HkxTarget InspectObject, ObjectTree and PackfileDiagnostics answer for one object id and a refused header") {
    // Constraint: detects an object lookup that loses the incoming pointer, an id past the list that is not refused by name, and a broken header that errors instead of being diagnosed.
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    HkxService service;
    HkxTarget target(service);
    const auto file = cleanup.root / "rig.hkx";
    writeBytes(file, objectGraphPackfile());

    const auto bone = target.invoke("InspectObject", nlohmann::json::array({file.string(), 1}));
    REQUIRE(bone.has_value());
    CHECK((*bone)["object"].get<std::size_t>() == 1u);
    CHECK((*bone)["class_name"].get<std::string>() == "hkaBone");
    CHECK((*bone)["fields"].size() > 0u);
    bool sawLock = false;
    for (const auto& field : (*bone)["fields"]) {
        if (field["name"].get<std::string>() != "lockTranslation") continue;
        sawLock = true;
        CHECK(field["type"].get<std::string>() == "bool");
        CHECK(field["value"].get<std::string>() == "false");
    }
    CHECK(sawLock);
    CHECK_FALSE((*bone)["partial"].get<bool>());
    REQUIRE((*bone)["incoming"].size() == 1u);
    CHECK((*bone)["incoming"][0]["member"].get<std::string>() == "bones");
    CHECK((*bone)["incoming"][0]["source_object"].get<int>() == 0);
    CHECK((*bone)["outgoing_total"].get<std::size_t>() == 0u);

    const auto past = target.invoke("InspectObject", nlohmann::json::array({file.string(), 9}));
    REQUIRE_FALSE(past.has_value());
    CHECK(past.error().type == "ObjectNotFound");
    const auto negative = target.invoke("InspectObject", nlohmann::json::array({file.string(), -1}));
    REQUIRE_FALSE(negative.has_value());
    CHECK(negative.error().type == "InvalidObject");

    const auto triggerReply = [&](std::uint64_t payload, const char* name) {
        BehaviorFixture fixture;
        const std::size_t at = fixture.add("hkbClipTrigger");
        fixture.set<std::uint64_t>(at + member_at("hkbClipTrigger", "event") + member_at("hkbEventBase", "payload"), payload);
        const auto triggerFile = cleanup.root / name;
        writeBytes(triggerFile, fixture.bytes());
        return target.invoke("InspectObject", nlohmann::json::array({triggerFile.string(), 0}));
    };
    const auto cleanStruct = triggerReply(0, "trigger_clean.hkx");
    REQUIRE(cleanStruct.has_value());
    CHECK_FALSE((*cleanStruct)["partial"].get<bool>());
    const auto partialStruct = triggerReply(1, "trigger_partial.hkx");
    REQUIRE(partialStruct.has_value());
    CHECK((*partialStruct)["partial"].get<bool>());
    bool sawEvent = false;
    for (const auto& field : (*partialStruct)["fields"]) {
        if (field["name"].get<std::string>() != "event") continue;
        sawEvent = true;
        CHECK(field["value"].get<std::string>() == "{id=0, payload=?}");
        CHECK_FALSE(field["reason"].get<std::string>().empty());
    }
    CHECK(sawEvent);

    const auto tree = target.invoke("ObjectTree", nlohmann::json::array({file.string()}));
    REQUIRE(tree.has_value());
    REQUIRE((*tree)["nodes"].size() == 2u);
    CHECK((*tree)["nodes"][0]["class_name"].get<std::string>() == "hkaSkeleton");
    CHECK((*tree)["nodes"][1]["member"].get<std::string>() == "bones");
    CHECK((*tree)["nodes"][1]["parent"].get<int>() == 0);
    CHECK((*tree)["nodes"][1]["summary"].is_string());
    CHECK((*tree)["unreachable"].get<std::size_t>() == 0u);

    const auto healthy = target.invoke("PackfileDiagnostics", nlohmann::json::array({file.string()}));
    REQUIRE(healthy.has_value());
    CHECK((*healthy)["readable"].get<bool>());
    CHECK((*healthy)["classes"].size() == 2u);
    CHECK((*healthy)["issues"].empty());

    const auto broken = cleanup.root / "broken.hkx";
    writeBytes(broken, std::vector<std::byte>(64, std::byte{1}));
    const auto refused = target.invoke("PackfileDiagnostics", nlohmann::json::array({broken.string()}));
    REQUIRE(refused.has_value());
    CHECK_FALSE((*refused)["readable"].get<bool>());
    CHECK((*refused)["issues"].size() == 1u);
}

TEST_CASE("HkxTarget RepackCheck reports a clean round trip, an unreadable file and a missing file") {
    // Constraint: detects a repack tool that reports an unreadable header as equal, fails the call instead of replying read_failed, or drops the verdict a UI reads.
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    HkxService service;
    HkxTarget target(service);
    const auto graph = cleanup.root / "graph.hkx";
    writeBytes(graph, behavior_graph_packfile());

    const auto equal = target.invoke("RepackCheck", nlohmann::json::array({graph.string()}));
    REQUIRE(equal.has_value());
    CHECK((*equal)["verdict"].get<std::string>() == "equal");
    CHECK((*equal)["model_equal"].get<bool>());
    CHECK((*equal)["first_difference"].get<std::string>().empty());
    CHECK((*equal)["source_size"].get<std::size_t>() == std::filesystem::file_size(graph));

    const auto broken = cleanup.root / "broken.hkx";
    writeBytes(broken, std::vector<std::byte>(64, std::byte{1}));
    const auto unreadable = target.invoke("RepackCheck", nlohmann::json::array({broken.string()}));
    REQUIRE(unreadable.has_value());
    CHECK((*unreadable)["verdict"].get<std::string>() == "read_failed");
    CHECK_FALSE((*unreadable)["model_equal"].get<bool>());
    CHECK((*unreadable)["status"].get<std::string>() != "Ok");

    const auto missing = target.invoke("RepackCheck", nlohmann::json::array({(cleanup.root / "none.hkx").string()}));
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().type == "FileNotFound");
}

TEST_CASE("HkxTarget SearchHkx finds symbols across a folder and counts the file it could not read") {
    // Constraint: detects a search that matches only file names, drops an unreadable file without counting it, or lists everything for an empty query.
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root / "anim");
    HkxService service;
    HkxTarget target(service);
    writeBytes(cleanup.root / "anim" / "walk.hkx", behavior_graph_packfile());
    writeBytes(cleanup.root / "rig.hkx", skeletonPackfile());
    writeBytes(cleanup.root / "broken.hkx", std::vector<std::byte>{std::byte{1}, std::byte{2}});

    const auto event = target.invoke("SearchHkx", nlohmann::json::array({cleanup.root.string(), "wave", 100}));
    REQUIRE(event.has_value());
    CHECK((*event)["skipped"].get<std::size_t>() == 1u);
    bool sawEvent = false;
    for (const auto& hit : (*event)["hits"]) {
        if (hit["kind"].get<std::string>() == "event" && hit["text"].get<std::string>() == "Wave" &&
            hit["file"].get<std::string>().find("walk.hkx") != std::string::npos) {
            sawEvent = true;
        }
    }
    CHECK(sawEvent);

    const auto value = target.invoke("SearchHkx", nlohmann::json::array({cleanup.root.string(), "ClipA", 100}));
    REQUIRE(value.has_value());
    bool sawValue = false;
    for (const auto& hit : (*value)["hits"]) {
        if (hit["kind"].get<std::string>() == "name" && hit["text"].get<std::string>() == "hkbClipGenerator.name = ClipA") {
            sawValue = true;
        }
    }
    CHECK(sawValue);

    const auto bone = target.invoke("SearchHkx", nlohmann::json::array({cleanup.root.string(), "head", 100}));
    REQUIRE(bone.has_value());
    bool sawBone = false;
    for (const auto& hit : (*bone)["hits"]) {
        if (hit["kind"].get<std::string>() == "name" && hit["text"].get<std::string>() == "Head" &&
            hit["file"].get<std::string>().find("rig.hkx") != std::string::npos) {
            sawBone = true;
        }
    }
    CHECK(sawBone);

    const auto byName = target.invoke("SearchHkx", nlohmann::json::array({cleanup.root.string(), "walk", 100}));
    REQUIRE(byName.has_value());
    REQUIRE((*byName)["hits"].size() >= 1u);
    CHECK((*byName)["hits"][0]["kind"].get<std::string>() == "file");

    const auto none = target.invoke("SearchHkx", nlohmann::json::array({cleanup.root.string(), "", 100}));
    REQUIRE(none.has_value());
    CHECK((*none)["hits"].empty());

    const auto capped = target.invoke("SearchHkx", nlohmann::json::array({cleanup.root.string(), "a", 1}));
    REQUIRE(capped.has_value());
    CHECK((*capped)["hits"].size() == 1u);
    CHECK((*capped)["truncated"].get<bool>());

    CHECK_FALSE(target.invoke("SearchHkx", nlohmann::json::array({(cleanup.root / "nope").string(), "a", 10})).has_value());
}

TEST_CASE("HkxTarget AuditChannels audits a folder of clips and lists the files it cannot pose") {
    // Constraint: detects a sweep that silently drops a clip it cannot pose or audits a non-animation file as an empty clip.
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root / "clips");
    HkxService service;
    HkxTarget target(service);
    writeBytes(cleanup.root / "rig.hkx", skeletonPackfile());
    writeBytes(cleanup.root / "clips" / "a.hkx", clipPackfile());
    writeBytes(cleanup.root / "clips" / "behavior.hkx", behaviorPackfile());
    writeBytes(cleanup.root / "clips" / "broken.hkx", std::vector<std::byte>{std::byte{1}, std::byte{2}});

    const auto sweep = target.invoke("AuditChannels", nlohmann::json::array({(cleanup.root / "rig.hkx").string(),
                                                                             (cleanup.root / "clips").string()}));
    REQUIRE(sweep.has_value());
    CHECK((*sweep)["skeleton"].get<std::string>() == "Rig");
    CHECK((*sweep)["file_count"].get<std::size_t>() == 3u);
    CHECK((*sweep)["audited"].get<std::size_t>() == 1u);
    CHECK((*sweep)["skipped"].get<std::size_t>() == 2u);
    REQUIRE((*sweep)["files"].size() == 1u);
    CHECK((*sweep)["files"][0]["tracks"].get<std::size_t>() == kTracks);
    CHECK((*sweep)["files"][0]["offset_total"].get<std::size_t>() == 0u);
    CHECK((*sweep)["skipped_files"].size() == 2u);

    const auto single = target.invoke("AuditChannels", nlohmann::json::array({(cleanup.root / "rig.hkx").string(),
                                                                              (cleanup.root / "clips" / "a.hkx").string()}));
    REQUIRE(single.has_value());
    CHECK((*single)["audited"].get<std::size_t>() == 1u);

    CHECK_FALSE(target.invoke("AuditChannels", nlohmann::json::array({(cleanup.root / "none.hkx").string(),
                                                                      (cleanup.root / "clips").string()})).has_value());
}

TEST_CASE("HkxTarget SymbolUsage lists event, variable and state use and refuses a file with no behavior graph") {
    // Constraint: detects usage rows that drop an event's second site, lose a state's incoming transition count, or call a skeleton a behavior graph.
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    HkxService service;
    HkxTarget target(service);
    writeBytes(cleanup.root / "graph.hkx", behavior_graph_packfile());
    writeBytes(cleanup.root / "rig.hkx", skeletonPackfile());

    const auto usage = target.invoke("SymbolUsage", nlohmann::json::array({(cleanup.root / "graph.hkx").string()}));
    REQUIRE(usage.has_value());
    CHECK((*usage)["partial"].get<bool>());
    REQUIRE((*usage)["events"].size() >= 3u);
    CHECK((*usage)["events"][2]["name"].get<std::string>() == "Wave");
    CHECK((*usage)["events"][2]["site_total"].get<std::size_t>() >= 2u);
    REQUIRE((*usage)["states"].size() == 2u);
    CHECK((*usage)["states"][0]["is_start"].get<bool>());
    CHECK((*usage)["states"][1]["incoming"].get<std::size_t>() == 1u);
    CHECK((*usage)["variables"][1]["site_total"].get<std::size_t>() == 1u);

    CHECK_FALSE(target.invoke("SymbolUsage", nlohmann::json::array({(cleanup.root / "rig.hkx").string()})).has_value());
}

namespace {

std::vector<std::byte> versionedPackfile() {
    auto bytes = single_object_packfile("hkaNotAFallout4Class", 0x12345678u, filled(32, std::byte{0}), {});
    const std::string version = "hk_2014.1.0-r1";
    for (std::size_t i = 0; i < version.size(); ++i) bytes[0x28 + i] = static_cast<std::byte>(version[i]);
    return bytes;
}

std::vector<char> fileText(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

}  // namespace

// Regression: a ConvertAsset preview must write nothing, a write must land only at a distinct new path, and the source must never change.
TEST_CASE("HkxTarget ConvertAsset previews, writes a distinct file and never touches the source") {
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    const auto source = cleanup.root / "source.hkx";
    const auto out = cleanup.root / "converted.hkx";
    writeBytes(source, versionedPackfile());
    const auto before = fileText(source);
    HkxService service;
    HkxTarget target(service);

    const auto preview = target.invoke("ConvertAsset", nlohmann::json::array({source.string(), "", "", "", false}));
    REQUIRE(preview.has_value());
    CHECK((*preview)["ok"].get<bool>());
    CHECK((*preview)["saved"].get<bool>() == false);
    CHECK((*preview)["status"].get<std::string>() == "ok");
    CHECK((*preview)["source_version"].get<std::string>() == "hk_2014.1.0-r1");
    CHECK((*preview)["changed_total"].get<std::size_t>() == 0u);
    REQUIRE((*preview)["opaque_classes"].size() == 1u);
    CHECK((*preview)["opaque_classes"][0].get<std::string>() == "hkaNotAFallout4Class");
    CHECK_FALSE(std::filesystem::exists(out));

    const auto saved = target.invoke(
        "ConvertAsset", nlohmann::json::array({source.string(), "hk_2014.1.0-r1", out.string(), (*preview)["source"], false}));
    REQUIRE(saved.has_value());
    CHECK((*saved)["saved"].get<bool>());
    CHECK(fileText(out) == before);
    CHECK(fileText(source) == before);

    const auto again = target.invoke("ConvertAsset", nlohmann::json::array({source.string(), "", out.string(), "", false}));
    REQUIRE_FALSE(again.has_value());
    CHECK(again.error().type == "OutputExists");

    const auto inPlace = target.invoke("ConvertAsset", nlohmann::json::array({source.string(), "", source.string(), "", true}));
    REQUIRE_FALSE(inPlace.has_value());
    CHECK(inPlace.error().type == "HkxConvertInPlace");
    CHECK(fileText(source) == before);
}

// Regression: an unsupported version pair, a stale source or a wrong target name must report a reason and leave no file.
TEST_CASE("HkxTarget ConvertAsset refuses an unsupported pair, a stale source and a bad target without writing a file") {
    const Cleanup cleanup{tempRoot()};
    std::filesystem::create_directories(cleanup.root);
    const auto source = cleanup.root / "source.hkx";
    const auto out = cleanup.root / "converted.hkx";
    writeBytes(source, versionedPackfile());
    HkxService service;
    HkxTarget target(service);

    const auto preview = target.invoke("ConvertAsset", nlohmann::json::array({source.string(), "hk_2012.2.1-r1", "", "", false}));
    REQUIRE(preview.has_value());
    CHECK_FALSE((*preview)["ok"].get<bool>());
    CHECK((*preview)["status"].get<std::string>() == "unsupported-pair");
    CHECK((*preview)["reason"].get<std::string>().find("hk_2012.2.1-r1") != std::string::npos);

    const auto refused = target.invoke(
        "ConvertAsset", nlohmann::json::array({source.string(), "hk_2012.2.1-r1", out.string(), "", false}));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "HkxConvertRefused");
    CHECK_FALSE(std::filesystem::exists(out));

    const auto stale = target.invoke("ConvertAsset", nlohmann::json::array({source.string(), "", out.string(), "00", false}));
    REQUIRE_FALSE(stale.has_value());
    CHECK(stale.error().type == "HkxChanged");
    CHECK_FALSE(std::filesystem::exists(out));

    const auto wrongName = target.invoke(
        "ConvertAsset", nlohmann::json::array({source.string(), "", (cleanup.root / "converted.txt").string(), "", false}));
    REQUIRE_FALSE(wrongName.has_value());
    CHECK(wrongName.error().type == "InvalidPath");

    writeBytes(cleanup.root / "junk.hkx", filled(64, std::byte{0}));
    const auto junk = target.invoke("ConvertAsset", nlohmann::json::array({(cleanup.root / "junk.hkx").string(), "", "", "", false}));
    REQUIRE(junk.has_value());
    CHECK((*junk)["status"].get<std::string>() == "unreadable");
    CHECK_FALSE((*junk)["ok"].get<bool>());
}
