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

TEST_CASE("NifService authoring without niftool names the missing tool") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "sample.nif";
    touch(nif, "original nif");

    FakeRunner runner;
    fo4ide::assets::NifService service(runner, std::filesystem::path{});
    auto verify = service.verify(nif);
    auto fix = service.fix(nif, root / "fixed.nif");

    REQUIRE_FALSE(verify.has_value());
    CHECK(verify.error().type == "NifToolNotFound");
    CHECK(verify.error().message.find("niftool not installed; authoring needs it") != std::string::npos);
    REQUIRE_FALSE(fix.has_value());
    CHECK(fix.error().message.find("niftool not installed; authoring needs it") != std::string::npos);
    CHECK(runner.calls == 0);
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
