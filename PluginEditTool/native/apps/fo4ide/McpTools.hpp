#pragma once

#include <memory>

#include <fo4ide/logging/Logger.hpp>
#include <fo4ide/mcp/McpServer.hpp>
#include <fo4ide/rpc/RpcDispatcher.hpp>
#include <fo4ide/settings/Settings.hpp>

#include "McpPathPolicy.hpp"

bool registerNativeMcpTools(fo4ide::mcp::Registry& registry,
                            const fo4ide::rpc::Dispatcher& rpc,
                            const std::shared_ptr<spdlog::logger>& log,
                            const fo4ide::settings::SharedSettings& settings,
                            const fo4ide::app::AutoRootResolver& autoRoots);

bool registerPreviewMcpTools(fo4ide::mcp::Registry& registry,
                             const fo4ide::rpc::Dispatcher& rpc,
                             const std::shared_ptr<spdlog::logger>& log,
                             const fo4ide::settings::SharedSettings& settings,
                             const fo4ide::app::AutoRootResolver& autoRoots);
