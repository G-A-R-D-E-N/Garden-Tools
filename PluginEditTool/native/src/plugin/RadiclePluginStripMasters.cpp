#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "RadiclePluginSupport.hpp"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <system_error>
#include <vector>

#include <fo4/plugin/strip_masters.hpp>
#include <fo4ide/core/ProtectedWriteName.hpp>

namespace fo4ide::plugin {
namespace {

std::filesystem::path utf8Path(const std::string& text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

}

Result<nlohmann::json> RadiclePluginBackend::stripMastersClean(const nlohmann::json& arguments) {
    const auto sourceText = support::stringArgument(arguments, 0);
    const auto mastersText = support::stringArgument(arguments, 1);
    const auto outputText = support::stringArgument(arguments, 2);
    if (sourceText.empty()) return std::unexpected(Error{"SourceMissing", "source_path is empty"});
    bool dryRun = true;
    if (arguments.is_array() && arguments.size() > 3 && !arguments[3].is_null() &&
        !support::tryBoolArgument(arguments, 3, dryRun)) {
        return std::unexpected(Error{"InvalidDryRunFlag", "dry_run must be boolean"});
    }

    nlohmann::json namesJson;
    try {
        namesJson = nlohmann::json::parse(mastersText.empty() ? "[]" : mastersText);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InvalidMasters", error.what()});
    }
    if (!namesJson.is_array()) return std::unexpected(Error{"InvalidMasters", "masters must be a JSON array of file names"});
    std::set<fo4::PluginKey> strip;
    for (const auto& item : namesJson) {
        if (!item.is_string()) return std::unexpected(Error{"InvalidMasters", "master names must be strings"});
        if (item.get<std::string>().empty()) continue;
        try {
            strip.insert(fo4::PluginKey::from_filename(item.get<std::string>()));
        } catch (const std::exception& error) {
            return std::unexpected(Error{"InvalidMasters", error.what()});
        }
    }
    if (strip.empty()) return std::unexpected(Error{"MastersMissing", "Provide at least one master name to strip"});

    const std::filesystem::path source = utf8Path(sourceText);
    std::error_code existsEc;
    if (!std::filesystem::is_regular_file(source, existsEc)) {
        return std::unexpected(Error{"SourceNotFound", "Source file not found: " + sourceText});
    }

    std::optional<std::filesystem::path> output;
    std::optional<OpLock> lock;
    if (!dryRun) {
        if (outputText.empty()) return std::unexpected(Error{"OutputMissing", "output_path is required when dry_run is false"});
        const auto requested = utf8Path(outputText);
        if (const auto denied = core::protectedWriteName(requested.filename().string())) {
            return std::unexpected(Error{"SavePathNotAllowed", "Refusing to write " + outputText + ": " + *denied});
        }
        lock.emplace(*this, OpLock::Mode::Read);
        const auto allowed = allowedSavePath(requested);
        if (!allowed) {
            return std::unexpected(Error{
                "SavePathNotAllowed",
                "Refusing to write: output must be a .esp/.esm/.esl inside a directory hosting a trusted, editable loaded plugin: " +
                    outputText});
        }
        for (const auto& loaded : plugins_) {
            std::error_code loadedEc;
            const auto loadedPath = std::filesystem::weakly_canonical(loaded.path, loadedEc);
            if (!loadedEc && loadedPath == *allowed) {
                return std::unexpected(Error{"OutputIsLoadedPlugin",
                                             "Refusing to overwrite a loaded plugin's file: " + outputText});
            }
        }
        output = *allowed;
    }

    const auto report = fo4::strip_masters_clean(source, strip, output);
    if (!report) return std::unexpected(Error{report.error().code, report.error().message});

    const auto fileName = source.filename().string();
    auto summary = "Loaded '" + fileName + "': " + std::to_string(report->dropped_conditions) + " condition(s) across " +
                   std::to_string(report->touched_records) + " record(s) reference the stripped master(s). " +
                   (report->targets.empty() ? std::string{"None of the named masters are declared in this plugin's header."}
                                            : "Header master(s) named: " + support::joinNames(report->targets) + ".") +
                   " Removable: " + (report->removed.empty() ? std::string{"none"} : support::joinNames(report->removed)) + ".";
    if (!report->survivors.empty()) {
        summary += " STILL USED by records, so kept: " + support::joinNames(report->survivors) + ".";
    }
    if (dryRun) return nlohmann::json("[DRY RUN] " + summary + " Pass dry_run=false to write.");
    if (!report->written) return nlohmann::json(summary + " Nothing to do; file left untouched.");

    return nlohmann::json("Saved to '" + output->string() + "'. Removed " + std::to_string(report->removed.size()) +
                          " of " + std::to_string(report->targets.size()) + " named header master(s); dropped " +
                          std::to_string(report->dropped_conditions) + " condition(s) across " +
                          std::to_string(report->touched_records) + " record(s). Masters now: [" +
                          support::joinNames(report->masters_after) + "]." +
                          (report->survivors.empty() ? std::string{}
                                                     : " STILL PRESENT (records reference them): " +
                                                           support::joinNames(report->survivors) + "."));
}

}
