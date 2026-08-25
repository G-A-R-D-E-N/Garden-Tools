#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/app/ServiceTargets.hpp>
#include <fo4ide/papyrus/AnalysisService.hpp>
#include <fo4ide/papyrus/Decompiler.hpp>
#include <fo4ide/papyrus/Pex.hpp>

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
    fo4ide::assets::TextureResolver textures;
    fo4ide::app::NifTarget target(service, textures, platform);

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
    fo4ide::assets::TextureResolver textures;
    fo4ide::app::NifTarget target(service, textures, platform);

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

namespace {

// Owns the Locator so the target's reference stays valid for the test body.
struct PapyrusTargetFixture {
    fo4ide::tools::Locator locator;
    fo4ide::app::PapyrusTarget target;

    PapyrusTargetFixture(fo4ide::process::Runner& runner,
                         fo4ide::platform::Services& platform,
                         fo4ide::settings::Settings& settings,
                         const std::filesystem::path& executablePath)
        : locator(settings, executablePath), target(runner, locator, platform) {}
};

// Compiles a trivial script with the built-in engine and returns the written
// .pex path, so decompile tests exercise the real PexFile I/O.
std::filesystem::path buildPex(const std::filesystem::path& sourceFile,
                               const std::filesystem::path& outDir) {
    const auto out = outDir.string();
    const auto result = fo4ide::papyrus::AnalysisService::compile(sourceFile.string(), &out);
    REQUIRE(result.find("RESULT: 1 succeeded") != std::string::npos);
    return outDir / "Greeter.pex";
}

}

TEST_CASE("PapyrusTarget Compile uses the built-in engine with the Auto note") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto script = root / "Greeter.psc";
    touch(script, "Scriptname Greeter\nFunction Ping()\nEndFunction\n");

    FakeRunner runner;
    FakePlatform platform;
    fo4ide::settings::Settings settings;
    PapyrusTargetFixture fixture(runner, platform, settings, root / "host");
    auto& target = fixture.target;

    auto result = target.invoke("Compile", nlohmann::json::array({
        script.string(), "", "", "", false, false, false, "", ""}));
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    CHECK(result->get<std::string>().find("RESULT: 1 succeeded") != std::string::npos);
    CHECK(result->get<std::string>().find("No PapyrusCompiler.exe was found") != std::string::npos);
}

TEST_CASE("PapyrusTarget Compile rejects a missing source") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    FakeRunner runner;
    FakePlatform platform;
    fo4ide::settings::Settings settings;
    PapyrusTargetFixture fixture(runner, platform, settings, root / "host");
    auto& target = fixture.target;

    auto result = target.invoke("Compile", nlohmann::json::array({
        (root / "nope.psc").string(), "", "", "", false, false, false, "", ""}));
    REQUIRE(result.has_value());
    CHECK(*result == "Source not found: " + (root / "nope.psc").string());
}

TEST_CASE("PapyrusTarget Compile with engine=creationkit and no exe explains itself") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto script = root / "Greeter.psc";
    touch(script, "Scriptname Greeter\nFunction Ping()\nEndFunction\n");

    FakeRunner runner;
    FakePlatform platform;
    fo4ide::settings::Settings settings;
    PapyrusTargetFixture fixture(runner, platform, settings, root / "host");
    auto& target = fixture.target;

    auto result = target.invoke("Compile", nlohmann::json::array({
        script.string(), "", "", "", false, false, false, "", "creationkit"}));
    REQUIRE(result.has_value());
    const auto text = result->get<std::string>();
    CHECK(text.find("PapyrusCompiler.exe not found") != std::string::npos);
    CHECK(text.find("engine='builtin'") != std::string::npos);
}

