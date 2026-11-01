#include "fo4/forms/effects.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/signature.hpp"
#include "../plugin/record_edit.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace fo4::forms {
namespace {

constexpr auto k_alch = Signature::from_string("ALCH");
constexpr auto k_ench = Signature::from_string("ENCH");
constexpr auto k_ingr = Signature::from_string("INGR");
constexpr auto k_spel = Signature::from_string("SPEL");
constexpr auto k_mgef = Signature::from_string("MGEF");
constexpr auto k_efid = Signature::from_string("EFID");
constexpr auto k_efit = Signature::from_string("EFIT");
constexpr auto k_ctda = Signature::from_string("CTDA");
constexpr auto k_cis1 = Signature::from_string("CIS1");
constexpr auto k_cis2 = Signature::from_string("CIS2");

constexpr std::size_t k_efid_size = 4;
constexpr std::size_t k_efit_size = 12;

bool carries_effects(Signature record) {
  return record == k_alch || record == k_ench || record == k_ingr || record == k_spel;
}

bool is_effect_subrecord(Signature signature) {
  return signature == k_efid || signature == k_efit || signature == k_ctda ||
         signature == k_cis1 || signature == k_cis2;
}

std::size_t next_efid(const std::vector<Subrecord>& subrecords, std::size_t from) {
  for (std::size_t i = from; i < subrecords.size(); ++i) {
    if (subrecords[i].signature == k_efid) return i;
  }
  return subrecords.size();
}

bool block_is_contiguous(const std::vector<Subrecord>& subrecords, std::size_t first) {
  for (std::size_t i = first; i < subrecords.size(); ++i) {
    if (!is_effect_subrecord(subrecords[i].signature)) return false;
  }
  return true;
}

bool is_magic_effect_target(const Plugin& plugin, const FormKey& effect) {
  try {
    const auto context = plugin_form_id_context(plugin);
    const auto raw = encode_plugin_form_id(effect, context);
    if (raw.is_null()) return false;
    if (effect.owner() != context.current_plugin) return true;
    const auto* record = find_record(plugin, resolve_plugin_form_id(raw, context));
    return record != nullptr && record->header.signature == k_mgef;
  } catch (const FormIdError&) {
    return false;
  }
}

std::uint32_t read_u32(const std::vector<std::byte>& data, std::size_t offset) {
  std::uint32_t value = 0;
  std::memcpy(&value, data.data() + offset, sizeof(value));
  return value;
}

float read_f32(const std::vector<std::byte>& data, std::size_t offset) {
  float value = 0.0f;
  std::memcpy(&value, data.data() + offset, sizeof(value));
  return value;
}

void write_u32(std::vector<std::byte>& data, std::size_t offset, std::uint32_t value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

void write_f32(std::vector<std::byte>& data, std::size_t offset, float value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

Subrecord make_subrecord(Signature signature, std::vector<std::byte> data) {
  Subrecord subrecord;
  subrecord.signature = signature;
  subrecord.data = std::move(data);
  return subrecord;
}

std::vector<MagicEffectEntry> decode_effects(const Plugin& plugin, const Record& record) {
  const auto& subrecords = record.subrecords;
  const auto first = next_efid(subrecords, 0);
  if (!block_is_contiguous(subrecords, first)) {
    throw MagicEffectError("record " + record.form_key.to_string() +
                           " has a subrecord inside its effect block");
  }
  const auto context = plugin_form_id_context(plugin);
  std::vector<MagicEffectEntry> result;
  for (std::size_t index = first; index < subrecords.size();) {
    const auto next = next_efid(subrecords, index + 1);
    if (index + 1 >= next || subrecords[index + 1].signature != k_efit) {
      throw MagicEffectError("EFID without EFIT in " + record.form_key.to_string());
    }
    const auto& efid = subrecords[index].data;
    const auto& efit = subrecords[index + 1].data;
    if (efid.size() != k_efid_size) throw MagicEffectError("EFID must be four bytes");
    if (efit.size() != k_efit_size) throw MagicEffectError("EFIT must be twelve bytes");

    MagicEffectEntry entry;
    try {
      entry.effect = resolve_plugin_form_id(RawFormId{read_u32(efid, 0)}, context);
    } catch (const FormIdError& error) {
      throw MagicEffectError(std::string("invalid effect form ID: ") + error.what());
    }
    entry.magnitude = read_f32(efit, 0);
    entry.area = read_u32(efit, 4);
    entry.duration = read_u32(efit, 8);

    if (index + 2 < next && subrecords[index + 2].signature != k_ctda) {
      throw MagicEffectError("effect conditions must begin with CTDA in " +
                             record.form_key.to_string());
    }
    for (std::size_t i = index + 2; i < next; ++i) {
      if (subrecords[i].signature == k_efit) {
        throw MagicEffectError("duplicate EFIT in " + record.form_key.to_string());
      }
    }
    entry.conditions = read_ctda_conditions_in(record, index + 2, next);
    result.push_back(std::move(entry));
    index = next;
  }
  return result;
}

std::vector<Subrecord> encode_effects(const PluginFormIdContext& context,
                                      std::span<const MagicEffectEntry> entries) {
  std::vector<Subrecord> encoded;
  for (const auto& entry : entries) {
    RawFormId raw{};
    try {
      raw = encode_plugin_form_id(entry.effect, context);
    } catch (const FormIdError& error) {
      throw MagicEffectError(std::string("cannot encode effect FormKey: ") + error.what());
    }
    std::vector<std::byte> efid(k_efid_size);
    write_u32(efid, 0, raw.value());
    encoded.push_back(make_subrecord(k_efid, std::move(efid)));

    std::vector<std::byte> efit(k_efit_size);
    write_f32(efit, 0, entry.magnitude);
    write_u32(efit, 4, entry.area);
    write_u32(efit, 8, entry.duration);
    encoded.push_back(make_subrecord(k_efit, std::move(efit)));

    auto run = serialize_ctda_run(entry.conditions);
    encoded.insert(encoded.end(), std::make_move_iterator(run.begin()),
                   std::make_move_iterator(run.end()));
  }
  return encoded;
}

EffectListWrite decode_mutable_effects(Plugin& plugin,
                                       const FormKey& key,
                                       std::vector<MagicEffectEntry>& entries) {
  assign_record_form_keys(plugin);
  const auto* record = fo4::detail::find_mutable_record(plugin, key);
  if (record == nullptr) return EffectListWrite::record_missing;
  if (!carries_effects(record->header.signature)) return EffectListWrite::unsupported_record;
  try {
    entries = decode_effects(plugin, *record);
  } catch (const MagicEffectError&) {
    return EffectListWrite::unexpected_layout;
  }
  return EffectListWrite::written;
}

}  // namespace

std::vector<MagicEffectEntry> read_magic_effects(const Plugin& plugin, const Record& record) {
  if (!carries_effects(record.header.signature)) {
    throw MagicEffectError("record does not carry magic effects: " +
                           record.header.signature.to_string());
  }
  return decode_effects(plugin, record);
}

EffectListWrite set_magic_effects(Plugin& plugin,
                                  const FormKey& key,
                                  std::span<const MagicEffectEntry> entries) {
  assign_record_form_keys(plugin);
  auto* record = fo4::detail::find_mutable_record(plugin, key);
  if (record == nullptr) return EffectListWrite::record_missing;
  if (!carries_effects(record->header.signature)) return EffectListWrite::unsupported_record;
  const auto first = next_efid(record->subrecords, 0);
  if (!block_is_contiguous(record->subrecords, first)) return EffectListWrite::unexpected_layout;

  auto encoded = encode_effects(plugin_form_id_context(plugin), entries);
  record->subrecords.erase(record->subrecords.begin() + static_cast<std::ptrdiff_t>(first),
                           record->subrecords.end());
  record->subrecords.insert(record->subrecords.end(), std::make_move_iterator(encoded.begin()),
                            std::make_move_iterator(encoded.end()));
  fo4::detail::mark_record_modified(plugin, *record);
  return EffectListWrite::written;
}

EffectListWrite add_magic_effect(Plugin& plugin,
                                 const FormKey& key,
                                 MagicEffectEntry entry,
                                 std::size_t* out_index) {
  std::vector<MagicEffectEntry> entries;
  const auto decoded = decode_mutable_effects(plugin, key, entries);
  if (decoded != EffectListWrite::written) return decoded;
  if (!is_magic_effect_target(plugin, entry.effect)) {
    return EffectListWrite::effect_not_magic_effect;
  }
  const auto index = entries.size();
  entries.push_back(std::move(entry));
  const auto result = set_magic_effects(plugin, key, entries);
  if (result == EffectListWrite::written && out_index != nullptr) *out_index = index;
  return result;
}

EffectListWrite set_magic_effect(Plugin& plugin,
                                 const FormKey& key,
                                 std::size_t index,
                                 MagicEffectEntry entry) {
  std::vector<MagicEffectEntry> entries;
  const auto decoded = decode_mutable_effects(plugin, key, entries);
  if (decoded != EffectListWrite::written) return decoded;
  if (index >= entries.size()) return EffectListWrite::index_out_of_range;
  if (!is_magic_effect_target(plugin, entry.effect)) {
    return EffectListWrite::effect_not_magic_effect;
  }
  entries[index] = std::move(entry);
  return set_magic_effects(plugin, key, entries);
}

EffectListWrite remove_magic_effect(Plugin& plugin, const FormKey& key, std::size_t index) {
  std::vector<MagicEffectEntry> entries;
  const auto decoded = decode_mutable_effects(plugin, key, entries);
  if (decoded != EffectListWrite::written) return decoded;
  if (index >= entries.size()) return EffectListWrite::index_out_of_range;
  entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(index));
  return set_magic_effects(plugin, key, entries);
}

}  // namespace fo4::forms
