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

    if (*next == *key) return std::unexpected(Error{"InvalidPerkNext", "PERK cannot link to itself: " + key->to_string()});
    const auto edit = [&](fo4::Plugin& mutated) -> std::optional<Error> {
        const auto* record = fo4::find_record(mutated, *key);
        if (const auto error = wrongPerkType(*record, *key)) return *error;
        if (*next == fo4::FormKey{}) {
            return Error{"InvalidPerkNext", "PERK next must be a PERK FormKey, not null"};
        }
        if (const auto error = checkLoadedSlot(graph_, *next, {"PERK"}, "PERK next")) return *error;
        support::ensureOwnerMaster(mutated, next->owner(), plugins_);
        if (fo4::forms::set_perk_next(mutated, *key, *next) != fo4::forms::PerkLinkWrite::written) {
            return Error{"PerkWriteFailed", "PERK next could not be written to " + key->to_string()};
        }
        return std::nullopt;
    };
    const auto staged = stageTypedRecordEdit(pluginName, *key, "PerkWriteFailed", edit);
    if (!staged) return std::unexpected(staged.error());
    if (!*staged) {
        return nlohmann::json("No change for PERK next on " + key->to_string() + " in " + pluginName + ".");
    }
    return nlohmann::json("Set PERK next on " + key->to_string() + " to " + next->to_string() +
                          " in " + pluginName + ". Call save_plugin to persist.");
}

Result<nlohmann::json> RadiclePluginBackend::setPerkSound(const nlohmann::json& arguments) {
    const auto pluginName = support::stringArgument(arguments, 0);
    const auto key = support::formKeyArgument(support::stringArgument(arguments, 1));
    if (!key) return std::unexpected(key.error());
    if (!arguments.is_array() || arguments.size() <= 2 || !arguments[2].is_string()) {
        return std::unexpected(Error{"InvalidPerkSound", "PERK sound must be a string: an SNDR FormKey, or empty to remove it"});
    }
    const auto text = arguments[2].get<std::string>();
    fo4::FormKey sound;
    if (!text.empty()) {
        const auto parsed = support::formKeyArgument(text);
        if (!parsed || *parsed == fo4::FormKey{}) {
            return std::unexpected(Error{"InvalidPerkSound", "PERK sound must be an SNDR FormKey or empty"});
        }
        sound = *parsed;
    }

    const auto edit = [&](fo4::Plugin& mutated) -> std::optional<Error> {
        const auto* record = fo4::find_record(mutated, *key);
        if (const auto error = wrongPerkType(*record, *key)) return *error;
        if (sound != fo4::FormKey{}) {
            if (const auto error = checkLoadedSlot(graph_, sound, {"SNDR"}, "PERK sound")) return *error;
            support::ensureOwnerMaster(mutated, sound.owner(), plugins_);
        }
        if (fo4::forms::set_perk_sound(mutated, *key, sound) != fo4::forms::PerkLinkWrite::written) {
            return Error{"PerkWriteFailed", "PERK sound could not be written to " + key->to_string()};
        }
        return std::nullopt;
    };
    const auto staged = stageTypedRecordEdit(pluginName, *key, "PerkWriteFailed", edit);
    if (!staged) return std::unexpected(staged.error());
    if (sound == fo4::FormKey{}) {
        if (!*staged) return nlohmann::json("No change for PERK sound on " + key->to_string() + " in " + pluginName + ".");
        return nlohmann::json("Removed PERK sound from " + key->to_string() + " in " + pluginName + ". Call save_plugin to persist.");
    }
    if (!*staged) return nlohmann::json("No change for PERK sound on " + key->to_string() + " in " + pluginName + ".");
    return nlohmann::json("Set PERK sound on " + key->to_string() + " to " + sound.to_string() + " in " +
                          pluginName + ". Call save_plugin to persist.");
}

Result<nlohmann::json> RadiclePluginBackend::setPerkSwf(const nlohmann::json& arguments) {
    const auto pluginName = support::stringArgument(arguments, 0);
    const auto key = support::formKeyArgument(support::stringArgument(arguments, 1));
    if (!key) return std::unexpected(key.error());
    if (!arguments.is_array() || arguments.size() <= 2 || !arguments[2].is_string()) {
        return std::unexpected(Error{"InvalidPerkSwf", "PERK swf must be a string; empty removes the FNAM"});
    }
    const auto text = arguments[2].get<std::string>();
    if (text.find('\0') != std::string::npos) return std::unexpected(Error{"InvalidPerkSwf", "PERK swf must not contain a NUL byte"});
    const auto stored = fo4::text::utf8_to_windows1252(text);
    if (!stored) return std::unexpected(Error{"InvalidPerkSwf", "PERK swf is not representable in Windows-1252"});

    const auto edit = [&](fo4::Plugin& mutated) -> std::optional<Error> {
        const auto* record = fo4::find_record(mutated, *key);
        if (const auto error = wrongPerkType(*record, *key)) return *error;
        if (fo4::forms::set_perk_swf(mutated, *key, *stored) != fo4::forms::PerkLinkWrite::written) {
            return Error{"PerkWriteFailed", "PERK swf could not be written to " + key->to_string()};
        }
        return std::nullopt;
    };
    const auto staged = stageTypedRecordEdit(pluginName, *key, "PerkWriteFailed", edit);
    if (!staged) return std::unexpected(staged.error());
    if (!*staged) return nlohmann::json("No change for PERK swf on " + key->to_string() + " in " + pluginName + ".");
    if (stored->empty()) {
        return nlohmann::json("Removed PERK swf from " + key->to_string() + " in " + pluginName + ". Call save_plugin to persist.");
    }
    return nlohmann::json("Set PERK swf on " + key->to_string() + " in " + pluginName + ". Call save_plugin to persist.");
}

}  // namespace fo4ide::plugin