TEST_CASE("PapyrusTarget Compile runs the Creation Kit process path when exe exists") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto exe = root / "PapyrusCompiler.exe";
    touch(exe);
    const auto script = root / "Greeter.psc";
    touch(script, "Scriptname Greeter\nFunction Ping()\nEndFunction\n");

    FakeRunner runner;
    runner.output.stdOut =
        "Batch compile: 2 succeeded, 1 failed\nCompilation succeeded.\n";
    FakePlatform platform;
    fo4ide::settings::Settings settings;
    PapyrusTargetFixture fixture(runner, platform, settings, root / "host");
    auto& target = fixture.target;

    auto result = target.invoke("Compile", nlohmann::json::array({
        script.string(), "", "Data\\Scripts", "", false, true, false, exe.string(), "creationkit"}));
    REQUIRE(result.has_value());
    const auto text = result->get<std::string>();
    CHECK(text.find("RESULT: 2 succeeded, 1 failed") != std::string::npos);
    CHECK(text.find("engine") == std::string::npos);
    CHECK(runner.last.executable == exe);
    CHECK(runner.last.timeout == std::chrono::seconds(300));
    CHECK(runner.last.workingDirectory == root);
    REQUIRE(runner.last.arguments.size() == 5);
    CHECK(runner.last.arguments[0] == "Greeter.psc");
    CHECK(runner.last.arguments[1] == "-f=\"Institute_Papyrus_Flags.flg\"");
    CHECK(runner.last.arguments[2].find("-i=\"Data\\Scripts") != std::string::npos);
    CHECK(runner.last.arguments[3] == "-o=\"" + root.string() + "\"");
    CHECK(runner.last.arguments[4] == "-op");
}

TEST_CASE("PapyrusTarget Decompile shows the NOT SAVED trailer without write") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto script = root / "Greeter.psc";
    touch(script, "Scriptname Greeter\nFunction Ping()\nEndFunction\n");
    const auto outDir = root / "out";
    const auto pex = buildPex(script, outDir);

    FakeRunner runner;
    FakePlatform platform;
    fo4ide::settings::Settings settings;
    PapyrusTargetFixture fixture(runner, platform, settings, root / "host");
    auto& target = fixture.target;

    auto result = target.invoke("Decompile", nlohmann::json::array({pex.string(), "", false, false}));
    REQUIRE(result.has_value());
    const auto text = result->get<std::string>();
    CHECK(text.find("ScriptName Greeter") != std::string::npos);
    CHECK(text.find("NOT SAVED") != std::string::npos);
}

TEST_CASE("PapyrusTarget Decompile writes a namespaced .psc when asked") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto script = root / "Greeter.psc";
    touch(script, "Scriptname Greeter\nFunction Ping()\nEndFunction\n");
    const auto outDir = root / "out";
    const auto pex = buildPex(script, outDir);

    FakeRunner runner;
    FakePlatform platform;
    fo4ide::settings::Settings settings;
    PapyrusTargetFixture fixture(runner, platform, settings, root / "host");
    auto& target = fixture.target;

    auto result = target.invoke("Decompile", nlohmann::json::array({pex.string(), "", false, true}));
    REQUIRE(result.has_value());
    const auto text = result->get<std::string>();
    CHECK(text.find("SAVED ->") != std::string::npos);
    CHECK(text.find("Greeter.psc") != std::string::npos);
    REQUIRE(std::filesystem::is_regular_file(outDir / "Greeter.psc"));
    std::ifstream stream(outDir / "Greeter.psc");
    const std::string contents((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    CHECK(contents.find("ScriptName Greeter") != std::string::npos);
}

TEST_CASE("PapyrusTarget Decompile handles a folder of pex files") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto script = root / "Greeter.psc";
    touch(script, "Scriptname Greeter\nFunction Ping()\nEndFunction\n");
    const auto outDir = root / "out";
    const auto pex = buildPex(script, outDir);
    const auto folderOut = root / "decompiled";

    FakeRunner runner;
    FakePlatform platform;
    fo4ide::settings::Settings settings;
    PapyrusTargetFixture fixture(runner, platform, settings, root / "host");
    auto& target = fixture.target;

    auto result = target.invoke("Decompile",
                                nlohmann::json::array({outDir.string(), folderOut.string(), false, true}));
    REQUIRE(result.has_value());
    const auto text = result->get<std::string>();
    CHECK(text.find("RESULT: 1/1 decompiled -> .psc") != std::string::npos);
    CHECK(text.find("OUTPUT: " + folderOut.string()) != std::string::npos);
    REQUIRE(std::filesystem::is_regular_file(folderOut / "Greeter.psc"));
}

