#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/nif/controller_keys.hpp>
#include <fo4/nif/nif.hpp>
#include <fo4/nif/shader.hpp>
#include <fo4ide/app/ServiceTargets.hpp>
#include <fo4ide/logging/Logger.hpp>
#include <fo4ide/plugin/IPluginBackend.hpp>

#include "../helpers/NifFixture.hpp"
#include "../helpers/ServiceTargetFakes.hpp"
#include "McpTools.hpp"

using namespace fo4ide::app::test;

namespace {

std::string fileBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

}

TEST_CASE("MCP archive extract tools expose the optional overwrite flag that the refusal text tells users to set") {
    fo4ide::rpc::Dispatcher dispatcher;
    const fo4ide::settings::SharedSettings settings{fo4ide::settings::Settings{}};
    fo4ide::mcp::Registry registry;
    REQUIRE(registerNativeMcpTools(registry, dispatcher, fo4ide::logging::get("mcp-service-targets"), settings,
                                   [] { return std::vector<std::filesystem::path>{}; }));

    std::map<std::string, nlohmann::json> schemas;
    for (const auto& definition : registry.definitions()) {
        schemas[definition.at("name").get<std::string>()] = definition.at("inputSchema");
    }
    for (const auto* name : {"archive_extract_file", "archive_extract_selected", "archive_extract_all"}) {
        INFO(name);
        REQUIRE(schemas.contains(name));
        const auto& schema = schemas.at(name);
        REQUIRE(schema.at("properties").contains("overwrite"));
        CHECK(schema.at("properties").at("overwrite").at("type") == "boolean");
        const auto& required = schema.at("required");
        CHECK(std::find(required.begin(), required.end(), "overwrite") == required.end());
    }
}

TEST_CASE("MCP NIF write tools refuse a different existing out_nif until overwrite is set and still save in place") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto uv = root / "uv.nif";
    const auto paint = root / "paint.nif";
    const auto untextured = root / "untextured.nif";
    const auto connect = root / "connect.nif";
    fo4ide::test::writeBytes(uv, fo4ide::test::dualUvTriangleNif());
    fo4ide::test::writeBytes(paint, fo4ide::test::coloredLightingNif());
    fo4ide::test::writeBytes(untextured, fo4ide::test::nifFile(
        {{"NiNode", fo4ide::test::nodeBlock(2U, 0.5f, {1U})},
         {"BSTriShape", fo4ide::test::triangleShapeBlock(2U, 0.25f)},
         {"BSLightingShaderProperty", fo4ide::test::shaderBlock(1U, 0xFFFFFFFFU)}},
        {"Tri", "Material", "Root"}));
    fo4ide::test::writeBytes(connect, fo4ide::test::connectNif());
    const auto uvBlock = fo4::nif::read_shapes(fo4::nif::read_nif(fo4ide::test::dualUvTriangleNif())).at(0).block;
    const auto paintBlock = fo4::nif::read_shapes(fo4::nif::read_nif(fo4ide::test::coloredLightingNif())).at(0).block;

    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::rpc::Dispatcher dispatcher;
    REQUIRE(dispatcher.registerTarget("nif", std::make_shared<fo4ide::app::NifTarget>(service, textures, platform, plugins)).has_value());
    const fo4ide::settings::SharedSettings settings{[&] {
        fo4ide::settings::Settings allowed;
        allowed.mo2InstancePath = root;
        return allowed;
    }()};
    fo4ide::mcp::Registry registry;
    REQUIRE(registerNativeMcpTools(registry, dispatcher, fo4ide::logging::get("mcp-service-targets"), settings,
                                   [] { return std::vector<std::filesystem::path>{}; }));

    const std::vector<std::pair<std::string, nlohmann::json>> cases{
        {"nif_uv_transform", {{"path", uv.string()}, {"block", uvBlock}, {"du", 0.25}}},
        {"nif_vertex_color_set", {{"path", paint.string()}, {"block", paintBlock}, {"kind", "fill"},
                                  {"color", nlohmann::json::array({0.0, 0.0, 0.0, 1.0})}}},
        {"nif_create_texture_set", {{"path", untextured.string()}, {"block", 1},
                                    {"textures", nlohmann::json::array({"Textures\\Thing_d.dds"})}}},
        {"nif_connect_points_set", {{"path", connect.string()}, {"ops", "[]"}}},
    };
    for (const auto& [tool, base] : cases) {
        INFO(tool);
        const auto out = root / (tool + "-out.nif");
        touch(out, "sentinel");
        auto args = base;
        args["out_nif"] = out.string();

        const auto refused = registry.call(tool, args);
        CHECK(refused.isError);
        CHECK(refused.text.starts_with("EXISTS: "));
        CHECK(fileBytes(out) == "sentinel");

        args["overwrite"] = true;
        const auto replaced = registry.call(tool, args);
        CHECK_FALSE(replaced.isError);
        CHECK(fileBytes(out) != "sentinel");

        auto inPlace = base;
        inPlace["out_nif"] = base.at("path");
        const auto saved = registry.call(tool, inPlace);
        CHECK_FALSE(saved.isError);
    }
}

