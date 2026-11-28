#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/archive/assets.hpp>
#include <fo4/archive/ba2.hpp>
#include <fo4ide/app/ServiceTargets.hpp>
#include <fo4ide/assets/NifService.hpp>
#include <fo4ide/assets/TextureResolver.hpp>
#include <fo4ide/plugin/IPluginBackend.hpp>

#include "../helpers/NifFixture.hpp"
#include "../helpers/ServiceTargetFakes.hpp"
#include "hkx_test_fixture.hpp"
#include "nif_fixture.hpp"

using namespace fo4ide::app::test;

namespace {

std::string fileBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

std::vector<std::byte> withLatin1ShapeName(std::vector<std::byte> bytes) {
    const std::vector<std::byte> entry{std::byte{3}, std::byte{0}, std::byte{0}, std::byte{0},
                                       std::byte{'T'}, std::byte{'r'}, std::byte{'i'}};
    const auto at = std::search(bytes.begin(), bytes.end(), entry.begin(), entry.end());
    REQUIRE(at != bytes.end());
    *(at + 6) = std::byte{0xE9};
    return bytes;
}

void writeLoose(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << text;
}

void writePacked(const std::filesystem::path& path, std::string_view inner, std::string_view text) {
    std::vector<std::byte> bytes;
    for (const unsigned char ch : text) bytes.push_back(static_cast<std::byte>(ch));
    fo4::archive::Ba2GeneralArchive archive;
    fo4::archive::Ba2GeneralEntry entry;
    entry.path = std::string(inner);
    entry.chunks.push_back(fo4::archive::make_stored_ba2_chunk(bytes));
    archive.entries.push_back(std::move(entry));
    std::filesystem::create_directories(path.parent_path());
    fo4::archive::write_ba2_general_file(archive, path);
}

nlohmann::json resolveAssetOver(const std::vector<std::filesystem::path>& roots,
                                const std::filesystem::path& cache,
                                nlohmann::json args) {
    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures(cache);
    textures.setSessionRoots(roots);
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::app::NifTarget target(service, textures, platform, plugins);
    auto reply = target.invoke("ResolveAsset", std::move(args));
    if (!reply) INFO(reply.error().type + ": " + reply.error().message);
    REQUIRE(reply.has_value());
    return nlohmann::json::parse(reply->get<std::string>());
}

}  // namespace

TEST_CASE("NifTarget UvGet and VertexColorGet replace a non-UTF-8 shape name instead of failing with NifParseFailed") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto uv = root / "uv.nif";
    const auto colors = root / "colors.nif";
    fo4ide::test::writeBytes(uv, withLatin1ShapeName(fo4ide::test::dualUvTriangleNif()));
    fo4ide::test::writeBytes(colors, withLatin1ShapeName(fo4ide::test::coloredLightingNif()));

    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::app::NifTarget target(service, textures, platform, plugins);

    const std::vector<std::pair<std::string, std::filesystem::path>> reads{
        {"UvGet", uv}, {"VertexColorGet", colors}};
    for (const auto& [method, path] : reads) {
        auto reply = target.invoke(method, nlohmann::json::array({path.string()}));
        REQUIRE(reply.has_value());
        const auto json = nlohmann::json::parse(reply->get<std::string>());
        REQUIRE_FALSE(json.contains("error"));
        CHECK(json.at("shapes")[0].at("name").get<std::string>() == "Tr\xEF\xBF\xBD");
    }
}

TEST_CASE("NifTarget ApplyEdits refuses a save whose NIF changed on disk after Tree") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "tree.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::treeNif("Materials\\Clutter\\Thing.BGSM", {}));
    const auto edits = nlohmann::json::array({{{"id", 1}, {"cat", "shape"}, {"key", "name"}, {"value", "Renamed"}}});

    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::app::NifTarget target(service, textures, platform, plugins);

    auto tree = target.invoke("Tree", nlohmann::json::array({nif.string()}));
    REQUIRE(tree.has_value());
    const auto source = nlohmann::json::parse(tree->get<std::string>()).at("source").get<std::string>();
    fo4ide::test::writeBytes(nif, fo4ide::test::treeNif("Materials\\Clutter\\Other.BGSM", {}));

    auto saved = target.invoke("ApplyEdits", nlohmann::json::array({nif.string(), edits.dump(), "", source}));
    REQUIRE(saved.has_value());
    CHECK(saved->get<std::string>().find("changed on disk") != std::string::npos);
    CHECK(saved->get<std::string>().find("set OK") == std::string::npos);
}

TEST_CASE("NifTarget ShapeOp replies EXISTS for an existing output until overwrite is passed") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "input.nif";
    const auto output = root / "output.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::treeNif("Materials\\Clutter\\Thing.BGSM", {}));
    touch(output, "old");

    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::app::NifTarget target(service, textures, platform, plugins);

    auto refused = target.invoke("ShapeOp", nlohmann::json::array({nif.string(), output.string(), "recalc_normals", "[0]", 180, false}));
    REQUIRE(refused.has_value());
    CHECK(refused->get<std::string>().starts_with("EXISTS: Refusing to replace an existing file"));
    CHECK(std::filesystem::file_size(output) == 3U);
}

TEST_CASE("NifTarget ApplyEdits writes a segment save with the Segments digest and refuses a stale one after its own save") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "seg.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::subIndexNif());

    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::app::NifTarget target(service, textures, platform, plugins);
    const auto readTable = [&] {
        auto reply = target.invoke("Segments", nlohmann::json::array({nif.string(), 0}));
        REQUIRE(reply.has_value());
        return nlohmann::json::parse(reply->get<std::string>());
    };

    auto table = readTable();
    const auto staleSource = table.at("source");
    table["primitives"] = 9;
    const auto payload = nlohmann::json::array({{{"id", 0}, {"key", "segments"}, {"value", table}}}).dump();

    auto saved = target.invoke("ApplyEdits", nlohmann::json::array({nif.string(), payload, "", staleSource}));
    REQUIRE(saved.has_value());
    CHECK(saved->get<std::string>().find("set OK") != std::string::npos);
    const auto afterSave = fileBytes(nif);

    auto refused = target.invoke("ApplyEdits", nlohmann::json::array({nif.string(), payload, "", staleSource}));
    REQUIRE(refused.has_value());
    CHECK(refused->get<std::string>().find("changed on disk") != std::string::npos);
    CHECK(fileBytes(nif) == afterSave);

    auto fresh = target.invoke("ApplyEdits", nlohmann::json::array({nif.string(), payload, "", readTable().at("source")}));
    REQUIRE(fresh.has_value());
    CHECK(fresh->get<std::string>().find("set OK") != std::string::npos);
}

TEST_CASE("NifTarget ApplyEdits writes a UV save with the Geo digest and refuses a stale one after its own save") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "uv.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::dualUvTriangleNif());

    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::app::NifTarget target(service, textures, platform, plugins);
    const auto readGeo = [&] {
        auto reply = target.invoke("Geo", nlohmann::json::array({nif.string()}));
        REQUIRE(reply.has_value());
        return nlohmann::json::parse(reply->get<std::string>());
    };

    const auto geo = readGeo();
    const auto staleSource = geo.at("source");
    const auto& shape = geo.at("shapes")[0];
    auto uvs = shape.at("uvs");
    uvs[0] = 0.5;
    const auto payload = nlohmann::json::array({{{"id", shape.at("block")}, {"key", "uvs"}, {"value", uvs}}}).dump();

    auto saved = target.invoke("ApplyEdits", nlohmann::json::array({nif.string(), payload, "", staleSource}));
    REQUIRE(saved.has_value());
    CHECK(saved->get<std::string>().find("set OK") != std::string::npos);
    const auto afterSave = fileBytes(nif);

    auto refused = target.invoke("ApplyEdits", nlohmann::json::array({nif.string(), payload, "", staleSource}));
    REQUIRE(refused.has_value());
    CHECK(refused->get<std::string>().find("changed on disk") != std::string::npos);
    CHECK(fileBytes(nif) == afterSave);

    auto fresh = target.invoke("ApplyEdits", nlohmann::json::array({nif.string(), payload, "", readGeo().at("source")}));
    REQUIRE(fresh.has_value());
    CHECK(fresh->get<std::string>().find("set OK") != std::string::npos);
}

TEST_CASE("NifTarget GeoSkinned serves the Geo reply for a rigid shape and no skin object") {
    // Constraint: detects a skin key invented for a rigid shape, or a GeoSkinned reply that drifts from Geo for it.
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "rigid.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::triangleNif());

    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::app::NifTarget target(service, textures, platform, plugins);

    auto plain = target.invoke("Geo", nlohmann::json::array({nif.string()}));
    auto skinned = target.invoke("GeoSkinned", nlohmann::json::array({nif.string()}));
    REQUIRE(plain.has_value());
    REQUIRE(skinned.has_value());
    CHECK(skinned->get<std::string>() == plain->get<std::string>());
    CHECK_FALSE(nlohmann::json::parse(skinned->get<std::string>()).at("shapes")[0].contains("skin"));
}

