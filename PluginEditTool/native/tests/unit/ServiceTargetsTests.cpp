#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
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
    fo4ide::process::Output output{true, false, 0, "native nif result", {}};

    fo4ide::Result<fo4ide::process::Output> run(const fo4ide::process::Request& request) override {
        last = request;
        return output;
    }
};

class FakePlatform final : public fo4ide::platform::Services {
public:
    std::filesystem::path fileSelection;
    std::filesystem::path folderSelection;
    std::filesystem::path saveSelection;
    std::filesystem::path openedFolder;
    std::string openedExternal;

    fo4ide::Result<std::filesystem::path> pickFile(const fo4ide::platform::FileDialogOptions&) override {
        return fileSelection;
    }

    fo4ide::Result<std::filesystem::path> pickFolder(const fo4ide::platform::FileDialogOptions&) override {
        return folderSelection;
    }

    fo4ide::Result<std::filesystem::path> pickSavePath(const fo4ide::platform::FileDialogOptions&) override {
        return saveSelection;
    }

    fo4ide::Result<void> openExternal(std::string_view value) override {
        openedExternal = value;
        return {};
    }

    fo4ide::Result<void> openFolder(const std::filesystem::path& path) override {
        openedFolder = path;
        return {};
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
    FakeRunner runner;
    FakePlatform platform;

    fo4ide::app::SettingsTarget target(store, settings, platform, runner);
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
    FakeRunner runner;
    FakePlatform platform;

    fo4ide::app::SettingsTarget target(store, settings, platform, runner);
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

TEST_CASE("SettingsTarget delegates folder selection to native platform services") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::settings::Store store(root / "settings.json");
    fo4ide::settings::Settings settings;
    FakeRunner runner;
    FakePlatform platform;
    platform.folderSelection = root / "MO2";

    fo4ide::app::SettingsTarget target(store, settings, platform, runner);
    auto result = target.invoke("BrowseFolder", nlohmann::json::array({"Choose MO2", ""}));

    REQUIRE(result.has_value());
    CHECK(*result == platform.folderSelection.string());
}

TEST_CASE("SettingsTarget routes Claude version checks through ProcessRunner") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::settings::Store store(root / "settings.json");
    fo4ide::settings::Settings settings;
    FakeRunner runner;
    runner.output.stdOut = "claude-native 1.0\n";
    FakePlatform platform;

    fo4ide::app::SettingsTarget target(store, settings, platform, runner);
    auto result = target.invoke("TestClaude", nlohmann::json::array({"claude"}));

    REQUIRE(result.has_value());
    CHECK(*result == "Found: claude-native 1.0");
    CHECK(runner.last.timeout == std::chrono::seconds(30));
}

TEST_CASE("NifTarget maps Verify to the native NIF service") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto tool = root / "niftool";
    const auto nif = root / "mesh.nif";
    touch(tool);
    touch(nif);

    FakeRunner runner;
    FakePlatform platform;
    fo4ide::assets::NifService service(runner, tool);
    fo4ide::app::NifTarget target(service, platform);

    auto result = target.invoke("Verify", nlohmann::json::array({nif.string()}));
    REQUIRE(result.has_value());
    CHECK(*result == "native nif result");
    CHECK(runner.last.arguments == std::vector<std::string>{"verify", nif.string()});
}

TEST_CASE("NifTarget stages browser dropped bytes without the CSharp host") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto tool = root / "niftool";
    touch(tool);

    FakeRunner runner;
    FakePlatform platform;
    fo4ide::assets::NifService service(runner, tool);
    fo4ide::app::NifTarget target(service, platform);

    auto result = target.invoke("StageDroppedFile", nlohmann::json::array({"mesh.nif", "aGVsbG8="}));
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    const auto path = std::filesystem::path(result->get<std::string>());
    REQUIRE(std::filesystem::is_regular_file(path));
    std::ifstream stream(path, std::ios::binary);
    const std::string contents((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    CHECK(contents == "hello");
    std::error_code ec;
    std::filesystem::remove_all(path.parent_path(), ec);
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
