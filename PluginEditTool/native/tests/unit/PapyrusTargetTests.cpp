#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <tuple>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <fo4ide/app/ServiceTargets.hpp>
#include <fo4ide/papyrus/AnalysisService.hpp>
#include <fo4/papyrus/analysis_service.hpp>

#include "../helpers/GraphTestSupport.hpp"
#include "../helpers/ServiceTargetFakes.hpp"
#include "../../src/app/PapyrusStaging.hpp"

using namespace fo4ide::app::test;

namespace {

struct PapyrusTargetFixture {
    fo4ide::settings::SharedSettings shared;
    fo4ide::tools::Locator locator;
    fo4ide::app::PapyrusTarget target;

    PapyrusTargetFixture(fo4ide::process::Runner& runner,
                         fo4ide::platform::Services& platform,
                         const fo4ide::settings::Settings& settings,
                         const std::filesystem::path& executablePath)
        : shared(settings), locator(shared, executablePath), target(runner, locator, platform) {}
};

std::filesystem::path buildPex(const std::filesystem::path& sourceFile,
                               const std::filesystem::path& outDir) {
    const auto out = outDir.string();
    const auto result = fo4ide::papyrus::AnalysisService::compile(sourceFile.string(), &out);
    REQUIRE(result.find("RESULT: 1 succeeded") != std::string::npos);
    return outDir / "Greeter.pex";
}

std::string readBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
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
    settings.fallout4Path = root;
    PapyrusTargetFixture fixture(runner, platform, settings, root / "host");
    if (!fixture.locator.resolve(fo4ide::tools::Tool::PapyrusCompiler).empty()) {
        SKIP("PAPYRUS_COMPILER_PATH or PATH provides PapyrusCompiler.exe");
    }
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
    settings.fallout4Path = root;
    PapyrusTargetFixture fixture(runner, platform, settings, root / "host");
    if (!fixture.locator.resolve(fo4ide::tools::Tool::PapyrusCompiler).empty()) {
        SKIP("PAPYRUS_COMPILER_PATH or PATH provides PapyrusCompiler.exe");
    }
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

TEST_CASE("PapyrusTarget Compile refuses a folder whose ScriptName escapes and runs no compiler") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto exe = root / "PapyrusCompiler.exe";
    touch(exe);
    const auto folder = root / "scripts";
    touch(folder / "Evil.psc", "ScriptName A:..:B\nFunction Ping()\nEndFunction\n");

    FakeRunner runner;
    FakePlatform platform;
    fo4ide::settings::Settings settings;
    PapyrusTargetFixture fixture(runner, platform, settings, root / "host");
    auto& target = fixture.target;

    auto result = target.invoke("Compile", nlohmann::json::array({
        folder.string(), "", "", "", false, false, false, exe.string(), "creationkit"}));
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    CHECK(result->get<std::string>().find("Refused to compile") != std::string::npos);
    CHECK(runner.last.executable.empty());
}

TEST_CASE("PapyrusTarget Compile refuses a ScriptName part with a character that is invalid in file names and runs no compiler") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto exe = root / "PapyrusCompiler.exe";
    touch(exe);
    const auto folder = root / "scripts";
    touch(folder / "Star.psc", "ScriptName Bad*:Foo\nFunction Ping()\nEndFunction\n");

    FakeRunner runner;
    FakePlatform platform;
    fo4ide::settings::Settings settings;
    PapyrusTargetFixture fixture(runner, platform, settings, root / "host");
    auto& target = fixture.target;

    auto result = target.invoke("Compile", nlohmann::json::array({
        folder.string(), "", "", "", false, false, false, exe.string(), "creationkit"}));
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    CHECK(result->get<std::string>().find("Refused to compile") != std::string::npos);
    CHECK(runner.last.executable.empty());
}

TEST_CASE("PapyrusStaging reports an unstageable namespace part as an error, not as no staging") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto source = root / "src" / "Good.psc";
    touch(source, "ScriptName Good:Foo\nFunction Ping()\nEndFunction\n");
    const auto staging = root / "staging";
    touch(staging / "Good");

    const auto result = fo4ide::app::stageNamespacedScripts({source}, staging);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("Refused to compile") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(staging));
}

