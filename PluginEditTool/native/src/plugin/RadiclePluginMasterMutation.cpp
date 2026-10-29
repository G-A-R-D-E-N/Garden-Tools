#include "RadiclePluginSupport.hpp"
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include <algorithm>
#include <cctype>
#include <exception>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fo4/plugin/writer.hpp>

namespace fo4ide::plugin {
namespace {

std::string trim(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    return value;
}

std::vector<std::string> splitCsv(std::string_view csv) {
    std::vector<std::string> values;
    std::size_t begin = 0;
    while (begin <= csv.size()) {
        const auto comma = csv.find(',', begin);
        const auto end = comma == std::string_view::npos ? csv.size() : comma;
        auto value = trim(std::string(csv.substr(begin, end - begin)));
        if (!value.empty()) values.push_back(std::move(value));
        if (comma == std::string_view::npos) break;
        begin = comma + 1;
    }
    return values;
}

}

Result<nlohmann::json> RadiclePluginBackend::addMasters(const nlohmann::json& arguments) {
    const auto pluginName = support::stringArgument(arguments, 0);
    const auto csv = support::stringArgument(arguments, 1);
    if (pluginName.empty()) {
        return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    }
    if (csv.empty()) {
        return std::unexpected(Error{"MastersMissing", "No master filenames were supplied"});
    }

    const auto requestedNames = splitCsv(csv);
    if (requestedNames.empty()) {
        return std::unexpected(Error{"MastersMissing", "No usable master filenames were supplied"});
    }

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* target = findPlugin(pluginName);
    if (target == nullptr) {
        return std::unexpected(Error{
            "PluginNotLoaded",
            "Plugin is not loaded: " + pluginName
        });
    }
    if (!isEditable(*target)) {
        return std::unexpected(Error{
            "PluginReadOnly",
            "Refusing to add masters to protected or game-Data plugin: " + target->name
        });
    }
    if (!fileUnchangedSinceLoad(*target)) {
        return std::unexpected(Error{
            "PluginChangedOnDisk",
            "Plugin was modified on disk after it was loaded; reload it before writing: " +
                target->path.string()
        });
    }

    std::vector<fo4::MasterDescriptor> masters;
    masters.reserve(target->plugin.header.masters.size() + requestedNames.size());
    std::set<fo4::PluginKey> seen;
    for (const auto& master : target->plugin.header.masters) {
        masters.push_back({master.key, master.kind});
        seen.insert(master.key);
    }

    std::size_t added = 0;
    for (const auto& name : requestedNames) {
        auto key = fo4::PluginKey::from_filename(name);
        if (key.empty()) continue;
        if (key == target->plugin.key) {
            return std::unexpected(Error{
                "SelfMaster",
                "A plugin cannot declare itself as a master: " + std::string(key.filename())
            });
        }
        if (!seen.insert(key).second) continue;

        const auto* loadedMaster = findPlugin(key.filename());
        if (loadedMaster == nullptr && !key.looks_light()) {
            return std::unexpected(Error{
                "MasterNotLoaded",
                "Cannot determine full/light kind for unloaded master: " +
                    std::string(key.filename())
            });
        }

        const auto kind = loadedMaster != nullptr
            ? fo4::classify_plugin_kind(loadedMaster->plugin.header.tes4_flags, loadedMaster->plugin.key)
            : fo4::PluginKind::light;
        masters.push_back({std::move(key), kind});
        ++added;
    }

    if (added == 0) {
        return nlohmann::json("No new masters were needed for " + pluginName + ".");
    }

    try {
        auto mutated = fo4::remap_plugin_masters(target->plugin, std::move(masters));
        stagePluginMutationLocked(*target, std::move(mutated));
        return nlohmann::json(
            "Added " + std::to_string(added) + " master" +
            (added == 1 ? std::string{} : "s") + " to " + pluginName + ".");
    } catch (const std::exception& error) {
        return std::unexpected(Error{"AddMastersFailed", error.what()});
    }
}

}
