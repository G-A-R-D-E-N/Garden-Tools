#include <fo4ide/app/AudioTarget.hpp>
#include <fo4ide/core/JsonInt.hpp>

#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace fo4ide::app {
namespace {

std::string stringArgument(const nlohmann::json& args, std::size_t index) {
    if (!args.is_array() || index >= args.size() || !args[index].is_string()) return {};
    return args[index].get<std::string>();
}

bool boolArgument(const nlohmann::json& args, std::size_t index) {
    return args.is_array() && index < args.size() && args[index].is_boolean() && args[index].get<bool>();
}

int intArgument(const nlohmann::json& args, std::size_t index) {
    return args.is_array() && index < args.size() ? core::jsonInt(args[index]).value_or(0) : 0;
}

nlohmann::json serviceText(Result<std::string> result) {
    if (!result) return "Error: " + result.error().message;
    return std::move(*result);
}

nlohmann::json pathResult(Result<std::filesystem::path> result) {
    if (!result) return std::string{};
    return result->string();
}

int base64Value(unsigned char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

Result<std::vector<unsigned char>> decodeBase64(std::string_view input) {
    std::vector<unsigned char> output;
    output.reserve(input.size() * 3 / 4);
    unsigned int value = 0;
    int bits = -8;
    for (const unsigned char c : input) {
        if (std::isspace(c)) continue;
        if (c == '=') break;
        const int digit = base64Value(c);
        if (digit < 0) return std::unexpected(Error{"InvalidBase64", "Dropped audio payload is not valid base64"});
        value = (value << 6) | static_cast<unsigned int>(digit);
        bits += 6;
        if (bits >= 0) {
            output.push_back(static_cast<unsigned char>((value >> bits) & 0xFFU));
            bits -= 8;
        }
    }
    return output;
}

std::filesystem::path uniqueStageDirectory() {
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto thread = std::hash<std::thread::id>{}(std::this_thread::get_id());
    return std::filesystem::temp_directory_path() /
        ("FO4RE_Audio_" + std::to_string(ticks) + "_" + std::to_string(thread));
}

}

AudioTarget::AudioTarget(audio::AudioService& service, platform::Services& platform)
    : service_(service), platform_(platform) {}

Result<nlohmann::json> AudioTarget::invoke(std::string_view method,
                                            const nlohmann::json& args) {
    if (method == "BrowseForFile") {
        return pathResult(platform_.pickFile({stringArgument(args, 0), stringArgument(args, 1), {}}));
    }
    if (method == "BrowseForFolder") {
        return pathResult(platform_.pickFolder({stringArgument(args, 0), {}, {}}));
    }
    if (method == "BrowseForSave") {
        return pathResult(platform_.pickSavePath({stringArgument(args, 0), stringArgument(args, 1), {}}));
    }
    if (method == "ConvertToXwm") {
        const auto bitrate = intArgument(args, 2);
        return serviceText(service_.convertToXwm(
            stringArgument(args, 0), stringArgument(args, 1), bitrate > 0 ? std::optional<int>(bitrate) : std::nullopt));
    }
    if (method == "ConvertFromXwm") {
        return serviceText(service_.convertFromXwm(stringArgument(args, 0), stringArgument(args, 1), stringArgument(args, 2)));
    }
    if (method == "MakeFuz") {
        return serviceText(service_.makeFuz(stringArgument(args, 0), stringArgument(args, 1), stringArgument(args, 2), boolArgument(args, 3)));
    }
    if (method == "ExtractFuz") {
        return serviceText(service_.extractFuz(stringArgument(args, 0), stringArgument(args, 1), stringArgument(args, 2), boolArgument(args, 3)));
    }
    if (method == "OpenFolder") {
        auto opened = platform_.openFolder(stringArgument(args, 0));
        return nlohmann::json(opened ? std::string{} : "Error: " + opened.error().message);
    }
    if (method == "StageDroppedFile") return stageDroppedFile(args);
    return std::unexpected(Error{"UnknownMethod", "Unknown audio RPC method: " + std::string(method)});
}

Result<nlohmann::json> AudioTarget::stageDroppedFile(const nlohmann::json& args) const {
    try {
        auto name = std::filesystem::path(stringArgument(args, 0)).filename();
        if (name.empty()) name = "dropped.audio";
        auto decoded = decodeBase64(stringArgument(args, 1));
        if (!decoded) return nlohmann::json("ERR:" + decoded.error().message);

        const auto directory = uniqueStageDirectory();
        std::filesystem::create_directories(directory);
        const auto path = directory / name;
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream) return nlohmann::json("ERR:Could not create staged audio file");
        stream.write(reinterpret_cast<const char*>(decoded->data()), static_cast<std::streamsize>(decoded->size()));
        stream.close();
        if (!stream) return nlohmann::json("ERR:Could not write staged audio file");
        return nlohmann::json(path.string());
    } catch (const std::exception& ex) {
        return nlohmann::json("ERR:" + std::string(ex.what()));
    }
}

}
