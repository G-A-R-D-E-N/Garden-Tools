#include "fo4/forms/perk.hpp"

#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/subrecord_authoring.hpp"
#include "fo4/schema/preflight.hpp"
#include "fo4/schema/scalar.hpp"
#include "../plugin/record_edit.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::forms {
namespace {

constexpr auto k_perk = Signature::from_string("PERK");
constexpr auto k_prke = Signature::from_string("PRKE");
constexpr auto k_prkc = Signature::from_string("PRKC");
constexpr auto k_prkf = Signature::from_string("PRKF");
constexpr auto k_data = Signature::from_string("DATA");
constexpr auto k_epft = Signature::from_string("EPFT");
constexpr auto k_epfb = Signature::from_string("EPFB");
constexpr auto k_epf2 = Signature::from_string("EPF2");
constexpr auto k_epf3 = Signature::from_string("EPF3");
constexpr auto k_epfd = Signature::from_string("EPFD");
constexpr auto k_ctda = Signature::from_string("CTDA");
constexpr auto k_cis1 = Signature::from_string("CIS1");
constexpr auto k_cis2 = Signature::from_string("CIS2");
constexpr auto k_full = Signature::from_string("FULL");
constexpr auto k_desc = Signature::from_string("DESC");
constexpr auto k_snam = Signature::from_string("SNAM");
constexpr auto k_nnam = Signature::from_string("NNAM");
constexpr auto k_fnam = Signature::from_string("FNAM");

constexpr std::size_t k_prke_size = 3;
constexpr std::size_t k_quest_stage_size = 6;
constexpr std::size_t k_ability_size = 4;
constexpr std::size_t k_entry_point_size = 3;

bool is_parameter_subrecord(Signature signature) {
  return signature == k_epft || signature == k_epfb || signature == k_epf2 ||
         signature == k_epf3 || signature == k_epfd;
}

bool is_condition_subrecord(Signature signature) {
  return signature == k_ctda || signature == k_cis1 || signature == k_cis2;
}

bool is_effect_subrecord(Signature signature) {
  return signature == k_prke || signature == k_prkc || signature == k_prkf ||
         signature == k_data || is_parameter_subrecord(signature) ||
         is_condition_subrecord(signature);
}

std::size_t next_effect(const std::vector<Subrecord>& subrecords, std::size_t from) {
  for (std::size_t i = from; i < subrecords.size(); ++i) {
    if (subrecords[i].signature == k_prke) return i;
  }
  return subrecords.size();
}

bool trailing_block_is_effects(const std::vector<Subrecord>& subrecords, std::size_t first) {
  for (std::size_t i = first; i < subrecords.size(); ++i) {
    if (!is_effect_subrecord(subrecords[i].signature)) return false;
  }
  return true;
}

std::uint16_t read_u16(std::span<const std::byte> data, std::size_t offset) {
  std::uint16_t value = 0;
  std::memcpy(&value, data.data() + offset, sizeof(value));
  return value;
}

std::uint32_t read_u32(std::span<const std::byte> data, std::size_t offset) {
  std::uint32_t value = 0;
  std::memcpy(&value, data.data() + offset, sizeof(value));
  return value;
}

std::vector<std::byte> u8_bytes(std::uint8_t value) {
  return std::vector<std::byte>{static_cast<std::byte>(value)};
}

std::vector<std::byte> u16_bytes(std::uint16_t value) {
  std::vector<std::byte> data(sizeof(value));
  std::memcpy(data.data(), &value, sizeof(value));
  return data;
}

std::vector<std::byte> u32_bytes(std::uint32_t value) {
  std::vector<std::byte> data(sizeof(value));
  std::memcpy(data.data(), &value, sizeof(value));
  return data;
}

Subrecord make_subrecord(Signature signature, std::vector<std::byte> data) {
  Subrecord subrecord;
  subrecord.signature = signature;
  subrecord.data = std::move(data);
  return subrecord;
}

PerkFunctionParameters decode_parameters(const Plugin& plugin,
                                         const Record& record,
                                         std::size_t begin,
                                         std::size_t end) {
  PerkFunctionParameters parameters;
  for (std::size_t i = begin; i < end; ++i) {
    const auto& subrecord = record.subrecords[i];
    const auto& payload = subrecord.data;
    if (subrecord.signature == k_epft) {
      if (parameters.type.has_value() || payload.size() != 1) {
        throw PerkError("PERK EPFT must be one byte and occur once");
      }
      parameters.type = std::to_integer<std::uint8_t>(payload[0]);
    } else if (subrecord.signature == k_epfb) {
      if (parameters.entry_id.has_value() || payload.size() != 2) {
        throw PerkError("PERK EPFB must be two bytes and occur once");
      }
      parameters.entry_id = read_u16(payload, 0);
    } else if (subrecord.signature == k_epf2) {
      if (parameters.button_label.has_value()) throw PerkError("duplicate PERK EPF2");
      parameters.button_label = schema::read_localized_string(plugin, k_perk, k_epf2, subrecord);
    } else if (subrecord.signature == k_epf3) {
      if (parameters.script_flags.has_value() || payload.size() != 2) {
        throw PerkError("PERK EPF3 must be two bytes and occur once");
      }
      parameters.script_flags = read_u16(payload, 0);
    } else if (subrecord.signature == k_epfd) {
      if (parameters.data.has_value()) throw PerkError("duplicate PERK EPFD");
      parameters.data = payload;
    } else {
      throw PerkError("PERK effect has an unexpected subrecord " +
                      subrecord.signature.to_string());
    }
  }
  return parameters;
}

PerkEffectEntry decode_effect(const Plugin& plugin,
                              const Record& record,
                              std::size_t begin,
                              std::size_t end,
                              const PluginFormIdContext& context) {
  const auto& subrecords = record.subrecords;
  const auto& prke = subrecords[begin].data;
  if (prke.size() != k_prke_size) throw PerkError("PERK PRKE must be three bytes");

  PerkEffectEntry effect;
  const auto raw_type = std::to_integer<std::uint8_t>(prke[0]);
  if (raw_type > 2) throw PerkError("unknown PERK effect type");
  effect.type = static_cast<PerkEffectType>(raw_type);
  effect.rank = std::to_integer<std::uint8_t>(prke[1]);
  effect.priority = std::to_integer<std::uint8_t>(prke[2]);

  if (begin + 1 >= end || subrecords[begin + 1].signature != k_data) {
    throw PerkError("PERK effect without its data subrecord");
  }
  const auto& data = subrecords[begin + 1].data;
  if (effect.type == PerkEffectType::quest_stage) {
    if (data.size() != k_quest_stage_size) {
      throw PerkError("PERK quest effect data must be six bytes");
    }
    effect.quest = resolve_plugin_form_id(RawFormId{read_u32(data, 0)}, context);
    effect.quest_stage = read_u16(data, 4);
  } else if (effect.type == PerkEffectType::ability) {
    if (data.size() != k_ability_size) {
      throw PerkError("PERK ability effect data must be four bytes");
    }
    effect.ability = resolve_plugin_form_id(RawFormId{read_u32(data, 0)}, context);
  } else {
    if (data.size() != k_entry_point_size) {
      throw PerkError("PERK entry point data must be three bytes");
    }
    effect.entry_point = std::to_integer<std::uint8_t>(data[0]);
    effect.function = std::to_integer<std::uint8_t>(data[1]);
    effect.condition_tab_count = std::to_integer<std::uint8_t>(data[2]);
  }

  bool saw_end_marker = false;
  std::size_t parameters_begin = end;
  std::size_t parameters_end = end;
  for (std::size_t i = begin + 2; i < end;) {
    const auto signature = subrecords[i].signature;
    if (signature == k_prke || signature == k_data) {
      throw PerkError("PERK effect repeats a header subrecord");
    }
    if (signature == k_prkc) {
      if (parameters_begin != end) {
        throw PerkError("PERK condition tabs must precede the parameter group");
      }
      if (subrecords[i].data.size() != 1) throw PerkError("PERK PRKC must be one byte");
      PerkConditionTab tab;
      tab.run_on = static_cast<std::int8_t>(std::to_integer<std::uint8_t>(subrecords[i].data[0]));
      std::size_t condition_end = i + 1;
      while (condition_end < end && is_condition_subrecord(subrecords[condition_end].signature)) {
        ++condition_end;
      }
      tab.conditions = read_ctda_conditions_in(record, i + 1, condition_end);
      effect.tabs.push_back(std::move(tab));
      i = condition_end;
      continue;
    }
    if (signature == k_prkf) {
      if (!subrecords[i].data.empty()) throw PerkError("PERK PRKF must be empty");
      if (i + 1 != end) throw PerkError("PERK PRKF must end the effect");
      saw_end_marker = true;
      parameters_end = i;
      ++i;
      continue;
    }
    if (!is_parameter_subrecord(signature)) {
      throw PerkError("PERK effect has an unexpected subrecord " + signature.to_string());
    }
    if (parameters_begin == end) parameters_begin = i;
    parameters_end = i + 1;
    ++i;
  }
  if (!saw_end_marker) throw PerkError("PERK effect without its end marker");
  effect.parameters = decode_parameters(plugin, record, parameters_begin, parameters_end);
  return effect;
}

std::vector<PerkEffectEntry> decode_effects(const Plugin& plugin, const Record& record) {
  const auto first = next_effect(record.subrecords, 0);
  if (first == record.subrecords.size()) return {};
  if (!trailing_block_is_effects(record.subrecords, first)) {
    throw PerkError("record " + record.form_key.to_string() +
                    " has a subrecord inside its effect block");
  }
  const auto context = plugin_form_id_context(plugin);
  std::vector<PerkEffectEntry> effects;
  for (std::size_t i = first; i < record.subrecords.size();) {
    const auto next = next_effect(record.subrecords, i + 1);
    effects.push_back(decode_effect(plugin, record, i, next, context));
    i = next;
  }
  return effects;
}

std::vector<Subrecord> encode_effects(const PluginFormIdContext& context,
                                      std::span<const PerkEffectEntry> effects) {
  std::vector<Subrecord> encoded;
  for (const auto& effect : effects) {
    const auto raw_type = static_cast<std::uint8_t>(effect.type);
    if (raw_type > 2) throw PerkError("unknown PERK effect type");
    encoded.push_back(make_subrecord(
        k_prke, std::vector<std::byte>{static_cast<std::byte>(raw_type),
                                       static_cast<std::byte>(effect.rank),
                                       static_cast<std::byte>(effect.priority)}));

    if (effect.type == PerkEffectType::quest_stage) {
      const auto raw = u32_bytes(encode_plugin_form_id(effect.quest, context).value());
      std::vector<std::byte> data(k_quest_stage_size);
      std::memcpy(data.data(), raw.data(), raw.size());
      const auto stage = u16_bytes(effect.quest_stage);
      std::memcpy(data.data() + 4, stage.data(), stage.size());
      encoded.push_back(make_subrecord(k_data, std::move(data)));
    } else if (effect.type == PerkEffectType::ability) {
      encoded.push_back(
          make_subrecord(k_data, u32_bytes(encode_plugin_form_id(effect.ability, context).value())));
    } else {
      encoded.push_back(make_subrecord(
          k_data, std::vector<std::byte>{static_cast<std::byte>(effect.entry_point),
                                         static_cast<std::byte>(effect.function),
                                         static_cast<std::byte>(effect.condition_tab_count)}));
    }

    for (const auto& tab : effect.tabs) {
      encoded.push_back(make_subrecord(k_prkc, u8_bytes(static_cast<std::uint8_t>(tab.run_on))));
      auto run = serialize_ctda_run(tab.conditions);
      encoded.insert(encoded.end(), std::make_move_iterator(run.begin()),
                     std::make_move_iterator(run.end()));
    }

    const auto& parameters = effect.parameters;
    if (parameters.type.has_value()) {
      encoded.push_back(make_subrecord(k_epft, u8_bytes(*parameters.type)));
    }
    if (parameters.entry_id.has_value()) {
      encoded.push_back(make_subrecord(k_epfb, u16_bytes(*parameters.entry_id)));
    }
    if (parameters.button_label.has_value()) {
      encoded.push_back(
          make_subrecord(k_epf2, schema::write_localized_string(*parameters.button_label)));
    }
    if (parameters.script_flags.has_value()) {
      encoded.push_back(make_subrecord(k_epf3, u16_bytes(*parameters.script_flags)));
    }
    if (parameters.data.has_value()) {
      encoded.push_back(make_subrecord(k_epfd, *parameters.data));
    }
    encoded.push_back(make_subrecord(k_prkf, {}));
  }
  return encoded;
}

std::vector<std::byte> zstring_bytes(std::string_view text) {
  const auto bytes = std::as_bytes(std::span{text});
  std::vector<std::byte> data{bytes.begin(), bytes.end()};
  data.push_back(std::byte{0});
  return data;
}

void set_header_scalar(Plugin& plugin, const FormKey& key, std::string_view path, std::uint8_t value) {
  const auto field = schema::scalar_field_descriptor(k_perk, path).value();
  const auto status = set_record_scalar(plugin, key, field, std::to_string(value));
  if (status != ScalarMutationStatus::updated && status != ScalarMutationStatus::unchanged) {
    throw PerkError("PERK header field " + std::string(path) + " could not be set");
  }
}

}  // namespace

