#pragma once

#include <fo4ide/plugin/RadiclePluginBackend.hpp>
#include <fo4/text/encoding.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace fo4ide::plugin::support {

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

}
