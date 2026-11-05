#include <algorithm>
#include "../helpers/TempRoot.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/papyrus/CodeGenerator.hpp>
#include <fo4ide/papyrus/Diagnostic.hpp>
#include <fo4ide/papyrus/Parser.hpp>
#include <fo4ide/papyrus/Pex.hpp>
#include <fo4ide/papyrus/Resolver.hpp>
#include <fo4ide/papyrus/ScriptIndex.hpp>
#include <fo4ide/papyrus/TypeChecker.hpp>

namespace {

using fo4ide::papyrus::CodeGenOptions;
using fo4ide::papyrus::CodeGenerator;
using fo4ide::papyrus::Diagnostic;
using fo4ide::papyrus::DiagnosticCode;
using fo4ide::papyrus::PexFile;
using fo4ide::papyrus::PexFunction;
using fo4ide::papyrus::PexObject;
using fo4ide::papyrus::PexValue;
using fo4ide::papyrus::PexValueType;
using fo4ide::papyrus::Resolution;
using fo4ide::papyrus::Resolver;
using fo4ide::papyrus::Script;
using fo4ide::papyrus::ScriptIndex;
using fo4ide::papyrus::TypeChecker;

std::string uniqueRoot() {
    const auto base = std::filesystem::temp_directory_path() / "fo4re-codegen";
    std::filesystem::create_directories(base);
    return (base / fo4ide_test::uniqueTempName())
        .string();
}

struct CompileResult {
    bool success{false};
    bool sourcesComplete{false};
    std::unique_ptr<PexFile> pex;
    std::vector<Diagnostic> diagnostics;
};

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

    CompileResult compile(std::string_view scriptName, bool emitDebugOnly = true) {
        index.addRoot(root);
        const Script* script = index.resolve(scriptName);
        REQUIRE(script != nullptr);

        CompileResult result;
        if (script->hasErrors()) {
            result.diagnostics = script->diagnostics;
            result.sourcesComplete = false;
            return result;
        }

        result.diagnostics = fo4ide::papyrus::declarationCheck(*script);
        for (const auto& d : result.diagnostics) {
            if (d.severity == fo4ide::papyrus::Severity::Error) return result;
        }

        const auto resolution = Resolver(index).resolve(script);
        result.diagnostics.insert(result.diagnostics.end(), resolution.diagnostics.begin(),
                                  resolution.diagnostics.end());

        TypeChecker checker(index);
        const auto checked = checker.check(resolution);
        result.diagnostics.insert(result.diagnostics.end(), checked.begin(), checked.end());
        for (const auto& d : result.diagnostics) {
            if (d.severity == fo4ide::papyrus::Severity::Error) {
                result.sourcesComplete = resolution.baseChainComplete;
                return result;
            }
        }

        CodeGenOptions options;
        options.sourceFileName = std::string(scriptName) + ".psc";
        options.emitDebugInfo = true;
        options.emitDebugOnlyCode = emitDebugOnly;
        CodeGenerator generator(index);
        std::vector<Diagnostic> genDiags;
        auto* pex = generator.generate(*script, resolution, options, genDiags);
        result.diagnostics.insert(result.diagnostics.end(), genDiags.begin(), genDiags.end());
        result.sourcesComplete = resolution.baseChainComplete;
        if (pex) {
            result.success = true;
            result.pex.reset(pex);
        }
        return result;
    }

    PexObject compileObject(std::string_view scriptName) {
        auto result = compile(scriptName);
        if (!result.success) {
            std::string joined;
            for (const auto& d : result.diagnostics) joined += d.toString() + " | ";
            FAIL("compilation should succeed; diagnostics were: " << joined);
        }
        REQUIRE(result.pex != nullptr);
        REQUIRE(result.pex->objects.size() == 1);
        return std::move(result.pex->objects[0]);
    }

    void writeWithScriptObject() { write("ScriptObject", "Scriptname ScriptObject Native Hidden\n"); }
};

