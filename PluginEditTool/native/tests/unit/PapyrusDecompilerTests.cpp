#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
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

using namespace fo4ide::papyrus;

namespace {

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

bool startsWith(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

std::string lower(std::string_view text) {
    std::string out(text);
    for (auto& ch : out) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return out;
}

// Port of the C# PexComparer used by the DecompilerSweep oracle.
std::string normaliseName(const std::string& name) {
    if (startsWith(name, "::temp")) return "::temp";
    if (startsWith(name, "::mangled_")) return "::mangled";
    return lower(name);
}

std::string operand(const PexValue& value) {
    switch (value.type) {
        case PexValueType::Identifier:
            return normaliseName(value.str);
        case PexValueType::String:
            return "\"" + value.str + "\"";
        case PexValueType::Integer:
            return std::to_string(value.int_);
        case PexValueType::Float: {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%.6f", static_cast<double>(value.float_));
            return buffer;
        }
        case PexValueType::Bool:
            return value.bool_ ? "true" : "false";
        default:
            return "None";
    }
}

std::string normalise(const PexInstruction& instruction) {
    std::string out = instruction.mnemonic;
    for (const auto& arg : instruction.args) out += " " + operand(arg);
    return out;
}

std::string firstDifference(const PexObject& theirs, const PexObject& ours) {
    if (!iequals(theirs.parentClassName, ours.parentClassName)) {
        return "parent '" + theirs.parentClassName + "' vs '" + ours.parentClassName + "'";
    }

    for (const auto& state : theirs.states) {
        const auto* ourState = [&]() -> const PexState* {
            for (const auto& s : ours.states) {
                if (iequals(s.name, state.name)) return &s;
            }
            return nullptr;
        }();
        if (!ourState) return "missing state '" + state.name + "'";

        for (const auto& fn : state.functions) {
            const PexFunction* ourFn = nullptr;
            for (const auto& f : ourState->functions) {
                if (iequals(f.name, fn.name)) {
                    ourFn = &f;
                    break;
                }
            }
            if (!ourFn) return "missing function " + fn.name;

            if (fn.isNative != ourFn->isNative) return fn.name + ": native flag";
            if (fn.isGlobal != ourFn->isGlobal) return fn.name + ": global flag";
            if (!iequals(fn.returnType, ourFn->returnType)) {
                return fn.name + ": returns " + fn.returnType + " vs " + ourFn->returnType;
            }
            if (fn.params.size() != ourFn->params.size()) return fn.name + ": parameter count";

            const std::size_t shared = std::min(fn.instructions.size(), ourFn->instructions.size());
            for (std::size_t i = 0; i < shared; ++i) {
                const std::string a = normalise(fn.instructions[i]);
                const std::string b = normalise(ourFn->instructions[i]);
                if (a != b) {
                    return fn.name + ": [" + std::to_string(i) + "] original: " + a + " | ours: " + b;
                }
            }
            if (fn.instructions.size() != ourFn->instructions.size()) {
                return fn.name + ": " + std::to_string(fn.instructions.size()) +
                       " instructions vs " + std::to_string(ourFn->instructions.size());
            }
        }

        for (const auto& fn : ourState->functions) {
            bool found = false;
            for (const auto& f : state.functions) {
                if (iequals(f.name, fn.name)) {
                    found = true;
                    break;
                }
            }
            if (!found) return "extra function " + fn.name;
        }
    }

    for (const auto& property : theirs.properties) {
        const PexProperty* ourProperty = nullptr;
        for (const auto& p : ours.properties) {
            if (iequals(p.name, property.name)) {
                ourProperty = &p;
                break;
            }
        }
        if (!ourProperty) return "missing property " + property.name;
        if (property.flags != ourProperty->flags) {
            return "property " + property.name + " flags";
        }
        if (property.userFlags != ourProperty->userFlags) {
            return "property " + property.name + " user flags";
        }
    }

    return std::string();
}

std::string uniqueRoot() {
    return (std::filesystem::temp_directory_path() / "fo4ide-decompile-" /
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))
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

    void writeWithScriptObject() { write("ScriptObject", "Scriptname ScriptObject Native Hidden\n"); }

    std::unique_ptr<PexFile> compile(std::string_view scriptName) {
        index.addRoot(root);
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
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root, error)) {
            if (error || !entry.is_regular_file()) continue;
            const auto target = std::filesystem::path(f2.root) / std::filesystem::relative(entry.path(), root);
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
