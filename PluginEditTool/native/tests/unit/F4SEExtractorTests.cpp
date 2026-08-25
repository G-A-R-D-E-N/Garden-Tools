#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/f4se/CppScanner.hpp>
#include <fo4ide/f4se/RegistrationExtractor.hpp>

namespace {

using fo4ide::f4se::CppScanner;
using fo4ide::f4se::F4SEModuleSchema;
using fo4ide::f4se::RegistrationExtractor;

RegistrationExtractor extractor;

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

std::size_t countOf(const std::string& text, const std::string& needle) {
    std::size_t count = 0;
    std::size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::string::npos) {
        count++;
        pos += needle.size();
    }
    return count;
}

}  // namespace

TEST_CASE("F4SE extractor: the compact style is recovered") {
    const auto schema = extractor.extract(R"(
        void papyrusActor::RegisterFuncs(VirtualMachine* vm)
        {
            vm->RegisterFunction(
                new NativeFunction2<Actor, WornItem, UInt32, bool>("GetWornItem", "Actor", papyrusActor::GetWornItem, vm));
        }
    )");
    CHECK(schema.problems.empty());
    REQUIRE(schema.natives.size() == 1);
    const auto& fn = schema.natives[0];
    CHECK(fn.functionName == "GetWornItem");
    CHECK(fn.papyrusClass == "Actor");
    CHECK(fn.cppBaseType == "Actor");
    CHECK(fn.cppFunctionName == "papyrusActor::GetWornItem");
    CHECK(fn.arity() == 2);
    CHECK_FALSE(fn.isGlobal());
    CHECK_FALSE(fn.isLatent);
}

TEST_CASE("F4SE extractor: the spaced style with a static tag is recovered") {
    const auto schema = extractor.extract(R"(
        vm->RegisterFunction(
            new NativeFunction0 <StaticFunctionTag, TESObjectREFR*>("GetCurrentConsoleRef", "Game", papyrusGame::GetCurrentConsoleRef, vm));
    )");
    CHECK(schema.problems.empty());
    REQUIRE(schema.natives.size() == 1);
    const auto& fn = schema.natives[0];
    CHECK(fn.functionName == "GetCurrentConsoleRef");
    CHECK(fn.isGlobal());
    CHECK(fn.returnType.toString() == "ObjectReference");
    CHECK(fn.arity() == 0);
}

TEST_CASE("F4SE extractor: a registration split across lines is recovered") {
    const auto schema = extractor.extract(R"(
        vm->RegisterFunction(
            new NativeFunction1<
                TESForm,
                bool,
                BGSKeyword*
            >(
                "HasKeyword",
                "Form",
                papyrusForm::HasKeyword,
                vm));
    )");
    CHECK(schema.problems.empty());
    REQUIRE(schema.natives.size() == 1);
    CHECK(schema.natives[0].functionName == "HasKeyword");
}

TEST_CASE("F4SE extractor: a nested template argument is split correctly") {
    const auto schema = extractor.extract(R"(
        vm->RegisterFunction(new NativeFunction1<Actor, VMArray<BGSMod::Attachment::Mod*>, UInt32>("GetWornItemMods", "Actor", papyrusActor::GetWornItemMods, vm));
    )");
    CHECK(schema.problems.empty());
    REQUIRE(schema.natives.size() == 1);
    const auto& fn = schema.natives[0];
    CHECK(fn.returnType.toString() == "ObjectMod[]");
    CHECK(fn.arity() == 1);
    CHECK(fn.parameters[0].type.toString() == "int");
}

TEST_CASE("F4SE extractor: a scoped cpp type survives intact") {
    const auto schema = extractor.extract(R"(
        vm->RegisterFunction(new NativeFunction0<BGSMod::Attachment::Mod, BSFixedString>("GetName", "ObjectMod", papyrusObjectMod::GetName, vm));
    )");
    REQUIRE(schema.natives.size() == 1);
    CHECK(schema.natives[0].cppBaseType == "BGSMod::Attachment::Mod");
}

TEST_CASE("F4SE extractor: a pointer with a space before the star is recovered") {
    const auto schema = extractor.extract(R"(
        vm->RegisterFunction(new NativeFunction0<StaticFunctionTag, TESObjectMISC *>("Get", "Thing", papyrusThing::Get, vm));
    )");
    CHECK(schema.problems.empty());
    REQUIRE(schema.natives.size() == 1);
    CHECK(schema.natives[0].returnType.toString() == "MiscObject");
}

