#include <chrono>
#include "../helpers/TempRoot.hpp"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/archive/assets.hpp>
#include <fo4ide/assets/NifGltfService.hpp>
#include <fo4ide/assets/NifService.hpp>

#include "../helpers/NifFixture.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = fo4ide_test::uniqueTempName();
    return std::filesystem::temp_directory_path() / ("fo4ide_nif_test_" + id);
}

struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

void touch(const std::filesystem::path& path, std::string_view contents = {}) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    stream << contents;
}

std::string fileBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

nlohmann::json treeValue(const nlohmann::json& block, const char* key) {
    for (const auto& field : block.at("fields")) {
        if (field.at("key") == key) return field.at("value");
    }
    return nullptr;
}

bool flagOn(const nlohmann::json& block, const char* key) {
    for (const auto& flag : treeValue(block, "shaderFlags")) {
        if (flag.at("key") == key) return flag.at("on").get<bool>();
    }
    return false;
}

}

TEST_CASE("NifService inspect reads an FO4 NIF in-process") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "sample.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::triangleNif());

    fo4ide::assets::NifService service;
    auto result = service.inspect(nif);

    REQUIRE(result.has_value());
    const auto parsed = nlohmann::json::parse(*result);
    CHECK(parsed.at("fo4") == true);
    CHECK(parsed.at("hasBSX") == false);
    CHECK(parsed.at("hasCollision") == false);
    REQUIRE(parsed.at("shapes").size() == 1);
    CHECK(parsed.at("shapes")[0].at("name") == "Tri");
    CHECK(parsed.at("shapes")[0].at("verts") == 3);
    CHECK(parsed.at("shapes")[0].at("tris") == 1);
}


TEST_CASE("NifService applies edits over the target in place") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "sample.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::treeNif("Materials\\Clutter\\Thing.BGSM", {}));

    fo4ide::assets::NifService service;
    auto result = service.applyEdits(nif, nlohmann::json::array({{
        {"id", 1}, {"key", "name"}, {"value", "changed"}}}));

    REQUIRE(result.has_value());
    CHECK(*result == "set OK (in-process)\nSaved: " + nif.string());
    CHECK_FALSE(std::filesystem::exists(nif.string() + ".niftmp"));
    auto tree = service.tree(nif);
    REQUIRE(tree.has_value());
    CHECK(treeValue(nlohmann::json::parse(*tree).at("blocks")[1], "name") == "changed");
}

TEST_CASE("NifService applies name, translation and texture edits in-process") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "tree.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::treeNif("Materials\\Clutter\\Thing.BGSM",
                                                       {"Textures\\Thing_d.dds", "Textures\\Thing_n.dds"}));
    const auto before = fileBytes(nif);

    fo4ide::assets::NifService service;
    auto result = service.applyEdits(nif, nlohmann::json::array({
        {{"id", 1}, {"cat", "shape"}, {"key", "name"}, {"value", "Renamed"}},
        {{"id", 1}, {"cat", "shape"}, {"key", "translation"}, {"value", nlohmann::json::array({0.75, 0.0, 0.0})}},
        {{"id", 1}, {"cat", "shape"}, {"key", "tex0"}, {"value", "Textures\\Thing_new_d.dds"}},
    }));

    REQUIRE(result.has_value());
    CHECK(result->find("set OK") != std::string::npos);
    CHECK(result->find("\nSaved: " + nif.string()) != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(nif.string() + ".niftmp"));
    CHECK(fileBytes(nif) != before);

    auto tree = service.tree(nif);
    REQUIRE(tree.has_value());
    const auto parsed = nlohmann::json::parse(*tree);
    const auto& node = parsed.at("blocks")[0];
    const auto& shape = parsed.at("blocks")[1];
    CHECK(treeValue(shape, "name") == "Renamed");
    CHECK(treeValue(shape, "translation") == nlohmann::json::array({0.75, 0.0, 0.0}));
    CHECK(treeValue(shape, "tex0") == "Textures\\Thing_new_d.dds");
    CHECK(treeValue(shape, "tex1") == "Textures\\Thing_n.dds");
    CHECK(treeValue(shape, "material") == "Materials\\Clutter\\Thing.BGSM");
    // Invariant: the tree reports its read-only counters as text fields, so the editor renders "3", not 3.
    CHECK(treeValue(shape, "verts") == "3");
    CHECK(treeValue(node, "name") == "Root");
    CHECK(treeValue(node, "translation") == nlohmann::json::array({0.5, 0.0, 0.0}));
}

