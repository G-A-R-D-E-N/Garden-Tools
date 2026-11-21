#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/app/ServiceTargets.hpp>
#include <fo4ide/hkx/HkxService.hpp>

#include "fo4/hkx/layout.hpp"
#include "hkx_animation_fixture.hpp"
#include "hkx_behavior_graph_fixture.hpp"
#include "hkx_physics_fixture.hpp"
#include "hkx_test_fixture.hpp"
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

}  // namespace

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

    const auto cut = target.invoke("ReadRagdoll", nlohmann::json::array({file.string(), 1}));
    REQUIRE(cut.has_value());
    CHECK((*cut)["bodies"].size() == 1u);
    CHECK((*cut)["body_total"].get<std::size_t>() == 2u);
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