std::string operand(const PexValue& v) {
    switch (v.type) {
        case PexValueType::Identifier:
            return v.str.rfind("::temp", 0) == 0 ? "T" : v.str;
        case PexValueType::String: {
            std::string out = "\"";
            for (const char ch : v.str) {
                if (ch == '"') out += "\\\"";
                else out += ch;
            }
            out += "\"";
            return out;
        }
        case PexValueType::Integer:
            return std::to_string(v.int_);
        case PexValueType::Float: {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%.3g", static_cast<double>(v.float_));
            std::string text = buffer;
            // Match the oracle's "0.0###" formatting.
            if (text.find('.') == std::string::npos) text += ".0";
            return text;
        }
        case PexValueType::Bool:
            return v.bool_ ? "true" : "false";
        default:
            return "None";
    }
}

const PexFunction& function(const PexObject& obj, std::string_view name, std::string_view state = "") {
    const PexFunction* found = nullptr;
    for (const auto& s : obj.states) {
        if (s.name != state) continue;
        for (const auto& fn : s.functions) {
            if (found == nullptr && fn.name == name) found = &fn;
        }
    }
    if (found == nullptr) FAIL("no function " << name << " in state '" << state << "'");
    return *found;
}

std::vector<std::string> listing(const PexFunction& fn) {
    std::vector<std::string> out;
    for (const auto& instruction : fn.instructions) {
        std::string line = instruction.mnemonic;
        for (const auto& arg : instruction.args) line += " " + operand(arg);
        out.push_back(std::move(line));
    }
    return out;
}

}  // namespace

TEST_CASE("CodeGen a script that extends nothing still names ScriptObject as its parent") {
    Fixture f;
    f.writeWithScriptObject();
    f.write("Bare", "Scriptname Bare\n");
    CHECK(f.compileObject("Bare").parentClassName == "ScriptObject");
}

TEST_CASE("CodeGen ScriptObject itself has no parent") {
    Fixture f;
    f.writeWithScriptObject();
    CHECK(f.compileObject("ScriptObject").parentClassName.empty());
}

TEST_CASE("CodeGen header carries the docstring const flag and user flags") {
    Fixture f;
    f.writeWithScriptObject();
    f.write("Meta", "Scriptname Meta Const Hidden\n{What it is}\n");
    const auto obj = f.compileObject("Meta");

    CHECK(obj.docString == "What it is");
    CHECK(obj.isConst);
    CHECK(obj.userFlags == 1u);
}

TEST_CASE("CodeGen an auto property gets a backing variable and the read write autovar flags") {
    Fixture f;
    f.writeWithScriptObject();
    f.write("Props", "Scriptname Props\nint Property Count = 3 Auto Mandatory\n");
    const auto obj = f.compileObject("Props");

    REQUIRE(obj.properties.size() == 1);
    const auto& property = obj.properties[0];
    CHECK(property.flags == 0x07);
    CHECK(property.autoVarName == "::Count_var");
    CHECK(property.userFlags == 32u);

    REQUIRE(obj.variables.size() == 1);
    CHECK(obj.variables[0].name == "::Count_var");
    CHECK(obj.variables[0].type == "Int");
    REQUIRE(obj.variables[0].defaultValue.has_value());
    CHECK(obj.variables[0].defaultValue->int_ == 3);
}

TEST_CASE("CodeGen an auto read only property becomes a getter returning the constant") {
    Fixture f;
    f.writeWithScriptObject();
    f.write("Consts", "Scriptname Consts\nint Property Mask = 0x00200000 AutoReadOnly\n");
    const auto obj = f.compileObject("Consts");

    CHECK(obj.variables.empty());
    REQUIRE(obj.properties.size() == 1);
    const auto& property = obj.properties[0];
    CHECK(property.flags == 0x01);
    CHECK(property.autoVarName.empty());
    REQUIRE(property.readHandler.has_value());
    CHECK(listing(*property.readHandler) == std::vector<std::string>{"return 2097152"});
}

