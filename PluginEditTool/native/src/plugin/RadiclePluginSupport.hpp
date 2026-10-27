#pragma once

#include <fo4ide/plugin/RadiclePluginBackend.hpp>
#include <fo4/text/encoding.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <format>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

#include <fo4/plugin/mutation.hpp>
#include <fo4/plugin/reader.hpp>

namespace fo4ide::plugin::support {

struct EslSlots {
    std::uint32_t first;
    std::uint32_t last;
    std::size_t capacity;
    std::string range;
};

struct EslBlocker {
    fo4::FormKey key;
    std::string signature;
    bool nested;
};

inline constexpr const char* lightFlagHint =
    "Run masters.SetLight to flag the plugin light; Radicle does not flag it from a .esl file name.";

inline std::string stringArgument(const nlohmann::json& args, std::size_t index) {
    if (!args.is_array() || index >= args.size() || !args[index].is_string()) return {};
    return args[index].get<std::string>();
}

inline bool boolArgument(const nlohmann::json& args, std::size_t index, bool fallback = false) {
    if (!args.is_array() || index >= args.size() || !args[index].is_boolean()) return fallback;
    return args[index].get<bool>();
}

inline std::string localHex(std::uint32_t value) {
    return std::format("{:06X}", value);
}

inline nlohmann::json argumentAt(const nlohmann::json& args, std::size_t index) {
    return args.is_array() && index < args.size() ? args[index] : nlohmann::json();
}

template <class T>
std::optional<T> integerValue(const nlohmann::json& value) {
    if (!value.is_number_integer()) return std::nullopt;
    const auto raw = value.get<std::int64_t>();
    if (raw < static_cast<std::int64_t>(std::numeric_limits<T>::min()) ||
        raw > static_cast<std::int64_t>(std::numeric_limits<T>::max())) {
        return std::nullopt;
    }
    return static_cast<T>(raw);
}

template <class T>
std::optional<T> integerArgument(const nlohmann::json& args, std::size_t index) {
    if (!args.is_array() || index >= args.size()) return std::nullopt;
    return integerValue<T>(args[index]);
}

template <class T>
std::optional<T> integerField(const nlohmann::json& object, const char* name) {
    if (!object.is_object() || !object.contains(name)) return std::nullopt;
    return integerValue<T>(object.at(name));
}

inline std::string stringField(const nlohmann::json& object, const char* name) {
    if (!object.is_object() || !object.contains(name) || !object.at(name).is_string()) return {};
    return object.at(name).get<std::string>();
}

inline nlohmann::json dumpPayload(const nlohmann::json& payload) {
    return nlohmann::json(payload.dump());
}

inline bool parseIndex(std::string_view text, std::size_t& value) {
    if (text.empty()) return false;
    const auto* begin = text.data();
    const auto* end = begin + text.size();
    const auto [ptr, error] = std::from_chars(begin, end, value, 10);
    return error == std::errc{} && ptr == end;
}

struct AuthoringPath {
    fo4::Signature signature{};
    std::optional<std::size_t> occurrence;
};

inline std::optional<AuthoringPath> parseAuthoringPath(std::string_view path) {
    constexpr std::string_view prefix = "Authoring\\";
    if (!path.starts_with(prefix)) return std::nullopt;
    path.remove_prefix(prefix.size());
    if (path.size() < 4) return std::nullopt;
    AuthoringPath result{fo4::Signature::from_string(path.substr(0, 4)), std::nullopt};
    if (path.size() > 4) {
        if (path[4] != '[' || path.back() != ']') return std::nullopt;
        std::size_t index = 0;
        if (!parseIndex(path.substr(5, path.size() - 6), index)) return std::nullopt;
        result.occurrence = index;
    }
    return result;
}

inline Result<std::string> stringLanguage(const nlohmann::json& arguments) {
    auto language = stringArgument(arguments, 1);
    if (language.empty()) return std::string("en");
    const bool valid = std::all_of(language.begin(), language.end(), [](unsigned char ch) {
        return std::isalnum(ch) != 0;
    });
    if (!valid) {
        return std::unexpected(Error{
            "InvalidStringLanguage",
            "string table language must be an alphanumeric token"
        });
    }
    return language;
}

inline std::string displayText(std::string_view language, const std::vector<std::byte>& bytes) {
    const std::string raw(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (language != "en" && fo4::text::is_valid_utf8(raw)) return raw;
    return fo4::text::windows1252_to_utf8(raw);
}

inline Result<fo4::FormKey> formKeyArgument(std::string_view text) {
    if (text.empty()) return std::unexpected(Error{"FormKeyMissing", "FormKey is empty"});
    try {
        return fo4::FormKey::parse(text);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InvalidFormKey", error.what()});
    }
}

inline nlohmann::json formKeyJson(const fo4::FormKey& key) {
    return key == fo4::FormKey{} ? nlohmann::json(nullptr) : nlohmann::json(key.to_string());
}

template <class Plugins>
Result<const fo4::Plugin*> winningPlugin(const Plugins& plugins, const fo4::FormKey& key) {
    for (auto it = plugins.rbegin(); it != plugins.rend(); ++it) {
        const auto* record = fo4::find_record(it->plugin, key);
        if (record == nullptr) continue;
        if (record->header.flags.is_deleted()) {
            return std::unexpected(Error{
                "RecordDeleted",
                "The winning version of " + key.to_string() + " is deleted in " + it->name
            });
        }
        return &it->plugin;
    }
    return std::unexpected(Error{"RecordNotFound", "Record not found: " + key.to_string()});
}

inline const fo4::Record* topLevelRecord(const fo4::Plugin& plugin, const fo4::FormKey& key) {
    for (const auto& group : plugin.groups) {
        if (group.header.group_type != static_cast<std::int32_t>(fo4::GroupType::top)) continue;
        for (const auto& child : group.children) {
            const auto* record = std::get_if<fo4::Record>(&child);
            if (record != nullptr && record->form_key == key) return record;
        }
    }
    return nullptr;
}

inline std::string editorId(const fo4::Record& record) {
    for (const auto& subrecord : record.subrecords) {
        if (subrecord.signature != fo4::sig_edid || subrecord.data.empty()) continue;
        const auto* begin = reinterpret_cast<const char*>(subrecord.data.data());
        const auto* end = begin + subrecord.data.size();
        const auto* nul = std::find(begin, end, '\0');
        return fo4::text::windows1252_to_utf8(std::string(begin, nul));
    }
    return {};
}

inline Result<std::string> encodeEditorId(std::string_view utf8) {
    auto stored = fo4::text::utf8_to_windows1252(utf8);
    if (!stored) {
        return std::unexpected(Error{
            "InvalidEditorId",
            "EditorID is not valid UTF-8 text representable in Windows-1252"
        });
    }
    return std::move(*stored);
}

inline std::string asciiLower(std::string_view value) {
    std::string out(value);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return out;
}

inline std::string joinNames(const std::vector<std::string>& names) {
    std::string out;
    for (const auto& name : names) {
        if (!out.empty()) out += ", ";
        out += name;
    }
    return out;
}

inline EslSlots eslSlots(const fo4::Plugin& plugin) {
    fo4::Plugin targetPolicy;
    targetPolicy.header = plugin.header;
    targetPolicy.header.tes4_flags = fo4::RecordFlags{
        targetPolicy.header.tes4_flags.value() | fo4::RecordFlags::small_file};
    const auto [first, last] = fo4::local_form_id_range(targetPolicy);
    return {first, last, last - first + 1u, std::format("0x{:03X}-0x{:03X}", first, last)};
}

inline std::string lightFlagAdvice(const fo4::Plugin& plugin) {
    return fo4::classify_plugin_kind(plugin.header.tes4_flags, plugin.key) == fo4::PluginKind::light
        ? std::string{} : " " + std::string(lightFlagHint);
}

inline std::vector<EslBlocker> eslCompactionBlockers(const fo4::Plugin& plugin,
                                                     std::uint32_t first,
                                                     std::uint32_t last) {
    std::set<fo4::FormKey> topLevel;
    for (const auto& group : plugin.groups) {
        if (group.header.group_type != static_cast<std::int32_t>(fo4::GroupType::top)) continue;
        for (const auto& child : group.children) {
            const auto* record = std::get_if<fo4::Record>(&child);
            if (record != nullptr) topLevel.insert(record->form_key);
        }
    }
    std::vector<EslBlocker> blockers;
    for (const auto* record : fo4::collect_records(plugin)) {
        if (record->header.signature == fo4::sig_tes4 || record->form_key.owner() != plugin.key) {
            continue;
        }
        const auto local = record->form_key.local().value();
        if (local >= first && local <= last) continue;
        const auto isTopLevel = topLevel.contains(record->form_key);
        if (isTopLevel && !fo4::has_form_id_labelled_child_group(plugin, record->form_key)) continue;
        blockers.push_back({record->form_key, record->header.signature.to_string(), !isTopLevel});
    }
    return blockers;
}

inline Error parentGroupRelabelError(const fo4::FormKey& key) {
    return Error{
        "ParentGroupRelabelUnsupported",
        "Cannot renumber " + key.to_string() +
            ": it has child groups labelled with its form ID; parent GRUP label rewriting is not supported yet."
    };
}

inline std::optional<Error> parentGroupRelabelRefusal(const fo4::Plugin& plugin,
                                                      const fo4::FormKey& key) {
    if (!fo4::has_form_id_labelled_child_group(plugin, key)) return std::nullopt;
    return parentGroupRelabelError(key);
}

}
