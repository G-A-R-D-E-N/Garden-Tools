#include <fo4ide/http/HttpServer.hpp>

#include <atomic>
#include <chrono>
#include <string_view>
#include <thread>
#include <utility>

#include <httplib.h>
#include <nlohmann/json.hpp>

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
    if (!config.webRoot.empty() && !std::filesystem::is_directory(config.webRoot)) {
        return std::unexpected(Error{"WebRootMissing", "Web UI directory not found: " + config.webRoot.string()});
    }

    impl_->config = config;
    auto& server = impl_->server;

    server.Post("/rpc", [this](const httplib::Request& request, httplib::Response& response) {
        try {
            const auto json = nlohmann::json::parse(request.body);
            const auto result = impl_->dispatcher.invokeJson(json).toJson();
            response.set_content(result.dump(), "application/json; charset=utf-8");
        } catch (const std::exception& ex) {
            const nlohmann::json error{
                {"ok", false},
                {"error", ex.what()},
                {"type", "InvalidRequest"}
            };
            response.status = 400;
            response.set_content(error.dump(), "application/json; charset=utf-8");
        }
    });

    server.Get("/api/health", [this](const httplib::Request&, httplib::Response& response) {
        const nlohmann::json health{
            {"ok", true},
            {"native", true},
            {"targets", impl_->dispatcher.targetNames()}
        };
        response.set_content(health.dump(), "application/json; charset=utf-8");
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
                    const auto payload = "data: " + eventJson(*event).dump() + "\n\n";
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
