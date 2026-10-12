#pragma once

#include "fo4/environment/environment.hpp"
#include "fo4/environment/sound.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/subrecord_authoring.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/preflight.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <initializer_list>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::test::sound {

using Bytes = std::vector<std::byte>;
using Sub = std::pair<std::string, Bytes>;
using Order = std::vector<std::string>;
struct Entry { std::string signature; std::uint32_t id; std::vector<Sub> subs; std::uint16_t version; };

inline constexpr const char* k_plugin = "Snd.esp";
inline constexpr std::uint32_t k_std = 0x800, k_compound = 0x801, k_auto = 0x802, k_nokind = 0x803, k_bare = 0x804;
inline constexpr std::uint32_t k_marker = 0x805, k_marker_old = 0x806, k_music = 0x807, k_step = 0x808, k_set = 0x809;
inline constexpr std::uint32_t k_snct = 0x810, k_sopm = 0x811, k_must = 0x812, k_ipds = 0x813, k_misc = 0x814, k_step2 = 0x815;
inline constexpr std::uint32_t k_standard = 0x1EEF540A, k_compound_kind = 0x54651A43, k_auto_kind = 0xED157AE3;

inline Bytes words(std::initializer_list<std::uint32_t> values) {
  ByteWriter out;
  for (const auto value : values) { out.u32(value); }
  return out.bytes;
}

inline Bytes f32s(std::initializer_list<float> values) {
  ByteWriter out;
  for (const auto value : values) { out.f32(value); }
  return out.bytes;
}

inline Bytes zstr(std::string_view text) {
  Bytes out;
  for (const char ch : text) { out.push_back(std::byte{static_cast<std::uint8_t>(ch)}); }
  out.push_back(std::byte{0});
  return out;
}

inline Bytes raw(std::initializer_list<std::uint8_t> values) {
  Bytes out;
  for (const auto value : values) { out.push_back(std::byte{value}); }
  return out;
}

inline Bytes filled(std::size_t size, std::uint8_t value) { return Bytes(size, std::byte{value}); }

inline Bytes build(const std::vector<Entry>& entries) {
  ByteWriter out;
  const auto tes4 = begin_record(out, "TES4", 0, 0);
  ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x900);
  write_subrecord(out, "HEDR", hedr.bytes);
  end_record(out, tes4);
  std::vector<std::string> done;
  for (const auto& top : entries) {
    if (std::find(done.begin(), done.end(), top.signature) != done.end()) { continue; }
    done.push_back(top.signature);
    const auto group = begin_group(out, top.signature, 0);
    for (const auto& entry : entries) {
      if (entry.signature != top.signature) { continue; }
      const auto record = begin_record(out, entry.signature, 0, entry.id, entry.version);
      for (const auto& [name, data] : entry.subs) { write_subrecord(out, name, data); }
      end_record(out, record);
    }
    end_group(out, group);
  }
  return out.bytes;
}

