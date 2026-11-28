#include "fo4/forms/ui_records.hpp"

#include "fo4/forms/perk.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/schema/condition_scopes.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace fo4::forms {
namespace {

constexpr std::size_t k_item_entry_size = 8;
constexpr std::size_t k_marker_params_old_size = 20;
constexpr std::size_t k_marker_params_size = 24;
constexpr std::uint16_t k_marker_params_entry_types_version = 125;

constexpr Signature sig_cont_record = Signature::from_string("CONT");
constexpr Signature sig_door_record = Signature::from_string("DOOR");
constexpr Signature sig_attx = Signature::from_string("ATTX");
constexpr Signature sig_fnpr = Signature::from_string("FNPR");
constexpr Signature sig_wbdt = Signature::from_string("WBDT");
constexpr Signature sig_xmrk = Signature::from_string("XMRK");
constexpr Signature sig_bptd = Signature::from_string("BPTD");
constexpr Signature sig_bptn = Signature::from_string("BPTN");
constexpr Signature sig_bpnn = Signature::from_string("BPNN");
constexpr Signature sig_bpnt = Signature::from_string("BPNT");
constexpr Signature sig_bpnd = Signature::from_string("BPND");

constexpr std::size_t k_misc_data_size = 8;
constexpr std::size_t k_ctda_compare_offset = 4;
constexpr std::size_t k_ctda_param1_offset = 12;
constexpr std::size_t k_ctda_param2_offset = 16;
constexpr std::size_t k_ctda_reference_offset = 24;

constexpr std::size_t k_perk_data_min_size = 3;
constexpr std::size_t k_note_data_size = 8;
constexpr std::size_t k_holotape_entry_size = 8;
constexpr std::size_t k_body_part_node_size = 101;

void require_signature(const Record& record, Signature expected) {
  if (record.header.signature != expected) {
    throw UiRecordError(record.form_key.to_string() + " is not a " + expected.to_string());
  }
  try {
    require_decodable(record);
  } catch (const std::invalid_argument& error) {
    throw UiRecordError(error.what());
  }
}

template <typename T>
T read_at(const Record& record, const Subrecord& subrecord, std::size_t offset) {
  static_assert(std::is_trivially_copyable_v<T>);
  if (offset + sizeof(T) > subrecord.data.size()) {
    throw UiRecordError(record.form_key.to_string() + " " + subrecord.signature.to_string() +
                        " is shorter than " + std::to_string(offset + sizeof(T)) + " bytes");
  }
  T value{};
  std::memcpy(&value, subrecord.data.data() + offset, sizeof(T));
  return value;
}

std::optional<FormKey> link_at(const Record& record,
                               const PluginFormIdContext& context,
                               const Subrecord& subrecord,
                               std::size_t offset) {
  const RawFormId raw{read_at<std::uint32_t>(record, subrecord, offset)};
  try {
    const auto key = resolve_plugin_link(raw, context);
    if (key.owner().empty()) return std::nullopt;
    return key;
  } catch (const FormIdError& error) {
    throw UiRecordError(record.form_key.to_string() + " " + subrecord.signature.to_string() +
                        ": " + error.what());
  }
}

std::optional<FormKey> single_link(const Record& record,
                                   const PluginFormIdContext& context,
                                   const Subrecord& subrecord) {
  if (subrecord.data.size() != 4) {
    throw UiRecordError(record.form_key.to_string() + " " + subrecord.signature.to_string() +
                        " must be 4 bytes");
  }
  return link_at(record, context, subrecord, 0);
}

std::string zstring(const Record& record, const Subrecord& subrecord) {
  const auto& bytes = subrecord.data;
  if (bytes.empty() || bytes.back() != std::byte{0} ||
      std::find(bytes.begin(), bytes.end() - 1, std::byte{0}) != bytes.end() - 1) {
    throw UiRecordError(record.form_key.to_string() + " " + subrecord.signature.to_string() +
                        " must be one NUL-terminated string");
  }
  std::string text;
  for (std::size_t index = 0; index + 1 < subrecord.data.size(); ++index) {
    text.push_back(static_cast<char>(std::to_integer<std::uint8_t>(subrecord.data[index])));
  }
  return text;
}

schema::LocalizedString localized(const Plugin& plugin, const Record& record, const Subrecord& subrecord) {
  try {
    return schema::read_localized_string(plugin, record.header.signature, subrecord.signature, subrecord);
  } catch (const std::exception& error) {
    throw UiRecordError(record.form_key.to_string() + " " + subrecord.signature.to_string() +
                        ": " + error.what());
  }
}

class Seen {
public:
  explicit Seen(const Record& record) : record_(record) {}

