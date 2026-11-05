#include "fo4/plugin/vmad_writer.hpp"
#include "fo4/schema/vmad.hpp"
#include "record_edit.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4 {
namespace {

constexpr auto k_vmad = Signature::from_string("VMAD");
constexpr std::uint8_t k_type_object = 1;
constexpr std::uint8_t k_type_string = 2;
constexpr std::uint8_t k_type_int = 3;
constexpr std::uint8_t k_type_float = 4;
constexpr std::uint8_t k_type_bool = 5;
constexpr std::uint8_t k_type_struct = 7;
constexpr std::uint8_t k_type_object_array = 11;
constexpr std::uint8_t k_type_string_array = 12;
constexpr std::uint8_t k_type_int_array = 13;
constexpr std::uint8_t k_type_float_array = 14;
constexpr std::uint8_t k_type_bool_array = 15;
constexpr std::uint8_t k_type_variable_array = 16;
constexpr std::uint8_t k_type_struct_array = 17;

void put(std::vector<std::byte>& data, std::size_t at, std::uint32_t value, std::size_t width) {
  for (std::size_t i = 0; i < width; ++i) {
    data[at + i] = std::byte{static_cast<std::uint8_t>(value >> (i * 8))};
  }
}

std::size_t value_width(std::span<const std::byte> data, std::size_t at, std::uint8_t type) {
  if (type == 2) {
    return 2 + std::to_integer<std::uint8_t>(data[at]) +
               (static_cast<std::size_t>(std::to_integer<std::uint8_t>(data[at + 1])) << 8);
  }
  return type == 1 ? 8 : type == 5 ? 1 : 4;
}

std::vector<std::byte> encode_value(const Plugin& plugin, const VmadValue& value,
                                   std::span<const std::byte> old, std::int16_t format) {
  std::vector<std::byte> bytes(old.begin(), old.end());
  if (const auto* object = std::get_if<VmadObjectValue>(&value)) {
    if (!object->target.owner().empty() && object->target.owner() == plugin.key) {
      constexpr std::string_view allowed[] = {
        "AACT", "ACHR", "ACTI", "ALCH", "AMMO", "ARMO", "AVIF", "BOOK", "CAMS", "CELL",
        "CMPO", "COBJ", "CONT", "CSTY", "DIAL", "DOOR", "ECZN", "EFSH", "EXPL", "FACT",
        "FLST", "FURN", "GLOB", "HAZD", "IDLE", "IDLM", "IMAD", "INNR", "IPDS", "KEYM",
        "KYWD", "LCRT", "LCTN", "LIGH", "LVLI", "LVLN", "MESG", "MGEF", "MISC", "MSTT",
        "MUSC", "NOTE", "NPC_", "OMOD", "OTFT", "PACK", "PERK", "PHZD", "PLYR", "PROJ",
        "QUST", "RACE", "REFR", "RFCT", "RFGP", "SCEN", "SCSN", "SNCT", "SNDR", "SOPM",
        "SPEL", "STAT", "TACT", "TERM", "VTYP", "WEAP", "WRLD", "WTHR"};
      const auto* target = find_record(plugin, object->target);
      if (target == nullptr || std::find(std::begin(allowed), std::end(allowed),
                                         target->header.signature.to_string()) == std::end(allowed)) {
        throw schema::ScalarError("VMAD object target type is not allowed");
      }
    }
    const auto raw = encode_plugin_form_id(object->target, plugin_form_id_context(plugin)).value();
    put(bytes, format == 2 ? 4 : 0, raw, 4);
    put(bytes, format == 2 ? 2 : 4, static_cast<std::uint16_t>(object->alias), 2);
  } else if (const auto* text = std::get_if<std::string>(&value)) {
    if (text->size() > std::numeric_limits<std::uint16_t>::max()) {
      throw schema::ScalarError("VMAD string exceeds its 16-bit length");
    }
    bytes.resize(2 + text->size());
    put(bytes, 0, static_cast<std::uint32_t>(text->size()), 2);
    std::transform(text->begin(), text->end(), bytes.begin() + 2,
                   [](char c) { return std::byte{static_cast<std::uint8_t>(c)}; });
  } else if (const auto* integer = std::get_if<std::int32_t>(&value)) {
    put(bytes, 0, static_cast<std::uint32_t>(*integer), 4);
  } else if (const auto* real = std::get_if<float>(&value)) {
    if (!std::isfinite(*real)) throw schema::ScalarError("VMAD float must be finite");
    std::uint32_t bits = 0;
    std::memcpy(&bits, real, 4);
    put(bytes, 0, bits, 4);
  } else {
    bytes[0] = std::byte{static_cast<std::uint8_t>(std::get<bool>(value))};
  }
  return bytes;
}

void append_u8(std::vector<std::byte>& out, std::uint8_t value) {
  out.push_back(std::byte{value});
}

void append_u16(std::vector<std::byte>& out, std::uint16_t value) {
  append_u8(out, static_cast<std::uint8_t>(value));
  append_u8(out, static_cast<std::uint8_t>(value >> 8));
}

void append_u32(std::vector<std::byte>& out, std::uint32_t value) {
  append_u16(out, static_cast<std::uint16_t>(value));
  append_u16(out, static_cast<std::uint16_t>(value >> 16));
}

void append_name(std::vector<std::byte>& out, std::string_view text) {
  if (text.size() > std::numeric_limits<std::uint16_t>::max()) {
    throw schema::ScalarError("VMAD name exceeds its 16-bit length");
  }
  append_u16(out, static_cast<std::uint16_t>(text.size()));
  for (const char ch : text) {
    append_u8(out, static_cast<std::uint8_t>(ch));
  }
}

void append_object(std::vector<std::byte>& out, RawFormId object, std::int16_t alias,
                   std::int16_t format) {
  if (format == 2) {
    append_u16(out, 0);
    append_u16(out, static_cast<std::uint16_t>(alias));
    append_u32(out, object.value());
    return;
  }
  if (format == 1) {
    append_u32(out, object.value());
    append_u16(out, static_cast<std::uint16_t>(alias));
    append_u16(out, 0);
    return;
  }
  throw schema::ScalarError("unsupported VMAD version or object format");
}

void append_bool(std::vector<std::byte>& out, bool value) {
  append_u8(out, static_cast<std::uint8_t>(value ? 1 : 0));
}

void append_float(std::vector<std::byte>& out, float value) {
  if (!std::isfinite(value)) throw schema::ScalarError("VMAD float must be finite");
  std::uint32_t bits = 0;
  std::memcpy(&bits, &value, 4);
  append_u32(out, bits);
}

std::pair<RawFormId, std::int16_t> object_value(const VmadProperty& property) {
  if (!property.object.has_value()) throw schema::ScalarError("VMAD object value is missing");
  return {*property.object, property.alias.value_or(-1)};
}

std::pair<RawFormId, std::int16_t> object_value(const VmadStructMember& member) {
  if (!member.object.has_value()) throw schema::ScalarError("VMAD object value is missing");
  return {member.object->object, member.object->alias};
}

template <class Value>
void append_typed_value(std::vector<std::byte>& out, const Value& value, std::int16_t format) {
  switch (value.type) {
    case 0:
      return;
    case k_type_object: {
      const auto [object, alias] = object_value(value);
      append_object(out, object, alias, format);
      return;
    }
    case k_type_string:
      if (!value.text.has_value()) throw schema::ScalarError("VMAD string value is missing");
      append_name(out, *value.text);
      return;
    case k_type_int:
      if (!value.integer.has_value()) throw schema::ScalarError("VMAD integer value is missing");
      append_u32(out, static_cast<std::uint32_t>(*value.integer));
      return;
    case k_type_float:
      if (!value.real.has_value()) throw schema::ScalarError("VMAD float value is missing");
      append_float(out, *value.real);
      return;
    case k_type_bool:
      if (!value.boolean.has_value()) throw schema::ScalarError("VMAD boolean value is missing");
      append_bool(out, *value.boolean);
      return;
    default:
      throw schema::ScalarError("VMAD struct member type is not authorable");
  }
}

void append_struct(std::vector<std::byte>& out, const VmadStruct& value, std::int16_t format) {
  append_u32(out, static_cast<std::uint32_t>(value.members.size()));
  for (const auto& member : value.members) {
    append_name(out, member.name);
    append_u8(out, member.type);
    append_u8(out, member.flags);
    append_typed_value(out, member, format);
  }
}

const std::vector<VmadStruct>& structs_of(const VmadProperty& property) {
  if (!property.struct_elements.has_value()) {
    throw schema::ScalarError("VMAD struct value is not authorable");
  }
  return *property.struct_elements;
}

std::vector<std::byte> encode_property(const VmadProperty& property, std::int16_t format) {
  std::vector<std::byte> out;
  append_name(out, property.name);
  append_u8(out, property.type);
  append_u8(out, property.flags);
  switch (property.type) {
    case 0:
      return out;
    case k_type_object:
    case k_type_string:
    case k_type_int:
    case k_type_float:
    case k_type_bool:
      append_typed_value(out, property, format);
      return out;
    case k_type_struct: {
      const auto& structs = structs_of(property);
      if (structs.size() != 1) throw schema::ScalarError("a VMAD struct property holds one struct");
      append_struct(out, structs.front(), format);
      return out;
    }
    case k_type_object_array: {
      if (!property.object_elements.has_value()) {
        throw schema::ScalarError("VMAD object array value is missing");
      }
      append_u32(out, static_cast<std::uint32_t>(property.object_elements->size()));
      for (const auto& element : *property.object_elements) {
        append_object(out, element.object, element.alias, format);
      }
      return out;
    }
    case k_type_string_array: {
      if (!property.text_elements.has_value()) {
        throw schema::ScalarError("VMAD string array value is missing");
      }
      append_u32(out, static_cast<std::uint32_t>(property.text_elements->size()));
      for (const auto& element : *property.text_elements) {
        append_name(out, element);
      }
      return out;
    }
    case k_type_int_array: {
      if (!property.integer_elements.has_value()) {
        throw schema::ScalarError("VMAD integer array value is missing");
      }
      append_u32(out, static_cast<std::uint32_t>(property.integer_elements->size()));
      for (const auto element : *property.integer_elements) {
        append_u32(out, static_cast<std::uint32_t>(element));
      }
      return out;
    }
    case k_type_float_array: {
      if (!property.real_elements.has_value()) {
        throw schema::ScalarError("VMAD float array value is missing");
      }
      append_u32(out, static_cast<std::uint32_t>(property.real_elements->size()));
      for (const auto element : *property.real_elements) {
        append_float(out, element);
      }
      return out;
    }
    case k_type_bool_array: {
      if (!property.boolean_elements.has_value()) {
        throw schema::ScalarError("VMAD boolean array value is missing");
      }
      append_u32(out, static_cast<std::uint32_t>(property.boolean_elements->size()));
      for (const auto element : *property.boolean_elements) {
        append_bool(out, element);
      }
      return out;
    }
    case k_type_variable_array:
      if (property.element_count.value_or(0) != 0) {
        throw schema::ScalarError("a nonempty VMAD variable array is not authorable");
      }
      append_u32(out, 0);
      return out;
    case k_type_struct_array: {
      const auto& structs = structs_of(property);
      append_u32(out, static_cast<std::uint32_t>(structs.size()));
      for (const auto& element : structs) {
        append_struct(out, element, format);
      }
      return out;
    }
    default:
      throw schema::ScalarError("VMAD property type is not authorable");
  }
}

void splice(std::vector<std::byte>& data, std::size_t at, std::size_t count,
            std::span<const std::byte> inserted) {
  data.erase(data.begin() + static_cast<std::ptrdiff_t>(at),
             data.begin() + static_cast<std::ptrdiff_t>(at + count));
  data.insert(data.begin() + static_cast<std::ptrdiff_t>(at), inserted.begin(), inserted.end());
}

struct VmadEdit {
  Record* record{nullptr};
  Subrecord* vmad{nullptr};
  VmadScripts scripts;
  std::size_t leading_end{0};
};

enum class VmadLookup { ok, record_missing, subrecord_missing };

VmadLookup load_vmad(Plugin& plugin, const FormKey& record_key, VmadEdit& edit) {
  edit.record = detail::find_mutable_record(plugin, record_key);
  if (edit.record == nullptr) {
    assign_record_form_keys(plugin);
    edit.record = detail::find_mutable_record(plugin, record_key);
  }
  if (edit.record == nullptr) return VmadLookup::record_missing;
  for (auto& sub : edit.record->subrecords) {
    if (sub.signature != k_vmad) continue;
    if (edit.vmad != nullptr) throw schema::ScalarError("duplicate VMAD subrecords");
    edit.vmad = &sub;
  }
  if (edit.vmad == nullptr) return VmadLookup::subrecord_missing;
  if (!vmad_coverage(edit.vmad->data, edit.record->header.signature).complete()) {
    throw schema::ScalarError("VMAD layout is unsupported, malformed or has unknown trailing bytes");
  }
  edit.scripts = read_vmad_scripts(edit.vmad->data);
  if (edit.scripts.unsupported) {
    throw schema::ScalarError("VMAD has properties that cannot be re-encoded");
  }
  if ((edit.scripts.version != 5 && edit.scripts.version != 6) ||
      (edit.scripts.object_format != 1 && edit.scripts.object_format != 2)) {
    throw schema::ScalarError("unsupported VMAD version or object format");
  }
  edit.leading_end = edit.vmad->data.size() - edit.scripts.trailing_bytes;
  return VmadLookup::ok;
}

VmadStructureMutationStatus absent_status(VmadLookup lookup) {
  return lookup == VmadLookup::record_missing ? VmadStructureMutationStatus::record_missing
                                              : VmadStructureMutationStatus::subrecord_missing;
}

std::size_t tail_start(const VmadEdit& edit, std::size_t script_index) {
  return script_index + 1 < edit.scripts.scripts.size()
             ? edit.scripts.scripts[script_index + 1].offset
             : edit.leading_end;
}

bool vmad_authorable(Signature signature) {
  constexpr std::string_view records[] = {
      "ACHR", "ACTI", "ALCH", "AMMO", "ARMO", "BOOK", "CONT", "DOOR", "FLOR", "FURN",
      "INFO", "INGR", "KEYM", "LIGH", "MGEF", "MISC", "MSTT", "NOTE", "NPC_", "PACK",
      "PARW", "PBAR", "PBEA", "PCON", "PERK", "PFLA", "PGRE", "PHZD", "PMIS", "QUST",
      "RACE", "REFR", "SCEN", "STAT", "TACT", "TERM", "TREE", "WEAP"};
  const auto text = signature.to_string();
  return std::find(std::begin(records), std::end(records), text) != std::end(records);
}

// Constraint: Papyrus script identifiers compare case-insensitively, so a second entry for one script is refused.
bool same_name(std::string_view left, std::string_view right) {
  return left.size() == right.size() &&
         std::equal(left.begin(), left.end(), right.begin(), [](char a, char b) {
           return std::tolower(static_cast<unsigned char>(a)) ==
                  std::tolower(static_cast<unsigned char>(b));
         });
}

void append_quest_fragment(std::vector<std::byte>& out, const QuestVmadFragment& fragment) {
  append_u16(out, fragment.stage);
  append_u16(out, fragment.unknown);
  append_u32(out, static_cast<std::uint32_t>(fragment.stage_index));
  append_u8(out, fragment.unknown_byte);
  append_name(out, fragment.script);
  append_name(out, fragment.function);
}

struct QuestFragmentEdit {
  Record* record{nullptr};
  Subrecord* vmad{nullptr};
  std::size_t tail_start{0};
  QuestVmadExtras extras;
};

std::optional<VmadStructureMutationStatus> load_quest_fragments(Plugin& plugin,
                                                               const FormKey& record_key,
                                                               QuestFragmentEdit& edit) {
  VmadEdit vmad;
  const auto lookup = load_vmad(plugin, record_key, vmad);
  if (lookup != VmadLookup::ok) return absent_status(lookup);
  if (vmad.record->header.signature != sig_qust) {
    return VmadStructureMutationStatus::not_authorable;
  }
  edit.record = vmad.record;
  edit.vmad = vmad.vmad;
  edit.tail_start = vmad.leading_end;
  if (vmad.scripts.trailing_bytes == 0) return VmadStructureMutationStatus::binding_missing;
  edit.extras = read_quest_vmad_extras(
      std::span<const std::byte>(edit.vmad->data).subspan(edit.tail_start),
      vmad.scripts.object_format);
  if (edit.extras.unsupported) {
    throw schema::ScalarError(edit.extras.unsupported_reason.empty()
                                  ? "VMAD quest extras are unsupported"
                                  : edit.extras.unsupported_reason);
  }
  if (!edit.extras.fully_consumed) {
    throw schema::ScalarError("VMAD quest extras were not fully consumed");
  }
  return std::nullopt;
}

const QuestVmadFragment* find_quest_fragment(const std::vector<QuestVmadFragment>& fragments,
                                             std::uint16_t stage,
                                             std::optional<std::int32_t> stage_index) {
  const QuestVmadFragment* found = nullptr;
  for (const auto& fragment : fragments) {
    if (fragment.stage != stage) continue;
    if (stage_index.has_value() && fragment.stage_index != *stage_index) continue;
    if (found != nullptr) {
      throw schema::ScalarError(
          stage_index.has_value()
              ? "the quest VMAD binds one stage index twice"
              : "the quest VMAD binds one stage twice; address the fragment by its stage "
                "index");
    }
    found = &fragment;
  }
  return found;
}

VmadStructureMutationStatus set_quest_fragment(Plugin& plugin, const FormKey& record_key,
                                               std::uint16_t stage,
                                               std::optional<std::int32_t> stage_index,
                                               std::string_view script,
                                               std::string_view function) {
  QuestFragmentEdit edit;
  if (const auto refusal = load_quest_fragments(plugin, record_key, edit)) return *refusal;
  if (script.empty() || function.empty()) {
    throw schema::ScalarError("VMAD fragment script and function names are required");
  }
  const auto* found = find_quest_fragment(edit.extras.fragments, stage, stage_index);
  if (found == nullptr) return VmadStructureMutationStatus::binding_missing;
  auto updated = *found;
  updated.script = std::string(script);
  updated.function = std::string(function);
  std::vector<std::byte> entry;
  append_quest_fragment(entry, updated);
  const auto begin = edit.tail_start + found->offset;
  const auto end = edit.tail_start + found->end;
  if (entry.size() == end - begin &&
      std::equal(entry.begin(), entry.end(),
                 edit.vmad->data.begin() + static_cast<std::ptrdiff_t>(begin))) {
    return VmadStructureMutationStatus::unchanged;
  }
  splice(edit.vmad->data, begin, end - begin, entry);
  detail::mark_record_modified(plugin, *edit.record);
  return VmadStructureMutationStatus::updated;
}

VmadStructureMutationStatus remove_quest_fragment(Plugin& plugin, const FormKey& record_key,
                                                  std::uint16_t stage,
                                                  std::optional<std::int32_t> stage_index) {
  QuestFragmentEdit edit;
  if (const auto refusal = load_quest_fragments(plugin, record_key, edit)) return *refusal;
  const auto* found = find_quest_fragment(edit.extras.fragments, stage, stage_index);
  if (found == nullptr) return VmadStructureMutationStatus::binding_missing;
  const auto begin = edit.tail_start + found->offset;
  const auto end = edit.tail_start + found->end;
  splice(edit.vmad->data, begin, end - begin, {});
  put(edit.vmad->data, edit.tail_start + edit.extras.count_offset,
      static_cast<std::uint32_t>(edit.extras.fragments.size() - 1), 2);
  detail::mark_record_modified(plugin, *edit.record);
  return VmadStructureMutationStatus::updated;
}

ScalarMutationStatus write_property_value(const Plugin& plugin, std::vector<std::byte>& data,
                                          std::size_t base, const VmadProperty& property,
                                          const VmadValue& value, std::size_t element_index,
                                          std::int16_t format) {
  const bool array = property.type >= 11 && property.type <= 15;
  const auto type = array ? property.type - 10 : property.type;
  if (type < 1 || type > 5 || static_cast<std::size_t>(type) != value.index() + 1) {
    throw schema::ScalarError("VMAD property type does not match the typed setter");
  }
  if ((array && element_index >= *property.element_count) || (!array && element_index != 0)) {
    return ScalarMutationStatus::subrecord_missing;
  }
  auto at = base + property.value_offset + (array ? 4 : 0);
  for (std::size_t i = 0; i < element_index; ++i) {
    at += value_width(data, at, static_cast<std::uint8_t>(type));
  }
  const auto width = value_width(data, at, static_cast<std::uint8_t>(type));
  const auto old = std::span<const std::byte>(data).subspan(at, width);
  const auto encoded = encode_value(plugin, value, old, format);
  if (std::equal(old.begin(), old.end(), encoded.begin(), encoded.end())) {
    return ScalarMutationStatus::unchanged;
  }
  splice(data, at, width, encoded);
  return ScalarMutationStatus::updated;
}

struct QuestAliasEdit {
  Record* record{nullptr};
  Subrecord* vmad{nullptr};
  std::size_t tail_start{0};
  std::int16_t version{0};
  std::int16_t object_format{0};
  bool has_extras{false};
  QuestVmadExtras extras;
};

void read_quest_alias_extras(QuestAliasEdit& edit) {
  edit.extras = read_quest_vmad_extras(
      std::span<const std::byte>(edit.vmad->data).subspan(edit.tail_start), edit.object_format);
  if (edit.extras.unsupported) {
    throw schema::ScalarError(edit.extras.unsupported_reason.empty()
                                  ? "VMAD quest extras are unsupported"
                                  : edit.extras.unsupported_reason);
  }
  if (!edit.extras.fully_consumed) {
    throw schema::ScalarError("VMAD quest extras were not fully consumed");
  }
  edit.has_extras = true;
}

std::optional<VmadStructureMutationStatus> load_quest_aliases(Plugin& plugin,
                                                              const FormKey& record_key,
                                                              QuestAliasEdit& edit) {
  if (const auto* record = detail::find_mutable_record(plugin, record_key);
      record != nullptr && record->header.signature != sig_qust) {
    return VmadStructureMutationStatus::not_authorable;
  }
  VmadEdit vmad;
  const auto lookup = load_vmad(plugin, record_key, vmad);
  if (lookup != VmadLookup::ok) return absent_status(lookup);
  if (vmad.record->header.signature != sig_qust) {
    return VmadStructureMutationStatus::not_authorable;
  }
  edit.record = vmad.record;
  edit.vmad = vmad.vmad;
  edit.tail_start = vmad.leading_end;
  edit.version = vmad.scripts.version;
  edit.object_format = vmad.scripts.object_format;
  if (vmad.scripts.trailing_bytes != 0) read_quest_alias_extras(edit);
  return std::nullopt;
}

void open_quest_alias_extras(QuestAliasEdit& edit) {
  if (edit.has_extras) return;
  const std::vector<std::byte> header{std::byte{3}, std::byte{0}, std::byte{0}, std::byte{0},
                                      std::byte{0}, std::byte{0}, std::byte{0}};
  splice(edit.vmad->data, edit.tail_start, 0, header);
  read_quest_alias_extras(edit);
}

std::optional<std::size_t> find_quest_alias(const QuestAliasEdit& edit, std::int16_t alias_id) {
  std::optional<std::size_t> found;
  for (std::size_t index = 0; index < edit.extras.aliases.size(); ++index) {
    if (edit.extras.aliases[index].alias_id != alias_id) continue;
    if (found.has_value()) throw schema::ScalarError("the quest VMAD binds one alias twice");
    found = index;
  }
  return found;
}

std::size_t alias_script_end(const QuestVmadAliasScripts& alias, std::size_t script) {
  return script + 1 < alias.scripts.size() ? alias.scripts[script + 1].offset : alias.end;
}

}