TEST_CASE("CodeGen a full property emits its handlers and no backing variable") {
    Fixture f;
    f.writeWithScriptObject();
    f.write("Full",
            "Scriptname Full\nint mCount\nint Property Count\n    int Function Get()\n"
            "        return mCount\n    EndFunction\n    Function Set(int aiValue)\n"
            "        mCount = aiValue\n    EndFunction\nEndProperty\n");
    const auto obj = f.compileObject("Full");

    REQUIRE(obj.properties.size() == 1);
    const auto& property = obj.properties[0];
    CHECK(property.flags == 0x03);
    CHECK_FALSE(property.isAutoVar());
    REQUIRE(obj.variables.size() == 1);
    CHECK(obj.variables[0].name == "mCount");
    REQUIRE(property.readHandler.has_value());
    REQUIRE(property.writeHandler.has_value());
    CHECK(listing(*property.readHandler) == std::vector<std::string>{"return mCount"});
    CHECK(listing(*property.writeHandler) == std::vector<std::string>{"assign mCount aiValue"});
}

TEST_CASE("CodeGen a struct keeps its member order in the debug struct order table") {
    Fixture f;
    f.writeWithScriptObject();
    f.write("WithStruct",
            "Scriptname WithStruct\nStruct Point\n    float fX = 0.0\n    float fY = 1.5\n"
            "EndStruct\n");
    const auto result = f.compile("WithStruct");
    CHECK(result.success);

    REQUIRE(result.pex != nullptr);
    REQUIRE(result.pex->structOrders.size() == 1);
    CHECK(result.pex->structOrders[0].memberNames == std::vector<std::string>{"fX", "fY"});
    REQUIRE(result.pex->objects.size() == 1);
    REQUIRE(result.pex->objects[0].structs.size() == 1);
    const auto& members = result.pex->objects[0].structs[0].members;
    REQUIRE(members.size() == 2);
    CHECK(members[0].type == "Float");
    CHECK(members[1].type == "Float");
    REQUIRE(members[1].defaultValue.has_value());
    CHECK(members[1].defaultValue->float_ == 1.5F);
}

TEST_CASE("CodeGen ungrouped properties are listed under an unnamed property group") {
    Fixture f;
    f.writeWithScriptObject();
    f.write("Grouped", "Scriptname Grouped\nint Property A Auto\nint Property B Auto\n");
    const auto result = f.compile("Grouped");

    REQUIRE(result.pex != nullptr);
    REQUIRE(result.pex->propertyGroups.size() == 1);
    CHECK(result.pex->propertyGroups[0].groupName.empty());
    CHECK(result.pex->propertyGroups[0].propertyNames == std::vector<std::string>{"A", "B"});
}

TEST_CASE("CodeGen a state override lands in its own state and the auto state is named") {
    Fixture f;
    f.writeWithScriptObject();
    f.write("States",
            "Scriptname States\nFunction Poke()\nEndFunction\nAuto State Idle\n    Function Poke()\n"
            "    EndFunction\nEndState\n");
    const auto obj = f.compileObject("States");

    CHECK(obj.autoStateName == "Idle");
    REQUIRE(obj.states.size() == 2);
    CHECK(obj.states[0].name.empty());
    CHECK(obj.states[1].name == "Idle");
    REQUIRE(obj.states[1].functions.size() == 1);
    CHECK(obj.states[1].functions[0].name == "Poke");
}

TEST_CASE("CodeGen a lone if emits a trailing jump to the end") {
    Fixture f;
    f.writeWithScriptObject();
    f.write("Flow",
            "Scriptname Flow\nFunction Go(bool abFlag)\n    if abFlag\n        Go(false)\n    endIf\n"
            "EndFunction\n");
    const auto obj = f.compileObject("Flow");

    CHECK(listing(function(obj, "Go")) ==
          std::vector<std::string>{"jmpf abFlag 3", "callmethod Go self ::nonevar false", "jmp 1"});
}

