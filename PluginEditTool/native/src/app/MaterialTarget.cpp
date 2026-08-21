#include <fo4ide/app/MaterialTarget.hpp>

#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace fo4ide::app {
namespace {

std::string stringArgument(const nlohmann::json& args, std::size_t index) {
    if (!args.is_array() || index >= args.size() || !args[index].is_string()) return {};
    return args[index].get<std::string>();
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
        if (digit < 0) return std::unexpected(Error{"InvalidBase64", "Dropped material payload is not valid base64"});
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
        ("FO4RE_Mat_" + std::to_string(ticks) + "_" + std::to_string(thread));
}

}

MaterialTarget::MaterialTarget(material::MaterialService& service, platform::Services& platform)
    : service_(service), platform_(platform) {}

Result<nlohmann::json> MaterialTarget::invoke(std::string_view method,
                                               const nlohmann::json& args) {
    if (method == "BrowseForFile") {
        return pathResult(platform_.pickFile({stringArgument(args, 0), stringArgument(args, 1), {}}));
    }
    if (method == "Inspect") {
        auto inspected = service_.inspect(stringArgument(args, 0));
        if (!inspected) return nlohmann::json{{"error", "Error: " + inspected.error().message}}.dump();
        return inspected->dump();
    }
    if (method == "SetFields") {
        try {
            const auto fieldsJson = nlohmann::json::parse(stringArgument(args, 1));
            if (!fieldsJson.is_object()) return nlohmann::json("Error: fields must be a JSON object");
            std::map<std::string, std::string, std::less<>> fields;
            for (auto it = fieldsJson.begin(); it != fieldsJson.end(); ++it) {
                if (!it.value().is_string()) return nlohmann::json("Error: material field values must be strings");
                fields.emplace(it.key(), it.value().get<std::string>());
            }
            auto result = service_.setFields(stringArgument(args, 0), fields, stringArgument(args, 2));
            return nlohmann::json(result ? *result : "Error: " + result.error().message);
        } catch (const std::exception& ex) {
            return nlohmann::json("Error: " + std::string(ex.what()));
        }
    }
    if (method == "OpenFolder") {
        auto opened = platform_.openFolder(stringArgument(args, 0));
        return nlohmann::json(opened ? std::string{} : "Error: " + opened.error().message);
    }
    if (method == "StageDroppedFile") return stageDroppedFile(args);
    return std::unexpected(Error{"UnknownMethod", "Unknown material RPC method: " + std::string(method)});
}

Result<nlohmann::json> MaterialTarget::stageDroppedFile(const nlohmann::json& args) const {
    try {
        auto name = std::filesystem::path(stringArgument(args, 0)).filename();
        if (name.empty()) name = "dropped.bgsm";
        auto decoded = decodeBase64(stringArgument(args, 1));
        if (!decoded) return nlohmann::json("ERR:" + decoded.error().message);

        const auto directory = uniqueStageDirectory();
        std::filesystem::create_directories(directory);
        const auto path = directory / name;
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream) return nlohmann::json("ERR:Could not create staged material file");
        stream.write(reinterpret_cast<const char*>(decoded->data()), static_cast<std::streamsize>(decoded->size()));
        stream.close();
        if (!stream) return nlohmann::json("ERR:Could not write staged material file");
        return nlohmann::json(path.string());
    } catch (const std::exception& ex) {
        return nlohmann::json("ERR:" + std::string(ex.what()));
    }
}

}
