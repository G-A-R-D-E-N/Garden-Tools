#pragma once

#include <charconv>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include <fo4ide/core/Result.hpp>
#include <fo4ide/events/EventBus.hpp>
#include <fo4ide/rpc/RpcDispatcher.hpp>

namespace fo4ide::http {

struct Config {
    std::string host{"127.0.0.1"};
    int port{0};
    std::filesystem::path webRoot;
    std::string token;
};

inline std::optional<int> parsePort(std::string_view text) {
    int port = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), port);
    if (error != std::errc{} || end != text.data() + text.size()) return std::nullopt;
    if (port < 0 || port > 65535) return std::nullopt;
    return port;
}

class HttpServer {
public:
    HttpServer(rpc::Dispatcher& dispatcher, events::EventBus& events);
    ~HttpServer();

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    Result<int> start(const Config& config);
    void stop();
    bool running() const noexcept;
    int port() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