TEST_CASE("CodeGen an if else jumps over the else and the else falls through") {
    Fixture f;
    f.writeWithScriptObject();
    f.write("Flow2",
            "Scriptname Flow2\nFunction Go(bool abFlag)\n    if abFlag\n        Go(false)\n"
            "    else\n        Go(true)\n    endIf\nEndFunction\n");
    const auto obj = f.compileObject("Flow2");

    CHECK(listing(function(obj, "Go")) ==
          std::vector<std::string>{"jmpf abFlag 3", "callmethod Go self ::nonevar false", "jmp 2",
                                   "callmethod Go self ::nonevar true"});
}

TEST_CASE("CodeGen a while loop jumps back to its condition") {
    Fixture f;
    f.writeWithScriptObject();
    f.write("Loop",
            "Scriptname Loop\nFunction Go()\n    int i = 0\n    while i < 3\n        i += 1\n"
            "    endWhile\nEndFunction\n");
    const auto obj = f.compileObject("Loop");

    CHECK(listing(function(obj, "Go")) == std::vector<std::string>{
                                              "assign i 0", "cmp_lt T i 3", "jmpf T 3",
                                              "iadd i i 1", "jmp -3"});
}

TEST_CASE("CodeGen and short circuits through one shared slot") {
    Fixture f;
    f.writeWithScriptObject();
    f.write("Logic",
            "Scriptname Logic\nFunction Go(bool abLeft, bool abRight)\n    if abLeft && abRight\n"
            "        Go(false, false)\n    endIf\nEndFunction\n");
    const auto obj = f.compileObject("Logic");

    CHECK(listing(function(obj, "Go")) ==
          std::vector<std::string>{"cast T abLeft", "jmpf T 2", "cast T abRight", "jmpf T 3",
                                   "callmethod Go self ::nonevar false false", "jmp 1"});
}

TEST_CASE("CodeGen or short circuits on true") {
    Fixture f;
    f.writeWithScriptObject();
    f.write("Logic2",
            "Scriptname Logic2\nFunction Go(bool abLeft, bool abRight)\n    if abLeft || abRight\n"
            "        Go(false, false)\n    endIf\nEndFunction\n");
    const auto obj = f.compileObject("Logic2");

    const auto lines = listing(function(obj, "Go"));
    REQUIRE(lines.size() > 1);
    CHECK(lines[1] == "jmpt T 2");
}

TEST_CASE("CodeGen inequality is equality followed by not") {
    Fixture f;
    f.writeWithScriptObject();
    f.write("Cmp", "Scriptname Cmp\nFunction Go(int aiLeft)\n    bool b = aiLeft != 2\nEndFunction\n");
    const auto obj = f.compileObject("Cmp");

    CHECK(listing(function(obj, "Go")) == std::vector<std::string>{"cmp_eq b aiLeft 2", "not b b"});
}

TEST_CASE("CodeGen a mixed comparison promotes the int side to float") {
    Fixture f;
    f.writeWithScriptObject();
    f.write("Promote",
            "Scriptname Promote\nFunction Go(float afValue, int aiOther)\n    bool b = afValue > "
            "aiOther\nEndFunction\n");
    const auto obj = f.compileObject("Promote");

    CHECK(listing(function(obj, "Go")) == std::vector<std::string>{"cast T aiOther", "cmp_gt b afValue T"});
}

TEST_CASE("CodeGen an int literal in a float slot is folded") {
    Fixture f;
    f.writeWithScriptObject();
    f.write("Fold", "Scriptname Fold\nFunction Go()\n    float f = 5\nEndFunction\n");
    const auto obj = f.compileObject("Fold");

    CHECK(listing(function(obj, "Go")) == std::vector<std::string>{"assign f 5.0"});
}