std::vector<PerkEffectEntry> read_perk_effects(const Plugin& plugin, const Record& record) {
  if (record.header.signature != k_perk) {
    throw PerkError("record does not carry perk effects: " + record.header.signature.to_string());
  }
  return decode_effects(plugin, record);
}

PerkEffectWrite set_perk_effects(Plugin& plugin,
                                 const FormKey& key,
                                 std::span<const PerkEffectEntry> effects) {
  assign_record_form_keys(plugin);
  auto* record = fo4::detail::find_mutable_record(plugin, key);
  if (record == nullptr) return PerkEffectWrite::record_missing;
  if (record->header.signature != k_perk) return PerkEffectWrite::unsupported_record;
  const auto first = next_effect(record->subrecords, 0);
  if (!trailing_block_is_effects(record->subrecords, first)) {
    return PerkEffectWrite::unexpected_layout;
  }

  auto encoded = encode_effects(plugin_form_id_context(plugin), effects);
  record->subrecords.erase(record->subrecords.begin() + static_cast<std::ptrdiff_t>(first),
                           record->subrecords.end());
  record->subrecords.insert(record->subrecords.end(), std::make_move_iterator(encoded.begin()),
                            std::make_move_iterator(encoded.end()));
  fo4::detail::mark_record_modified(plugin, *record);
  return PerkEffectWrite::written;
}

