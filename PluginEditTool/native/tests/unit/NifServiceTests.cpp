#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/assets/NifService.hpp>

#include "../helpers/NifFixture.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / ("fo4ide_nif_test_" + std::to_string(id));
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

class FakeRunner final : public fo4ide::process::Runner {
public:
    fo4ide::process::Request last;
    fo4ide::process::Output output{true, false, 0, "OK", {}};
    int calls = 0;

    fo4ide::Result<fo4ide::process::Output> run(const fo4ide::process::Request& request) override {
        ++calls;
        last = request;
        if (!request.arguments.empty() && request.arguments[0] == "set" && request.arguments.size() >= 3) {
            touch(request.arguments[2], "rewritten nif");
            output.stdOut = "set OK";
        }
        return output;
    }
};

}

TEST_CASE("NifService inspect reads an FO4 NIF in-process without niftool") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "sample.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::triangleNif());

    FakeRunner runner;
    fo4ide::assets::NifService service(runner, std::filesystem::path{});
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
    CHECK(runner.calls == 0);
}

TEST_CASE("NifService maps inspect onto the existing niftool command") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto tool = root / "niftool";
    const auto nif = root / "sample.nif";
    touch(tool);
    touch(nif);

    FakeRunner runner;
    fo4ide::assets::NifService service(runner, tool);
    auto result = service.inspect(nif);

    REQUIRE(result.has_value());
    REQUIRE(runner.last.arguments.size() == 2);
    CHECK(runner.last.arguments[0] == "inspect");
    CHECK(runner.last.arguments[1] == nif.string());
    CHECK(runner.last.workingDirectory == root);
}

TEST_CASE("NifService geometry maps onto the existing niftool geo command") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto tool = root / "niftool";
    const auto nif = root / "sample.nif";
    touch(tool);
    touch(nif);

    FakeRunner runner;
    fo4ide::assets::NifService service(runner, tool);
    auto result = service.geometry(nif);

    REQUIRE(result.has_value());
    REQUIRE(runner.last.arguments.size() == 2);
    CHECK(runner.last.arguments[0] == "geo");
    CHECK(runner.last.arguments[1] == nif.string());
    CHECK(runner.calls == 1);
}

TEST_CASE("NifService preserves import option ordering") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto tool = root / "niftool";
    const auto obj = root / "mesh.obj";
    touch(tool);
    touch(obj);

    FakeRunner runner;
    fo4ide::assets::NifService service(runner, tool);
    auto result = service.importObj(obj, root / "mesh.nif", {
        .material = "Materials/test.bgsm",
        .diffuseTexture = "Textures/test_d.dds",
        .normalTexture = "Textures/test_n.dds",
        .collisionBox = true,
        .fromBlender = true,
    });

    REQUIRE(result.has_value());
    CHECK(runner.last.arguments == std::vector<std::string>{
        "import", obj.string(), (root / "mesh.nif").string(),
        "--material", "Materials/test.bgsm",
        "--tex-diffuse", "Textures/test_d.dds",
        "--tex-normal", "Textures/test_n.dds",
        "--collision", "box", "--from-blender"
    });
}