inline std::vector<Entry> fixture() {
  const Sub edid{"EDID", zstr("Other")};
  std::vector<Entry> out{
      {"SNDR", k_std,
       {{"EDID", zstr("Std")}, {"CNAM", words({k_standard})}, {"GNAM", words({k_snct})}, {"SNAM", words({k_compound})},
        {"ANAM", zstr("a.wav")}, {"ANAM", zstr("b.wav")}, {"ONAM", words({k_sopm})}, {"CTDA", filled(32, 0)},
        {"LNAM", raw({1, 8, 2, 3})}, {"BNAM", raw({0xFE, 3, 50, 4, 0x10, 0x27})}}, 131},
      {"SNDR", k_compound, {{"EDID", zstr("Comp")}, {"CNAM", words({k_compound_kind})}, {"DNAM", words({k_std})}, {"DNAM", words({k_nokind})}}, 131},
      {"SNDR", k_auto,
       {{"EDID", zstr("Auto")}, {"CNAM", words({k_auto_kind})}, {"BNAM", words({k_std})}, {"ITMC", words({2})},
        {"ITMS", {}}, {"INTV", words({600})}, {"FNAM", zstr("r1.wav")}, {"ITME", {}},
        {"ITMS", {}}, {"INTV", words({900})}, {"FNAM", zstr("r2.wav")}, {"ITME", {}}}, 131},
      {"SNDR", k_nokind, {{"EDID", zstr("NoKind")}, {"LNAM", raw({0, 0, 0, 0})}, {"BNAM", raw({0, 0, 0, 0, 0, 0})}}, 131},
      {"SNDR", k_bare, {{"EDID", zstr("Bare")}}, 131},
      {"SOUN", k_marker, {{"EDID", zstr("Marker")}, {"OBND", filled(12, 0)}, {"SDSC", words({k_std})}, {"REPT", raw({0, 0, 0x20, 0x40, 0, 0, 0x80, 0x40, 1})}}, 131},
      {"SOUN", k_marker_old, {{"EDID", zstr("Old")}, {"OBND", filled(12, 0)}, {"SDSC", words({k_auto})}, {"REPT", f32s({1.0f, 2.0f})}}, 0x70},
      {"MUSC", k_music, {{"EDID", zstr("Music")}, {"FNAM", words({0x41})}, {"PNAM", raw({50, 0, 0x2C, 0x01})}, {"WNAM", f32s({3.0f})}, {"TNAM", words({k_must, 0})}}, 131},
      {"FSTP", k_step, {{"EDID", zstr("Step")}, {"DATA", words({k_ipds})}, {"ANAM", zstr("Left")}}, 131},
      {"FSTS", k_set, {{"EDID", zstr("Set")}, {"XCNT", words({1, 2, 0, 1, 1})}, {"DATA", words({k_step, k_step, k_step, k_step2, k_step})}}, 131},
  };
  const std::pair<const char*, std::uint32_t> others[] = {{"SNCT", k_snct}, {"SOPM", k_sopm}, {"MUST", k_must}, {"IPDS", k_ipds}, {"MISC", k_misc}};
  for (const auto& [signature, id] : others) { out.push_back({signature, id, {edid}, 131}); }
  out.push_back({"FSTP", k_step2, {{"EDID", zstr("Step2")}, {"DATA", words({0})}, {"ANAM", zstr("Right")}}, 131});
  return out;
}

inline fo4::FormKey key(std::uint32_t local) { return fo4::FormKey{fo4::PluginKey::from_filename(k_plugin), fo4::LocalFormId{local & 0xFFFFFF}}; }
inline fo4::Signature sig(const char* text) { return fo4::Signature::from_string(text); }

inline fo4::Plugin read(const Bytes& bytes) {
  auto plugin = fo4::read_plugin(bytes, k_plugin);
  fo4::assign_record_form_keys(plugin);
  return plugin;
}

inline fo4::Plugin reload(const fo4::Plugin& plugin) { return read(fo4::write_plugin(plugin)); }

inline Order order(const fo4::Plugin& plugin, std::uint32_t local) {
  Order out;
  const auto* record = fo4::find_record(plugin, key(local));
  if (record == nullptr) { return out; }
  for (const auto& subrecord : record->subrecords) { out.push_back(subrecord.signature.to_string()); }
  return out;
}

inline std::vector<Bytes> payloads(const fo4::Plugin& plugin, std::uint32_t local, const char* name) {
  std::vector<Bytes> out;
  const auto* record = fo4::find_record(plugin, key(local));
  if (record == nullptr) { return out; }
  for (const auto& subrecord : record->subrecords) {
    if (subrecord.signature == sig(name)) { out.push_back(subrecord.data); }
  }
  return out;
}

inline std::map<fo4::FormKey, Bytes> snapshot(const fo4::Plugin& plugin) {
  std::map<fo4::FormKey, Bytes> out;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    if (record.header.signature == fo4::Signature::from_string("TES4")) { return; }
    out[record.form_key] = fo4::test::record_original_bytes(plugin, record);
  });
  return out;
}

}