  void mark(const Subrecord& subrecord) {
    if (!seen_.insert(subrecord.signature.packed()).second) {
      throw UiRecordError(record_.form_key.to_string() + " has a duplicate " +
                          subrecord.signature.to_string());
    }
  }

  void reset() { seen_.clear(); }

private:
  const Record& record_;
  std::set<std::uint32_t> seen_;
};

void read_item(const Record& record, const PluginFormIdContext& context, const Subrecord& subrecord,
               std::vector<ItemCount>& items) {
  if (subrecord.data.size() != k_item_entry_size) {
    throw UiRecordError(record.form_key.to_string() + " CNTO must be 8 bytes");
  }
  if (const auto item = link_at(record, context, subrecord, 0)) {
    items.push_back(ItemCount{*item, read_at<std::int32_t>(record, subrecord, 4)});
  }
}

ConditionLinks links_of_ctda(const Record& record, const PluginFormIdContext& context, const Subrecord& subrecord) {
  ConditionLinks links;
  for (const auto offset : ctda_formid_offsets(subrecord)) {
    auto key = link_at(record, context, subrecord, offset);
    if (offset == k_ctda_compare_offset) links.global = key;
    else if (offset == k_ctda_param1_offset) links.param1 = key;
    else if (offset == k_ctda_param2_offset) links.param2 = key;
    else if (offset == k_ctda_reference_offset) links.reference = key;
  }
  return links;
}

std::vector<ConditionLinks> condition_links_in(const Record& record, const PluginFormIdContext& context,
                                               std::size_t begin, std::size_t end) {
  std::vector<ConditionLinks> out;
  for (std::size_t i = begin; i < end; ++i) {
    const auto& subrecord = record.subrecords[i];
    if (subrecord.signature != sig_ctda) continue;
    out.push_back(links_of_ctda(record, context, subrecord));
  }
  return out;
}

void read_conditions(const Record& record, const PluginFormIdContext& context, ConstructibleObject& out) {
  const auto read = schema::read_root_conditions(record);
  if (!read.ok) throw UiRecordError(record.form_key.to_string() + " CTDA list cannot be read losslessly");
  out.conditions = read.conditions;
  out.condition_links = condition_links_in(record, context, 0, record.subrecords.size());
  if (out.condition_links.size() != out.conditions.size()) {
    throw UiRecordError(record.form_key.to_string() + " has CTDA rows outside its condition list");
  }
}

schema::ConditionScopeDescriptor terminal_scope(std::string_view path) {
  const auto found = schema::scoped_condition_path(sig_term, path);
  if (!found) throw UiRecordError(std::string("TERM condition scope is missing: ") + std::string(path));
  return found->descriptor;
}

void read_owner_conditions(const Record& record, const PluginFormIdContext& context,
                           const schema::ConditionScopeDescriptor& scope, std::size_t scope_index,
                           std::vector<CtdaCondition>& conditions, std::vector<ConditionLinks>& condition_links) {
  const auto range = schema::scoped_condition_range(record, scope, scope_index);
  if (!range) throw UiRecordError(record.form_key.to_string() + " condition scope cannot be resolved");
  try {
    conditions = read_ctda_conditions_in(record, range->begin, range->end);
  } catch (const CtdaMultipleListsError& error) {
    throw UiRecordError(record.form_key.to_string() + " " + error.what());
  } catch (const CtdaNotLosslessError&) {
    throw UiRecordError(record.form_key.to_string() + " CTDA list cannot be read losslessly");
  }
  condition_links = condition_links_in(record, context, range->begin, range->end);
}

std::vector<ComponentEntry> linked_components(const Plugin& plugin, const Record& record) {
  std::vector<ComponentEntry> out;
  try {
    for (auto& entry : decode_components(plugin, record.form_key)) {
      if (!entry.component.owner().empty()) out.push_back(std::move(entry));
    }
  } catch (const ComponentError& error) {
    throw UiRecordError(error.what());
  }
  return out;
}

}  // namespace