TEST_CASE("NifService applies edits through a temporary output before replacing target") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto tool = root / "niftool";
    const auto nif = root / "sample.nif";
    touch(tool);
    touch(nif, "original nif");

    FakeRunner runner;
    fo4ide::assets::NifService service(runner, tool);
    auto result = service.applyEdits(nif, nlohmann::json::array({{{"id", 1}, {"key", "name"}, {"value", "changed"}}}));

    REQUIRE(result.has_value());
    REQUIRE(runner.last.arguments.size() == 4);
    CHECK(runner.last.arguments[0] == "set");
    CHECK(runner.last.arguments[1] == nif.string());
    CHECK(runner.last.arguments[2] == nif.string() + ".niftmp");

    std::ifstream stream(nif, std::ios::binary);
    const std::string contents((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    CHECK(contents == "rewritten nif");
    CHECK_FALSE(std::filesystem::exists(nif.string() + ".niftmp"));
}

TEST_CASE("NifService applies name, translation and texture edits in-process without niftool") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "tree.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::treeNif("Materials\\Clutter\\Thing.BGSM",
                                                       {"Textures\\Thing_d.dds", "Textures\\Thing_n.dds"}));
    const auto before = fileBytes(nif);

    FakeRunner runner;
    fo4ide::assets::NifService service(runner, std::filesystem::path{});
    auto result = service.applyEdits(nif, nlohmann::json::array({
        {{"id", 1}, {"cat", "shape"}, {"key", "name"}, {"value", "Renamed"}},
        {{"id", 1}, {"cat", "shape"}, {"key", "translation"}, {"value", nlohmann::json::array({0.75, 0.0, 0.0})}},
        {{"id", 1}, {"cat", "shape"}, {"key", "tex0"}, {"value", "Textures\\Thing_new_d.dds"}},
    }));

    REQUIRE(result.has_value());
    CHECK(result->find("set OK") != std::string::npos);
    CHECK(result->find("\nSaved: " + nif.string()) != std::string::npos);
    CHECK(runner.calls == 0);
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
    CHECK(treeValue(shape, "verts") == 3);
    CHECK(treeValue(node, "name") == "Root");
    CHECK(treeValue(node, "translation") == nlohmann::json::array({0.5, 0.0, 0.0}));
}

TEST_CASE("NifService leaves the NIF byte-identical when an in-process edit key is unsupported") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "tree.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::treeNif("Materials\\Clutter\\Thing.BGSM",
                                                       {"Textures\\Thing_d.dds", "Textures\\Thing_n.dds"}));
    const auto before = fileBytes(nif);

    FakeRunner runner;
    fo4ide::assets::NifService service(runner, std::filesystem::path{});
    auto result = service.applyEdits(nif, nlohmann::json::array({
        {{"id", 1}, {"cat", "shape"}, {"key", "name"}, {"value", "Renamed"}},
        {{"id", 2}, {"cat", "shader"}, {"key", "shaderFlags"},
         {"value", nlohmann::json::array({{{"key", "Specular"}, {"label", "Specular"}, {"on", true}}})}},
    }));

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "NifEditFailed");
    CHECK(result.error().message.find("'shaderFlags'") != std::string::npos);
    CHECK(fileBytes(nif) == before);
    CHECK_FALSE(std::filesystem::exists(nif.string() + ".niftmp"));
    CHECK(runner.calls == 0);
}

TEST_CASE("NifService reports a writer-rejected read-only key as NifEditFailed and keeps the NIF") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "tree.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::treeNif("Materials\\Clutter\\Thing.BGSM",
                                                       {"Textures\\Thing_d.dds", "Textures\\Thing_n.dds"}));
    const auto before = fileBytes(nif);

    FakeRunner runner;
    fo4ide::assets::NifService service(runner, std::filesystem::path{});
    auto result = service.applyEdits(nif, nlohmann::json::array({{{"id", 1}, {"cat", "shape"}, {"key", "verts"}, {"value", 9.0}}}));

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "NifEditFailed");
    CHECK(result.error().message.find("'verts' is read-only") != std::string::npos);
    CHECK(fileBytes(nif) == before);
    CHECK_FALSE(std::filesystem::exists(nif.string() + ".niftmp"));

    auto negative = service.applyEdits(nif, nlohmann::json::array({{{"id", -1}, {"key", "name"}, {"value", "x"}}}));
    REQUIRE_FALSE(negative.has_value());
    CHECK(negative.error().message.find("unsigned block id") != std::string::npos);
    CHECK(fileBytes(nif) == before);
}

