#include <algorithm>
#include <cctype>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <fo4/f4se/type_map.hpp>

namespace {

using fo4::f4se::CppTypeRef;
using fo4::f4se::F4SETypeMap;
using fo4::f4se::PapyrusTypeText;

F4SETypeMap map({"WornItem", "Owner", "PluginInfo"});

PapyrusTypeText p(std::string name, bool array = false) {
    return PapyrusTypeText{std::move(name), array};
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

bool equivalent(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); i++) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

}

TEST(F4seTypeMap, ASignatureTypeParsesAndFormatsBack) {
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"bool", "bool"},
        {"TESForm*", "TESForm*"},
        {"TESObjectMISC *", "TESObjectMISC*"},
        {"  BSFixedString  ", "BSFixedString"},
        {"VMArray<TESForm*>", "VMArray<TESForm*>"},
        {"VMArray< BGSKeyword* >", "VMArray<BGSKeyword*>"},
        {"BGSMod::Attachment::Mod*", "BGSMod::Attachment::Mod*"},
        {"VMArray<BGSMod::Attachment::Mod*>", "VMArray<BGSMod::Attachment::Mod*>"},
    };
    for (const auto& [text, expected] : cases) {
        const auto parsed = CppTypeRef::tryParse(text);
        ASSERT_TRUE(parsed.has_value());
        EXPECT_TRUE(parsed->format() == expected);
    }
}

TEST(F4seTypeMap, ATypeTheMarshallerCannotCarryIsRefused) {
    const std::vector<std::string> cases = {"", "   ", "TESForm**", "VMArray<"};
    for (const auto& text : cases) {
        EXPECT_FALSE(CppTypeRef::tryParse(text).has_value());
    }
}

TEST(F4seTypeMap, APapyrusTypeMapsToTheCppTypeF4seUses) {
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"bool", "bool"},
        {"float", "float"},
        {"string", "BSFixedString"},
        {"var", "VMVariable"},
        {"None", "void"},
        {"Form", "TESForm*"},
        {"ObjectReference", "TESObjectREFR*"},
        {"Actor", "Actor*"},
        {"ActorBase", "TESNPC*"},
        {"ActorValue", "ActorValueInfo*"},
        {"Keyword", "BGSKeyword*"},
        {"FormList", "BGSListForm*"},
        {"GlobalVariable", "TESGlobal*"},
        {"ObjectMod", "BGSMod::Attachment::Mod*"},
        {"MatSwap", "BGSMaterialSwap*"},
        {"MiscObject", "TESObjectMISC*"},
        {"Spell", "SpellItem*"},
        {"Enchantment", "EnchantmentItem*"},
        {"LeveledItem", "TESLevItem*"},
        {"WaterType", "TESWaterForm*"},
        {"ScriptObject", "VMObject"},
    };
    for (const auto& [papyrus, expected] : cases) {
        std::optional<CppTypeRef> cpp;
        std::string refusal;
        SCOPED_TRACE("papyrus: " + papyrus);
        ASSERT_TRUE(map.tryToCpp(p(papyrus), false, cpp, &refusal));
        EXPECT_TRUE(cpp->format() == expected);
    }
}

TEST(F4seTypeMap, IntPicksItsSignednessFromTheCaller) {
    std::optional<CppTypeRef> signed_;
    std::optional<CppTypeRef> unsigned_;
    ASSERT_TRUE(map.tryToCpp(p("int"), false, signed_, nullptr));
    EXPECT_TRUE(signed_->format() == "SInt32");
    ASSERT_TRUE(map.tryToCpp(p("int"), true, unsigned_, nullptr));
    EXPECT_TRUE(unsigned_->format() == "UInt32");
}

TEST(F4seTypeMap, AnArrayMapsThroughItsElementType) {
    std::optional<CppTypeRef> forms;
    std::optional<CppTypeRef> strings;
    std::optional<CppTypeRef> vars;
    ASSERT_TRUE(map.tryToCpp(p("Form", true), false, forms, nullptr));
    EXPECT_TRUE(forms->format() == "VMArray<TESForm*>");
    ASSERT_TRUE(map.tryToCpp(p("string", true), false, strings, nullptr));
    EXPECT_TRUE(strings->format() == "VMArray<BSFixedString>");
    ASSERT_TRUE(map.tryToCpp(p("var", true), false, vars, nullptr));
    EXPECT_TRUE(vars->format() == "VMArray<VMVariable>");
}

