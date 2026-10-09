#pragma once

#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <fo4/plugin/mutation.hpp>

namespace fo4ide::plugin::support {

struct EslSlots {
    std::uint32_t first;
    std::uint32_t last;
    std::size_t capacity;
    std::string range;
};

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
        return std::string(begin, nul);
    }
    return {};
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

}