TEST_CASE("NifService refuses missing inputs before spawning a process") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto tool = root / "niftool";
    touch(tool);

    FakeRunner runner;
    fo4ide::assets::NifService service(runner, tool);
    auto result = service.verify(root / "missing.nif");

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "FileNotFound");
}

TEST_CASE("NifService geometry returns the viewer contract for an FO4 NIF with niftool absent") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "sample.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::triangleNif());

    FakeRunner runner;
    fo4ide::assets::NifService service(runner, std::filesystem::path{});
    auto result = service.geometry(nif);

    REQUIRE(result.has_value());
    const auto parsed = nlohmann::json::parse(*result);
    CHECK(parsed.at("fo4") == true);
    REQUIRE(parsed.at("shapes").size() == 1);
    const auto& shape = parsed.at("shapes")[0];
    CHECK(shape.at("name") == "Tri");
    CHECK(shape.at("verts").get<std::vector<float>>() ==
          std::vector<float>{0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f});
    CHECK(shape.at("tris").get<std::vector<int>>() == std::vector<int>{0, 1, 2});
    CHECK(shape.at("normals").empty());
    CHECK(shape.at("uvs").empty());
    CHECK(shape.at("textures").empty());
    CHECK_FALSE(shape.contains("billboard"));
    CHECK(runner.calls == 0);
}

TEST_CASE("NifService geometry reports a truncated NIF as an error") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "truncated.nif";
    auto bytes = fo4ide::test::triangleNif();
    bytes.resize(bytes.size() - 8);
    fo4ide::test::writeBytes(nif, bytes);

    FakeRunner runner;
    fo4ide::assets::NifService service(runner, std::filesystem::path{});
    auto result = service.geometry(nif);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "NifParseFailed");
    CHECK_FALSE(result.error().message.empty());
    CHECK(result.error().message.find("plugin parse error") == std::string::npos);
    CHECK(runner.calls == 0);
}

TEST_CASE("NifService fix without niftool repairs in-process and the written NIF verifies") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "tree.nif";
    const auto fixed = root / "fixed.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::treeNif("Materials\\Clutter\\Thing.BGSM ",
                                                       {" Textures\\Thing_d.dds ", "Textures\\Thing_n.dds"}));
    const auto before = fileBytes(nif);

    FakeRunner runner;
    fo4ide::assets::NifService service(runner, std::filesystem::path{});
    auto fix = service.fix(nif, fixed);

    REQUIRE(fix.has_value());
    CHECK(fix->starts_with("RESULT: fix OK (in-process)\nSaved: " + fixed.string() +
                           "\nRESULT: 8 checks, 0 failed\n"));
    CHECK(fileBytes(nif) == before);
    CHECK(std::filesystem::is_regular_file(fixed));
    CHECK_FALSE(std::filesystem::exists(fixed.string() + ".niftmp"));
    CHECK(runner.calls == 0);

    auto inspect = service.inspect(fixed);
    REQUIRE(inspect.has_value());
    const auto report = nlohmann::json::parse(*inspect);
    CHECK(report.at("hasBSX") == true);
    CHECK(report.at("shapes").at(0).at("shader") == "Materials\\Clutter\\Thing.BGSM");
    CHECK(report.at("shapes").at(0).at("textures").at(0).at("path") == "Textures\\Thing_d.dds");
}

TEST_CASE("NifService fix without niftool refuses an unreadable NIF and writes nothing") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "sample.nif";
    touch(nif, "original nif");

    FakeRunner runner;
    fo4ide::assets::NifService service(runner, std::filesystem::path{});
    auto fix = service.fix(nif, root / "fixed.nif");

    REQUIRE_FALSE(fix.has_value());
    CHECK(fix.error().type == "NifFixFailed");
    CHECK_FALSE(std::filesystem::exists(root / "fixed.nif"));
    CHECK(fileBytes(nif) == "original nif");
    CHECK(runner.calls == 0);
}

