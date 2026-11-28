#include <fo4ide/app/MaterialTarget.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "ServiceTargetSupport.hpp"

namespace fo4ide::app {
namespace {

nlohmann::json pathResult(Result<std::filesystem::path> result) {
    if (!result) return std::string{};
    return result->string();
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
        return pathResult(platform_.pickFile({support::stringArgument(args, 0), support::stringArgument(args, 1), {}}));
    }
    if (method == "Inspect") {
        auto inspected = service_.inspect(support::stringArgument(args, 0));
        if (!inspected) return nlohmann::json{{"error", "Error: " + inspected.error().message}}.dump();
        return inspected->dump();
    }
    if (method == "SetFields") {
        try {
            const auto fieldsJson = nlohmann::json::parse(support::stringArgument(args, 1));
            if (!fieldsJson.is_object()) return nlohmann::json("Error: fields must be a JSON object");
            std::map<std::string, std::string, std::less<>> fields;
            for (auto it = fieldsJson.begin(); it != fieldsJson.end(); ++it) {
                if (!it.value().is_string()) return nlohmann::json("Error: material field values must be strings");
                fields.emplace(it.key(), it.value().get<std::string>());
            }
            auto result = service_.setFields(support::stringArgument(args, 0), fields, support::stringArgument(args, 2));
            return nlohmann::json(result ? *result : "Error: " + result.error().message);
        } catch (const std::exception& ex) {
            return nlohmann::json("Error: " + std::string(ex.what()));
        }
    }
    if (method == "OpenFolder") {
        auto opened = platform_.openFolder(support::stringArgument(args, 0));
        return nlohmann::json(opened ? std::string{} : "Error: " + opened.error().message);
    }
    if (method == "StageDroppedFile") return stageDroppedFile(args);
    return std::unexpected(Error{"UnknownMethod", "Unknown material RPC method: " + std::string(method)});
}

Result<nlohmann::json> MaterialTarget::stageDroppedFile(const nlohmann::json& args) const {
    try {
        auto name = std::filesystem::path(support::stringArgument(args, 0)).filename();
        if (name.empty()) name = "dropped.bgsm";
        auto decoded = support::decodeBase64(support::stringArgument(args, 1));
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
