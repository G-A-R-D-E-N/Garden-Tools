#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include <exception>
#include <string>
#include <string_view>
#include <utility>

#include <fo4/plugin/mutation.hpp>
#include <fo4/plugin/writer.hpp>

namespace fo4ide::plugin {
namespace {

std::string stringArgument(const nlohmann::json& args, std::size_t index) {
    if (!args.is_array() || index >= args.size() || !args[index].is_string()) return {};
    return args[index].get<std::string>();
}

}

Result<nlohmann::json> RadiclePluginBackend::invokeBackend(
    std::string_view method,
    const nlohmann::json& arguments) {
    if (method == "SetField") return setField(arguments);
    return invoke("backend." + std::string(method), arguments);
}

Result<nlohmann::json> RadiclePluginBackend::setField(const nlohmann::json& arguments) {
    const auto pluginName = stringArgument(arguments, 0);
    const auto formKeyText = stringArgument(arguments, 1);
    const auto field = stringArgument(arguments, 2);
    const auto value = stringArgument(arguments, 3);

    if (pluginName.empty()) {
        return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    }
    if (formKeyText.empty()) {
        return std::unexpected(Error{"FormKeyMissing", "Record FormKey is empty"});
    }
    if (field != "Subrecords\\EDID[0]" && field != "EDID") {
        return std::unexpected(Error{
            "FieldWriteUnsupported",
            "Native Radicle editing currently supports EDID only"
        });
    }
    if (value.find('\0') != std::string::npos) {
        return std::unexpected(Error{"InvalidEditorId", "EDID cannot contain an embedded NUL"});
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

    try {
        auto mutated = loaded->plugin;
        if (!fo4::set_record_editor_id(mutated, key, value)) {
            return std::unexpected(Error{
                "RecordNotFound",
                "Record is not present in " + pluginName + ": " + formKeyText
            });
        }

        fo4::write_plugin_file(mutated, loaded->path);
        loaded->plugin = std::move(mutated);
        rebuildGraphLocked();
        return nlohmann::json(
            "Updated EDID for " + formKeyText + " in " + pluginName + ".");
    } catch (const std::exception& error) {
        return std::unexpected(Error{"FieldWriteFailed", error.what()});
    }
}

}
