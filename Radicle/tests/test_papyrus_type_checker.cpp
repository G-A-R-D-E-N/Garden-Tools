#include <algorithm>
#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <fo4/papyrus/diagnostic.hpp>
#include <fo4/papyrus/resolver.hpp>
#include <fo4/papyrus/script_index.hpp>
#include <fo4/papyrus/type_checker.hpp>

namespace {

using fo4::papyrus::Diagnostic;
using fo4::papyrus::DiagnosticCode;
using fo4::papyrus::Resolver;
using fo4::papyrus::Script;
using fo4::papyrus::ScriptIndex;
using fo4::papyrus::TypeChecker;

std::string uniqueRoot() {
    const auto base = std::filesystem::temp_directory_path() / "fo4re-typecheck";
    std::filesystem::create_directories(base);
    return (base / std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))
        .string();
}

struct Fixture {
    std::string root = uniqueRoot();
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
        if (script == nullptr) throw std::runtime_error("script not indexed: " + std::string(scriptName));
        for (const auto& d : script->diagnostics) {
            if (d.severity == fo4::papyrus::Severity::Error) {
                throw std::runtime_error("the source under test has to parse before it can be checked: " + d.toString());
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

}

TEST(PapyrusTypeChecker, AssigningAStringToAnIntIsRejected) {
    Fixture f;
    f.writeWithHierarchy("\n    int x = 0\n    string s = \"\"\n    x = s");

    const auto diagnostics = f.check("A");
    ASSERT_TRUE(diagnostics.size() == 1);
    EXPECT_TRUE(diagnostics[0].code == DiagnosticCode::TypeMismatch);
}

TEST(PapyrusTypeChecker, AssigningAFloatToAnIntIsRejectedBecauseItWouldTruncate) {
    Fixture f;
    f.writeWithHierarchy("\n    int x = 0\n    float f = 1.5\n    x = f");

    const auto diagnostics = f.check("A");
    ASSERT_TRUE(diagnostics.size() == 1);
    EXPECT_TRUE(diagnostics[0].message.find("float") != std::string::npos);
    EXPECT_TRUE(diagnostics[0].message.find("int") != std::string::npos);
}

TEST(PapyrusTypeChecker, AssigningAnIntToAFloatIsFine) {
    Fixture f;
    f.writeWithHierarchy("\n    float f = 0.0\n    int x = 1\n    f = x");

    EXPECT_TRUE(f.check("A").empty());
}

TEST(PapyrusTypeChecker, AssigningAChildObjectToAParentIsFineButNotTheReverse) {
    {
        Fixture f;
        f.writeWithHierarchy("\n    Actor a = None\n    Form f = None\n    f = a");
        EXPECT_TRUE(f.check("A").empty());
    }
    {
        Fixture f;
        f.writeWithHierarchy("\n    Actor a = None\n    Form f = None\n    a = f");
        const auto diagnostics = f.check("A");
        ASSERT_TRUE(diagnostics.size() == 1);
    }
}

TEST(PapyrusTypeChecker, NoneMayBeAssignedToAnObject) {
    Fixture f;
    f.writeWithHierarchy("\n    Actor a = None\n    a = None");

    EXPECT_TRUE(f.check("A").empty());
}

TEST(PapyrusTypeChecker, ReturningTheWrongTypeIsRejected) {
    Fixture f;
    f.write("A", "\nScriptName A\nint Function Go()\n    return \"nope\"\nEndFunction");

    const auto diagnostics = f.check("A");
    ASSERT_TRUE(diagnostics.size() == 1);
    EXPECT_TRUE(diagnostics[0].code == DiagnosticCode::TypeMismatch);
}

TEST(PapyrusTypeChecker, ReturningAValueFromAFunctionThatReturnsNothingIsRejected) {
    Fixture f;
    f.write("A", "\nScriptName A\nFunction Go()\n    return 1\nEndFunction");

    const auto diagnostics = f.check("A");
    ASSERT_TRUE(diagnostics.size() == 1);
    EXPECT_TRUE(diagnostics[0].message.find("returns nothing") != std::string::npos);
}

TEST(PapyrusTypeChecker, ABareReturnIsAlwaysFine) {
    Fixture f;
    f.write("A", "\nScriptName A\nFunction Go()\n    return\nEndFunction");

    EXPECT_TRUE(f.check("A").empty());
}

namespace {

constexpr std::string_view kCallee =
    "\nFunction Takes(int a, string b, float c = 1.0)\nEndFunction";

}

TEST(PapyrusTypeChecker, TooManyArgumentsAreRejected) {
    Fixture f;
    f.writeWithHierarchy("    Takes(1, \"x\", 2.0, 3)", kCallee);

    const auto diagnostics = f.check("A");
    ASSERT_TRUE(diagnostics.size() == 1);
    EXPECT_TRUE(diagnostics[0].code == DiagnosticCode::ArgumentCount);
}

TEST(PapyrusTypeChecker, TooFewArgumentsAreRejected) {
    Fixture f;
    f.writeWithHierarchy("    Takes(1)", kCallee);

    const auto diagnostics = f.check("A");
    ASSERT_TRUE(diagnostics.size() == 1);
    EXPECT_TRUE(diagnostics[0].code == DiagnosticCode::ArgumentCount);
}

TEST(PapyrusTypeChecker, OmittingAnOptionalArgumentIsFine) {
    Fixture f;
    f.writeWithHierarchy("    Takes(1, \"x\")", kCallee);

    EXPECT_TRUE(f.check("A").empty());
}

TEST(PapyrusTypeChecker, ANamedArgumentMaySkipAnEarlierOptionalOne) {
    Fixture f;
    f.writeWithHierarchy("    Takes(1, \"x\", c = 2.0)", kCallee);

    EXPECT_TRUE(f.check("A").empty());
}

TEST(PapyrusTypeChecker, ANamedArgumentThatMatchesNoParameterIsRejected) {
    Fixture f;
    f.writeWithHierarchy("    Takes(1, \"x\", nope = 2.0)", kCallee);

    const auto diagnostics = f.check("A");
    ASSERT_TRUE(diagnostics.size() == 1);
    EXPECT_TRUE(diagnostics[0].code == DiagnosticCode::UnknownArgumentName);
}

TEST(PapyrusTypeChecker, AnArgumentOfTheWrongTypeIsRejected) {
    Fixture f;
    f.writeWithHierarchy("\n    float f = 1.5\n    Takes(f, \"x\")", kCallee);

    const auto diagnostics = f.check("A");
    ASSERT_TRUE(diagnostics.size() == 1);
    EXPECT_TRUE(diagnostics[0].message.find("pass to 'a'") != std::string::npos);
}

TEST(PapyrusTypeChecker, ArrayBuiltinArgumentsAreLeftUnchecked) {
    Fixture f;
    f.writeWithHierarchy("\n    int[] items = new int[4]\n    items.Add(1, 2)");

    EXPECT_TRUE(f.check("A").empty());
}

TEST(PapyrusTypeChecker, ABoolCastsToANumberEvenThoughTheReferenceOmitsIt) {
    for (const std::string_view target : {"int", "float"}) {
        Fixture f;
        f.writeWithHierarchy("\n    bool flag = true\n    " + std::string(target) +
                             " n = flag as " + std::string(target));
        EXPECT_TRUE(f.check("A").empty());
    }
}

TEST(PapyrusTypeChecker, CastingToAStructIsRejected) {
    Fixture f;
    f.write("A",
            "\nScriptName A\nStruct Point\n    int X\nEndStruct\nFunction Go()\n    int n = 1\n"
            "    Point p = n as Point\nEndFunction");

    const auto diagnostics = f.check("A");
    ASSERT_TRUE(diagnostics.size() == 1);
    EXPECT_TRUE(diagnostics[0].code == DiagnosticCode::InvalidCast);
}

TEST(PapyrusTypeChecker, ADowncastBetweenRelatedObjectsIsAllowed) {
    Fixture f;
    f.writeWithHierarchy("\n    Form f = None\n    Actor a = f as Actor");

    EXPECT_TRUE(f.check("A").empty());
}

TEST(PapyrusTypeChecker, AnOverrideWithADifferentParameterCountIsRejected) {
    Fixture f;
    f.write("Base", "\nScriptName Base\nFunction Go(int a)\nEndFunction");
    f.write("Child", "\nScriptName Child extends Base\nFunction Go(int a, int b)\nEndFunction");

    const auto diagnostics = f.check("Child");
    ASSERT_TRUE(diagnostics.size() == 1);
    EXPECT_TRUE(diagnostics[0].code == DiagnosticCode::OverrideMismatch);
}

TEST(PapyrusTypeChecker, AMatchingOverrideIsFine) {
    Fixture f;
    f.write("Base", "\nScriptName Base\nFunction Go(int a)\nEndFunction");
    f.write("Child", "\nScriptName Child extends Base\nFunction Go(int a)\nEndFunction");

    EXPECT_TRUE(f.check("Child").empty());
}

TEST(PapyrusTypeChecker, ARequiredParameterAfterAnOptionalOneIsRejected) {
    Fixture f;
    f.write("A", "\nScriptName A\nFunction Go(int a = 1, int b)\nEndFunction");

    const auto diagnostics = f.check("A");
    ASSERT_TRUE(diagnostics.size() == 1);
    EXPECT_TRUE(diagnostics[0].code == DiagnosticCode::ParameterOrder);
}

TEST(PapyrusTypeChecker, NothingIsReportedWhenTheSourcesAreIncomplete) {
    Fixture f;
    f.write("Child",
            "\nScriptName Child extends NotOnDisk\nint Function Go()\n"
            "    return \"this would be wrong if we could see the parent\"\nEndFunction");

    EXPECT_TRUE(f.check("Child").empty());
}

TEST(PapyrusTypeChecker, ACallDoesNotBindToASameNamedLocal) {
    Fixture f;
    f.write("A",
            "\nScriptName A\nint Function Value()\n    return 1\nEndFunction\n"
            "Function Go(string value)\n    int n = Value()\nEndFunction");

    EXPECT_TRUE(f.check("A").empty());
}

TEST(PapyrusTypeChecker, AVarAcceptsAnythingAndIsAcceptedAnywhere) {
    Fixture f;
    f.writeWithHierarchy("\n    var slot = 1\n    slot = \"text\"\n    int n = 0\n    n = slot as int");

    EXPECT_TRUE(f.check("A").empty());
}