VmadStructureMutationStatus add_vmad(Plugin& plugin, const FormKey& record_key) {
  auto* record = detail::find_mutable_record(plugin, record_key);
  if (record == nullptr) {
    assign_record_form_keys(plugin);
    record = detail::find_mutable_record(plugin, record_key);
  }
  if (record == nullptr) return VmadStructureMutationStatus::record_missing;
  if (!vmad_authorable(record->header.signature)) {
    return VmadStructureMutationStatus::not_authorable;
  }
  std::size_t position = 0;
  for (std::size_t index = 0; index < record->subrecords.size(); ++index) {
    if (record->subrecords[index].signature == k_vmad) {
      return VmadStructureMutationStatus::already_present;
    }
    if (record->subrecords[index].signature == sig_edid) position = index + 1;
  }
  const std::vector<std::byte> empty{std::byte{6}, std::byte{0}, std::byte{2},
                                     std::byte{0}, std::byte{0}, std::byte{0}};
  record->subrecords.insert(record->subrecords.begin() + static_cast<std::ptrdiff_t>(position),
                            Subrecord{k_vmad, empty, false});
  detail::mark_record_modified(plugin, *record);
  return VmadStructureMutationStatus::updated;
}

VmadStructureMutationStatus add_vmad_script(Plugin& plugin, const FormKey& record_key,
                                            std::size_t index, std::string_view name,
                                            std::uint8_t flags) {
  VmadEdit edit;
  const auto lookup = load_vmad(plugin, record_key, edit);
  if (lookup != VmadLookup::ok) return absent_status(lookup);
  if (name.empty()) throw schema::ScalarError("VMAD script name is required");
  if (index > edit.scripts.scripts.size()) {
    return VmadStructureMutationStatus::index_out_of_range;
  }
  for (const auto& script : edit.scripts.scripts) {
    if (same_name(script.name, name)) return VmadStructureMutationStatus::already_present;
  }
  if (edit.scripts.scripts.size() >= std::numeric_limits<std::uint16_t>::max()) {
    throw schema::ScalarError("the VMAD script list is full");
  }
  std::vector<std::byte> entry;
  append_name(entry, name);
  append_u8(entry, flags);
  append_u16(entry, 0);
  const auto at = index < edit.scripts.scripts.size() ? edit.scripts.scripts[index].offset
                                                      : edit.leading_end;
  splice(edit.vmad->data, at, 0, entry);
  put(edit.vmad->data, 4, static_cast<std::uint32_t>(edit.scripts.scripts.size() + 1), 2);
  detail::mark_record_modified(plugin, *edit.record);
  return VmadStructureMutationStatus::updated;
}

