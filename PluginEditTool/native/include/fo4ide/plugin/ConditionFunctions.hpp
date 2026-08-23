#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4ide::plugin {

// Port of ConditionFunctions.cs: the FO4 condition-function parameter table
// and the JSON contract the UI's condition editor consumes.
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

private:
    static const std::vector<std::pair<std::string, std::vector<std::string>>>& table();
    static std::string spaced(std::string_view pascal);
};

// Port of WriteService.ConditionFunctionNames: the Condition.Function enum
// names (from Mutagen), case-insensitively sorted, as a JSON array.
std::string conditionFunctionNames();

// Port of WriteService.ConditionRunOnNames: the Condition.RunOnType enum
// names in declaration order, as a JSON array.
std::string conditionRunOnNames();

}  // namespace fo4ide::plugin
