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

TEST(PapyrusTypeChecker, ANonVoidFunctionMayFallOffItsEndBecauseTheCreationKitCompilesIt) {
    Fixture f;
    f.write("A",
            "\nScriptName A\nint Function Fall()\n    int x = 1\nEndFunction\n"
            "int Function Pick(bool b, bool c)\n    If b\n        Return 1\n    ElseIf c\n"
            "        Return 2\n    EndIf\nEndFunction");

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

TEST(PapyrusTypeChecker, AnOverrideWithADifferentParameterTypeIsRejected) {
    Fixture f;
    f.write("Base", "\nScriptName Base\nFunction Go(int a)\nEndFunction");
    f.write("Child", "\nScriptName Child extends Base\nFunction Go(float a)\nEndFunction");

    const auto diagnostics = f.check("Child");
    ASSERT_TRUE(diagnostics.size() == 1);
    EXPECT_TRUE(diagnostics[0].code == DiagnosticCode::OverrideMismatch);
    EXPECT_TRUE(diagnostics[0].message.find("whose parameter 1 is 'int', not 'float'") !=
                std::string::npos);
}

TEST(PapyrusTypeChecker, AnOverrideWithADifferentReturnTypeIsRejected) {
    Fixture f;
    f.write("Base", "\nScriptName Base\nint Function Go()\n    return 1\nEndFunction");
    f.write("Child", "\nScriptName Child extends Base\nstring Function Go()\n"
                     "    return \"text\"\nEndFunction");

    const auto diagnostics = f.check("Child");
    ASSERT_TRUE(diagnostics.size() == 1);
    EXPECT_TRUE(diagnostics[0].code == DiagnosticCode::OverrideMismatch);
    EXPECT_TRUE(diagnostics[0].message.find("which returns int, not string") != std::string::npos);
}

TEST(PapyrusTypeChecker, AnOverrideWithTheSameParameterAndReturnTypesIsFine) {
    Fixture f;
    f.write("Base", "\nScriptName Base\nint Function Go(int a)\n    return a\nEndFunction");
    f.write("Child", "\nScriptName Child extends Base\nint Function Go(int a)\n"
                     "    return a\nEndFunction");

    EXPECT_TRUE(f.check("Child").empty());
}

TEST(PapyrusTypeChecker, AnOverrideMayNotNarrowAParameterToAChildObject) {
    Fixture f;
    f.write("Form", "\nScriptName Form");
    f.write("Actor", "\nScriptName Actor extends Form");
    f.write("Base", "\nScriptName Base\nFunction Take(Form akForm)\nEndFunction");
    f.write("Child", "\nScriptName Child extends Base\nFunction Take(Actor akForm)\nEndFunction");

    const auto diagnostics = f.check("Child");
    ASSERT_TRUE(diagnostics.size() == 1);
    EXPECT_TRUE(diagnostics[0].code == DiagnosticCode::OverrideMismatch);
    EXPECT_TRUE(diagnostics[0].message.find("whose parameter 1 is 'Form', not 'Actor'") !=
                std::string::npos);
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

// Invariant: an explicit float-to-int cast is accepted, so a regression in isExplicit cannot silently reject it.
TEST(PapyrusTypeChecker, AnExplicitFloatToIntCastIsAccepted) {
    Fixture f;
    f.writeWithHierarchy("\n    float f = 1.5\n    int n = f as int");

    EXPECT_TRUE(f.check("A").empty());
}

// Invariant: an int cast to an object type is refused as InvalidCast, not treated as a downcast of a number.
TEST(PapyrusTypeChecker, CastingAnIntToAnObjectIsRefused) {
    Fixture f;
    f.writeWithHierarchy("\n    int n = 1\n    Actor a = n as Actor");

    const auto diagnostics = f.check("A");
    ASSERT_TRUE(diagnostics.size() == 1);
    EXPECT_TRUE(diagnostics[0].code == DiagnosticCode::InvalidCast);
}

// Invariant: a cast between two object types with no inheritance between them is refused as InvalidCast.
TEST(PapyrusTypeChecker, CastingBetweenUnrelatedObjectsIsRefused) {
    Fixture f;
    f.writeWithHierarchy("\n    Actor a = None\n    Weapon w = a as Weapon");
    f.write("Weapon", "ScriptName Weapon extends Form");

    const auto diagnostics = f.check("A");
    ASSERT_TRUE(diagnostics.size() == 1);
    EXPECT_TRUE(diagnostics[0].code == DiagnosticCode::InvalidCast);
}

// Invariant: an int may be cast to a string explicitly, so as-string on a number is not refused.
TEST(PapyrusTypeChecker, AnIntCastToAStringIsAccepted) {
    Fixture f;
    f.writeWithHierarchy("\n    int n = 1\n    string s = n as string");

    EXPECT_TRUE(f.check("A").empty());
}

// Invariant: an int compared with a string is accepted under all six operators, so string mixes are not over-refused.
TEST(PapyrusTypeChecker, EveryComparisonOperatorAcceptsAnIntAgainstAString) {
    Fixture f;
    f.writeWithHierarchy(
        "\n    int x = 0\n    bool a = x == \"x\"\n    bool b = x != \"x\"\n    bool c = x < \"x\"\n"
        "    bool d = x > \"x\"\n    bool e = x <= \"x\"\n    bool g = x >= \"x\"");

    EXPECT_TRUE(f.check("A").empty());
}

// Invariant: an object compared with an int is refused under every comparison operator.
TEST(PapyrusTypeChecker, EveryComparisonOperatorRefusesAnObjectAgainstAnInt) {
    Fixture f;
    f.writeWithHierarchy(
        "\n    Actor a = None\n    int x = 0\n    bool b = a == x\n    bool c = a != x\n"
        "    bool d = a < x\n    bool e = a > x\n    bool g = a <= x\n    bool h = a >= x");

    const auto diagnostics = f.check("A");
    ASSERT_TRUE(diagnostics.size() == 6);
    for (const auto& d : diagnostics) EXPECT_TRUE(d.code == DiagnosticCode::TypeMismatch);
}

// Invariant: an object may be compared with a bool in either operand order, as vanilla scripts do with false.
TEST(PapyrusTypeChecker, AnObjectMayBeComparedWithABoolInEitherOrder) {
    Fixture f;
    f.writeWithHierarchy("\n    Actor a = None\n    bool b = a == false\n    bool c = false == a");

    EXPECT_TRUE(f.check("A").empty());
}

// Invariant: two unrelated script object types cannot be compared, so a sibling compare is refused.
TEST(PapyrusTypeChecker, UnrelatedObjectTypesCannotBeCompared) {
    Fixture f;
    f.writeWithHierarchy("\n    Actor a = None\n    Weapon w = None\n    bool same = a == w");
    f.write("Weapon", "ScriptName Weapon extends Form");

    const auto diagnostics = f.check("A");
    ASSERT_TRUE(diagnostics.size() == 1);
    EXPECT_TRUE(diagnostics[0].code == DiagnosticCode::TypeMismatch);
}

// Invariant: int and float operands compare without a diagnostic, so numeric promotion is not over-refused.
TEST(PapyrusTypeChecker, IntAndFloatOperandsMayBeCompared) {
    Fixture f;
    f.writeWithHierarchy("\n    int i = 1\n    float f = 1.0\n    bool a = i == f\n    bool b = f < i");

    EXPECT_TRUE(f.check("A").empty());
}

// Invariant: same-typed string, int and bool operands compare without a diagnostic.
TEST(PapyrusTypeChecker, SameTypedOperandsMayBeCompared) {
    Fixture f;
    f.writeWithHierarchy(
        "\n    string s = \"\"\n    int i = 1\n    bool p = true\n    bool a = s == \"\"\n"
        "    bool b = i != 2\n    bool c = p == true");

    EXPECT_TRUE(f.check("A").empty());
}

// Invariant: None compared with an object is accepted in either operand order.
TEST(PapyrusTypeChecker, NoneMayBeComparedWithAnObjectInEitherOrder) {
    Fixture f;
    f.writeWithHierarchy("\n    Actor a = None\n    bool b = a == None\n    bool c = None != a");

    EXPECT_TRUE(f.check("A").empty());
}

// Invariant: an object compared with its base or its child is accepted in either direction.
TEST(PapyrusTypeChecker, ObjectsRelatedByInheritanceMayBeComparedInEitherDirection) {
    Fixture f;
    f.writeWithHierarchy("\n    Form f = None\n    Actor a = None\n    bool b = f == a\n    bool c = a != f");

    EXPECT_TRUE(f.check("A").empty());
}

// Invariant: an object whose ancestry is not fully on disk is not refused, because the index cannot prove it unrelated.
TEST(PapyrusTypeChecker, AnObjectWhoseAncestryIsNotOnDiskIsNotRefused) {
    Fixture f;
    f.writeWithHierarchy("\n    Form f = None\n    Actor a = None\n    bool b = f == a");
    f.write("Actor", "ScriptName Actor extends NotOnDisk");

    EXPECT_TRUE(f.check("A").empty());
}

// Invariant: a literal array length above the Creation Kit limit of 128 is reported once, as a warning.
TEST(PapyrusTypeChecker, ALiteralArrayLengthAbove128IsAWarning) {
    Fixture f;
    f.writeWithHierarchy("\n    int[] items = new int[129]");

    const auto diagnostics = f.check("A");
    ASSERT_TRUE(diagnostics.size() == 1);
    EXPECT_TRUE(diagnostics[0].code == DiagnosticCode::ArrayLengthLimit);
    EXPECT_TRUE(diagnostics[0].severity == fo4::papyrus::Severity::Warning);
}

// Invariant: a literal array length of exactly 128 is at the Creation Kit limit and is not reported.
TEST(PapyrusTypeChecker, ALiteralArrayLengthOf128IsAccepted) {
    Fixture f;
    f.writeWithHierarchy("\n    int[] items = new int[128]");

    EXPECT_TRUE(f.check("A").empty());
}
