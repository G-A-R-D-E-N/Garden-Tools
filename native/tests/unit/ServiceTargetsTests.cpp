#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/app/ServiceTargets.hpp>

namespace {

std::filesystem::path tempRoot() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / ("fo4ide_service_test_" + std::to_string(id));
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

    fo4ide::Result<fo4ide::process::Output> run(const fo4ide::process::Request& request) override {
        last = request;
        return fo4ide::process::Output{true, false, 0, "native nif result", {}};
    }
};

}

TEST_CASE("SettingsTarget returns the existing JSON string contract") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::settings::Store store(root / "settings.json");
    fo4ide::settings::Settings settings;
    settings.model = "test-model";
    settings.dataFolder = root / "Data";

    fo4ide::app::SettingsTarget target(store, settings);
    auto result = target.invoke("GetSettings", nlohmann::json::array());

    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    const auto json = nlohmann::json::parse(result->get<std::string>());
    CHECK(json.at("Model") == "test-model");
    CHECK(json.at("DataFolder") == (root / "Data").string());
}

TEST_CASE("SettingsTarget applies partial updates and persists them") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::settings::Store store(root / "settings.json");
    fo4ide::settings::Settings settings;
    settings.model = "before";
    settings.archive2Path = root / "Archive2.exe";

    fo4ide::app::SettingsTarget target(store, settings);
    const nlohmann::json payload{
        {"Model", "after"},
        {"DataFolder", (root / "Data").string()}
    };
    auto result = target.invoke("SaveSettings", nlohmann::json::array({payload.dump()}));

    REQUIRE(result.has_value());
    CHECK(*result == "Settings saved.");
    CHECK(settings.model == "after");
    CHECK(settings.dataFolder == root / "Data");
    CHECK(settings.archive2Path == root / "Archive2.exe");

    auto loaded = store.load();
    REQUIRE(loaded.has_value());
    CHECK(loaded->model == "after");
}

TEST_CASE("NifTarget maps Verify to the native NIF service") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto tool = root / "niftool";
    const auto nif = root / "mesh.nif";
    touch(tool);
    touch(nif);

    FakeRunner runner;
    fo4ide::assets::NifService service(runner, tool);
    fo4ide::app::NifTarget target(service);

    auto result = target.invoke("Verify", nlohmann::json::array({nif.string()}));
    REQUIRE(result.has_value());
    CHECK(*result == "native nif result");
    CHECK(runner.last.arguments == std::vector<std::string>{"verify", nif.string()});
}

TEST_CASE("NIF locator honors an explicit configured tool") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto tool = root / "custom-niftool";
    touch(tool);

    fo4ide::settings::Settings settings;
    settings.nifToolPath = tool;
    CHECK(fo4ide::app::locateNifTool(settings, root / "FO4IDE-native") == tool);
}
