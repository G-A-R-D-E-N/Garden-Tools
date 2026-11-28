#include "PluginCheck.hpp"

#include <cstdio>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>


#include <fo4/plugin/reader.hpp>

#include <fo4ide/plugin/PluginValidation.hpp>

namespace fo4ide::app {
namespace {

constexpr int k_exit_clean = 0;
constexpr int k_exit_findings = 1;
constexpr int k_exit_cannot_run = 2;

struct CheckArgs {
    std::string plugin;
    std::string contract;
    std::string scripts;
    bool hasContract{false};
    bool json{false};
    bool strict{false};
    bool help{false};
};

void usage(std::ostream& out) {
    out << "FO4RecordEditor --check <plugin.esp> [--contract <json>] [--json] [--strict]\n"
           "                 [--scripts <dir>]\n"
           "\n"
           "Validates one plugin without a window, a server or an MO2 profile. Structural\n"
           "checks (HEDR, FormIDs, required subrecords, effect lists, quest scripts) run\n"
           "from the file alone; master presence and load order need a profile and are\n"
           "reported as skipped. A compiled-script comparison uses <dir>/Scripts beside the\n"
           "plugin unless --scripts names another folder.\n"
           "\n"
           "Exit status: 0 clean, 1 findings (`--strict` makes warnings fail too),\n"
           "2 when the check could not run (missing file, unreadable plugin, bad contract).\n";
}

bool checkRequested(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--check") return true;
    }
    return false;
}

bool parseArgs(int argc, char** argv, CheckArgs& args) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        const auto next = [&](std::string& target) {
            if (i + 1 >= argc || std::string_view(argv[i + 1]).starts_with("--")) return false;
            target = argv[++i];
            return true;
        };
        if (arg == "--check") {
            if (!next(args.plugin)) return false;
        } else if (arg == "--contract") {
            args.hasContract = true;
            if (!next(args.contract)) return false;
        } else if (arg == "--scripts") {
            if (!next(args.scripts)) return false;
        } else if (arg == "--json") {
            args.json = true;
        } else if (arg == "--strict") {
            args.strict = true;
        } else if (arg == "--help" || arg == "-h") {
            args.help = true;
        } else {
            return false;
        }
    }
    return true;
}

std::filesystem::path resolveScriptsDirectory(const CheckArgs& args,
                                             const std::filesystem::path& pluginPath) {
    std::error_code ec;
    if (!args.scripts.empty()) return args.scripts;
    const auto besidePlugin = pluginPath.parent_path() / "Scripts";
    if (std::filesystem::is_directory(besidePlugin, ec) && !ec) return besidePlugin;
    return {};
}

void printText(const std::string& pluginName,
               const plugin::ValidationReport& report,
               bool strict) {
    std::cout << pluginName << ": " << report.errors << " error(s), " << report.warnings
              << " warning(s), " << report.infos << " note(s)\n";
    for (const auto& finding : report.findings) {
        std::cout << plugin::severityName(finding.severity) << " " << finding.code;
        if (!finding.formKey.empty()) std::cout << " " << finding.formKey;
        if (!finding.editorId.empty()) std::cout << " (" << finding.editorId << ")";
        if (!finding.field.empty()) std::cout << " [" << finding.field << "]";
        std::cout << ": " << finding.message << "\n";
    }
    if (strict && report.errors == 0 && report.warnings > 0) {
        std::cout << "--strict: warnings fail the check\n";
    }
}

}

bool runPluginCheckIfRequested(int argc, char** argv, int& exitCode) {
    if (!checkRequested(argc, argv)) return false;

    CheckArgs args;
    if (!parseArgs(argc, argv, args)) {
        usage(std::cerr);
        exitCode = k_exit_cannot_run;
        return true;
    }
    if (args.help) {
        usage(std::cout);
        exitCode = k_exit_clean;
        return true;
    }
    if (args.plugin.empty()) {
        std::cerr << "--check needs a plugin path\n";
        usage(std::cerr);
        exitCode = k_exit_cannot_run;
        return true;
    }

    const std::filesystem::path pluginPath(args.plugin);
    std::error_code ec;
    if (!std::filesystem::is_regular_file(pluginPath, ec) || ec) {
        std::cerr << "PluginsNotFound: " << pluginPath.string() << " is not a readable file\n";
        exitCode = k_exit_cannot_run;
        return true;
    }

    if (!args.scripts.empty() && (!std::filesystem::is_directory(args.scripts, ec) || ec)) {
        std::cerr << "ScriptsNotFound: " << args.scripts << " is not a readable folder\n";
        exitCode = k_exit_cannot_run;
        return true;
    }

    fo4::Plugin plugin;
    try {
        plugin = fo4::read_plugin_file(pluginPath);
    } catch (const std::exception& error) {
        std::cerr << "PluginUnreadable: " << pluginPath.filename().string() << ": "
                  << error.what() << "\n";
        exitCode = k_exit_cannot_run;
        return true;
    }

    std::optional<nlohmann::json> contract;
    if (args.hasContract) {
        if (args.contract.empty()) {
            std::cerr << "InvalidContract: Contract is empty\n";
            exitCode = k_exit_cannot_run;
            return true;
        }
        const std::filesystem::path contractPath(args.contract);
        if (!std::filesystem::is_regular_file(contractPath, ec) || ec) {
            std::cerr << "ContractNotFound: " << contractPath.string() << " is not a readable file\n";
            exitCode = k_exit_cannot_run;
            return true;
        }
        auto parsed = plugin::parseContract(args.contract);
        if (!parsed.error.empty()) {
            std::cerr << "InvalidContract: " << parsed.error << "\n";
            exitCode = k_exit_cannot_run;
            return true;
        }
        contract = std::move(parsed.value);
    }

    plugin::ValidationOptions options;
    options.plugin = &plugin;
    const auto scriptsDirectory = resolveScriptsDirectory(args, pluginPath);
    if (!scriptsDirectory.empty()) options.scriptsDirectories = {scriptsDirectory};
    options.contract = contract.has_value() ? &contract.value() : nullptr;
    options.minimum = plugin::Severity::info;

    const auto report = plugin::validatePlugin(options);
    const auto pluginName = pluginPath.filename().string();

    if (args.json) {
        auto json = report.toJson();
        json["plugin"] = pluginName;
        json["severityFilter"] = "info";
        json["strict"] = args.strict;
        std::cout << json.dump(2) << "\n";
    } else {
        printText(pluginName, report, args.strict);
    }

    const bool failed = report.errors > 0 || (args.strict && report.warnings > 0);
    exitCode = failed ? k_exit_findings : k_exit_clean;
    return true;
}

}
