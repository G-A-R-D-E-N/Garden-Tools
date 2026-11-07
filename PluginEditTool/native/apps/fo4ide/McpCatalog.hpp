#pragma once

#include <memory>
#include <span>
#include <string_view>

#include <fo4ide/logging/Logger.hpp>
#include <fo4ide/mcp/McpServer.hpp>
#include <fo4ide/rpc/RpcDispatcher.hpp>

namespace fo4ide::app {

struct CatalogTool {
    std::string_view name;
    std::string_view target;
    std::string_view method;
    std::string_view fields;
    std::string_view description;
};

std::span<const CatalogTool> readCatalogTools();
std::span<const CatalogTool> editCatalogTools();
std::span<const CatalogTool> serviceCatalogTools();

bool registerCatalogTools(fo4ide::mcp::Registry& registry,
                          const fo4ide::rpc::Dispatcher& rpc,
                          const std::shared_ptr<spdlog::logger>& log);

}
