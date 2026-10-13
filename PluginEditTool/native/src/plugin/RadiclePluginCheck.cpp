#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <zlib.h>

#include <fo4/graph/check_errors.hpp>
#include <fo4/graph/reachability.hpp>
#include <fo4/plugin/maintenance.hpp>

namespace fo4ide::plugin {

Result<nlohmann::json> RadiclePluginBackend::checkForErrors(const nlohmann::json& arguments) const {
    std::string pluginName;
    std::size_t limit = 500;
    if (arguments.is_array() && !arguments.empty() && arguments[0].is_string()) {
        pluginName = arguments[0].get<std::string>();
    }
    if (arguments.is_array() && arguments.size() > 1 && arguments[1].is_number_integer()) {
        const auto raw = arguments[1].get<std::int64_t>();
        if (raw > 0) limit = std::min<std::size_t>(static_cast<std::size_t>(raw), 100000);
    }

    OpLock lock(*this, OpLock::Mode::Read);
    std::vector<const fo4::Plugin*> loadOrder;
    loadOrder.reserve(plugins_.size());
    for (const auto& loaded : plugins_) loadOrder.push_back(&loaded.plugin);

    fo4::CheckOptions options;
    options.limit = limit;
    if (!pluginName.empty()) {
        const auto* loaded = findPlugin(pluginName);
        if (loaded == nullptr) {
            return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
        }
        options.plugins.push_back(loaded->plugin.key);
    }

    try {
        nlohmann::json rows = nlohmann::json::array();
        for (const auto& finding : fo4::check_load_order(loadOrder, options)) {
            rows.push_back({
                {"Code", std::string(fo4::check_code_name(finding.code))},
                {"Detail", finding.detail},
                {"Severity", std::string(fo4::check_severity_name(finding.severity))},
                {"Plugin", std::string(finding.plugin.filename())},
                {"FormKey", finding.record.to_string()},
                {"Subrecord", finding.subrecord == fo4::Signature{} ? std::string{}
                                                                    : finding.subrecord.to_string()},
                {"Target", finding.target.to_string()},
                {"Message", finding.message}
            });
        }
        return nlohmann::json(rows.dump());
    } catch (const std::exception& error) {
        return std::unexpected(Error{"CheckForErrorsFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::lootCleaningReport(const nlohmann::json& arguments) const {
    const auto pluginName = arguments.is_array() && !arguments.empty() && arguments[0].is_string()
                                ? arguments[0].get<std::string>()
                                : std::string{};
    if (pluginName.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});

    OpLock lock(*this, OpLock::Mode::Read);
    const auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }
    if (loaded->dirty) {
        return std::unexpected(Error{
            "PluginHasPendingChanges",
            "Save or discard pending changes first; LOOT matches the file on disk: " + pluginName
        });
    }

    try {
        std::ifstream in(loaded->path, std::ios::binary);
        if (!in) return std::unexpected(Error{"PluginReadFailed", "Cannot read " + loaded->path.string()});
        std::vector<char> bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        auto crc = crc32(0L, Z_NULL, 0);
        crc = crc32(crc, reinterpret_cast<const Bytef*>(bytes.data()), static_cast<uInt>(bytes.size()));

        std::vector<const fo4::Plugin*> loadOrder;
        std::size_t index = 0;
        for (std::size_t i = 0; i < plugins_.size(); ++i) {
            loadOrder.push_back(&plugins_[i].plugin);
            if (&plugins_[i] == loaded) index = i;
        }
        const auto itm = fo4::find_identical_to_master(loadOrder, index).size();
        const auto deleted = fo4::count_deleted_records(loaded->plugin);

        char crcText[11];
        std::snprintf(crcText, sizeof(crcText), "0x%08lX", static_cast<unsigned long>(crc));
        std::string yaml = "- name: '" + pluginName + "'\n";
        const bool dirty = itm != 0 || deleted.references != 0 || deleted.navmeshes != 0;
        yaml += dirty ? "  dirty:\n" : "  clean:\n";
        yaml += "    - crc: " + std::string(crcText) + "\n";
        yaml += "      util: 'PluginEditTool'\n";
        if (dirty) {
            yaml += "      itm: " + std::to_string(itm) + "\n";
            yaml += "      udr: " + std::to_string(deleted.references) + "\n";
            yaml += "      nav: " + std::to_string(deleted.navmeshes) + "\n";
        }
        return nlohmann::json(std::move(yaml));
    } catch (const std::exception& error) {
        return std::unexpected(Error{"LootCleaningReportFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::findUnreachable(const nlohmann::json& arguments) const {
    if (!arguments.is_array() || arguments.empty() || !arguments[0].is_string()) {
        return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    }
    const auto pluginName = arguments[0].get<std::string>();
    std::size_t limit = 500;
    if (arguments.size() > 1 && arguments[1].is_number_integer()) {
        const auto raw = arguments[1].get<std::int64_t>();
        if (raw > 0) limit = std::min<std::size_t>(static_cast<std::size_t>(raw), 100000);
    }

    OpLock lock(*this, OpLock::Mode::Read);
    const auto* wanted = findPlugin(pluginName);
    std::vector<const fo4::Plugin*> loadOrder;
    std::size_t target = plugins_.size();
    for (std::size_t index = 0; index < plugins_.size(); ++index) {
        loadOrder.push_back(&plugins_[index].plugin);
        if (wanted == &plugins_[index]) target = index;
    }
    if (target == plugins_.size()) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }

    try {
        const auto unreachable = fo4::find_unreachable(loadOrder, target);
        nlohmann::json rows = nlohmann::json::array();
        nlohmann::json bySignature = nlohmann::json::object();
        for (const auto& item : unreachable) {
            const auto signature = item.signature.to_string();
            bySignature[signature] = bySignature.value(signature, 0) + 1;
            if (rows.size() < limit) rows.push_back({{"FormKey", item.key.to_string()}, {"Signature", signature}});
        }
        return nlohmann::json(nlohmann::json{
            {"Total", unreachable.size()}, {"BySignature", std::move(bySignature)}, {"Records", std::move(rows)}
        }.dump());
    } catch (const std::exception& error) {
        return std::unexpected(Error{"FindUnreachableFailed", error.what()});
    }
}

}  // namespace fo4ide::plugin