TEST_CASE("MCP NIF structural tools attach an alpha property and remove its block again") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto plain = root / "plain.nif";
    const auto withAlpha = root / "alpha.nif";
    const auto stripped = root / "stripped.nif";
    fo4ide::test::writeBytes(plain, fo4ide::test::nifFile(
        {{"NiNode", fo4ide::test::nodeBlock(2U, 0.5f, {1U})},
         {"BSTriShape", fo4ide::test::triangleShapeBlock(2U, 0.25f)},
         {"BSLightingShaderProperty", fo4ide::test::shaderBlock(1U, 0xFFFFFFFFU)}},
        {"Tri", "Material", "Root"}));
    const auto readBack = [](const std::filesystem::path& path) {
        const std::string text = fileBytes(path);
        std::vector<std::byte> bytes;
        bytes.reserve(text.size());
        for (const char c : text) bytes.push_back(static_cast<std::byte>(c));
        return fo4::nif::read_nif(bytes);
    };

    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::rpc::Dispatcher dispatcher;
    REQUIRE(dispatcher.registerTarget("nif", std::make_shared<fo4ide::app::NifTarget>(service, textures, platform, plugins)).has_value());
    const fo4ide::settings::SharedSettings settings{[&] {
        fo4ide::settings::Settings allowed;
        allowed.mo2InstancePath = root;
        return allowed;
    }()};
    fo4ide::mcp::Registry registry;
    REQUIRE(registerNativeMcpTools(registry, dispatcher, fo4ide::logging::get("mcp-service-targets"), settings,
                                   [] { return std::vector<std::filesystem::path>{}; }));

    const auto attached = registry.call("nif_attach_alpha", {{"path", plain.string()}, {"block", 1},
                                                              {"out_nif", withAlpha.string()}});
    INFO(attached.text);
    REQUIRE_FALSE(attached.isError);
    const auto alphaNif = readBack(withAlpha);
    const auto alphaRef = fo4::nif::read_shapes(alphaNif).at(0).alpha_ref;
    REQUIRE(alphaRef >= 0);
    const auto alphaBlock = static_cast<std::uint32_t>(alphaRef);
    REQUIRE(alphaNif.blocks().at(alphaBlock).type == "NiAlphaProperty");
    const auto property = fo4::nif::read_alpha(alphaNif, alphaBlock);
    CHECK(property.flags == 0xECU);
    CHECK(property.threshold == 128);

    const auto duplicate = registry.call("nif_attach_alpha", {{"path", withAlpha.string()}, {"block", 1},
                                                               {"out_nif", withAlpha.string()}, {"overwrite", true}});
    CHECK(duplicate.isError);

    const auto badFlags = registry.call("nif_attach_alpha", {{"path", plain.string()}, {"block", 1},
                                                               {"flags", 70000}, {"out_nif", withAlpha.string()},
                                                               {"overwrite", true}});
    CHECK(badFlags.isError);
    CHECK(badFlags.text.find("flags from 0 to 65535") != std::string::npos);

    const auto owned = registry.call("nif_remove_block", {{"path", withAlpha.string()}, {"block", alphaBlock},
                                                               {"out_nif", stripped.string()}});
    CHECK(owned.isError);
    CHECK(owned.text.find("still referenced") != std::string::npos);

    const auto removed = registry.call("nif_remove_block", {{"path", withAlpha.string()}, {"block", 1},
                                                              {"out_nif", stripped.string()}});
    INFO(removed.text);
    REQUIRE_FALSE(removed.isError);
    CHECK(removed.text.find("Removed:") != std::string::npos);
    const auto strippedNif = readBack(stripped);
    REQUIRE(strippedNif.blocks().size() == 1U);
    CHECK(strippedNif.blocks().at(0).type == "NiNode");

    const auto keepRoot = registry.call("nif_remove_block", {{"path", stripped.string()}, {"block", 0},
                                                               {"out_nif", stripped.string()}, {"overwrite", true}});
    CHECK(keepRoot.isError);
}