PerkLinks read_perk_links(const Plugin& plugin, const Record& record) {
  if (record.header.signature != k_perk) {
    throw PerkError("record does not carry perk links: " + record.header.signature.to_string());
  }
  PerkLinks links;
  const auto context = plugin_form_id_context(plugin);
  bool saw_next = false;
  bool saw_sound = false;
  for (const auto& subrecord : record.subrecords) {
    const auto& payload = subrecord.data;
    if (subrecord.signature == k_nnam || subrecord.signature == k_snam) {
      const bool next = subrecord.signature == k_nnam;
      if (payload.size() != 4) throw PerkError("PERK " + subrecord.signature.to_string() + " must be four bytes");
      if (next ? saw_next : saw_sound) throw PerkError("duplicate PERK " + subrecord.signature.to_string());
      (next ? saw_next : saw_sound) = true;
      (next ? links.next_perk : links.sound) = resolve_plugin_form_id(RawFormId{read_u32(payload, 0)}, context);
    } else if (subrecord.signature == k_fnam) {
      if (links.swf.has_value()) throw PerkError("duplicate PERK FNAM");
      if (payload.empty() || payload.back() != std::byte{0}) throw PerkError("PERK FNAM must be NUL-terminated");
      const auto* begin = reinterpret_cast<const char*>(payload.data());
      links.swf = std::string(begin, begin + payload.size() - 1);
    }
  }
  return links;
}

