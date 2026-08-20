#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/assets/NifService.hpp>

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

    fo4ide::Result<fo4ide::process::Output> run(const fo4ide::process::Request& request) override {
        last = request;
        if (!request.arguments.empty() && request.arguments[0] == "set" && request.arguments.size() >= 3) {
            touch(request.arguments[2], "rewritten nif");
            output.stdOut = "set OK";
        }
        return output;
    }
};

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