// Detects: nif_block_op reaching the wrong Radicle op or dropping a required argument; a bake that changed nothing must be refused, never written as an unchanged copy.
// Detects: a join that keeps the donor block or loses the target's material, a separate that drops, repeats or mis-parents triangles, a lost groups argument; a refused join or separate must never write a file.
TEST_CASE("MCP nif_block_op duplicates, appends, sorts, merges, bakes, joins and separates through the dispatcher and refuses bad input") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto base = root / "base.nif";
    const auto donor = root / "donor.nif";
    fo4ide::test::writeBytes(base, fo4ide::test::nifFile(
        {{"NiNode", fo4ide::test::nodeBlock(3U, 0.5f, {1U})},
         {"BSTriShape", fo4ide::test::triangleShapeBlock(0xFFFFFFFFU, 0.25f)},
         {"NiNode", fo4ide::test::nodeBlock(1U, 0.0f, {})},
         {"NiNode", fo4ide::test::nodeBlock(2U, 0.0f, {})}},
        {"Tri", "Zed", "Alpha", "Root"}));
    fo4ide::test::writeBytes(donor, fo4ide::test::nifFile(
        {{"NiNode", fo4ide::test::nodeBlock(1U, 0.0f, {1U})},
         {"BSTriShape", fo4ide::test::triangleShapeBlock(0xFFFFFFFFU, 0.0f, 0xFFFFFFFFU, 0U)}},
        {"DonorShape", "DonorRoot"}));
    const auto readBack = [](const std::filesystem::path& path) {
        const std::string text = fileBytes(path);
        std::vector<std::byte> bytes;
        bytes.reserve(text.size());
        for (const char c : text) bytes.push_back(static_cast<std::byte>(c));
        return fo4::nif::read_nif(bytes);
    };

    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::rpc::Dispatcher dispatcher;
    REQUIRE(dispatcher.registerTarget("nif", std::make_shared<fo4ide::app::NifTarget>(service, textures, platform, plugins)).has_value());
    const fo4ide::settings::SharedSettings settings{[&] {
        fo4ide::settings::Settings allowed;
        allowed.mo2InstancePath = root;
        return allowed;
    }()};
    fo4ide::mcp::Registry registry;
    REQUIRE(registerNativeMcpTools(registry, dispatcher, fo4ide::logging::get("mcp-service-targets"), settings,
                                   [] { return std::vector<std::filesystem::path>{}; }));
    const auto call = [&](const std::string& op, const std::filesystem::path& from, const std::filesystem::path& to,
                          nlohmann::json extra) {
        extra["path"] = from.string();
        extra["op"] = op;
        extra["out_nif"] = to.string();
        return registry.call("nif_block_op", extra);
    };

    const auto unknown = call("explode", base, root / "x.nif", {});
    CHECK(unknown.isError);
    CHECK(unknown.text.find("Unknown block op") != std::string::npos);
    const auto noChild = call("append_child", base, root / "x.nif", {{"block", 0}});
    CHECK(noChild.isError);
    CHECK(noChild.text.find("needs child") != std::string::npos);
    const auto noBlocks = call("bake_shape_transforms", base, root / "x.nif", {});
    CHECK(noBlocks.isError);
    CHECK(noBlocks.text.find("needs blocks") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(root / "x.nif"));

    const auto duplicated = call("duplicate_block", base, root / "dup.nif", {{"block", 1}});
    INFO(duplicated.text);
    REQUIRE_FALSE(duplicated.isError);
    const auto dupNif = readBack(root / "dup.nif");
    REQUIRE(dupNif.blocks().size() == 5U);
    CHECK(dupNif.blocks().at(4).type == "BSTriShape");
    CHECK(dupNif.parent(4) == -1);
    CHECK(call("duplicate_block", base, root / "dup.nif", {{"block", 1}}).isError);
    const auto withChildren = call("duplicate_block", base, root / "x.nif", {{"block", 0}});
    CHECK(withChildren.isError);
    CHECK(withChildren.text.find("has children") != std::string::npos);

    const auto adopted = call("append_child", base, root / "adopt1.nif", {{"block", 0}, {"child", 2}});
    INFO(adopted.text);
    REQUIRE_FALSE(adopted.isError);
    REQUIRE_FALSE(call("append_child", root / "adopt1.nif", root / "adopt2.nif", {{"block", 0}, {"child", 3}}).isError);
    const auto adoptedNif = readBack(root / "adopt2.nif");
    CHECK(adoptedNif.children(0) == std::vector<std::uint32_t>{1, 2, 3});
    CHECK(call("append_child", base, root / "x.nif", {{"block", 0}, {"child", 1}}).isError);

    const auto sorted = call("sort_children_by_name", root / "adopt2.nif", root / "sorted.nif", {{"block", 0}});
    INFO(sorted.text);
    REQUIRE_FALSE(sorted.isError);
    CHECK(readBack(root / "sorted.nif").children(0) == std::vector<std::uint32_t>{3, 1, 2});

    const auto moved = call("reparent_block", root / "adopt2.nif", root / "moved.nif", {{"block", 2}, {"child", 1}});
    INFO(moved.text);
    REQUIRE_FALSE(moved.isError);
    const auto movedNif = readBack(root / "moved.nif");
    CHECK(movedNif.children(0) == std::vector<std::uint32_t>{2, 3});
    CHECK(movedNif.children(2) == std::vector<std::uint32_t>{1});
    CHECK(movedNif.parent(1) == 2);
    const auto noMoveChild = call("reparent_block", root / "adopt2.nif", root / "x.nif", {{"block", 2}});
    CHECK(noMoveChild.isError);
    CHECK(noMoveChild.text.find("needs child") != std::string::npos);
    REQUIRE_FALSE(call("reparent_block", root / "adopt2.nif", root / "nested.nif", {{"block", 2}, {"child", 3}}).isError);
    const auto cycle = call("reparent_block", root / "nested.nif", root / "x.nif", {{"block", 3}, {"child", 2}});
    CHECK(cycle.isError);
    CHECK(cycle.text.find("make a cycle") != std::string::npos);
    const auto rootMove = call("reparent_block", root / "adopt2.nif", root / "x.nif", {{"block", 2}, {"child", 0}});
    CHECK(rootMove.isError);
    CHECK_FALSE(std::filesystem::exists(root / "x.nif"));

    const auto merged = call("merge_nif_into", base, root / "merged.nif", {{"block", 0}, {"donor_path", donor.string()}});
    INFO(merged.text);
    REQUIRE_FALSE(merged.isError);
    const auto mergedNif = readBack(root / "merged.nif");
    REQUIRE(mergedNif.blocks().size() == 6U);
    CHECK(mergedNif.blocks().at(5).type == "BSTriShape");
    CHECK(mergedNif.children(0) == std::vector<std::uint32_t>{1, 4});
    const auto noDonor = call("merge_nif_into", base, root / "x.nif", {{"block", 0}});
    CHECK(noDonor.isError);
    CHECK(noDonor.text.find("needs donor_path") != std::string::npos);

    const auto baked = call("bake_shape_transforms", base, root / "baked.nif", {{"blocks", nlohmann::json::array({1U})}});
    INFO(baked.text);
    REQUIRE_FALSE(baked.isError);
    CHECK(baked.text.find("Baked: 1") != std::string::npos);
    const auto bakedNif = readBack(root / "baked.nif");
    const auto bakedShape = fo4::nif::read_shape(bakedNif, 1);
    REQUIRE(bakedShape.verts.size() == 9U);
    CHECK(std::abs(bakedShape.verts[0] - 0.25f) < 1e-5f);
    CHECK(std::abs(bakedShape.verts[3] - 1.25f) < 1e-5f);
    CHECK(std::abs(bakedShape.verts[6] - 0.25f) < 1e-5f);
    const auto nothing = call("bake_shape_transforms", base, root / "x.nif", {{"blocks", nlohmann::json::array({2U})}});
    CHECK(nothing.isError);
    CHECK(nothing.text.find("Nothing baked") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(root / "x.nif"));

    const auto shapesNif = [&](float secondX) {
        return fo4ide::test::nifFile(
            {{"NiNode", fo4ide::test::nodeBlock(2U, 0.5f, {1U, 2U})},
             {"BSTriShape", fo4ide::test::triangleShapeBlock(3U, 0.25f)},
             {"BSTriShape", fo4ide::test::triangleShapeBlock(3U, secondX)},
             {"BSLightingShaderProperty", fo4ide::test::shaderBlock(1U, 4U)},
             {"BSShaderTextureSet", fo4ide::test::texturedSetBlock({"textures\\a_d.dds"})}},
            {"Tri", "Materials\\Clutter\\Thing.BGSM", "Root"});
    };
    const auto pair = root / "pair.nif";
    const auto skewed = root / "skewed.nif";
    fo4ide::test::writeBytes(pair, shapesNif(0.25f));
    fo4ide::test::writeBytes(skewed, shapesNif(0.0f));
    const auto ids = [](std::initializer_list<std::uint32_t> list) { return nlohmann::json(std::vector<std::uint32_t>(list)); };

    const auto tooFew = call("join_shapes", pair, root / "x.nif", {{"blocks", ids({1})}});
    CHECK(tooFew.isError);
    CHECK(tooFew.text.find("at least two blocks") != std::string::npos);
    const auto noJoinBlocks = call("join_shapes", pair, root / "x.nif", {});
    CHECK(noJoinBlocks.isError);
    CHECK(noJoinBlocks.text.find("needs blocks") != std::string::npos);
    const auto nodeJoin = call("join_shapes", pair, root / "x.nif", {{"blocks", ids({1, 0})}});
    CHECK(nodeJoin.isError);
    CHECK(nodeJoin.text.find("supports only BSTriShape") != std::string::npos);
    const auto skewJoin = call("join_shapes", skewed, root / "x.nif", {{"blocks", ids({1, 2})}});
    CHECK(skewJoin.isError);
    CHECK(skewJoin.text.find("bake it first") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(root / "x.nif"));

    const auto joined = call("join_shapes", pair, root / "joined.nif", {{"blocks", ids({1, 2})}});
    INFO(joined.text);
    REQUIRE_FALSE(joined.isError);
    CHECK(joined.text.find("Target: 1") != std::string::npos);
    CHECK(joined.text.find("Removed: 2") != std::string::npos);
    CHECK(joined.text.find("Vertices: 3 -> 6") != std::string::npos);
    CHECK(joined.text.find("Triangles: 1 -> 2") != std::string::npos);
    const auto joinedNif = readBack(root / "joined.nif");
    REQUIRE(joinedNif.blocks().size() == 4U);
    CHECK(joinedNif.children(0) == std::vector<std::uint32_t>{1});
    const auto joinedShape = fo4::nif::read_shape(joinedNif, 1);
    CHECK(joinedShape.verts.size() == 18U);
    CHECK(joinedShape.tris.size() == 6U);
    CHECK(joinedShape.material == "Materials\\Clutter\\Thing.BGSM");

    const auto noGroups = call("separate_shape", root / "joined.nif", root / "x.nif", {{"block", 1}});
    CHECK(noGroups.isError);
    CHECK(noGroups.text.find("needs groups") != std::string::npos);
    const auto repeated = call("separate_shape", root / "joined.nif", root / "x.nif",
                               {{"block", 1}, {"groups", nlohmann::json::array({ids({0}), ids({0})})}});
    CHECK(repeated.isError);
    CHECK(repeated.text.find("twice") != std::string::npos);
    const auto oneGroup = call("separate_shape", root / "joined.nif", root / "x.nif",
                               {{"block", 1}, {"groups", nlohmann::json::array({ids({0, 1})})}});
    CHECK(oneGroup.isError);
    CHECK(oneGroup.text.find("at least two groups") != std::string::npos);
    const auto unskinned = call("separate_by_bone", root / "joined.nif", root / "x.nif", {{"block", 1}});
    CHECK(unskinned.isError);
    CHECK(unskinned.text.find("needs a skinned shape") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(root / "x.nif"));

    const auto separated = call("separate_shape", root / "joined.nif", root / "split.nif",
                                {{"block", 1}, {"groups", nlohmann::json::array({ids({0}), ids({1})})}});
    INFO(separated.text);
    REQUIRE_FALSE(separated.isError);
    CHECK(separated.text.find("Shapes: 1, 4") != std::string::npos);
    const auto splitNif = readBack(root / "split.nif");
    REQUIRE(splitNif.blocks().size() == 5U);
    CHECK(splitNif.blocks().at(4).type == "BSTriShape");
    CHECK(splitNif.children(0) == std::vector<std::uint32_t>{1, 4});
    for (const std::uint32_t half : {1U, 4U}) {
        const auto shape = fo4::nif::read_shape(splitNif, half);
        CHECK(shape.tris.size() == 3U);
        CHECK(shape.verts.size() == 9U);
    }
}