TEST_CASE("F4SE extractor: a latent registration is marked latent") {
    const auto schema = extractor.extract(R"(
        vm->RegisterFunction(new LatentNativeFunction3<StaticFunctionTag, bool, BSFixedString, BSFixedString, VMVariable>("Set", "UI", papyrusUI::Set, vm));
    )");
    REQUIRE(schema.natives.size() == 1);
    const auto& fn = schema.natives[0];
    CHECK(fn.isLatent);
    CHECK(fn.arity() == 3);
}

TEST_CASE("F4SE extractor: a no wait flag lands on the function it names") {
    const auto schema = extractor.extract(R"(
        vm->RegisterFunction(new NativeFunction0<TESObjectREFR, void>("AttachWire", "ObjectReference", papyrusObjectReference::AttachWire, vm));
        vm->RegisterFunction(new NativeFunction0<TESObjectREFR, void>("Other", "ObjectReference", papyrusObjectReference::Other, vm));
        vm->SetFunctionFlags("ObjectReference", "AttachWire", IFunction::kFunctionFlag_NoWait);
    )");
    CHECK(schema.problems.empty());
    REQUIRE(schema.natives.size() == 2);
    bool attachNoWait = false;
    bool otherNoWait = false;
    for (const auto& native : schema.natives) {
        if (native.functionName == "AttachWire") attachNoWait = native.noWait;
        if (native.functionName == "Other") otherNoWait = native.noWait;
    }
    CHECK(attachNoWait);
    CHECK_FALSE(otherNoWait);
}

TEST_CASE("F4SE extractor: a flag naming an unregistered function is reported rather than ignored") {
    const auto schema = extractor.extract(R"(
        vm->SetFunctionFlags("ObjectReference", "NotHere", IFunction::kFunctionFlag_NoWait);
    )");
    REQUIRE(schema.problems.size() == 1);
    CHECK(contains(schema.problems[0], "does not register"));
}

TEST_CASE("F4SE extractor: a struct declaration is recovered with its owning script") {
    const struct {
        const char* source;
        const char* name;
        const char* owner;
    } cases[] = {
        {"DECLARE_STRUCT(WornItem, \"Actor\")", "WornItem", "Actor"},
        {"\tDECLARE_STRUCT(Owner, \"InstanceData\")", "Owner", "InstanceData"},
        {"DECLARE_STRUCT(RemapData, \"MatSwap\");", "RemapData", "MatSwap"},
    };
    for (const auto& entry : cases) {
        const auto schema = extractor.extract(entry.source);
        REQUIRE(schema.structs.size() == 1);
        CHECK(schema.structs[0].name == entry.name);
        CHECK(schema.structs[0].ownerScript == entry.owner);
        CHECK(schema.structs[0].vmTypeName() == std::string(entry.owner) + "#" + entry.name);
    }
}

TEST_CASE("F4SE extractor: a line commented registration is not recovered") {
    const auto schema = extractor.extract(
        "// vm->RegisterFunction(new NativeFunction0<Actor, bool>(\"Ghost\", \"Actor\", "
        "papyrusActor::Ghost, vm));\n"
        "vm->RegisterFunction(new NativeFunction0<Actor, bool>(\"Real\", \"Actor\", "
        "papyrusActor::Real, vm));\n");
    REQUIRE(schema.natives.size() == 1);
    CHECK(schema.natives[0].functionName == "Real");
}

TEST_CASE("F4SE extractor: a block commented registration is not recovered") {
    const auto schema = extractor.extract(
        "/*\n"
        "vm->RegisterFunction(new NativeFunction0<Actor, bool>(\"Ghost\", \"Actor\", "
        "papyrusActor::Ghost, vm));\n"
        "*/\n"
        "vm->RegisterFunction(new NativeFunction0<Actor, bool>(\"Real\", \"Actor\", "
        "papyrusActor::Real, vm));\n");
    REQUIRE(schema.natives.size() == 1);
    CHECK(schema.natives[0].functionName == "Real");
}