ConstructibleObject decode_constructible_object(const Plugin& plugin, const Record& record) {
  require_signature(record, sig_cobj);
  const auto context = plugin_form_id_context(plugin);
  ConstructibleObject out;
  out.key = record.form_key;
  Seen seen(record);
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature == sig_bnam) {
      seen.mark(subrecord);
      out.workbench_keyword = single_link(record, context, subrecord);
    } else if (subrecord.signature == sig_cnam) {
      seen.mark(subrecord);
      out.created_object = single_link(record, context, subrecord);
    } else if (subrecord.signature == sig_fnam) {
      if (subrecord.data.size() % 4 != 0) {
        throw UiRecordError(record.form_key.to_string() + " FNAM length must be a multiple of four bytes");
      }
      for (std::size_t offset = 0; offset < subrecord.data.size(); offset += 4) {
        if (const auto key = link_at(record, context, subrecord, offset)) out.category_keywords.push_back(*key);
      }
    } else if (subrecord.signature == sig_intv) {
      seen.mark(subrecord);
      out.created_count = read_at<std::uint16_t>(record, subrecord, 0);
      if (subrecord.data.size() >= 4) out.priority = read_at<std::uint16_t>(record, subrecord, 2);
    } else if (subrecord.signature == sig_desc) {
      seen.mark(subrecord);
      out.description = localized(plugin, record, subrecord);
    }
  }
  out.components = linked_components(plugin, record);
  read_conditions(record, context, out);
  return out;
}

PerkEntryPointsInfo decode_perk_entry_points(const Plugin& plugin, const Record& record) {
  require_signature(record, sig_perk);
  PerkEntryPointsInfo out;
  out.key = record.form_key;
  const auto context = plugin_form_id_context(plugin);
  std::vector<PerkEffectEntry> entries;
  try {
    entries = read_perk_effects(plugin, record);
  } catch (const PerkError& error) {
    throw UiRecordError(error.what());
  } catch (const FormIdError& error) {
    throw UiRecordError(record.form_key.to_string() + " PERK effect: " + error.what());
  } catch (const CtdaMultipleListsError& error) {
    throw UiRecordError(record.form_key.to_string() + " PERK effect: " + error.what());
  } catch (const CtdaNotLosslessError&) {
    throw UiRecordError(record.form_key.to_string() + " PERK effect CTDA list cannot be read losslessly");
  }
  for (const auto& entry : entries) {
    if (entry.type != PerkEffectType::entry_point) continue;
    PerkEntryPointEffect effect;
    effect.rank = entry.rank;
    effect.priority = entry.priority;
    effect.entry_point = entry.entry_point;
    effect.function = entry.function;
    effect.parameter_type = entry.parameters.type;
    effect.value = perk_epfd_float(entry);
    effect.function_type = static_cast<std::uint8_t>(perk_function_type(entry));
    effect.value_pair = perk_epfd_float_pair(entry);
    effect.text = perk_epfd_text(entry);
    // Constraint: an unresolvable EPFD form link must leave this effect's form and actor value empty, not fail the whole PERK decode.
    try {
      effect.form = perk_epfd_form(plugin, entry);
      effect.actor_value = perk_epfd_actor_value(plugin, entry);
    } catch (const FormIdError&) {
      effect.form.reset();
      effect.actor_value.reset();
    }
    for (const auto& tab : entry.tabs) {
      PerkEntryPointTab row;
      row.run_on = tab.run_on;
      row.conditions = tab.conditions;
      for (const auto& condition : tab.conditions) {
        Subrecord ctda;
        ctda.signature = sig_ctda;
        ctda.data = serialize_ctda_condition(condition);
        row.condition_links.push_back(links_of_ctda(record, context, ctda));
      }
      effect.tabs.push_back(std::move(row));
    }
    out.effects.push_back(std::move(effect));
  }
  return out;
}

