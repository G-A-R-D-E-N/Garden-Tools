#pragma once

#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <fo4/graph/conflict_graph.hpp>
#include <fo4/plugin/container.hpp>
#include <fo4ide/plugin/IPluginBackend.hpp>

namespace fo4ide::plugin {

class RadiclePluginBackend final : public IPluginBackend {
public:
    Capabilities capabilities() const noexcept override;
    Result<nlohmann::json> invoke(std::string_view operation,
                                  const nlohmann::json& arguments) override;
    Result<nlohmann::json> invokeBackend(std::string_view method,
                                         const nlohmann::json& arguments) override;

private:
    struct LoadedPlugin {
        std::string name;
        std::filesystem::path path;
        std::string source;
        fo4::Plugin plugin;
    };

    Result<nlohmann::json> loadEnvironment(const nlohmann::json& arguments);
    Result<nlohmann::json> getPlugins() const;
    Result<nlohmann::json> getChildren(const nlohmann::json& arguments) const;
    Result<nlohmann::json> openRecord(const nlohmann::json& arguments) const;
    Result<nlohmann::json> openPlugin(const nlohmann::json& arguments);
    Result<nlohmann::json> savePlugin(const nlohmann::json& arguments);
    Result<nlohmann::json> setField(const nlohmann::json& arguments);
    Result<nlohmann::json> copyAsOverride(const nlohmann::json& arguments);
    Result<nlohmann::json> getEditablePlugins() const;
    Result<nlohmann::json> getActivePlugins() const;
    Result<nlohmann::json> getRecordTypeIndex() const;
    Result<nlohmann::json> getLoadOrderSummary() const;
    Result<nlohmann::json> getConflicts() const;
    Result<nlohmann::json> getConflictMatrix(const nlohmann::json& arguments) const;
    Result<nlohmann::json> getReferencedBy(const nlohmann::json& arguments) const;
    Result<nlohmann::json> getDependencies(const nlohmann::json& arguments) const;
    Result<nlohmann::json> getProblems(const nlohmann::json& arguments) const;
    Result<nlohmann::json> getHistory(const nlohmann::json& arguments) const;
    Result<nlohmann::json> searchRecords(const nlohmann::json& arguments) const;
    Result<nlohmann::json> scanConflicts() const;
    Result<nlohmann::json> scanBrokenRefs() const;
    Result<nlohmann::json> listMasters(const nlohmann::json& arguments) const;
    Result<nlohmann::json> reorderMasters(const nlohmann::json& arguments);
    Result<nlohmann::json> setLight(const nlohmann::json& arguments);
    Result<nlohmann::json> saveMastersPlugin(const nlohmann::json& arguments);

    LoadedPlugin* findPlugin(std::string_view name);
    const LoadedPlugin* findPlugin(std::string_view name) const;
    bool isEditable(const LoadedPlugin& plugin) const;
    void rebuildGraphLocked();

    mutable std::mutex mutex_;
    std::vector<LoadedPlugin> plugins_;
    fo4::ConflictGraph graph_;
};

}
