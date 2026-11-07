#include "RadiclePluginSupport.hpp"

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

using support::editorId;

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
        if (componentLower(editorId(*record)) == wanted) {
            outKey = record->form_key;
            return record;
        }
    }
    return nullptr;
}

bool parseComponentCount(const nlohmann::json& element, std::uint32_t& out) {
    if (!element.contains("count")) {
        out = 1;
        return true;
    }
    const auto& count = element["count"];
    if (count.is_number_unsigned()) {
        const auto value = count.get<std::uint64_t>();
        if (value < 1 || value > 0xFFFFFFFFull) return false;
        out = static_cast<std::uint32_t>(value);
        return true;
    }
    if (count.is_number_integer()) {
        const auto value = count.get<std::int64_t>();
        if (value < 1 || value > 0xFFFFFFFFll) return false;
        out = static_cast<std::uint32_t>(value);
        return true;
    }
    return false;
}

bool isItemSignature(fo4::Signature signature) {
    static const fo4::Signature kItems[] = {
        fo4::Signature::from_string("AMMO"), fo4::Signature::from_string("ARMO"),
        fo4::Signature::from_string("BOOK"), fo4::Signature::from_string("CMPO"),
        fo4::Signature::from_string("COBJ"), fo4::Signature::from_string("NOTE"),
        fo4::Signature::from_string("ALCH"), fo4::Signature::from_string("INGR"),
        fo4::Signature::from_string("KEYM"), fo4::Signature::from_string("LVLI"),
        fo4::Signature::from_string("LIGH"), fo4::Signature::from_string("MISC"),
        fo4::Signature::from_string("WEAP"),
    };
    for (const auto item : kItems) {
        if (item == signature) return true;
    }
    return false;
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
    if (componentsJson.empty()) {
        return std::unexpected(Error{
            "InvalidComponentsJson",
            "components JSON is empty; pass [] to clear"
        });
    }

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
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
        document = nlohmann::json::parse(componentsJson);
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
    std::size_t index = 0;
    for (const auto& element : document) {
        const auto tag = "#" + std::to_string(++index) + ": ";
        if (!element.is_object()) {
            failures.push_back(tag + "(non-object entry)");
            continue;
        }
        const auto component =
            element.contains("component") && element["component"].is_string()
                ? element["component"].get<std::string>()
                : std::string{};
        if (component.empty()) {
            failures.push_back(tag + "(missing component)");
            continue;
        }
        const auto resolved = resolveListValue(component);
        if (!resolved.has_value()) {
            failures.push_back(tag + component + ": unresolved");
            continue;
        }
        const auto* winner = (*graph_).winner(*resolved);
        if (winner == nullptr) {
            failures.push_back(tag + component + ": unresolved");
            continue;
        }
        if (isMisc) {
            if (winner->signature != cmpo) {
                failures.push_back(tag + component + ": " + winner->signature.to_string() +
                                   " is not a CMPO component");
                continue;
            }
        } else if (!isItemSignature(winner->signature)) {
            failures.push_back(tag + component + ": " + winner->signature.to_string() +
                               " is not a valid recipe item");
            continue;
        }
        std::uint32_t count = 1;
        if (!parseComponentCount(element, count)) {
            failures.push_back(tag + component + ": count must be 1..4294967295");
            continue;
        }
        entries.push_back(fo4::forms::ComponentEntry{*resolved, count});
    }

    if (!failures.empty()) {
        std::string message = "Refused: " + std::to_string(failures.size()) + " of " +
                              std::to_string(document.size()) +
                              " component entries failed: ";
        for (std::size_t i = 0; i < failures.size(); ++i) {
            if (i > 0) message += "; ";
            message += failures[i];
        }
        return std::unexpected(Error{"ComponentEntriesRefused", message + "."});
    }

    try {
        auto mutated = loaded->plugin;
        for (const auto& entry : entries) {
            ensureOwnerMaster(mutated, entry.component.owner());
        }
        const auto written = fo4::forms::set_record_components(
            mutated, recordKey,
            std::span<const fo4::forms::ComponentEntry>{entries});

        stagePluginMutationLocked(*loaded, std::move(mutated));

        std::string message = "Set " + std::to_string(written) + " component(s) on " +
                              recordText + " in " + pluginName + ". SavePlugin to persist.";
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