TEST_CASE("NifService writes the editor's flag toggles and booleans in-process") {
    // Invariant: the editor sends shader flags as {key,label,on} objects and every other toggle as a JSON boolean.
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "tree.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::treeNif("Materials\\Clutter\\Thing.BGSM",
                                                       {"Textures\\Thing_d.dds", "Textures\\Thing_n.dds"}));
    const auto before = fileBytes(nif);

    fo4ide::assets::NifService service;
    auto result = service.applyEdits(nif, nlohmann::json::array({
        {{"id", 2}, {"cat", "shader"}, {"key", "shaderFlags"},
         {"value", nlohmann::json::array({{{"key", "Specular"}, {"label", "Specular"}, {"on", true}},
                                          {{"key", "Skinned"}, {"label", "Skinned"}, {"on", false}}})}},
        {{"id", 2}, {"cat", "shader"}, {"key", "envMapSSR"}, {"value", true}},
    }));

    REQUIRE(result.has_value());
    CHECK(result->find("set OK") != std::string::npos);
    CHECK(fileBytes(nif) != before);

    auto tree = service.tree(nif);
    REQUIRE(tree.has_value());
    const auto shader = nlohmann::json::parse(*tree).at("blocks")[2];
    CHECK(flagOn(shader, "Specular"));
    CHECK_FALSE(flagOn(shader, "Skinned"));
    CHECK(treeValue(shader, "envMapSSR") == true);
}

TEST_CASE("NifService reports a writer-rejected read-only key as NifEditFailed and keeps the NIF") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "tree.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::treeNif("Materials\\Clutter\\Thing.BGSM",
                                                       {"Textures\\Thing_d.dds", "Textures\\Thing_n.dds"}));
    const auto before = fileBytes(nif);

    fo4ide::assets::NifService service;
    auto result = service.applyEdits(nif, nlohmann::json::array({{{"id", 1}, {"cat", "shape"}, {"key", "verts"}, {"value", 9.0}}}));

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "NifEditFailed");
    CHECK(result.error().message.find("'verts' is read-only") != std::string::npos);
    CHECK(fileBytes(nif) == before);
    CHECK_FALSE(std::filesystem::exists(nif.string() + ".niftmp"));

    auto negative = service.applyEdits(nif, nlohmann::json::array({{{"id", -1}, {"key", "name"}, {"value", "x"}}}));
    REQUIRE_FALSE(negative.has_value());
    CHECK(negative.error().message.find("block id -1 is out of range") != std::string::npos);
    CHECK(fileBytes(nif) == before);
}

TEST_CASE("NifService refuses missing inputs before reading") {
    const auto root = tempRoot();
    Cleanup cleanup{root};

    fo4ide::assets::NifService service;
    auto result = service.verify(root / "missing.nif");

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "FileNotFound");
}

TEST_CASE("NifService geometry returns the viewer contract for an FO4 NIF") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "sample.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::triangleNif());

    fo4ide::assets::NifService service;
    auto result = service.geometry(nif);

    REQUIRE(result.has_value());
    const auto parsed = nlohmann::json::parse(*result);
    CHECK(parsed.at("fo4") == true);
    REQUIRE(parsed.at("shapes").size() == 1);
    const auto& shape = parsed.at("shapes")[0];
    CHECK(shape.at("name") == "Tri");
    CHECK(shape.at("block") == 0);
    CHECK(shape.at("verts").get<std::vector<float>>() ==
          std::vector<float>{0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f});
    CHECK(shape.at("tris").get<std::vector<int>>() == std::vector<int>{0, 1, 2});
    CHECK(shape.at("normals").empty());
    CHECK(shape.at("uvs").empty());
    CHECK(shape.at("textures").empty());
    CHECK_FALSE(shape.contains("billboard"));
}

