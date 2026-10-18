#include "fo4/forms/perk.hpp"

#include "fo4/plugin/reader.hpp"
#include "../plugin/record_edit.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <span>
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

std::uint8_t epfd_decider(const PerkEffectEntry& effect) {
  if (!effect.parameters.type.has_value()) return 0;
  const auto type = *effect.parameters.type;
  if (type != 2) return type;
  const bool actor_value = effect.function == 5 || effect.function == 12 ||
                           effect.function == 13 || effect.function == 14;
  return actor_value ? 8 : 2;
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

std::optional<float> perk_epfd_float(const PerkEffectEntry& effect) {
  const auto& data = effect.parameters.data;
  if (!data.has_value() || epfd_decider(effect) != 1 || data->size() != 4) return std::nullopt;
  float value = 0.0f;
  std::memcpy(&value, data->data(), sizeof(value));
  return value;
}

std::optional<std::pair<float, float>> perk_epfd_float_pair(const PerkEffectEntry& effect) {
  const auto& data = effect.parameters.data;
  if (!data.has_value() || epfd_decider(effect) != 2 || data->size() != 8) return std::nullopt;
  std::pair<float, float> value{0.0f, 0.0f};
  std::memcpy(&value.first, data->data(), sizeof(float));
  std::memcpy(&value.second, data->data() + 4, sizeof(float));
  return value;
}

std::optional<FormKey> perk_epfd_form(const Plugin& plugin, const PerkEffectEntry& effect) {
  const auto decider = epfd_decider(effect);
  const auto& data = effect.parameters.data;
  if (!data.has_value() || data->size() != 4) return std::nullopt;
  if (decider != 3 && decider != 4 && decider != 5) return std::nullopt;
  return resolve_plugin_form_id(RawFormId{read_u32(*data, 0)}, plugin_form_id_context(plugin));
}

std::optional<std::string> perk_epfd_text(const PerkEffectEntry& effect) {
  const auto& data = effect.parameters.data;
  if (!data.has_value() || epfd_decider(effect) != 6) return std::nullopt;
  if (data->empty() || data->back() != std::byte{0}) return std::nullopt;
  const auto* begin = reinterpret_cast<const char*>(data->data());
  return std::string(begin, begin + data->size() - 1);
}

std::optional<std::pair<FormKey, float>> perk_epfd_actor_value(const Plugin& plugin,
                                                               const PerkEffectEntry& effect) {
  const auto& data = effect.parameters.data;
  if (!data.has_value() || epfd_decider(effect) != 8 || data->size() != 8) return std::nullopt;
  std::pair<FormKey, float> value{};
  value.first = resolve_plugin_form_id(RawFormId{read_u32(*data, 0)}, plugin_form_id_context(plugin));
  std::memcpy(&value.second, data->data() + 4, sizeof(float));
  return value;
}

}  // namespace fo4::forms
