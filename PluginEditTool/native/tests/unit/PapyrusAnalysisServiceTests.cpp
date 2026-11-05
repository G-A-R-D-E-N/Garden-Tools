#include <catch2/catch_test_macros.hpp>
#include "../helpers/TempRoot.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

#include <fo4ide/papyrus/AnalysisService.hpp>

using namespace fo4ide::papyrus;

namespace {

bool startsWith(const std::string& text, const std::string& prefix) {
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

struct SourceRoot {
    std::string path = [] {
        const auto dir = std::filesystem::temp_directory_path() /
                         ("fo4ide-analysis-" +
                          fo4ide_test::uniqueTempName());
        std::filesystem::create_directories(dir);
        return dir.string();
    }();

    ~SourceRoot() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }

    std::string write(const std::string& name, const std::string& content) {
        const auto file = (std::filesystem::path(path) / name).string();
        std::filesystem::create_directories(std::filesystem::path(file).parent_path());
        std::ofstream out(file);
        out << content;
        return file;
    }
};

}  // namespace

TEST_CASE("Offset of line and column") {
    CHECK(AnalysisService::offsetOf("abc\ndef", 1, 1) == 0);
    CHECK(AnalysisService::offsetOf("abc\ndef", 2, 1) == 4);
    CHECK(AnalysisService::offsetOf("abc\ndef", 2, 3) == 6);
    CHECK(AnalysisService::offsetOf("abc\r\ndef", 2, 1) == 5);
}

TEST_CASE("Out of range positions are rejected rather than clamped") {
    CHECK(AnalysisService::offsetOf("abc", 0, 1) == -1);
    CHECK(AnalysisService::offsetOf("abc", 1, 0) == -1);
    CHECK(AnalysisService::offsetOf("abc", 5, 1) == -1);
    CHECK(AnalysisService::offsetOf("abc\ndef", 1, 9) == -1);
}

TEST_CASE("Check reports a clean file") {
    SourceRoot root;
    const auto file = root.write("A.psc", "ScriptName A\nFunction F()\nEndFunction\n");

    const auto result = AnalysisService::check(file);
    CHECK(startsWith(result, "RESULT: 1 clean, 0 with syntax errors, 0 with name or type errors"));
}

TEST_CASE("Check without the semantic pass still says it only parsed") {
    SourceRoot root;
    const auto file = root.write("A.psc", "ScriptName A\nFunction F()\nEndFunction\n");

    const auto result = AnalysisService::check(file, true, false);
    CHECK(startsWith(result, "RESULT: 1 clean, 0 with syntax errors,"));
    CHECK(result.find("does not resolve names or check types") != std::string::npos);
}

TEST_CASE("Check reports a name the script does not define") {
    SourceRoot root;
    const auto file =
        root.write("A.psc", "ScriptName A\nFunction F()\n    int n = nope\nEndFunction\n");

    const auto result = AnalysisService::check(file);
    CHECK(result.find("1 with name or type errors") != std::string::npos);
    CHECK(result.find("PAP0030") != std::string::npos);
}

TEST_CASE("Check counts files whose sources were incomplete separately") {
    SourceRoot root;
    const auto file = root.write(
        "A.psc", "ScriptName A extends NotOnAnyRoot\nFunction F()\n    Inherited()\nEndFunction\n");

    const auto result = AnalysisService::check(file);
    CHECK(result.find("0 with name or type errors") != std::string::npos);
    CHECK(result.find("not on the roots") != std::string::npos);
}

TEST_CASE("Check reports the error position") {
    SourceRoot root;
    const auto file = root.write("A.psc", "ScriptName A\nFunction F(\nEndFunction\n");

    const auto result = AnalysisService::check(file);
    CHECK(startsWith(result, "RESULT: 0 clean, 1 with syntax errors"));
    CHECK(result.find("A.psc(") != std::string::npos);
}

TEST_CASE("Check walks a folder") {
    SourceRoot root;
    root.write("A.psc", "ScriptName A\n");
    root.write("nested/B.psc", "ScriptName nested:B\n");

    CHECK(startsWith(AnalysisService::check(root.path), "RESULT: 2 clean, 0 with syntax errors"));
}

TEST_CASE("Check refuses a pex with guidance") {
    SourceRoot root;
    const auto file = root.write("A.pex", "not really a pex");

    CHECK(AnalysisService::check(file).find("decompile_papyrus") != std::string::npos);
}

TEST_CASE("Check reports a missing path rather than throwing") {
    const auto missing = (std::filesystem::temp_directory_path() / "fo4re-no-such-file.psc").string();
    CHECK(AnalysisService::check(missing).find("Not found") != std::string::npos);
}

TEST_CASE("Outline lists declarations with positions") {
    SourceRoot root;
    const auto file =
        root.write("A.psc", "ScriptName A\nint Property Health auto\nFunction F()\nEndFunction\n");

    const auto result = AnalysisService::outline(file);
    CHECK(result.find("Property") != std::string::npos);
    CHECK(result.find("int Property Health Auto") != std::string::npos);
    CHECK(result.find("Function F()") != std::string::npos);
}

TEST_CASE("Outline still works on a file with errors and says so") {
    SourceRoot root;
    const auto file = root.write("A.psc", "ScriptName A\nint Property Health auto\n%%%\n");

    const auto result = AnalysisService::outline(file);
    CHECK(result.find("syntax error") != std::string::npos);
    CHECK(result.find("Health") != std::string::npos);
}

