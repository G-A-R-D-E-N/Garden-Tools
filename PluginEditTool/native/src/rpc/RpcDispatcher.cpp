#include <fo4ide/rpc/RpcDispatcher.hpp>

#include <algorithm>
#include <exception>
#include <mutex>
#include <vector>

namespace fo4ide::rpc {

nlohmann::json Response::toJson() const {
    if (ok) return {{"ok", true}, {"value", value}};
    return {{"ok", false}, {"error", error}, {"type", type}};
}

Result<void> Dispatcher::registerTarget(std::string name, std::shared_ptr<Target> target) {
    if (name.empty()) return std::unexpected(Error{"InvalidTarget", "RPC target name is empty"});
    if (!target) return std::unexpected(Error{"InvalidTarget", "RPC target is null"});

    std::unique_lock lock(mutex_);
    if (targets_.contains(name)) {
        return std::unexpected(Error{"DuplicateTarget", "RPC target is already registered: " + name});
    }
    targets_.emplace(std::move(name), std::move(target));
    return {};
}

Response Dispatcher::invoke(const Request& request) const {
    std::shared_ptr<Target> target;
    {
        std::shared_lock lock(mutex_);
        const auto it = targets_.find(request.target);
        if (it == targets_.end()) {
            return {false, nullptr, "Unknown RPC target: " + request.target, "UnknownTarget"};
        }
        target = it->second;
    }

    try {
        auto result = target->invoke(request.method, request.args);
        if (!result) return {false, nullptr, result.error().message, result.error().type};
        return {true, std::move(*result), {}, {}};
    } catch (const std::exception& ex) {
        return {false, nullptr, ex.what(), "Exception"};
    } catch (...) {
        return {false, nullptr, "Unhandled non-standard exception", "Exception"};
    }
}

Response Dispatcher::invokeJson(const nlohmann::json& request) const {
    if (!request.is_object()) return {false, nullptr, "RPC request must be an object", "InvalidRequest"};

    const auto target = request.value("target", std::string{});
    const auto method = request.value("method", std::string{});
    const auto args = request.contains("args") ? request.at("args") : nlohmann::json::array();

    if (target.empty() || method.empty()) {
        return {false, nullptr, "RPC target and method are required", "InvalidRequest"};
    }
    if (!args.is_array()) return {false, nullptr, "RPC args must be an array", "InvalidRequest"};

    return invoke(Request{target, method, args});
}

nlohmann::json Dispatcher::targetNames() const {
    std::vector<std::string> names;
    {
        std::shared_lock lock(mutex_);
        names.reserve(targets_.size());
        for (const auto& [name, target] : targets_) {
            static_cast<void>(target);
            names.push_back(name);
        }
    }
    std::ranges::sort(names);
    return names;
}

}