TEST_CASE("NifService geometry reports a truncated NIF as an error") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "truncated.nif";
    auto bytes = fo4ide::test::triangleNif();
    bytes.resize(bytes.size() - 8);
    fo4ide::test::writeBytes(nif, bytes);

    fo4ide::assets::NifService service;
    auto result = service.geometry(nif);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "NifParseFailed");
    CHECK_FALSE(result.error().message.empty());
    CHECK(result.error().message.find("plugin parse error") == std::string::npos);
}

TEST_CASE("NifService fix repairs in-process and the written NIF verifies") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "tree.nif";
    const auto fixed = root / "fixed.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::treeNif("Materials\\Clutter\\Thing.BGSM ",
                                                       {" Textures\\Thing_d.dds ", "Textures\\Thing_n.dds"}));
    const auto before = fileBytes(nif);

    fo4ide::assets::NifService service;
    auto fix = service.fix(nif, fixed);

    REQUIRE(fix.has_value());
    CHECK(fix->starts_with("RESULT: fix OK (in-process)\nSaved: " + fixed.string() +
                           "\nRESULT: 18 checks, 0 failed\n"));
    CHECK(fileBytes(nif) == before);
    CHECK(std::filesystem::is_regular_file(fixed));
    CHECK_FALSE(std::filesystem::exists(fixed.string() + ".niftmp"));

    auto inspect = service.inspect(fixed);
    REQUIRE(inspect.has_value());
    const auto report = nlohmann::json::parse(*inspect);
    CHECK(report.at("hasBSX") == true);
    CHECK(report.at("shapes").at(0).at("shader") == "Materials\\Clutter\\Thing.BGSM");
    CHECK(report.at("shapes").at(0).at("textures").at(0).at("path") == "Textures\\Thing_d.dds");
}

TEST_CASE("NifService fix refuses an unreadable NIF and writes nothing") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "sample.nif";
    touch(nif, "original nif");

    fo4ide::assets::NifService service;
    auto fix = service.fix(nif, root / "fixed.nif");

    REQUIRE_FALSE(fix.has_value());
    CHECK(fix.error().type == "NifFixFailed");
    CHECK_FALSE(std::filesystem::exists(root / "fixed.nif"));
    CHECK(fileBytes(nif) == "original nif");
}



TEST_CASE("NifService geometry names the BGSM material of an untextured shape") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "material.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::materialNif("Materials\\Clutter\\Thing.BGSM"));

    fo4ide::assets::NifService service;
    auto result = service.geometry(nif);

    REQUIRE(result.has_value());
    const auto parsed = nlohmann::json::parse(*result);
    REQUIRE(parsed.at("shapes").size() == 1);
    const auto& shape = parsed.at("shapes")[0];
    CHECK(shape.at("bgsmPath") == "Materials\\Clutter\\Thing.BGSM");
    CHECK(shape.at("textures").empty());
}

