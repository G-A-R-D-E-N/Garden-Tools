#include "fo4/plugin/vmad_writer.hpp"
#include "fo4/schema/vmad.hpp"
#include "record_edit.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <string_view>

namespace fo4 {
namespace {

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
    if (std::to_integer<std::uint8_t>(old[0]) > 1) {
      throw schema::ScalarError("VMAD boolean has an unsupported stored value");
    }
    bytes[0] = std::byte{static_cast<std::uint8_t>(std::get<bool>(value))};
  }
  return bytes;
}

}

ScalarMutationStatus set_vmad_property(Plugin& plugin, const FormKey& record_key,
                                        std::size_t script_index, std::size_t property_index,
                                        const VmadValue& value, std::size_t element_index) {
  auto* record = detail::find_mutable_record(plugin, record_key);
  if (record == nullptr) {
    assign_record_form_keys(plugin);
    record = detail::find_mutable_record(plugin, record_key);
  }
  if (record == nullptr) return ScalarMutationStatus::record_missing;
  Subrecord* vmad = nullptr;
  for (auto& sub : record->subrecords) {
    if (sub.signature != Signature::from_string("VMAD")) continue;
    if (vmad != nullptr) throw schema::ScalarError("duplicate VMAD subrecords");
    vmad = &sub;
  }
  if (vmad == nullptr) return ScalarMutationStatus::subrecord_missing;
  if (!vmad_coverage(vmad->data, record->header.signature).complete()) {
    throw schema::ScalarError("VMAD layout is unsupported, malformed or has unknown trailing bytes");
  }
  const auto scripts = read_vmad_scripts(vmad->data);
  if ((scripts.version != 5 && scripts.version != 6) ||
      (scripts.object_format != 1 && scripts.object_format != 2)) {
    throw schema::ScalarError("unsupported VMAD version or object format");
  }
  if (script_index >= scripts.scripts.size() ||
      property_index >= scripts.scripts[script_index].properties.size()) {
    return ScalarMutationStatus::subrecord_missing;
  }
  const auto& property = scripts.scripts[script_index].properties[property_index];
  const bool array = property.type >= 11 && property.type <= 15;
  const auto type = array ? property.type - 10 : property.type;
  if (type < 1 || type > 5 || static_cast<std::size_t>(type) != value.index() + 1) {
    throw schema::ScalarError("VMAD property type does not match the typed setter");
  }
  if ((array && element_index >= *property.element_count) || (!array && element_index != 0)) {
    return ScalarMutationStatus::subrecord_missing;
  }
  auto at = property.value_offset + (array ? 4 : 0);
  for (std::size_t i = 0; i < element_index; ++i) {
    at += value_width(vmad->data, at, static_cast<std::uint8_t>(type));
  }
  const auto width = value_width(vmad->data, at, static_cast<std::uint8_t>(type));
  const auto old = std::span<const std::byte>(vmad->data).subspan(at, width);
  const auto encoded = encode_value(plugin, value, old, scripts.object_format);
  if (std::equal(old.begin(), old.end(), encoded.begin(), encoded.end())) {
    return ScalarMutationStatus::unchanged;
  }
  auto replacement = vmad->data;
  replacement.erase(replacement.begin() + at, replacement.begin() + at + width);
  replacement.insert(replacement.begin() + at, encoded.begin(), encoded.end());
  vmad->data = std::move(replacement);
  detail::mark_record_modified(plugin, *record);
  return ScalarMutationStatus::updated;
}

}