PerkInfo decode_perk_info(const Plugin& plugin, const Record& record) {
  require_signature(record, sig_perk);
  PerkInfo out;
  out.key = record.form_key;
  try {
    const auto links = read_perk_links(plugin, record);
    if (!links.next_perk.owner().empty()) out.next_perk = links.next_perk;
    if (!links.sound.owner().empty()) out.sound = links.sound;
    out.swf = links.swf;
  } catch (const PerkError& error) {
    throw UiRecordError(error.what());
  } catch (const FormIdError& error) {
    throw UiRecordError(record.form_key.to_string() + " PERK link: " + error.what());
  }
  Seen seen(record);
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature == sig_prke) break;  // Invariant: each effect carries its own DATA after PRKE.
    if (subrecord.signature == sig_full) {
      seen.mark(subrecord);
      out.full_name = localized(plugin, record, subrecord);
    } else if (subrecord.signature == sig_desc) {
      seen.mark(subrecord);
      out.description = localized(plugin, record, subrecord);
    } else if (subrecord.signature == sig_icon) {
      seen.mark(subrecord);
      out.image = zstring(record, subrecord);
    } else if (subrecord.signature == sig_data) {
      seen.mark(subrecord);
      if (subrecord.data.size() < k_perk_data_min_size) {
        throw UiRecordError(record.form_key.to_string() + " DATA must be at least 3 bytes");
      }
      const auto size = subrecord.data.size();
      out.data = PerkData{read_at<std::uint8_t>(record, subrecord, 0), read_at<std::uint8_t>(record, subrecord, 1),
                          read_at<std::uint8_t>(record, subrecord, 2),
                          size >= 4 ? read_at<std::uint8_t>(record, subrecord, 3) : std::uint8_t{0},
                          size >= 5 ? read_at<std::uint8_t>(record, subrecord, 4) : std::uint8_t{0}};
    }
  }
  return out;
}

NoteInfo decode_note_info(const Plugin& plugin, const Record& record) {
  require_signature(record, sig_note);
  const auto context = plugin_form_id_context(plugin);
  NoteInfo out;
  out.key = record.form_key;
  Seen seen(record);
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature == sig_full) {
      seen.mark(subrecord);
      out.full_name = localized(plugin, record, subrecord);
    } else if (subrecord.signature == sig_dnam) {
      seen.mark(subrecord);
      out.type = read_at<std::uint8_t>(record, subrecord, 0);
    } else if (subrecord.signature == sig_pnam) {
      seen.mark(subrecord);
      out.program_file = zstring(record, subrecord);
    } else if (subrecord.signature == sig_snam) {
      seen.mark(subrecord);
      out.target = single_link(record, context, subrecord);
    } else if (subrecord.signature == sig_data) {
      seen.mark(subrecord);
      if (subrecord.data.size() != k_note_data_size) {
        throw UiRecordError(record.form_key.to_string() + " DATA must be 8 bytes");
      }
      out.value = read_at<std::uint32_t>(record, subrecord, 0);
      out.weight = read_at<float>(record, subrecord, 4);
    }
  }
  return out;
}

