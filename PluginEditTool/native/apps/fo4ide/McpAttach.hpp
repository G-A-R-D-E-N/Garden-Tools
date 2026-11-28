#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include <fo4ide/rpc/RpcDispatcher.hpp>

struct AttachOptions {
    bool requested{false};
    std::optional<int> port;
    std::string token;
};

struct AttachOutcome {
    bool attached{false};
    std::string message;
};

AttachOutcome attachMcpToGui(const std::filesystem::path& configDir,
                             const AttachOptions& options,
                             fo4ide::rpc::Dispatcher& remote);