TEST_CASE("NifTarget VertexColorSet writes a paint with the VertexColorGet digest and refuses a stale one after its own save") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "lit_paint.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::coloredLightingNif());

    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::app::NifTarget target(service, textures, platform, plugins);
    const auto readInfo = [&] {
        auto reply = target.invoke("VertexColorGet", nlohmann::json::array({nif.string()}));
        REQUIRE(reply.has_value());
        return nlohmann::json::parse(reply->get<std::string>());
    };

    const auto info = readInfo();
    const auto block = info.at("shapes")[0].at("block");
    const auto staleSource = info.at("source");
    const auto fill = [&](const nlohmann::json& source) {
        return target.invoke("VertexColorSet", nlohmann::json::array({nif.string(), block, "fill", nullptr, nullptr, nullptr,
                                                                      nlohmann::json::array({0.0, 0.0, 0.0, 1.0}), nullptr,
                                                                      nullptr, "", source}));
    };

    auto saved = fill(staleSource);
    REQUIRE(saved.has_value());
    CHECK(saved->get<std::string>().find("vertex color OK") != std::string::npos);
    const auto afterSave = fileBytes(nif);

    auto refused = fill(staleSource);
    REQUIRE(refused.has_value());
    CHECK(refused->get<std::string>().find("changed on disk") != std::string::npos);
    CHECK(fileBytes(nif) == afterSave);

    auto fresh = fill(readInfo().at("source"));
    REQUIRE(fresh.has_value());
    CHECK(fresh->get<std::string>().find("vertex color OK") != std::string::npos);
}

namespace {

// Invariant: these four vertices mirror across X, so each negative vertex has one positive partner.
std::vector<std::byte> mirroredSkinNif(const std::array<std::uint8_t, 4>& bones = {0, 0, 1, 1},
                                       bool bonesUnderRoot = false) {
    const std::uint32_t flags = nif_fixture::k_full | nif_fixture::k_has_uv | nif_fixture::k_has_normal |
                                nif_fixture::k_has_tangent | nif_fixture::k_has_colour | nif_fixture::k_has_skin;
    constexpr std::size_t stride = 44;
    auto vertex = [](float x, float y, std::uint8_t bone) {
        auto value = nif_fixture::skinned_vertex(x, y, 0.0f, bone);
        for (int slot = 0; slot < 4; ++slot) value.weights[slot] = 0;
        value.weights[0] = 0x3C00;
        for (int slot = 0; slot < 4; ++slot) value.bones[slot] = bone;
        return nif_fixture::encode_vertex(value);
    };
    std::vector<std::byte> vertices;
    for (const auto& encoded : {vertex(1.0f, 0.0f, bones[0]), vertex(1.0f, 1.0f, bones[1]),
                                vertex(-1.0f, 0.0f, bones[2]), vertex(-1.0f, 1.0f, bones[3])}) {
        vertices.insert(vertices.end(), encoded.begin(), encoded.end());
    }
    const nif_fixture::BoneFixture identity{"", {0.0f, 0.0f, 0.0f}, 0.0f,
                                            {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f},
                                            {0.0f, 0.0f, 0.0f}, 1.0f};
    const std::vector<nif_fixture::Block> blocks{
        nif_fixture::node_block("NiNode", 0, fo4::nif::Transform{},
                                bonesUnderRoot ? std::vector<std::int32_t>{1, 3, 4, 5} : std::vector<std::int32_t>{1}),
        nif_fixture::shape_block("BSTriShape", 1, fo4::nif::Transform{}, flags, stride, 2, -1, -1, vertices,
                                 {0, 1, 2, 0, 2, 3}),
        nif_fixture::skin_instance_block(0, 6, {3, 4, 5}),
        nif_fixture::node_block("NiNode", 2, fo4::nif::Transform{}, {}),
        nif_fixture::node_block("NiNode", 3, fo4::nif::Transform{}, {}),
        nif_fixture::node_block("NiNode", 4, fo4::nif::Transform{}, {}),
        nif_fixture::bone_data_block({identity, identity, identity}),
    };
    return nif_fixture::build_nif({"root", "body", "LArm", "RArm", "Spine"}, blocks, {0});
}

// Constraint: half-float weights round by at most 2^-12, so a tolerance beats an exact comparison.
bool near(double value, double wanted, double tolerance) {
    return std::fabs(value - wanted) <= tolerance;
}

std::vector<std::byte> bytesOf(const std::filesystem::path& path) {
    const auto text = fileBytes(path);
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    return bytes;
}

}  // namespace

TEST_CASE("NifTarget SkinInfluences names one vertex's bones and SkinInfluencedBy lists a bone's vertices") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "skin.nif";
    fo4ide::test::writeBytes(nif, mirroredSkinNif());

    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::app::NifTarget target(service, textures, platform, plugins);

    auto vertexReply = target.invoke("SkinInfluences", nlohmann::json::array({nif.string(), 1, 2}));
    REQUIRE(vertexReply.has_value());
    const auto vertex = nlohmann::json::parse(vertexReply->get<std::string>());
    REQUIRE_FALSE(vertex.contains("error"));
    CHECK(vertex.at("bones").size() == 3);
    CHECK(vertex.at("bones")[0].get<std::string>() == "LArm");
    REQUIRE(vertex.at("influences").size() == 1);
    CHECK(vertex.at("influences")[0].at("bone").get<int>() == 1);
    CHECK(vertex.at("influences")[0].at("name").get<std::string>() == "RArm");
    CHECK(near(vertex.at("influences")[0].at("weight").get<double>(), 1.0, 2e-3));

    auto boneReply = target.invoke("SkinInfluencedBy", nlohmann::json::array({nif.string(), 1, 1, 0.4}));
    REQUIRE(boneReply.has_value());
    const auto bone = nlohmann::json::parse(boneReply->get<std::string>());
    REQUIRE_FALSE(bone.contains("error"));
    CHECK(bone.at("bone_name").get<std::string>() == "RArm");
    REQUIRE(bone.at("vertices").size() == 2);
    CHECK(bone.at("vertices")[0].get<int>() == 2);
    CHECK(bone.at("vertices")[1].get<int>() == 3);
    CHECK(near(bone.at("weight").get<double>(), 2.0, 4e-3));
    CHECK(near(bone.at("min")[0].get<double>(), -1.0, 2e-3));
    CHECK(near(bone.at("max")[1].get<double>(), 1.0, 2e-3));
}

TEST_CASE("NifTarget SkinEdit mirrors the positive side's weights and writes the changed NIF") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "skin.nif";
    const auto out = root / "mirrored.nif";
    fo4ide::test::writeBytes(nif, mirroredSkinNif());

    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::app::NifTarget target(service, textures, platform, plugins);

    nlohmann::json seeded = nlohmann::json::array();
    seeded.push_back({{"bone", 0}, {"weight", 1.0}});
    auto seedWrite = target.invoke("SkinEdit", nlohmann::json::array(
        {nif.string(), std::string(), "set_influences", 1, 2, seeded, nullptr, nullptr, false, 0, true, false}));
    REQUIRE(seedWrite.has_value());
    REQUIRE(seedWrite->get<std::string>().find("OK") != std::string::npos);

    auto saved = target.invoke("SkinEdit", nlohmann::json::array(
        {nif.string(), out.string(), "mirror_weights", 1, nullptr, nullptr, nullptr, nullptr, false, 0, true, false}));
    REQUIRE(saved.has_value());
    const auto text = saved->get<std::string>();
    CHECK(text.find("RESULT: skin edit mirror_weights OK") != std::string::npos);
    CHECK(text.find("Mirrored: 2") != std::string::npos);
    CHECK(text.find("no partner bone 0") != std::string::npos);

    const auto shape = fo4::nif::read_shape(fo4::nif::read_nif(bytesOf(out)), 1);
    REQUIRE(shape.bone_indices.size() == 16);
    CHECK(shape.bone_indices[2 * 4] == 1);
    CHECK(shape.bone_indices[3 * 4] == 1);
    CHECK(near(shape.weights[2 * 4], 1.0, 2e-3));
    CHECK(near(shape.weights[3 * 4], 1.0, 2e-3));
}

TEST_CASE("NifTarget SkinEdit refuses an unskinned shape and a duplicate bone on one vertex") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "skin.nif";
    const auto plain = root / "plain.nif";
    fo4ide::test::writeBytes(nif, mirroredSkinNif());
    fo4ide::test::writeBytes(plain, fo4ide::test::dualUvTriangleNif());

    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::app::NifTarget target(service, textures, platform, plugins);

    const auto out = root / "out.nif";
    auto skinless = target.invoke("SkinEdit", nlohmann::json::array(
        {plain.string(), out.string(), "copy_bone", 0, nullptr, nullptr, 0, 1, false, 0, true, false}));
    REQUIRE(skinless.has_value());
    CHECK(skinless->get<std::string>().find("has no skin") != std::string::npos);

    const nlohmann::json duplicate = {{{"bone", 0}, {"weight", 0.5}}, {{"bone", 0}, {"weight", 0.5}}};
    auto repeated = target.invoke("SkinEdit", nlohmann::json::array(
        {nif.string(), out.string(), "set_influences", 1, 0, duplicate, nullptr, nullptr, false, 0, true, false}));
    REQUIRE(repeated.has_value());
    CHECK(repeated->get<std::string>().find("same bone appears twice") != std::string::npos);
}

