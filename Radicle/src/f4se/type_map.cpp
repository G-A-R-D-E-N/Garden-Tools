#include <fo4/f4se/type_map.hpp>

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace fo4::f4se {

int CiLess::icompare(std::string_view a, std::string_view b) {
    const auto n = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < n; ++i) {
        const auto ca = static_cast<unsigned char>(a[i]);
        const auto cb = static_cast<unsigned char>(b[i]);
        const auto la = std::tolower(ca);
        const auto lb = std::tolower(cb);
        if (la != lb) return la < lb ? -1 : 1;
    }
    if (a.size() == b.size()) return 0;
    return a.size() < b.size() ? -1 : 1;
}

namespace {

std::string_view trimView(std::string_view text) {
    while (!text.empty() &&
           std::isspace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.remove_suffix(1);
    }
    return text;
}

bool startsWith(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

struct FormRow {
    const char* papyrus;
    const char* cpp;
    bool pointer;
};

const std::vector<FormRow>& formTypeRows() {
    static const std::vector<FormRow> rows = {
        {"Form", "TESForm", true},
        {"ObjectReference", "TESObjectREFR", true},
        {"Actor", "Actor", true},
        {"ActorBase", "TESNPC", true},
        {"ActorValue", "ActorValueInfo", true},
        {"Ammo", "TESAmmo", true},
        {"Armor", "TESObjectARMO", true},
        {"ArmorAddon", "TESObjectARMA", true},
        {"Cell", "TESObjectCELL", true},
        {"Component", "BGSComponent", true},
        {"ConstructibleObject", "BGSConstructibleObject", true},
        {"DefaultObject", "BGSDefaultObject", true},
        {"Enchantment", "EnchantmentItem", true},
        {"EncounterZone", "BGSEncounterZone", true},
        {"EquipSlot", "BGSEquipSlot", true},
        {"FormList", "BGSListForm", true},
        {"GlobalVariable", "TESGlobal", true},
        {"HeadPart", "BGSHeadPart", true},
        {"Keyword", "BGSKeyword", true},
        {"LeveledItem", "TESLevItem", true},
        {"Location", "BGSLocation", true},
        {"MatSwap", "BGSMaterialSwap", true},
        {"MiscObject", "TESObjectMISC", true},
        {"ObjectMod", "BGSMod::Attachment::Mod", true},
        {"Outfit", "BGSOutfit", true},
        {"Perk", "BGSPerk", true},
        {"Projectile", "BGSProjectile", true},
        {"Race", "TESRace", true},
        {"Spell", "SpellItem", true},
        {"WaterType", "TESWaterForm", true},
        {"Weapon", "TESObjectWEAP", true},
        {"ScriptObject", "VMObject", false},
    };
    return rows;
}

const std::vector<std::pair<const char*, const char*>>& reverseOnlyRows() {
    static const std::vector<std::pair<const char*, const char*>> rows = {
        {"VMRefOrInventoryObj", "ObjectReference"},
        {"UInt32", "int"},
        {"int", "int"},
    };
    return rows;
}

std::map<std::string, std::pair<std::string, bool>, CiLess> buildByPapyrus() {
    std::map<std::string, std::pair<std::string, bool>, CiLess> map;
    map["bool"] = {"bool", false};
    map["float"] = {"float", false};
    map["string"] = {"BSFixedString", false};
    map["var"] = {"VMVariable", false};
    map["none"] = {"void", false};
    for (const auto& row : formTypeRows()) map[row.papyrus] = {row.cpp, row.pointer};
    return map;
}

std::map<std::string, std::string> buildByCpp() {
    std::map<std::string, std::string> map;
    map["bool"] = "bool";
    map["float"] = "float";
    map["BSFixedString"] = "string";
    map["VMVariable"] = "Var";
    map["void"] = "None";
    map["SInt32"] = "int";
    for (const auto& row : formTypeRows()) map[row.cpp] = row.papyrus;
    for (const auto& [cpp, papyrus] : reverseOnlyRows()) map[cpp] = papyrus;
    return map;
}

const std::map<std::string, std::pair<std::string, bool>, CiLess>& byPapyrus() {
    static const auto map = buildByPapyrus();
    return map;
}

const std::map<std::string, std::string>& byCpp() {
    static const auto map = buildByCpp();
    return map;
}

}

std::optional<CppTypeRef> CppTypeRef::tryParse(std::string_view text) {
    auto trimmed = trimView(text);
    if (trimmed.empty()) return std::nullopt;

    if (startsWith(trimmed, kArrayTemplate)) {
        const auto open = trimmed.find('<');
        const auto close = trimmed.rfind('>');
        if (open != std::string_view::npos && close != std::string_view::npos &&
            close > open && trimView(trimmed.substr(0, open)) == kArrayTemplate) {
            auto element = tryParse(trimmed.substr(open + 1, close - open - 1));
            if (!element) return std::nullopt;
            return arrayOf(std::move(*element));
        }
    }

    bool pointer = false;
    while (!trimmed.empty() && trimmed.back() == '*') {
        if (pointer) return std::nullopt;
        pointer = true;
        trimmed = trimView(trimmed.substr(0, trimmed.size() - 1));
    }

    if (trimmed.empty() || trimmed.find('<') != std::string_view::npos ||
        trimmed.find('>') != std::string_view::npos) {
        return std::nullopt;
    }
    return CppTypeRef(std::string(trimmed), pointer);
}

CppTypeRef CppTypeRef::parse(std::string_view text) {
    auto parsed = tryParse(text);
    if (!parsed) throw std::invalid_argument(std::string("Not a signature type: '") +
                                             std::string(text) + "'.");
    return std::move(*parsed);
}

F4SETypeMap::F4SETypeMap(std::vector<std::string> structNames)
    : structNames_(std::move(structNames)) {}

std::map<std::string, std::string, CiLess> F4SETypeMap::formTypes() {
    std::map<std::string, std::string, CiLess> map;
    for (const auto& row : formTypeRows()) map[row.papyrus] = row.cpp;
    return map;
}

const std::vector<std::string>& F4SETypeMap::coreIncludes() {
    static const std::vector<std::string> includes = {
        "f4se/PapyrusVM.h",
        "f4se/PapyrusNativeFunctions.h",
        "f4se/PapyrusArgs.h",
    };
    return includes;
}

const std::vector<std::string>& F4SETypeMap::gameIncludes() {
    static const std::vector<std::string> includes = {
        "f4se/GameForms.h",
        "f4se/GameReferences.h",
        "f4se/GameObjects.h",
        "f4se/GameData.h",
        "f4se/GameRTTI.h",
    };
    return includes;
}

bool F4SETypeMap::tryToCpp(const PapyrusTypeText& papyrus, bool isUnsigned,
                           std::optional<CppTypeRef>& outCpp,
                           std::string* refusal) const {
    outCpp.reset();
    if (refusal) refusal->clear();

    if (papyrus.name.empty()) {
        if (refusal) *refusal = "the Papyrus type has no name";
        return false;
    }

    if (papyrus.isArray) {
        if (papyrus.name.size() >= 2 &&
            papyrus.name.compare(papyrus.name.size() - 2, 2, "[]") == 0) {
            if (refusal) {
                *refusal =
                    "Papyrus has no array of arrays, so there is no VMArray<VMArray<>> to emit";
            }
            return false;
        }
        PapyrusTypeText elementText{papyrus.name, false};
        std::optional<CppTypeRef> element;
        if (!tryToCpp(elementText, isUnsigned, element, refusal)) return false;
        if (element->name == "void") {
            if (refusal) *refusal = "None has no array form";
            return false;
        }
        outCpp = CppTypeRef::arrayOf(std::move(*element));
        return true;
    }

    if (CiEqual{}(papyrus.name, "int")) {
        outCpp = CppTypeRef(isUnsigned ? "UInt32" : "SInt32");
        return true;
    }

    const auto& map = byPapyrus();
    const auto found = map.find(papyrus.name);
    if (found != map.end()) {
        outCpp = CppTypeRef(found->second.first, found->second.second);
        return true;
    }

    if (std::find_if(structNames_.begin(), structNames_.end(), [&](const std::string& s) {
            return CiEqual{}(s, papyrus.name);
        }) != structNames_.end()) {
        outCpp = CppTypeRef(papyrus.name);
        return true;
    }

    if (refusal) {
        *refusal = "'" + papyrus.name +
                   "' is not a marshallable type: it is neither a known form type nor a "
                   "declared struct, and F4SE has no specialisation to carry it";
    }
    return false;
}

bool F4SETypeMap::tryToPapyrus(const CppTypeRef* cpp, PapyrusTypeText& outPapyrus,
                               std::string* refusal) const {
    outPapyrus = PapyrusTypeText{};
    if (refusal) refusal->clear();

    if (cpp == nullptr) {
        if (refusal) *refusal = "no type given";
        return false;
    }

    if (cpp->isArray()) {
        PapyrusTypeText element;
        if (!tryToPapyrus(cpp->elementType.get(), element, refusal)) return false;
        if (element.isArray) {
            if (refusal) {
                *refusal =
                    "VMArray<VMArray<>> has no Papyrus type: the language has no jagged arrays";
            }
            return false;
        }
        outPapyrus = PapyrusTypeText{element.name, true};
        return true;
    }

    if (cpp->name == kStaticFunctionTag) {
        if (refusal) *refusal = "StaticFunctionTag is a receiver tag, not a value type";
        return false;
    }

    const auto& map = byCpp();
    const auto found = map.find(cpp->name);
    if (found != map.end()) {
        outPapyrus = PapyrusTypeText{found->second, false};
        return true;
    }

    if (cpp->isPointer) {
        if (refusal) {
            *refusal = "'" + cpp->format() + "' is a pointer to a type the form table does not name";
        }
        return false;
    }

    if (std::find_if(structNames_.begin(), structNames_.end(), [&](const std::string& s) {
            return CiEqual{}(s, cpp->name);
        }) != structNames_.end()) {
        outPapyrus = PapyrusTypeText{cpp->name, false};
        return true;
    }

    if (refusal) {
        *refusal = "'" + cpp->format() + "' is not a known marshalled type or a declared struct";
    }
    return false;
}

namespace {

bool needsStructHeader(const CppTypeRef& type, const std::vector<std::string>& structNames) {
    if (type.isArray()) return needsStructHeader(*type.elementType, structNames);
    return std::find_if(structNames.begin(), structNames.end(), [&](const std::string& s) {
               return CiEqual{}(s, type.name);
           }) != structNames.end();
}

bool needsGameHeaders(const CppTypeRef& type) {
    return type.isArray() ? needsGameHeaders(*type.elementType) : type.isPointer;
}

}

std::vector<std::string> F4SETypeMap::requiredIncludesFor(
    const std::vector<CppTypeRef>& types) const {
    auto includes = coreIncludes();
    const bool structHeader = std::any_of(types.begin(), types.end(), [&](const CppTypeRef& t) {
        return needsStructHeader(t, structNames_);
    });
    if (structHeader) includes.push_back("f4se/PapyrusStruct.h");
    const bool gameHeaders = std::any_of(types.begin(), types.end(), needsGameHeaders);
    if (gameHeaders) {
        const auto& game = gameIncludes();
        includes.insert(includes.end(), game.begin(), game.end());
    }
    return includes;
}

std::string F4SETypeMap::templateInstantiation(bool latent, int arity,
                                               std::string_view cppBaseType,
                                               const CppTypeRef& returnType,
                                               const std::vector<CppTypeRef>& parameters) {
    std::string text;
    text += latent ? "LatentNativeFunction" : "NativeFunction";
    text += std::to_string(arity);
    text += '<';
    text += cppBaseType;
    text += ", ";
    text += returnType.format();
    for (const auto& parameter : parameters) {
        text += ", ";
        text += parameter.format();
    }
    text += '>';
    return text;
}

}