TEST_CASE("PapyrusStaging refuses an unsafe ScriptName and writes nothing outside staging") {
    const std::vector<std::tuple<std::string, std::string, std::string>> rows = {
        {"ScriptName A:B?:C", "", "unsafe namespace part"},
        {"ScriptName ..:..:Evil", "../Evil.psc", "Refused to compile"},
        {"ScriptName A:..:B", "", "Refused to compile"},
        {"ScriptName MyMod:/fo4ide_gt086_escape:Evil", "/fo4ide_gt086_escape/Evil.psc", "Refused to compile"},
    };
    const auto& [scriptLine, escapeText, fragment] =
        rows[GENERATE(std::size_t{0}, std::size_t{1}, std::size_t{2}, std::size_t{3})];
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto source = root / "src" / "Evil.psc";
    touch(source, scriptLine + "\nFunction Ping()\nEndFunction\n");
    const auto staging = root / "staging";
    const bool hasEscape = !escapeText.empty();
    const auto escaped = hasEscape ? root / escapeText : std::filesystem::path{};

    const auto result = fo4ide::app::stageNamespacedScripts({source}, staging);
    const bool escapedWritten = hasEscape && std::filesystem::exists(escaped);
    std::error_code ec;
    if (hasEscape) std::filesystem::remove(escaped, ec);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "UnsafeScriptName");
    CHECK(result.error().message.find(fragment) != std::string::npos);
    if (hasEscape) CHECK_FALSE(escapedWritten);
    CHECK_FALSE(std::filesystem::exists(staging));
}

TEST_CASE("PapyrusStaging stages a namespaced script under its namespace folders") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto source = root / "src" / "Foo.psc";
    touch(source, "ScriptName MyMod:Quests:Foo\nFunction Ping()\nEndFunction\n");
    const auto staging = root / "staging";

    const auto result = fo4ide::app::stageNamespacedScripts({source}, staging);
    REQUIRE(result.has_value());
    REQUIRE(result->has_value());
    CHECK(result->value() == staging);
    CHECK(std::filesystem::is_regular_file(staging / "MyMod" / "Quests" / "Foo.psc"));
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

TEST_CASE("PapyrusTarget Check resolves Extends from Settings base imports without the env var") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto script = root / "Greeter.psc";
    touch(script, "Scriptname Greeter extends ObjectReference\n");

    FakeRunner runner;
    FakePlatform platform;
    fo4ide::settings::Settings settings;
    settings.fallout4Path = root / "Fallout 4";
    settings.papyrusBaseImports = fo4::graph::test::baseStubs();
    PapyrusTargetFixture fixture(runner, platform, settings, root / "host");
    auto& target = fixture.target;

    auto result = target.invoke("Check", nlohmann::json::array({script.string(), true, ""}));
    REQUIRE(result.has_value());
    CHECK(result->get<std::string>().find("not on the roots") == std::string::npos);
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

TEST_CASE("PapyrusTarget Complete, SignatureHelp and CheckText answer an unsaved buffer from base roots") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto base = root / "base";
    touch(base / "Parent.psc",
          "ScriptName Parent\nInt Property Health Auto\nFunction DoStuff()\nEndFunction\n"
          "Function Pick(Int first, Int second, Int third)\nEndFunction\n");
    FakeRunner runner;
    FakePlatform platform;
    fo4ide::settings::Settings settings;
    PapyrusTargetFixture fixture(runner, platform, settings, root / "host");
    auto& target = fixture.target;
    const auto roots = fixture.locator.papyrusBaseImports();
    const auto imports = base.string();
    const auto hasLabel = [](const nlohmann::json& reply, const std::string& label) {
        for (const auto& item : reply.at("items")) {
            if (item.at("label").get<std::string>() == label) return true;
        }
        return false;
    };
    const auto hasCode = [](const nlohmann::json& reply, const std::string& code) {
        for (const auto& diagnostic : reply.at("diagnostics")) {
            if (diagnostic.at("code").get<std::string>() == code) return true;
        }
        return false;
    };

    const std::string completeText =
        "ScriptName Child extends Parent\nParent Property Link Auto\nFunction F()\n  Link.\nEndFunction\n";
    const auto completeOffset = static_cast<int>(completeText.find("Link.") + 5);
    auto complete = target.invoke("Complete",
                                  nlohmann::json::array({completeText, "", completeOffset, imports}));
    REQUIRE(complete.has_value());
    REQUIRE(complete->is_string());
    CHECK(complete->get<std::string>() ==
          fo4::papyrus::AnalysisService::completionJson(completeText, nullptr, completeOffset,
                                                        &imports, roots));
    const auto completed = nlohmann::json::parse(complete->get<std::string>());
    CHECK(hasLabel(completed, "DoStuff"));
    CHECK(hasLabel(completed, "Health"));

    const std::string signatureText =
        "ScriptName Child extends Parent\nFunction F()\n  Pick(1, 2, \nEndFunction\n";
    const auto signatureOffset = static_cast<int>(signatureText.find("Pick(1, 2, ") + 11);
    auto signature = target.invoke("SignatureHelp",
                                   nlohmann::json::array({signatureText, "", signatureOffset, imports}));
    REQUIRE(signature.has_value());
    REQUIRE(signature->is_string());
    CHECK(signature->get<std::string>() ==
          fo4::papyrus::AnalysisService::signatureHelpJson(signatureText, nullptr, signatureOffset,
                                                           &imports, roots));
    const auto help = nlohmann::json::parse(signature->get<std::string>());
    CHECK(help.at("active").get<bool>());
    CHECK(help.at("activeParameter").get<int>() == 2);

    const std::string checkBuffer =
        "ScriptName Child extends Parent\nFunction F()\n    int n = nope\nEndFunction\n";
    auto check = target.invoke("CheckText", nlohmann::json::array({checkBuffer, "", 7, imports}));
    REQUIRE(check.has_value());
    REQUIRE(check->is_string());
    CHECK(check->get<std::string>() ==
          fo4::papyrus::AnalysisService::checkTextJson(checkBuffer, nullptr, 7, &imports, roots));
    const auto checked = nlohmann::json::parse(check->get<std::string>());
    CHECK(checked.at("revision").get<int>() == 7);
    CHECK(checked.at("complete").get<bool>());
    CHECK(hasCode(checked, "PAP0030"));
}

