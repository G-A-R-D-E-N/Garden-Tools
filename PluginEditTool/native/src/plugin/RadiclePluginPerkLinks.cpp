#include "RadiclePluginLifecycleFields.hpp"
#include "RadiclePluginPerkInternal.hpp"
#include "RadiclePluginSupport.hpp"

#include <fo4/forms/perk.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/schema/condition_scopes.hpp>
#include <fo4/text/encoding.hpp>
#include <fo4ide/plugin/ConditionCodec.hpp>

#include <exception>
#include <optional>
#include <string>

namespace fo4ide::plugin {

using namespace lifecycle;
using namespace perk_detail;

Result<nlohmann::json> RadiclePluginBackend::getPerkLinks(const nlohmann::json& arguments) const {
    const auto key = support::formKeyArgument(support::stringArgument(arguments, 1));
    if (!key) return std::unexpected(key.error());

    OpLock lock(*this, OpLock::Mode::Read);
    const auto version = versionPlugin(support::stringArgument(arguments, 0), *key);
    if (!version) return std::unexpected(version.error());
    const auto* source = *version;
    const auto* record = fo4::find_record(*source, *key);
    if (record == nullptr) {
        return std::unexpected(Error{"RecordNotFound", "Record is not present: " + key->to_string()});
    }
    if (const auto error = wrongPerkType(*record, *key)) return std::unexpected(*error);
    try {
        const auto links = fo4::forms::read_perk_links(*source, *record);
        const auto root = fo4::schema::read_root_conditions(*record);
        if (!root.ok) {
            return std::unexpected(Error{
                "ConditionPayloadUnsupported",
                "Condition bytes cannot round-trip on PERK " + key->to_string()
            });
        }
        nlohmann::json conditions = nlohmann::json::array();
        for (const auto& condition : root.conditions) conditions.push_back(describeCondition(condition, *source));
        return nlohmann::json{
            {"formKey", key->to_string()},
            {"nextPerk", support::formKeyJson(links.next_perk)},
            {"sound", support::formKeyJson(links.sound)},
            {"swf", links.swf.has_value() ? nlohmann::json(fo4::text::windows1252_to_utf8(*links.swf))
                                          : nlohmann::json(nullptr)},
            {"conditions", std::move(conditions)}
        };
    } catch (const std::exception& error) {
        return std::unexpected(Error{"PerkReadFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::setPerkNext(const nlohmann::json& arguments) {
    const auto pluginName = support::stringArgument(arguments, 0);
    const auto key = support::formKeyArgument(support::stringArgument(arguments, 1));
    if (!key) return std::unexpected(key.error());
    const auto next = support::formKeyArgument(support::stringArgument(arguments, 2));
    if (!next) return std::unexpected(Error{"InvalidPerkNext", "PERK next must be a PERK FormKey"});

    std::optional<Error> refusal;
    const auto edit = [&](fo4::Plugin& mutated) {
        const auto* record = fo4::find_record(mutated, *key);
        if (const auto error = wrongPerkType(*record, *key)) {
            refusal = *error;
            return;
        }
        if (const auto error = checkLoadedSlot(graph_, *next, {"PERK"}, "PERK next")) {
            refusal = *error;
            return;
        }
        if (const auto error = requireDeclaredOwner(mutated, *next, "PERK next")) {
            refusal = *error;
            return;
        }
        if (fo4::forms::set_perk_next(mutated, *key, *next) != fo4::forms::PerkLinkWrite::written) {
            refusal = Error{"PerkWriteFailed", "PERK next could not be written to " + key->to_string()};
        }
    };
    const auto staged = stageTypedRecordEdit(pluginName, *key, "PerkWriteFailed", edit);
    if (refusal.has_value()) return std::unexpected(*refusal);
    if (!staged) return std::unexpected(staged.error());
    if (!*staged) {
        return nlohmann::json("No change for PERK next on " + key->to_string() + " in " + pluginName + ".");
    }
    return nlohmann::json("Set PERK next on " + key->to_string() + " to " + next->to_string() +
                          " in " + pluginName + ". Call save_plugin to persist.");
}

}  // namespace fo4ide::plugin
