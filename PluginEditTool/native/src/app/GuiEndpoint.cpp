#include <fo4ide/app/GuiEndpoint.hpp>

#include <cerrno>
#include <cstddef>
#include <fstream>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <csignal>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace fo4ide::app {
namespace {

constexpr std::string_view endpointFileName = "gui-endpoint.json";

bool isLowerHexDigit(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

Error invalid(std::string message) {
    return Error{"GuiEndpointInvalid", std::move(message)};
}

}

std::filesystem::path guiEndpointPath(const std::filesystem::path& configDir) {
    return configDir / std::string(endpointFileName);
}

bool isAccessToken(std::string_view token) {
    if (token.size() != 32) return false;
    for (const char c : token) {
        if (!isLowerHexDigit(c)) return false;
    }
    return true;
}

nlohmann::json guiEndpointJson(const GuiEndpoint& endpoint) {
    return {
        {"pid", endpoint.pid},
        {"port", endpoint.port},
        {"token", endpoint.token},
        {"instance", endpoint.instance}
    };
}

Result<GuiEndpoint> parseGuiEndpoint(const nlohmann::json& json) {
    if (!json.is_object()) return std::unexpected(invalid("GUI endpoint must be a JSON object"));
    if (!json.contains("pid") || !json.at("pid").is_number_unsigned()) {
        return std::unexpected(invalid("GUI endpoint pid must be a positive integer"));
    }
    const auto pid = json.at("pid").get<std::uint64_t>();
    if (pid == 0 || pid > 0xFFFFFFFFull) return std::unexpected(invalid("GUI endpoint pid is out of range"));

    if (!json.contains("port") || !json.at("port").is_number_integer()) {
        return std::unexpected(invalid("GUI endpoint port must be an integer"));
    }
    const auto port = json.at("port").get<std::int64_t>();
    if (port < 1 || port > 65535) return std::unexpected(invalid("GUI endpoint port is out of range"));

    if (!json.contains("token") || !json.at("token").is_string()) {
        return std::unexpected(invalid("GUI endpoint token must be a string"));
    }
    const auto token = json.at("token").get<std::string>();
    if (!isAccessToken(token)) return std::unexpected(invalid("GUI endpoint token is malformed"));

    std::string instance;
    if (json.contains("instance")) {
        if (!json.at("instance").is_string()) return std::unexpected(invalid("GUI endpoint instance must be a string"));
        instance = json.at("instance").get<std::string>();
    }

    return GuiEndpoint{static_cast<std::uint32_t>(pid), static_cast<int>(port), token, instance};
}

Result<GuiEndpoint> readGuiEndpoint(const std::filesystem::path& file) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec)) {
        return std::unexpected(Error{"GuiEndpointMissing", "GUI endpoint file not found: " + file.string()});
    }
    std::ifstream input(file, std::ios::binary);
    if (!input) return std::unexpected(Error{"GuiEndpointUnreadable", "GUI endpoint file cannot be read"});
    std::ostringstream text;
    text << input.rdbuf();

    const auto json = nlohmann::json::parse(text.str(), nullptr, false);
    if (json.is_discarded()) return std::unexpected(invalid("GUI endpoint file is not valid JSON"));
    return parseGuiEndpoint(json);
}

Result<void> writeGuiEndpoint(const std::filesystem::path& file, const GuiEndpoint& endpoint) {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    auto temp = file;
    temp += ".tmp";
#if !defined(_WIN32)
    // Constraint: the temp file is owner-only before the token is written into it.
    if (const int fd = ::open(temp.string().c_str(), O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR); fd >= 0) {
        ::close(fd);
    }
    ::chmod(temp.string().c_str(), S_IRUSR | S_IWUSR);
#endif
    {
        std::ofstream output(temp, std::ios::binary | std::ios::trunc);
        if (!output) return std::unexpected(Error{"GuiEndpointWrite", "GUI endpoint file cannot be created"});
        output << guiEndpointJson(endpoint).dump(2);
        if (!output) return std::unexpected(Error{"GuiEndpointWrite", "GUI endpoint file cannot be written"});
    }
    std::filesystem::rename(temp, file, ec);
    if (ec) {
        std::filesystem::remove(temp, ec);
        return std::unexpected(Error{"GuiEndpointWrite", "GUI endpoint file cannot be replaced"});
    }
    return {};
}

// Invariant: only the process the file names removes it, so a GUI that exits leaves a newer GUI's endpoint in place.
void removeGuiEndpoint(const std::filesystem::path& file, std::uint32_t ownerPid) {
    const auto current = readGuiEndpoint(file);
    if (!current || current->pid != ownerPid) return;
    std::error_code ec;
    std::filesystem::remove(file, ec);
}

bool isProcessAlive(std::uint32_t pid) {
    if (pid == 0) return false;
#if defined(_WIN32)
    const HANDLE process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!process) return ::GetLastError() == ERROR_ACCESS_DENIED;
    DWORD code = 0;
    const bool alive = ::GetExitCodeProcess(process, &code) != 0 && code == STILL_ACTIVE;
    ::CloseHandle(process);
    return alive;
#else
    if (::kill(static_cast<pid_t>(pid), 0) == 0) return true;
    return errno == EPERM;
#endif
}

std::uint32_t currentProcessId() {
#if defined(_WIN32)
    return static_cast<std::uint32_t>(::GetCurrentProcessId());
#else
    return static_cast<std::uint32_t>(::getpid());
#endif
}

}