TEST_CASE("NifTarget SkinValidate reports the skin findings and keeps the file") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "skin.nif";
    fo4ide::test::writeBytes(nif, mirroredSkinNif());
    const auto before = bytesOf(nif);

    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::app::NifTarget target(service, textures, platform, plugins);

    auto reply = target.invoke("SkinValidate", nlohmann::json::array({nif.string(), 1}));
    REQUIRE(reply.has_value());
    const auto report = nlohmann::json::parse(reply->get<std::string>());
    REQUIRE_FALSE(report.contains("error"));
    CHECK(report.at("bones").get<int>() == 3);
    CHECK(report.at("bind_transforms").get<int>() == 3);
    CHECK(report.at("used_bones").get<int>() == 2);
    CHECK(report.at("errors").get<int>() == 0);
    CHECK(report.at("ok").get<bool>());
    REQUIRE(report.at("findings").size() == 1);
    CHECK(report.at("findings")[0].at("level").get<std::string>() == "warning");
    CHECK(report.at("findings")[0].at("what").get<std::string>() == "1 move no vertex of this shape: Spine");
    CHECK(bytesOf(nif) == before);
}

namespace {

std::vector<std::byte> skeletonHkx(const std::vector<std::string>& names) {
    using namespace fo4::test::hkx;
    const std::size_t boneSize = fo4::hkx::find_layout("hkaBone")->size;
    std::vector<std::byte> data(fo4::hkx::find_layout("hkaSkeleton")->size, std::byte{0});
    std::vector<std::uint32_t> local;
    const auto text = [&data](const std::string& value) {
        const std::size_t at = data.size();
        for (const char ch : value) data.push_back(static_cast<std::byte>(ch));
        data.push_back(std::byte{0});
        return at;
    };

    const std::size_t bonesAt = data.size();
    data.resize(bonesAt + names.size() * boneSize, std::byte{0});
    for (std::size_t i = 0; i < names.size(); ++i) {
        link(local, bonesAt + i * boneSize + member_at("hkaBone", "name"), text(names[i]));
    }
    const std::size_t parentsAt = data.size();
    for (std::size_t i = 0; i < names.size(); ++i) {
        const auto parent = static_cast<std::uint16_t>(static_cast<std::int16_t>(i) - 1);
        data.push_back(static_cast<std::byte>(parent & 0xFF));
        data.push_back(static_cast<std::byte>(parent >> 8));
    }
    const std::size_t poseAt = data.size();
    for (std::size_t i = 0; i < names.size(); ++i) {
        for (const float value : {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f}) {
            put_float(data, value);
        }
    }
    link(local, member_at("hkaSkeleton", "name"), text("Rig"));
    link(local, member_at("hkaSkeleton", "parentIndices"), parentsAt);
    set_count(data, member_at("hkaSkeleton", "parentIndices"), names.size());
    link(local, member_at("hkaSkeleton", "bones"), bonesAt);
    set_count(data, member_at("hkaSkeleton", "bones"), names.size());
    link(local, member_at("hkaSkeleton", "referencePose"), poseAt);
    set_count(data, member_at("hkaSkeleton", "referencePose"), names.size());

    std::vector<std::byte> classNames;
    const auto skeletonClass = add_class_name(classNames, fo4::hkx::find_layout("hkaSkeleton")->signature, "hkaSkeleton");
    return build_packfile({
        TestSection{"__classnames__", classNames, {}, {}, {}},
        TestSection{"__types__", {}, {}, {}, {}},
        TestSection{"__data__", std::move(data), std::move(local), {}, {0, 0, skeletonClass}},
    });
}

}  // namespace

// Detects: the skeleton check must count bones by their exact names (case-insensitive) against the HKX skeleton, name the bone that finds no skeleton bone with the vertices it moves, and leave the NIF unchanged.
TEST_CASE("NifTarget SkinBinding counts matched bones against a skeleton HKX and keeps the file") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "skin.nif";
    const auto skeleton = root / "skeleton.hkx";
    fo4ide::test::writeBytes(nif, mirroredSkinNif({0, 2, 1, 1}));
    fo4ide::test::writeBytes(skeleton, skeletonHkx({"Pelvis", "larm", "RArm"}));
    const auto before = bytesOf(nif);

    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::app::NifTarget target(service, textures, platform, plugins);

    auto reply = target.invoke("SkinBinding", nlohmann::json::array({nif.string(), 1, skeleton.string(), ""}));
    REQUIRE(reply.has_value());
    const auto report = nlohmann::json::parse(reply->get<std::string>());
    REQUIRE_FALSE(report.contains("error"));
    CHECK(report.at("skeleton").at("bones").get<int>() == 3);
    REQUIRE(report.at("shapes").size() == 1);
    const auto& shape = report.at("shapes")[0];
    CHECK(shape.at("exact").get<int>() == 2);
    CHECK(shape.at("unmatched").get<int>() == 1);
    CHECK_FALSE(shape.at("complete").get<bool>());
    CHECK(shape.at("vertices").get<int>() == 4);
    CHECK(shape.at("fully_bound_vertices").get<int>() == 3);
    REQUIRE(shape.at("bones").size() == 3);
    CHECK(shape.at("bones")[0].at("skeleton_bone_name").get<std::string>() == "larm");
    CHECK(shape.at("bones")[2].at("match").get<std::string>() == "unmatched");
    CHECK(shape.at("bones")[2].at("weighted_vertices").get<int>() == 1);
    CHECK(shape.at("warning").get<std::string>().find("Spine") != std::string::npos);

    auto every = target.invoke("SkinBinding", nlohmann::json::array({nif.string(), nullptr, skeleton.string(), ""}));
    REQUIRE(every.has_value());
    const auto all = nlohmann::json::parse(every->get<std::string>());
    CHECK(all.at("shapes").size() == 1);
    CHECK(all.at("refused").empty());
    CHECK(bytesOf(nif) == before);
}

TEST_CASE("NifTarget SkinBinding binds the committed Turret mesh to its committed mounted skeleton and keeps both files") {
    // Constraint: detects a skeleton check that finds no exact bone match on the shipped Turret pair.
    const std::filesystem::path samples{FO4IDE_HKX_SAMPLES_DIR};
    const auto nif = samples / "Meshes" / "Actors" / "Turret" / "CharacterAssets" / "TurretMounted.nif";
    const auto skeleton = samples / "TurretMountedSkeleton.hkx";
    if (!std::filesystem::exists(nif) || !std::filesystem::exists(skeleton)) {
        if (std::getenv("CI") != nullptr) FAIL("CI is missing the committed Turret samples");
        SKIP("no committed Turret samples");
    }
    const auto nifBefore = bytesOf(nif);
    const auto skeletonBefore = bytesOf(skeleton);

    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::app::NifTarget target(service, textures, platform, plugins);

    auto reply = target.invoke("SkinBinding", nlohmann::json::array({nif.string(), nullptr, skeleton.string(), ""}));
    REQUIRE(reply.has_value());
    const auto report = nlohmann::json::parse(reply->get<std::string>());
    REQUIRE_FALSE(report.contains("error"));
    CHECK(report.at("skeleton").at("bones").get<int>() == 8);
    REQUIRE_FALSE(report.at("shapes").empty());
    int exact = 0;
    for (const auto& shape : report.at("shapes")) exact += shape.at("exact").get<int>();
    CHECK(exact > 0);
    CHECK(bytesOf(nif) == nifBefore);
    CHECK(bytesOf(skeleton) == skeletonBefore);
}

// Detects: a missing skeleton file, a block that is not in the NIF and a block that is not a shape must each answer an error naming the cause instead of an empty report that reads as a clean bind.
TEST_CASE("NifTarget SkinBinding fails closed on a missing skeleton, a bad block and a block that is not a shape") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "skin.nif";
    const auto skeleton = root / "skeleton.hkx";
    fo4ide::test::writeBytes(nif, mirroredSkinNif());
    fo4ide::test::writeBytes(skeleton, skeletonHkx({"Pelvis", "LArm"}));

    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::app::NifTarget target(service, textures, platform, plugins);

    const auto errorOf = [&](const nlohmann::json& args) {
        auto reply = target.invoke("SkinBinding", args);
        REQUIRE(reply.has_value());
        const auto text = reply->get<std::string>();
        const auto parsed = nlohmann::json::parse(text, nullptr, false);
        return parsed.is_object() && parsed.contains("error") ? parsed.at("error").get<std::string>() : text;
    };
    CHECK(errorOf(nlohmann::json::array({nif.string(), 1, (root / "none.hkx").string(), ""})).find("not found") !=
          std::string::npos);
    CHECK(errorOf(nlohmann::json::array({nif.string(), 99, skeleton.string(), ""})).find("not in the NIF") !=
          std::string::npos);
    CHECK(errorOf(nlohmann::json::array({nif.string(), 0, skeleton.string(), ""})).find("is not a shape") != std::string::npos);
    CHECK(errorOf(nlohmann::json::array({nif.string(), 1, skeleton.string(), (root / "none.nif").string()}))
              .find("not found") != std::string::npos);
}

