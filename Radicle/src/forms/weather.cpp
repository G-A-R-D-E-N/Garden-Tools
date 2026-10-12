#include "fo4/forms/weather.hpp"

#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/signature.hpp"
#include "../plugin/record_edit.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <string_view>

namespace fo4::forms {
namespace {

constexpr auto k_wthr = Signature::from_string("WTHR");
constexpr auto k_spgd = Signature::from_string("SPGD");
constexpr auto k_lnam = Signature::from_string("LNAM");
constexpr auto k_mnam = Signature::from_string("MNAM");
constexpr auto k_nnam = Signature::from_string("NNAM");
constexpr auto k_rnam = Signature::from_string("RNAM");
constexpr auto k_qnam = Signature::from_string("QNAM");
constexpr auto k_pnam = Signature::from_string("PNAM");
constexpr auto k_jnam = Signature::from_string("JNAM");
constexpr auto k_nam0 = Signature::from_string("NAM0");
constexpr auto k_nam1 = Signature::from_string("NAM1");
constexpr auto k_nam4 = Signature::from_string("NAM4");
constexpr auto k_fnam = Signature::from_string("FNAM");
constexpr auto k_imsp = Signature::from_string("IMSP");
constexpr auto k_wgdr = Signature::from_string("WGDR");
constexpr auto k_gnam = Signature::from_string("GNAM");
constexpr auto k_tnam = Signature::from_string("TNAM");
constexpr auto k_unam = Signature::from_string("UNAM");
constexpr auto k_vnam = Signature::from_string("VNAM");
constexpr auto k_wnam = Signature::from_string("WNAM");
constexpr auto k_modl = Signature::from_string("MODL");
constexpr auto k_modt = Signature::from_string("MODT");
constexpr auto k_dalc = Signature::from_string("DALC");
constexpr std::uint16_t k_weather_legacy_cloud_form_version = 35;
constexpr std::uint32_t k_cloud_tex_base = Signature::from_string("00TX").packed();

using fo4::detail::find_mutable_record;

void mark_modified(Plugin& plugin, Record& record) {
  record.original_range.clear();
  record.decompressed_payload.clear();
  record.header.data_size = 0;
  record.uncompressed_size = 0;
  plugin.original_range.clear();
}

std::uint32_t read_u32(const std::vector<std::byte>& data, std::size_t offset) {
  if (offset + 4 > data.size()) {
    throw WeatherError("WTHR payload truncated while reading u32");
  }
  std::uint32_t value = 0;
  std::memcpy(&value, data.data() + offset, 4);
  return value;
}

float read_f32(const std::vector<std::byte>& data, std::size_t offset) {
  if (offset + 4 > data.size()) {
    throw WeatherError("WTHR payload truncated while reading f32");
  }
  float value = 0.0f;
  std::memcpy(&value, data.data() + offset, 4);
  return value;
}

std::optional<std::string> decode_zstring(const std::vector<std::byte>& data) {
  if (data.empty() || data.back() != std::byte{0}) return std::nullopt;
  if (std::find(data.begin(), data.end() - 1, std::byte{0}) != data.end() - 1) {
    return std::nullopt;
  }
  const auto* begin = reinterpret_cast<const char*>(data.data());
  return std::string(begin, begin + data.size() - 1);
}

std::optional<FormKey> decode_optional_form(const PluginFormIdContext& context,
                                            const std::vector<std::byte>& data,
                                            std::size_t offset,
                                            std::string_view label) {
  const auto raw = RawFormId{read_u32(data, offset)};
  if (raw.is_null()) return std::nullopt;
  try {
    return resolve_plugin_form_id(raw, context);
  } catch (const FormIdError& error) {
    throw WeatherError(std::string(label) + " FormID resolve failed: " + error.what());
  }
}

const Subrecord* unique_required(const Record& record, Signature signature, std::string_view name) {
  const Subrecord* found = nullptr;
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature != signature) continue;
    if (found != nullptr) {
      throw WeatherError("WTHR contains duplicate " + std::string(name));
    }
    found = &subrecord;
  }
  if (found == nullptr) {
    throw WeatherError("WTHR is missing required " + std::string(name));
  }
  return found;
}

const Subrecord* unique_optional(const Record& record, Signature signature, std::string_view name) {
  const Subrecord* found = nullptr;
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature != signature) continue;
    if (found != nullptr) {
      throw WeatherError("WTHR contains duplicate " + std::string(name));
    }
    found = &subrecord;
  }
  return found;
}

