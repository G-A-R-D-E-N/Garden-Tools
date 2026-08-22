#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include <exception>
#include <filesystem>
#include <string>
#include <string_view>

#include <fo4/plugin/mutation.hpp>
#include <fo4/plugin/writer.hpp>
#include <fo4/schema/form_links.hpp>
#include <fo4/signature.hpp>

namespace fo4ide::plugin {
namespace {

std::string lifecycleStringArgument(const nlohmann::json& args, std::size_t index) {
    if (!args.is_array() || index >= args.size() || !args[index].is_string()) return {};
    return args[index].get<std::string>();
}

}

Result<nlohmann::json> RadiclePluginBackend::createRecord(const nlohmann::json& arguments) {
    const auto pluginName = lifecycleStringArgument(arguments, 0);
    const auto signatureText = lifecycleStringArgument(arguments, 1);
    if (pluginName.empty()) {
        return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    }
    if (signatureText.size() != 4) {
        return std::unexpected(Error{
            "InvalidRecordSignature",
            "Record signature must contain exactly four characters"
        });
    }

    std::scoped_lock lock(mutex_);
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }
    if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);
    if (loaded->plugin.header.tes4_flags.is_small_file()) {
        return std::unexpected(Error{
            "LightPluginLifecycleUnsupported",
            "Creating records in light plugins is disabled until the engine's small-file FormID predicate is proven"
        });
    }

    try {
        auto mutated = loaded->plugin;
        const auto key = fo4::create_local_record(
            mutated, fo4::Signature::from_string(signatureText));
        fo4::write_plugin_file(mutated, loaded->path);
        loaded->plugin = std::move(mutated);
        {
            std::error_code ec;
            loaded->loadedTime = std::filesystem::last_write_time(loaded->path, ec);
        }
        rebuildGraphLocked();
        return nlohmann::json(nlohmann::json{
            {"ok", true},
            {"plugin", pluginName},
            {"signature", signatureText},
            {"formKey", key.to_string()}
        }.dump());
    } catch (const std::exception& error) {
        return std::unexpected(Error{"RecordCreateFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::deleteRecord(const nlohmann::json& arguments) {
    const auto pluginName = lifecycleStringArgument(arguments, 0);
    const auto formKeyText = lifecycleStringArgument(arguments, 1);
    if (pluginName.empty()) {
        return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    }
    if (formKeyText.empty()) {
        return std::unexpected(Error{"FormKeyMissing", "Record FormKey is empty"});
    }

    fo4::FormKey key;
    try {
        key = fo4::FormKey::parse(formKeyText);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InvalidFormKey", error.what()});
    }

    std::scoped_lock lock(mutex_);
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }
    if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);
    if (key.owner() != loaded->plugin.key) {
        return std::unexpected(Error{
            "RecordNotLocal",
            "Only records owned by the target plugin can be deleted: " + formKeyText
        });
    }

    try {
        // The graph is only as complete as the schema scanner. Strictly walk
        // every loaded plugin first so unsupported VMAD or malformed NAVM/NAVI
        // cannot hide an inbound reference to the record being deleted.
        for (const auto& candidate : plugins_) {
            auto coverageProbe = candidate.plugin;
            try {
                fo4::rekey_form_links(coverageProbe, key, key);
            } catch (const std::exception& error) {
                return std::unexpected(Error{
                    "ReferenceCoverageIncomplete",
                    "Cannot prove delete safety while scanning " + candidate.name + ": " + error.what()
                });
            }
        }

        for (const auto& version : graph_.versions(key)) {
            if (version.plugin != loaded->plugin.key) {
                return std::unexpected(Error{
                    "RecordHasExternalVersions",
                    "Cannot delete a record that has an override in another loaded plugin: " +
                        std::string(version.plugin.filename())
                });
            }
        }
        if (!graph_.referenced_by(key).empty()) {
            return std::unexpected(Error{
                "RecordReferenced",
                "Cannot delete a record while loaded records still reference it: " + formKeyText
            });
        }

        auto mutated = loaded->plugin;
        const auto status = fo4::delete_local_record(mutated, key);
        switch (status) {
            case fo4::RecordDeleteStatus::record_missing:
                return std::unexpected(Error{"RecordNotFound", "Record is not present: " + formKeyText});
            case fo4::RecordDeleteStatus::not_local:
                return std::unexpected(Error{"RecordNotLocal", "Record is not owned by " + pluginName});
            case fo4::RecordDeleteStatus::referenced:
                return std::unexpected(Error{"RecordReferenced", "Record is still referenced: " + formKeyText});
            case fo4::RecordDeleteStatus::deleted:
                break;
        }

        fo4::write_plugin_file(mutated, loaded->path);
        loaded->plugin = std::move(mutated);
        {
            std::error_code ec;
            loaded->loadedTime = std::filesystem::last_write_time(loaded->path, ec);
        }
        rebuildGraphLocked();
        return nlohmann::json(nlohmann::json{
            {"ok", true},
            {"plugin", pluginName},
            {"deleted", formKeyText}
        }.dump());
    } catch (const std::exception& error) {
        return std::unexpected(Error{"RecordDeleteFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::renumberRecord(const nlohmann::json& arguments) {
    const auto pluginName = lifecycleStringArgument(arguments, 0);
    const auto oldFormKeyText = lifecycleStringArgument(arguments, 1);
    const auto newFormKeyText = lifecycleStringArgument(arguments, 2);
    if (pluginName.empty()) {
        return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    }
    if (oldFormKeyText.empty() || newFormKeyText.empty()) {
        return std::unexpected(Error{"FormKeyMissing", "Old and new record FormKeys are required"});
    }

    fo4::FormKey oldKey;
    fo4::FormKey newKey;
    try {
        oldKey = fo4::FormKey::parse(oldFormKeyText);
        newKey = fo4::FormKey::parse(newFormKeyText);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InvalidFormKey", error.what()});
    }

    std::scoped_lock lock(mutex_);
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }
    if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);
    if (loaded->plugin.header.tes4_flags.is_small_file()) {
        return std::unexpected(Error{
            "LightPluginLifecycleUnsupported",
            "Renumbering records in light plugins is disabled until the engine's small-file FormID predicate is proven"
        });
    }
    if (oldKey.owner() != loaded->plugin.key || newKey.owner() != loaded->plugin.key) {
        return std::unexpected(Error{
            "RecordNotLocal",
            "Renumbering requires both old and new FormKeys to be owned by " + pluginName
        });
    }

    try {
        for (const auto& candidate : plugins_) {
            auto coverageProbe = candidate.plugin;
            try {
                fo4::rekey_form_links(coverageProbe, oldKey, oldKey);
            } catch (const std::exception& error) {
                return std::unexpected(Error{
                    "ReferenceCoverageIncomplete",
                    "Cannot prove renumber safety while scanning " + candidate.name + ": " + error.what()
                });
            }
        }

        for (const auto& version : graph_.versions(oldKey)) {
            if (version.plugin != loaded->plugin.key) {
                return std::unexpected(Error{
                    "RecordHasExternalVersions",
                    "Cannot renumber a record that has an override in another loaded plugin: " +
                        std::string(version.plugin.filename())
                });
            }
        }
        for (const auto& reference : graph_.referenced_by(oldKey)) {
            if (reference.plugin != loaded->plugin.key) {
                return std::unexpected(Error{
                    "RecordReferencedExternally",
                    "Cannot renumber a record referenced by another loaded plugin: " +
                        std::string(reference.plugin.filename())
                });
            }
        }

        auto mutated = loaded->plugin;
        const auto status = fo4::renumber_local_record(mutated, oldKey, newKey.local());
        switch (status) {
            case fo4::RecordRenumberStatus::record_missing:
                return std::unexpected(Error{"RecordNotFound", "Record is not present: " + oldFormKeyText});
            case fo4::RecordRenumberStatus::not_local:
                return std::unexpected(Error{"RecordNotLocal", "Record is not owned by " + pluginName});
            case fo4::RecordRenumberStatus::target_exists:
                return std::unexpected(Error{"RecordFormIdExists", "Target FormKey already exists: " + newFormKeyText});
            case fo4::RecordRenumberStatus::unchanged:
                return nlohmann::json(nlohmann::json{
                    {"ok", true},
                    {"plugin", pluginName},
                    {"oldFormKey", oldFormKeyText},
                    {"newFormKey", newFormKeyText},
                    {"changed", false}
                }.dump());
            case fo4::RecordRenumberStatus::renumbered:
                break;
        }

        fo4::write_plugin_file(mutated, loaded->path);
        loaded->plugin = std::move(mutated);
        {
            std::error_code ec;
            loaded->loadedTime = std::filesystem::last_write_time(loaded->path, ec);
        }
        rebuildGraphLocked();
        return nlohmann::json(nlohmann::json{
            {"ok", true},
            {"plugin", pluginName},
            {"oldFormKey", oldFormKeyText},
            {"newFormKey", newFormKeyText},
            {"changed", true}
        }.dump());
    } catch (const std::exception& error) {
        return std::unexpected(Error{"RecordRenumberFailed", error.what()});
    }
}

}