namespace {

std::vector<std::byte> skinVertexBytes(const std::array<std::uint8_t, 4>& bones) {
    std::vector<std::byte> vertices;
    for (int corner = 0; corner < 4; ++corner) {
        auto value = nif_fixture::skinned_vertex(corner < 2 ? 1.0f : -1.0f, static_cast<float>(corner % 2), 0.0f, bones[corner]);
        for (int slot = 0; slot < 4; ++slot) value.weights[slot] = 0;
        value.weights[0] = 0x3C00;
        for (int slot = 0; slot < 4; ++slot) value.bones[slot] = bones[corner];
        const auto encoded = nif_fixture::encode_vertex(value);
        vertices.insert(vertices.end(), encoded.begin(), encoded.end());
    }
    return vertices;
}

// Invariant: block 1 binds, block 2 is a skin-flagged shape with no skin, block 3 points its skin at a node so the skin is lost, and block 4 names a bone past its list so it does not parse.
std::vector<std::byte> mixedSkinNif() {
    const std::uint32_t flags = nif_fixture::k_full | nif_fixture::k_has_uv | nif_fixture::k_has_normal |
                                nif_fixture::k_has_tangent | nif_fixture::k_has_colour | nif_fixture::k_has_skin;
    constexpr std::size_t stride = 44;
    const std::vector<std::uint16_t> triangles{0, 1, 2, 0, 2, 3};
    const auto good = skinVertexBytes({0, 0, 1, 1});
    const nif_fixture::BoneFixture identity{"", {0.0f, 0.0f, 0.0f}, 0.0f,
                                            {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f},
                                            {0.0f, 0.0f, 0.0f}, 1.0f};
    const fo4::nif::Transform none;
    const std::vector<nif_fixture::Block> blocks{
        nif_fixture::node_block("NiNode", 0, none, {1, 2, 3, 4}),
        nif_fixture::shape_block("BSTriShape", 1, none, flags, stride, 5, -1, -1, good, triangles),
        nif_fixture::shape_block("BSTriShape", 2, none, flags, stride, -1, -1, -1, good, triangles),
        nif_fixture::shape_block("BSTriShape", 3, none, flags, stride, 0, -1, -1, good, triangles),
        nif_fixture::shape_block("BSTriShape", 4, none, flags, stride, 5, -1, -1, skinVertexBytes({9, 9, 9, 9}), triangles),
        nif_fixture::skin_instance_block(0, 9, {6, 7, 8}),
        nif_fixture::node_block("NiNode", 5, none, {}),
        nif_fixture::node_block("NiNode", 6, none, {}),
        nif_fixture::node_block("NiNode", 7, none, {}),
        nif_fixture::bone_data_block({identity, identity, identity}),
    };
    return nif_fixture::build_nif({"root", "body", "plain", "lost", "broken", "LArm", "RArm", "Spine"}, blocks, {0});
}

}  // namespace

// Detects: the all-shapes reply must list a shape with a lost skin or an unparsable shape under refused with the reason, count a skin-flagged shape with no skin as unskinned, and keep reporting the shape that binds.
TEST_CASE("NifTarget SkinBinding lists refused shapes, counts unskinned ones and reports NoSkinnedShape") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "mixed.nif";
    const auto bare = root / "bare.nif";
    const auto skeleton = root / "skeleton.hkx";
    fo4ide::test::writeBytes(nif, mixedSkinNif());
    fo4ide::test::writeBytes(bare, nif_fixture::build_nif(
        {"root"}, {nif_fixture::node_block("NiNode", 0, fo4::nif::Transform{}, {})}, {0}));
    fo4ide::test::writeBytes(skeleton, skeletonHkx({"LArm", "RArm", "Spine"}));

    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::app::NifTarget target(service, textures, platform, plugins);

    auto reply = target.invoke("SkinBinding", nlohmann::json::array({nif.string(), nullptr, skeleton.string(), ""}));
    REQUIRE(reply.has_value());
    const auto report = nlohmann::json::parse(reply->get<std::string>());
    REQUIRE_FALSE(report.contains("error"));
    REQUIRE(report.at("shapes").size() == 1);
    CHECK(report.at("shapes")[0].at("block").get<int>() == 1);
    CHECK(report.at("shapes")[0].at("complete").get<bool>());
    CHECK(report.at("unskinned").get<int>() == 1);
    REQUIRE(report.at("refused").size() == 2);
    CHECK(report.at("refused")[0].at("block").get<int>() == 3);
    CHECK(report.at("refused")[0].at("reason").get<std::string>().find("lost its skin") != std::string::npos);
    CHECK(report.at("refused")[0].at("reason").get<std::string>().find("BSSkin::Instance") != std::string::npos);
    CHECK(report.at("refused")[1].at("block").get<int>() == 4);
    CHECK(report.at("refused")[1].at("reason").get<std::string>().find("bone index") != std::string::npos);

    auto single = target.invoke("SkinBinding", nlohmann::json::array({nif.string(), 3, skeleton.string(), ""}));
    REQUIRE(single.has_value());
    CHECK(single->get<std::string>().find("lost its skin") != std::string::npos);

    auto none = target.invoke("SkinBinding", nlohmann::json::array({bare.string(), nullptr, skeleton.string(), ""}));
    REQUIRE(none.has_value());
    CHECK(none->get<std::string>().find("no skinned shape") != std::string::npos);
}

// Detects: a helper bone that only exists under a skeleton bone in the body NIF's own node tree must bind through that tree as the source after the skeleton NIF, not read as unmatched.
TEST_CASE("NifTarget SkinBinding resolves a helper bone through the body NIF's node tree after the skeleton NIF") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "skin.nif";
    const auto flat = root / "flat.nif";
    const auto skeleton = root / "skeleton.hkx";
    fo4ide::test::writeBytes(nif, mirroredSkinNif({0, 0, 1, 1}, true));
    fo4ide::test::writeBytes(flat, mirroredSkinNif());
    fo4ide::test::writeBytes(skeleton, skeletonHkx({"root", "RArm", "Spine"}));

    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::app::NifTarget target(service, textures, platform, plugins);

    for (const auto& skeletonNif : {std::string{}, flat.string()}) {
        auto reply = target.invoke("SkinBinding", nlohmann::json::array({nif.string(), 1, skeleton.string(), skeletonNif}));
        REQUIRE(reply.has_value());
        const auto report = nlohmann::json::parse(reply->get<std::string>());
        REQUIRE_FALSE(report.contains("error"));
        const auto& shape = report.at("shapes")[0];
        CHECK(shape.at("node_chain").get<int>() == 1);
        CHECK(shape.at("unmatched").get<int>() == 0);
        CHECK(shape.at("complete").get<bool>());
        CHECK(shape.at("fully_bound_vertices").get<int>() == 2);
        const auto& bone = shape.at("bones")[0];
        CHECK(bone.at("match").get<std::string>() == "node_chain");
        CHECK(bone.at("via").get<std::string>() == "root");
        CHECK(bone.at("source").get<std::string>() == "body NIF");
        CHECK(bone.at("skeleton_bone").get<int>() == 0);
        CHECK_FALSE(bone.at("ambiguous").get<bool>());
    }
}