TEST_CASE("F4SE extractor: a registration named inside a string literal is not recovered") {
    const auto schema = extractor.extract(
        "const char * help = \"new NativeFunction0<Actor, bool>(\\\"Fake\\\", \\\"Actor\\\", f, "
        "vm)\";\n");
    CHECK(schema.natives.empty());
}

TEST_CASE("F4SE extractor: an arity that disagrees with its type name is refused") {
    const auto schema = extractor.extract(R"(
        vm->RegisterFunction(new NativeFunction2<Actor, bool, UInt32>("Wrong", "Actor", papyrusActor::Wrong, vm));
    )");
    CHECK(schema.natives.empty());
    REQUIRE(schema.problems.size() == 1);
    CHECK(contains(schema.problems[0], "carries 1 parameter types"));
}

TEST_CASE("F4SE extractor: a recovered binding carries the line it came from") {
    const auto schema = extractor.extract(
        "// one\n"
        "// two\n"
        "vm->RegisterFunction(new NativeFunction0<Actor, bool>(\"Third\", \"Actor\", "
        "papyrusActor::Third, vm));\n");
    REQUIRE(schema.natives.size() == 1);
    CHECK(schema.natives[0].sourceLine == 3);
}

TEST_CASE("F4SE extractor: blanking comments preserves every offset") {
    const auto source = std::string("a /* two */ b\n") + "// tail\nc";
    const auto blanked = CppScanner::blankComments(source);
    CHECK(blanked.size() == source.size());
    CHECK(countOf(blanked, "\n") == 2);
    CHECK(blanked.rfind("a ", 0) == 0);
    CHECK(contains(blanked, "b"));
    CHECK(blanked.back() == 'c');
    CHECK_FALSE(contains(blanked, "two"));
    CHECK_FALSE(contains(blanked, "tail"));
}

TEST_CASE("F4SE extractor: a struct the same file declares is mapped in signatures") {
    const auto schema = extractor.extract(R"(
        DECLARE_STRUCT(WornItem, "Actor")
        vm->RegisterFunction(new NativeFunction2<Actor, WornItem, UInt32, bool>("GetWornItem", "Actor", papyrusActor::GetWornItem, vm));
    )");
    CHECK(schema.problems.empty());
    REQUIRE(schema.natives.size() == 1);
    CHECK(schema.natives[0].returnType.toString() == "WornItem");
}

TEST_CASE("F4SE extractor: a struct declared in another translation unit is mapped") {
    const auto schema = extractor.extract(R"(
        DECLARE_EXTERN_STRUCT(Owner)
        vm->RegisterFunction(new NativeFunction1<StaticFunctionTag, float, Owner>("GetAttackDamage", "InstanceData", papyrusInstanceData::GetAttackDamage, vm));
    )");
    CHECK(schema.problems.empty());
    REQUIRE(schema.natives.size() == 1);
    CHECK(schema.natives[0].parameters[0].type.toString() == "Owner");
    CHECK(schema.structs.empty());
}

TEST_CASE("F4SE extractor: a struct name supplied by the caller is mapped") {
    const auto schema = extractor.extract(
        R"(
        vm->RegisterFunction(new NativeFunction0<StaticFunctionTag, VMArray<PluginInfo>>("GetInstalledPlugins", "Game", papyrusGame::GetInstalledPlugins, vm));
        )",
        "", {"PluginInfo"});
    REQUIRE(schema.natives.size() == 1);
    CHECK(schema.natives[0].returnType.toString() == "PluginInfo[]");
}

TEST_CASE("F4SE extractor: an unmapped type is kept and marked rather than dropped") {
    const auto schema = extractor.extract(R"(
        vm->RegisterFunction(new NativeFunction1<Actor, bool, TESObjectTREE*>("Odd", "Actor", papyrusActor::Odd, vm));
    )");
    REQUIRE(schema.natives.size() == 1);
    CHECK(schema.natives[0].parameters[0].type.name == RegistrationExtractor::kUnknownTypeName);
}

TEST_CASE("F4SE extractor: a less than that is a comparison does not swallow the file") {
    const auto schema = extractor.extract(R"(
        if (a < b) { doThing(); }
        vm->RegisterFunction(new NativeFunction0<Actor, bool>("Real", "Actor", papyrusActor::Real, vm));
    )");
    REQUIRE(schema.natives.size() == 1);
    CHECK(schema.natives[0].functionName == "Real");
}
