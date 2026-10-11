#include <fo4ide/http/HttpServer.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <string_view>
#include <thread>
#include <utility>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <fo4ide/core/PayloadLimits.hpp>

namespace fo4ide::http {
namespace {

nlohmann::json eventJson(const events::Event& event) {
    nlohmann::json value;
    if (event.payload.is_object()) {
        value = event.payload;
    } else {
        value = {{"Value", event.payload}};
    }
    if (!value.contains("Type")) value["Type"] = event.type;
    return value;
}

std::string dumpJson(const nlohmann::json& value) {
    return value.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

void reject(httplib::Response& response, int status, std::string type, std::string message) {
    const nlohmann::json error{
        {"ok", false},
        {"error", std::move(message)},
        {"type", std::move(type)}
    };
    response.status = status;
    response.set_header("Connection", "close");
    response.set_content(dumpJson(error), "application/json; charset=utf-8");
}

bool tokenMatches(const httplib::Request& request, std::string_view token) {
    const auto provided = request.get_header_value("X-FO4IDE-Token");
    unsigned int difference = provided.size() == token.size() ? 0u : 1u;
    for (std::size_t i = 0; i < token.size() && i < provided.size(); ++i) {
        difference |= static_cast<unsigned char>(provided[i] ^ token[i]);
    }
    return difference == 0;
}

bool isJsonContentType(std::string_view contentType) {
    constexpr std::string_view jsonType = "application/json";
    auto mediaType = contentType.substr(0, contentType.find(';'));
    while (!mediaType.empty() && mediaType.back() == ' ') mediaType.remove_suffix(1);
    return mediaType.size() == jsonType.size() &&
           std::equal(mediaType.begin(), mediaType.end(), jsonType.begin(), [](char a, char b) {
               return std::tolower(static_cast<unsigned char>(a)) ==
                      std::tolower(static_cast<unsigned char>(b));
           });
}

bool hostAllowed(const httplib::Request& request, std::string_view bindHost, int port) {
    const auto suffix = ":" + std::to_string(port);
    const auto host = request.get_header_value("Host");
    if (host == "127.0.0.1" + suffix || host == "localhost" + suffix) return true;
    return bindHost == "::1" && host == "[::1]" + suffix;
}

bool wantsHtml(const httplib::Request& request) {
    if (request.method != "GET") return false;
    const auto accept = request.get_header_value("Accept");
    return accept.find("text/html") != std::string::npos;
}

}

struct HttpServer::Impl {
    Impl(rpc::Dispatcher& dispatcherIn, events::EventBus& eventsIn)
        : dispatcher(dispatcherIn), events(eventsIn) {}

