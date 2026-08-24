#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <fo4ide/core/Result.hpp>

namespace fo4ide::plugin {

struct Capabilities {
    bool available{false};
    bool readPlugins{false};
    bool writePlugins{false};
    bool resolveReferences{false};
};

class IPluginBackend {
public:
    virtual ~IPluginBackend() = default;

    virtual Capabilities capabilities() const noexcept = 0;
    virtual Result<nlohmann::json> invoke(std::string_view operation,
                                          const nlohmann::json& arguments) = 0;

    virtual Result<nlohmann::json> invokeBackend(std::string_view method,
                                                 const nlohmann::json& arguments) {
        return invoke("backend." + std::string(method), arguments);
    }

    virtual Result<nlohmann::json> invokeLegacyBackend(
        std::string_view method,
        const nlohmann::json& arguments) {
        return invokeBackend(method, arguments);
    }

    virtual Result<void> setTrustedNewPluginRoot(const std::filesystem::path&) {
        return {};
    }

    virtual Result<nlohmann::json> createPlugin(const nlohmann::json& arguments) {
        return invokeLegacyBackend("CreatePlugin", arguments);
    }

    virtual Result<void> discardCreatedPlugin(std::string_view) {
        return std::unexpected(Error{
            "DiscardCreatedPluginUnavailable",
            "This backend cannot roll back an auto-created plugin"
        });
    }

    virtual Result<nlohmann::json> setFieldValue(const nlohmann::json& arguments) {
        return invokeBackend("SetField", arguments);
    }

    virtual Result<nlohmann::json> compactToEsl(const nlohmann::json&) {
        return std::unexpected(Error{
            "CompactToEslUnavailable",
            "Native compact-to-ESL is unavailable for this backend"
        });
    }

    virtual Result<nlohmann::json> renumberPluginFormIds(const nlohmann::json&) {
        return std::unexpected(Error{
            "RenumberPluginUnavailable",
            "Native plugin-wide FormID renumbering is unavailable for this backend"
        });
    }

    virtual Result<nlohmann::json> deepCopyAsOverride(const nlohmann::json&) {
        return std::unexpected(Error{
            "DeepCopyUnavailable",
            "Native deep-copy-as-override is unavailable for this backend"
        });
    }

    virtual Result<nlohmann::json> changeReferencingRecords(const nlohmann::json&) {
        return std::unexpected(Error{
            "ReferenceRemapUnavailable",
            "Native referencing-record remapping is unavailable for this backend"
        });
    }

    virtual Result<nlohmann::json> checkCircularLeveledLists(const nlohmann::json&) const {
        return std::unexpected(Error{
            "CircularLeveledListCheckUnavailable",
            "Native circular leveled-list checking is unavailable for this backend"
        });
    }

    virtual Result<nlohmann::json> createSeqFile(const nlohmann::json&) const {
        return std::unexpected(Error{
            "SeqGenerationUnavailable",
            "Native SEQ generation is unavailable for this backend"
        });
    }

    virtual Result<nlohmann::json> conditionFunctionParams() const {
        return std::unexpected(Error{
            "ConditionMetadataUnavailable",
            "Condition parameter metadata is not provided by this backend"
        });
    }

    virtual Result<nlohmann::json> describeElement(const nlohmann::json&) const {
        return std::unexpected(Error{"ElementBackendUnavailable", "Generic element queries are unavailable"});
    }
    virtual Result<nlohmann::json> addElement(const nlohmann::json&) {
        return std::unexpected(Error{"ElementBackendUnavailable", "Generic element insertion is unavailable"});
    }
    virtual Result<nlohmann::json> removeElement(const nlohmann::json&) {
        return std::unexpected(Error{"ElementBackendUnavailable", "Generic element removal is unavailable"});
    }
    virtual Result<nlohmann::json> moveElement(const nlohmann::json&) {
        return std::unexpected(Error{"ElementBackendUnavailable", "Generic element movement is unavailable"});
    }
    virtual Result<nlohmann::json> clearElement(const nlohmann::json&) {
        return std::unexpected(Error{"ElementBackendUnavailable", "Generic element clearing is unavailable"});
    }

    virtual Result<nlohmann::json> getPlacedReferences(const nlohmann::json&) const {
        return std::unexpected(Error{"CellBackendUnavailable", "Placed-reference queries are unavailable"});
    }
    virtual Result<nlohmann::json> getPlacedReferencesAtGrid(const nlohmann::json&) const {
        return std::unexpected(Error{"CellBackendUnavailable", "Exterior-cell queries are unavailable"});
    }
    virtual Result<nlohmann::json> setPlacedReferenceTransform(const nlohmann::json&) {
        return std::unexpected(Error{"CellBackendUnavailable", "Placed-reference writes are unavailable"});
    }
};

class NullPluginBackend final : public IPluginBackend {
public:
    Capabilities capabilities() const noexcept override {
        return {};
    }

    Result<nlohmann::json> invoke(std::string_view,
                                  const nlohmann::json&) override {
        return std::unexpected(Error{
            "PluginBackendUnavailable",
            "Native plugin backend is not connected yet"
        });
    }
};

}