TEST_CASE("PapyrusTarget References, Rename and WorkspaceSymbols answer a script tree and a rename writes no file") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto scripts = root / "scripts";
    touch(scripts / "Helper.psc", "ScriptName Helper\nFunction Help()\nEndFunction\n");
    touch(scripts / "User.psc", "ScriptName User\nFunction F()\n  Helper h = None\nEndFunction\n");
    FakeRunner runner;
    FakePlatform platform;
    fo4ide::settings::Settings settings;
    PapyrusTargetFixture fixture(runner, platform, settings, root / "host");
    auto& target = fixture.target;
    const auto imports = scripts.string();
    const auto helperPath = (scripts / "Helper.psc").string();
    const std::string helperText = "ScriptName Helper\nFunction Help()\nEndFunction\n";
    const auto helperOffset = static_cast<int>(helperText.find("Helper") + 1);
    const auto userBefore = readBytes(scripts / "User.psc");
    const auto entriesBefore = std::distance(std::filesystem::directory_iterator(scripts),
                                             std::filesystem::directory_iterator{});

    auto references = target.invoke("References",
                                    nlohmann::json::array({helperText, helperPath, helperOffset, imports}));
    REQUIRE(references.has_value());
    REQUIRE(references->is_string());
    const auto found = nlohmann::json::parse(references->get<std::string>());
    CHECK(found.at("declaration").at("kind").get<std::string>() == "Script");
    bool inUser = false;
    for (const auto& hit : found.at("references")) {
        if (hit.at("file").is_string() && hit.at("file").get<std::string>().ends_with("User.psc")) inUser = true;
    }
    CHECK(inUser);

    auto renamed = target.invoke("Rename",
                                 nlohmann::json::array({helperText, helperPath, helperOffset, "Helper2", imports}));
    REQUIRE(renamed.has_value());
    REQUIRE(renamed->is_string());
    const auto renamePlan = nlohmann::json::parse(renamed->get<std::string>());
    CHECK(renamePlan.at("ok").get<bool>());
    CHECK(renamePlan.at("fileRename").at("to").get<std::string>().ends_with("Helper2.psc"));
    CHECK(std::filesystem::is_regular_file(scripts / "Helper.psc"));
    CHECK(!std::filesystem::exists(scripts / "Helper2.psc"));
    CHECK(readBytes(scripts / "User.psc") == userBefore);
    CHECK(std::distance(std::filesystem::directory_iterator(scripts), std::filesystem::directory_iterator{}) ==
          entriesBefore);

    const std::string keywordRename = "Int";
    auto refused = target.invoke("Rename",
                                 nlohmann::json::array({helperText, helperPath, helperOffset, keywordRename, imports}));
    REQUIRE(refused.has_value());
    CHECK(nlohmann::json::parse(refused->get<std::string>()).at("ok") == false);

    const std::string localText = "ScriptName Counter\nFunction F()\n  Int n = 1\n  Int m = n\nEndFunction\n";
    const auto localOffset = static_cast<int>(localText.find("Int n = 1") + 4);
    auto localRefs = target.invoke("References", nlohmann::json::array({localText, "", localOffset, ""}));
    REQUIRE(localRefs.has_value());
    REQUIRE(localRefs->is_string());
    CHECK(nlohmann::json::parse(localRefs->get<std::string>()).at("references").size() == 2);
    auto localRename = target.invoke("Rename", nlohmann::json::array({localText, "", localOffset, "total", ""}));
    REQUIRE(localRename.has_value());
    REQUIRE(localRename->is_string());
    const auto edited = nlohmann::json::parse(localRename->get<std::string>());
    CHECK(edited.at("edits").size() == 2);
    CHECK(edited.at("fileRename").is_null());

    auto symbols = target.invoke("WorkspaceSymbols", nlohmann::json::array({"help", 0, imports}));
    REQUIRE(symbols.has_value());
    REQUIRE(symbols->is_string());
    bool helpListed = false;
    for (const auto& symbol : nlohmann::json::parse(symbols->get<std::string>()).at("results")) {
        if (symbol.at("name").get<std::string>() == "Help" && symbol.at("file").is_string() &&
            symbol.at("file").get<std::string>().ends_with("Helper.psc")) {
            helpListed = true;
        }
    }
    CHECK(helpListed);
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