TEST_CASE("NifService tree maps nodes and shape slots onto the editor contract in-process") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "tree.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::treeNif("Materials\\Clutter\\Thing.BGSM",
                                                       {"Textures\\Thing_d.dds", "Textures\\Thing_n.dds"}));

    fo4ide::assets::NifService service;
    auto result = service.tree(nif);

    REQUIRE(result.has_value());
    const auto parsed = nlohmann::json::parse(*result);
    CHECK(parsed.at("fo4") == true);
    REQUIRE(parsed.at("blocks").size() == 4);
    const auto& node = parsed.at("blocks")[0];
    CHECK(node.at("id") == 0);
    CHECK(node.at("group") == "Nodes");
    CHECK(treeValue(node, "name") == "Root");
    CHECK(treeValue(node, "translation") == nlohmann::json::array({0.5, 0.0, 0.0}));
    const auto& shape = parsed.at("blocks")[1];
    CHECK(shape.at("id") == 1);
    CHECK(shape.at("type") == "BSTriShape");
    CHECK(shape.at("group") == "Shapes");
    CHECK(treeValue(shape, "translation") == nlohmann::json::array({0.25, 0.0, 0.0}));
    CHECK(treeValue(shape, "verts") == "3");
    CHECK(treeValue(shape, "material") == "Materials\\Clutter\\Thing.BGSM");
    CHECK(treeValue(shape, "tex0") == "Textures\\Thing_d.dds");
    CHECK(treeValue(shape, "tex1") == "Textures\\Thing_n.dds");
    CHECK(treeValue(shape, "tex2") == "");
    const auto& shader = parsed.at("blocks")[2];
    CHECK(shader.at("type") == "BSLightingShaderProperty");
    CHECK(shader.at("cat") == "shader");
    CHECK(treeValue(shader, "materialClass") == "Envmap");
    CHECK(treeValue(shader, "envMapScale") == 1.0);
    const auto& textureSet = parsed.at("blocks")[3];
    CHECK(textureSet.at("cat") == "texture");
    CHECK(treeValue(textureSet, "tex0") == "Textures\\Thing_d.dds");
    CHECK(treeValue(textureSet, "tex1") == "Textures\\Thing_n.dds");
}

TEST_CASE("NifService verify reports a readable FO4 NIF in-process") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "sample.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::triangleNif());

    fo4ide::assets::NifService service;
    auto result = service.verify(nif);

    REQUIRE(result.has_value());
    const auto text = nlohmann::json::parse(*result).at("text").get<std::string>();
    CHECK(text.starts_with("RESULT: 18 checks, 0 failed\n[ok] header parses\n"));
    CHECK(text.find("[FAIL]") == std::string::npos);
}

// Detects: a verify reply that drops the per-check fixable flag, which would show a fix button for a check no repair clears
TEST_CASE("NifService verify returns each check with its name, failure and fixable flag") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "paths.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::treeNif("Materials\\Clutter\\Thing.BGSM",
                                                       {"C:\\Art\\Thing_d.dds", "Textures\\Thing_n.dds"}));

    fo4ide::assets::NifService service;
    auto result = service.verify(nif);

    REQUIRE(result.has_value());
    const auto checks = nlohmann::json::parse(*result).at("checks");
    REQUIRE(checks.size() == 18U);
    const auto named = [&checks](std::string_view name) {
        for (const auto& check : checks) {
            if (check.at("name").get<std::string>() == name) return check;
        }
        return nlohmann::json{};
    };
    CHECK(named("texture paths are relative").at("failure").get<std::string>().find("is a drive-letter path") !=
          std::string::npos);
    CHECK_FALSE(named("texture paths are relative").at("fixable").get<bool>());
    CHECK(named("no editor markers").at("fixable").get<bool>());
    CHECK(named("no editor markers").at("failure").get<std::string>().empty());
}

TEST_CASE("NifService verify reports a truncated NIF as a failed header check") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "truncated.nif";
    auto bytes = fo4ide::test::triangleNif();
    bytes.resize(bytes.size() - 8);
    fo4ide::test::writeBytes(nif, bytes);

    fo4ide::assets::NifService service;
    auto result = service.verify(nif);

    REQUIRE(result.has_value());
    const auto text = nlohmann::json::parse(*result).at("text").get<std::string>();
    CHECK(text.starts_with("RESULT: 1 checks, 1 failed\n[FAIL] header parses: "));
}

