#pragma once

#include <fo4ide/plugin/RadiclePluginBackend.hpp>
#include <fo4/text/encoding.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <fo4/plugin/mutation.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/text/encoding.hpp>

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

inline Result<std::optional<std::string>> encodeOptionalEditorId(
    const std::optional<std::string>& utf8) {
    if (!utf8.has_value()) return std::optional<std::string>{};
    auto stored = encodeEditorId(*utf8);
    if (!stored) return std::unexpected(stored.error());
    return std::optional<std::string>{std::move(*stored)};
}

inline std::string asciiLower(std::string_view value) {
    std::string out(value);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
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
    return plugin.header.tes4_flags.is_small_file() ? std::string{} : " " + std::string(lightFlagHint);
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
