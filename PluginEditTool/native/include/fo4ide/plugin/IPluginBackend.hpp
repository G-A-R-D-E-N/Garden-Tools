#pragma once

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

    // Compatibility dispatch for legacy backend methods that are being moved
    // into native modules incrementally. Backends without extra compatibility
    // behavior retain the normal backend namespace dispatch.
    virtual Result<nlohmann::json> invokeLegacyBackend(
        std::string_view method,
        const nlohmann::json& arguments) {
        return invokeBackend(method, arguments);
    }

    // Dedicated seam for schema-driven field writes. Existing backends retain
    // their current SetField behavior; Radicle overrides this to intercept
    // registered scalar fields before delegating to its typed record writers.
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

    virtual Result<nlohmann::json> checkCircularLeveledLists(const nlohmann::json&) const {
        return std::unexpected(Error{
            "CircularLeveledListCheckUnavailable",
            "Native circular leveled-list checking is unavailable for this backend"
        });
    }

    // Compatibility metadata used by the legacy condition editor. Keeping it
    // on the backend interface avoids teaching the app/RPC layer about a
    // concrete Fallout 4 implementation.
    virtual Result<nlohmann::json> conditionFunctionParams() const {
        return std::unexpected(Error{
            "ConditionMetadataUnavailable",
            "Condition parameter metadata is not provided by this backend"
        });
    }

    // Generic ElementService compatibility lives on the backend seam so the
    // RPC layer never needs to know Radicle schema types. Implementations may
    // expose only schema-proven element families and fail closed for the rest.
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

    // Cell-viewer seams stay on the abstract plugin backend because they are
    // load-order queries/mutations, not UI concerns. Non-FO4 backends can
    // decline them without the app layer knowing any Radicle types.
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