TEST_CASE("PapyrusTarget Analyze returns the analyze JSON contract") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    FakeRunner runner;
    FakePlatform platform;
    fo4ide::settings::Settings settings;
    PapyrusTargetFixture fixture(runner, platform, settings, root / "host");
    auto& target = fixture.target;

    auto result = target.invoke("Analyze", nlohmann::json::array({
        "Scriptname Greeter\nFunction Ping()\nEndFunction\n", ""}));
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    const auto json = nlohmann::json::parse(result->get<std::string>());
    CHECK(json.at("script") == "Greeter");
}

TEST_CASE("PapyrusTarget Check reports clean and broken scripts") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto good = root / "Good.psc";
    touch(good, "Scriptname Good\nFunction Ping()\nEndFunction\n");
    const auto bad = root / "Bad.psc";
    touch(bad, "Scriptname Bad\nFunction Broken(\nEndFunction\n");

    FakeRunner runner;
    FakePlatform platform;
    fo4ide::settings::Settings settings;
    PapyrusTargetFixture fixture(runner, platform, settings, root / "host");
    auto& target = fixture.target;

    auto clean = target.invoke("Check", nlohmann::json::array({good.string(), true, ""}));
    REQUIRE(clean.has_value());
    CHECK(clean->get<std::string>().find("RESULT: 1 clean") != std::string::npos);

    auto broken = target.invoke("Check", nlohmann::json::array({bad.string(), true, ""}));
    REQUIRE(broken.has_value());
    const auto brokenText = broken->get<std::string>();
    CHECK(brokenText.find("syntax errors") != std::string::npos);
    CHECK(brokenText.find("Bad.psc") != std::string::npos);

    auto syntaxOnly = target.invoke("Check", nlohmann::json::array({bad.string(), false, ""}));
    REQUIRE(syntaxOnly.has_value());
    CHECK(syntaxOnly->get<std::string>().find("Syntax only") != std::string::npos);
}

TEST_CASE("PapyrusTarget SymbolAt returns the symbol JSON contract") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    FakeRunner runner;
    FakePlatform platform;
    fo4ide::settings::Settings settings;
    PapyrusTargetFixture fixture(runner, platform, settings, root / "host");
    auto& target = fixture.target;

    const std::string text = "Scriptname Greeter\nFunction Ping()\nEndFunction\n";
    const auto offset = static_cast<int>(text.find("Ping"));
    auto result = target.invoke("SymbolAt",
                                nlohmann::json::array({text, "", offset, ""}));
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    const auto json = nlohmann::json::parse(result->get<std::string>());
    CHECK(json.contains("resolved"));
    CHECK(json.at("name") == "Ping");
}

