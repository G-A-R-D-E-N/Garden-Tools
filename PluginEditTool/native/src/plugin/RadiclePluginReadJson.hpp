#pragma once

#include <fo4/plugin/reader.hpp>
#include <fo4/schema/ctda.hpp>
#include <fo4/signature.hpp>
#include <fo4/text/encoding.hpp>

#include <fo4ide/plugin/ConditionCodec.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fo4ide::plugin::readjson {

template <class T>
T readAt(const std::vector<std::byte>& payload, std::size_t offset) {
    T value{};
    if (offset + sizeof(T) <= payload.size()) std::memcpy(&value, payload.data() + offset, sizeof(T));
    return value;
}

inline bool sized(const fo4::Subrecord& subrecord, std::size_t size) {
    return subrecord.data.size() == size;
}

inline const fo4::Subrecord* firstOf(const fo4::Record& record, fo4::Signature signature) {
    for (const auto& subrecord : record.subrecords) {
        if (subrecord.signature == signature) return &subrecord;
    }
    return nullptr;
}

inline std::string cp1252Text(const std::vector<std::byte>& payload) {
    std::string_view bytes(reinterpret_cast<const char*>(payload.data()), payload.size());
    if (!bytes.empty() && bytes.back() == '\0') bytes.remove_suffix(1);
    return fo4::text::windows1252_to_utf8(bytes);
}

// Requires: a localized plugin stores the string table index in place of the text, so it reads back as <key>Id.
inline void putText(nlohmann::json& object, const char* key, const fo4::Plugin& plugin,
                    const std::vector<std::byte>& payload) {
    if (plugin.header.tes4_flags.is_localized() && payload.size() == 4) {
        object[std::string(key) + "Id"] = readAt<std::uint32_t>(payload, 0);
        return;
    }
    object[key] = cp1252Text(payload);
}

inline void putText(nlohmann::json& object, const char* key, const fo4::Plugin& plugin,
                    const fo4::Subrecord& subrecord) {
    putText(object, key, plugin, subrecord.data);
}

inline nlohmann::json linkJson(const fo4::Plugin& plugin, std::uint32_t raw) {
    if (raw == 0 || raw == 0xFFFFFFFFu) return nullptr;
    return fo4::resolve_plugin_link(fo4::RawFormId{raw}, fo4::plugin_form_id_context(plugin)).to_string();
}

inline std::uint32_t rawLink(const fo4::Subrecord& subrecord) {
    return readAt<std::uint32_t>(subrecord.data, 0);
}

inline void putFloat(nlohmann::json& object, const char* key, const fo4::Subrecord& subrecord,
                     std::size_t offset) {
    if (offset + sizeof(float) > subrecord.data.size()) return;
    object[key] = readAt<float>(subrecord.data, offset);
}

inline nlohmann::json conditionsJson(const fo4::Plugin& plugin,
                                     std::span<const fo4::CtdaCondition> conditions) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto& condition : conditions) {
        out.push_back(describeCondition(condition, plugin));
    }
    return out;
}

}
