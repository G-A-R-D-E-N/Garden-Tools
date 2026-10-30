#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <fo4ide/papyrus/CodeGenerator.hpp>
#include <fo4ide/papyrus/Decompiler.hpp>
#include <fo4ide/papyrus/Parser.hpp>
#include <fo4ide/papyrus/Resolver.hpp>
#include <fo4ide/papyrus/ScriptIndex.hpp>
#include <fo4ide/papyrus/TypeChecker.hpp>

#include "../helpers/PexComparer.hpp"
#include "../helpers/TempRoot.hpp"

using namespace fo4ide::papyrus;
using namespace fo4ide::papyrus::test;
using fo4ide_test::TempRoot;

namespace {

struct Fixture {
    TempRoot temp{"fo4ide-decompile-"};
    ScriptIndex index;

    void write(std::string_view scriptName, std::string_view source) {
        std::string relative(scriptName);
        std::replace(relative.begin(), relative.end(), ':', '/');
        const auto path = temp.path() / (relative + ".psc");
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path);
        out << source;
    }

    void writeWithScriptObject() { write("ScriptObject", "Scriptname ScriptObject Native Hidden\n"); }

    std::unique_ptr<PexFile> compile(std::string_view scriptName) {
        index.addRoot(temp.path());
        const Script* script = index.resolve(scriptName);
        REQUIRE(script != nullptr);
        if (script->hasErrors()) return nullptr;

        for (const auto& d : declarationCheck(*script)) {
            if (d.severity == Severity::Error) return nullptr;
        }

        const auto resolution = Resolver(index).resolve(script);
        TypeChecker checker(index);
        const auto checked = checker.check(resolution);
        for (const auto& d : checked) {
            if (d.severity == Severity::Error) return nullptr;
        }

        CodeGenOptions options;
        options.sourceFileName = std::string(scriptName) + ".psc";
        options.userName = "test";
        options.computerName = "native";
        options.emitDebugInfo = true;
        options.emitDebugOnlyCode = true;
        options.emitBetaOnlyCode = true;

        CodeGenerator generator(index);
        std::vector<Diagnostic> diagnostics;
        auto* pex = generator.generate(*script, resolution, options, diagnostics);
        if (!pex) return nullptr;
        return std::unique_ptr<PexFile>(pex);
    }

    // compile -> decompile(source) -> parse -> compile, asserting the second
    // PEX matches the first (the C# DecompilerSweep oracle's core assertion).
    void roundTrip(std::string_view scriptName) {
        auto original = compile(scriptName);
        REQUIRE(original != nullptr);
        REQUIRE(original->objects.size() == 1);

        const std::string text = Decompiler::toSource(*original);
        INFO("decompiled source:\n" << text);

        std::string relative(scriptName);
        std::replace(relative.begin(), relative.end(), ':', '/');
        const std::string name = relative.substr(relative.find_last_of('/') + 1);

        const auto reparsed = Parser::parse(text, name + ".psc");
        REQUIRE_FALSE(reparsed.hasErrors());

        // The C# sweep compiles both passes against the same index, so every
        // source file the first pass could resolve (e.g. a callee script) must
        // be visible to the second pass too.
        Fixture f2;
        std::error_code error;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(temp.path(), error)) {
            if (error || !entry.is_regular_file()) continue;
            const auto target = f2.temp.path() / std::filesystem::relative(entry.path(), temp.path());
            std::filesystem::create_directories(target.parent_path(), error);
            std::filesystem::copy_file(entry.path(), target,
                                       std::filesystem::copy_options::overwrite_existing, error);
        }
        f2.write(scriptName, text);
        auto second = f2.compile(scriptName);
        REQUIRE(second != nullptr);
        REQUIRE(second->objects.size() == 1);

        const std::string difference = firstDifference(original->objects[0], second->objects[0]);
        INFO("difference: " << difference);
        CHECK(difference.empty());
    }
};

}  // namespace

TEST_CASE("Decompiler a simple function round-trips") {
    Fixture f;
    f.writeWithScriptObject();
    f.write("Simple",
            "Scriptname Simple\n"
            "int Function Go()\n"
            "    int i = 5\n"
            "    i += 1\n"
            "    Return i\n"
            "EndFunction\n");
    f.roundTrip("Simple");
}