TEST_CASE("NifService fix maps onto the niftool fix command when niftool resolves") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto tool = root / "niftool";
    const auto nif = root / "sample.nif";
    const auto fixed = root / "fixed.nif";
    touch(tool);
    touch(nif, "original nif");

    FakeRunner runner;
    fo4ide::assets::NifService service(runner, tool);
    auto fix = service.fix(nif, fixed);

    REQUIRE(fix.has_value());
    CHECK(runner.calls == 1);
    REQUIRE(runner.last.arguments.size() == 3);
    CHECK(runner.last.arguments[0] == "fix");
    CHECK(runner.last.arguments[1] == nif.string());
    CHECK(runner.last.arguments[2] == fixed.string());
}

TEST_CASE("NifService reads in-process when the configured niftool is missing") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "sample.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::triangleNif());

    FakeRunner runner;
    fo4ide::assets::NifService service(runner, root / "missing" / "niftool");
    auto geometry = service.geometry(nif);
    auto inspect = service.inspect(nif);

    REQUIRE(geometry.has_value());
    REQUIRE(inspect.has_value());
    const auto geo = nlohmann::json::parse(*geometry);
    const auto info = nlohmann::json::parse(*inspect);
    REQUIRE(geo.at("shapes").size() == 1);
    REQUIRE(info.at("shapes").size() == 1);
    CHECK(runner.calls == 0);
}

TEST_CASE("NifService geometry names the BGSM material of an untextured shape") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "material.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::materialNif("Materials\\Clutter\\Thing.BGSM"));

    FakeRunner runner;
    fo4ide::assets::NifService service(runner, std::filesystem::path{});
    auto result = service.geometry(nif);

    REQUIRE(result.has_value());
    const auto parsed = nlohmann::json::parse(*result);
    REQUIRE(parsed.at("shapes").size() == 1);
    const auto& shape = parsed.at("shapes")[0];
    CHECK(shape.at("bgsmPath") == "Materials\\Clutter\\Thing.BGSM");
    CHECK(shape.at("textures").empty());
    CHECK(runner.calls == 0);
}

TEST_CASE("NifService tree maps nodes and shape slots onto the editor contract in-process") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "tree.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::treeNif("Materials\\Clutter\\Thing.BGSM",
                                                       {"Textures\\Thing_d.dds", "Textures\\Thing_n.dds"}));

    FakeRunner runner;
    fo4ide::assets::NifService service(runner, std::filesystem::path{});
    auto result = service.tree(nif);

    REQUIRE(result.has_value());
    const auto parsed = nlohmann::json::parse(*result);
    CHECK(parsed.at("fo4") == true);
    REQUIRE(parsed.at("blocks").size() == 2);
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
    CHECK(treeValue(shape, "verts") == 3);
    CHECK(treeValue(shape, "material") == "Materials\\Clutter\\Thing.BGSM");
    CHECK(treeValue(shape, "tex0") == "Textures\\Thing_d.dds");
    CHECK(treeValue(shape, "tex1") == "Textures\\Thing_n.dds");
    CHECK(treeValue(shape, "tex2") == "");
    CHECK(runner.calls == 0);
}

TEST_CASE("NifService maps tree and verify onto niftool when it resolves") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto tool = root / "niftool";
    const auto nif = root / "sample.nif";
    touch(tool);
    touch(nif);

    FakeRunner runner;
    fo4ide::assets::NifService service(runner, tool);
    auto tree = service.tree(nif);
    REQUIRE(tree.has_value());
    CHECK(runner.last.arguments == std::vector<std::string>{"tree", nif.string()});
    auto verify = service.verify(nif);
    REQUIRE(verify.has_value());
    CHECK(runner.last.arguments == std::vector<std::string>{"verify", nif.string()});
    CHECK(runner.calls == 2);
}

