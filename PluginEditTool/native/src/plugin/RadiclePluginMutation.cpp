#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include <algorithm>
#include <cctype>
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

bool boolArgument(const nlohmann::json& args, std::size_t index, bool& value) {
    if (!args.is_array() || index >= args.size() || !args[index].is_boolean()) return false;
    value = args[index].get<bool>();
    return true;
}

std::string asciiLower(std::string_view value) {
    std::string out(value);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return out;
}

std::string pluginKind(const fo4::Plugin& plugin) {
    if (plugin.header.tes4_flags.is_small_file()) return "light";
    if (plugin.header.tes4_flags.is_master()) return "master";
    return "plugin";
}

bool officialPluginName(std::string_view name) {
    const auto lower = asciiLower(name);
    if (lower == "fallout4.esm" ||
        lower == "dlcrobot.esm" ||
        lower == "dlccoast.esm" ||
        lower == "dlcworkshop01.esm" ||
        lower == "dlcworkshop02.esm" ||
        lower == "dlcworkshop03.esm" ||
        lower == "dlcnukaworld.esm" ||
        lower == "dlcultrahighresolution.esm") {
        return true;
    }
    return lower.starts_with("cc") &&
           (lower.ends_with(".esm") || lower.ends_with(".esp") || lower.ends_with(".esl"));
}

}

bool RadiclePluginBackend::isEditable(const LoadedPlugin& loaded) const {
    if (asciiLower(loaded.source) == "data") return false;
    return !officialPluginName(loaded.name);
}

Result<nlohmann::json> RadiclePluginBackend::invokeBackend(
    std::string_view method,
    const nlohmann::json& arguments) {
    if (method == "SetField") return setField(arguments);
    if (method == "CopyAsOverride") return copyAsOverride(arguments);
    if (method == "CopyAsOverrideMany") return copyAsOverrideMany(arguments);

    if (method == "GetEditablePlugins") {
        nlohmann::json rows = nlohmann::json::array();
        std::scoped_lock lock(mutex_);
        for (const auto& loaded : plugins_) {
            if (isEditable(loaded)) rows.push_back(loaded.name);
        }
        return nlohmann::json(rows.dump());
    }

    if (method == "GetActivePlugins") {
        nlohmann::json rows = nlohmann::json::array();
        std::scoped_lock lock(mutex_);
        for (std::size_t index = 0; index < plugins_.size(); ++index) {
            const auto& loaded = plugins_[index];
            rows.push_back({
                {"Name", loaded.name},
                {"Kind", pluginKind(loaded.plugin)},
                {"LoadOrder", index},
                {"Editable", isEditable(loaded)},
                {"Size", loaded.plugin.original_bytes.size()}
            });
        }
        return nlohmann::json(rows.dump());
    }

    if (method == "SavePlugin") {
        const auto name = stringArgument(arguments, 0);
        {
            std::scoped_lock lock(mutex_);
            const auto* loaded = findPlugin(name);
            if (loaded != nullptr && !isEditable(*loaded)) {
                return std::unexpected(Error{
                    "PluginReadOnly",
                    "Refusing to write protected or game-Data plugin: " + loaded->name
                });
            }
        }
    }

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
    if (!isEditable(*loaded)) {
        return std::unexpected(Error{
            "PluginReadOnly",
            "Refusing to edit protected or game-Data plugin: " + loaded->name
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

Result<nlohmann::json> RadiclePluginBackend::copyAsOverride(
    const nlohmann::json& arguments) {
    const auto sourceName = stringArgument(arguments, 0);
    const auto formKeyText = stringArgument(arguments, 1);
    const auto targetName = stringArgument(arguments, 2);
    bool overwrite = false;

    if (sourceName.empty()) {
        return std::unexpected(Error{"SourcePluginMissing", "Source plugin name is empty"});
    }
    if (formKeyText.empty()) {
        return std::unexpected(Error{"FormKeyMissing", "Record FormKey is empty"});
    }
    if (targetName.empty()) {
        return std::unexpected(Error{"TargetPluginMissing", "Target plugin name is empty"});
    }
    if (!boolArgument(arguments, 3, overwrite)) {
        return std::unexpected(Error{"InvalidOverwriteFlag", "Overwrite flag must be boolean"});
    }

    fo4::FormKey key;
    try {
        key = fo4::FormKey::parse(formKeyText);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InvalidFormKey", error.what()});
    }

    std::scoped_lock lock(mutex_);
    const auto* source = findPlugin(sourceName);
    if (source == nullptr) {
        return std::unexpected(Error{
            "SourcePluginNotLoaded",
            "Source plugin is not loaded: " + sourceName
        });
    }
    auto* target = findPlugin(targetName);
    if (target == nullptr) {
        return std::unexpected(Error{
            "TargetPluginNotLoaded",
            "Target plugin is not loaded yet: " + targetName
        });
    }
    if (!isEditable(*target)) {
        return std::unexpected(Error{
            "PluginReadOnly",
            "Refusing to write override into protected or game-Data plugin: " + target->name
        });
    }

    try {
        auto mutated = target->plugin;
        const auto status = fo4::copy_record_as_override(mutated, source->plugin, key, overwrite);
        switch (status) {
            case fo4::RecordCopyStatus::source_missing:
                return std::unexpected(Error{
                    "RecordNotFound",
                    "Record is not present in " + sourceName + ": " + formKeyText
                });
            case fo4::RecordCopyStatus::already_exists:
                return nlohmann::json(
                    "EXISTS: " + targetName + " already contains " + formKeyText + ".");
            case fo4::RecordCopyStatus::inserted:
            case fo4::RecordCopyStatus::replaced:
                break;
        }

        fo4::write_plugin_file(mutated, target->path);
        target->plugin = std::move(mutated);
        rebuildGraphLocked();

        return nlohmann::json(
            std::string(status == fo4::RecordCopyStatus::replaced ? "Replaced " : "Copied ") +
            formKeyText + " as override in " + targetName + ".");
    } catch (const std::exception& error) {
        return std::unexpected(Error{"CopyOverrideFailed", error.what()});
    }
}

}
