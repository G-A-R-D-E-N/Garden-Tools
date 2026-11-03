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
}

std::expected<std::string, std::string> updateQuestFragmentSource(
    std::string_view source, std::string_view scriptName, std::uint16_t stage, std::uint32_t index,
    const std::vector<QuestFragmentAction>& actions) {
    if (!validScriptName(scriptName) || actions.empty()) return std::unexpected("invalid fragment input");
    const auto header = Parser::parse("Scriptname " + std::string(scriptName) + " Extends Quest Hidden Const\n");
    if (header.hasErrors() || !sameName(header.name, scriptName)) return std::unexpected("invalid script name");
    std::ostringstream name;
    name << "Fragment_Stage_" << std::setfill('0') << std::setw(4) << stage
         << "_Item_" << std::setw(2) << index;
    std::ostringstream function;
    function << "Function " << name.str() << "()\n";
    for (const auto& action : actions) {
        const char* method = nullptr;
        switch (action.kind) {
            case QuestFragmentActionKind::Display: method = "SetObjectiveDisplayed"; break;
            case QuestFragmentActionKind::Complete: method = "SetObjectiveCompleted"; break;
            case QuestFragmentActionKind::SetStage: method = "SetStage"; break;
        }
        if (!method) return std::unexpected("unknown fragment action");
        function << "    " << method << '(' << action.value << ")\n";
    }
    function << "EndFunction\n";
    if (source.empty()) {
        return std::string(marker) + "Scriptname " + std::string(scriptName) +
               " Extends Quest Hidden Const\n\n" + function.str();
    }
    if (!source.starts_with(marker)) return std::unexpected("fragment source is not managed");
    const auto parsed = Parser::parse(source);
    if (parsed.hasErrors() || !sameName(parsed.name, scriptName) ||
        !parsed.extends || !sameName(*parsed.extends, "Quest")) {
        return std::unexpected("fragment source is malformed or names a different script");
    }
    const FunctionDecl* existing = nullptr;
    for (const auto& candidate : parsed.functions) {
        if (!sameName(candidate.name, name.str())) continue;
        if (existing || candidate.isNative || candidate.isGlobal || candidate.returnType ||
            !candidate.parameters.empty()) {
            return std::unexpected("fragment function is ambiguous");
        }
        existing = &candidate;
    }
    std::string result(source);
    if (!existing) return result + "\n" + function.str();
    const auto begin = static_cast<std::size_t>(existing->span.start);
    const auto length = static_cast<std::size_t>(existing->span.length);
    if (begin > result.size() || length > result.size() - begin) {
        return std::unexpected("fragment function has an invalid source span");
    }
    result.replace(begin, length, function.str());
    return result;
}

}
