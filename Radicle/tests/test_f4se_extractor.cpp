#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <fo4/f4se/cpp_scanner.hpp>
#include <fo4/f4se/registration_extractor.hpp>

namespace {

using fo4::f4se::CppScanner;
using fo4::f4se::F4SEModuleSchema;
using fo4::f4se::RegistrationExtractor;

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

}

TEST(F4seExtractor, TheCompactStyleIsRecovered) {
    const auto schema = extractor.extract(R"(
        void papyrusActor::RegisterFuncs(VirtualMachine* vm)
        {
            vm->RegisterFunction(
                new NativeFunction2<Actor, WornItem, UInt32, bool>("GetWornItem", "Actor", papyrusActor::GetWornItem, vm));
        }
    )");
    EXPECT_TRUE(schema.problems.empty());
    ASSERT_TRUE(schema.natives.size() == 1);
    const auto& fn = schema.natives[0];
    EXPECT_TRUE(fn.functionName == "GetWornItem");
    EXPECT_TRUE(fn.papyrusClass == "Actor");
    EXPECT_TRUE(fn.cppBaseType == "Actor");
    EXPECT_TRUE(fn.cppFunctionName == "papyrusActor::GetWornItem");
    EXPECT_TRUE(fn.arity() == 2);
    EXPECT_FALSE(fn.isGlobal());
    EXPECT_FALSE(fn.isLatent);
}

TEST(F4seExtractor, TheSpacedStyleWithAStaticTagIsRecovered) {
    const auto schema = extractor.extract(R"(
        vm->RegisterFunction(
            new NativeFunction0 <StaticFunctionTag, TESObjectREFR*>("GetCurrentConsoleRef", "Game", papyrusGame::GetCurrentConsoleRef, vm));
    )");
    EXPECT_TRUE(schema.problems.empty());
    ASSERT_TRUE(schema.natives.size() == 1);
    const auto& fn = schema.natives[0];
    EXPECT_TRUE(fn.functionName == "GetCurrentConsoleRef");
    EXPECT_TRUE(fn.isGlobal());
    EXPECT_TRUE(fn.returnType.toString() == "ObjectReference");
    EXPECT_TRUE(fn.arity() == 0);
}

TEST(F4seExtractor, ARegistrationSplitAcrossLinesIsRecovered) {
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
    EXPECT_TRUE(schema.problems.empty());
    ASSERT_TRUE(schema.natives.size() == 1);
    EXPECT_TRUE(schema.natives[0].functionName == "HasKeyword");
}

TEST(F4seExtractor, ANestedTemplateArgumentIsSplitCorrectly) {
    const auto schema = extractor.extract(R"(
        vm->RegisterFunction(new NativeFunction1<Actor, VMArray<BGSMod::Attachment::Mod*>, UInt32>("GetWornItemMods", "Actor", papyrusActor::GetWornItemMods, vm));
    )");
    EXPECT_TRUE(schema.problems.empty());
    ASSERT_TRUE(schema.natives.size() == 1);
    const auto& fn = schema.natives[0];
    EXPECT_TRUE(fn.returnType.toString() == "ObjectMod[]");
    EXPECT_TRUE(fn.arity() == 1);
    EXPECT_TRUE(fn.parameters[0].type.toString() == "int");
}

TEST(F4seExtractor, AScopedCppTypeSurvivesIntact) {
    const auto schema = extractor.extract(R"(
        vm->RegisterFunction(new NativeFunction0<BGSMod::Attachment::Mod, BSFixedString>("GetName", "ObjectMod", papyrusObjectMod::GetName, vm));
    )");
    ASSERT_TRUE(schema.natives.size() == 1);
    EXPECT_TRUE(schema.natives[0].cppBaseType == "BGSMod::Attachment::Mod");
}

TEST(F4seExtractor, APointerWithASpaceBeforeTheStarIsRecovered) {
    const auto schema = extractor.extract(R"(
        vm->RegisterFunction(new NativeFunction0<StaticFunctionTag, TESObjectMISC *>("Get", "Thing", papyrusThing::Get, vm));
    )");
    EXPECT_TRUE(schema.problems.empty());
    ASSERT_TRUE(schema.natives.size() == 1);
    EXPECT_TRUE(schema.natives[0].returnType.toString() == "MiscObject");
}

