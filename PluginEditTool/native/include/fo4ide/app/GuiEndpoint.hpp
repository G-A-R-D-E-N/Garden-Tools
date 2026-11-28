#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <fo4ide/core/Result.hpp>

namespace fo4ide::app {

struct GuiEndpoint {
    std::uint32_t pid{0};
    int port{0};
    std::string token;
    std::string instance;
};

std::filesystem::path guiEndpointPath(const std::filesystem::path& configDir);
bool isAccessToken(std::string_view token);
nlohmann::json guiEndpointJson(const GuiEndpoint& endpoint);
Result<GuiEndpoint> parseGuiEndpoint(const nlohmann::json& json);
Result<GuiEndpoint> readGuiEndpoint(const std::filesystem::path& file);
Result<void> writeGuiEndpoint(const std::filesystem::path& file, const GuiEndpoint& endpoint);
void removeGuiEndpoint(const std::filesystem::path& file, std::uint32_t ownerPid);
bool isProcessAlive(std::uint32_t pid);
std::uint32_t currentProcessId();

}