TEST_CASE("NifService verify reports a readable FO4 NIF in-process") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "sample.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::triangleNif());

    FakeRunner runner;
    fo4ide::assets::NifService service(runner, std::filesystem::path{});
    auto result = service.verify(nif);

    REQUIRE(result.has_value());
    CHECK(result->starts_with("RESULT: 8 checks, 0 failed\n[ok] header parses\n"));
    CHECK(result->find("[FAIL]") == std::string::npos);
    CHECK(runner.calls == 0);
}

TEST_CASE("NifService verify reports a truncated NIF as a failed header check") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "truncated.nif";
    auto bytes = fo4ide::test::triangleNif();
    bytes.resize(bytes.size() - 8);
    fo4ide::test::writeBytes(nif, bytes);

    FakeRunner runner;
    fo4ide::assets::NifService service(runner, std::filesystem::path{});
    auto result = service.verify(nif);

    REQUIRE(result.has_value());
    CHECK(result->starts_with("RESULT: 1 checks, 1 failed\n[FAIL] header parses: "));
    CHECK(runner.calls == 0);
}

TEST_CASE("NifService verify flags a drive-letter texture path") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "paths.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::treeNif("Materials\\Clutter\\Thing.BGSM",
                                                       {"C:\\Art\\Thing_d.dds", "Textures\\Thing_n.dds"}));

    FakeRunner runner;
    fo4ide::assets::NifService service(runner, std::filesystem::path{});
    auto result = service.verify(nif);

    REQUIRE(result.has_value());
    CHECK(result->starts_with("RESULT: 8 checks, 1 failed\n"));
    CHECK(result->find("[FAIL] texture paths are relative: shape 'Tri' (block 1) texture 'C:\\Art\\Thing_d.dds' "
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

    FakeRunner runner;
    fo4ide::assets::NifService service(runner, std::filesystem::path{});
    auto result = service.verify(nif);

    REQUIRE(result.has_value());
    CHECK(result->starts_with("RESULT: 8 checks, 1 failed\n"));
    CHECK(result->find("[FAIL] each block is listed under one parent only: block 1 is listed under node 2 "
                       "but its parent is 0") != std::string::npos);
}

TEST_CASE("NifService imports an OBJ in-process without niftool and the written NIF verifies") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto obj = root / "quad.obj";
    const auto nif = root / "quad.nif";
    touch(obj, "o Body\nv 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nvn 0 0 1\n"
               "f 1/1/1 2/2/1 3/3/1 4/4/1\n");

    FakeRunner runner;
    fo4ide::assets::NifService service(runner, std::filesystem::path{});
    auto result = service.importObj(obj, nif, {
        .material = "Materials/quad.bgsm",
        .diffuseTexture = "Textures/quad_d.dds",
        .normalTexture = "Textures/quad_n.dds",
    });

    REQUIRE(result.has_value());
    CHECK(result->starts_with("RESULT: import OK (in-process)\nSaved: " + nif.string() +
                              "\nRESULT: 8 checks, 0 failed\n"));
    CHECK(std::filesystem::is_regular_file(nif));
    CHECK_FALSE(std::filesystem::exists(root / "quad.nif.niftmp"));
    CHECK(runner.calls == 0);
}

TEST_CASE("NifService refuses box collision in-process and writes no NIF") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto obj = root / "quad.obj";
    touch(obj, "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");

    FakeRunner runner;
    fo4ide::assets::NifService service(runner, std::filesystem::path{});
    auto result = service.importObj(obj, root / "quad.nif", {.collisionBox = true});

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("box collision needs niftool") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(root / "quad.nif"));
    CHECK(runner.calls == 0);
}

TEST_CASE("NifService in-process import rejects an OBJ without faces and writes no NIF") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto obj = root / "points.obj";
    touch(obj, "v 0 0 0\nv 1 0 0\nv 0 1 0\n");

    FakeRunner runner;
    fo4ide::assets::NifService service(runner, std::filesystem::path{});
    auto result = service.importObj(obj, root / "points.nif");

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("OBJ has no faces to import") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(root / "points.nif"));
    CHECK(runner.calls == 0);
}
