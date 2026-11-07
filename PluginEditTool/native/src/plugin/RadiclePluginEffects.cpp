#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "RadiclePluginSupport.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <fo4/forms/effects.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/signature.hpp>

namespace fo4ide::plugin {
namespace {

constexpr auto k_mgef = fo4::Signature::from_string("MGEF");

std::optional<float> floatArgument(const nlohmann::json& arguments, std::size_t index) {
    if (!arguments.is_array() || index >= arguments.size()) return std::nullopt;
    if (!arguments[index].is_number()) return std::nullopt;
    return arguments[index].get<float>();
}

std::optional<std::size_t> sizeArgument(const nlohmann::json& arguments, std::size_t index) {
    const auto value = support::integerArgument<std::uint32_t>(arguments, index);
    if (!value) return std::nullopt;
    return static_cast<std::size_t>(*value);
}

Error effectStatusError(fo4::forms::EffectListWrite status, std::string_view formKeyText) {
    switch (status) {
        case fo4::forms::EffectListWrite::record_missing:
            return Error{"RecordNotFound", "Record is not present: " + std::string(formKeyText)};
        case fo4::forms::EffectListWrite::unsupported_record:
            return Error{"UnsupportedRecord",
                         "Record must carry an effect list: SPEL, ALCH, ENCH or INGR"};
        case fo4::forms::EffectListWrite::unexpected_layout:
            return Error{"UnexpectedEffectLayout",
                         "The record's effect block is not a contiguous EFID/EFIT list"};
        case fo4::forms::EffectListWrite::index_out_of_range:
            return Error{"EffectIndexOutOfRange",
                         "Effect index is outside the list: " + std::string(formKeyText)};
        case fo4::forms::EffectListWrite::effect_not_magic_effect:
            return Error{"NotMagicEffect", "effect must be a magic effect (MGEF) FormKey"};
        case fo4::forms::EffectListWrite::written:
            break;
    }
    return Error{"MagicEffectWriteFailed", "The effect list was not written"};
}

template <class Plugins>
std::optional<Error> verifyEffectTarget(const Plugins& plugins,
                                        const fo4::Plugin& edited,
                                        const fo4::FormKey& effect) {
    if (effect.owner() == edited.key || effect.owner().empty()) return std::nullopt;
    const auto ownerLoaded = std::ranges::any_of(plugins, [&effect](const auto& loaded) {
        return loaded.plugin.key == effect.owner();
    });
    if (!ownerLoaded) {
        return Error{"MagicEffectOwnerNotLoaded",
                     "The owner of effect " + effect.to_string() + " is not loaded"};
    }
    const auto& masters = edited.header.masters;
    if (std::ranges::none_of(masters, [&effect](const auto& master) { return master.key == effect.owner(); })) {
        return Error{"MagicEffectOwnerNotMaster",
                     "The owner of effect " + effect.to_string() +
                         " is not a master of this plugin; call add_master first"};
    }
    const auto winner = support::winningPlugin(plugins, effect);
    if (!winner) return winner.error();
    const auto* record = fo4::find_record(**winner, effect);
    if (record->header.signature != k_mgef) {
        return effectStatusError(fo4::forms::EffectListWrite::effect_not_magic_effect,
                                 effect.to_string());
    }
    return std::nullopt;
}

std::optional<fo4::forms::MagicEffectEntry> magicEffectEntry(const nlohmann::json& arguments,
                                                             std::size_t first) {
    const auto effectText = support::stringArgument(arguments, first);
    if (effectText.empty()) return std::nullopt;
    const auto effect = support::formKeyArgument(effectText);
    if (!effect) return std::nullopt;
    const auto magnitude = floatArgument(arguments, first + 1);
    const auto area = support::integerArgument<std::uint32_t>(arguments, first + 2);
    const auto duration = support::integerArgument<std::uint32_t>(arguments, first + 3);
    if (!magnitude || !area || !duration) return std::nullopt;
    fo4::forms::MagicEffectEntry entry;
    entry.effect = *effect;
    entry.magnitude = *magnitude;
    entry.area = *area;
    entry.duration = *duration;
    return entry;
}

nlohmann::json magicEffectJson(const fo4::forms::MagicEffectEntry& entry, std::size_t index) {
    return nlohmann::json{
        {"index", index},
        {"effect", support::formKeyJson(entry.effect)},
        {"magnitude", entry.magnitude},
        {"area", entry.area},
        {"duration", entry.duration}
    };
}

}

Result<nlohmann::json> RadiclePluginBackend::getMagicEffects(
    const nlohmann::json& arguments) const {
    const auto key = support::formKeyArgument(support::stringArgument(arguments, 1));
    if (!key) return std::unexpected(key.error());

    OpLock lock(*this, OpLock::Mode::Read);
    const auto version = versionPlugin(support::stringArgument(arguments, 0), *key);
    if (!version) return std::unexpected(version.error());
    const auto* record = fo4::find_record(**version, *key);
    if (record == nullptr) {
        return std::unexpected(Error{
            "RecordNotFound",
            "Record is not present: " + key->to_string()
        });
    }
    try {
        const auto entries = fo4::forms::read_magic_effects(**version, *record);
        nlohmann::json effects = nlohmann::json::array();
        for (std::size_t index = 0; index < entries.size(); ++index) {
            effects.push_back(magicEffectJson(entries[index], index));
        }
        return nlohmann::json{
            {"formKey", key->to_string()},
            {"count", effects.size()},
            {"effects", std::move(effects)}
        };
    } catch (const fo4::forms::MagicEffectError& error) {
        return std::unexpected(Error{"MagicEffectReadFailed", error.what()});
    } catch (const std::exception& error) {
        return std::unexpected(Error{"MagicEffectReadFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::addMagicEffect(const nlohmann::json& arguments) {
    const auto pluginName = support::stringArgument(arguments, 0);
    const auto key = support::formKeyArgument(support::stringArgument(arguments, 1));
    if (!key) return std::unexpected(key.error());
    const auto entry = magicEffectEntry(arguments, 2);
    if (!entry) {
        return std::unexpected(Error{
            "InvalidMagicEffect",
            "effect, magnitude, area and duration are required"
        });
    }
    if (pluginName.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }
    if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);
    if (const auto error = verifyEffectTarget(plugins_, loaded->plugin, entry->effect)) {
        return std::unexpected(*error);
    }

    auto mutated = loaded->plugin;
    std::size_t index = 0;
    const auto status = fo4::forms::add_magic_effect(mutated, *key, *entry, &index);
    if (status != fo4::forms::EffectListWrite::written) {
        return std::unexpected(effectStatusError(status, key->to_string()));
    }
    stagePluginMutationLocked(*loaded, std::move(mutated));
    return nlohmann::json{
        {"index", index},
        {"formKey", key->to_string()},
        {"message", "Added effect " + entry->effect.to_string() + " at index " +
                        std::to_string(index) + " in " + pluginName + "."}
    };
}

Result<nlohmann::json> RadiclePluginBackend::setMagicEffect(const nlohmann::json& arguments) {
    const auto pluginName = support::stringArgument(arguments, 0);
    const auto key = support::formKeyArgument(support::stringArgument(arguments, 1));
    if (!key) return std::unexpected(key.error());
    const auto index = sizeArgument(arguments, 2);
    if (!index) return std::unexpected(Error{"InvalidEffectIndex", "effect index is required"});
    const auto entry = magicEffectEntry(arguments, 3);
    if (!entry) {
        return std::unexpected(Error{
            "InvalidMagicEffect",
            "effect, magnitude, area and duration are required"
        });
    }
    if (pluginName.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }
    if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);
    if (const auto error = verifyEffectTarget(plugins_, loaded->plugin, entry->effect)) {
        return std::unexpected(*error);
    }

    auto mutated = loaded->plugin;
    const auto status = fo4::forms::set_magic_effect(mutated, *key, *index, *entry);
    if (status != fo4::forms::EffectListWrite::written) {
        return std::unexpected(effectStatusError(status, key->to_string()));
    }
    stagePluginMutationLocked(*loaded, std::move(mutated));
    return nlohmann::json{
        {"index", *index},
        {"formKey", key->to_string()},
        {"message", "Updated effect " + std::to_string(*index) + " in " + pluginName + "."}
    };
}

Result<nlohmann::json> RadiclePluginBackend::removeMagicEffect(
    const nlohmann::json& arguments) {
    const auto pluginName = support::stringArgument(arguments, 0);
    const auto key = support::formKeyArgument(support::stringArgument(arguments, 1));
    if (!key) return std::unexpected(key.error());
    const auto index = sizeArgument(arguments, 2);
    if (!index) return std::unexpected(Error{"InvalidEffectIndex", "effect index is required"});
    if (pluginName.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }
    if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);

    auto mutated = loaded->plugin;
    const auto status = fo4::forms::remove_magic_effect(mutated, *key, *index);
    if (status != fo4::forms::EffectListWrite::written) {
        return std::unexpected(effectStatusError(status, key->to_string()));
    }
    stagePluginMutationLocked(*loaded, std::move(mutated));
    return nlohmann::json{
        {"index", *index},
        {"formKey", key->to_string()},
        {"message", "Removed effect " + std::to_string(*index) + " from " + pluginName + "."}
    };
}

}
