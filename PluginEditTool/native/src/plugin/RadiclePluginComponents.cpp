#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <fo4/forms/components.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/plugin/writer.hpp>
#include <fo4/signature.hpp>

namespace fo4ide::plugin {
namespace {

std::string componentStringArg(const nlohmann::json& args, std::size_t index) {
    if (!args.is_array() || index >= args.size() || !args[index].is_string()) return {};
    return args[index].get<std::string>();
}

std::string componentLower(std::string_view value) {
    std::string out(value);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return out;
}

std::string componentEditorId(const fo4::Record& record) {
    for (const auto& subrecord : record.subrecords) {
        if (subrecord.signature != fo4::sig_edid || subrecord.data.empty()) continue;
        const auto* begin = reinterpret_cast<const char*>(subrecord.data.data());
        const auto* end = begin + subrecord.data.size();
        const auto* nul = std::find(begin, end, '\0');
        return std::string(begin, nul);
    }
    return {};
}

const fo4::Record* resolveComponentRecord(const fo4::Plugin& plugin,
                                          std::string_view recordText,
                                          fo4::FormKey& outKey) {
    if (recordText.find(':') != std::string_view::npos) {
        try {
            outKey = fo4::FormKey::parse(recordText);
        } catch (const std::exception&) {
            return nullptr;
        }
        return fo4::find_record(plugin, outKey);
    }
    const auto wanted = componentLower(recordText);
    for (const auto* record : fo4::collect_records(plugin)) {
        if (componentLower(componentEditorId(*record)) == wanted) {
            outKey = record->form_key;
            return record;
        }
    }
    return nullptr;
}

std::uint32_t componentCount(const nlohmann::json& element) {
    if (!element.contains("count")) return 1;
    const auto& count = element["count"];
    if (count.is_number_unsigned()) return count.get<std::uint32_t>();
    if (count.is_number_integer()) {
        const auto value = count.get<std::int64_t>();
        return value > 0 ? static_cast<std::uint32_t>(value) : 0u;
    }
    return 1;
}

}  // namespace

Result<nlohmann::json> RadiclePluginBackend::setComponents(
    const nlohmann::json& arguments) {
    const auto pluginName = componentStringArg(arguments, 0);
    const auto recordText = componentStringArg(arguments, 1);
    const auto componentsJson = componentStringArg(arguments, 2);
    if (pluginName.empty()) {
        return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    }
    if (recordText.empty()) {
        return std::unexpected(Error{"RecordMissing", "Record is empty"});
    }

    std::scoped_lock lock(mutex_);
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }
    if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);

    fo4::FormKey recordKey;
    const auto* record = resolveComponentRecord(loaded->plugin, recordText, recordKey);
    if (record == nullptr) {
        return std::unexpected(Error{
            "RecordNotFound", "Record '" + recordText + "' not found in " + pluginName + "."});
    }

    const auto signature = record->header.signature;
    if (!fo4::forms::record_supports_components(signature)) {
        return std::unexpected(Error{
            "ComponentsUnsupported",
            "set_components applies to COBJ (recipe components) and MISC (scrap components); " +
                recordText + " is " + signature.to_string() + "."});
    }
    const bool isMisc = signature == fo4::Signature::from_string("MISC");
    const auto cmpo = fo4::Signature::from_string("CMPO");

    nlohmann::json document;
    try {
        document = nlohmann::json::parse(componentsJson.empty() ? "[]" : componentsJson);
    } catch (const std::exception& error) {
        return std::unexpected(Error{
            "InvalidComponentsJson",
            "Could not parse components JSON: " + std::string(error.what()) +
                R"(. Expected [{"component":"01FAA5:Fallout4.esm","count":1}].)"});
    }
    if (!document.is_array()) {
        return std::unexpected(Error{"InvalidComponentsJson", "Components JSON must be an array."});
    }

    std::vector<fo4::forms::ComponentEntry> entries;
    std::vector<std::string> failures;
    for (const auto& element : document) {
        if (!element.is_object()) {
            failures.push_back("(non-object entry)");
            continue;
        }
        const auto component =
            element.contains("component") && element["component"].is_string()
                ? element["component"].get<std::string>()
                : std::string{};
        if (component.empty()) {
            failures.push_back("(missing component)");
            continue;
        }
        const auto resolved = resolveListValue(component);
        if (!resolved.has_value()) {
            failures.push_back(component + ": unresolved");
            continue;
        }
        const auto* winner = graph_.winner(*resolved);
        if (winner == nullptr) {
            failures.push_back(component + ": unresolved");
            continue;
        }
        if (isMisc && winner->signature != cmpo) {
            failures.push_back(component + ": " + winner->signature.to_string() +
                               " is not a CMPO component");
            continue;
        }
        entries.push_back(fo4::forms::ComponentEntry{*resolved, componentCount(element)});
    }

    try {
        auto mutated = loaded->plugin;
        for (const auto& entry : entries) {
            ensureOwnerMaster(mutated, entry.component.owner());
        }
        const auto written = fo4::forms::set_record_components(
            mutated, recordKey,
            std::span<const fo4::forms::ComponentEntry>{entries});

        fo4::write_plugin_file(mutated, loaded->path);
        loaded->plugin = std::move(mutated);
        {
            std::error_code ec;
            loaded->loadedTime = std::filesystem::last_write_time(loaded->path, ec);
        }
        rebuildGraphLocked();

        std::string message = "Set " + std::to_string(written) + " component(s) on " +
                              recordText + " in " + pluginName + ".";
        if (!failures.empty()) {
            message += " Skipped " + std::to_string(failures.size()) + ": ";
            for (std::size_t i = 0; i < failures.size(); ++i) {
                if (i > 0) message += "; ";
                message += failures[i];
            }
            message += ".";
        }
        message += " Written to disk.";
        return nlohmann::json(std::move(message));
    } catch (const fo4::forms::ComponentError& error) {
        return std::unexpected(Error{"SetComponentsFailed", error.what()});
    } catch (const fo4::FormIdError& error) {
        return std::unexpected(Error{"FormLinkEncodingFailed", error.what()});
    } catch (const std::exception& error) {
        return std::unexpected(Error{"SetComponentsFailed", error.what()});
    }
}

}  // namespace fo4ide::plugin