namespace {

constexpr std::array<Signature, 3> k_followers_sound{k_nnam, k_fnam, k_prke};
constexpr std::array<Signature, 2> k_followers_next{k_fnam, k_prke};
constexpr std::array<Signature, 1> k_followers_swf{k_prke};

PerkLinkWrite write_perk_link(Plugin& plugin,
                              const FormKey& key,
                              Signature signature,
                              std::optional<std::vector<std::byte>> payload,
                              std::size_t fixed_size,
                              std::span<const Signature> followers) {
  assign_record_form_keys(plugin);
  auto* record = fo4::detail::find_mutable_record(plugin, key);
  if (record == nullptr) return PerkLinkWrite::record_missing;
  if (record->header.signature != k_perk) return PerkLinkWrite::unsupported_record;

  const auto existing = std::find_if(record->subrecords.begin(), record->subrecords.end(),
                                     [signature](const Subrecord& subrecord) { return subrecord.signature == signature; });
  if (!payload.has_value()) {
    if (existing != record->subrecords.end()) record->subrecords.erase(existing);
  } else if (existing != record->subrecords.end()) {
    if (fixed_size != 0 && existing->data.size() != fixed_size) return PerkLinkWrite::unexpected_layout;
    existing->data = std::move(*payload);
  } else {
    const auto before = std::find_if(record->subrecords.begin(), record->subrecords.end(),
                                     [followers](const Subrecord& subrecord) {
                                       return std::find(followers.begin(), followers.end(), subrecord.signature) !=
                                              followers.end();
                                     });
    record->subrecords.insert(before, make_subrecord(signature, std::move(*payload)));
  }
  fo4::detail::mark_record_modified(plugin, *record);
  return PerkLinkWrite::written;
}

}  // namespace