TEST(F4seExtractor, ALatentRegistrationIsMarkedLatent) {
    const auto schema = extractor.extract(R"(
        vm->RegisterFunction(new LatentNativeFunction3<StaticFunctionTag, bool, BSFixedString, BSFixedString, VMVariable>("Set", "UI", papyrusUI::Set, vm));
    )");
    ASSERT_TRUE(schema.natives.size() == 1);
    const auto& fn = schema.natives[0];
    EXPECT_TRUE(fn.isLatent);
    EXPECT_TRUE(fn.arity() == 3);
}

TEST(F4seExtractor, ANoWaitFlagLandsOnTheFunctionItNames) {
    const auto schema = extractor.extract(R"(
        vm->RegisterFunction(new NativeFunction0<TESObjectREFR, void>("AttachWire", "ObjectReference", papyrusObjectReference::AttachWire, vm));
        vm->RegisterFunction(new NativeFunction0<TESObjectREFR, void>("Other", "ObjectReference", papyrusObjectReference::Other, vm));
        vm->SetFunctionFlags("ObjectReference", "AttachWire", IFunction::kFunctionFlag_NoWait);
    )");
    EXPECT_TRUE(schema.problems.empty());
    ASSERT_TRUE(schema.natives.size() == 2);
    bool attachNoWait = false;
    bool otherNoWait = false;
    for (const auto& native : schema.natives) {
        if (native.functionName == "AttachWire") attachNoWait = native.noWait;
        if (native.functionName == "Other") otherNoWait = native.noWait;
    }
    EXPECT_TRUE(attachNoWait);
    EXPECT_FALSE(otherNoWait);
}

TEST(F4seExtractor, AFlagNamingAnUnregisteredFunctionIsReportedRatherThanIgnored) {
    const auto schema = extractor.extract(R"(
        vm->SetFunctionFlags("ObjectReference", "NotHere", IFunction::kFunctionFlag_NoWait);
    )");
    ASSERT_TRUE(schema.problems.size() == 1);
    EXPECT_TRUE(contains(schema.problems[0], "does not register"));
}

TEST(F4seExtractor, AStructDeclarationIsRecoveredWithItsOwningScript) {
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
        ASSERT_TRUE(schema.structs.size() == 1);
        EXPECT_TRUE(schema.structs[0].name == entry.name);
        EXPECT_TRUE(schema.structs[0].ownerScript == entry.owner);
        EXPECT_TRUE(schema.structs[0].vmTypeName() == std::string(entry.owner) + "#" + entry.name);
    }
}

TEST(F4seExtractor, ALineCommentedRegistrationIsNotRecovered) {
    const auto schema = extractor.extract(
        "// vm->RegisterFunction(new NativeFunction0<Actor, bool>(\"Ghost\", \"Actor\", "
        "papyrusActor::Ghost, vm));\n"
        "vm->RegisterFunction(new NativeFunction0<Actor, bool>(\"Real\", \"Actor\", "
        "papyrusActor::Real, vm));\n");
    ASSERT_TRUE(schema.natives.size() == 1);
    EXPECT_TRUE(schema.natives[0].functionName == "Real");
}

TEST(F4seExtractor, ABlockCommentedRegistrationIsNotRecovered) {
    const auto schema = extractor.extract(
        "/*\n"
        "vm->RegisterFunction(new NativeFunction0<Actor, bool>(\"Ghost\", \"Actor\", "
        "papyrusActor::Ghost, vm));\n"
        "*/\n"
        "vm->RegisterFunction(new NativeFunction0<Actor, bool>(\"Real\", \"Actor\", "
        "papyrusActor::Real, vm));\n");
    ASSERT_TRUE(schema.natives.size() == 1);
    EXPECT_TRUE(schema.natives[0].functionName == "Real");
}

TEST(F4seExtractor, ARegistrationNamedInsideAStringLiteralIsNotRecovered) {
    const auto schema = extractor.extract(
        "const char * help = \"new NativeFunction0<Actor, bool>(\\\"Fake\\\", \\\"Actor\\\", f, "
        "vm)\";\n");
    EXPECT_TRUE(schema.natives.empty());
}

