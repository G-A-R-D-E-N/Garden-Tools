#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <fo4ide/mcp/McpServer.hpp>
#include <fo4ide/settings/Settings.hpp>

namespace fo4ide::app {

enum class PathUse { Read, Write, Compiler, DataRelative, PluginName };
enum class PathForm { Single, JsonList, SingleOrObject, SemicolonList, LineList };

struct PathArgument {
    std::string name;
    PathUse use;
    PathForm form;
};

using AutoRootResolver = std::function<std::vector<std::filesystem::path>()>;

std::function<bool(const std::filesystem::path&)> readAllowedFilter(const fo4ide::settings::SharedSettings& settings,
                                                                  AutoRootResolver autoRoots);

std::shared_ptr<fo4ide::mcp::Tool> withPathPolicy(std::shared_ptr<fo4ide::mcp::Tool> tool,
                                                  std::vector<PathArgument> paths,
                                                  const fo4ide::settings::SharedSettings& settings,
                                                  AutoRootResolver autoRoots);

}
