#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/app/ServiceTargets.hpp>
#include <fo4ide/hkx/HkxService.hpp>

#include "hkx_animation_fixture.hpp"
#include "hkx_test_fixture.hpp"

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

void putTransform(std::vector<std::byte>& data, const std::array<float, 3>& translation) {
    for (float value : translation) put_float(data, value);
    put_float(data, 0.0f);
    for (float value : {0.0f, 0.0f, 0.0f, 1.0f}) put_float(data, value);
    for (float value : {1.0f, 1.0f, 1.0f}) put_float(data, value);
    put_float(data, 0.0f);
}

std::vector<std::byte> skeletonPackfile() {
    const std::vector<std::string> names{"Root", "Spine", "Head"};
    const std::vector<std::int16_t> parents{-1, 0, 1};
    const std::vector<std::array<float, 3>> pose{{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}};

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

    link(local, member_at("hkaSkeleton", "name"), appendString(data, "Rig"));
    link(local, member_at("hkaSkeleton", "parentIndices"), parentsAt);
    set_count(data, member_at("hkaSkeleton", "parentIndices"), parents.size());
    link(local, member_at("hkaSkeleton", "bones"), bonesAt);
    set_count(data, member_at("hkaSkeleton", "bones"), names.size());
    link(local, member_at("hkaSkeleton", "referencePose"), poseAt);
    set_count(data, member_at("hkaSkeleton", "referencePose"), pose.size());
    return single_object_packfile("hkaSkeleton", signature_of("hkaSkeleton"), std::move(data), std::move(local));
}

std::vector<std::byte> clipPackfile() {
    std::vector<std::byte> names;
    const auto clipName = add_class_name(names, signature_of("hkaInterleavedUncompressedAnimation"),
                                         "hkaInterleavedUncompressedAnimation");
    const auto bindingName = add_class_name(names, signature_of("hkaAnimationBinding"), "hkaAnimationBinding");

    std::vector<std::byte> data(fo4::hkx::find_layout("hkaInterleavedUncompressedAnimation")->size, std::byte{0});
    const std::size_t bindingAt = data.size();
    data.resize(bindingAt + fo4::hkx::find_layout("hkaAnimationBinding")->size, std::byte{0});
    std::vector<std::uint32_t> local;

    set_float_member(data, "hkaAnimation", "duration", 1.0f);
    set_int_member(data, "hkaAnimation", "numberOfTransformTracks", static_cast<std::uint32_t>(kTracks));
    const std::size_t transformsAt = data.size();
    const std::array<std::array<float, 3>, kTracks * kFrames> samples{{
        {1.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {2.0f, 0.0f, 0.0f},
        {3.0f, 0.0f, 0.0f},
    }};
    for (const auto& translation : samples) putTransform(data, translation);
    link(local, member_at("hkaInterleavedUncompressedAnimation", "transforms"), transformsAt);
    set_count(data, member_at("hkaInterleavedUncompressedAnimation", "transforms"), kTracks * kFrames);

    const std::size_t bonesAt = data.size();
    for (std::int16_t bone : {std::int16_t{1}, std::int16_t{2}}) {
        const auto bits = static_cast<std::uint16_t>(bone);
        data.push_back(static_cast<std::byte>(bits & 0xFF));
        data.push_back(static_cast<std::byte>(bits >> 8));
    }
    link(local, bindingAt + member_at("hkaAnimationBinding", "transformTrackToBoneIndices"), bonesAt);
    set_count(data, bindingAt + member_at("hkaAnimationBinding", "transformTrackToBoneIndices"), kTracks);

    return build_packfile({
        TestSection{"__classnames__", names, {}, {}, {}},
        TestSection{"__types__", {}, {}, {}, {}},
        TestSection{"__data__", std::move(data), std::move(local), {},
                    {0, 0, clipName, static_cast<std::uint32_t>(bindingAt), 0, bindingName}},
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