TEST(F4seExtractor, AnArityThatDisagreesWithItsTypeNameIsRefused) {
    const auto schema = extractor.extract(R"(
        vm->RegisterFunction(new NativeFunction2<Actor, bool, UInt32>("Wrong", "Actor", papyrusActor::Wrong, vm));
    )");
    EXPECT_TRUE(schema.natives.empty());
    ASSERT_TRUE(schema.problems.size() == 1);
    EXPECT_TRUE(contains(schema.problems[0], "carries 1 parameter types"));
}

TEST(F4seExtractor, ARecoveredBindingCarriesTheLineItCameFrom) {
    const auto schema = extractor.extract(
        "// one\n"
        "// two\n"
        "vm->RegisterFunction(new NativeFunction0<Actor, bool>(\"Third\", \"Actor\", "
        "papyrusActor::Third, vm));\n");
    ASSERT_TRUE(schema.natives.size() == 1);
    EXPECT_TRUE(schema.natives[0].sourceLine == 3);
}

TEST(F4seExtractor, BlankingCommentsPreservesEveryOffset) {
    const auto source = std::string("a /* two */ b\n") + "// tail\nc";
    const auto blanked = CppScanner::blankComments(source);
    EXPECT_TRUE(blanked.size() == source.size());
    EXPECT_TRUE(countOf(blanked, "\n") == 2);
    EXPECT_TRUE(blanked.rfind("a ", 0) == 0);
    EXPECT_TRUE(contains(blanked, "b"));
    EXPECT_TRUE(blanked.back() == 'c');
    EXPECT_FALSE(contains(blanked, "two"));
    EXPECT_FALSE(contains(blanked, "tail"));
}

TEST(F4seExtractor, AStructTheSameFileDeclaresIsMappedInSignatures) {
    const auto schema = extractor.extract(R"(
        DECLARE_STRUCT(WornItem, "Actor")
        vm->RegisterFunction(new NativeFunction2<Actor, WornItem, UInt32, bool>("GetWornItem", "Actor", papyrusActor::GetWornItem, vm));
    )");
    EXPECT_TRUE(schema.problems.empty());
    ASSERT_TRUE(schema.natives.size() == 1);
    EXPECT_TRUE(schema.natives[0].returnType.toString() == "WornItem");
}

TEST(F4seExtractor, AStructDeclaredInAnotherTranslationUnitIsMapped) {
    const auto schema = extractor.extract(R"(
        DECLARE_EXTERN_STRUCT(Owner)
        vm->RegisterFunction(new NativeFunction1<StaticFunctionTag, float, Owner>("GetAttackDamage", "InstanceData", papyrusInstanceData::GetAttackDamage, vm));
    )");
    EXPECT_TRUE(schema.problems.empty());
    ASSERT_TRUE(schema.natives.size() == 1);
    EXPECT_TRUE(schema.natives[0].parameters[0].type.toString() == "Owner");
    EXPECT_TRUE(schema.structs.empty());
}

TEST(F4seExtractor, AStructNameSuppliedByTheCallerIsMapped) {
    const auto schema = extractor.extract(
        R"(
        vm->RegisterFunction(new NativeFunction0<StaticFunctionTag, VMArray<PluginInfo>>("GetInstalledPlugins", "Game", papyrusGame::GetInstalledPlugins, vm));
        )",
        "", {"PluginInfo"});
    ASSERT_TRUE(schema.natives.size() == 1);
    EXPECT_TRUE(schema.natives[0].returnType.toString() == "PluginInfo[]");
}

TEST(F4seExtractor, AnUnmappedTypeIsKeptAndMarkedRatherThanDropped) {
    const auto schema = extractor.extract(R"(
        vm->RegisterFunction(new NativeFunction1<Actor, bool, TESObjectTREE*>("Odd", "Actor", papyrusActor::Odd, vm));
    )");
    ASSERT_TRUE(schema.natives.size() == 1);
    EXPECT_TRUE(schema.natives[0].parameters[0].type.name == RegistrationExtractor::kUnknownTypeName);
}

TEST(F4seExtractor, ALessThanThatIsAComparisonDoesNotSwallowTheFile) {
    const auto schema = extractor.extract(R"(
        if (a < b) { doThing(); }
        vm->RegisterFunction(new NativeFunction0<Actor, bool>("Real", "Actor", papyrusActor::Real, vm));
    )");
    ASSERT_TRUE(schema.natives.size() == 1);
    EXPECT_TRUE(schema.natives[0].functionName == "Real");
}
