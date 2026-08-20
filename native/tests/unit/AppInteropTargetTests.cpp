#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/app/AppInteropTarget.hpp>

namespace {

std::filesystem::path tempRoot() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / ("fo4ide_appinterop_test_" + std::to_string(id));
}

struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

void writeFile(const std::filesystem::path& path, std::string_view contents = {}) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    stream << contents;
}

class FakePlatform final : public fo4ide::platform::Services {
public:
    std::filesystem::path folder;

    fo4ide::Result<std::filesystem::path> pickFile(const fo4ide::platform::FileDialogOptions&) override {
        return std::filesystem::path{};
    }

    fo4ide::Result<std::filesystem::path> pickFolder(const fo4ide::platform::FileDialogOptions&) override {
        return folder;
    }

    fo4ide::Result<std::filesystem::path> pickSavePath(const fo4ide::platform::FileDialogOptions&) override {
        return std::filesystem::path{};
    }

    fo4ide::Result<void> openExternal(std::string_view) override {
        return {};
    }

    fo4ide::Result<void> openFolder(const std::filesystem::path&) override {
        return {};
    }
};

void makeProfile(const std::filesystem::path& root) {
    const auto game = root / "Game";
    writeFile(root / "ModOrganizer.ini",
        "selected_profile=Test\n"
        "gamePath=" + game.string() + "\n");
    writeFile(root / "profiles" / "Test" / "plugins.txt", "*Example.esp\n");
    writeFile(game / "Data" / "Fallout4.esm");
    writeFile(game / "Data" / "Example.esp");
}

}

TEST_CASE("AppInterop validates selected MO2 instance folders") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    std::filesystem::create_directories(root / "profiles");

    fo4ide::settings::Store store(root / "settings.json");
    fo4ide::settings::Settings settings;
    FakePlatform platform;
    platform.folder = root;
    fo4ide::app::AppInteropTarget target(store, settings, platform);

    auto result = target.invoke("BrowseForMo2Folder", nlohmann::json::array());
    REQUIRE(result.has_value());
    CHECK(*result == root.string());
}

TEST_CASE("AppInterop opens MO2 metadata and returns top-level plugin rows") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    makeProfile(root);

    fo4ide::settings::Store store(root / "settings.json");
    fo4ide::settings::Settings settings;
    FakePlatform platform;
    fo4ide::app::AppInteropTarget target(store, settings, platform);

    auto opened = target.invoke("OpenMo2Profile", nlohmann::json::array({root.string()}));
    REQUIRE(opened.has_value());
    CHECK(settings.mo2InstancePath == std::filesystem::absolute(root));
    CHECK(settings.dataFolder == root / "Game" / "Data");

    auto plugins = target.invoke("GetPlugins", nlohmann::json::array());
    REQUIRE(plugins.has_value());
    const auto rows = nlohmann::json::parse(plugins->get<std::string>());
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].at("Key") == "Fallout4.esm");
    CHECK(rows[1].at("Key") == "Example.esp");
    CHECK(rows[1].at("FilePath") == (root / "Game" / "Data" / "Example.esp").string());
}

TEST_CASE("AppInterop keeps profile metadata available for the future plugin backend") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    makeProfile(root);

    fo4ide::settings::Store store(root / "settings.json");
    fo4ide::settings::Settings settings;
    FakePlatform platform;
    fo4ide::app::AppInteropTarget target(store, settings, platform);

    REQUIRE(target.invoke("OpenMo2Profile", nlohmann::json::array({root.string()})).has_value());
    const auto snapshot = target.snapshot();
    REQUIRE(snapshot.has_value());
    CHECK(snapshot->plugins.size() == 2);
    CHECK(snapshot->missingPlugins.empty());
}