PerkLinkWrite set_perk_next(Plugin& plugin, const FormKey& key, const FormKey& next_perk) {
  const auto raw = u32_bytes(encode_plugin_form_id(next_perk, plugin_form_id_context(plugin)).value());
  return write_perk_link(plugin, key, k_nnam, raw, 4, k_followers_next);
}

PerkLinkWrite set_perk_sound(Plugin& plugin, const FormKey& key, const FormKey& sound) {
  std::optional<std::vector<std::byte>> payload;
  if (sound != FormKey{}) {
    payload = u32_bytes(encode_plugin_form_id(sound, plugin_form_id_context(plugin)).value());
  }
  return write_perk_link(plugin, key, k_snam, std::move(payload), 4, k_followers_sound);
}

PerkLinkWrite set_perk_swf(Plugin& plugin, const FormKey& key, std::string_view stored_swf) {
  std::optional<std::vector<std::byte>> payload;
  if (!stored_swf.empty()) payload = zstring_bytes(stored_swf);
  return write_perk_link(plugin, key, k_fnam, std::move(payload), 0, k_followers_swf);
}

FormKey create_perk(Plugin& plugin, const PerkCreation& creation) {
  const bool localized = plugin.header.tes4_flags.is_localized();
  if (creation.full_name.has_value() && creation.full_name->localized() != localized) {
    throw PerkError(localized ? "PERK FULL in a localized plugin must be a string ID"
                              : "PERK FULL in a non-localized plugin must be inline text");
  }
  if (creation.description.has_value() && creation.description->localized() != localized) {
    throw PerkError(localized ? "PERK DESC in a localized plugin must be a string ID"
                              : "PERK DESC in a non-localized plugin must be inline text");
  }

  auto next = plugin;
  const auto created = create_record(next, k_perk, stored_editor_id(creation.editor_id));
  if (created.status != RecordCreateStatus::created) throw PerkError("PERK creation failed");
  const auto key = created.key;

  auto* record = fo4::detail::find_mutable_record(next, key);
  if (record == nullptr) throw PerkError("PERK record missing after creation");
  const auto data = std::find_if(record->subrecords.begin(), record->subrecords.end(),
                                 [](const Subrecord& subrecord) { return subrecord.signature == k_data; });
  std::vector<Subrecord> text;
  if (creation.full_name.has_value()) {
    text.push_back(make_subrecord(k_full, schema::write_localized_string(*creation.full_name)));
  }
  if (creation.description.has_value()) {
    text.push_back(make_subrecord(k_desc, schema::write_localized_string(*creation.description)));
  }
  record->subrecords.insert(data, std::make_move_iterator(text.begin()), std::make_move_iterator(text.end()));

  if (!creation.conditions.empty() &&
      set_record_conditions(next, key, creation.conditions) != ConditionMutationStatus::updated) {
    throw PerkError("PERK conditions could not be written");
  }

  const auto context = plugin_form_id_context(next);
  std::vector<Subrecord> links;
  if (creation.sound != FormKey{}) {
    links.push_back(make_subrecord(k_snam, u32_bytes(encode_plugin_form_id(creation.sound, context).value())));
  }
  if (creation.next_perk != FormKey{}) {
    links.push_back(make_subrecord(k_nnam, u32_bytes(encode_plugin_form_id(creation.next_perk, context).value())));
  }
  if (!creation.swf.empty()) {
    links.push_back(make_subrecord(k_fnam, zstring_bytes(stored_inline_text(creation.swf, "PERK SWF"))));
  }
  const auto after_data = std::find_if(record->subrecords.begin(), record->subrecords.end(),
                                       [](const Subrecord& subrecord) { return subrecord.signature == k_data; }) + 1;
  record->subrecords.insert(after_data, std::make_move_iterator(links.begin()), std::make_move_iterator(links.end()));

  set_header_scalar(next, key, "PERK\\Trait", creation.trait);
  set_header_scalar(next, key, "PERK\\Level", creation.level);
  set_header_scalar(next, key, "PERK\\NumRanks", creation.ranks);
  set_header_scalar(next, key, "PERK\\Playable", creation.playable);
  if (set_perk_effects(next, key, creation.effects) != PerkEffectWrite::written) {
    throw PerkError("PERK effects could not be written");
  }

  for (const auto& issue : schema::preflight_authoring_record(next, key)) {
    if (issue.severity == schema::PreflightSeverity::error) {
      throw PerkError("PERK authoring preflight failed: " + issue.message);
    }
  }
  plugin = std::move(next);
  return key;
}

