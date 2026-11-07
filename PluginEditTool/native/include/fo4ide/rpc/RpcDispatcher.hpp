#pragma once

#include <memory>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include <fo4ide/core/Result.hpp>

namespace fo4ide::rpc {

struct Request {
    std::string target;
    std::string method;
    nlohmann::json args{nlohmann::json::array()};
};

struct Response {
    bool ok{false};
    nlohmann::json value{nullptr};
    std::string error;
    std::string type;

    nlohmann::json toJson() const;
};

class Target {
public:
    virtual ~Target() = default;
    virtual Result<nlohmann::json> invoke(std::string_view method, const nlohmann::json& args) = 0;
};

class Dispatcher {
public:
    Result<void> registerTarget(std::string name, std::shared_ptr<Target> target);
    Response invoke(const Request& request) const;
    Response invokeJson(const nlohmann::json& request) const;
    nlohmann::json targetNames() const;

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<Target>> targets_;
};

}