TEST_CASE("NifService verify flags a drive-letter texture path") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "paths.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::treeNif("Materials\\Clutter\\Thing.BGSM",
                                                       {"C:\\Art\\Thing_d.dds", "Textures\\Thing_n.dds"}));

    fo4ide::assets::NifService service;
    auto result = service.verify(nif);

    REQUIRE(result.has_value());
    const auto text = nlohmann::json::parse(*result).at("text").get<std::string>();
    CHECK(text.starts_with("RESULT: 18 checks, 1 failed\n"));
    CHECK(text.find("[FAIL] texture paths are relative: shape 'Tri' (block 1) texture 'C:\\Art\\Thing_d.dds' "
                    "is a drive-letter path") != std::string::npos);
}

TEST_CASE("NifService verify flags a child that two nodes both list") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "shared_child.nif";
    const std::vector<fo4ide::test::FixtureBlock> blocks{
        {"NiNode", fo4ide::test::nodeBlock(0U, 0.0f, {1U})},
        {"BSTriShape", fo4ide::test::triangleShapeBlock(0xFFFFFFFFU)},
        {"NiNode", fo4ide::test::nodeBlock(0U, 0.0f, {1U})},
    };
    fo4ide::test::writeBytes(nif, fo4ide::test::nifFile(blocks, {"Tri"}));

    fo4ide::assets::NifService service;
    auto result = service.verify(nif);

    REQUIRE(result.has_value());
    const auto text = nlohmann::json::parse(*result).at("text").get<std::string>();
    CHECK(text.starts_with("RESULT: 18 checks, 1 failed\n"));
    CHECK(text.find("[FAIL] each block is listed under one parent only: block 1 is listed under node 2 "
                    "but its parent is 0") != std::string::npos);
}

TEST_CASE("NifService imports an OBJ in-process and the written NIF verifies") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto obj = root / "quad.obj";
    const auto nif = root / "quad.nif";
    touch(obj, "o Body\nv 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nvn 0 0 1\n"
               "f 1/1/1 2/2/1 3/3/1 4/4/1\n");

    fo4ide::assets::NifService service;
    auto result = service.importObj(obj, nif, {
        .material = "Materials/quad.bgsm",
        .diffuseTexture = "Textures/quad_d.dds",
        .normalTexture = "Textures/quad_n.dds",
        .skeleton = {},
    });

    REQUIRE(result.has_value());
    CHECK(result->starts_with("RESULT: import OK (in-process)\nSaved: " + nif.string() +
                              "\nRESULT: 18 checks, 0 failed\n"));
    CHECK(std::filesystem::is_regular_file(nif));
    CHECK_FALSE(std::filesystem::exists(root / "quad.nif.niftmp"));
}

TEST_CASE("NifService writes box collision in-process") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto obj = root / "quad.obj";
    touch(obj, "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");

    fo4ide::assets::NifService service;
    fo4ide::assets::NifImportOptions options;
    options.collision = fo4ide::assets::NifCollisionShape::box;
    auto result = service.importObj(obj, root / "quad.nif", options);

    REQUIRE(result.has_value());
    std::ifstream written(root / "quad.nif", std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(written)), std::istreambuf_iterator<char>());
    CHECK(bytes.find("bhkPhysicsSystem") != std::string::npos);
}

TEST_CASE("NifService in-process import rejects an OBJ without faces and writes no NIF") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto obj = root / "points.obj";
    touch(obj, "v 0 0 0\nv 1 0 0\nv 0 1 0\n");

    fo4ide::assets::NifService service;
    auto result = service.importObj(obj, root / "points.nif");

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("OBJ has no faces to import") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(root / "points.nif"));
}

TEST_CASE("NifService glTF export writes a binary glTF and refuses to overwrite its NIF") {
    // Regression: the glTF export wrote nothing for a valid triangle NIF, or replaced the NIF it was reading.
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "tri.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::triangleNif());
    const auto before = fileBytes(nif);

    const auto out = root / "tri.glb";
    auto result = fo4ide::assets::exportNifGltf(nif, out);
    REQUIRE(result.has_value());
    CHECK(result->starts_with("RESULT: gltf export OK"));
    CHECK(fileBytes(out).starts_with("glTF"));

    auto refused = fo4ide::assets::exportNifGltf(nif, nif);
    CHECK_FALSE(refused.has_value());
    CHECK(fileBytes(nif) == before);
}

