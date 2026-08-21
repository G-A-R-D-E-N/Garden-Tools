#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include <fo4ide/core/Result.hpp>
#include <fo4ide/events/EventBus.hpp>
#include <fo4ide/rpc/RpcDispatcher.hpp>

namespace fo4ide::http {

struct Config {
    std::string host{"127.0.0.1"};
    int port{0};
    std::filesystem::path webRoot;
};

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