TerminalInfo decode_terminal_info(const Plugin& plugin, const Record& record) {
  require_signature(record, sig_term);
  const auto context = plugin_form_id_context(plugin);
  TerminalInfo out;
  out.key = record.form_key;
  const auto body_scope = terminal_scope("Body Text[0].Conditions");
  const auto item_scope = terminal_scope("Menu Items[0].Conditions");
  enum class Owner { none, body, item } owner = Owner::none;
  Seen seen(record);
  Seen item_seen(record);
  const auto in_item = [&](const Subrecord& subrecord) -> TerminalMenuItem& {
    if (owner != Owner::item) {
      throw UiRecordError(record.form_key.to_string() + " " + subrecord.signature.to_string() +
                          " appears outside a menu item");
    }
    item_seen.mark(subrecord);
    return out.menu_items.back();
  };
  for (const auto& subrecord : record.subrecords) {
    const auto signature = subrecord.signature;
    if (signature == sig_full) {
      seen.mark(subrecord);
      out.full_name = localized(plugin, record, subrecord);
    } else if (signature == sig_nam0) {
      seen.mark(subrecord);
      out.header_text = localized(plugin, record, subrecord);
    } else if (signature == sig_wnam) {
      seen.mark(subrecord);
      out.welcome_text = localized(plugin, record, subrecord);
    } else if (signature == sig_cnto) {
      if (subrecord.data.size() != k_holotape_entry_size) {
        throw UiRecordError(record.form_key.to_string() + " CNTO must be 8 bytes");
      }
      if (const auto holotape = link_at(record, context, subrecord, 0)) {
        out.holotapes.push_back(TerminalHolotape{*holotape, read_at<std::int32_t>(record, subrecord, 4)});
      }
    } else if (signature == sig_bsiz || signature == sig_isiz) {
      owner = Owner::none;
    } else if (signature == sig_btxt) {
      TerminalBodyText body;
      body.text = localized(plugin, record, subrecord);
      read_owner_conditions(record, context, body_scope, out.body_texts.size(), body.conditions, body.condition_links);
      body.condition_count = body.conditions.size();
      out.body_texts.push_back(std::move(body));
      owner = Owner::body;
    } else if (signature == sig_itxt) {
      TerminalMenuItem item;
      item.item_text = localized(plugin, record, subrecord);
      read_owner_conditions(record, context, item_scope, out.menu_items.size(), item.conditions, item.condition_links);
      item.condition_count = item.conditions.size();
      out.menu_items.push_back(std::move(item));
      item_seen.reset();
      owner = Owner::item;
    } else if (signature == sig_rnam) {
      in_item(subrecord).response_text = localized(plugin, record, subrecord);
    } else if (signature == sig_anam) {
      in_item(subrecord).type = read_at<std::uint8_t>(record, subrecord, 0);
    } else if (signature == sig_itid) {
      in_item(subrecord).item_id = read_at<std::uint16_t>(record, subrecord, 0);
    } else if (signature == sig_unam) {
      in_item(subrecord).display_text = localized(plugin, record, subrecord);
    } else if (signature == sig_vnam) {
      in_item(subrecord).image = zstring(record, subrecord);
    } else if (signature == sig_tnam) {
      in_item(subrecord).submenu = single_link(record, context, subrecord);
    }
  }
  return out;
}

