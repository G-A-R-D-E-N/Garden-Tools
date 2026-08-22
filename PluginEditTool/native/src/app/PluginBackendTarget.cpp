#include <fo4ide/app/PluginBackendTarget.hpp>

#include <utility>

namespace fo4ide::app {

PluginBackendTarget::PluginBackendTarget(plugin::IPluginBackend& backend, std::string targetNamespace)
    : backend_(backend), targetNamespace_(std::move(targetNamespace)) {}

Result<nlohmann::json> PluginBackendTarget::invoke(std::string_view method,
                                                    const nlohmann::json& args) {
    if (targetNamespace_ == "backend") {
        return backend_.invokeBackend(method, args);
    }
    return backend_.invoke(targetNamespace_ + "." + std::string(method), args);
}

}