PerkFunctionType perk_function_type(const PerkEffectEntry& effect) {
  if (!effect.parameters.type.has_value()) return PerkFunctionType::none;
  const auto type = *effect.parameters.type;
  if (type != 2) return static_cast<PerkFunctionType>(type);
  const bool actor_value = effect.function == 5 || effect.function == 12 ||
                           effect.function == 13 || effect.function == 14;
  return actor_value ? PerkFunctionType::actor_value : PerkFunctionType::float_pair;
}

std::optional<float> perk_epfd_float(const PerkEffectEntry& effect) {
  const auto& data = effect.parameters.data;
  if (!data.has_value() || perk_function_type(effect) != PerkFunctionType::single_float || data->size() != 4) {
    return std::nullopt;
  }
  float value = 0.0f;
  std::memcpy(&value, data->data(), sizeof(value));
  return value;
}

std::optional<std::pair<float, float>> perk_epfd_float_pair(const PerkEffectEntry& effect) {
  const auto& data = effect.parameters.data;
  if (!data.has_value() || perk_function_type(effect) != PerkFunctionType::float_pair || data->size() != 8) {
    return std::nullopt;
  }
  std::pair<float, float> value{0.0f, 0.0f};
  std::memcpy(&value.first, data->data(), sizeof(float));
  std::memcpy(&value.second, data->data() + 4, sizeof(float));
  return value;
}

std::optional<FormKey> perk_epfd_form(const Plugin& plugin, const PerkEffectEntry& effect) {
  const auto shape = perk_function_type(effect);
  const auto& data = effect.parameters.data;
  if (!data.has_value() || data->size() != 4) return std::nullopt;
  if (shape != PerkFunctionType::leveled_item && shape != PerkFunctionType::labeled_spell &&
      shape != PerkFunctionType::spell) {
    return std::nullopt;
  }
  return resolve_plugin_form_id(RawFormId{read_u32(*data, 0)}, plugin_form_id_context(plugin));
}

std::optional<std::string> perk_epfd_text(const PerkEffectEntry& effect) {
  const auto& data = effect.parameters.data;
  if (!data.has_value() || perk_function_type(effect) != PerkFunctionType::text) return std::nullopt;
  if (data->empty() || data->back() != std::byte{0}) return std::nullopt;
  const auto* begin = reinterpret_cast<const char*>(data->data());
  return std::string(begin, begin + data->size() - 1);
}

std::optional<std::pair<FormKey, float>> perk_epfd_actor_value(const Plugin& plugin,
                                                               const PerkEffectEntry& effect) {
  const auto& data = effect.parameters.data;
  if (!data.has_value() || perk_function_type(effect) != PerkFunctionType::actor_value || data->size() != 8) {
    return std::nullopt;
  }
  std::pair<FormKey, float> value{};
  value.first = resolve_plugin_form_id(RawFormId{read_u32(*data, 0)}, plugin_form_id_context(plugin));
  std::memcpy(&value.second, data->data() + 4, sizeof(float));
  return value;
}

}  // namespace fo4::forms