void decode_speed_bytes(std::array<std::uint8_t, k_weather_cloud_layer_slots>& out,
                        const Subrecord& sub,
                        std::string_view name) {
  if (sub.data.size() != k_weather_cloud_layer_slots) {
    throw WeatherError(std::string(name) + " must be exactly 32 bytes");
  }
  for (std::size_t i = 0; i < k_weather_cloud_layer_slots; ++i) {
    out[i] = std::to_integer<std::uint8_t>(sub.data[i]);
  }
}

void decode_cloud_colors(Weather& result, const Subrecord& sub) {
  const bool legacy = result.form_version < k_weather_legacy_cloud_form_version;
  const std::size_t layers = legacy ? 4u : k_weather_cloud_layer_slots;
  if (sub.data.size() != layers * weather_time_slots(result.form_version) * 4) {
    throw WeatherError("PNAM size does not match its form version");
  }
  result.cloud_color_layer_count = static_cast<std::uint32_t>(layers);
  result.cloud_colors.resize(sub.data.size() / 4);
  for (std::size_t i = 0; i < result.cloud_colors.size(); ++i) {
    result.cloud_colors[i] = read_u32(sub.data, i * 4);
  }
}

void decode_cloud_alphas(Weather& result, const Subrecord& sub) {
  if (sub.data.size() != k_weather_cloud_layer_slots * weather_time_slots(result.form_version) * 4) {
    throw WeatherError("JNAM size does not match its form version");
  }
  if (sub.data.size() / k_weather_cloud_layer_slots != result.cloud_colors.size() * 4 / result.cloud_color_layer_count) {
    throw WeatherError("JNAM times per layer do not match PNAM");
  }
  result.cloud_alphas.resize(sub.data.size() / 4);
  for (std::size_t i = 0; i < result.cloud_alphas.size(); ++i) {
    result.cloud_alphas[i] = read_f32(sub.data, i * 4);
  }
}

bool has_master(const Plugin& plugin, const PluginKey& owner) {
  for (const auto& master : plugin.header.masters) {
    if (master.key == owner) return true;
  }
  return false;
}

PluginKind resolve_master_kind(const PluginKey& owner,
                               std::span<const Plugin* const> environment) {
  for (const auto* candidate : environment) {
    if (candidate == nullptr || candidate->key != owner) continue;
    return classify_plugin_kind(candidate->header.tes4_flags, candidate->key);
  }
  return owner.looks_light() ? PluginKind::light : PluginKind::full;
}

const Record* find_environment_record(const Plugin& plugin,
                                      std::span<const Plugin* const> environment,
                                      const FormKey& key) {
  if (const auto* local = find_record(plugin, key)) return local;
  for (const auto* candidate : environment) {
    if (candidate == nullptr) continue;
    if (const auto* found = find_record(*candidate, key)) return found;
  }
  return nullptr;
}

void require_spgd_target(const Plugin& plugin,
                         std::span<const Plugin* const> environment,
                         const FormKey& target) {
  const auto* record = find_environment_record(plugin, environment, target);
  if (record == nullptr) {
    throw WeatherError("MNAM target could not be resolved for SPGD validation");
  }
  if (record->header.signature != k_spgd) {
    throw WeatherError("MNAM target must be SPGD");
  }
}


Subrecord* unique_mnam_slot(Record& record) {
  Subrecord* slot = nullptr;
  for (auto& subrecord : record.subrecords) {
    if (subrecord.signature != k_mnam) continue;
    if (slot != nullptr) throw WeatherError("WTHR contains duplicate MNAM");
    slot = &subrecord;
  }
  if (slot == nullptr) throw WeatherError("WTHR is missing required MNAM");
  if (slot->data.size() != 4) throw WeatherError("MNAM must be exactly 4 bytes");
  return slot;
}

Record& require_wthr_record(Plugin& plugin, const FormKey& key) {
  auto* record = find_mutable_record(plugin, key);
  if (record == nullptr) throw WeatherError("WTHR record not found: " + key.to_string());
  if (record->header.signature != k_wthr) {
    throw WeatherError("record is not WTHR: " + key.to_string());
  }
  return *record;
}

std::optional<FormKey> decode_mnam_value(const Plugin& plugin, const Subrecord& slot) {
  const auto raw = RawFormId{read_u32(slot.data, 0)};
  if (raw.is_null()) return std::nullopt;
  try {
    return resolve_plugin_form_id(raw, plugin_form_id_context(plugin));
  } catch (const FormIdError& error) {
    throw WeatherError(std::string("MNAM FormID resolve failed: ") + error.what());
  }
}

}