TEST_CASE("PapyrusTarget analysis RPCs match the PET oracle on real sources") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    FakeRunner runner;
    FakePlatform platform;
    fo4ide::settings::Settings settings;
    settings.papyrusBaseImports = fo4::graph::test::baseStubs();
    PapyrusTargetFixture fixture(runner, platform, settings, root / "host");
    auto& target = fixture.target;
    const auto imports = fixture.locator.papyrusBaseImports();

    const auto event = fo4::graph::test::graphScripts() / "FixtureEventSource.psc";
    const auto actor = fo4::graph::test::baseStubs() / "Actor.psc";
    const auto bad = root / "Bad.psc";
    touch(bad, "Scriptname Bad\nFunction Broken(\nEndFunction\n");

    SECTION("Check") {
        for (const auto& file : {event, actor, bad}) {
            const auto path = file.string();
            const auto oracle =
                fo4ide::papyrus::AnalysisService::check(path, true, true, nullptr, imports);
            auto result = target.invoke("Check", nlohmann::json::array({path, true, ""}));
            REQUIRE(result.has_value());
            CHECK(result->get<std::string>() == oracle);
        }
    }

    SECTION("Analyze") {
        for (const auto& file : {event, actor, bad}) {
            const auto path = file.string();
            const auto text = readBytes(file);
            const auto oracle = fo4ide::papyrus::AnalysisService::analyzeJson(text, &path);
            auto result = target.invoke("Analyze", nlohmann::json::array({text, path}));
            REQUIRE(result.has_value());
            CHECK(result->get<std::string>() == oracle);
        }
    }

    SECTION("SymbolAt") {
        const auto path = event.string();
        const auto text = readBytes(event);
        const auto offset = static_cast<int>(text.find("RaiseIt"));
        const auto oracle =
            fo4ide::papyrus::AnalysisService::symbolAtJson(text, &path, offset, nullptr, imports);
        auto result = target.invoke("SymbolAt", nlohmann::json::array({text, path, offset, ""}));
        REQUIRE(result.has_value());
        CHECK(result->get<std::string>() == oracle);
    }

    SECTION("Compile") {
        const auto path = event.string();
        const auto out = (root / "compiled").string();
        const auto pex = std::filesystem::path(out) / "FixtureEventSource.pex";
        const auto oracle =
            fo4ide::papyrus::AnalysisService::compile(path, &out, nullptr, false, true, nullptr, imports);
        REQUIRE(oracle.find("RESULT: 1 succeeded") != std::string::npos);
        const auto oracleBytes = readBytes(pex);
        REQUIRE(!oracleBytes.empty());
        std::filesystem::remove(pex);

        auto result = target.invoke(
            "Compile", nlohmann::json::array({path, out, "", "", false, false, false, "", "builtin"}));
        REQUIRE(result.has_value());
        CHECK(result->get<std::string>() == oracle);
        CHECK(readBytes(pex) == oracleBytes);
    }
}
