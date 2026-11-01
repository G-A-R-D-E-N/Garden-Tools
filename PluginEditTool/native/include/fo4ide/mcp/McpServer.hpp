#pragma once

#include <functional>
#include <istream>
#include <memory>
#include <mutex>
#include <optional>
#include <ostream>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include <fo4ide/core/Result.hpp>
#include <fo4ide/rpc/RpcDispatcher.hpp>

namespace fo4ide::mcp {

inline constexpr std::string_view ProtocolVersion = "2024-11-05";

struct ToolDefinition {
    std::string name;
    std::string description;
    nlohmann::json inputSchema{{"type", "object"}, {"properties", nlohmann::json::object()}};

    nlohmann::json toJson() const;
};

struct ToolResult {
    std::string text;
    bool isError{false};
};

class Tool {
public:
    virtual ~Tool() = default;
    virtual const ToolDefinition& definition() const noexcept = 0;
    virtual ToolResult call(const nlohmann::json& arguments) = 0;
};

class FunctionTool final : public Tool {
public:
    using Handler = std::function<ToolResult(const nlohmann::json&)>;

    FunctionTool(ToolDefinition definition, Handler handler);

    const ToolDefinition& definition() const noexcept override;
    ToolResult call(const nlohmann::json& arguments) override;

private:
    ToolDefinition definition_;
    Handler handler_;
};

class RpcTool final : public Tool {
public:
    RpcTool(ToolDefinition definition,
            const rpc::Dispatcher& dispatcher,
            std::string target,
            std::string method,
            std::vector<std::string> argumentOrder = {});

    const ToolDefinition& definition() const noexcept override;
    ToolResult call(const nlohmann::json& arguments) override;

private:
    ToolDefinition definition_;
    const rpc::Dispatcher& dispatcher_;
    std::string target_;
    std::string method_;
    std::vector<std::string> argumentOrder_;
};

class Registry {
public:
    Result<void> registerTool(std::shared_ptr<Tool> tool);
    nlohmann::json definitions() const;
    ToolResult call(std::string_view name, const nlohmann::json& arguments) const;
    void setBeforeFirstCall(std::function<std::optional<std::string>()> hook);

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<Tool>> tools_;
    mutable std::mutex hookMutex_;
    mutable std::function<std::optional<std::string>()> beforeFirstCall_;
};

class StdioServer {
public:
    explicit StdioServer(Registry& registry);

    std::optional<std::string> handleLine(std::string_view line) const;
    void run(std::istream& input, std::ostream& output) const;

private:
    std::string result(const nlohmann::json& id, const nlohmann::json& value) const;
    std::string error(const nlohmann::json& id, int code, std::string message) const;

    Registry& registry_;
};

}