// Detects: a glTF import must refuse to replace an existing NIF without overwrite, and must write the NIF for a valid .glb.
TEST_CASE("NifService imports a glTF in-process and refuses to replace its NIF without overwrite") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "tri.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::triangleNif());
    const auto glb = root / "tri.glb";
    REQUIRE(fo4ide::assets::exportNifGltf(nif, glb).has_value());

    const auto out = root / "back.nif";
    touch(out, "old");
    fo4ide::assets::NifService service;
    auto refused = service.importGltf(glb, out);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "OutputExists");
    CHECK(fileBytes(out) == "old");

    auto imported = service.importGltf(glb, out, {}, true);
    REQUIRE(imported.has_value());
    CHECK(imported->starts_with("RESULT: import glTF OK (in-process)\nSaved: " + out.string()));
    CHECK(fileBytes(out).starts_with("Gamebryo File Format"));
}

// Detects: an existing .glb replaced by an export that did not ask for overwrite, from the MCP tool or the panel button.
TEST_CASE("NifService glTF export refuses an existing output unless overwrite is set") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "tri.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::triangleNif());
    const auto out = root / "tri.glb";
    touch(out, "old");

    auto refused = fo4ide::assets::exportNifGltf(nif, out);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "OutputExists");
    CHECK(fileBytes(out) == "old");

    auto replaced = fo4ide::assets::exportNifGltf(nif, out, true);
    REQUIRE(replaced.has_value());
    CHECK(fileBytes(out).starts_with("glTF"));
}

// Detects: a copy export that must refuse to replace a texture file beside the .glb without overwrite, and must write the resolved DDS when overwrite is set.
TEST_CASE("NifService glTF copy export refuses to replace an existing texture file unless overwrite is set") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "tree.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::treeNif("", {"textures\\armor\\a_d.dds"}));
    touch(root / "source" / "textures" / "armor" / "a_d.dds", "DDS source");
    const std::vector<std::filesystem::path> roots{root / "source"};
    const auto assets = fo4::archive::AssetResolver::open_roots(roots, {});
    fo4ide::assets::NifGltfOptions options;
    options.textures = fo4::nif::GltfTextureMode::copy;
    options.assets = &assets;

    const auto out = root / "out" / "tree.glb";
    const auto texture = root / "out" / "textures" / "armor" / "a_d.dds";
    touch(texture, "old");

    auto refused = fo4ide::assets::exportNifGltf(nif, out, false, options);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "OutputExists");
    CHECK(fileBytes(texture) == "old");
    CHECK_FALSE(std::filesystem::exists(out));

    auto replaced = fo4ide::assets::exportNifGltf(nif, out, true, options);
    REQUIRE(replaced.has_value());
    CHECK(fileBytes(texture) == "DDS source");
}

// Detects: an unknown textures argument that must be refused, or a blank one that must keep the png default.
TEST_CASE("glTF texture mode names map to the export modes and reject others") {
    CHECK(fo4ide::assets::parseGltfTextureMode("") == fo4::nif::GltfTextureMode::png);
    CHECK(fo4ide::assets::parseGltfTextureMode("png") == fo4::nif::GltfTextureMode::png);
    CHECK(fo4ide::assets::parseGltfTextureMode("copy") == fo4::nif::GltfTextureMode::copy);
    CHECK(fo4ide::assets::parseGltfTextureMode("png") == fo4::nif::GltfTextureMode::png);
    CHECK(fo4ide::assets::parseGltfTextureMode("embed") == fo4::nif::GltfTextureMode::embed);
    CHECK(fo4ide::assets::parseGltfTextureMode("uri") == fo4::nif::GltfTextureMode::uri);
    CHECK_FALSE(fo4ide::assets::parseGltfTextureMode("dds").has_value());
}
