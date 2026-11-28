#pragma once

#include <cstddef>
#include <cstdint>
#include <format>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <fo4/formid.hpp>
#include <fo4/plugin/field_access.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/plugin_core/reader.hpp>
#include <fo4/signature.hpp>

namespace fo4ide::plugin::effects {

struct Entry {
    fo4::FormKey effect;
    float magnitude{0.0F};
    std::uint32_t area{0};
    std::uint32_t duration{0};
};

enum class Write {
    written,
    record_missing,
    unsupported_record,
    unexpected_layout,
    index_out_of_range,
    effect_not_magic_effect
};

inline bool carriesEffects(fo4::Signature signature) {
    return signature == fo4::sig_alch || signature == fo4::sig_ench ||
           signature == fo4::sig_ingr || signature == fo4::sig_spel;
}

inline std::string path(fo4::Signature signature, std::string_view leaf) {
    return signature.to_string() + "\\" + std::string(leaf);
}

inline bool isMagicEffectTarget(const fo4::Plugin& plugin, const fo4::FormKey& effect) {
    try {
        const auto context = fo4::plugin_form_id_context(plugin);
        const auto raw = fo4::encode_plugin_form_id(effect, context);
        if (raw.is_null()) return false;
        if (effect.owner() != context.current_plugin) return true;
        const auto* record = fo4::find_record(plugin, fo4::resolve_plugin_form_id(raw, context));
        return record != nullptr && record->header.signature == fo4::sig_mgef;
    } catch (const fo4::FormIdError&) {
        return false;
    }
}

inline void require(fo4::FieldStatus status, const std::string& what) {
    if (status != fo4::FieldStatus::ok) throw std::runtime_error("effect row " + what + " failed");
}

inline std::size_t occurrences(const fo4::Plugin& plugin, const fo4::Record& record, std::string_view leaf) {
    const auto rowPath = path(record.header.signature, leaf);
    std::string text;
    for (std::size_t count = 0;; ++count) {
        const auto status = fo4::read_record_field(plugin, record, rowPath, count, text);
        if (status == fo4::FieldStatus::occurrence_missing) return count;
        require(status, std::string(leaf));
    }
}

// Requires: effectCount compares the EFID link-row occurrences with the EFIT-row occurrences (EffectMagnitude); the greedy Effects group does not check that.
inline std::size_t effectCount(const fo4::Plugin& plugin, const fo4::Record& record) {
    const auto size = occurrences(plugin, record, "EffectBaseEffect");
    if (size != occurrences(plugin, record, "EffectMagnitude")) {
        throw std::runtime_error("effect block of " + record.form_key.to_string() +
                                 " is not a contiguous EFID/EFIT list");
    }
    return size;
}

inline Write inspect(const fo4::Plugin& plugin, const fo4::FormKey& key, std::size_t& size) {
    size = 0;
    const auto* record = fo4::find_record(plugin, key);
    if (record == nullptr) return Write::record_missing;
    if (!carriesEffects(record->header.signature)) return Write::unsupported_record;
    try {
        size = effectCount(plugin, *record);
    } catch (const std::exception&) {
        return Write::unexpected_layout;
    }
    return Write::written;
}

inline std::vector<Entry> read(const fo4::Plugin& plugin, const fo4::Record& record) {
    const auto signature = record.header.signature;
    if (!carriesEffects(signature)) {
        throw std::runtime_error("record does not carry magic effects: " + signature.to_string());
    }
    const auto size = effectCount(plugin, record);
    const auto field = [&](std::string_view leaf, std::size_t index) {
        std::string text;
        require(fo4::read_record_field(plugin, record, path(signature, leaf), index, text),
                std::string(leaf));
        return text;
    };
    std::vector<Entry> result;
    for (std::size_t index = 0; index < size; ++index) {
        Entry entry;
        const auto effect = field("EffectBaseEffect", index);
        if (!effect.empty()) entry.effect = fo4::FormKey::parse(effect);
        entry.magnitude = static_cast<float>(std::stod(field("EffectMagnitude", index)));
        entry.area = static_cast<std::uint32_t>(std::stoul(field("EffectArea", index)));
        entry.duration = static_cast<std::uint32_t>(std::stoul(field("EffectDuration", index)));
        result.push_back(entry);
    }
    return result;
}

inline void writeEntry(fo4::Plugin& plugin,
                       const fo4::FormKey& key,
                       fo4::Signature signature,
                       std::size_t index,
                       const Entry& entry) {
    const auto put = [&](std::string_view leaf, const std::string& text) {
        require(fo4::write_record_field(plugin, key, path(signature, leaf), index, text),
                std::string(leaf));
    };
    put("EffectBaseEffect", entry.effect.to_string());
    put("EffectMagnitude", std::format("{}", entry.magnitude));
    put("EffectArea", std::to_string(entry.area));
    put("EffectDuration", std::to_string(entry.duration));
}

inline fo4::Signature signatureOf(const fo4::Plugin& plugin, const fo4::FormKey& key) {
    return fo4::find_record(plugin, key)->header.signature;
}

inline Write add(fo4::Plugin& plugin,
                 const fo4::FormKey& key,
                 const Entry& entry,
                 std::size_t* outIndex = nullptr) {
    fo4::assign_record_form_keys(plugin);
    std::size_t size = 0;
    if (const auto status = inspect(plugin, key, size); status != Write::written) return status;
    if (!isMagicEffectTarget(plugin, entry.effect)) return Write::effect_not_magic_effect;
    const auto signature = signatureOf(plugin, key);
    require(fo4::insert_record_list_element(plugin, key, path(signature, "Effects"), size, nullptr),
            "insert");
    writeEntry(plugin, key, signature, size, entry);
    if (outIndex != nullptr) *outIndex = size;
    return Write::written;
}

inline Write set(fo4::Plugin& plugin,
                 const fo4::FormKey& key,
                 std::size_t index,
                 const Entry& entry) {
    fo4::assign_record_form_keys(plugin);
    std::size_t size = 0;
    if (const auto status = inspect(plugin, key, size); status != Write::written) return status;
    if (index >= size) return Write::index_out_of_range;
    if (!isMagicEffectTarget(plugin, entry.effect)) return Write::effect_not_magic_effect;
    writeEntry(plugin, key, signatureOf(plugin, key), index, entry);
    return Write::written;
}

inline Write remove(fo4::Plugin& plugin, const fo4::FormKey& key, std::size_t index) {
    fo4::assign_record_form_keys(plugin);
    std::size_t size = 0;
    if (const auto status = inspect(plugin, key, size); status != Write::written) return status;
    if (index >= size) return Write::index_out_of_range;
    require(fo4::remove_record_list_element(plugin, key, path(signatureOf(plugin, key), "Effects"), index),
            "remove");
    return Write::written;
}

}
