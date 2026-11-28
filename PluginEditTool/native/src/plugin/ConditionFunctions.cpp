#include <fo4ide/plugin/ConditionFunctions.hpp>

#include <fo4/schema/condition_functions.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace fo4ide::plugin {

namespace {

std::string toLower(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const auto ch : text) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return out;
}

bool equalsIgnoreCase(std::string_view left, std::string_view right) {
    return left.size() == right.size() &&
           std::equal(left.begin(), left.end(), right.begin(), [](char a, char b) {
               return std::tolower(static_cast<unsigned char>(a)) ==
                      std::tolower(static_cast<unsigned char>(b));
           });
}

// Constraint: Radicle names parameter types in snake_case ("actor_value", "faction_null"); the editor contract has always used the PascalCase name. "worldspace" is the one type whose editor label carries an internal capital ("WorldSpace"), which spaced() then renders as "World Space".
std::string pascalType(std::string_view snake) {
    std::string out;
    out.reserve(snake.size());
    bool upper = true;
    for (const char ch : snake) {
        if (ch == '_') {
            upper = true;
            continue;
        }
        out.push_back(upper ? static_cast<char>(std::toupper(static_cast<unsigned char>(ch))) : ch);
        upper = false;
    }
    if (out == "Worldspace") return "WorldSpace";
    return out;
}

std::string spaced(std::string_view pascal) {
    std::string out;
    out.reserve(pascal.size() + 4);
    for (std::size_t i = 0; i < pascal.size(); i++) {
        if (i > 0 && std::isupper(static_cast<unsigned char>(pascal[i])) &&
            !std::isupper(static_cast<unsigned char>(pascal[i - 1]))) {
            out.push_back(' ');
        }
        out.push_back(pascal[i]);
    }
    return out;
}

// Constraint: The Run On enum names read "Combat Target" in the schema and "CombatTarget" in the editor contract.
std::string withoutSpaces(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char ch : text) {
        if (ch != ' ') out.push_back(ch);
    }
    return out;
}

// Constraint: The editor only offers the schema rows flagged "editor": true; every other function still resolves for name/index lookups but has no descriptor.
const fo4::schema::ConditionFunction* editorFunction(std::string_view function) {
    const auto* found = fo4::schema::condition_function_by_name(function);
    if (found == nullptr) return nullptr;
    const auto indices = fo4::schema::condition_editor_function_indices();
    if (std::find(indices.begin(), indices.end(), found->index) == indices.end()) {
        return nullptr;
    }
    return found;
}

std::vector<std::string> declaredTypes(const fo4::schema::ConditionFunction& function) {
    std::vector<std::string> out;
    for (const auto* parameter : function.parameters) {
        if (parameter == nullptr) break;
        out.push_back(pascalType(parameter->name));
    }
    return out;
}

// Constraint: (kind, types) for a parameter type, mirroring the EditorFor switch: record types name the record(s) to pick from, String/Event are free text, everything else is a bare number.
std::pair<std::string, std::string> editorForType(std::string_view paramType) {
    struct Row { const char* type; const char* kinds; };
    static const Row kRecords[] = {
        {"Actor", "PlacedNpc"},
        {"ActorBase", "Npc"},
        {"ActorValue", "ActorValueInformation"},
        {"AssociationType", "AssociationType"},
        {"BaseEffect", "MagicEffect"},
        {"BaseObject", ""},
        {"Cell", "Cell"},
        {"Class", "Class"},
        {"DamageType", "DamageType"},
        {"EffectItem", "Ingestible,ObjectEffect,Ingredient,Spell"},
        {"EncounterZone", "EncounterZone"},
        {"EquipType", "EquipType"},
        {"EventData", "Location,Keyword,FormList"},
        {"Faction", "Faction"},
        {"FactionNull", "Faction"},
        {"FormList", "FormList"},
        {"Furniture", "Furniture"},
        {"Global", "Global"},
        {"IdleForm", "IdleAnimation"},
        {"Keyword", "Keyword"},
        {"Location", "Location"},
        {"LocationRefType", "LocationReferenceType"},
        {"Owner", "Faction,Npc"},
        {"Package", "Package"},
        {"Perk", "Perk"},
        {"Quest", "Quest"},
        {"Race", "Race"},
        {"Reference", "PlacedObject,PlacedNpc"},
        {"Region", "Region"},
        {"Scene", "Scene"},
        {"VoiceType", "VoiceType"},
        {"Weather", "Weather"},
        {"WorldSpace", "Worldspace"},
    };
    for (const auto& row : kRecords) {
        if (equalsIgnoreCase(paramType, row.type)) {
            return {std::string("record"), row.kinds};
        }
    }
    if (equalsIgnoreCase(paramType, "String") || equalsIgnoreCase(paramType, "Event")) {
        return {"text", ""};
    }
    return {"number", ""};
}

}  // namespace

std::size_t ConditionFunctions::count() {
    return fo4::schema::condition_editor_function_indices().size();
}

std::vector<std::string> ConditionFunctions::paramsOf(std::string_view function) {
    const auto* found = editorFunction(function);
    if (found == nullptr) return {};
    return declaredTypes(*found);
}

std::pair<std::string, std::string> ConditionFunctions::editorFor(std::string_view paramType) {
    return editorForType(paramType);
}

std::string ConditionFunctions::asJson() {
    nlohmann::json out = nlohmann::json::object();
    for (const auto index : fo4::schema::condition_editor_function_indices()) {
        const auto* function = fo4::schema::condition_function(index);
        if (function == nullptr) continue;
        auto& entry = out[std::string(function->name)];
        entry = nlohmann::json::array();
        for (const auto& param : declaredTypes(*function)) {
            const auto [kind, types] = editorForType(param);
            entry.push_back({
                {"label", spaced(param)},
                {"kind", kind},
                {"types", types},
            });
        }
    }
    return out.dump();
}

std::string conditionFunctionNames() {
    // Constraint: names are case-insensitively sorted like Enum.GetNames<Condition.Function>().OrderBy(n => n, OrdinalIgnoreCase).
    std::vector<std::string> names;
    const auto functions = fo4::schema::condition_functions();
    names.reserve(functions.size());
    for (const auto& function : functions) names.emplace_back(function.name);
    std::sort(names.begin(), names.end(),
              [](const std::string& left, const std::string& right) {
                  return toLower(left) < toLower(right);
              });
    return nlohmann::json(names).dump();
}

std::string conditionRunOnNames() {
    std::vector<std::string> names;
    const auto runOns = fo4::schema::condition_run_ons();
    names.reserve(runOns.size());
    for (const auto& row : runOns) names.push_back(withoutSpaces(row.name));
    return nlohmann::json(names).dump();
}

std::string functionName(std::uint16_t index) {
    const auto* function = fo4::schema::condition_function(index);
    return function == nullptr ? std::string{} : std::string{function->name};
}

std::optional<std::uint16_t> functionIndex(std::string_view name) {
    const auto* function = fo4::schema::condition_function_by_name(name);
    if (function == nullptr) return std::nullopt;
    return function->index;
}

std::string runOnName(std::uint32_t value) {
    const auto* row = fo4::schema::condition_run_on(value);
    return row == nullptr ? std::string{} : withoutSpaces(row->name);
}

std::optional<std::uint32_t> runOnValue(std::string_view name) {
    const auto wanted = toLower(name);
    for (const auto& row : fo4::schema::condition_run_ons()) {
        if (toLower(withoutSpaces(row.name)) == wanted) return row.value;
    }
    return std::nullopt;
}

}  // namespace fo4ide::plugin
