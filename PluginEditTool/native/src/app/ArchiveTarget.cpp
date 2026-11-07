#include <fo4ide/app/ArchiveTarget.hpp>
#include <fo4ide/core/JsonInt.hpp>

#include <filesystem>
#include <string>
#include <vector>

#include "ServiceTargetSupport.hpp"

namespace fo4ide::app {
namespace {

int intArgument(const nlohmann::json& args, std::size_t index, int fallback) {
    if (!args.is_array() || index >= args.size()) return fallback;
    return core::jsonIntSaturated(args[index]).value_or(fallback);
}

bool boolArgument(const nlohmann::json& args, std::size_t index) {
    return args.is_array() && index < args.size() && args[index].is_boolean() && args[index].get<bool>();
}

nlohmann::json pathResult(Result<std::filesystem::path> result) {
    if (!result) return std::string{};
    return result->string();
}

nlohmann::json textOrError(Result<std::string> result) {
    return result ? nlohmann::json(*result) : nlohmann::json("Error: " + result.error().message);
}

nlohmann::json jsonStringOrError(Result<nlohmann::json> result) {
    if (!result) return nlohmann::json{{"error", "Error: " + result.error().message}}.dump();
    return result->dump();
}

}

ArchiveTarget::ArchiveTarget(archive::ArchiveService& service, platform::Services& platform)
    : service_(service), platform_(platform) {}

Result<nlohmann::json> ArchiveTarget::invoke(std::string_view method,
                                              const nlohmann::json& args) {
    if (method == "BrowseForFile") {
        return pathResult(platform_.pickFile({support::stringArgument(args, 0), support::stringArgument(args, 1), {}}));
    }
    if (method == "BrowseForFolder") {
        return pathResult(platform_.pickFolder({support::stringArgument(args, 0), {}, {}}));
    }
    if (method == "BrowseForSave") {
        return pathResult(platform_.pickSavePath({support::stringArgument(args, 0), support::stringArgument(args, 1), {}}));
    }
    if (method == "List") {
        return jsonStringOrError(service_.list(
            support::stringArgument(args, 0), support::stringArgument(args, 1),
            intArgument(args, 2, 2000), support::stringArgument(args, 3)));
    }
    if (method == "ExtractFile") {
        return textOrError(service_.extractFile(
            support::stringArgument(args, 0), support::stringArgument(args, 1), support::stringArgument(args, 2)));
    }
    if (method == "ExtractSelected") {
        try {
            const auto raw = nlohmann::json::parse(support::stringArgument(args, 1));
            if (!raw.is_array()) return nlohmann::json("Error: selected archive paths must be a JSON array");
            std::vector<std::string> paths;
            for (const auto& value : raw) {
                if (!value.is_string()) return nlohmann::json("Error: selected archive paths must be strings");
                paths.push_back(value.get<std::string>());
            }
            return textOrError(service_.extractSelected(
                support::stringArgument(args, 0), paths, support::stringArgument(args, 2)));
        } catch (const std::exception& ex) {
            return nlohmann::json("Error: " + std::string(ex.what()));
        }
    }
    if (method == "ExtractAll") {
        return textOrError(service_.extractAll(
            support::stringArgument(args, 0), support::stringArgument(args, 1), support::stringArgument(args, 2),
            intArgument(args, 3, 5000), support::stringArgument(args, 4)));
    }
    if (method == "Compare") {
        return jsonStringOrError(service_.compare(support::stringArgument(args, 0), support::stringArgument(args, 1)));
    }
    if (method == "Pack") {
        try {
            const auto raw = nlohmann::json::parse(support::stringArgument(args, 0));
            if (!raw.is_array()) return nlohmann::json("Error: source paths must be a JSON array");
            std::vector<std::filesystem::path> paths;
            for (const auto& value : raw) {
                if (!value.is_string()) return nlohmann::json("Error: source paths must be strings");
                paths.emplace_back(value.get<std::string>());
            }
            return textOrError(service_.pack(
                paths, support::stringArgument(args, 1), support::stringArgument(args, 2),
                support::stringArgument(args, 3), boolArgument(args, 4)));
        } catch (const std::exception& ex) {
            return nlohmann::json("Error: " + std::string(ex.what()));
        }
    }
    if (method == "OpenFolder") {
        auto opened = platform_.openFolder(support::stringArgument(args, 0));
        return nlohmann::json(opened ? std::string{} : "Error: " + opened.error().message);
    }
    return std::unexpected(Error{"UnknownMethod", "Unknown archive RPC method: " + std::string(method)});
}

}