// Detects: ResolveAsset must name the winning loose or BA2 provider across the session roots in priority order, list every lower-priority provider, report a miss, and extract a BA2 winner to the cache.
TEST_CASE("NifTarget ResolveAsset reports the winner and lower-priority providers across roots") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto high = root / "ModHigh";
    const auto low = root / "ModLow";
    writeLoose(high / "Meshes" / "Rock.nif", "loose-rock");
    writePacked(low / "Low - Main.ba2", "Meshes\\Rock.nif", "packed-rock");
    writePacked(low / "Only - Main.ba2", "Meshes\\Only.nif", "packed-only");

    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures(root / "cache");
    textures.setSessionRoots({high, low});
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::app::NifTarget target(service, textures, platform, plugins);

    const auto call = [&](nlohmann::json args) {
        auto reply = target.invoke("ResolveAsset", std::move(args));
        if (!reply) INFO(reply.error().type + ": " + reply.error().message);
        REQUIRE(reply.has_value());
        return nlohmann::json::parse(reply->get<std::string>());
    };

    const auto rock = call(nlohmann::json::array({"Meshes\\Rock.nif"}));
    CHECK(rock.at("Found") == true);
    CHECK(rock.at("Providers") == 2);
    CHECK(rock.at("Ambiguous") == true);
    CHECK(rock.at("Winner").at("Kind") == "loose");
    CHECK(rock.at("Winner").at("Provider") == "ModHigh");
    REQUIRE(rock.at("Others").size() == 1);
    CHECK(rock.at("Others")[0].at("Kind") == "ba2");
    CHECK(rock.at("Others")[0].at("Provider") == "ModLow");

    const auto capped = call(nlohmann::json::array({"Meshes\\Rock.nif", false, 0}));
    CHECK(capped.at("Others").size() == 0);
    CHECK(capped.at("Providers") == 2);

    const auto only = call(nlohmann::json::array({"Meshes\\Only.nif", true}));
    CHECK(only.at("Winner").at("Kind") == "ba2");
    CHECK(only.at("Ambiguous") == false);
    const auto extracted = std::filesystem::path(only.at("ExtractedTo").get<std::string>());
    CHECK(extracted.string().starts_with((root / "cache").string()));
    CHECK(fileBytes(extracted) == "packed-only");

    const auto missing = call(nlohmann::json::array({"Meshes\\Nowhere.nif"}));
    CHECK(missing.at("Found") == false);

    CHECK(target.invoke("ResolveAsset", nlohmann::json::array({"..\\Escape.nif"})).error().type == "AssetResolveFailed");
    CHECK(target.invoke("ResolveAsset", nlohmann::json::array({""})).error().type == "AssetPathMissing");
}

TEST_CASE("AssetResolverContentionTests: two loose providers name the winner and the lower-priority mod") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeLoose(root / "ModHigh" / "Meshes" / "Shared.nif", "loose-high");
    writeLoose(root / "ModLow" / "Meshes" / "Shared.nif", "loose-low");

    const auto shared = resolveAssetOver({root / "ModHigh", root / "ModLow"}, root / "cache",
                                         nlohmann::json::array({"Meshes\\Shared.nif"}));
    CHECK(shared.at("Found") == true);
    CHECK(shared.at("Providers") == 2);
    CHECK(shared.at("Ambiguous") == true);
    CHECK(shared.at("Winner").at("Kind") == "loose");
    CHECK(shared.at("Winner").at("Provider") == "ModHigh");
    CHECK(shared.at("Winner").at("Size") == 10);
    REQUIRE(shared.at("Others").size() == 1);
    CHECK(shared.at("Others")[0].at("Kind") == "loose");
    CHECK(shared.at("Others")[0].at("Provider") == "ModLow");
    CHECK(shared.at("Others")[0].at("Size") == 9);
}

TEST_CASE("AssetResolverContentionTests: a mod with a loose and a packed copy is listed once per source hit") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeLoose(root / "ModBoth" / "Meshes" / "Both.nif", "loose-both");
    writePacked(root / "ModBoth" / "Both - Main.ba2", "Meshes\\Both.nif", "packed-both");

    const auto both = resolveAssetOver({root / "ModBoth"}, root / "cache",
                                       nlohmann::json::array({"Meshes\\Both.nif"}));
    CHECK(both.at("Providers") == 2);
    CHECK(both.at("Ambiguous") == true);
    CHECK(both.at("Winner").at("Kind") == "loose");
    CHECK(both.at("Winner").at("Provider") == "ModBoth");
    REQUIRE(both.at("Others").size() == 1);
    CHECK(both.at("Others")[0].at("Kind") == "ba2");
    CHECK(both.at("Others")[0].at("Provider") == "ModBoth");
}

TEST_CASE("AssetResolverContentionTests: a mod with two archives lists both archives") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writePacked(root / "ModTwo" / "Two - Main.ba2", "Meshes\\Twice.nif", "packed-a");
    writePacked(root / "ModTwo" / "Two - Extra.ba2", "Meshes\\Twice.nif", "packed-b");

    const auto twice = resolveAssetOver({root / "ModTwo"}, root / "cache",
                                        nlohmann::json::array({"Meshes\\Twice.nif"}));
    CHECK(twice.at("Providers") == 2);
    CHECK(twice.at("Ambiguous") == true);
    CHECK(twice.at("Winner").at("Kind") == "ba2");
    CHECK(twice.at("Winner").at("Provider") == "ModTwo");
    REQUIRE(twice.at("Others").size() == 1);
    CHECK(twice.at("Others")[0].at("Kind") == "ba2");
    CHECK(twice.at("Others")[0].at("Provider") == "ModTwo");
    std::vector<std::string> archives{
        std::filesystem::path(twice.at("Winner").at("Container").get<std::string>()).filename().string(),
        std::filesystem::path(twice.at("Others")[0].at("Container").get<std::string>()).filename().string()};
    std::sort(archives.begin(), archives.end());
    CHECK(archives == std::vector<std::string>{"Two - Extra.ba2", "Two - Main.ba2"});
}

namespace {

class AssetRefsBackend final : public fo4ide::plugin::IPluginBackend {
public:
    fo4ide::plugin::Capabilities capabilities() const noexcept override { return {}; }

    fo4ide::Result<nlohmann::json> invoke(std::string_view operation, const nlohmann::json& arguments) override {
        if (operation != "backend.CollectAssetReferences") {
            return std::unexpected(fo4ide::Error{"UnexpectedOperation", std::string(operation)});
        }
        requested = arguments;
        return reply;
    }

    nlohmann::json reply;
    nlohmann::json requested;
};

nlohmann::json assetRef(std::string path, int records) {
    nlohmann::json keys = nlohmann::json::array();
    for (int index = 0; index < records; ++index) keys.push_back("00080" + std::to_string(index) + ":Patch.esp");
    return {{"path", std::move(path)}, {"records", std::move(keys)}};
}

}

TEST_CASE("NifTarget AuditAssetUsage reports used and missing asset paths") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto high = root / "ModHigh";
    const auto low = root / "ModLow";
    const auto pluginFolder = root / "PluginMod";
    writeLoose(high / "meshes" / "clutter" / "rock.nif", "rock");
    writeLoose(pluginFolder / "textures" / "shipped.dds", "dds");
    writePacked(low / "Low - Main.ba2", "interface\\panel.swf", "swf");

    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures(root / "cache");
    textures.setSessionRoots({high, low});
    AssetRefsBackend plugins;
    plugins.reply = {{"plugin", "Patch.esp"},
                     {"pluginDirectory", pluginFolder.string()},
                     {"nativeRecords", 4},
                     {"assets", nlohmann::json::array({assetRef("clutter/rock.nif", 2), assetRef("clutter/gone.nif", 7),
                                                        assetRef("clutter/lost.nif", 1), assetRef("textures/shipped.dds", 1),
                                                        assetRef("interface/panel.swf", 1), assetRef("..\\evil.nif", 1)})}};
    fo4ide::app::NifTarget target(service, textures, platform, plugins);

    const auto reply = target.invoke("AuditAssetUsage", nlohmann::json::array({"Patch.esp"}));
    REQUIRE(reply.has_value());
    CHECK(plugins.requested == nlohmann::json::array({"Patch.esp"}));
    CHECK(reply->at("plugin") == "Patch.esp");
    CHECK(reply->at("nativeRecords") == 4);
    CHECK(reply->at("referenced") == 6);
    CHECK(reply->at("resolved") == 3);
    CHECK(reply->at("missing") == 2);
    CHECK(reply->at("unusable") == 1);
    CHECK(reply->at("truncated") == false);
    const auto& rows = reply->at("missingAssets");
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].at("path") == "meshes/clutter/gone.nif");
    CHECK(rows[0].at("recordCount") == 7);
    CHECK(rows[0].at("records").size() == 5);
    CHECK(rows[1].at("path") == "meshes/clutter/lost.nif");
    CHECK_FALSE(reply->contains("note"));

    const auto capped = target.invoke("AuditAssetUsage", nlohmann::json::array({"Patch.esp", 1}));
    REQUIRE(capped.has_value());
    CHECK(capped->at("missing") == 2);
    CHECK(capped->at("missingAssets").size() == 1);
    CHECK(capped->at("truncated") == true);
}

TEST_CASE("NifTarget AuditAssetUsage warns when no data root is known and refuses bad input") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures(root / "cache");
    AssetRefsBackend plugins;
    plugins.reply = {{"plugin", "Patch.esp"},
                     {"pluginDirectory", (root / "PluginMod").string()},
                     {"nativeRecords", 1},
                     {"assets", nlohmann::json::array({assetRef("clutter/rock.nif", 1)})}};
    fo4ide::app::NifTarget target(service, textures, platform, plugins);

    const auto reply = target.invoke("AuditAssetUsage", nlohmann::json::array({"Patch.esp"}));
    REQUIRE(reply.has_value());
    CHECK(reply->at("missing") == 1);
    CHECK(reply->contains("note"));

    CHECK(target.invoke("AuditAssetUsage", nlohmann::json::array({""})).error().type == "PluginMissing");
    fo4ide::plugin::NullPluginBackend unavailable;
    fo4ide::app::NifTarget offline(service, textures, platform, unavailable);
    CHECK(offline.invoke("AuditAssetUsage", nlohmann::json::array({"Patch.esp"})).error().type ==
          "PluginBackendUnavailable");
}