VmadStructureMutationStatus remove_vmad_script(Plugin& plugin, const FormKey& record_key,
                                               std::size_t index) {
  VmadEdit edit;
  const auto lookup = load_vmad(plugin, record_key, edit);
  if (lookup != VmadLookup::ok) return absent_status(lookup);
  if (index >= edit.scripts.scripts.size()) {
    return VmadStructureMutationStatus::index_out_of_range;
  }
  const auto begin = edit.scripts.scripts[index].offset;
  const auto end = tail_start(edit, index);
  splice(edit.vmad->data, begin, end - begin, {});
  put(edit.vmad->data, 4, static_cast<std::uint32_t>(edit.scripts.scripts.size() - 1), 2);
  detail::mark_record_modified(plugin, *edit.record);
  return VmadStructureMutationStatus::updated;
}

VmadStructureMutationStatus add_vmad_property(Plugin& plugin, const FormKey& record_key,
                                              std::size_t script, std::size_t index,
                                              const VmadProperty& property) {
  VmadEdit edit;
  const auto lookup = load_vmad(plugin, record_key, edit);
  if (lookup != VmadLookup::ok) return absent_status(lookup);
  if (script >= edit.scripts.scripts.size()) {
    return VmadStructureMutationStatus::index_out_of_range;
  }
  const auto& target = edit.scripts.scripts[script];
  if (index > target.properties.size()) return VmadStructureMutationStatus::index_out_of_range;
  for (const auto& existing : target.properties) {
    if (same_name(existing.name, property.name)) return VmadStructureMutationStatus::already_present;
  }
  if (target.properties.size() >= std::numeric_limits<std::uint16_t>::max()) {
    throw schema::ScalarError("the VMAD property list is full");
  }
  const auto encoded = encode_property(property, edit.scripts.object_format);
  const auto at = index < target.properties.size() ? target.properties[index].offset
                                                   : tail_start(edit, script);
  splice(edit.vmad->data, at, 0, encoded);
  put(edit.vmad->data, target.property_count_offset,
      static_cast<std::uint32_t>(target.properties.size() + 1), 2);
  detail::mark_record_modified(plugin, *edit.record);
  return VmadStructureMutationStatus::updated;
}

