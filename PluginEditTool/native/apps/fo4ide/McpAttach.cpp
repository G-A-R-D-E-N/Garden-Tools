#include "McpAttach.hpp"

#include <memory>
#include <string>
#include <utility>

#include <fo4ide/app/GuiEndpoint.hpp>

#if defined(FO4IDE_HAS_HTTP)
#include <fo4ide/rpc/RemoteTarget.hpp>
#endif

namespace {

AttachOutcome fail(std::string message) {
    return {false, std::move(message)};
}

#if defined(FO4IDE_HAS_HTTP)
constexpr const char* loopbackHost = "127.0.0.1";

fo4ide::Result<fo4ide::rpc::RemoteEndpoint> resolveEndpoint(const std::filesystem::path& configDir,
                                                            const AttachOptions& options) {
    if (options.port) {
        if (options.token.empty()) {
            return std::unexpected(fo4ide::Error{"AttachTokenMissing", "--attach=<port> requires --token"});
        }
        return fo4ide::rpc::RemoteEndpoint{.host = loopbackHost, .port = *options.port, .token = options.token};
    }

    auto file = fo4ide::app::readGuiEndpoint(fo4ide::app::guiEndpointPath(configDir));
    if (!file) return std::unexpected(file.error());
    if (!fo4ide::app::isProcessAlive(file->pid)) {
        return std::unexpected(fo4ide::Error{
            "GuiEndpointStale",
            "GUI process " + std::to_string(file->pid) + " is not running"});
    }
    return fo4ide::rpc::RemoteEndpoint{
        .host = loopbackHost,
        .port = file->port,
        .token = options.token.empty() ? file->token : options.token};
}
#endif

}

AttachOutcome attachMcpToGui(const std::filesystem::path& configDir,
                             const AttachOptions& options,
                             fo4ide::rpc::Dispatcher& remote) {
#if defined(FO4IDE_HAS_HTTP)
    auto endpoint = resolveEndpoint(configDir, options);
    if (!endpoint) return fail(endpoint.error().message);

    auto names = fo4ide::rpc::fetchRemoteTargetNames(*endpoint);
    if (!names) return fail(names.error().message);
    if (names->empty()) return fail("GUI exposes no RPC targets");

    const auto probe = fo4ide::rpc::RemoteTarget(*endpoint, "app").invoke("GetHealth", nlohmann::json::array());
    if (!probe) return fail(probe.error().type + ": " + probe.error().message);

    for (const auto& name : *names) {
        auto registered = remote.registerTarget(name, std::make_shared<fo4ide::rpc::RemoteTarget>(*endpoint, name));
        if (!registered) return fail(registered.error().message);
    }
    return {true, "attached to GUI on port " + std::to_string(endpoint->port) +
                  " with " + std::to_string(names->size()) + " RPC targets"};
#else
    static_cast<void>(configDir);
    static_cast<void>(options);
    static_cast<void>(remote);
    return fail("this build has no HTTP transport");
#endif
}
