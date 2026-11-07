#include "fo4/forms/weapon.hpp"

#include "fo4/plugin/reader.hpp"

#include <cstring>
#include <span>

namespace fo4::forms {
namespace {

constexpr auto k_weap = Signature::from_string("WEAP");
constexpr auto k_dama = Signature::from_string("DAMA");
constexpr std::size_t k_entry_size = 8;
constexpr std::size_t k_curve_entry_size = 12;

std::uint32_t u32_at(std::span<const std::byte> data, std::size_t offset) {
  std::uint32_t value = 0;
  std::memcpy(&value, data.data() + offset, 4);
  return value;
}

FormKey form_at(std::span<const std::byte> data, std::size_t offset,
                const PluginFormIdContext& context) {
  const auto raw = RawFormId{u32_at(data, offset)};
  if (raw.is_null()) return FormKey{};
  return resolve_plugin_form_id(raw, context);
}

}  // namespace

std::vector<WeaponDamageType> decode_weapon_damage_types(const Plugin& plugin, const FormKey& key) {
  const auto* record = find_record(plugin, key);
  if (record == nullptr || record->header.signature != k_weap) {
    throw WeaponError("FormKey does not resolve to a WEAP record in this plugin: " + key.to_string());
  }

  const Subrecord* dama = nullptr;
  for (const auto& sub : record->subrecords) {
    if (sub.signature != k_dama) continue;
    if (dama != nullptr) {
      throw WeaponError("WEAP has more than one DAMA subrecord: " + key.to_string());
    }
    dama = &sub;
  }

  std::vector<WeaponDamageType> out;
  if (dama == nullptr) return out;

  const bool curve = record->header.form_version >= k_weapon_damage_curve_form_version;
  const std::size_t width = curve ? k_curve_entry_size : k_entry_size;
  if (dama->data.size() % width != 0) {
    throw WeaponError("WEAP DAMA payload is not a whole number of entries: " + key.to_string());
  }

  const auto context = plugin_form_id_context(plugin);
  for (std::size_t offset = 0; offset < dama->data.size(); offset += width) {
    WeaponDamageType entry;
    entry.type = form_at(dama->data, offset, context);
    entry.amount = u32_at(dama->data, offset + 4);
    if (curve) entry.curve_table = form_at(dama->data, offset + 8, context);
    out.push_back(entry);
  }
  return out;
}

}  // namespace fo4::forms