TEST_CASE("PapyrusTarget ReadScript and WriteScript round-trip with guards") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    FakeRunner runner;
    FakePlatform platform;
    fo4ide::settings::Settings settings;
    PapyrusTargetFixture fixture(runner, platform, settings, root / "host");
    auto& target = fixture.target;

    auto missing = target.invoke("ReadScript", nlohmann::json::array({(root / "nope.psc").string()}));
    REQUIRE(missing.has_value());
    CHECK(missing->get<std::string>().find("ERR:Not found:") == 0);

    auto bad = target.invoke("WriteScript", nlohmann::json::array({(root / "x.txt").string(), "x"}));
    REQUIRE(bad.has_value());
    CHECK(bad->get<std::string>().find("ERR:Refusing to write") == 0);

    const auto written = root / "sub" / "Greeter.psc";
    auto ok = target.invoke("WriteScript", nlohmann::json::array({written.string(), "Scriptname Greeter\n"}));
    REQUIRE(ok.has_value());
    CHECK(ok->get<std::string>().empty());
    REQUIRE(std::filesystem::is_regular_file(written));

    auto read = target.invoke("ReadScript", nlohmann::json::array({written.string()}));
    REQUIRE(read.has_value());
    CHECK(read->get<std::string>() == "Scriptname Greeter\n");
}

TEST_CASE("PapyrusTarget OpenFolder routes through platform services") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    std::filesystem::create_directories(root / "scripts");
    FakeRunner runner;
    FakePlatform platform;
    fo4ide::settings::Settings settings;
    PapyrusTargetFixture fixture(runner, platform, settings, root / "host");
    auto& target = fixture.target;

    auto result = target.invoke("OpenFolder", nlohmann::json::array({(root / "scripts").string()}));
    REQUIRE(result.has_value());
    CHECK(result->get<std::string>().empty());
    CHECK(platform.openedFolder == root / "scripts");

    auto missing = target.invoke("OpenFolder", nlohmann::json::array({(root / "void").string()}));
    REQUIRE(missing.has_value());
    CHECK(missing->get<std::string>() == "Folder not found: " + (root / "void").string());
}

TEST_CASE("PapyrusTarget wiki lookups report no mirror until configured") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    FakeRunner runner;
    FakePlatform platform;
    fo4ide::settings::Settings settings;
    PapyrusTargetFixture fixture(runner, platform, settings, root / "host");
    auto& target = fixture.target;

    auto result = target.invoke("LookupFunction", nlohmann::json::array({"Form", "GetBaseObject"}));
    REQUIRE(result.has_value());
    CHECK(result->get<std::string>().find("No CK wiki mirror configured") != std::string::npos);
}

TEST_CASE("PapyrusTarget wiki lookups resolve the configured mirror") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto wiki = root / "ckwiki" / "fallout4";
    touch(wiki / "GetBaseObject_-_Form.html",
          "<html><h1>GetBaseObject - Form</h1>"
          "<h2><span id=\"Syntax\">Syntax</span></h2><p><b>Form</b> Function GetBaseObject()</p>"
          "<div id=\"catlinks\"></div></html>");

    FakeRunner runner;
    FakePlatform platform;
    fo4ide::settings::Settings settings;
    settings.ckWikiPath = wiki;
    PapyrusTargetFixture fixture(runner, platform, settings, root / "host");
    auto& target = fixture.target;

    auto result = target.invoke("LookupFunction",
                                nlohmann::json::array({"Form", "GetBaseObject"}));
    REQUIRE(result.has_value());
    const auto text = result->get<std::string>();
    CHECK(text.find("GetBaseObject - Form") != std::string::npos);
    CHECK(text.find("Syntax: Form Function GetBaseObject()") != std::string::npos);
}

TEST_CASE("PapyrusTarget StageDroppedFile mirrors the NIF staging contract") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    FakeRunner runner;
    FakePlatform platform;
    fo4ide::settings::Settings settings;
    PapyrusTargetFixture fixture(runner, platform, settings, root / "host");
    auto& target = fixture.target;

    auto result = target.invoke("StageDroppedFile", nlohmann::json::array({"script.psc", "U2NyaXB0bmFtZSBYLg=="}));
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    const auto path = std::filesystem::path(result->get<std::string>());
    REQUIRE(std::filesystem::is_regular_file(path));
    std::ifstream stream(path, std::ios::binary);
    const std::string contents((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    CHECK(contents == "Scriptname X.");
    std::error_code ec;
    std::filesystem::remove_all(path.parent_path(), ec);
}
