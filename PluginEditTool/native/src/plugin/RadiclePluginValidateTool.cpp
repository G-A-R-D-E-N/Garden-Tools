#include <fo4ide/plugin/PluginValidation.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include <cstddef>
#include <cstdio>
#include <expected>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace fo4ide::plugin {
namespace {

std::string validateArgument(const nlohmann::json& arguments, std::size_t index) {
    if (!arguments.is_array() || index >= arguments.size() || !arguments[index].is_string()) {
        return {};
    }
    return arguments[index].get<std::string>();
}

std::vector<std::filesystem::path> scriptRootsFor(const std::filesystem::path& dataDirectory,
                                                  const std::filesystem::path& pluginPath) {
    std::vector<std::filesystem::path> roots;
    std::error_code ec;
    if (!dataDirectory.empty()) {
        const auto candidate = dataDirectory / "Scripts";
        if (std::filesystem::is_directory(candidate, ec) && !ec) roots.push_back(candidate);
    }
    const auto besidePlugin = pluginPath.parent_path() / "Scripts";
    if (std::filesystem::is_directory(besidePlugin, ec) && !ec) roots.push_back(besidePlugin);
    return roots;
}

}

Result<nlohmann::json> RadiclePluginBackend::runPluginValidation(
    const nlohmann::json& arguments) const {
    const auto pluginName = validateArgument(arguments, 0);
    std::string contractText;
    const bool suppliedContract = arguments.is_array() && arguments.size() > 1 && !arguments[1].is_null();
    if (suppliedContract) {
        if (arguments[1].is_object()) contractText = arguments[1].dump();
        else if (arguments[1].is_string()) contractText = arguments[1].get<std::string>();
        else return std::unexpected(Error{"InvalidContract", "contract must be an object or a string"});
    }
    const auto severityText = validateArgument(arguments, 2);

    if (pluginName.empty()) {
        return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    }

    Severity minimum = Severity::info;
    if (!severityText.empty() && !parseSeverity(severityText, minimum)) {
        return std::unexpected(Error{
            "InvalidSeverityFilter",
            "severity must be one of info, warning or error: " + severityText
        });
    }

    nlohmann::json contract;
    bool hasContract = false;
    if (suppliedContract) {
        auto parsed = parseContract(contractText);
        if (!parsed.error.empty()) {
            return std::unexpected(Error{"InvalidContract", std::move(parsed.error)});
        }
        contract = std::move(parsed.value);
        hasContract = true;
    }

    OpLock lock(*this, OpLock::Mode::Read);
    const auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }

    std::vector<ProfileEntry> profile;
    profile.reserve(plugins_.size());
    for (const auto& item : plugins_) {
        profile.push_back(ProfileEntry{item.name, item.path});
    }

    ValidationOptions options;
    options.plugin = &loaded->plugin;
    options.profile = &profile;
    options.contract = hasContract ? &contract : nullptr;
    options.minimum = minimum;
    options.scriptsDirectories = scriptRootsFor(dataDirectory_, loaded->path);

    const auto report = validatePlugin(options);
    auto json = report.toJson();
    json["plugin"] = loaded->name;
    json["severityFilter"] = std::string(severityName(minimum));
    return json;
}

Result<nlohmann::json> RadiclePluginBackend::getDuplicateFormIds(const nlohmann::json& arguments) {
    const auto pluginName = validateArgument(arguments, 0);
    if (pluginName.empty()) {
        return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    }

    OpLock lock(*this, OpLock::Mode::Read);
    const auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }

    const auto scan = duplicateFormIds_.scan(loaded->path);
    if (scan.error.has_value()) {
        return std::unexpected(Error{
            scan.is_io_error ? "DuplicateFormIdIo" : "DuplicateFormIdScan",
            *scan.error
        });
    }

    auto rows = nlohmann::json::array();
    for (const auto& entry : scan.duplicates) {
        char formId[16]{};
        std::snprintf(formId, sizeof(formId), "%08X", entry.raw_form_id);
        rows.push_back({
            {"FormId", std::string(formId)},
            {"Count", entry.count},
            {"RecordTypes", entry.record_types}
        });
    }
    return rows;
}

}