TEST(F4seTypeMap, ADeclaredStructMarshalsByValueUnderItsOwnName) {
    std::optional<CppTypeRef> worn;
    std::string refusal;
    ASSERT_TRUE(map.tryToCpp(p("WornItem"), false, worn, &refusal));
    EXPECT_TRUE(worn->format() == "WornItem");

    std::optional<CppTypeRef> plugins;
    ASSERT_TRUE(map.tryToCpp(p("PluginInfo", true), false, plugins, nullptr));
    EXPECT_TRUE(plugins->format() == "VMArray<PluginInfo>");
}

TEST(F4seTypeMap, AnUndeclaredTypeIsRefusedRatherThanInvented) {
    std::optional<CppTypeRef> cpp;
    std::string refusal;
    EXPECT_FALSE(map.tryToCpp(p("SomeModsCustomScript"), false, cpp, &refusal));
    EXPECT_FALSE(cpp.has_value());
    EXPECT_TRUE(contains(refusal, "not a marshallable type"));
}

TEST(F4seTypeMap, NoneHasNoArrayForm) {
    std::optional<CppTypeRef> cpp;
    std::string refusal;
    EXPECT_FALSE(map.tryToCpp(p("None", true), false, cpp, &refusal));
    EXPECT_TRUE(contains(refusal, "None"));
}

TEST(F4seTypeMap, ACppTypeMapsBackToThePapyrusTypeItCarries) {
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"bool", "bool"},
        {"float", "float"},
        {"BSFixedString", "string"},
        {"VMVariable", "Var"},
        {"void", "None"},
        {"SInt32", "int"},
        {"UInt32", "int"},
        {"int", "int"},
        {"TESForm*", "Form"},
        {"TESObjectREFR*", "ObjectReference"},
        {"VMRefOrInventoryObj", "ObjectReference"},
        {"VMObject", "ScriptObject"},
        {"BGSMod::Attachment::Mod*", "ObjectMod"},
        {"TESObjectMISC *", "MiscObject"},
        {"VMArray<TESForm*>", "Form[]"},
        {"VMArray<BSFixedString>", "string[]"},
        {"VMArray<VMVariable>", "Var[]"},
    };
    for (const auto& [cppText, expected] : cases) {
        const auto parsed = CppTypeRef::tryParse(cppText);
        ASSERT_TRUE(parsed.has_value());
        PapyrusTypeText papyrus;
        std::string refusal;
        SCOPED_TRACE("cpp: " + cppText);
        ASSERT_TRUE(map.tryToPapyrus(&*parsed, papyrus, &refusal));
        EXPECT_TRUE(papyrus.toString() == expected);
    }
}

TEST(F4seTypeMap, TheReceiverTagIsNotAValueType) {
    PapyrusTypeText papyrus;
    std::string refusal;
    CppTypeRef tag(F4SETypeMap::kStaticFunctionTag);
    EXPECT_FALSE(map.tryToPapyrus(&tag, papyrus, &refusal));
    EXPECT_TRUE(contains(refusal, "receiver tag"));
}

TEST(F4seTypeMap, AnUnnamedFormPointerIsRefusedRatherThanGuessedFromItsSpelling) {
    const auto tree = CppTypeRef::tryParse("TESObjectTREE*");
    ASSERT_TRUE(tree.has_value());
    PapyrusTypeText papyrus;
    std::string refusal;
    EXPECT_FALSE(map.tryToPapyrus(&*tree, papyrus, &refusal));
    EXPECT_TRUE(contains(refusal, "the form table does not name"));
}

TEST(F4seTypeMap, AJaggedArrayHasNoPapyrusType) {
    const auto jagged =
        CppTypeRef::arrayOf(CppTypeRef::arrayOf(CppTypeRef("TESForm", true)));
    PapyrusTypeText papyrus;
    std::string refusal;
    EXPECT_FALSE(map.tryToPapyrus(&jagged, papyrus, &refusal));
    EXPECT_TRUE(contains(refusal, "jagged"));
}

TEST(F4seTypeMap, PapyrusToCppAndBackIsTheIdentity) {
    const std::vector<std::string> cases = {
        "bool",          "float",          "string",     "var",
        "None",          "int",            "Form",       "ObjectReference",
        "Actor",         "ObjectMod",      "ScriptObject", "WornItem",
    };
    for (const auto& papyrus : cases) {
        std::optional<CppTypeRef> cpp;
        std::string refusal;
        SCOPED_TRACE("papyrus: " + papyrus);
        ASSERT_TRUE(map.tryToCpp(p(papyrus), false, cpp, &refusal));
        PapyrusTypeText back;
        ASSERT_TRUE(map.tryToPapyrus(&*cpp, back, &refusal));
        EXPECT_TRUE(equivalent(back.name, papyrus));
        EXPECT_FALSE(back.isArray);
    }
}