// Detects: apply_pose writing the wrong pose or bone must fail the translation check; a pose that wrote nothing must be refused, not saved as success; nif_pose_list must keep the names and the skin dry-run.
// Detects: set_keys must write each key at its own channel position, a negative value and a float interpolation must not go through the unsigned id path, and a refused edit must leave no file.
TEST_CASE("MCP nif_block_op poses a skin from a pose library and writes keys, and nif_pose_list reads the library") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto skinned = root / "skinned.nif";
    const auto keyed = root / "keyed.nif";
    const auto library = root / "poses.json";
    std::vector<std::byte> instance;
    for (const std::uint32_t word : {0U, 3U, 1U, 1U, 0U}) fo4ide::test::putUint(instance, word, 4);
    std::vector<std::byte> boneData;
    fo4ide::test::putUint(boneData, 1, 4);
    for (int i = 0; i < 17; ++i) fo4ide::test::putFloat(boneData, i == 16 ? 1.0f : 0.0f);
    fo4ide::test::writeBytes(skinned, fo4ide::test::nifFile(
        {{"NiNode", fo4ide::test::nodeBlock(1U, 0.0f, {1U})},
         {"NiNode", fo4ide::test::nodeBlock(0U, 0.0f, {})},
         {"BSSkin::Instance", instance},
         {"BSSkin::BoneData", boneData}},
        {"RootBone", "Root"}));
    std::vector<std::byte> floatData;
    for (const std::uint32_t word : {2U, 1U}) fo4ide::test::putUint(floatData, word, 4);
    for (const float word : {0.0f, 1.5f, 1.0f, 2.5f}) fo4ide::test::putFloat(floatData, word);
    fo4ide::test::writeBytes(keyed, fo4ide::test::nifFile({{"NiFloatData", floatData}}, {"Keys"}));
    {
        std::ofstream stream(library);
        stream << R"({"poses":[
            {"name":"idle","bones":{"RootBone":{"t":[1,2,3],"r":[0,0,0,1],"s":1}}},
            {"name":"ghost","bones":{"Ghost":{"t":[0,0,0],"r":[0,0,0,1],"s":1}}}]})";
    }
    const auto readBack = [](const std::filesystem::path& path) {
        const std::string text = fileBytes(path);
        std::vector<std::byte> bytes;
        bytes.reserve(text.size());
        for (const char c : text) bytes.push_back(static_cast<std::byte>(c));
        return fo4::nif::read_nif(bytes);
    };

    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::rpc::Dispatcher dispatcher;
    REQUIRE(dispatcher.registerTarget("nif", std::make_shared<fo4ide::app::NifTarget>(service, textures, platform, plugins)).has_value());
    const fo4ide::settings::SharedSettings settings{[&] {
        fo4ide::settings::Settings allowed;
        allowed.mo2InstancePath = root;
        return allowed;
    }()};
    fo4ide::mcp::Registry registry;
    REQUIRE(registerNativeMcpTools(registry, dispatcher, fo4ide::logging::get("mcp-service-targets"), settings,
                                   [] { return std::vector<std::filesystem::path>{}; }));
    const auto call = [&](const std::string& op, const std::filesystem::path& from, const std::filesystem::path& to,
                          nlohmann::json extra) {
        extra["path"] = from.string();
        extra["op"] = op;
        extra["out_nif"] = to.string();
        return registry.call("nif_block_op", extra);
    };
    const auto pose = [&](const std::string& name, std::uint32_t block, const std::filesystem::path& to) {
        return call("apply_pose", skinned, to, {{"block", block}, {"pose_path", library.string()}, {"pose", name}});
    };
    const auto key = [](float time, float value) { return nlohmann::json{{"time", time}, {"value", nlohmann::json::array({value})}}; };
    const auto keyList = [](std::initializer_list<nlohmann::json> list) { return nlohmann::json(std::vector<nlohmann::json>(list)); };

    const auto listed = registry.call("nif_pose_list", {{"pose_path", library.string()}});
    INFO(listed.text);
    REQUIRE_FALSE(listed.isError);
    const auto names = nlohmann::json::parse(listed.text).at("poses");
    REQUIRE(names.size() == 2U);
    CHECK(names[0].at("name") == "idle");
    CHECK(names[0].at("bones") == 1);
    CHECK(names[1].at("name") == "ghost");
    CHECK_FALSE(names[0].contains("applied"));
    const auto dry = registry.call("nif_pose_list", {{"pose_path", library.string()}, {"path", skinned.string()}, {"block", 2}});
    INFO(dry.text);
    REQUIRE_FALSE(dry.isError);
    const auto verdicts = nlohmann::json::parse(dry.text).at("poses");
    CHECK(verdicts[0].at("applied") == 1);
    CHECK(verdicts[1].at("applied") == 0);
    CHECK(verdicts[1].at("unknown_pose_bones") == nlohmann::json::array({"Ghost"}));
    CHECK(verdicts[1].at("unposed_bones") == nlohmann::json::array({"RootBone"}));
    const auto notSkin = registry.call("nif_pose_list", {{"pose_path", library.string()}, {"path", skinned.string()}, {"block", 0}});
    REQUIRE_FALSE(notSkin.isError);
    CHECK(nlohmann::json::parse(notSkin.text).at("poses")[0].at("error").get<std::string>().find("not a skin instance") != std::string::npos);
    const auto lonely = registry.call("nif_pose_list", {{"pose_path", library.string()}, {"path", skinned.string()}});
    CHECK(lonely.isError);
    CHECK(lonely.text.find("go together") != std::string::npos);
    CHECK(registry.call("nif_pose_list", {{"pose_path", (root / "missing.json").string()}}).isError);

    const auto posed = pose("idle", 2, root / "posed.nif");
    INFO(posed.text);
    REQUIRE_FALSE(posed.isError);
    CHECK(posed.text.find("Applied: 1") != std::string::npos);
    const auto bones = fo4::nif::read_bones(readBack(root / "posed.nif"), 2);
    REQUIRE(bones.size() == 1U);
    CHECK(bones[0].translation[0] == 1.0f);
    CHECK(bones[0].translation[1] == 2.0f);
    CHECK(bones[0].translation[2] == 3.0f);
    const auto nothing = pose("ghost", 2, root / "x.nif");
    CHECK(nothing.isError);
    CHECK(nothing.text.find("Nothing posed") != std::string::npos);
    CHECK(nothing.text.find("Unknown pose bones: Ghost") != std::string::npos);
    const auto unknown = pose("nope", 2, root / "x.nif");
    CHECK(unknown.isError);
    CHECK(unknown.text.find("Poses: idle, ghost") != std::string::npos);
    const auto onNode = pose("idle", 0, root / "x.nif");
    CHECK(onNode.isError);
    CHECK(onNode.text.find("not a skin instance") != std::string::npos);
    const auto negativeBlock = call("apply_pose", skinned, root / "x.nif", {{"block", -1}, {"pose_path", library.string()}, {"pose", "idle"}});
    CHECK(negativeBlock.isError);
    CHECK(negativeBlock.text.find("whole block index") != std::string::npos);
    const auto fractionalBlock = call("apply_pose", skinned, root / "x.nif", {{"block", 2.5}, {"pose_path", library.string()}, {"pose", "idle"}});
    CHECK(fractionalBlock.isError);
    const auto noPose = call("apply_pose", skinned, root / "x.nif", {{"block", 2}});
    CHECK(noPose.isError);
    CHECK(noPose.text.find("needs pose_path and pose") != std::string::npos);
    const auto noLibrary = call("apply_pose", skinned, root / "x.nif", {{"block", 2}, {"pose_path", (root / "missing.json").string()}, {"pose", "idle"}});
    CHECK(noLibrary.isError);
    CHECK_FALSE(std::filesystem::exists(root / "x.nif"));

    const auto before = registry.call("nif_keys_get", {{"path", keyed.string()}, {"block", 0}});
    INFO(before.text);
    REQUIRE_FALSE(before.isError);
    const auto beforeChannels = nlohmann::json::parse(before.text).at("channels");
    REQUIRE(beforeChannels.size() == 1U);
    CHECK(beforeChannels[0].at("channel") == "data");
    CHECK(beforeChannels[0].at("interpolation") == 1);
    CHECK(beforeChannels[0].at("components") == 1);
    REQUIRE(beforeChannels[0].at("keys").size() == 2U);
    CHECK(beforeChannels[0].at("keys")[0].at("value")[0].get<float>() == 1.5f);
    CHECK(beforeChannels[0].at("keys")[1].at("time").get<float>() == 1.0f);
    CHECK(beforeChannels[0].at("keys")[1].at("value")[0].get<float>() == 2.5f);
    CHECK(registry.call("nif_keys_get", {{"path", keyed.string()}}).isError);
    const auto notKeys = registry.call("nif_keys_get", {{"path", skinned.string()}, {"block", 2}});
    CHECK(nlohmann::json::parse(notKeys.text).contains("error"));

    const auto written = call("set_keys", keyed, root / "keys.nif",
                              {{"block", 0}, {"channel", "data"}, {"keys", keyList({key(0.0f, -1.5f), key(2.0f, 4.0f), key(3.0f, 0.25f)})}});
    INFO(written.text);
    REQUIRE_FALSE(written.isError);
    CHECK(written.text.find("Keys: 3") != std::string::npos);
    const auto channel = fo4::nif::read_controller_keys(readBack(root / "keys.nif"), 0).data;
    REQUIRE(channel.keys.size() == 3U);
    CHECK(channel.interpolation == fo4::nif::k_key_linear);
    CHECK(channel.keys[1].time == 2.0f);
    float stored = 0.0f;
    std::memcpy(&stored, channel.keys[0].value.data(), sizeof stored);
    CHECK(stored == -1.5f);
    std::memcpy(&stored, channel.keys[1].value.data(), sizeof stored);
    CHECK(stored == 4.0f);
    const auto after = registry.call("nif_keys_get", {{"path", (root / "keys.nif").string()}, {"block", 0}});
    INFO(after.text);
    REQUIRE_FALSE(after.isError);
    const auto afterKeys = nlohmann::json::parse(after.text).at("channels")[0].at("keys");
    REQUIRE(afterKeys.size() == 3U);
    CHECK(afterKeys[0].at("value")[0].get<float>() == -1.5f);
    CHECK(afterKeys[2].at("time").get<float>() == 3.0f);
    CHECK(afterKeys[2].at("value")[0].get<float>() == 0.25f);

    const auto setKeys = [&](nlohmann::json extra) {
        extra["block"] = 0;
        return call("set_keys", keyed, root / "x.nif", std::move(extra));
    };
    const auto wide = setKeys({{"channel", "data"}, {"keys", keyList({nlohmann::json{{"time", 0.0}, {"value", nlohmann::json::array({1.0, 2.0})}}})}});
    CHECK(wide.isError);
    CHECK(wide.text.find("needs 1 numbers") != std::string::npos);
    const auto xyzKind = setKeys({{"channel", "data"}, {"interpolation", 4}, {"keys", keyList({key(0.0f, 1.0f)})}});
    CHECK(xyzKind.isError);
    CHECK(xyzKind.text.find("interpolation must be 1") != std::string::npos);
    const auto negativeKind = setKeys({{"channel", "data"}, {"interpolation", -3}, {"keys", keyList({key(0.0f, 1.0f)})}});
    CHECK(negativeKind.isError);
    CHECK(negativeKind.text.find("interpolation must be a whole number") != std::string::npos);
    CHECK(setKeys({{"channel", "data"}, {"interpolation", 1.5}, {"keys", keyList({key(0.0f, 1.0f)})}}).isError);
    const auto backwards = setKeys({{"channel", "data"}, {"keys", keyList({key(2.0f, 1.0f), key(1.0f, 1.0f)})}});
    CHECK(backwards.isError);
    CHECK(backwards.text.find("non-decreasing") != std::string::npos);
    const auto wrongChannel = setKeys({{"channel", "translation"}, {"keys", keyList({key(0.0f, 1.0f)})}});
    CHECK(wrongChannel.isError);
    CHECK(wrongChannel.text.find("does not exist on NiFloatData") != std::string::npos);
    const auto badKey = setKeys({{"channel", "data"}, {"keys", keyList({nlohmann::json{{"time", "soon"}, {"value", nlohmann::json::array({1.0})}}})}});
    CHECK(badKey.isError);
    const auto noKeys = setKeys({{"channel", "data"}});
    CHECK(noKeys.isError);
    CHECK(noKeys.text.find("needs channel and keys") != std::string::npos);
    const auto onSkin = call("set_keys", skinned, root / "x.nif", {{"block", 2}, {"channel", "data"}, {"keys", keyList({key(0.0f, 1.0f)})}});
    CHECK(onSkin.isError);
    CHECK_FALSE(std::filesystem::exists(root / "x.nif"));
}