TEST_CASE("CodeGen string concatenation uses strcat and casts the other side") {
    Fixture f;
    f.writeWithScriptObject();
    f.write("Cat",
            "Scriptname Cat\nFunction Go(int aiCount)\n    string s = \"n=\" + aiCount\n"
            "EndFunction\n");
    const auto obj = f.compileObject("Cat");

    CHECK(listing(function(obj, "Go")) ==
          std::vector<std::string>{"cast T aiCount", "strcat s \"n=\" T"});
}

TEST_CASE("CodeGen a local with no initialiser is still zeroed") {
    Fixture f;
    f.writeWithScriptObject();
    f.write("Zero", "Scriptname Zero\nFunction Go()\n    int i\n    string s\nEndFunction\n");
    const auto obj = f.compileObject("Zero");

    CHECK(listing(function(obj, "Go")) ==
          std::vector<std::string>{"assign i 0", "assign s \"\""});
}

TEST_CASE("CodeGen compound assignment reads modifies and writes in place") {
    Fixture f;
    f.writeWithScriptObject();
    f.write("Compound",
            "Scriptname Compound\nFunction Go()\n    float f = 1.0\n    f *= 2.0\nEndFunction\n");
    const auto obj = f.compileObject("Compound");

    CHECK(listing(function(obj, "Go")) == std::vector<std::string>{"assign f 1.0", "fmul f f 2.0"});
}

TEST_CASE("CodeGen new array and element access use the array opcodes") {
    Fixture f;
    f.writeWithScriptObject();
    f.write("Arrays",
            "Scriptname Arrays\nFunction Go()\n    int[] xs = new int[4]\n    xs[0] = 7\n"
            "    int n = xs.Length\n    xs.Add(9)\n    int at = xs.Find(9)\nEndFunction\n");
    const auto obj = f.compileObject("Arrays");

    CHECK(listing(function(obj, "Go")) == std::vector<std::string>{
                                              "array_create xs 4", "array_setelement xs 0 7",
                                              "array_length n xs", "array_add xs 9 1",
                                              "array_findelement xs at 9 0"});
}

TEST_CASE("CodeGen struct members are read and written through the struct opcodes") {
    Fixture f;
    f.writeWithScriptObject();
    f.write("Structs",
            "Scriptname Structs\nStruct Point\n    float fX = 0.0\nEndStruct\nFunction Go()\n"
            "    Point p = new Point\n    p.fX = 2.0\n    float got = p.fX\nEndFunction\n");
    const auto obj = f.compileObject("Structs");

    CHECK(listing(function(obj, "Go")) == std::vector<std::string>{
                                              "struct_create p", "assign T 2.0",
                                              "struct_set p fX T", "struct_get got p fX"});
}

TEST_CASE("CodeGen the is operator names its type as an identifier") {
    Fixture f;
    f.writeWithScriptObject();
    f.write("Other", "Scriptname Other\n");
    f.write("Check",
            "Scriptname Check\nFunction Go(ScriptObject akThing)\n    bool b = akThing is Other\n"
            "EndFunction\n");
    const auto obj = f.compileObject("Check");

    CHECK(listing(function(obj, "Go")) == std::vector<std::string>{"is b akThing Other"});
}

TEST_CASE("CodeGen optional arguments are filled in from the declaration") {
    Fixture f;
    f.writeWithScriptObject();
    f.write("Defaults",
            "Scriptname Defaults\nFunction Target(int aiFirst, bool abSecond = true, string "
            "asThird = \"x\")\nEndFunction\nFunction Go()\n    Target(1)\nEndFunction\n");
    const auto obj = f.compileObject("Defaults");

    CHECK(listing(function(obj, "Go")) ==
          std::vector<std::string>{"callmethod Target self ::nonevar 1 true \"x\""});
}

