#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include <cstddef>
#include <exception>
#include <string>
#include <utility>
#include <vector>

#include "RadiclePluginSupport.hpp"

#include <fo4/environment/strings.hpp>
#include <fo4/forms/message_buttons.hpp>
#include <fo4/text/encoding.hpp>

namespace fo4ide::plugin {
namespace {

constexpr std::size_t k_max_buttons = 65535;

Error invalidButtons(const std::string& message) { return Error{"InvalidMessageButtons", message}; }

Result<std::vector<std::string>> buttonTextsFromJson(const nlohmann::json& argument) {
    nlohmann::json parsed = argument;
    if (parsed.is_null() || (parsed.is_string() && parsed.get<std::string>().find_first_not_of(" \t\r\n") == std::string::npos)) {
        parsed = nlohmann::json::array();
    } else if (parsed.is_string()) {
        parsed = nlohmann::json::parse(parsed.get<std::string>(), nullptr, false);
    }
    if (!parsed.is_array()) return std::unexpected(invalidButtons("buttons must be a JSON array"));
    if (parsed.size() > k_max_buttons) return std::unexpected(invalidButtons("A MESG record holds at most 65535 buttons"));

    std::vector<std::string> texts;
    texts.reserve(parsed.size());
    for (const auto& item : parsed) {
        const nlohmann::json* text = &item;
        if (item.is_object()) {
            const auto found = item.find("text");
            if (found == item.end()) return std::unexpected(invalidButtons("Every button needs a non-empty 'text'"));
            text = &*found;
        }
        if (!text->is_string()) return std::unexpected(invalidButtons("Every button must be a string or an object with a string 'text'"));
        auto value = text->get<std::string>();
        if (value.empty()) return std::unexpected(invalidButtons("Every button needs a non-empty 'text'"));
        if (value.find('\0') != std::string::npos) return std::unexpected(invalidButtons("A button text must not contain a NUL byte"));
        texts.push_back(std::move(value));
    }
    return texts;
}

Error messageButtonsWriteError(fo4::forms::MessageButtonsWrite status, const std::string& key) {
    using Write = fo4::forms::MessageButtonsWrite;
    switch (status) {
        case Write::written: break;
        case Write::record_missing: return Error{"RecordNotFound", "Record is not present: " + key};
        case Write::unsupported_record: return Error{"WrongRecordType", "set_message_buttons requires a MESG record: " + key};
        case Write::too_many_buttons: return invalidButtons("A MESG record holds at most 65535 buttons");
        case Write::invalid_text: return invalidButtons("A button text must be non-empty without a NUL byte");
        case Write::localization_mismatch:
            return Error{"MessageButtonsLocalization", "A button text does not match how this plugin stores localized text"};
        case Write::unexpected_layout:
            return Error{"MessageButtonsLayout", "The button or flag subrecords of " + key + " are not in the expected layout"};
        case Write::no_insertion_point:
            return Error{"ElementUnknownOrder", "A valid insertion order cannot be established for " + key};
    }
    return Error{"MessageButtonsWriteFailed", "Message buttons could not be written to " + key};
}

}  // namespace

Result<nlohmann::json> RadiclePluginBackend::setMessageButtons(const nlohmann::json& arguments) {
    const auto pluginName = support::stringArgument(arguments, 0);
    const auto key = support::formKeyArgument(support::stringArgument(arguments, 1));
    if (!key) return std::unexpected(key.error());
    const auto texts = buttonTextsFromJson(support::argumentAt(arguments, 2));
    if (!texts) return std::unexpected(texts.error());
    if (pluginName.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    if (const auto guard = writeGuard(*loaded)) return std::unexpected(*guard);

    const auto* record = fo4::find_record(loaded->plugin, *key);
    if (record == nullptr) return std::unexpected(Error{"RecordNotFound", "Record is not present: " + key->to_string()});
    const auto message = fo4::Signature::from_string("MESG");
    if (record->header.signature != message) {
        return std::unexpected(Error{"WrongRecordType", "set_message_buttons requires a MESG record: " + key->to_string()});
    }

    try {
        auto mutated = loaded->plugin;
        const bool localized = mutated.header.tes4_flags.is_localized();
        LocalizedStringAllocation strings;
        std::vector<fo4::schema::LocalizedString> values;
        values.reserve(texts->size());
        if (localized && !texts->empty()) {
            auto opened = openLocalizedStrings(*loaded, "en");
            if (!opened) return std::unexpected(opened.error());
            strings = std::move(*opened);
        }
        const auto source = fo4::env::string_source(message, fo4::Signature::from_string("ITXT"));
        for (const auto& text : *texts) {
            fo4::schema::LocalizedString value;
            if (localized) {
                const auto id = allocateLocalizedString(strings, source, text);
                if (!id) return std::unexpected(id.error());
                value.id = *id;
            } else {
                auto stored = fo4::text::utf8_to_windows1252(text);
                if (!stored) return std::unexpected(invalidButtons("Button text must be valid UTF-8 representable in Windows-1252"));
                value.text = std::move(*stored);
            }
            values.push_back(std::move(value));
        }

        const auto status = fo4::forms::set_message_buttons(mutated, *key, values);
        if (status != fo4::forms::MessageButtonsWrite::written) {
            return std::unexpected(messageButtonsWriteError(status, key->to_string()));
        }
        if (localized && !values.empty()) {
            if (const auto staged = stageLocalizedStringsAndPlugin(loaded->name, std::move(strings), std::move(mutated)); !staged) {
                return std::unexpected(staged.error());
            }
        } else {
            stagePluginMutationLocked(*loaded, std::move(mutated));
        }
    } catch (const std::exception& error) {
        return std::unexpected(Error{"MessageButtonsWriteFailed", error.what()});
    }
    return nlohmann::json("Set " + std::to_string(texts->size()) + " menu button(s) on " + key->to_string() + " in " +
                          pluginName + " and flagged it MessageBox. Show() returns the zero-based button index. Call save_plugin to persist.");
}

}  // namespace fo4ide::plugin