BodyPartDataInfo decode_body_part_data(const Plugin& plugin, const Record& record) {
  require_signature(record, sig_bptd);
  const auto context = plugin_form_id_context(plugin);
  BodyPartDataInfo out;
  out.key = record.form_key;
  Seen part_seen(record);
  const auto in_part = [&](const Subrecord& subrecord) -> BodyPart& {
    if (out.parts.empty()) {
      throw UiRecordError(record.form_key.to_string() + " " + subrecord.signature.to_string() +
                          " appears outside a body part");
    }
    part_seen.mark(subrecord);
    return out.parts.back();
  };
  for (const auto& subrecord : record.subrecords) {
    const auto signature = subrecord.signature;
    if (signature == sig_bptn) {
      out.parts.emplace_back();
      part_seen.reset();
      part_seen.mark(subrecord);
      out.parts.back().name = localized(plugin, record, subrecord);
    } else if (signature == sig_bpnn) {
      if (out.parts.empty() || out.parts.back().part_node) {
        out.parts.emplace_back();
        part_seen.reset();
      }
      part_seen.mark(subrecord);
      out.parts.back().part_node = zstring(record, subrecord);
    } else if (signature == sig_bpnt) {
      in_part(subrecord).vats_target = zstring(record, subrecord);
    } else if (signature == sig_bpnd) {
      auto& part = in_part(subrecord);
      if (subrecord.data.size() != k_body_part_node_size) {
        throw UiRecordError(record.form_key.to_string() + " BPND must be 101 bytes");
      }
      BodyPartNodeData data;
      data.damage_mult = read_at<float>(record, subrecord, 0);
      data.flags = read_at<std::uint8_t>(record, subrecord, 64);
      data.part_type = read_at<std::uint8_t>(record, subrecord, 65);
      data.health_percent = read_at<std::uint8_t>(record, subrecord, 66);
      data.actor_value = link_at(record, context, subrecord, 67);
      data.to_hit_chance = read_at<std::uint8_t>(record, subrecord, 71);
      data.explodable_explosion_chance = read_at<std::uint8_t>(record, subrecord, 72);
      data.non_lethal_dismemberment_chance = read_at<std::uint8_t>(record, subrecord, 73);
      data.geometry_segment_index = read_at<std::uint8_t>(record, subrecord, 78);
      part.data = data;
    }
  }
  return out;
}

LockKind classify_lock_level(std::uint8_t level) {
  if (level == 0) return LockKind::none;
  if (level < 50) return LockKind::novice;
  if (level < 75) return LockKind::advanced;
  if (level < 100) return LockKind::expert;
  if (level < 251) return LockKind::master;
  switch (level) {
    case 251: return LockKind::barred;
    case 252: return LockKind::chained;
    case 253: return LockKind::requires_terminal;
    case 254: return LockKind::inaccessible;
    default: return LockKind::requires_key;
  }
}

MiscInfo decode_misc_info(const Plugin& plugin, const Record& record) {
  require_signature(record, sig_misc);
  MiscInfo out;
  out.key = record.form_key;
  Seen seen(record);
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature == sig_full) {
      seen.mark(subrecord);
      out.full_name = localized(plugin, record, subrecord);
    } else if (subrecord.signature == sig_data) {
      seen.mark(subrecord);
      if (subrecord.data.size() != k_misc_data_size) {
        throw UiRecordError(record.form_key.to_string() + " DATA must be 8 bytes");
      }
      out.value = read_at<std::int32_t>(record, subrecord, 0);
      out.weight = read_at<float>(record, subrecord, 4);
    }
  }
  out.components = linked_components(plugin, record);
  return out;
}

ActivatorInfo decode_activator_info(const Plugin& plugin, const Record& record) {
  require_signature(record, sig_acti);
  const auto context = plugin_form_id_context(plugin);
  ActivatorInfo out;
  out.key = record.form_key;
  Seen seen(record);
  for (const auto& subrecord : record.subrecords) {
    const auto signature = subrecord.signature;
    if (signature == sig_full) {
      seen.mark(subrecord);
      out.full_name = localized(plugin, record, subrecord);
    } else if (signature == sig_attx) {
      seen.mark(subrecord);
      out.activate_text = localized(plugin, record, subrecord);
    } else if (signature == sig_fnam) {
      seen.mark(subrecord);
      out.flags = read_at<std::uint16_t>(record, subrecord, 0);
    } else if (signature == sig_knam) {
      seen.mark(subrecord);
      out.interaction_keyword = single_link(record, context, subrecord);
    } else if (signature == sig_snam) {
      seen.mark(subrecord);
      out.looping_sound = single_link(record, context, subrecord);
    } else if (signature == sig_vnam) {
      seen.mark(subrecord);
      out.activation_sound = single_link(record, context, subrecord);
    } else if (signature == sig_wnam) {
      seen.mark(subrecord);
      out.water_type = single_link(record, context, subrecord);
    }
  }
  return out;
}

