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

// FormID text -> raw FormID for the plugin, mirroring the C# ResolveFk:
// FormKey.TryFactory first, then editor-ID resolution via the loaded set.
// `resolveEditorId` is called only when the text is not a parseable FormKey;
// the backend supplies the loaded-plugin scan. Returns nullopt when neither
// resolves.
using EditorIdResolver = std::function<std::optional<fo4::RawFormId>(std::string_view)>;
std::optional<fo4::RawFormId> resolveConditionFormId(std::string_view text,
                                                     const fo4::Plugin& plugin,
                                                     const EditorIdResolver& resolveEditorId);

std::optional<fo4::CtdaCondition> buildCondition(const nlohmann::json& element,
                                                 const fo4::Plugin& plugin,
                                                 const EditorIdResolver& resolveEditorId,
                                                 std::string& failure);

std::optional<std::vector<fo4::CtdaCondition>> buildConditions(
    const nlohmann::json& document, const fo4::Plugin& plugin,
    std::span<const fo4::CtdaCondition> existing,
    const EditorIdResolver& resolveEditorId, std::string& failure);

std::optional<std::string> conditionCisFromUtf8(std::string_view utf8);

// Formatting helpers exposed for the tests.
std::string conditionOperatorString(std::uint8_t compareOperator);
std::string conditionFlagString(std::uint8_t flags);

}  // namespace fo4ide::plugin
