#pragma once

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
            "Native plugin support is not part of Milestone 1"
        });
    }
};

}