// Detects: nif_shape_op dropping a model-op argument or reading it from the wrong position (a knife normal or offset arriving as zero), and a cut that loses vertices or triangles on the way to the file.
// Detects: a float read through the unsigned id path, a ring or edge entry that is not exactly two vertices, a request naming two blocks or none, and an existing out_nif replaced without overwrite; every refusal must leave no file.
TEST_CASE("MCP nif_shape_op cuts a shape with knife_plane and refuses bad model-op input through the dispatcher") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto source = root / "source.nif";
    const auto out = root / "out.nif";
    fo4ide::test::writeBytes(source, fo4ide::test::nifFile(
        {{"NiNode", fo4ide::test::nodeBlock(1U, 0.0f, {1U})},
         {"BSTriShape", fo4ide::test::triangleShapeBlock(0xFFFFFFFFU, 0.25f)}},
        {"Tri", "Root"}));

    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::rpc::Dispatcher dispatcher;
    REQUIRE(dispatcher.registerTarget("nif", std::make_shared<fo4ide::app::NifTarget>(service, textures, platform, plugins)).has_value());
    const fo4ide::settings::SharedSettings settings{[&] {
        fo4ide::settings::Settings allowed;
        allowed.mo2InstancePath = root;
        return allowed;
    }()};
    fo4ide::mcp::Registry registry;
    REQUIRE(registerNativeMcpTools(registry, dispatcher, fo4ide::logging::get("mcp-service-targets"), settings,
                                   [] { return std::vector<std::filesystem::path>{}; }));
    const auto call = [&](const std::string& op, nlohmann::json extra) {
        extra["nif_path"] = source.string();
        extra["op"] = op;
        extra["out_nif"] = out.string();
        return registry.call("nif_shape_op", extra);
    };
    const auto refused = [&](const std::string& op, const nlohmann::json& extra, const std::string& text) {
        INFO(op << ' ' << extra.dump());
        const auto reply = call(op, extra);
        CHECK(reply.isError);
        CHECK(reply.text.find(text) != std::string::npos);
        CHECK_FALSE(std::filesystem::exists(out));
    };
    const auto ids = [](std::initializer_list<std::uint32_t> list) { return nlohmann::json(std::vector<std::uint32_t>(list)); };
    const auto xPlane = nlohmann::json::array({1.0, 0.0, 0.0});

    refused("knife_plane", {{"blocks", ids({1})}, {"normal", xPlane}}, "needs normal and offset");
    refused("knife_plane", {{"blocks", ids({1})}, {"offset", 0.5}}, "needs normal and offset");
    refused("knife_plane", {{"normal", xPlane}, {"offset", 0.5}}, "exactly one block");
    refused("knife_plane", {{"blocks", ids({0, 1})}, {"normal", xPlane}, {"offset", 0.5}}, "exactly one block");
    refused("knife_plane", {{"blocks", ids({9})}, {"normal", xPlane}, {"offset", 0.5}}, "names a block past the file");
    refused("knife_plane", {{"blocks", ids({0})}, {"normal", xPlane}, {"offset", 0.5}}, "only BSTriShape is supported");
    refused("knife_plane", {{"blocks", ids({1})}, {"normal", nlohmann::json::array({0.0, 0.0, 0.0})}, {"offset", 0.0}},
            "knife normal must be non-zero");
    refused("knife_plane", {{"blocks", ids({1})}, {"normal", nlohmann::json::array({1.0, 0.0})}, {"offset", 0.5}},
            "normal must be a list of three numbers");
    refused("knife_plane", {{"blocks", ids({1})}, {"normal", xPlane}, {"offset", "0.5"}}, "offset must be");
    refused("extrude_faces", {{"blocks", ids({1})}, {"tris", nlohmann::json::array({0.5})}, {"distance", 1.0}},
            "tris must be a list of triangle indices");
    refused("extrude_faces", {{"blocks", ids({1})}, {"tris", ids({0})}, {"distance", 1.0}}, "which are missing");
    refused("bridge_loops", {{"blocks", ids({1})}, {"loop_a", ids({0, 1})}}, "needs loop_a and loop_b");
    refused("cut_loop", {{"blocks", ids({1})}, {"edges", nlohmann::json::array({ids({0, 1, 2})})}},
            "edges must be a list of [vertex, vertex] pairs");
    refused("cut_loop", {{"blocks", ids({1})}, {"edges", nlohmann::json::array({ids({0})})}},
            "edges must be a list of [vertex, vertex] pairs");
    refused("cut_loop", {{"blocks", ids({1})}, {"edges", nlohmann::json::array({nlohmann::json::array({0.5, 1.0})})}},
            "edges must be a list of [vertex, vertex] pairs");
    refused("bevel_edges", {{"blocks", ids({1})}, {"edges", nlohmann::json::array({ids({0, 1})})}}, "needs edges and width");
    refused("bevel_edges", {{"blocks", ids({1})}, {"edges", nlohmann::json::array({ids({0, 1})})}, {"width", 0.75}},
            "bevel width must be between 0 and 0.5");
    refused("knife_plane", {{"blocks", ids({1})}, {"normal", xPlane}, {"offset", -4.0}}, "change nothing");

    touch(out, "sentinel");
    const auto existing = call("knife_plane", {{"blocks", ids({1})}, {"normal", xPlane}, {"offset", 0.5}});
    CHECK(existing.isError);
    CHECK(existing.text.starts_with("EXISTS: "));
    CHECK(fileBytes(out) == "sentinel");
    std::filesystem::remove(out);

    const auto cut = call("knife_plane", {{"blocks", ids({1})}, {"normal", xPlane}, {"offset", 0.5}});
    INFO(cut.text);
    REQUIRE_FALSE(cut.isError);
    CHECK(cut.text.find("RESULT: shape op knife_plane OK") != std::string::npos);
    CHECK(cut.text.find("Changed blocks: 1") != std::string::npos);
    CHECK(cut.text.find("Vertices: 3 -> 5, triangles: 1 -> 3") != std::string::npos);
    CHECK(cut.text.find("Knife pieces: 3 added, 0 zero-area dropped") != std::string::npos);
    const std::string text = fileBytes(out);
    std::vector<std::byte> bytes;
    bytes.reserve(text.size());
    for (const char c : text) bytes.push_back(static_cast<std::byte>(c));
    const auto cutNif = fo4::nif::read_nif(bytes);
    REQUIRE(cutNif.blocks().size() == 2U);
    const auto cutShape = fo4::nif::read_shape(cutNif, 1);
    CHECK(cutShape.verts.size() == 15U);
    CHECK(cutShape.tris.size() == 9U);
}

