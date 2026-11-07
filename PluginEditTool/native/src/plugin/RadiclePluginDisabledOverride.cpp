#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include <string>

#include "RadiclePluginSupport.hpp"

namespace fo4ide::plugin {

Result<nlohmann::json> RadiclePluginBackend::copyAsDisabledOverride(const nlohmann::json& arguments) {
    const auto sourceName = support::stringArgument(arguments, 0);
    const auto formKeyText = support::stringArgument(arguments, 1);
    const auto targetName = support::stringArgument(arguments, 2);

    if (sourceName.empty()) {
        return std::unexpected(Error{"SourcePluginMissing", "Source plugin name is empty"});
    }
    if (formKeyText.empty()) {
        return std::unexpected(Error{"FormKeyMissing", "Record FormKey is empty"});
    }
    if (targetName.empty()) {
        return std::unexpected(Error{"TargetPluginMissing", "Target plugin name is empty"});
    }

    OpLock lock(*this, OpLock::Mode::Write, {targetName, sourceName});
    auto copied = copyAsOverrideLocked(arguments, true);
    if (!copied || !copied->is_string()) return copied;
    const auto text = copied->get<std::string>();
    if (text.starts_with("LOSES:") || text.starts_with("EXISTS:")) return copied;
    return nlohmann::json(text + " Marked it Initially Disabled.");
}

}  // namespace fo4ide::plugin
