#include "RadiclePluginSupport.hpp"

#include <cstddef>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <fo4/archive/asset_scan.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/plugin/visit.hpp>
#include <fo4/signature.hpp>

namespace fo4ide::plugin {

namespace {

std::string utf8String(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

}

Result<nlohmann::json> RadiclePluginBackend::collectAssetReferences(
    const nlohmann::json& arguments) const {
    const auto name = support::stringArgument(arguments, 0);
    if (name.empty()) {
        return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    }

    OpLock lock(*this, OpLock::Mode::Read);
    const auto* loaded = findPlugin(name);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + name});
    }

    constexpr auto k_tes4 = fo4::Signature::from_string("TES4");
    std::size_t nativeRecords = 0;
    for (const auto* record : fo4::collect_records(loaded->plugin)) {
        if (record->header.signature == k_tes4 || record->form_key.owner() != loaded->plugin.key) continue;
        ++nativeRecords;
    }

    std::map<std::string, std::vector<std::string>> byPath;
    for (const auto& reference : fo4::archive::collect_asset_references(loaded->plugin)) {
        byPath[reference.path].push_back(reference.record.to_string());
    }
    nlohmann::json assets = nlohmann::json::array();
    for (auto& [path, records] : byPath) {
        assets.push_back({{"path", path}, {"records", std::move(records)}});
    }
    return nlohmann::json{{"plugin", std::string(loaded->plugin.key.filename())},
                          {"pluginDirectory", utf8String(loaded->path.parent_path())},
                          {"nativeRecords", nativeRecords},
                          {"assets", std::move(assets)}};
}

}