namespace {

nlohmann::json skinToolReply(fo4ide::app::NifTarget& target, const std::filesystem::path& nif, const char* op,
                             const nlohmann::json& body, std::string_view expectedSource = {}) {
    auto answered = target.invoke("SkinToolOp", nlohmann::json::array(
        {nif.string(), std::string(), op, body.dump(), false, std::string(expectedSource)}));
    REQUIRE(answered.has_value());
    return nlohmann::json::parse(answered->get<std::string>());
}

double weightOnBone(const fo4::nif::Shape& shape, std::size_t vertex, std::uint8_t bone) {
    for (std::size_t slot = 0; slot < 4; ++slot) {
        if (shape.bone_indices[vertex * 4 + slot] == bone && shape.weights[vertex * 4 + slot] > 0.0f) {
            return shape.weights[vertex * 4 + slot];
        }
    }
    return -1.0;
}

}  // namespace

TEST_CASE("NifTarget SkinToolOp brush paints inside the radius, keeps a locked bone and saves the shape") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "skin.nif";
    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::app::NifTarget target(service, textures, platform, plugins);

    const nlohmann::json stroke = {{"block", 1}, {"mode", "replace"}, {"falloff", "constant"}, {"center", {1.0, 0.0, 0.0}},
                                   {"radius", 0.5}, {"strength", 1.0}, {"bone", 2}, {"target", 1.0}};
    fo4ide::test::writeBytes(nif, mirroredSkinNif());
    const auto reply = skinToolReply(target, nif, "brush", stroke);
    REQUIRE_FALSE(reply.contains("error"));
    CHECK(reply.at("in_reach").get<int>() == 1);
    CHECK(reply.at("changed").get<int>() == 1);
    CHECK(reply.at("saved").get<std::string>() == nif.string());
    const auto painted = fo4::nif::read_shape(fo4::nif::read_nif(bytesOf(nif)), 1);
    CHECK(weightOnBone(painted, 0, 2) > 0.9);
    CHECK(weightOnBone(painted, 1, 0) > 0.9);
    CHECK(weightOnBone(painted, 2, 1) > 0.9);

    fo4ide::test::writeBytes(nif, mirroredSkinNif());
    auto locked = stroke;
    locked["locked"] = {0};
    const auto kept = skinToolReply(target, nif, "brush", locked);
    REQUIRE_FALSE(kept.contains("error"));
    const auto lockedShape = fo4::nif::read_shape(fo4::nif::read_nif(bytesOf(nif)), 1);
    CHECK(weightOnBone(lockedShape, 0, 0) > 0.9);
}

TEST_CASE("NifTarget SkinToolOp brush writes nothing when no vertex is in reach and refuses bad arguments") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "skin.nif";
    fo4ide::test::writeBytes(nif, mirroredSkinNif());
    const auto before = bytesOf(nif);
    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::app::NifTarget target(service, textures, platform, plugins);

    const nlohmann::json far = {{"block", 1}, {"mode", "add"}, {"falloff", "linear"}, {"center", {50.0, 50.0, 0.0}},
                                {"radius", 0.5}, {"strength", 1.0}, {"bone", 2}};
    const auto none = skinToolReply(target, nif, "brush", far);
    REQUIRE_FALSE(none.contains("error"));
    CHECK(none.at("changed").get<int>() == 0);
    CHECK(none.at("saved").get<std::string>().empty());
    CHECK(bytesOf(nif) == before);

    auto wrapped = far;
    wrapped["bone"] = 256;
    CHECK(skinToolReply(target, nif, "brush", wrapped).contains("error"));
    auto lockedWrap = far;
    lockedWrap["locked"] = {256};
    CHECK(skinToolReply(target, nif, "brush", lockedWrap).contains("error"));
    auto unknown = far;
    unknown["mode"] = "erase";
    CHECK(skinToolReply(target, nif, "brush", unknown).contains("error"));
    auto missingShape = far;
    missingShape["block"] = 99;
    CHECK(skinToolReply(target, nif, "brush", missingShape).contains("error"));
    CHECK(skinToolReply(target, nif, "paint", far).contains("error"));
    const auto stale = skinToolReply(target, nif, "brush", far, "0000");
    REQUIRE(stale.contains("error"));
    CHECK(stale.at("error").get<std::string>().find("changed on disk") != std::string::npos);
    CHECK(bytesOf(nif) == before);
}

TEST_CASE("NifTarget SkinToolOp transfer copies the donor's weights onto the target shape by bone name") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto donor = root / "donor.nif";
    const auto body = root / "body.nif";
    fo4ide::test::writeBytes(donor, mirroredSkinNif());
    fo4ide::test::writeBytes(body, mirroredSkinNif({2, 2, 2, 2}));
    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::app::NifTarget target(service, textures, platform, plugins);

    const auto reply = skinToolReply(target, body, "transfer", {{"block", 1}, {"source_path", donor.string()},
                                                                  {"source_block", 1}, {"method", "nearest_vertex"},
                                                                  {"max_distance", 0.0}});
    REQUIRE_FALSE(reply.contains("error"));
    CHECK(reply.at("transferred").get<int>() == 4);
    CHECK(reply.at("saved").get<std::string>() == body.string());
    const auto shape = fo4::nif::read_shape(fo4::nif::read_nif(bytesOf(body)), 1);
    CHECK(weightOnBone(shape, 0, 0) > 0.9);
    CHECK(weightOnBone(shape, 2, 1) > 0.9);
    CHECK(weightOnBone(shape, 0, 2) < 0.0);

    const auto missing = skinToolReply(target, body, "transfer", {{"block", 1}, {"source_path", (root / "no.nif").string()},
                                                                    {"source_block", 1}, {"method", "barycentric"}});
    CHECK(missing.contains("error"));
    CHECK(skinToolReply(target, body, "transfer", {{"block", 1}, {"source_path", donor.string()}, {"source_block", 1},
                                                   {"method", "closest"}}).contains("error"));
}

