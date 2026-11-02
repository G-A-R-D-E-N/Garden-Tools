#include <algorithm>
#include <cctype>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/f4se/TypeMap.hpp>

namespace {

using fo4ide::f4se::CppTypeRef;
using fo4ide::f4se::F4SETypeMap;
using fo4ide::f4se::PapyrusTypeText;

F4SETypeMap map({"WornItem", "Owner", "PluginInfo"});

PapyrusTypeText p(std::string name, bool array = false) {
    return PapyrusTypeText{std::move(name), array};
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

// Mirrors the C# BeEquivalentTo: case-insensitive equality.
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

}  // namespace

TEST_CASE("F4SE type map: a signature type parses and formats back") {
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
        REQUIRE(parsed.has_value());
        CHECK(parsed->format() == expected);
    }
}

TEST_CASE("F4SE type map: a type the marshaller cannot carry is refused") {
    const std::vector<std::string> cases = {"", "   ", "TESForm**", "VMArray<"};
    for (const auto& text : cases) {
        CHECK_FALSE(CppTypeRef::tryParse(text).has_value());
    }
}

TEST_CASE("F4SE type map: a papyrus type maps to the cpp type f4se uses") {
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
        INFO("papyrus: " + papyrus);
        REQUIRE(map.tryToCpp(p(papyrus), false, cpp, &refusal));
        CHECK(cpp->format() == expected);
    }
}

TEST_CASE("F4SE type map: int picks its signedness from the caller") {
    std::optional<CppTypeRef> signed_;
    std::optional<CppTypeRef> unsigned_;
    REQUIRE(map.tryToCpp(p("int"), false, signed_, nullptr));
    CHECK(signed_->format() == "SInt32");
    REQUIRE(map.tryToCpp(p("int"), true, unsigned_, nullptr));
    CHECK(unsigned_->format() == "UInt32");
}

TEST_CASE("F4SE type map: an array maps through its element type") {
    std::optional<CppTypeRef> forms;
    std::optional<CppTypeRef> strings;
    std::optional<CppTypeRef> vars;
    REQUIRE(map.tryToCpp(p("Form", true), false, forms, nullptr));
    CHECK(forms->format() == "VMArray<TESForm*>");
    REQUIRE(map.tryToCpp(p("string", true), false, strings, nullptr));
    CHECK(strings->format() == "VMArray<BSFixedString>");
    REQUIRE(map.tryToCpp(p("var", true), false, vars, nullptr));
    CHECK(vars->format() == "VMArray<VMVariable>");
}

TEST_CASE("F4SE type map: a declared struct marshals by value under its own name") {
    std::optional<CppTypeRef> worn;
    std::string refusal;
    REQUIRE(map.tryToCpp(p("WornItem"), false, worn, &refusal));
    CHECK(worn->format() == "WornItem");

    std::optional<CppTypeRef> plugins;
    REQUIRE(map.tryToCpp(p("PluginInfo", true), false, plugins, nullptr));
    CHECK(plugins->format() == "VMArray<PluginInfo>");
}

TEST_CASE("F4SE type map: an undeclared type is refused rather than invented") {
    std::optional<CppTypeRef> cpp;
    std::string refusal;
    CHECK_FALSE(map.tryToCpp(p("SomeModsCustomScript"), false, cpp, &refusal));
    CHECK_FALSE(cpp.has_value());
    CHECK(contains(refusal, "not a marshallable type"));
}

TEST_CASE("F4SE type map: None has no array form") {
    std::optional<CppTypeRef> cpp;
    std::string refusal;
    CHECK_FALSE(map.tryToCpp(p("None", true), false, cpp, &refusal));
    CHECK(contains(refusal, "None"));
}

TEST_CASE("F4SE type map: a cpp type maps back to the papyrus type it carries") {
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
        REQUIRE(parsed.has_value());
        PapyrusTypeText papyrus;
        std::string refusal;
        INFO("cpp: " + cppText);
        REQUIRE(map.tryToPapyrus(&*parsed, papyrus, &refusal));
        CHECK(papyrus.toString() == expected);
    }
}

TEST_CASE("F4SE type map: the receiver tag is not a value type") {
    PapyrusTypeText papyrus;
    std::string refusal;
    CppTypeRef tag(F4SETypeMap::kStaticFunctionTag);
    CHECK_FALSE(map.tryToPapyrus(&tag, papyrus, &refusal));
    CHECK(contains(refusal, "receiver tag"));
}

TEST_CASE("F4SE type map: an unnamed form pointer is refused rather than guessed from its spelling") {
    const auto tree = CppTypeRef::tryParse("TESObjectTREE*");
    REQUIRE(tree.has_value());
    PapyrusTypeText papyrus;
    std::string refusal;
    CHECK_FALSE(map.tryToPapyrus(&*tree, papyrus, &refusal));
    CHECK(contains(refusal, "the form table does not name"));
}

TEST_CASE("F4SE type map: a jagged array has no papyrus type") {
    const auto jagged =
        CppTypeRef::arrayOf(CppTypeRef::arrayOf(CppTypeRef("TESForm", true)));
    PapyrusTypeText papyrus;
    std::string refusal;
    CHECK_FALSE(map.tryToPapyrus(&jagged, papyrus, &refusal));
    CHECK(contains(refusal, "jagged"));
}