VmadStructureMutationStatus remove_vmad_property(Plugin& plugin, const FormKey& record_key,
                                                 std::size_t script, std::size_t index) {
  VmadEdit edit;
  const auto lookup = load_vmad(plugin, record_key, edit);
  if (lookup != VmadLookup::ok) return absent_status(lookup);
  if (script >= edit.scripts.scripts.size()) {
    return VmadStructureMutationStatus::index_out_of_range;
  }
  const auto& target = edit.scripts.scripts[script];
  if (index >= target.properties.size()) return VmadStructureMutationStatus::index_out_of_range;
  const auto& property = target.properties[index];
  const auto begin = property.offset;
  const auto end = property.value_offset + property.value_size;
  splice(edit.vmad->data, begin, end - begin, {});
  put(edit.vmad->data, target.property_count_offset,
      static_cast<std::uint32_t>(target.properties.size() - 1), 2);
  detail::mark_record_modified(plugin, *edit.record);
  return VmadStructureMutationStatus::updated;
}

VmadStructureMutationStatus replace_vmad_property(Plugin& plugin, const FormKey& record_key,
                                                  std::size_t script, std::size_t index,
                                                  const VmadProperty& property) {
  VmadEdit edit;
  const auto lookup = load_vmad(plugin, record_key, edit);
  if (lookup != VmadLookup::ok) return absent_status(lookup);
  if (script >= edit.scripts.scripts.size()) {
    return VmadStructureMutationStatus::index_out_of_range;
  }
  const auto& target = edit.scripts.scripts[script];
  if (index >= target.properties.size()) return VmadStructureMutationStatus::index_out_of_range;
  const auto& current = target.properties[index];
  const auto begin = current.offset;
  const auto end = current.value_offset + current.value_size;
  const auto encoded = encode_property(property, edit.scripts.object_format);
  if (encoded.size() == end - begin &&
      std::equal(encoded.begin(), encoded.end(),
                 edit.vmad->data.begin() + static_cast<std::ptrdiff_t>(begin))) {
    return VmadStructureMutationStatus::unchanged;
  }
  splice(edit.vmad->data, begin, end - begin, encoded);
  detail::mark_record_modified(plugin, *edit.record);
  return VmadStructureMutationStatus::updated;
}