TEST_CASE("NifTarget SkinToolOp remap creates, reads, validates and rewrites the customization remap") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "skin.nif";
    fo4ide::test::writeBytes(nif, mirroredSkinNif());
    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::app::NifTarget target(service, textures, platform, plugins);

    const auto empty = skinToolReply(target, nif, "remap_read", {{"block", 1}});
    REQUIRE_FALSE(empty.contains("error"));
    CHECK(empty.at("remap_block").get<int>() == -1);
    CHECK(empty.at("records").empty());
    CHECK(skinToolReply(target, nif, "remap_write", {{"block", 1}}).contains("error"));

    const auto created = skinToolReply(target, nif, "remap_create", {{"block", 1}});
    REQUIRE_FALSE(created.contains("error"));
    CHECK(created.at("records").get<int>() == 4);
    const auto read = skinToolReply(target, nif, "remap_read", {{"block", 1}});
    REQUIRE_FALSE(read.contains("error"));
    CHECK(read.at("remap_block").get<int>() >= 0);
    REQUIRE(read.at("records").size() == 4);
    CHECK(read.at("records")[2].at("bones")[0].get<int>() == 1);
    CHECK(skinToolReply(target, nif, "remap_create", {{"block", 1}}).contains("error"));

    const auto checked = skinToolReply(target, nif, "remap_validate", {{"block", 1}});
    REQUIRE_FALSE(checked.contains("error"));
    CHECK(checked.at("messages").empty());
    const auto broken = skinToolReply(target, nif, "remap_validate", {{"block", 1},
        {"records", {{{"bones", {0, 0, 0, 0}}, {"weights", {1.0, 0.0, 0.0, 0.0}}}}}});
    REQUIRE_FALSE(broken.contains("error"));
    CHECK_FALSE(broken.at("messages").empty());

    const nlohmann::json authoredRecord = {{"bones", {0, 1, 0, 0}}, {"weights", {0.5, 0.5, 0.0, 0.0}}};
    const nlohmann::json authoredRecords = nlohmann::json::array({authoredRecord, authoredRecord, authoredRecord, authoredRecord});
    const auto authored = skinToolReply(target, nif, "remap_write", {{"block", 1}, {"records", authoredRecords}});
    REQUIRE_FALSE(authored.contains("error"));
    const auto before = bytesOf(nif);

    const auto unconfirmed = skinToolReply(target, nif, "remap_write", {{"block", 1}, {"from_skin", true}});
    // Must catch a from_skin write that overwrites authored remap records without replace_records confirmation.
    REQUIRE(unconfirmed.contains("error"));
    CHECK(unconfirmed.at("error").get<std::string>().find("replace_records") != std::string::npos);
    CHECK(bytesOf(nif) == before);
    const auto stringFlag = skinToolReply(target, nif, "remap_write",
        {{"block", 1}, {"from_skin", "true"}, {"replace_records", true}});
    // Must catch a string from_skin read as false, which silently skipped the overwrite the caller asked for.
    REQUIRE(stringFlag.contains("error"));
    CHECK(bytesOf(nif) == before);
    const auto numberFlag = skinToolReply(target, nif, "remap_write", {{"block", 1}, {"from_skin", 1}, {"replace_records", true}});
    // Must catch a numeric from_skin read as false, which silently skipped the overwrite the caller asked for.
    REQUIRE(numberFlag.contains("error"));
    CHECK(bytesOf(nif) == before);
    const auto stringConfirm = skinToolReply(target, nif, "remap_write",
        {{"block", 1}, {"from_skin", true}, {"replace_records", "true"}});
    // Must catch a string replace_records accepted as confirmation of an overwrite that was never confirmed.
    REQUIRE(stringConfirm.contains("error"));
    CHECK(bytesOf(nif) == before);

    const auto written = skinToolReply(target, nif, "remap_write",
        {{"block", 1}, {"from_skin", true}, {"replace_records", true}});
    REQUIRE_FALSE(written.contains("error"));
    CHECK(written.at("records").get<int>() == 4);
    const auto restored = skinToolReply(target, nif, "remap_read", {{"block", 1}});
    REQUIRE(restored.at("records").size() == 4);
    // Must catch a confirmed from_skin write that leaves the authored 0.5 weights in place instead of the skin's 1.0.
    CHECK(restored.at("records")[0].at("weights")[0].get<double>() == 1.0);
    CHECK(skinToolReply(target, nif, "remap_write", {{"block", 1}, {"records", {{{"bones", {0, 0, 0}}, {"weights", {1.0}}}}}})
              .contains("error"));

    const auto bonesNif = root / "new_bones.nif";
    fo4ide::test::writeBytes(bonesNif, mirroredSkinNif());
    const std::string newBone = "4578747261" + std::string(22, '0') + std::string(384, 'f');
    const auto bonesCreated = skinToolReply(target, bonesNif, "remap_create",
        {{"block", 1}, {"new_bones", nlohmann::json::array({newBone})}});
    REQUIRE_FALSE(bonesCreated.contains("error"));
    const auto bonesRead = skinToolReply(target, bonesNif, "remap_read", {{"block", 1}});
    REQUIRE(bonesRead.at("new_bones").size() == 1);
    // Must catch new-bone bytes that do not round trip, which would corrupt the bone on the next write.
    CHECK(bonesRead.at("new_bones")[0].at("raw").get<std::string>() == newBone);
    // Must catch a new bone read under the wrong name, which would show the user a bone that is not in the file.
    CHECK(bonesRead.at("new_bones")[0].at("name").get<std::string>() == "Extra");
    const auto bonesBefore = bytesOf(bonesNif);
    const auto bonesWrite = skinToolReply(target, bonesNif, "remap_write",
        {{"block", 1}, {"from_skin", true}, {"replace_records", true}});
    // Must catch a confirmed skin overwrite that drops a remap using new bones, the data loss this guard exists for.
    REQUIRE(bonesWrite.contains("error"));
    CHECK(bonesWrite.at("error").get<std::string>().find("this remap uses new bones") != std::string::npos);
    CHECK(bytesOf(bonesNif) == bonesBefore);

    const auto indexNif = root / "bone_index.nif";
    fo4ide::test::writeBytes(indexNif, mirroredSkinNif());
    const nlohmann::json pastSkin = {{"bones", {9, 0, 0, 0}}, {"weights", {1.0, 0.0, 0.0, 0.0}}};
    const nlohmann::json pastSkinRecords = nlohmann::json::array({pastSkin, pastSkin, pastSkin, pastSkin});
    const auto indexCreated = skinToolReply(target, indexNif, "remap_create", {{"block", 1}, {"records", pastSkinRecords}});
    REQUIRE_FALSE(indexCreated.contains("error"));
    const auto indexBefore = bytesOf(indexNif);
    const auto indexWrite = skinToolReply(target, indexNif, "remap_write",
        {{"block", 1}, {"from_skin", true}, {"replace_records", true}});
    // Must catch a confirmed skin overwrite of a remap that names a bone past the skin when it has no new-bones block.
    REQUIRE(indexWrite.contains("error"));
    CHECK(indexWrite.at("error").get<std::string>().find("this remap uses new bones") != std::string::npos);
    CHECK(bytesOf(indexNif) == indexBefore);
}

namespace {

using OsdOffsets = std::vector<std::pair<std::uint16_t, std::array<float, 3>>>;

std::string osdBytes(const std::vector<std::pair<std::string, OsdOffsets>>& sets, std::uint32_t version = 1) {
    std::string out("\0DSO", 4);
    const auto put = [&out](const auto& value) { out.append(reinterpret_cast<const char*>(&value), sizeof(value)); };
    put(version);
    put(static_cast<std::uint32_t>(sets.size()));
    for (const auto& [name, offsets] : sets) {
        put(static_cast<std::uint8_t>(name.size()));
        out += name;
        put(static_cast<std::uint16_t>(offsets.size()));
        for (const auto& [vertex, delta] : offsets) {
            put(vertex);
            for (const float component : delta) put(component);
        }
    }
    return out;
}

// Invariant: the vertices are (0,0,0) (1,0,0) (0,1,0) (1,1,0), so a moved vertex index is read off its position.
std::vector<std::byte> osdShapeNif() {
    nif_fixture::Writer writer;
    for (const auto& point : {std::array<float, 3>{0.0f, 0.0f, 0.0f}, std::array<float, 3>{1.0f, 0.0f, 0.0f},
                              std::array<float, 3>{0.0f, 1.0f, 0.0f}, std::array<float, 3>{1.0f, 1.0f, 0.0f}}) {
        for (const float value : point) writer.f32(value);
        writer.f32(0.0f);
    }
    const std::vector<nif_fixture::Block> blocks{
        nif_fixture::node_block("NiNode", 0, fo4::nif::Transform{}, {1}),
        nif_fixture::shape_block("BSTriShape", 1, fo4::nif::Transform{}, nif_fixture::k_full, 16, -1, -1, -1,
                                 writer.bytes(), {0, 1, 2, 2, 1, 3})};
    return nif_fixture::build_nif({"root", "body"}, blocks, {0});
}

std::vector<float> osdShapeVerts(const std::filesystem::path& nif) {
    return fo4::nif::read_shape(fo4::nif::read_nif(bytesOf(nif)), 1).verts;
}

const OsdOffsets kBreastOffsets{{std::uint16_t{3}, {0.5f, 0.0f, 0.25f}}};
const OsdOffsets kOtherOffsets{{std::uint16_t{0}, {9.0f, 9.0f, 9.0f}}};

constexpr std::string_view kSampleOsp =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<SliderSetInfo version=\"1\"><SliderSet name=\"Body\"><DataFolder>F</DataFolder><SourceFile>Body.nif</SourceFile>"
    "<OutputPath>meshes\\armor</OutputPath><OutputFile>Body</OutputFile>"
    "<Shape target=\"Cuirass\">body</Shape><Shape target=\"Other\">other</Shape>"
    "<Slider name=\"Breasts\" small=\"0\" big=\"100\">"
    "<Data name=\"OtherB\" target=\"Other\" local=\"true\">Body.osd\\Other</Data>"
    "<Data name=\"Breasts\" target=\"Cuirass\" local=\"true\">Body.osd\\Breasts</Data></Slider>"
    "<Slider name=\"OtherOnly\"><Data name=\"OtherB\" target=\"Other\" local=\"true\">Body.osd\\Other</Data></Slider>"
    "<Slider name=\"Flipped\" invert=\"true\"><Data name=\"Flip\" target=\"Cuirass\" local=\"true\">Body.osd\\Breasts</Data></Slider>"
    "</SliderSet></SliderSetInfo>";

}  // namespace