Weather decode_weather(const Plugin& plugin, const FormKey& key) {
  const auto* record = fo4::find_record(plugin, key);
  if (record == nullptr) throw WeatherError("WTHR record not found: " + key.to_string());
  if (record->header.signature != k_wthr) {
    throw WeatherError("record is not WTHR: " + key.to_string());
  }

  Weather result;
  result.key = key;
  result.record_flags = record->header.flags;
  result.form_version = record->header.form_version;
  const auto context = plugin_form_id_context(plugin);

  for (const auto& subrecord : record->subrecords) {
    const auto packed = subrecord.signature.packed();
    if (packed >= k_cloud_tex_base && packed < k_cloud_tex_base + k_weather_cloud_layer_slots) {
      const auto layer = static_cast<std::size_t>(packed - k_cloud_tex_base);
      const auto text = decode_zstring(subrecord.data);
      if (!text.has_value()) {
        throw WeatherError("cloud texture subrecord is not a NUL-terminated path");
      }
      result.cloud_textures[layer] = *text;
    }
  }

  if (const auto* edid = unique_optional(*record, sig_edid, "EDID")) {
    result.editor_id = decode_zstring(edid->data);
    if (!result.editor_id.has_value()) {
      throw WeatherError("EDID is not a NUL-terminated string");
    }
  }

  result.lnam = unique_required(*record, k_lnam, "LNAM")->data;
  if (result.lnam.size() != 4) throw WeatherError("LNAM must be exactly 4 bytes");

  const auto* mnam = unique_required(*record, k_mnam, "MNAM");
  if (mnam->data.size() != 4) throw WeatherError("MNAM must be exactly 4 bytes");
  result.mnam = decode_optional_form(context, mnam->data, 0, "MNAM");

  const auto* nnam = unique_required(*record, k_nnam, "NNAM");
  if (nnam->data.size() != 4) throw WeatherError("NNAM must be exactly 4 bytes");
  result.nnam = decode_optional_form(context, nnam->data, 0, "NNAM");

  decode_speed_bytes(result.cloud_speed_y, *unique_required(*record, k_rnam, "RNAM"), "RNAM");
  decode_speed_bytes(result.cloud_speed_x, *unique_required(*record, k_qnam, "QNAM"), "QNAM");
  decode_cloud_colors(result, *unique_required(*record, k_pnam, "PNAM"));
  decode_cloud_alphas(result, *unique_required(*record, k_jnam, "JNAM"));

  result.nam0 = unique_required(*record, k_nam0, "NAM0")->data;
  if (result.nam0.size() != weather_nam0_size(result.form_version)) {
    throw WeatherError("NAM0 size does not match its form version");
  }
  if (const auto* nam4 = unique_optional(*record, k_nam4, "NAM4")) {
    if (nam4->data.size() != 128) throw WeatherError("NAM4 must be exactly 128 bytes when present");
    result.nam4 = nam4->data;
  }
  result.fnam = unique_required(*record, k_fnam, "FNAM")->data;
  if (result.fnam.size() != weather_fnam_size(result.form_version)) {
    throw WeatherError("FNAM size does not match its form version");
  }
  result.data = unique_required(*record, sig_data, "DATA")->data;
  if (result.data.size() != weather_data_size(result.form_version)) {
    throw WeatherError("DATA size does not match its form version");
  }

  const auto* nam1 = unique_required(*record, k_nam1, "NAM1");
  if (nam1->data.size() != 4) throw WeatherError("NAM1 must be exactly 4 bytes");
  result.nam1_bits = read_u32(nam1->data, 0);

  for (const auto& subrecord : record->subrecords) {
    if (subrecord.signature != sig_snam) continue;
    if (subrecord.data.size() != 8) throw WeatherError("SNAM must be exactly 8 bytes");
    WeatherSound sound;
    sound.sound = decode_optional_form(context, subrecord.data, 0, "SNAM");
    sound.type = read_u32(subrecord.data, 4);
    result.snam.push_back(std::move(sound));
  }

  for (const auto& subrecord : record->subrecords) {
    if (subrecord.signature != k_tnam) continue;
    if (subrecord.data.size() != 4) throw WeatherError("TNAM must be exactly 4 bytes");
    result.tnam.push_back(decode_optional_form(context, subrecord.data, 0, "TNAM"));
  }

  const auto* imsp = unique_required(*record, k_imsp, "IMSP");
  if (imsp->data.size() != weather_imsp_size(result.form_version)) {
    throw WeatherError("IMSP size does not match its form version");
  }
  result.imsp_count = static_cast<std::uint8_t>(imsp->data.size() / 4);
  for (std::uint8_t i = 0; i < result.imsp_count; ++i) {
    result.imsp[i] = decode_optional_form(context, imsp->data, i * 4u, "IMSP");
  }

  if (const auto* wgdr = unique_optional(*record, k_wgdr, "WGDR")) {
    if (wgdr->data.size() != 32) throw WeatherError("WGDR must be exactly 32 bytes");
    result.has_wgdr = true;
    for (std::size_t i = 0; i < k_weather_god_ray_slots; ++i) {
      result.wgdr[i] = decode_optional_form(context, wgdr->data, i * 4, "WGDR");
    }
  }

  for (const auto& subrecord : record->subrecords) {
    if (subrecord.signature != k_dalc) continue;
    const auto& data = subrecord.data;
    if (data.size() != 24 && data.size() != 28 && data.size() != 32) {
      throw WeatherError("DALC must be 24, 28, or 32 bytes");
    }
    WeatherAmbientColors ambient;
    for (std::size_t i = 0; i < ambient.directional.size(); ++i) {
      ambient.directional[i] = read_u32(data, i * 4);
    }
    if (data.size() >= 28) ambient.specular = read_u32(data, 24);
    if (data.size() == 32) ambient.fresnel = read_f32(data, 28);
    result.dalc.push_back(ambient);
  }
  if (result.dalc.size() != weather_dalc_count(result.form_version)) {
    throw WeatherError("WTHR DALC count does not match its form version");
  }

  if (const auto* gnam = unique_optional(*record, k_gnam, "GNAM")) {
    if (gnam->data.size() != 4) throw WeatherError("GNAM must be exactly 4 bytes");
    result.gnam = decode_optional_form(context, gnam->data, 0, "GNAM");
  }

  if (const auto* modl = unique_optional(*record, k_modl, "MODL")) {
    result.modl = decode_zstring(modl->data);
    if (!result.modl.has_value()) throw WeatherError("MODL is not a NUL-terminated path");
  }
  if (const auto* modt = unique_optional(*record, k_modt, "MODT")) {
    result.modt = modt->data;
  }

  if (const auto* unam = unique_optional(*record, k_unam, "UNAM")) {
    if (unam->data.size() != weather_unam_size(result.form_version)) {
      throw WeatherError("UNAM size does not match its form version");
    }
    result.has_unam = true;
    result.unam[0].spell = decode_optional_form(context, unam->data, 0, "UNAM+00");
    result.unam[0].threshold = read_f32(unam->data, 4);
    result.unam[1].spell = decode_optional_form(context, unam->data, 8, "UNAM+08");
    if (unam->data.size() == 24) {
      result.unam[1].threshold = read_f32(unam->data, 12);
    }
  }

  if (const auto* vnam = unique_optional(*record, k_vnam, "VNAM")) {
    if (vnam->data.size() != 4) throw WeatherError("VNAM must be exactly 4 bytes");
    result.vnam = read_f32(vnam->data, 0);
  }
  if (const auto* wnam = unique_optional(*record, k_wnam, "WNAM")) {
    if (wnam->data.size() != 4) throw WeatherError("WNAM must be exactly 4 bytes");
    result.wnam = read_f32(wnam->data, 0);
  }

  return result;
}

