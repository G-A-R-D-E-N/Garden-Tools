#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include <cstddef>
#include <exception>
#include <string>
#include <utility>

#include <fo4/plugin/mutation.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/plugin/writer.hpp>
#include <fo4/schema/scalar.hpp>

namespace fo4ide::plugin {
namespace {

std::string stringArgument(const nlohmann::json& args, std::size_t index) {
    if (!args.is_array() || index >= args.size() || !args[index].is_string()) return {};
    return args[index].get<std::string>();
}

constexpr auto k_refr = fo4::Signature::from_string("REFR");
constexpr auto k_xhlt = fo4::Signature::from_string("XHLT");

}

Result<nlohmann::json> RadiclePluginBackend::setFieldValue(
    const nlohmann::json& arguments) {
    const auto field = stringArgument(arguments, 2);
    if (field != "REFR\\HealthPercent") {
        return setField(arguments);
    }

    const auto pluginName = stringArgument(arguments, 0);
    const auto formKeyText = stringArgument(arguments, 1);
    const auto value = stringArgument(arguments, 3);
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
        return std::unexpected(Error{
            "PluginNotLoaded",
            "Plugin is not loaded: " + pluginName
        });
    }
    if (!isEditable(*loaded)) {
        return std::unexpected(Error{
            "PluginReadOnly",
            "Refusing to edit protected or game-Data plugin: " + loaded->name
        });
    }

    const auto* record = fo4::find_record(loaded->plugin, key);
    if (record == nullptr) {
        return std::unexpected(Error{
            "RecordNotFound",
            "Record is not present in " + pluginName + ": " + formKeyText
        });
    }
    if (record->header.signature != k_refr) {
        return std::unexpected(Error{
            "ScalarFieldRecordMismatch",
            "REFR\\HealthPercent can only be written on a REFR record"
        });
    }

    try {
        auto mutated = loaded->plugin;
        const auto status = fo4::set_record_scalar(mutated, key, k_xhlt, value);
        switch (status) {
            case fo4::ScalarMutationStatus::record_missing:
                return std::unexpected(Error{
                    "RecordNotFound",
                    "Record is not present in " + pluginName + ": " + formKeyText
                });
            case fo4::ScalarMutationStatus::subrecord_missing:
                return std::unexpected(Error{
                    "ScalarFieldMissing",
                    "REFR has no XHLT subrecord; Radicle will not guess a missing subrecord insertion position"
                });
            case fo4::ScalarMutationStatus::updated:
            case fo4::ScalarMutationStatus::unchanged:
                break;
        }

        if (status == fo4::ScalarMutationStatus::updated) {
            fo4::write_plugin_file(mutated, loaded->path);
            loaded->plugin = std::move(mutated);
            rebuildGraphLocked();
        }
        return nlohmann::json(
            "Updated REFR Health % for " + formKeyText + " in " + pluginName + ".");
    } catch (const fo4::schema::ScalarError& error) {
        return std::unexpected(Error{"ScalarFieldWriteFailed", error.what()});
    } catch (const std::exception& error) {
        return std::unexpected(Error{"FieldWriteFailed", error.what()});
    }
}

}  // namespace fo4ide::plugin