    rpc::Dispatcher& dispatcher;
    events::EventBus& events;
    httplib::Server server;
    std::jthread thread;
    std::atomic_bool running{false};
    std::atomic_int boundPort{0};
    Config config;
};

HttpServer::HttpServer(rpc::Dispatcher& dispatcher, events::EventBus& events)
    : impl_(std::make_unique<Impl>(dispatcher, events)) {}

HttpServer::~HttpServer() {
    stop();
}

Result<int> HttpServer::start(const Config& config) {
    if (impl_->running.load()) {
        return std::unexpected(Error{"HttpAlreadyRunning", "HTTP server is already running"});
    }
    if (config.host != "127.0.0.1" && config.host != "localhost" && config.host != "::1") {
        return std::unexpected(Error{"UnsafeHttpBind", "Milestone 1 only permits loopback HTTP binds"});
    }
    if (config.token.empty()) {
        return std::unexpected(Error{"HttpTokenMissing", "HTTP server requires an access token"});
    }
    if (!config.webRoot.empty() && !std::filesystem::is_directory(config.webRoot)) {
        return std::unexpected(Error{"WebRootMissing", "Web UI directory not found: " + config.webRoot.string()});
    }

    impl_->config = config;
    auto& server = impl_->server;
    server.set_payload_max_length(core::kMaxRequestBytes);

    server.set_pre_routing_handler([this](const httplib::Request& request, httplib::Response& response) {
        const auto& token = impl_->config.token;
        const bool isRpc = request.method == "POST" && request.path == "/rpc";
        const bool isEvents = (request.method == "GET" || request.method == "HEAD") && request.path == "/events";
        if (!hostAllowed(request, impl_->config.host, impl_->boundPort.load())) {
            reject(response, 403, "Forbidden", "Host header must name this loopback server");
        } else if ((isRpc || isEvents) && !tokenMatches(request, token)) {
            reject(response, 401, "Unauthorized", "Missing or invalid X-FO4IDE-Token");
        } else if (isRpc && !isJsonContentType(request.get_header_value("Content-Type"))) {
            reject(response, 415, "UnsupportedMediaType", "RPC requests must use application/json");
        } else {
            return httplib::Server::HandlerResponse::Unhandled;
        }
        return httplib::Server::HandlerResponse::Handled;
    });

    server.Post("/rpc", [this](const httplib::Request& request, httplib::Response& response) {
        try {
            const auto json = nlohmann::json::parse(request.body);
            const auto result = impl_->dispatcher.invokeJson(json).toJson();
            response.set_content(dumpJson(result), "application/json; charset=utf-8");
        } catch (const std::exception& ex) {
            reject(response, 400, "InvalidRequest", ex.what());
        }
    });

    server.Get("/api/health", [this](const httplib::Request&, httplib::Response& response) {
        const nlohmann::json health{
            {"ok", true},
            {"native", true},
            {"targets", impl_->dispatcher.targetNames()}
        };
        response.set_content(dumpJson(health), "application/json; charset=utf-8");
    });

    server.Get("/events", [this](const httplib::Request&, httplib::Response& response) {
        auto subscription = std::make_shared<events::Subscription>(impl_->events.subscribe());
        response.set_header("Cache-Control", "no-cache");
        response.set_header("X-Accel-Buffering", "no");
        response.set_chunked_content_provider(
            "text/event-stream",
            [subscription](std::size_t, httplib::DataSink& sink) {
                using namespace std::chrono_literals;
                if (const auto event = subscription->waitPop(1s)) {
                    const auto payload = "data: " + dumpJson(eventJson(*event)) + "\n\n";
                    return sink.write(payload.data(), payload.size());
                }
                static constexpr std::string_view heartbeat = ": keep-alive\n\n";
                return sink.write(heartbeat.data(), heartbeat.size());
            });
    });

    if (!config.webRoot.empty()) {
        if (!server.set_mount_point("/", config.webRoot.string())) {
            return std::unexpected(Error{"WebMountFailed", "Could not mount web UI directory"});
        }

        const auto index = config.webRoot / "index.html";
        server.set_error_handler([index](const httplib::Request& request, httplib::Response& response) {
            if (response.status == 404 && wantsHtml(request) && std::filesystem::is_regular_file(index)) {
                response.status = 200;
                response.set_file_content(index.string(), "text/html");
            }
        });
    }

    const int port = config.port > 0
        ? (server.bind_to_port(config.host, config.port) ? config.port : -1)
        : server.bind_to_any_port(config.host);
    if (port <= 0) {
        return std::unexpected(Error{"HttpBindFailed", "Could not bind HTTP server to " + config.host});
    }

    impl_->boundPort.store(port);
    impl_->running.store(true);
    impl_->thread = std::jthread([this] {
        impl_->server.listen_after_bind();
        impl_->running.store(false);
    });
    return port;
}

void HttpServer::stop() {
    if (!impl_) return;
    if (impl_->running.exchange(false)) impl_->server.stop();
    if (impl_->thread.joinable()) impl_->thread.join();
    impl_->boundPort.store(0);
}

bool HttpServer::running() const noexcept {
    return impl_->running.load();
}

int HttpServer::port() const noexcept {
    return impl_->boundPort.load();
}

}