VmadStructureMutationStatus add_quest_vmad_fragment(Plugin& plugin, const FormKey& record_key,
                                                    std::uint16_t stage, std::int32_t stage_index,
                                                    std::string_view script,
                                                    std::string_view function) {
  QuestFragmentEdit edit;
  if (const auto refusal = load_quest_fragments(plugin, record_key, edit)) return *refusal;
  if (script.empty() || function.empty()) {
    throw schema::ScalarError("VMAD fragment script and function names are required");
  }
  if (edit.extras.script_name.empty()) return VmadStructureMutationStatus::binding_missing;
  const auto index = static_cast<std::uint32_t>(stage_index);
  for (const auto& existing : edit.extras.fragments) {
    if (existing.stage == stage && static_cast<std::uint32_t>(existing.stage_index) == index) {
      return VmadStructureMutationStatus::already_present;
    }
  }
  if (edit.extras.fragments.size() >= std::numeric_limits<std::uint16_t>::max()) {
    throw schema::ScalarError("the quest VMAD fragment list is full");
  }
  QuestVmadFragment added;
  added.stage = stage;
  added.stage_index = stage_index;
  added.unknown_byte = 1;
  added.script = std::string(script);
  added.function = std::string(function);
  std::vector<std::byte> entry;
  append_quest_fragment(entry, added);
  auto at = edit.tail_start + edit.extras.entries_begin;
  for (const auto& existing : edit.extras.fragments) {
    if (existing.stage > stage ||
        (existing.stage == stage && static_cast<std::uint32_t>(existing.stage_index) > index)) {
      at = edit.tail_start + existing.offset;
      break;
    }
    at = edit.tail_start + existing.end;
  }
  splice(edit.vmad->data, at, 0, entry);
  put(edit.vmad->data, edit.tail_start + edit.extras.count_offset,
      static_cast<std::uint32_t>(edit.extras.fragments.size() + 1), 2);
  detail::mark_record_modified(plugin, *edit.record);
  return VmadStructureMutationStatus::updated;
}