TEST_CASE("F4SE type map: papyrus to cpp and back is the identity") {
    const std::vector<std::string> cases = {
        "bool",          "float",          "string",     "var",
        "None",          "int",            "Form",       "ObjectReference",
        "Actor",         "ObjectMod",      "ScriptObject", "WornItem",
    };
    for (const auto& papyrus : cases) {
        std::optional<CppTypeRef> cpp;
        std::string refusal;
        INFO("papyrus: " + papyrus);
        REQUIRE(map.tryToCpp(p(papyrus), false, cpp, &refusal));
        PapyrusTypeText back;
        REQUIRE(map.tryToPapyrus(&*cpp, back, &refusal));
        CHECK(equivalent(back.name, papyrus));
        CHECK_FALSE(back.isArray);
    }
}

TEST_CASE("F4SE type map: an array round trips too") {
    const std::vector<std::string> elements = {"Form", "string", "var", "Keyword", "PluginInfo"};
    for (const auto& element : elements) {
        std::optional<CppTypeRef> cpp;
        std::string refusal;
        INFO("element: " + element);
        REQUIRE(map.tryToCpp(p(element, true), false, cpp, &refusal));
        PapyrusTypeText back;
        REQUIRE(map.tryToPapyrus(&*cpp, back, &refusal));
        CHECK(equivalent(back.name, element));
        CHECK(back.isArray);
    }
}

TEST_CASE("F4SE type map: every form table row maps both ways") {
    for (const auto& [papyrus, cpp] : F4SETypeMap::formTypes()) {
        std::optional<CppTypeRef> forward;
        std::string refusal;
        INFO("papyrus: " + papyrus);
        REQUIRE(map.tryToCpp(p(papyrus), false, forward, &refusal));
        CHECK(forward->name == cpp);

        PapyrusTypeText back;
        REQUIRE(map.tryToPapyrus(&*forward, back, &refusal));
        CHECK(back.name == papyrus);
    }
}

TEST_CASE("F4SE type map: the template instantiation matches the shipped registration shape") {
    const std::vector<CppTypeRef> parameters = {CppTypeRef("UInt32"), CppTypeRef("bool")};
    CHECK(F4SETypeMap::templateInstantiation(false, 2, "Actor", CppTypeRef("WornItem"),
                                             parameters) ==
          "NativeFunction2<Actor, WornItem, UInt32, bool>");
}

TEST_CASE("F4SE type map: a global instantiates against the receiver tag") {
    CHECK(F4SETypeMap::templateInstantiation(false, 0, F4SETypeMap::kStaticFunctionTag,
                                             CppTypeRef("TESObjectREFR", true), {}) ==
          "NativeFunction0<StaticFunctionTag, TESObjectREFR*>");
}

TEST_CASE("F4SE type map: a latent function instantiates the latent template") {
    const std::vector<CppTypeRef> parameters = {
        CppTypeRef("BSFixedString"), CppTypeRef("BSFixedString"), CppTypeRef("VMVariable")};
    CHECK(F4SETypeMap::templateInstantiation(true, 3, F4SETypeMap::kStaticFunctionTag,
                                             CppTypeRef("bool"), parameters) ==
          "LatentNativeFunction3<StaticFunctionTag, bool, BSFixedString, BSFixedString, "
          "VMVariable>");
}

TEST_CASE("F4SE type map: the struct header is included only when a struct is used") {
    auto noStruct = map.requiredIncludesFor({CppTypeRef("bool")});
    CHECK(std::find(noStruct.begin(), noStruct.end(), "f4se/PapyrusStruct.h") == noStruct.end());

    auto withStruct = map.requiredIncludesFor({CppTypeRef("WornItem")});
    CHECK(std::find(withStruct.begin(), withStruct.end(), "f4se/PapyrusStruct.h") !=
          withStruct.end());

    auto arrayStruct = map.requiredIncludesFor({CppTypeRef::arrayOf(CppTypeRef("PluginInfo"))});
    CHECK(std::find(arrayStruct.begin(), arrayStruct.end(), "f4se/PapyrusStruct.h") !=
          arrayStruct.end());
}

TEST_CASE("F4SE type map: game headers come in only when a form pointer is used") {
    auto noForm = map.requiredIncludesFor({CppTypeRef("float")});
    CHECK(std::find(noForm.begin(), noForm.end(), "f4se/GameForms.h") == noForm.end());

    auto withForm = map.requiredIncludesFor({CppTypeRef("TESForm", true)});
    CHECK(std::find(withForm.begin(), withForm.end(), "f4se/GameForms.h") != withForm.end());
}

TEST_CASE("F4SE type map: the core includes are always present and come first") {
    const auto includes = map.requiredIncludesFor({CppTypeRef("TESForm", true)});
    const auto& core = F4SETypeMap::coreIncludes();
    REQUIRE(includes.size() >= core.size());
    for (std::size_t i = 0; i < core.size(); i++) {
        CHECK(includes[i] == core[i]);
    }
}