namespace {

class TextReplyTarget final : public fo4ide::rpc::Target {
public:
    explicit TextReplyTarget(std::string reply) : reply_(std::move(reply)) {}

    fo4ide::Result<nlohmann::json> invoke(std::string_view, const nlohmann::json&) override {
        return nlohmann::json(reply_);
    }

private:
    std::string reply_;
};

}

// Detects: a material or archive write the target answered Error:, ERR: or EXISTS: must reach the agent as an MCP tool error, not a success.
TEST_CASE("MCP material and archive tools flag Error:, ERR: and EXISTS: replies as tool errors") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto material = root / "test.bgsm";
    const auto pack = root / "out.ba2";
    touch(material, "material");

    for (const auto* reply : {"Error: unknown material field", "ERR:could not write the file",
                              "EXISTS: Refusing to replace an existing file"}) {
        INFO(reply);
        const fo4ide::settings::SharedSettings settings{[&] {
            fo4ide::settings::Settings allowed;
            allowed.mo2InstancePath = root;
            return allowed;
        }()};
        fo4ide::rpc::Dispatcher dispatcher;
        auto target = std::make_shared<TextReplyTarget>(reply);
        REQUIRE(dispatcher.registerTarget("material", target).has_value());
        REQUIRE(dispatcher.registerTarget("archive", target).has_value());
        fo4ide::mcp::Registry registry;
        REQUIRE(registerNativeMcpTools(registry, dispatcher, fo4ide::logging::get("mcp-service-targets"), settings,
                                       [] { return std::vector<std::filesystem::path>{}; }));

        const auto setFields = registry.call(
            "material_set_fields",
            {{"path", material.string()}, {"fields", {{"RootMaterialPath", "materials\\x.bgsm"}}}});
        CHECK(setFields.isError);
        CHECK(setFields.text == reply);

        const auto packed = registry.call(
            "archive_pack", {{"sources", nlohmann::json::array({material.string()})}, {"output", pack.string()},
                             {"rootDir", root.string()}, {"format", "General"}});
        CHECK(packed.isError);
        CHECK(packed.text == reply);
    }
}