VmadStructureMutationStatus set_quest_vmad_fragment(Plugin& plugin, const FormKey& record_key,
                                                    std::uint16_t stage, std::string_view script,
                                                    std::string_view function) {
  return set_quest_fragment(plugin, record_key, stage, std::nullopt, script, function);
}

VmadStructureMutationStatus set_quest_vmad_fragment(Plugin& plugin, const FormKey& record_key,
                                                    std::uint16_t stage, std::int32_t stage_index,
                                                    std::string_view script,
                                                    std::string_view function) {
  return set_quest_fragment(plugin, record_key, stage, stage_index, script, function);
}

VmadStructureMutationStatus remove_quest_vmad_fragment(Plugin& plugin, const FormKey& record_key,
                                                       std::uint16_t stage) {
  return remove_quest_fragment(plugin, record_key, stage, std::nullopt);
}

VmadStructureMutationStatus remove_quest_vmad_fragment(Plugin& plugin, const FormKey& record_key,
                                                       std::uint16_t stage,
                                                       std::int32_t stage_index) {
  return remove_quest_fragment(plugin, record_key, stage, stage_index);
}

VmadStructureMutationStatus add_quest_alias_script(Plugin& plugin, const FormKey& record_key,
                                                   std::int16_t alias_id, std::size_t index,
                                                   std::string_view name, std::uint8_t flags) {
  QuestAliasEdit edit;
  if (const auto refusal = load_quest_aliases(plugin, record_key, edit)) return *refusal;
  if (name.empty()) throw schema::ScalarError("VMAD script name is required");
  std::vector<std::byte> script;
  append_name(script, name);
  append_u8(script, flags);
  append_u16(script, 0);
  if (const auto found = find_quest_alias(edit, alias_id)) {
    const auto& alias = edit.extras.aliases[*found];
    if (index > alias.scripts.size()) return VmadStructureMutationStatus::index_out_of_range;
    for (const auto& existing : alias.scripts) {
      if (same_name(existing.name, name)) return VmadStructureMutationStatus::already_present;
    }
    if (alias.scripts.size() >= std::numeric_limits<std::uint16_t>::max()) {
      throw schema::ScalarError("the quest alias script list is full");
    }
    const auto at = edit.tail_start +
                    (index < alias.scripts.size() ? alias.scripts[index].offset : alias.end);
    splice(edit.vmad->data, at, 0, script);
    put(edit.vmad->data, edit.tail_start + alias.script_count_offset,
        static_cast<std::uint32_t>(alias.scripts.size() + 1), 2);
  } else {
    if (index != 0) return VmadStructureMutationStatus::index_out_of_range;
    if (edit.extras.aliases.size() >= std::numeric_limits<std::uint16_t>::max()) {
      throw schema::ScalarError("the quest alias list is full");
    }
    open_quest_alias_extras(edit);
    std::vector<std::byte> entry;
    append_object(entry, RawFormId{0u}, alias_id, edit.object_format);
    append_u16(entry, static_cast<std::uint16_t>(edit.version));
    append_u16(entry, static_cast<std::uint16_t>(edit.object_format));
    append_u16(entry, 1);
    entry.insert(entry.end(), script.begin(), script.end());
    auto at = edit.tail_start + edit.extras.aliases_count_offset + 2;
    for (const auto& alias : edit.extras.aliases) {
      if (alias.alias_id > alias_id) {
        at = edit.tail_start + alias.offset;
        break;
      }
      at = edit.tail_start + alias.end;
    }
    splice(edit.vmad->data, at, 0, entry);
    put(edit.vmad->data, edit.tail_start + edit.extras.aliases_count_offset,
        static_cast<std::uint32_t>(edit.extras.aliases.size() + 1), 2);
  }
  detail::mark_record_modified(plugin, *edit.record);
  return VmadStructureMutationStatus::updated;
}