TEST_CASE("Decompiler arithmetic and string concat round-trip") {
    Fixture f;
    f.writeWithScriptObject();
    f.write("Arith",
            "Scriptname Arith\n"
            "Function Go(int a, float b)\n"
            "    int c = (a * 2) + 1\n"
            "    float d = b / 2.0\n"
            "    string s = \"x\" + \"y\"\n"
            "    bool eq = a == 3\n"
            "EndFunction\n");
    f.roundTrip("Arith");
}

TEST_CASE("Decompiler an if else chain round-trips") {
    Fixture f;
    f.writeWithScriptObject();
    f.write("Cond",
            "Scriptname Cond\n"
            "Function Go(int a)\n"
            "    if a > 2\n"
            "        a = 1\n"
            "    else\n"
            "        a = 2\n"
            "    endIf\n"
            "EndFunction\n");
    f.roundTrip("Cond");
}

TEST_CASE("Decompiler a while loop round-trips") {
    Fixture f;
    f.writeWithScriptObject();
    f.write("Loop",
            "Scriptname Loop\n"
            "Function Go()\n"
            "    int i = 0\n"
            "    while i < 5\n"
            "        i += 1\n"
            "    endWhile\n"
            "EndFunction\n");
    f.roundTrip("Loop");
}

TEST_CASE("Decompiler an auto property round-trips") {
    Fixture f;
    f.writeWithScriptObject();
    f.write("Props",
            "Scriptname Props\n"
            "int Property Count = 3 Auto\n"
            "Function Go()\n"
            "    Count = 7\n"
            "EndFunction\n");
    f.roundTrip("Props");
}

TEST_CASE("Decompiler a custom property with handlers round-trips") {
    Fixture f;
    f.writeWithScriptObject();
    f.write("CustomProp",
            "Scriptname CustomProp\n"
            "int mValue = 0\n"
            "int Property Value\n"
            "    int Function Get()\n"
            "        Return mValue\n"
            "    EndFunction\n"
            "    Function Set(int newValue)\n"
            "        mValue = newValue\n"
            "    EndFunction\n"
            "EndProperty\n");
    f.roundTrip("CustomProp");
}

TEST_CASE("Decompiler structs and struct access round-trip") {
    Fixture f;
    f.writeWithScriptObject();
    f.write("Structs",
            "Scriptname Structs\n"
            "Struct Pair\n"
            "    int iKey = 0\n"
            "    string sValue = \"\"\n"
            "EndStruct\n"
            "Function Go()\n"
            "    Pair p = new Pair\n"
            "    p.iKey = 5\n"
            "EndFunction\n");
    f.roundTrip("Structs");
}

TEST_CASE("Decompiler arrays round-trip") {
    Fixture f;
    f.writeWithScriptObject();
    f.write("Arrays",
            "Scriptname Arrays\n"
            "Function Go()\n"
            "    int[] xs = new int[3]\n"
            "    xs[0] = 1\n"
            "    xs.Add(2)\n"
            "    int n = xs.Length\n"
            "EndFunction\n");
    f.roundTrip("Arrays");
}

TEST_CASE("Decompiler calls on other objects round-trip") {
    Fixture f;
    f.writeWithScriptObject();
    f.write("Holder", "Scriptname Holder\nint Function Give(int n)\n    Return n\nEndFunction\n");
    f.write("User",
            "Scriptname User\n"
            "Function Go(Holder h)\n"
            "    h.Give(5)\n"
            "    int x = h.Give(1)\n"
            "EndFunction\n");
    f.roundTrip("User");
}

TEST_CASE("Decompiler a native function is emitted as a header only") {
    Fixture f;
    f.writeWithScriptObject();
    // A native function is a header only: it has no body and no terminator,
    // exactly as the C# oracle parses it (PapyrusParserTests: "Native function
    // has no body and no terminator").
    f.write("NativeFn",
            "Scriptname NativeFn\n"
            "Function Ping() Native\n");
    auto pex = f.compile("NativeFn");
    REQUIRE(pex != nullptr);
    const std::string text = Decompiler::toSource(*pex);
    CHECK(text.find("Function Ping() Native") != std::string::npos);
    CHECK(text.find("EndFunction\n\nEndFunction") == std::string::npos);
}

