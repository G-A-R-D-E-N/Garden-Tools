#include "RadiclePluginSupport.hpp"

#include <fo4/environment/package_procedure.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <utility>

namespace fo4ide::plugin {

namespace {

std::string packageTreeFailure(fo4::env::PackageTreeMutationStatus status) {
    switch (status) {
        case fo4::env::PackageTreeMutationStatus::updated:
        case fo4::env::PackageTreeMutationStatus::unchanged:
            return {};
        default:
            return fo4::env::package_tree_mutation_status_message(status);
    }
}

std::optional<fo4::FormKey> formKeyArgument(const std::string& text) {
    if (text.empty()) return std::nullopt;
    try {
        return fo4::FormKey::parse(text);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

}  // namespace

Result<nlohmann::json> RadiclePluginBackend::addPackageProcedure(const nlohmann::json& arguments) {
    const auto pluginName = support::stringArgument(arguments, 0);
    const auto formKeyText = support::stringArgument(arguments, 1);
    const auto parentIndex = support::integerArgument<std::uint32_t>(arguments, 2);
    const auto procedure = support::stringArgument(arguments, 3);
    if (pluginName.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    if (formKeyText.empty()) return std::unexpected(Error{"FormKeyMissing", "Record FormKey is empty"});
    if (!parentIndex) return std::unexpected(Error{"ParentIndexMissing", "Parent node index is required"});
    if (procedure.empty()) return std::unexpected(Error{"InvalidProcedure", "Procedure name is empty"});
    const auto key = formKeyArgument(formKeyText);
    if (!key) return std::unexpected(Error{"InvalidFormKey", "Record FormKey is invalid"});

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }
    if (!isEditable(*loaded)) {
        return std::unexpected(Error{"PluginReadOnly", "Refusing to edit protected or game-Data plugin: " + loaded->name});
    }
    if (!fileUnchangedSinceLoad(*loaded)) {
        return std::unexpected(Error{"PluginChangedOnDisk",
                                     "Plugin was modified on disk after it was loaded; reload it before writing: " +
                                         loaded->path.string()});
    }

    std::size_t added = 0;
    try {
        auto mutated = loaded->plugin;
        fo4::assign_record_form_keys(mutated);
        const auto status = fo4::env::add_package_procedure_node(mutated, *key, *parentIndex, procedure, &added);
        if (const auto failure = packageTreeFailure(status); !failure.empty()) {
            return std::unexpected(Error{"PackageWriteFailed", failure});
        }
        stagePluginMutationLocked(*loaded, std::move(mutated));
    } catch (const std::exception& error) {
        return std::unexpected(Error{"PackageWriteFailed", error.what()});
    }
    return nlohmann::json("Added procedure '" + procedure + "' at node " + std::to_string(added) + " of " +
                          formKeyText + " in " + pluginName + ". SavePlugin to persist.");
}

Result<nlohmann::json> RadiclePluginBackend::removePackageProcedure(const nlohmann::json& arguments) {
    const auto pluginName = support::stringArgument(arguments, 0);
    const auto formKeyText = support::stringArgument(arguments, 1);
    const auto nodeIndex = support::integerArgument<std::uint32_t>(arguments, 2);
    if (pluginName.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    if (formKeyText.empty()) return std::unexpected(Error{"FormKeyMissing", "Record FormKey is empty"});
    if (!nodeIndex) return std::unexpected(Error{"NodeIndexMissing", "Node index is required"});
    const auto key = formKeyArgument(formKeyText);
    if (!key) return std::unexpected(Error{"InvalidFormKey", "Record FormKey is invalid"});

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }
    if (!isEditable(*loaded)) {
        return std::unexpected(Error{"PluginReadOnly", "Refusing to edit protected or game-Data plugin: " + loaded->name});
    }
    if (!fileUnchangedSinceLoad(*loaded)) {
        return std::unexpected(Error{"PluginChangedOnDisk",
                                     "Plugin was modified on disk after it was loaded; reload it before writing: " +
                                         loaded->path.string()});
    }

    try {
        auto mutated = loaded->plugin;
        fo4::assign_record_form_keys(mutated);
        const auto status = fo4::env::remove_package_procedure_node(mutated, *key, *nodeIndex);
        if (const auto failure = packageTreeFailure(status); !failure.empty()) {
            return std::unexpected(Error{"PackageWriteFailed", failure});
        }
        stagePluginMutationLocked(*loaded, std::move(mutated));
    } catch (const std::exception& error) {
        return std::unexpected(Error{"PackageWriteFailed", error.what()});
    }
    return nlohmann::json("Removed procedure node " + std::to_string(*nodeIndex) + " from " + formKeyText +
                          " in " + pluginName + ". SavePlugin to persist.");
}

}  // namespace fo4ide::plugin
