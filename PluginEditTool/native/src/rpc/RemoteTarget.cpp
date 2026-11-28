#include <fo4ide/rpc/RemoteTarget.hpp>

#include <chrono>
#include <utility>

#include <httplib.h>

namespace fo4ide::rpc {
namespace {

constexpr std::string_view tokenHeader = "X-FO4IDE-Token";
constexpr std::string_view clientHeader = "X-FO4IDE-Client";
constexpr std::string_view clientName = "mcp";

void configure(httplib::Client& client, std::chrono::seconds readTimeout) {
    client.set_connection_timeout(std::chrono::seconds(3));
    client.set_read_timeout(readTimeout);
    client.set_write_timeout(std::chrono::seconds(30));
}

std::string stringField(const nlohmann::json& json, const char* key, std::string fallback) {
    if (json.contains(key) && json.at(key).is_string()) return json.at(key).get<std::string>();
    return fallback;
}

Error unavailable(const httplib::Error& error) {
    return Error{"RemoteUnavailable", "GUI RPC request failed: " + httplib::to_string(error)};
}

Result<nlohmann::json> decodeRemoteResponse(const std::string& body, int status) {
    const auto json = nlohmann::json::parse(body, nullptr, false);
    if (json.is_discarded() || !json.is_object() || !json.contains("ok") || !json.at("ok").is_boolean()) {
        return std::unexpected(Error{"RemoteInvalidResponse",
                                     "GUI RPC response was not an RPC envelope (HTTP " + std::to_string(status) + ")"});
    }
    if (json.at("ok").get<bool>()) {
        return json.contains("value") ? json.at("value") : nlohmann::json(nullptr);
    }
    return std::unexpected(Error{stringField(json, "type", "RemoteError"),
                                 stringField(json, "error", "GUI RPC call failed")});
}

}

RemoteTarget::RemoteTarget(RemoteEndpoint endpoint, std::string targetName)
    : endpoint_(std::move(endpoint)), targetName_(std::move(targetName)) {}

Result<nlohmann::json> RemoteTarget::invoke(std::string_view method, const nlohmann::json& args) {
    httplib::Client client(endpoint_.host, endpoint_.port);
    configure(client, std::chrono::seconds(120));
    const nlohmann::json body{
        {"target", targetName_},
        {"method", std::string(method)},
        {"args", args}
    };
    const httplib::Headers headers{
        {std::string(tokenHeader), endpoint_.token},
        {std::string(clientHeader), std::string(clientName)}
    };
    const auto response = client.Post("/rpc", headers, body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace),
                                      "application/json");
    if (!response) return std::unexpected(unavailable(response.error()));
    return decodeRemoteResponse(response->body, response->status);
}

Result<std::vector<std::string>> fetchRemoteTargetNames(const RemoteEndpoint& endpoint) {
    httplib::Client client(endpoint.host, endpoint.port);
    configure(client, std::chrono::seconds(5));
    const auto response = client.Get("/api/health");
    if (!response) return std::unexpected(unavailable(response.error()));
    if (response->status != 200) {
        return std::unexpected(Error{"RemoteInvalidResponse",
                                     "GUI health check returned HTTP " + std::to_string(response->status)});
    }

    const auto json = nlohmann::json::parse(response->body, nullptr, false);
    if (json.is_discarded() || !json.is_object() || json.value("native", false) != true ||
        !json.contains("targets") || !json.at("targets").is_array()) {
        return std::unexpected(Error{"RemoteInvalidResponse", "GUI health response is not a native RPC host"});
    }

    std::vector<std::string> names;
    for (const auto& name : json.at("targets")) {
        if (name.is_string()) names.push_back(name.get<std::string>());
    }
    return names;
}

}
