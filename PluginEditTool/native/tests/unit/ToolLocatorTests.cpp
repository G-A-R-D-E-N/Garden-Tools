#include <algorithm>
#include "../helpers/TempRoot.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/tools/ToolLocator.hpp>

namespace {

std::filesystem::path tempRoot() {
    const auto id = fo4ide_test::uniqueTempName();
    return std::filesystem::temp_directory_path() / ("fo4ide_tools_test_" + id);
}

struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

void touch(const std::filesystem::path& path) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    stream << "tool";
}

}

TEST_CASE("Tool locator prefers explicit texconv setting") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto configured = root / "configured" / "Texconv.exe";
    touch(configured);

    fo4ide::settings::Settings settings;
    settings.texconvPath = configured;
    fo4ide::settings::SharedSettings shared(settings);
    fo4ide::tools::Locator locator(shared, root / "FO4IDE-native");

    CHECK(locator.resolve(fo4ide::tools::Tool::Texconv) == configured);
}

TEST_CASE("Tool locator discovers application-local tools") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto texconv = root / "tools" / "texconv" / "Texconvx64.exe";
    touch(texconv);

    fo4ide::settings::Settings settings;
    fo4ide::settings::SharedSettings shared(settings);
    fo4ide::tools::Locator locator(shared, root / "FO4IDE-native");
    CHECK(locator.resolve(fo4ide::tools::Tool::Texconv) == texconv);
}

TEST_CASE("Tool locator uses configured Fallout 4 root") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto fallout = root / "Fallout 4";
    std::filesystem::create_directories(fallout / "Data");

    fo4ide::settings::Settings settings;
    settings.fallout4Path = fallout;
    fo4ide::settings::SharedSettings shared(settings);
    fo4ide::tools::Locator locator(shared, root / "FO4IDE-native");

    CHECK(locator.fallout4Root() == fallout);
}

TEST_CASE("Tool locator collects bundled and game Papyrus imports without duplicates") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto fallout = root / "Fallout 4";
    const auto bundledBase = root / "tools" / "papyrus" / "Base";
    const auto bundledF4se = root / "tools" / "papyrus" / "F4SE";
    const auto gameBase = fallout / "Data" / "Scripts" / "Source" / "Base";
    const auto gameUser = fallout / "Data" / "Scripts" / "Source" / "User";
    std::filesystem::create_directories(bundledBase);
    std::filesystem::create_directories(bundledF4se);
    std::filesystem::create_directories(gameBase);
    std::filesystem::create_directories(gameUser);

    fo4ide::settings::Settings settings;
    settings.fallout4Path = fallout;
    settings.papyrusBaseImports = bundledBase;
    fo4ide::settings::SharedSettings shared(settings);
    fo4ide::tools::Locator locator(shared, root / "FO4IDE-native");

    const auto imports = locator.papyrusBaseImports();
    CHECK(std::find(imports.begin(), imports.end(), bundledBase) != imports.end());
    CHECK(std::find(imports.begin(), imports.end(), bundledF4se) != imports.end());
    CHECK(std::find(imports.begin(), imports.end(), gameBase) != imports.end());
    CHECK(std::find(imports.begin(), imports.end(), gameUser) != imports.end());
    CHECK(std::count(imports.begin(), imports.end(), bundledBase) == 1);
}