TEST_CASE("Definition resolves across files through extends") {
    SourceRoot root;
    root.write("Parent.psc",
               "ScriptName Parent\nFunction DoStuff()\n{Parent version}\nEndFunction\n");
    const auto child =
        root.write("Child.psc",
                   "ScriptName Child extends Parent\nFunction F()\n  DoStuff()\nEndFunction\n");

    const auto result = AnalysisService::definition(child, 3, 3);
    CHECK(result.find("RESULT: Function DoStuff") != std::string::npos);
    CHECK(result.find("in: Parent") != std::string::npos);
    CHECK(result.find("Parent.psc(2,10)") != std::string::npos);
    CHECK(result.find("Parent version") != std::string::npos);
}

TEST_CASE("Definition says plainly when it cannot resolve") {
    SourceRoot root;
    const auto file = root.write("A.psc",
                                 "ScriptName A\nFunction F()\n  GetThing().Unknown()\nEndFunction\n");

    const auto result = AnalysisService::definition(file, 3, 15);
    CHECK(startsWith(result, "RESULT: not resolved."));
    CHECK(result.find("no type checker") != std::string::npos);
}

TEST_CASE("Definition rejects a position outside the file") {
    SourceRoot root;
    const auto file = root.write("A.psc", "ScriptName A\n");

    CHECK(AnalysisService::definition(file, 99, 1).find("outside") != std::string::npos);
}

TEST_CASE("Analyze returns diagnostics and outline from buffer text not from disk") {
    SourceRoot root;
    const auto file = root.write("A.psc", "ScriptName A\nint Property Health auto\n");
    const auto buffer = "ScriptName A\nint Property Health auto\nFunction F(\nEndFunction\n";

    const auto json =
        nlohmann::json::parse(AnalysisService::analyzeJson(buffer, &file));
    CHECK(json["errorCount"].get<int>() > 0);
    const auto& first = json["diagnostics"][0];
    CHECK(first["line"].get<int>() == 3);
    CHECK(first["severity"].get<std::string>() == "error");
    bool hasHealth = false;
    for (const auto& s : json["symbols"]) {
        if (s["name"].get<std::string>() == "Health") hasHealth = true;
    }
    CHECK(hasHealth);
}

TEST_CASE("Analyze of a clean buffer reports no errors") {
    const auto json = nlohmann::json::parse(AnalysisService::analyzeJson("ScriptName A extends B\n"));
    CHECK(json["errorCount"].get<int>() == 0);
    CHECK(json["script"].get<std::string>() == "A");
    CHECK(json["extends"].get<std::string>() == "B");
}

TEST_CASE("Analyze survives an unsaved buffer with no path") {
    const auto json = nlohmann::json::parse(AnalysisService::analyzeJson("ScriptName A\n", nullptr));
    CHECK(json["errorCount"].get<int>() == 0);
}

TEST_CASE("Symbol at resolves within the buffer and says it is the same file") {
    SourceRoot root;
    const std::string text =
        "ScriptName A\nint Property Health auto\nFunction F()\n  Health = 1\nEndFunction\n";
    const auto file = root.write("A.psc", text);
    const auto offset = static_cast<int>(text.find("Health = 1"));

    const auto json = nlohmann::json::parse(AnalysisService::symbolAtJson(text, &file, offset));
    CHECK(json["resolved"].get<bool>());
    CHECK(json["kind"].get<std::string>() == "Property");
    CHECK(json["sameFile"].get<bool>());
    CHECK(json["line"].get<int>() == 2);
}

TEST_CASE("Symbol at reports another file as not same file so the panel knows to open it") {
    SourceRoot root;
    root.write("Parent.psc", "ScriptName Parent\nFunction DoStuff()\nEndFunction\n");
    const std::string text =
        "ScriptName Child extends Parent\nFunction F()\n  DoStuff()\nEndFunction\n";
    const auto file = root.write("Child.psc", text);
    const auto offset = static_cast<int>(text.find("DoStuff()"));

    const auto json = nlohmann::json::parse(AnalysisService::symbolAtJson(text, &file, offset));
    CHECK(json["resolved"].get<bool>());
    CHECK(!json["sameFile"].get<bool>());
    CHECK(json["file"].get<std::string>().ends_with("Parent.psc"));
}

TEST_CASE("Symbol at reports unresolved as an answer rather than an error") {
    const std::string text =
        "ScriptName A\nFunction F()\n  GetThing().Unknown()\nEndFunction\n";
    const auto offset = static_cast<int>(text.find("Unknown"));

    const auto json = nlohmann::json::parse(AnalysisService::symbolAtJson(text, nullptr, offset));
    CHECK(!json["resolved"].get<bool>());
    CHECK(!json.contains("error"));
}

TEST_CASE("Symbol at works on an unsaved buffer with no path") {
    const std::string text =
        "ScriptName A\nint Property Health auto\nFunction F()\n  Health = 1\nEndFunction\n";
    const auto offset = static_cast<int>(text.find("Health = 1"));

    const auto json = nlohmann::json::parse(AnalysisService::symbolAtJson(text, nullptr, offset));
    CHECK(json["resolved"].get<bool>());
    CHECK(json["sameFile"].get<bool>());
}