VmadStructureMutationStatus remove_quest_alias_script(Plugin& plugin, const FormKey& record_key,
                                                      std::int16_t alias_id, std::size_t index) {
  QuestAliasEdit edit;
  if (const auto refusal = load_quest_aliases(plugin, record_key, edit)) return *refusal;
  const auto found = find_quest_alias(edit, alias_id);
  if (!found) return VmadStructureMutationStatus::binding_missing;
  const auto& alias = edit.extras.aliases[*found];
  if (index >= alias.scripts.size()) return VmadStructureMutationStatus::index_out_of_range;
  if (alias.scripts.size() == 1) {
    splice(edit.vmad->data, edit.tail_start + alias.offset, alias.end - alias.offset, {});
    put(edit.vmad->data, edit.tail_start + edit.extras.aliases_count_offset,
        static_cast<std::uint32_t>(edit.extras.aliases.size() - 1), 2);
  } else {
    const auto begin = alias.scripts[index].offset;
    const auto end = alias_script_end(alias, index);
    splice(edit.vmad->data, edit.tail_start + begin, end - begin, {});
    put(edit.vmad->data, edit.tail_start + alias.script_count_offset,
        static_cast<std::uint32_t>(alias.scripts.size() - 1), 2);
  }
  detail::mark_record_modified(plugin, *edit.record);
  return VmadStructureMutationStatus::updated;
}

