#include <algorithm>
#include "../helpers/TempRoot.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/papyrus/Diagnostic.hpp>
#include <fo4ide/papyrus/Resolver.hpp>
#include <fo4ide/papyrus/ScriptIndex.hpp>
#include <fo4ide/papyrus/TypeChecker.hpp>

namespace {

using fo4ide::papyrus::Diagnostic;
using fo4ide::papyrus::DiagnosticCode;
using fo4ide::papyrus::Resolver;
using fo4ide::papyrus::Script;
using fo4ide::papyrus::ScriptIndex;
using fo4ide::papyrus::TypeChecker;

std::string uniqueRoot() {
    const auto base = std::filesystem::temp_directory_path() / "fo4re-typecheck";
    std::filesystem::create_directories(base);
    return (base / fo4ide_test::uniqueTempName())
        .string();
}

struct Fixture {
    std::string root = uniqueRoot();
    // Owns parsed scripts; diagnostics reference them only through the
    // resolution, which must outlive the check call.
    ScriptIndex index;

    ~Fixture() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    void write(std::string_view scriptName, std::string_view source) {
        std::string relative(scriptName);
        std::replace(relative.begin(), relative.end(), ':', '/');
        const auto path = std::filesystem::path(root) / (relative + ".psc");
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path);
        out << source;
    }

    std::vector<Diagnostic> check(std::string_view scriptName) {
        index.addRoot(root);
        const Script* script = index.resolve(scriptName);
        REQUIRE(script != nullptr);
        // The C# asserts the source parses clean before checking it.
        for (const auto& d : script->diagnostics) {
            if (d.severity == fo4ide::papyrus::Severity::Error) {
                FAIL("the source under test has to parse before it can be checked: " << d.toString());
            }
        }
        const auto resolution = Resolver(index).resolve(script);
        return TypeChecker(index).check(resolution);
    }

    void writeWithHierarchy(std::string_view body, std::string_view members = "") {
        write("Form", "ScriptName Form");
        write("ObjectReference", "ScriptName ObjectReference extends Form");
        write("Actor", "ScriptName Actor extends ObjectReference");
        write("A",
              "\nScriptName A\n" + std::string(members) + "\nFunction Go()\n" +
                  std::string(body) + "\nEndFunction");
    }
};

}  // namespace

TEST_CASE("TypeChecker assigning a string to an int is rejected") {
    Fixture f;
    f.writeWithHierarchy("\n    int x = 0\n    string s = \"\"\n    x = s");

    const auto diagnostics = f.check("A");
    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics[0].code == DiagnosticCode::TypeMismatch);
}

TEST_CASE("TypeChecker assigning a float to an int is rejected because it would truncate") {
    Fixture f;
    f.writeWithHierarchy("\n    int x = 0\n    float f = 1.5\n    x = f");

    const auto diagnostics = f.check("A");
    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics[0].message.find("float") != std::string::npos);
    CHECK(diagnostics[0].message.find("int") != std::string::npos);
}

TEST_CASE("TypeChecker assigning an int to a float is fine") {
    Fixture f;
    f.writeWithHierarchy("\n    float f = 0.0\n    int x = 1\n    f = x");

    CHECK(f.check("A").empty());
}

TEST_CASE("TypeChecker assigning a child object to a parent is fine but not the reverse") {
    {
        Fixture f;
        f.writeWithHierarchy("\n    Actor a = None\n    Form f = None\n    f = a");
        CHECK(f.check("A").empty());
    }
    {
        Fixture f;
        f.writeWithHierarchy("\n    Actor a = None\n    Form f = None\n    a = f");
        const auto diagnostics = f.check("A");
        REQUIRE(diagnostics.size() == 1);
    }
}

TEST_CASE("TypeChecker none may be assigned to an object") {
    Fixture f;
    f.writeWithHierarchy("\n    Actor a = None\n    a = None");

    CHECK(f.check("A").empty());
}

TEST_CASE("TypeChecker returning the wrong type is rejected") {
    Fixture f;
    f.write("A", "\nScriptName A\nint Function Go()\n    return \"nope\"\nEndFunction");

    const auto diagnostics = f.check("A");
    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics[0].code == DiagnosticCode::TypeMismatch);
}

TEST_CASE("TypeChecker returning a value from a function that returns nothing is rejected") {
    Fixture f;
    f.write("A", "\nScriptName A\nFunction Go()\n    return 1\nEndFunction");

    const auto diagnostics = f.check("A");
    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics[0].message.find("returns nothing") != std::string::npos);
}

TEST_CASE("TypeChecker a bare return is always fine") {
    Fixture f;
    f.write("A", "\nScriptName A\nFunction Go()\n    return\nEndFunction");

    CHECK(f.check("A").empty());
}

namespace {

constexpr std::string_view kCallee =
    "\nFunction Takes(int a, string b, float c = 1.0)\nEndFunction";

}  // namespace

