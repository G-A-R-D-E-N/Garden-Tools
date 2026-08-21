#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include <algorithm>
#include <exception>
#include <map>
#include <string>
#include <utility>

#include <fo4/plugin/reader.hpp>
#include <fo4/plugin/writer.hpp>
#include <fo4/plugin/visit.hpp>

namespace fo4ide::plugin {
namespace {

std::string stringArgument(const nlohmann::json& args, std::size_t index) {
    if (!args.is_array() || index >= args.size() || !args[index].is_string()) return {};
    return args[index].get<std::string>();
}

std::string pluginKind(const fo4::Plugin& plugin) {
    if (plugin.header.tes4_flags.is_small_file()) return "light";
    if (plugin.header.tes4_flags.is_master()) return "master";
    return "plugin";
}

std::pair<std::string, std::string> splitTreePath(std::string_view path) {
    const auto slash = path.find_first_of("\\/");
    if (slash == std::string_view::npos) return {std::string(path), {}};
    const auto second = path.find_first_of("\\/", slash + 1);
    if (second == std::string_view::npos) {
        return {std::string(path.substr(0, slash)), std::string(path.substr(slash + 1))};
    }
    return {std::string(path.substr(0, slash)), std::string(path.substr(slash + 1, second - slash - 1))};
}

}

Capabilities RadiclePluginBackend::capabilities() const noexcept {
    return {
        .available = true,
        .readPlugins = true,
        .writePlugins = true,
        .resolveReferences = true,
    };
}

Result<nlohmann::json> RadiclePluginBackend::invoke(std::string_view operation,
                                                     const nlohmann::json& arguments) {
    if (operation == "environment.LoadMo2") return loadEnvironment(arguments);
    if (operation == "appInterop.GetPlugins") return getPlugins();
    if (operation == "appInterop.GetChildren") return getChildren(arguments);
    if (operation == "appInterop.OpenRecord") return openRecord(arguments);
    if (operation == "backend.OpenPlugin") return openPlugin(arguments);
    if (operation == "backend.SavePlugin") return savePlugin(arguments);
    if (operation == "backend.GetEditablePlugins") return getEditablePlugins();
    if (operation == "backend.GetActivePlugins") return getEditablePlugins();
    if (operation == "backend.GetRecordTypeIndex") return getRecordTypeIndex();
    if (operation == "backend.GetLoadOrderSummary") return getLoadOrderSummary();
    if (operation == "backend.GetConflictMatrix") return getConflictMatrix(arguments);
    if (operation == "masters.GetPlugins") return getEditablePlugins();
    if (operation == "masters.List") return listMasters(arguments);

    return std::unexpected(Error{
        "RadicleOperationNotImplemented",
        "Radicle backend operation is not implemented yet: " + std::string(operation)
    });
}

Result<nlohmann::json> RadiclePluginBackend::loadEnvironment(const nlohmann::json& arguments) {
    if (!arguments.is_object() || !arguments.contains("plugins") || !arguments["plugins"].is_array()) {
        return std::unexpected(Error{"InvalidEnvironment", "MO2 environment is missing its plugin list"});
    }

    std::vector<LoadedPlugin> loaded;
    nlohmann::json errors = nlohmann::json::array();

    for (const auto& entry : arguments["plugins"]) {
        if (!entry.is_object() || !entry.value("resolved", false)) continue;
        const auto name = entry.value("name", std::string{});
        const auto path = entry.value("path", std::string{});
        if (name.empty() || path.empty()) continue;

        try {
            auto plugin = fo4::read_plugin_file(std::filesystem::u8path(path));
            loaded.push_back({
                .name = name,
                .path = std::filesystem::u8path(path),
                .source = entry.value("source", std::string{}),
                .plugin = std::move(plugin),
            });
        } catch (const std::exception& error) {
            errors.push_back({{"plugin", name}, {"path", path}, {"error", error.what()}});
        }
    }

    const auto loadedCount = loaded.size();
    {
        std::scoped_lock lock(mutex_);
        plugins_ = std::move(loaded);
    }

    return nlohmann::json{
        {"ok", true},
        {"loadedPlugins", loadedCount},
        {"failedPlugins", errors.size()},
        {"errors", std::move(errors)}
    };
}

Result<nlohmann::json> RadiclePluginBackend::getPlugins() const {
    nlohmann::json rows = nlohmann::json::array();
    std::scoped_lock lock(mutex_);
    for (const auto& loaded : plugins_) {
        rows.push_back({
            {"Key", loaded.name},
            {"ConflictStatus", 0},
            {"IsRecordNode", false},
            {"HasChildren", !loaded.plugin.groups.empty()},
            {"FilePath", loaded.path.string()}
        });
    }
    return nlohmann::json(rows.dump());
}

Result<nlohmann::json> RadiclePluginBackend::getChildren(const nlohmann::json& arguments) const {
    const auto path = stringArgument(arguments, 0);
    if (path.empty()) return nlohmann::json("[]");
    const auto [pluginName, signature] = splitTreePath(path);

    std::scoped_lock lock(mutex_);
    const auto* loaded = findPlugin(pluginName);
    if (!loaded) return nlohmann::json("[]");

    nlohmann::json rows = nlohmann::json::array();
    const auto records = fo4::collect_records(loaded->plugin);

    if (signature.empty()) {
        std::map<std::string, std::size_t> counts;
        for (const auto* record : records) ++counts[record->header.signature.to_string()];
        for (const auto& [sig, count] : counts) {
            rows.push_back({
                {"Key", sig + " (" + std::to_string(count) + ")"},
                {"ConflictStatus", 0},
                {"IsRecordNode", false},
                {"HasChildren", count != 0},
                {"FilePath", loaded->path.string()}
            });
        }
        return nlohmann::json(rows.dump());
    }

    const auto space = signature.find(' ');
    const auto normalizedSignature = signature.substr(0, space);
    for (const auto* record : records) {
        if (record->header.signature.to_string() != normalizedSignature) continue;
        const auto key = record->form_key.owner().empty()
            ? ("0x" + std::to_string(record->header.form_id.value()))
            : record->form_key.to_string();
        rows.push_back({
            {"Key", key},
            {"ConflictStatus", 0},
            {"IsRecordNode", true},
            {"HasChildren", false},
            {"FilePath", loaded->path.string()}
        });
    }
    return nlohmann::json(rows.dump());
}

Result<nlohmann::json> RadiclePluginBackend::openRecord(const nlohmann::json& arguments) const {
    const auto path = stringArgument(arguments, 0);
    if (path.empty()) return nlohmann::json("");
    const auto last = path.find_last_of("\\/");
    if (last == std::string::npos || last + 1 >= path.size()) return nlohmann::json("");
    return nlohmann::json(path.substr(last + 1));
}

Result<nlohmann::json> RadiclePluginBackend::openPlugin(const nlohmann::json& arguments) {
    const auto path = stringArgument(arguments, 0);
    if (path.empty()) return std::unexpected(Error{"PluginPathMissing", "Plugin path is empty"});

    try {
        auto plugin = fo4::read_plugin_file(std::filesystem::u8path(path));
        std::string name = plugin.key.filename().empty()
            ? std::filesystem::u8path(path).filename().string()
            : std::string(plugin.key.filename());
        std::scoped_lock lock(mutex_);
        auto* existing = findPlugin(name);
        if (existing) {
            existing->path = std::filesystem::u8path(path);
            existing->plugin = std::move(plugin);
        } else {
            plugins_.push_back({name, std::filesystem::u8path(path), {}, std::move(plugin)});
        }
        return nlohmann::json{{"ok", true}, {"plugin", name}}.dump();
    } catch (const std::exception& error) {
        return std::unexpected(Error{"PluginReadFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::savePlugin(const nlohmann::json& arguments) {
    const auto name = stringArgument(arguments, 0);
    const auto requestedPath = stringArgument(arguments, 1);
    if (name.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});

    std::scoped_lock lock(mutex_);
    auto* loaded = findPlugin(name);
    if (!loaded) return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + name});
    const auto output = requestedPath.empty() ? loaded->path : std::filesystem::u8path(requestedPath);
    try {
        fo4::write_plugin_file(loaded->plugin, output);
        loaded->path = output;
        return nlohmann::json{{"ok", true}, {"plugin", name}, {"path", output.string()}}.dump();
    } catch (const std::exception& error) {
        return std::unexpected(Error{"PluginWriteFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::getEditablePlugins() const {
    nlohmann::json rows = nlohmann::json::array();
    std::scoped_lock lock(mutex_);
    for (std::size_t index = 0; index < plugins_.size(); ++index) {
        const auto& loaded = plugins_[index];
        rows.push_back({
            {"Name", loaded.name},
            {"Kind", pluginKind(loaded.plugin)},
            {"LoadOrder", index},
            {"Editable", true},
            {"Size", loaded.plugin.original_bytes.size()}
        });
    }
    return nlohmann::json(rows.dump());
}

Result<nlohmann::json> RadiclePluginBackend::getRecordTypeIndex() const {
    std::map<std::string, std::size_t> counts;
    std::scoped_lock lock(mutex_);
    for (const auto& loaded : plugins_) {
        for (const auto* record : fo4::collect_records(loaded.plugin)) {
            ++counts[record->header.signature.to_string()];
        }
    }

    nlohmann::json rows = nlohmann::json::array();
    for (const auto& [signature, count] : counts) {
        rows.push_back({{"Type", signature}, {"FriendlyName", signature}, {"Count", count}});
    }
    return nlohmann::json(rows.dump());
}

Result<nlohmann::json> RadiclePluginBackend::getLoadOrderSummary() const {
    std::size_t totalRecords = 0;
    nlohmann::json names = nlohmann::json::array();
    std::scoped_lock lock(mutex_);
    for (const auto& loaded : plugins_) {
        names.push_back(loaded.name);
        totalRecords += fo4::collect_records(loaded.plugin).size();
    }
    return nlohmann::json(nlohmann::json{
        {"TotalRecords", totalRecords},
        {"PluginCount", plugins_.size()},
        {"Plugins", std::move(names)}
    }.dump());
}

Result<nlohmann::json> RadiclePluginBackend::getConflictMatrix(const nlohmann::json& arguments) const {
    const auto formKey = stringArgument(arguments, 0);
    if (formKey.empty()) return nlohmann::json("");

    std::scoped_lock lock(mutex_);
    for (const auto& loaded : plugins_) {
        for (const auto* record : fo4::collect_records(loaded.plugin)) {
            if (record->form_key.owner().empty() || record->form_key.to_string() != formKey) continue;
            const auto signature = record->header.signature.to_string();
            return nlohmann::json(nlohmann::json{
                {"FormKey", formKey},
                {"EditorID", ""},
                {"Type", signature},
                {"Winner", loaded.name},
                {"Plugins", nlohmann::json::array({loaded.name})},
                {"Rows", nlohmann::json::array()},
                {"Level", "none"}
            }.dump());
        }
    }
    return nlohmann::json("");
}

Result<nlohmann::json> RadiclePluginBackend::listMasters(const nlohmann::json& arguments) const {
    const auto name = stringArgument(arguments, 0);
    if (name.empty()) return nlohmann::json("{}");

    std::scoped_lock lock(mutex_);
    const auto* loaded = findPlugin(name);
    if (!loaded) return nlohmann::json("{}");

    nlohmann::json masters = nlohmann::json::array();
    for (std::size_t index = 0; index < loaded->plugin.header.masters.size(); ++index) {
        const auto& master = loaded->plugin.header.masters[index];
        masters.push_back({
            {"index", index},
            {"name", std::string(master.key.filename())},
            {"size", master.data_size},
            {"used", true}
        });
    }
    return nlohmann::json(nlohmann::json{
        {"pluginName", loaded->name},
        {"masters", std::move(masters)},
        {"light", loaded->plugin.header.tes4_flags.is_small_file()}
    }.dump());
}

RadiclePluginBackend::LoadedPlugin* RadiclePluginBackend::findPlugin(std::string_view name) {
    const auto it = std::find_if(plugins_.begin(), plugins_.end(),
                                 [name](const LoadedPlugin& plugin) { return plugin.name == name; });
    return it == plugins_.end() ? nullptr : &*it;
}

const RadiclePluginBackend::LoadedPlugin* RadiclePluginBackend::findPlugin(std::string_view name) const {
    const auto it = std::find_if(plugins_.begin(), plugins_.end(),
                                 [name](const LoadedPlugin& plugin) { return plugin.name == name; });
    return it == plugins_.end() ? nullptr : &*it;
}

}