ContainerInfo decode_container_info(const Plugin& plugin, const Record& record) {
  require_signature(record, sig_cont_record);
  const auto context = plugin_form_id_context(plugin);
  ContainerInfo out;
  out.key = record.form_key;
  Seen seen(record);
  for (const auto& subrecord : record.subrecords) {
    const auto signature = subrecord.signature;
    if (signature == sig_full) {
      seen.mark(subrecord);
      out.full_name = localized(plugin, record, subrecord);
    } else if (signature == sig_cnto) {
      read_item(record, context, subrecord, out.items);
    } else if (signature == sig_data) {
      seen.mark(subrecord);
      if (subrecord.data.size() != 5) throw UiRecordError(record.form_key.to_string() + " DATA must be 5 bytes");
      out.flags = read_at<std::uint8_t>(record, subrecord, 0);
      out.weight = read_at<float>(record, subrecord, 1);
    } else if (signature == sig_snam) {
      seen.mark(subrecord);
      out.open_sound = single_link(record, context, subrecord);
    } else if (signature == sig_qnam) {
      seen.mark(subrecord);
      out.close_sound = single_link(record, context, subrecord);
    } else if (signature == sig_tnam) {
      seen.mark(subrecord);
      out.take_all_sound = single_link(record, context, subrecord);
    } else if (signature == sig_onam) {
      seen.mark(subrecord);
      out.filter_list = single_link(record, context, subrecord);
    }
  }
  return out;
}

DoorInfo decode_door_info(const Plugin& plugin, const Record& record) {
  require_signature(record, sig_door_record);
  const auto context = plugin_form_id_context(plugin);
  DoorInfo out;
  out.key = record.form_key;
  Seen seen(record);
  for (const auto& subrecord : record.subrecords) {
    const auto signature = subrecord.signature;
    if (signature == sig_full) {
      seen.mark(subrecord);
      out.full_name = localized(plugin, record, subrecord);
    } else if (signature == sig_snam) {
      seen.mark(subrecord);
      out.open_sound = single_link(record, context, subrecord);
    } else if (signature == sig_anam) {
      seen.mark(subrecord);
      out.close_sound = single_link(record, context, subrecord);
    } else if (signature == sig_bnam) {
      seen.mark(subrecord);
      out.loop_sound = single_link(record, context, subrecord);
    } else if (signature == sig_fnam) {
      seen.mark(subrecord);
      out.flags = read_at<std::uint8_t>(record, subrecord, 0);
    } else if (signature == sig_onam) {
      seen.mark(subrecord);
      out.open_text = localized(plugin, record, subrecord);
    } else if (signature == sig_cnam) {
      seen.mark(subrecord);
      out.close_text = localized(plugin, record, subrecord);
    } else if (signature == sig_tnam) {
      if (const auto destination = single_link(record, context, subrecord)) {
        out.random_destinations.push_back(*destination);
      }
    }
  }
  return out;
}