VmadStructureMutationStatus add_quest_alias_script_property(Plugin& plugin,
                                                            const FormKey& record_key,
                                                            std::int16_t alias_id, std::size_t script,
                                                            std::size_t index,
                                                            const VmadProperty& property) {
  QuestAliasEdit edit;
  if (const auto refusal = load_quest_aliases(plugin, record_key, edit)) return *refusal;
  const auto found = find_quest_alias(edit, alias_id);
  if (!found) return VmadStructureMutationStatus::binding_missing;
  const auto& alias = edit.extras.aliases[*found];
  if (script >= alias.scripts.size()) return VmadStructureMutationStatus::index_out_of_range;
  const auto& target = alias.scripts[script];
  if (index > target.properties.size()) return VmadStructureMutationStatus::index_out_of_range;
  for (const auto& existing : target.properties) {
    if (same_name(existing.name, property.name)) return VmadStructureMutationStatus::already_present;
  }
  if (target.properties.size() >= std::numeric_limits<std::uint16_t>::max()) {
    throw schema::ScalarError("the quest alias property list is full");
  }
  const auto encoded = encode_property(property, alias.script_format);
  const auto at = edit.tail_start + (index < target.properties.size()
                                         ? target.properties[index].offset
                                         : alias_script_end(alias, script));
  splice(edit.vmad->data, at, 0, encoded);
  put(edit.vmad->data, edit.tail_start + target.property_count_offset,
      static_cast<std::uint32_t>(target.properties.size() + 1), 2);
  detail::mark_record_modified(plugin, *edit.record);
  return VmadStructureMutationStatus::updated;
}

VmadStructureMutationStatus remove_quest_alias_script_property(Plugin& plugin,
                                                               const FormKey& record_key,
                                                               std::int16_t alias_id,
                                                               std::size_t script,
                                                               std::size_t index) {
  QuestAliasEdit edit;
  if (const auto refusal = load_quest_aliases(plugin, record_key, edit)) return *refusal;
  const auto found = find_quest_alias(edit, alias_id);
  if (!found) return VmadStructureMutationStatus::binding_missing;
  const auto& alias = edit.extras.aliases[*found];
  if (script >= alias.scripts.size()) return VmadStructureMutationStatus::index_out_of_range;
  const auto& target = alias.scripts[script];
  if (index >= target.properties.size()) return VmadStructureMutationStatus::index_out_of_range;
  const auto& property = target.properties[index];
  const auto begin = property.offset;
  const auto end = property.value_offset + property.value_size;
  splice(edit.vmad->data, edit.tail_start + begin, end - begin, {});
  put(edit.vmad->data, edit.tail_start + target.property_count_offset,
      static_cast<std::uint32_t>(target.properties.size() - 1), 2);
  detail::mark_record_modified(plugin, *edit.record);
  return VmadStructureMutationStatus::updated;
}

ScalarMutationStatus set_vmad_property(Plugin& plugin, const FormKey& record_key,
                                        std::size_t script_index, std::size_t property_index,
                                        const VmadValue& value, std::size_t element_index) {
  VmadEdit edit;
  const auto lookup = load_vmad(plugin, record_key, edit);
  if (lookup != VmadLookup::ok) {
    return lookup == VmadLookup::record_missing ? ScalarMutationStatus::record_missing
                                                : ScalarMutationStatus::subrecord_missing;
  }
  const auto& scripts = edit.scripts;
  if (script_index >= scripts.scripts.size() ||
      property_index >= scripts.scripts[script_index].properties.size()) {
    return ScalarMutationStatus::subrecord_missing;
  }
  const auto status = write_property_value(plugin, edit.vmad->data, 0,
                                           scripts.scripts[script_index].properties[property_index],
                                           value, element_index, scripts.object_format);
  if (status == ScalarMutationStatus::updated) detail::mark_record_modified(plugin, *edit.record);
  return status;
}

VmadStructureMutationStatus set_quest_alias_script_value(Plugin& plugin, const FormKey& record_key,
                                                         std::int16_t alias_id, std::size_t script,
                                                         std::size_t property, const VmadValue& value,
                                                         std::size_t element_index) {
  QuestAliasEdit edit;
  if (const auto refusal = load_quest_aliases(plugin, record_key, edit)) return *refusal;
  const auto found = edit.has_extras ? find_quest_alias(edit, alias_id) : std::nullopt;
  if (!found) return VmadStructureMutationStatus::subrecord_missing;
  const auto& alias = edit.extras.aliases[*found];
  if (script >= alias.scripts.size() || property >= alias.scripts[script].properties.size()) {
    return VmadStructureMutationStatus::subrecord_missing;
  }
  const auto status = write_property_value(plugin, edit.vmad->data, edit.tail_start,
                                           alias.scripts[script].properties[property], value,
                                           element_index, alias.script_format);
  if (status == ScalarMutationStatus::updated) {
    detail::mark_record_modified(plugin, *edit.record);
    return VmadStructureMutationStatus::updated;
  }
  if (status == ScalarMutationStatus::unchanged) return VmadStructureMutationStatus::unchanged;
  return VmadStructureMutationStatus::subrecord_missing;
}

}