bool set_weather_mnam(Plugin& plugin,
                      const FormKey& key,
                      const std::optional<FormKey>& mnam,
                      std::span<const Plugin* const> environment) {
  auto next = plugin;

  auto* record = &require_wthr_record(next, key);
  auto* slot = unique_mnam_slot(*record);

  if (mnam.has_value()) {
    require_spgd_target(next, environment, *mnam);
  }

  if (decode_mnam_value(next, *slot) == mnam) {
    return false;
  }

  if (mnam.has_value()) {
    const auto& owner = mnam->owner();
    if (!owner.empty() && owner != next.key && !has_master(next, owner)) {
      std::vector<MasterDescriptor> masters;
      masters.reserve(next.header.masters.size() + 1);
      for (const auto& master : next.header.masters) {
        masters.push_back({master.key, master.kind});
      }
      masters.push_back({owner, resolve_master_kind(owner, environment)});
      next = remap_plugin_masters(std::move(next), std::move(masters));
      record = &require_wthr_record(next, key);
      slot = unique_mnam_slot(*record);
    }
  }

  const auto context = plugin_form_id_context(next);
  RawFormId encoded{0};
  if (mnam.has_value()) {
    try {
      encoded = encode_plugin_form_id(*mnam, context);
    } catch (const FormIdError& error) {
      throw WeatherError(std::string("MNAM encode failed: ") + error.what());
    }
  }
  std::array<std::byte, 4> bytes{};
  const auto value = encoded.value();
  std::memcpy(bytes.data(), &value, 4);
  slot->data.assign(bytes.begin(), bytes.end());
  mark_modified(next, *record);
  plugin = std::move(next);
  return true;
}

}
