#pragma once

#include <cstdint>
#include <optional>
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

// Port of the Condition.Function enum index<->name mapping (479 members).
// functionName returns the enum name for a binary function index (empty when
// unknown); functionIndex is the case-insensitive reverse, like
// Enum.TryParse<Condition.Function>(name, ignoreCase: true), returning nullopt
// when the name is not a member.
std::string functionName(std::uint16_t index);
std::optional<std::uint16_t> functionIndex(std::string_view name);

// Port of the Condition.RunOnType enum value<->name mapping (values 0-10).
// runOnName returns the name for a binary run-on
// value (empty when unknown); runOnValue is the case-insensitive reverse.
std::string runOnName(std::uint32_t value);
std::optional<std::uint32_t> runOnValue(std::string_view name);

}  // namespace fo4ide::plugin