FurnitureInfo decode_furniture_info(const Plugin& plugin, const Record& record) {
  require_signature(record, sig_furn);
  const auto context = plugin_form_id_context(plugin);
  const bool has_entry_types = record.header.form_version >= k_marker_params_entry_types_version;
  const std::size_t params_size = has_entry_types ? k_marker_params_size : k_marker_params_old_size;
  FurnitureInfo out;
  out.key = record.form_key;
  Seen seen(record);
  bool in_template = false;
  for (const auto& subrecord : record.subrecords) {
    const auto signature = subrecord.signature;
    if (signature == sig_obte) {
      in_template = true;
    } else if (signature == sig_full) {
      if (in_template) continue;
      seen.mark(subrecord);
      out.full_name = localized(plugin, record, subrecord);
    } else if (signature == sig_attx) {
      seen.mark(subrecord);
      out.activate_text = localized(plugin, record, subrecord);
    } else if (signature == sig_fnam) {
      seen.mark(subrecord);
      out.flags = read_at<std::uint16_t>(record, subrecord, 0);
    } else if (signature == sig_mnam) {
      seen.mark(subrecord);
      out.marker_flags = read_at<std::uint32_t>(record, subrecord, 0);
    } else if (signature == sig_wbdt) {
      seen.mark(subrecord);
      out.bench_type = read_at<std::uint8_t>(record, subrecord, 0);
    } else if (signature == sig_wnam) {
      seen.mark(subrecord);
      out.drinking_water_type = single_link(record, context, subrecord);
    } else if (signature == sig_nam1) {
      seen.mark(subrecord);
      out.associated_form = single_link(record, context, subrecord);
    } else if (signature == sig_cnto) {
      read_item(record, context, subrecord, out.items);
    } else if (signature == sig_enam) {
      out.markers.push_back(FurnitureMarker{read_at<std::int32_t>(record, subrecord, 0), std::nullopt});
    } else if (signature == sig_nam0) {
      if (out.markers.empty()) throw UiRecordError(record.form_key.to_string() + " NAM0 appears before any ENAM");
      out.markers.back().disabled_entry_points = read_at<std::uint16_t>(record, subrecord, 2);
    } else if (signature == sig_fnpr) {
      out.entry_points.push_back(FurnitureEntryPoint{read_at<std::uint16_t>(record, subrecord, 0),
                                                      read_at<std::uint16_t>(record, subrecord, 2)});
    } else if (signature == sig_xmrk) {
      seen.mark(subrecord);
      out.marker_model = zstring(record, subrecord);
    } else if (signature == sig_snam) {
      seen.mark(subrecord);
      if (subrecord.data.size() % params_size != 0) {
        throw UiRecordError(record.form_key.to_string() + " SNAM must be a multiple of " +
                            std::to_string(params_size) + " bytes");
      }
      for (std::size_t base = 0; base < subrecord.data.size(); base += params_size) {
        FurnitureMarkerParams params;
        for (std::size_t axis = 0; axis < 3; ++axis) {
          params.offset[axis] = read_at<float>(record, subrecord, base + axis * 4);
        }
        params.rotation_z = read_at<float>(record, subrecord, base + 12);
        params.keyword = link_at(record, context, subrecord, base + 16);
        if (has_entry_types) params.entry_types = read_at<std::uint8_t>(record, subrecord, base + 20);
        out.marker_params.push_back(params);
      }
    }
  }
  return out;
}

ComponentInfo decode_component_info(const Plugin& plugin, const Record& record) {
  require_signature(record, sig_cmpo);
  const auto context = plugin_form_id_context(plugin);
  ComponentInfo out;
  out.key = record.form_key;
  Seen seen(record);
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature == sig_full) {
      seen.mark(subrecord);
      out.full_name = localized(plugin, record, subrecord);
    } else if (subrecord.signature == sig_data) {
      seen.mark(subrecord);
      out.auto_calc_value = read_at<std::uint32_t>(record, subrecord, 0);
    } else if (subrecord.signature == sig_mnam) {
      seen.mark(subrecord);
      out.scrap_item = single_link(record, context, subrecord);
    } else if (subrecord.signature == sig_gnam) {
      seen.mark(subrecord);
      out.mod_scrap_scalar = single_link(record, context, subrecord);
    } else if (subrecord.signature == sig_cusd) {
      seen.mark(subrecord);
      out.crafting_sound = single_link(record, context, subrecord);
    }
  }
  return out;
}

}  // namespace fo4::forms
