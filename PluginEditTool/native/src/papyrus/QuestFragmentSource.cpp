#include <fo4ide/papyrus/QuestFragmentSource.hpp>
#include <fo4ide/papyrus/Parser.hpp>

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>

namespace fo4ide::papyrus {
namespace {
constexpr std::string_view marker = "; GardenTools managed quest fragments\n";

bool validScriptName(std::string_view name) {
    bool first = true;
    for (const unsigned char ch : name) {
        if (ch == ':') {
            if (first) return false;
            first = true;
        } else {
            if (!(std::isalpha(ch) || ch == '_' || (!first && std::isdigit(ch))) || ch > 127) return false;
            first = false;
        }
    }
    return !first;
}

bool sameName(std::string_view left, std::string_view right) {
    return left.size() == right.size() &&
           std::equal(left.begin(), left.end(), right.begin(), [](unsigned char a, unsigned char b) {
               return std::tolower(a) == std::tolower(b);
           });
}

std::string fragmentFunctionName(std::uint16_t stage, std::uint32_t index) {
    std::ostringstream name;
    name << "Fragment_Stage_" << std::setfill('0') << std::setw(4) << stage
         << "_Item_" << std::setw(2) << index;
    return name.str();
}

std::string boolText(bool value) {
    return value ? "true" : "false";
}

std::expected<std::string, std::string> actionCall(const QuestFragmentAction& action) {
    const auto number = std::to_string(action.value);
    switch (action.kind) {
        case QuestFragmentActionKind::Display:
            if (action.completed) return std::unexpected("completed applies only to complete actions");
            if (!action.visible && !action.force) return "SetObjectiveDisplayed(" + number + ")";
            return "SetObjectiveDisplayed(" + number + ", " + boolText(action.visible.value_or(true)) + ", " +
                   boolText(action.force.value_or(false)) + ")";
        case QuestFragmentActionKind::Complete:
            if (action.visible || action.force) return std::unexpected("visible and force apply only to display actions");
            if (!action.completed) return "SetObjectiveCompleted(" + number + ")";
            return "SetObjectiveCompleted(" + number + ", " + boolText(*action.completed) + ")";
        case QuestFragmentActionKind::SetStage:
            if (action.visible || action.force || action.completed) return std::unexpected("SetStage takes no flags");
            return "SetStage(" + number + ")";
    }
    return std::unexpected("unknown fragment action");
}

std::expected<const FunctionDecl*, std::string> findFragmentFunction(const Script& parsed, std::string_view scriptName,
                                                                    std::string_view name) {
    if (parsed.hasErrors() || !sameName(parsed.name, scriptName) ||
        !parsed.extends || !sameName(*parsed.extends, "Quest")) {
        return std::unexpected("fragment source is malformed or names a different script");
    }
    const FunctionDecl* existing = nullptr;
    for (const auto& candidate : parsed.functions) {
        if (!sameName(candidate.name, name)) continue;
        if (existing || candidate.isNative || candidate.isGlobal || candidate.returnType ||
            !candidate.parameters.empty()) {
            return std::unexpected("fragment function is ambiguous");
        }
        existing = &candidate;
    }
    return existing;
}

}

bool isManagedQuestFragmentSource(std::string_view source) {
    return source.starts_with(marker);
}

std::expected<std::string, std::string> updateQuestFragmentSource(
    std::string_view source, std::string_view scriptName, std::uint16_t stage, std::uint32_t index,
    const std::vector<QuestFragmentAction>& actions) {
    if (!validScriptName(scriptName) || actions.empty()) return std::unexpected("invalid fragment input");
    const auto header = Parser::parse("Scriptname " + std::string(scriptName) + " Extends Quest Hidden Const\n");
    if (header.hasErrors() || !sameName(header.name, scriptName)) return std::unexpected("invalid script name");
    const auto name = fragmentFunctionName(stage, index);
    std::ostringstream function;
    function << "Function " << name << "()\n";
    for (const auto& action : actions) {
        auto call = actionCall(action);
        if (!call) return std::unexpected(call.error());
        function << "    " << *call << "\n";
    }
    function << "EndFunction\n";
    if (source.empty()) {
        return std::string(marker) + "Scriptname " + std::string(scriptName) +
               " Extends Quest Hidden Const\n\n" + function.str();
    }
    if (!isManagedQuestFragmentSource(source)) return std::unexpected("fragment source is not managed");
    const auto parsed = Parser::parse(source);
    const auto existing = findFragmentFunction(parsed, scriptName, name);
    if (!existing) return std::unexpected(existing.error());
    std::string result(source);
    if (!*existing) return result + "\n" + function.str();
    const auto begin = static_cast<std::size_t>((*existing)->span.start);
    const auto length = static_cast<std::size_t>((*existing)->span.length);
    if (begin > result.size() || length > result.size() - begin) {
        return std::unexpected("fragment function has an invalid source span");
    }
    result.replace(begin, length, function.str());
    return result;
}

std::expected<std::string, std::string> removeQuestFragmentSource(
    std::string_view source, std::string_view scriptName, std::uint16_t stage, std::uint32_t index) {
    if (!validScriptName(scriptName)) return std::unexpected("invalid fragment input");
    if (!isManagedQuestFragmentSource(source)) return std::unexpected("fragment source is not managed");
    const auto parsed = Parser::parse(source);
    const auto name = fragmentFunctionName(stage, index);
    const auto existing = findFragmentFunction(parsed, scriptName, name);
    if (!existing) return std::unexpected(existing.error());
    if (!*existing) return std::unexpected("fragment function is missing");
    const auto begin = static_cast<std::size_t>((*existing)->span.start);
    const auto length = static_cast<std::size_t>((*existing)->span.length);
    if (begin > source.size() || length > source.size() - begin) {
        return std::unexpected("fragment function has an invalid source span");
    }
    std::string result(source);
    result.erase(begin, length);
    if (begin < result.size() && result[begin] == '\n') result.erase(begin, 1);
    return result;
}

}