TEST(F4seTypeMap, AnArrayRoundTripsToo) {
    const std::vector<std::string> elements = {"Form", "string", "var", "Keyword", "PluginInfo"};
    for (const auto& element : elements) {
        std::optional<CppTypeRef> cpp;
        std::string refusal;
        SCOPED_TRACE("element: " + element);
        ASSERT_TRUE(map.tryToCpp(p(element, true), false, cpp, &refusal));
        PapyrusTypeText back;
        ASSERT_TRUE(map.tryToPapyrus(&*cpp, back, &refusal));
        EXPECT_TRUE(equivalent(back.name, element));
        EXPECT_TRUE(back.isArray);
    }
}

TEST(F4seTypeMap, EveryFormTableRowMapsBothWays) {
    for (const auto& [papyrus, cpp] : F4SETypeMap::formTypes()) {
        std::optional<CppTypeRef> forward;
        std::string refusal;
        SCOPED_TRACE("papyrus: " + papyrus);
        ASSERT_TRUE(map.tryToCpp(p(papyrus), false, forward, &refusal));
        EXPECT_TRUE(forward->name == cpp);

        PapyrusTypeText back;
        ASSERT_TRUE(map.tryToPapyrus(&*forward, back, &refusal));
        EXPECT_TRUE(back.name == papyrus);
    }
}

TEST(F4seTypeMap, TheTemplateInstantiationMatchesTheShippedRegistrationShape) {
    const std::vector<CppTypeRef> parameters = {CppTypeRef("UInt32"), CppTypeRef("bool")};
    EXPECT_TRUE(F4SETypeMap::templateInstantiation(false, 2, "Actor", CppTypeRef("WornItem"),
                                             parameters) ==
          "NativeFunction2<Actor, WornItem, UInt32, bool>");
}

TEST(F4seTypeMap, AGlobalInstantiatesAgainstTheReceiverTag) {
    EXPECT_TRUE(F4SETypeMap::templateInstantiation(false, 0, F4SETypeMap::kStaticFunctionTag,
                                             CppTypeRef("TESObjectREFR", true), {}) ==
          "NativeFunction0<StaticFunctionTag, TESObjectREFR*>");
}

TEST(F4seTypeMap, ALatentFunctionInstantiatesTheLatentTemplate) {
    const std::vector<CppTypeRef> parameters = {
        CppTypeRef("BSFixedString"), CppTypeRef("BSFixedString"), CppTypeRef("VMVariable")};
    EXPECT_TRUE(F4SETypeMap::templateInstantiation(true, 3, F4SETypeMap::kStaticFunctionTag,
                                             CppTypeRef("bool"), parameters) ==
          "LatentNativeFunction3<StaticFunctionTag, bool, BSFixedString, BSFixedString, "
          "VMVariable>");
}

TEST(F4seTypeMap, TheStructHeaderIsIncludedOnlyWhenAStructIsUsed) {
    auto noStruct = map.requiredIncludesFor({CppTypeRef("bool")});
    EXPECT_TRUE(std::find(noStruct.begin(), noStruct.end(), "f4se/PapyrusStruct.h") == noStruct.end());

    auto withStruct = map.requiredIncludesFor({CppTypeRef("WornItem")});
    EXPECT_TRUE(std::find(withStruct.begin(), withStruct.end(), "f4se/PapyrusStruct.h") !=
          withStruct.end());

    auto arrayStruct = map.requiredIncludesFor({CppTypeRef::arrayOf(CppTypeRef("PluginInfo"))});
    EXPECT_TRUE(std::find(arrayStruct.begin(), arrayStruct.end(), "f4se/PapyrusStruct.h") !=
          arrayStruct.end());
}

TEST(F4seTypeMap, GameHeadersComeInOnlyWhenAFormPointerIsUsed) {
    auto noForm = map.requiredIncludesFor({CppTypeRef("float")});
    EXPECT_TRUE(std::find(noForm.begin(), noForm.end(), "f4se/GameForms.h") == noForm.end());

    auto withForm = map.requiredIncludesFor({CppTypeRef("TESForm", true)});
    EXPECT_TRUE(std::find(withForm.begin(), withForm.end(), "f4se/GameForms.h") != withForm.end());
}

TEST(F4seTypeMap, TheCoreIncludesAreAlwaysPresentAndComeFirst) {
    const auto includes = map.requiredIncludesFor({CppTypeRef("TESForm", true)});
    const auto& core = F4SETypeMap::coreIncludes();
    ASSERT_TRUE(includes.size() >= core.size());
    for (std::size_t i = 0; i < core.size(); i++) {
        EXPECT_TRUE(includes[i] == core[i]);
    }
}