TEST_CASE("NifTarget SkinToolOp osd_apply moves the listed vertex by delta times weight and saves the shape") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "body.nif";
    const auto osd = root / "Body.osd";
    fo4ide::test::writeBytes(nif, osdShapeNif());
    writeLoose(osd, osdBytes({{"Breasts", kBreastOffsets}, {"Other", kOtherOffsets}}));
    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::app::NifTarget target(service, textures, platform, plugins);

    const auto reply = skinToolReply(target, nif, "osd_apply",
        {{"block", 1}, {"osd_path", osd.string()}, {"data_set", "Breasts"}, {"weight", 0.5}});
    REQUIRE_FALSE(reply.contains("error"));
    // Must catch a save that does not reach the file the editor sent, or a moved count that is not the changed vertices.
    CHECK(reply.at("saved").get<std::string>() == nif.string());
    CHECK(reply.at("moved").get<int>() == 1);
    CHECK(reply.at("data_set").get<std::string>() == "Breasts");
    const auto verts = osdShapeVerts(nif);
    REQUIRE(verts.size() == 12);
    // Must catch an offset written without the weight (1.5 instead of 1.25) or onto the wrong vertex.
    CHECK(near(verts[9], 1.25, 1e-5));
    CHECK(near(verts[10], 1.0, 1e-5));
    CHECK(near(verts[11], 0.125, 1e-5));
    // Must catch a data set other than the named one being applied (the Other set would move vertex 0 by 4.5).
    CHECK(near(verts[0], 0.0, 1e-5));
    CHECK(near(verts[3], 1.0, 1e-5));
    CHECK(near(verts[7], 1.0, 1e-5));
}

TEST_CASE("NifTarget SkinToolOp osd_apply writes nothing for weight 0 and refuses bad input") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "body.nif";
    const auto osd = root / "Body.osd";
    fo4ide::test::writeBytes(nif, osdShapeNif());
    writeLoose(osd, osdBytes({{"Breasts", kBreastOffsets}, {"Past", {{std::uint16_t{9}, {1.0f, 0.0f, 0.0f}}}}}));
    const auto before = bytesOf(nif);
    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::app::NifTarget target(service, textures, platform, plugins);

    const nlohmann::json good = {{"block", 1}, {"osd_path", osd.string()}, {"data_set", "Breasts"}, {"weight", 1.0}};
    auto zero = good;
    zero["weight"] = 0.0;
    const auto idle = skinToolReply(target, nif, "osd_apply", zero);
    REQUIRE_FALSE(idle.contains("error"));
    // Must catch a weight 0 apply that still rewrites the NIF or answers a saved path.
    CHECK(idle.at("moved").get<int>() == 0);
    CHECK(idle.at("saved").get<std::string>().empty());
    CHECK(bytesOf(nif) == before);

    const auto refused = [&](const char* label, const nlohmann::json& body, std::string_view expectedSource = {}) {
        const auto answer = skinToolReply(target, nif, "osd_apply", body, expectedSource);
        INFO(label);
        CHECK(answer.contains("error"));
        CHECK(bytesOf(nif) == before);
        return answer;
    };
    auto negative = good;
    negative["weight"] = -0.1;
    // Must catch a weight below 0 reaching the library, which would push vertices the other way.
    refused("negative weight", negative);
    auto high = good;
    high["weight"] = 1.5;
    // Must catch a weight above 1 over-driving a slider.
    refused("weight above one", high);
    auto text = good;
    text["weight"] = "0.5";
    // Must catch a string weight being coerced to a number.
    refused("string weight", text);
    auto missing = good;
    missing.erase("weight");
    // Must catch a missing weight defaulting to a full-strength apply.
    refused("missing weight", missing);
    auto past = good;
    past["data_set"] = "Past";
    // Must catch an offset past the last vertex being written or half applied.
    refused("vertex past the shape", past);
    auto unknown = good;
    unknown["data_set"] = "Nope";
    const auto unknownAnswer = refused("unknown data set", unknown);
    // Must catch an unknown data set falling back to another set instead of naming the missing one.
    CHECK(unknownAnswer.at("error").get<std::string>().find("Nope") != std::string::npos);
    auto both = good;
    both["slider"] = "Breasts";
    // Must catch both selectors being accepted with one silently winning.
    refused("data_set and slider", both);
    auto neither = good;
    neither.erase("data_set");
    // Must catch no selector at all applying something.
    refused("no selector", neither);
    auto gone = good;
    gone["osd_path"] = (root / "missing.osd").string();
    // Must catch an unreadable .osd being treated as an empty one.
    refused("missing osd file", gone);
    auto wrongBlock = good;
    wrongBlock["block"] = 0;
    // Must catch a non-shape block (the root node) being accepted.
    refused("node block", wrongBlock);
    auto noShape = good;
    noShape["block"] = 99;
    // Must catch a block index past the file being accepted.
    refused("block past the file", noShape);
    const auto stale = refused("stale source", good, "0000");
    // Must catch a stale expectedSource no longer stopping a slider save.
    CHECK(stale.at("error").get<std::string>().find("changed on disk") != std::string::npos);

    const auto whole = osdBytes({{"Breasts", kBreastOffsets}});
    writeLoose(osd, std::string_view(whole).substr(0, whole.size() - 3));
    // Must catch a truncated .osd being applied as far as it parses.
    refused("truncated osd", good);
}

TEST_CASE("NifTarget SkinToolOp osp_read and osd_read list sliders and data sets and osd_apply resolves a slider through the shape name") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "body.nif";
    const auto osd = root / "Body.osd";
    const auto osp = root / "Body.osp";
    fo4ide::test::writeBytes(nif, osdShapeNif());
    writeLoose(osd, osdBytes({{"Breasts", kBreastOffsets}, {"Other", kOtherOffsets}}));
    writeLoose(osp, kSampleOsp);
    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::app::NifTarget target(service, textures, platform, plugins);

    const auto listing = skinToolReply(target, std::filesystem::path(), "osp_read", nlohmann::json::object({{"path", osp.string()}}));
    REQUIRE_FALSE(listing.contains("error"));
    const auto& set = listing.at("sets").at(0);
    // Must catch the listing losing the set, its shapes, the sliders, their flags or their data entries.
    CHECK(set.at("name").get<std::string>() == "Body");
    CHECK(set.at("shapes").size() == 2);
    REQUIRE(set.at("sliders").size() == 3);
    CHECK(set.at("sliders").at(0).at("name").get<std::string>() == "Breasts");
    CHECK(set.at("sliders").at(0).at("data").size() == 2);
    CHECK(set.at("sliders").at(0).at("data").at(1).at("name_in_file").get<std::string>() == "Breasts");
    CHECK(set.at("sliders").at(0).at("data").at(1).at("file").get<std::string>() == "Body.osd");
    CHECK(set.at("sliders").at(2).at("invert").get<bool>());
    CHECK_FALSE(set.at("sliders").at(0).at("invert").get<bool>());

    const auto sets = skinToolReply(target, std::filesystem::path(), "osd_read", nlohmann::json::object({{"path", osd.string()}}));
    REQUIRE_FALSE(sets.contains("error"));
    // Must catch the data set names or offset counts being dropped from the .osd listing.
    REQUIRE(sets.at("sets").size() == 2);
    CHECK(sets.at("sets").at(0).at("name").get<std::string>() == "Breasts");
    CHECK(sets.at("sets").at(0).at("offsets").get<int>() == 1);
    // Must catch a listing that parses the wrong format or an unreadable file and answers success instead of an error.
    CHECK(skinToolReply(target, std::filesystem::path(), "osp_read", nlohmann::json::object({{"path", osd.string()}})).contains("error"));
    CHECK(skinToolReply(target, std::filesystem::path(), "osd_read", nlohmann::json::object({{"path", osp.string()}})).contains("error"));
    CHECK(skinToolReply(target, std::filesystem::path(), "osd_read", nlohmann::json::object({{"path", (root / "no.osd").string()}})).contains("error"));

    const auto applied = skinToolReply(target, nif, "osd_apply",
        {{"block", 1}, {"osd_path", osd.string()}, {"osp_path", osp.string()}, {"slider", "Breasts"}, {"weight", 1.0}});
    REQUIRE_FALSE(applied.contains("error"));
    const auto verts = osdShapeVerts(nif);
    // Must catch the first data entry being taken without matching the shape target (vertex 0 would move by 9).
    CHECK(near(verts[0], 0.0, 1e-5));
    CHECK(applied.at("data_set").get<std::string>() == "Breasts");
    CHECK(near(verts[9], 1.5, 1e-5));
    CHECK(near(verts[11], 0.25, 1e-5));

    fo4ide::test::writeBytes(nif, osdShapeNif());
    const auto restored = bytesOf(nif);
    const auto refusedSlider = [&](const char* slider) {
        const auto answer = skinToolReply(target, nif, "osd_apply",
            {{"block", 1}, {"osd_path", osd.string()}, {"osp_path", osp.string()}, {"slider", slider}, {"weight", 1.0}});
        INFO(slider);
        CHECK(answer.contains("error"));
        CHECK(bytesOf(nif) == restored);
        return answer;
    };
    const auto flipped = refusedSlider("Flipped");
    // Must catch an invert slider being added as a plain position offset.
    CHECK(flipped.at("error").get<std::string>().find("invert") != std::string::npos);
    const auto otherOnly = refusedSlider("OtherOnly");
    // Must catch a slider with no data for this shape applying another shape data.
    CHECK(otherOnly.at("error").get<std::string>().find("body") != std::string::npos);
    // Must catch an unknown slider name applying anything.
    refusedSlider("Nope");
}