TEST_CASE("Decompiler boolean and comparison forms reparse cleanly") {
    Fixture f;
    f.writeWithScriptObject();
    f.write("Logic",
            "Scriptname Logic\n"
            "Function Go(int a, int b)\n"
            "    bool c = (a > 2) && (b < 4)\n"
            "    if !c\n"
            "        b = 0\n"
            "    endIf\n"
            "EndFunction\n");
    auto original = f.compile("Logic");
    REQUIRE(original != nullptr);
    // The && short-circuit decompiles to a jump-threaded if; the C# sweep
    // counts that shape as "differed", so here we only require that the
    // decompiled source reparses and recompiles without errors.
    const std::string text = Decompiler::toSource(*original);
    const auto reparsed = Parser::parse(text, "Logic.psc");
    REQUIRE_FALSE(reparsed.hasErrors());
    Fixture f2;
    f2.write("Logic", text);
    REQUIRE(f2.compile("Logic") != nullptr);
}

TEST_CASE("Decompiler the composite RoundTrip script round-trips") {
    Fixture f;
    f.writeWithScriptObject();
    f.write("RoundTrip",
            "Scriptname RoundTrip\n"
            "{A script with most of the shapes in it}\n"
            "Struct Pair\n"
            "    int iKey = 0\n"
            "    string sValue = \"\"\n"
            "EndStruct\n"
            "int Property Count = 1 Auto Hidden\n"
            "int Property Mask = 8 AutoReadOnly\n"
            "string mName\n"
            "Event OnInit()\n"
            "    Count += 1\n"
            "    Pair p = new Pair\n"
            "    p.iKey = Count\n"
            "    if Count > 2 && mName != \"\"\n"
            "        mName = \"big\"\n"
            "    else\n"
            "        mName = \"small\"\n"
            "    endIf\n"
            "    int i = 0\n"
            "    while i < Count\n"
            "        i += 1\n"
            "    endWhile\n"
            "EndEvent\n");
    auto original = f.compile("RoundTrip");
    REQUIRE(original != nullptr);
    // The && short-circuit decompiles to a jump-threaded If, and the zero-init
    // local gets hoisted to the top of the function, so the round-trip PEX
    // differs from the original the same way it does under the C# sweep. Here
    // we only require that the decompiled source reparses and recompiles
    // without errors, mirroring the Logic test.
    const std::string text = Decompiler::toSource(*original);
    const auto reparsed = Parser::parse(text, "RoundTrip.psc");
    REQUIRE_FALSE(reparsed.hasErrors());
    Fixture f2;
    f2.write("RoundTrip", text);
    REQUIRE(f2.compile("RoundTrip") != nullptr);
}

TEST_CASE("Decompiler a remote event name is reconstructed") {
    Fixture f;
    f.writeWithScriptObject();
    f.write("Sender", "Scriptname Sender\n");
    f.write("Listener",
            "Scriptname Listener\n"
            "Event Sender.OnSomething(Sender akSender)\n"
            "    int x = 1\n"
            "EndEvent\n");
    auto pex = f.compile("Listener");
    REQUIRE(pex != nullptr);
    const std::string text = Decompiler::toSource(*pex);
    CHECK(text.find("Event Sender.OnSomething(Sender akSender)") != std::string::npos);
}

TEST_CASE("Decompiler disassemble lists functions and instructions") {
    Fixture f;
    f.writeWithScriptObject();
    f.write("Asm", "Scriptname Asm\nFunction Go()\n    int i = 1\nEndFunction\n");
    auto pex = f.compile("Asm");
    REQUIRE(pex != nullptr);

    const std::string text = Decompiler::disassemble(*pex);
    CHECK(text.find("; Disassembly of Asm.psc") != std::string::npos);
    CHECK(text.find(".object Asm extends ScriptObject") != std::string::npos);
    CHECK(text.find(".function Go -> None") != std::string::npos);
    CHECK(text.find("assign") != std::string::npos);
}