TEST_CASE("TypeChecker too many arguments are rejected") {
    Fixture f;
    f.writeWithHierarchy("    Takes(1, \"x\", 2.0, 3)", kCallee);

    const auto diagnostics = f.check("A");
    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics[0].code == DiagnosticCode::ArgumentCount);
}

TEST_CASE("TypeChecker too few arguments are rejected") {
    Fixture f;
    f.writeWithHierarchy("    Takes(1)", kCallee);

    const auto diagnostics = f.check("A");
    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics[0].code == DiagnosticCode::ArgumentCount);
}

TEST_CASE("TypeChecker omitting an optional argument is fine") {
    Fixture f;
    f.writeWithHierarchy("    Takes(1, \"x\")", kCallee);

    CHECK(f.check("A").empty());
}

TEST_CASE("TypeChecker a named argument may skip an earlier optional one") {
    Fixture f;
    f.writeWithHierarchy("    Takes(1, \"x\", c = 2.0)", kCallee);

    CHECK(f.check("A").empty());
}

TEST_CASE("TypeChecker a named argument that matches no parameter is rejected") {
    Fixture f;
    f.writeWithHierarchy("    Takes(1, \"x\", nope = 2.0)", kCallee);

    const auto diagnostics = f.check("A");
    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics[0].code == DiagnosticCode::UnknownArgumentName);
}

TEST_CASE("TypeChecker an argument of the wrong type is rejected") {
    Fixture f;
    f.writeWithHierarchy("\n    float f = 1.5\n    Takes(f, \"x\")", kCallee);

    const auto diagnostics = f.check("A");
    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics[0].message.find("pass to 'a'") != std::string::npos);
}

TEST_CASE("TypeChecker array builtin arguments are left unchecked") {
    Fixture f;
    f.writeWithHierarchy("\n    int[] items = new int[4]\n    items.Add(1, 2)");

    CHECK(f.check("A").empty());
}

TEST_CASE("TypeChecker a bool casts to a number even though the reference omits it") {
    for (const std::string_view target : {"int", "float"}) {
        Fixture f;
        f.writeWithHierarchy("\n    bool flag = true\n    " + std::string(target) +
                             " n = flag as " + std::string(target));
        CHECK(f.check("A").empty());
    }
}

TEST_CASE("TypeChecker casting to a struct is rejected") {
    Fixture f;
    f.write("A",
            "\nScriptName A\nStruct Point\n    int X\nEndStruct\nFunction Go()\n    int n = 1\n"
            "    Point p = n as Point\nEndFunction");

    const auto diagnostics = f.check("A");
    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics[0].code == DiagnosticCode::InvalidCast);
}

TEST_CASE("TypeChecker a downcast between related objects is allowed") {
    Fixture f;
    f.writeWithHierarchy("\n    Form f = None\n    Actor a = f as Actor");

    CHECK(f.check("A").empty());
}

TEST_CASE("TypeChecker an override with a different parameter count is rejected") {
    Fixture f;
    f.write("Base", "\nScriptName Base\nFunction Go(int a)\nEndFunction");
    f.write("Child", "\nScriptName Child extends Base\nFunction Go(int a, int b)\nEndFunction");

    const auto diagnostics = f.check("Child");
    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics[0].code == DiagnosticCode::OverrideMismatch);
}

TEST_CASE("TypeChecker a matching override is fine") {
    Fixture f;
    f.write("Base", "\nScriptName Base\nFunction Go(int a)\nEndFunction");
    f.write("Child", "\nScriptName Child extends Base\nFunction Go(int a)\nEndFunction");

    CHECK(f.check("Child").empty());
}

TEST_CASE("TypeChecker a required parameter after an optional one is rejected") {
    Fixture f;
    f.write("A", "\nScriptName A\nFunction Go(int a = 1, int b)\nEndFunction");

    const auto diagnostics = f.check("A");
    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics[0].code == DiagnosticCode::ParameterOrder);
}

TEST_CASE("TypeChecker nothing is reported when the sources are incomplete") {
    Fixture f;
    f.write("Child",
            "\nScriptName Child extends NotOnDisk\nint Function Go()\n"
            "    return \"this would be wrong if we could see the parent\"\nEndFunction");

    CHECK(f.check("Child").empty());
}

TEST_CASE("TypeChecker a call does not bind to a same named local") {
    Fixture f;
    f.write("A",
            "\nScriptName A\nint Function Value()\n    return 1\nEndFunction\n"
            "Function Go(string value)\n    int n = Value()\nEndFunction");

    CHECK(f.check("A").empty());
}

TEST_CASE("TypeChecker a var accepts anything and is accepted anywhere") {
    Fixture f;
    f.writeWithHierarchy("\n    var slot = 1\n    slot = \"text\"\n    int n = 0\n    n = slot as int");

    CHECK(f.check("A").empty());
}
