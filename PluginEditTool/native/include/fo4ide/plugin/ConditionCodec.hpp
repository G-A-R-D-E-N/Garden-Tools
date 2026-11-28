#pragma once

#include <fo4/schema/ctda.hpp>

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace fo4ide::plugin {

nlohmann::json describeCondition(const fo4::CtdaCondition& condition,
                                 const fo4::Plugin& plugin);

using EditorIdResolver = std::function<std::optional<fo4::FormKey>(std::string_view)>;
using MasterEnsurer = std::function<void(fo4::Plugin&, const fo4::PluginKey&)>;
std::optional<fo4::RawFormId> resolveConditionFormId(std::string_view text,
                                                     fo4::Plugin& plugin,
                                                     const EditorIdResolver& resolveEditorId,
                                                     const MasterEnsurer& ensureMaster = {});

fo4::Plugin conditionBaseline(const fo4::Plugin& plugin);

std::optional<fo4::CtdaCondition> buildCondition(const nlohmann::json& element,
                                                 fo4::Plugin& plugin,
                                                 const EditorIdResolver& resolveEditorId,
                                                 std::string& failure,
                                                 const MasterEnsurer& ensureMaster = {},
                                                 const fo4::Plugin* original = nullptr);

std::optional<std::vector<fo4::CtdaCondition>> buildConditions(
    const nlohmann::json& document, fo4::Plugin& plugin,
    std::span<const fo4::CtdaCondition> existing,
    const EditorIdResolver& resolveEditorId, std::string& failure,
    const MasterEnsurer& ensureMaster = {}, const fo4::Plugin* original = nullptr);

std::optional<std::string> conditionCisFromUtf8(std::string_view utf8);

// Formatting helpers exposed for the tests.
std::string conditionOperatorString(std::uint8_t compareOperator);
std::string conditionFlagString(std::uint8_t flags);

}  // namespace fo4ide::plugin
