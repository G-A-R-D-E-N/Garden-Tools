#pragma once


#include <fo4/plugin/container.hpp>

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace fo4ide::plugin {

enum class Severity {
    info = 0,
    warning = 1,
    error = 2,
};

std::string_view severityName(Severity severity);

struct ProfileEntry {
    std::string name;
    std::filesystem::path path;
};

struct ValidationFinding {
    std::string code;
    Severity severity{Severity::error};
    std::string formKey;
    std::string editorId;
    std::string recordType;
    std::string field;
    std::string message;
};

struct ValidationOptions {
    const fo4::Plugin* plugin{nullptr};
    std::vector<std::filesystem::path> scriptsDirectories;
    const std::vector<ProfileEntry>* profile{nullptr};
    const nlohmann::json* contract{nullptr};
    Severity minimum{Severity::info};
};

struct ValidationReport {
    std::vector<ValidationFinding> findings;
    std::size_t errors{0};
    std::size_t warnings{0};
    std::size_t infos{0};

    bool clean() const noexcept { return errors == 0; }
    nlohmann::json toJson() const;
};

bool parseSeverity(std::string_view text, Severity& out);

ValidationReport validatePlugin(const ValidationOptions& options);

struct ContractParse {
    nlohmann::json value;
    std::string error;
};
ContractParse parseContract(std::string_view textOrPath);

}
