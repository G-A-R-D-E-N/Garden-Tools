#include "RadiclePluginSupport.hpp"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <string>
#include <utility>
#include <vector>

#include <fo4/plugin/itpo.hpp>
#include <fo4/plugin/reader.hpp>

namespace fo4ide::plugin {

Result<nlohmann::json> RadiclePluginBackend::findIdenticalToPreviousOverride(
    const nlohmann::json& arguments) const {
    const auto name = support::stringArgument(arguments, 0);
    if (name.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    const auto limit = support::integerArgument<std::size_t>(arguments, 1).value_or(0);

    OpLock lock(*this, OpLock::Mode::Read);
    const auto* loaded = findPlugin(name);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + name});
    }
    std::vector<const fo4::Plugin*> loadOrder;
    loadOrder.reserve(plugins_.size());
    std::size_t index = plugins_.size();
    for (std::size_t position = 0; position < plugins_.size(); ++position) {
        loadOrder.push_back(&plugins_[position].plugin);
        if (&plugins_[position] == loaded) index = position;
    }
    if (index == plugins_.size()) {
        return std::unexpected(Error{"PluginStateInvalid", "Loaded plugin index could not be resolved"});
    }

    try {
        const auto found = fo4::find_identical_to_previous_override(loadOrder, index, limit);
        if (found.empty()) {
            return nlohmann::json("No identical-to-previous-override records in " + name + ".");
        }
        std::string text = std::to_string(found.size()) +
                           " identical-to-previous-override record(s) in " + name + ":";
        for (const auto& item : found) {
            text += "\n  " + item.signature.to_string() + " " + item.key.to_string() +
                    " -- identical to " + std::string(item.previous.filename());
        }
        return nlohmann::json(std::move(text));
    } catch (const std::exception& error) {
        return std::unexpected(Error{"FindIdenticalToPreviousOverrideFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::removeIdenticalToPreviousOverride(
    const nlohmann::json& arguments) {
    const auto name = support::stringArgument(arguments, 0);
    if (name.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    const bool apply = support::boolArgument(arguments, 1, false);

    OpLock lock(*this, OpLock::Mode::Write, {name});
    auto* loaded = findPlugin(name);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + name});
    }
    if (apply) {
        if (const auto guard = writeGuard(*loaded)) return std::unexpected(*guard);
    }
    std::vector<const fo4::Plugin*> loadOrder;
    loadOrder.reserve(plugins_.size());
    std::size_t index = plugins_.size();
    for (std::size_t position = 0; position < plugins_.size(); ++position) {
        loadOrder.push_back(&plugins_[position].plugin);
        if (&plugins_[position] == loaded) index = position;
    }
    if (index == plugins_.size()) {
        return std::unexpected(Error{"PluginStateInvalid", "Loaded plugin index could not be resolved"});
    }

    try {
        const auto found = fo4::find_identical_to_previous_override(loadOrder, index, 0, true);
        if (found.empty()) {
            return nlohmann::json("No identical-to-previous-override records in " + name + ".");
        }
        if (!apply) {
            std::string preview = "DRY RUN: " + std::to_string(found.size()) +
                                  " identical-to-previous-override record(s) in " + name + ".";
            const auto shown = std::min<std::size_t>(found.size(), 50);
            for (std::size_t position = 0; position < shown; ++position) {
                const auto& item = found[position];
                preview += "\n  " + item.signature.to_string() + " " + item.key.to_string() +
                           " -- identical to " + std::string(item.previous.filename());
            }
            if (found.size() > shown) {
                preview += "\n  ... and " + std::to_string(found.size() - shown) + " more";
            }
            preview += "\nRe-run with apply=true to remove them.";
            return nlohmann::json(std::move(preview));
        }

        auto mutated = loaded->plugin;
        const auto removed = fo4::remove_identical_to_previous_override(mutated, loadOrder, index);
        if (removed == 0) {
            return nlohmann::json("No identical-to-previous-override records in " + name + ".");
        }
        stagePluginMutationLocked(*loaded, std::move(mutated));
        return nlohmann::json(
            "Removed " + std::to_string(removed) +
            " identical-to-previous-override record(s) from " + name + ". SavePlugin to persist it.");
    } catch (const std::exception& error) {
        return std::unexpected(Error{"RemoveIdenticalToPreviousOverrideFailed", error.what()});
    }
}

}
