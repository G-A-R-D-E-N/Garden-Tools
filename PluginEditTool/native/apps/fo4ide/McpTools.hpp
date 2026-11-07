#pragma once

#include <memory>

#include <fo4ide/logging/Logger.hpp>
#include <fo4ide/mcp/McpServer.hpp>
#include <fo4ide/rpc/RpcDispatcher.hpp>

bool registerNativeMcpTools(fo4ide::mcp::Registry& registry,
                            const fo4ide::rpc::Dispatcher& rpc,
                            const std::shared_ptr<spdlog::logger>& log);
