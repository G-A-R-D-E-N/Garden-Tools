#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4ide::plugin {

// Constraint: The FO4 condition-function parameter table and the JSON contract the UI's condition editor consumes. The rows, parameter types, enum names and Run On values all come from Radicle's schema (fo4/schema/condition_functions.hpp); this class only turns them into the editor's labels, kinds and JSON.
class ConditionFunctions {
public:
    // The parameter types a function's arguments expect (empty when none);
    // lookup is case-insensitive like the C# ByName dictionary.
    static std::vector<std::string> paramsOf(std::string_view function);

    // (kind, types) for a parameter type, mirroring the EditorFor switch:
    // record types name the record(s) to pick from, String/Event are free
    // text, everything else is a bare number.
    static std::pair<std::string, std::string> editorFor(std::string_view paramType);

    // The full JSON contract of ConditionFunctions.AsJson: an object keyed by
    // function name (table order), each value an array of per-parameter
    // {label, kind, types} descriptors.
    static std::string asJson();

    // Number of table entries (for the integrity tests).
    static std::size_t count();

};

std::string conditionFunctionNames();

std::string conditionRunOnNames();

std::string functionName(std::uint16_t index);
std::optional<std::uint16_t> functionIndex(std::string_view name);

std::string runOnName(std::uint32_t value);
std::optional<std::uint32_t> runOnValue(std::string_view name);

}  // namespace fo4ide::plugin
