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

    // Dedicated seam for schema-driven field writes. Existing backends retain
    // their current SetField behavior; Radicle overrides this to intercept
    // registered scalar fields before delegating to its typed record writers.
    virtual Result<nlohmann::json> setFieldValue(const nlohmann::json& arguments) {
        return invokeBackend("SetField", arguments);
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