TEST_CASE("CodeGen a named argument is placed by name") {
    Fixture f;
    f.writeWithScriptObject();
    f.write("Named",
            "Scriptname Named\nFunction Target(int aiFirst = 1, int aiSecond = 2)\nEndFunction\n"
            "Function Go()\n    Target(aiSecond = 9)\nEndFunction\n");
    const auto obj = f.compileObject("Named");

    CHECK(listing(function(obj, "Go")) ==
          std::vector<std::string>{"callmethod Target self ::nonevar 1 9"});
}

TEST_CASE("CodeGen a bare call to an own global is a static call on this script") {
    Fixture f;
    f.writeWithScriptObject();
    f.write("Globals",
            "Scriptname Globals\nint Function Helper() Global\n    return 1\nEndFunction\n"
            "int Function Go() Global\n    return Helper()\nEndFunction\n");
    const auto obj = f.compileObject("Globals");

    CHECK(listing(function(obj, "Go")) ==
          std::vector<std::string>{"callstatic Globals Helper T", "return T"});
}

TEST_CASE("CodeGen a qualified global call names the receiving script") {
    Fixture f;
    f.writeWithScriptObject();
    f.write("Util", "Scriptname Util\nint Function Read() Global\n    return 2\nEndFunction\n");
    f.write("Caller",
            "Scriptname Caller\nFunction Go()\n    int n = Util.Read()\nEndFunction\n");
    const auto obj = f.compileObject("Caller");

    CHECK(listing(function(obj, "Go")) == std::vector<std::string>{"callstatic Util Read n"});
}

TEST_CASE("CodeGen a parent call uses callparent") {
    Fixture f;
    f.writeWithScriptObject();
    f.write("Base", "Scriptname Base\nFunction Poke()\nEndFunction\n");
    f.write("Derived",
            "Scriptname Derived extends Base\nFunction Poke()\n    Parent.Poke()\nEndFunction\n");
    const auto obj = f.compileObject("Derived");

    CHECK(listing(function(obj, "Poke")) == std::vector<std::string>{"callparent Poke ::nonevar"});
}

TEST_CASE("CodeGen a method call's receiver is evaluated before its arguments") {
    Fixture f;
    f.writeWithScriptObject();
    f.write("Order",
            "Scriptname Order\nOrder Function First() Global\n    return None\nEndFunction\n"
            "int Function Second() Global\n    return 1\nEndFunction\nFunction Take(int aiValue)\n"
            "EndFunction\nFunction Go() Global\n    First().Take(Second())\nEndFunction\n");
    const auto obj = f.compileObject("Order");

    CHECK(listing(function(obj, "Go")) == std::vector<std::string>{
                                              "callstatic Order First T", "callstatic Order Second T",
                                              "callmethod Take T ::nonevar T"});
}

TEST_CASE("CodeGen an own auto property is read and written through its backing variable") {
    Fixture f;
    f.writeWithScriptObject();
    f.write("Self",
            "Scriptname Self\nint Property Count Auto\nFunction Go()\n    Count = Count + 1\n"
            "EndFunction\n");
    const auto obj = f.compileObject("Self");

    CHECK(listing(function(obj, "Go")) ==
          std::vector<std::string>{"iadd ::Count_var ::Count_var 1"});
}

TEST_CASE("CodeGen another object's property goes through propget and propset") {
    Fixture f;
    f.writeWithScriptObject();
    f.write("Holder", "Scriptname Holder\nint Property Count Auto\n");
    f.write("User",
            "Scriptname User\nFunction Go(Holder akHolder)\n    akHolder.Count = 5\n"
            "    int n = akHolder.Count\nEndFunction\n");
    const auto obj = f.compileObject("User");

    CHECK(listing(function(obj, "Go")) == std::vector<std::string>{
                                              "assign T 5", "propset Count akHolder T",
                                              "propget Count akHolder n"});
}

TEST_CASE("CodeGen a remote event handler is named after its type and event") {
    Fixture f;
    f.writeWithScriptObject();
    f.write("Sender", "Scriptname Sender\n");
    f.write("Listener",
            "Scriptname Listener\nEvent Sender.OnSomething(Sender akSender)\nEndEvent\n");
    const auto obj = f.compileObject("Listener");

    REQUIRE(obj.states.size() == 1);
    REQUIRE(obj.states[0].functions.size() == 1);
    CHECK(obj.states[0].functions[0].name == "::remote_Sender_OnSomething");
}

TEST_CASE("CodeGen a void call written as a statement discards into the none local") {
    Fixture f;
    f.writeWithScriptObject();
    f.write("Void",
            "Scriptname Void\nint Function Answer()\n    return 1\nEndFunction\n"
            "Function Nothing()\nEndFunction\nFunction Go()\n    Nothing()\n    Answer()\n"
            "EndFunction\n");
    const auto obj = f.compileObject("Void");
    const auto& fn = function(obj, "Go");

    CHECK(listing(fn) == std::vector<std::string>{"callmethod Nothing self ::nonevar",
                                                  "callmethod Answer self T"});
    bool hasNoneVar = false;
    for (const auto& l : fn.locals) {
        if (l.name == "::nonevar" && l.type == "None") hasNoneVar = true;
    }
    CHECK(hasNoneVar);
}

TEST_CASE("CodeGen a call into a script that is not on the roots is refused rather than guessed") {
    Fixture f;
    f.writeWithScriptObject();
    f.write("Blind",
            "Scriptname Blind extends MissingBase\nFunction Go()\n    SomeInheritedThing()\n"
            "EndFunction\n");
    const auto result = f.compile("Blind");

    CHECK_FALSE(result.success);
    CHECK_FALSE(result.sourcesComplete);
    CHECK(result.pex == nullptr);
}

TEST_CASE("CodeGen an array builtin with the wrong argument count is refused not thrown") {
    const std::string_view calls[] = {"xs.Add()", "xs.Insert(1)", "xs.Remove()", "xs.Clear(1)",
                                      "xs.RemoveLast(1)"};
    for (const auto call : calls) {
        Fixture f;
        f.writeWithScriptObject();
        f.write("BadArray",
                "Scriptname BadArray\nFunction Go()\n    int[] xs = new int[2]\n    " +
                    std::string(call) + "\nEndFunction\n");
        const auto result = f.compile("BadArray");

        CHECK_FALSE(result.success);
        bool found = false;
        for (const auto& d : result.diagnostics) {
            if (d.code == DiagnosticCode::ArgumentCount) found = true;
        }
        CHECK(found);
    }
}

TEST_CASE("CodeGen an array builtin with a legal argument count still compiles") {
    const std::string_view calls[] = {"xs.Find(1)", "xs.Find(1, 2)", "xs.Add(1)", "xs.Add(1, 3)",
                                      "xs.Insert(1, 0)", "xs.Remove(0)", "xs.RemoveLast()",
                                      "xs.Clear()"};
    for (const auto call : calls) {
        Fixture f;
        f.writeWithScriptObject();
        f.write("GoodArray",
                "Scriptname GoodArray\nFunction Go()\n    int[] xs = new int[2]\n    " +
                    std::string(call) + "\nEndFunction\n");
        const auto result = f.compile("GoodArray");
        CHECK(result.success);
    }
}

TEST_CASE("CodeGen an array builtin with no opcode is refused by name") {
    Fixture f;
    f.writeWithScriptObject();
    f.write("NoOpcode",
            "Scriptname NoOpcode\nStruct Pair\n    int iKey = 0\nEndStruct\nFunction Go()\n"
            "    Pair[] xs = new Pair[2]\n    Pair[] hits = xs.GetMatchingStructs(\"iKey\", 1)\n"
            "EndFunction\n");
    const auto result = f.compile("NoOpcode");

    CHECK_FALSE(result.success);
    bool found = false;
    for (const auto& d : result.diagnostics) {
        if (d.message.find("GetMatchingStructs") != std::string::npos) found = true;
    }
    CHECK(found);
}

TEST_CASE("CodeGen a non literal variable initialiser is refused") {
    Fixture f;
    f.writeWithScriptObject();
    f.write("BadInit", "Scriptname BadInit\nint Property Count = 1 + 1 Auto\n");
    const auto result = f.compile("BadInit");

    CHECK_FALSE(result.success);
    bool found = false;
    for (const auto& d : result.diagnostics) {
        if (d.code == DiagnosticCode::NonConstantInitializer) found = true;
    }
    CHECK(found);
}

TEST_CASE("CodeGen a generated file writes and reads back identically") {
    Fixture f;
    f.writeWithScriptObject();
    f.write("RoundTrip",
            "Scriptname RoundTrip\n{A script with most of the shapes in it}\nStruct Pair\n"
            "    int iKey = 0\n    string sValue = \"\"\nEndStruct\n"
            "int Property Count = 1 Auto Hidden\nint Property Mask = 8 AutoReadOnly\n"
            "string mName\nEvent OnInit()\n    Count += 1\n    Pair p = new Pair\n"
            "    p.iKey = Count\n    if Count > 2 && mName != \"\"\n        mName = \"big\"\n"
            "    else\n        mName = \"small\"\n    endIf\n    int i = 0\n"
            "    while i < Count\n        i += 1\n    endWhile\nEndEvent\n");
    const auto result = f.compile("RoundTrip");
    CHECK(result.success);

    REQUIRE(result.pex != nullptr);
    const auto bytes = result.pex->toBytes();
    const auto reread = PexFile::fromBytes(bytes);
    CHECK(reread.toBytes() == bytes);

    REQUIRE(reread.objects.size() == 1);
    CHECK(reread.objects[0].name == "RoundTrip");
    CHECK(reread.hasDebugInfo);
    bool hasOnInit = false;
    for (const auto& df : reread.debugFunctions) {
        if (df.functionName == "OnInit") hasOnInit = true;
    }
    CHECK(hasOnInit);
}

TEST_CASE("CodeGen debug line numbers are recorded one per instruction") {
    Fixture f;
    f.writeWithScriptObject();
    f.write("Lines", "Scriptname Lines\nFunction Go()\n    int a = 1\n    int b = 2\nEndFunction\n");
    const auto result = f.compile("Lines");

    REQUIRE(result.pex != nullptr);
    const auto* debug = static_cast<const void*>(nullptr);
    for (const auto& df : result.pex->debugFunctions) {
        if (df.functionName == "Go") debug = &df;
    }
    REQUIRE(debug != nullptr);
    const auto* df = static_cast<const fo4ide::papyrus::PexDebugFunction*>(debug);
    CHECK(df->lineNumbers == std::vector<std::uint16_t>{3, 4});
}

TEST_CASE("CodeGen a debug only call is dropped only when asked for") {
    Fixture f;
    f.writeWithScriptObject();
    f.write("Logger",
            "Scriptname Logger Native DebugOnly Hidden\nFunction Say(string asText) Global\n"
            "EndFunction\n");
    f.write("Talker", "Scriptname Talker\nFunction Go()\n    Logger.Say(\"hello\")\nEndFunction\n");

    const auto kept = f.compile("Talker", /*emitDebugOnly=*/true);
    REQUIRE(kept.success);
    REQUIRE(kept.pex != nullptr);
    CHECK(listing(function(kept.pex->objects[0], "Go")) ==
          std::vector<std::string>{"callstatic Logger Say ::nonevar \"hello\""});

    const auto stripped = f.compile("Talker", /*emitDebugOnly=*/false);
    REQUIRE(stripped.success);
    REQUIRE(stripped.pex != nullptr);
    CHECK(listing(function(stripped.pex->objects[0], "Go")).empty());
}
