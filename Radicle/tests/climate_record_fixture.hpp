#pragma once

#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/preflight.hpp"
#include "plugin_bytes.hpp"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <initializer_list>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::test::sky {

using Bytes = std::vector<std::byte>;
using Sub = std::pair<std::string, Bytes>;
using Order = std::vector<std::string>;
struct Entry { std::string signature; std::uint32_t id; std::vector<Sub> subs; std::uint16_t version; };

inline constexpr const char* k_plugin = "Sky.esp";
inline constexpr std::uint32_t k_clmt = 0x800, k_wthr = 0x801, k_wthr_old = 0x802, k_clmt_bare = 0x803;
inline constexpr std::uint32_t k_glob = 0x810, k_sndr = 0x811, k_stat = 0x812, k_lens = 0x813, k_spgd = 0x814, k_imgs = 0x815;
inline constexpr std::uint32_t k_spel = 0x816, k_misc = 0x817, k_rfct = 0x818, k_gdry = 0x819, k_soun = 0x81A;
inline constexpr std::uint32_t k_local = 0x01000000;

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

inline Bytes filled(std::size_t size, std::uint8_t value) { return Bytes(size, std::byte{value}); }

inline Bytes hex(std::string_view text) {
  Bytes out;
  for (std::size_t i = 0; i + 1 < text.size(); i += 2) {
    out.push_back(std::byte{static_cast<std::uint8_t>(std::stoi(std::string(text.substr(i, 2)), nullptr, 16))});
  }
  return out;
}

inline Bytes build(const std::vector<Entry>& entries, bool master) {
  ByteWriter out;
  const auto tes4 = begin_record(out, "TES4", 0, 0);
  ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x900);
  write_subrecord(out, "HEDR", hedr.bytes);
  if (master) {
    write_subrecord(out, "MAST", std::string_view("Master.esm\0", 11));
    write_subrecord(out, "DATA", words({0, 0}));
  }
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

inline std::vector<Sub> weather_subrecords(bool modern, std::uint32_t local) {
  std::vector<Sub> subs{{"EDID", zstr(modern ? "Modern" : "Legacy")}};
  if (modern) { subs.push_back({"00TX", zstr("sky\\a.dds")}); }
  subs.push_back({"LNAM", words({16})});
  subs.push_back({"MNAM", words({0})});
  subs.push_back({"NNAM", words({0})});
  subs.push_back({"RNAM", filled(32, 127)});
  subs.push_back({"QNAM", filled(32, 127)});
  subs.push_back({"PNAM", filled(modern ? 1024 : 512, 0)});
  subs.push_back({"JNAM", filled(modern ? 1024 : 512, 0)});
  subs.push_back({"NAM0", filled(modern ? 608 : 272, 0)});
  if (modern) { subs.push_back({"NAM4", filled(128, 0)}); }
  subs.push_back({"FNAM", filled(modern ? 72 : 32, 0)});
  subs.push_back({"DATA", filled(modern ? 20 : 19, 0)});
  subs.push_back({"NAM1", words({0xFFFFFFFFu})});
  if (modern) {
    subs.push_back({"SNAM", words({local | k_sndr, 1})});
    subs.push_back({"TNAM", words({local | k_stat})});
    subs.push_back({"TNAM", words({0})});
  }
  subs.push_back({"IMSP", filled(modern ? 32 : 16, 0)});
  if (modern) { subs.push_back({"WGDR", filled(32, 0)}); }
  if (modern) {
    for (int i = 0; i < 8; ++i) { subs.push_back({"DALC", filled(32, 0)}); }
    subs.push_back({"GNAM", words({local | k_lens})});
  } else {
    for (const std::size_t size : {24u, 28u, 32u, 32u}) { subs.push_back({"DALC", filled(size, 0)}); }
  }
  subs.push_back({"UNAM", filled(modern ? 24 : 12, 0)});
  if (modern) {
    subs.push_back({"VNAM", f32s({1.0f})});
    subs.push_back({"WNAM", f32s({1.0f})});
  }
  return subs;
}

inline std::vector<Entry> fixture(bool master_present = true) {
  const std::uint32_t local = master_present ? k_local : 0u;
  const Sub edid{"EDID", zstr("Sky")};
  std::vector<Entry> out{
      {"CLMT", local | k_clmt,
       {{"EDID", zstr("Clim")}, {"WLST", words({local | k_wthr, 60, 0, local | k_wthr_old, 40, local | k_glob})}, {"FNAM", zstr("sun.dds")},
        {"GNAM", zstr("glare.dds")}, {"MODL", zstr("m.nif")}, {"MODT", filled(12, 3)}, {"TNAM", hex("1e36667e0044")}}, 131},
      {"CLMT", local | k_clmt_bare, {{"EDID", zstr("Bare")}, {"TNAM", hex("1e36667e0044")}}, 131},
      {"WTHR", local | k_wthr, weather_subrecords(true, local), 131},
      {"WTHR", local | k_wthr_old, weather_subrecords(false, local), 40},
  };
  const std::pair<const char*, std::uint32_t> others[] = {{"GLOB", k_glob}, {"SNDR", k_sndr}, {"STAT", k_stat}, {"LENS", k_lens},
                                                          {"SPGD", k_spgd}, {"IMGS", k_imgs}, {"SPEL", k_spel}, {"MISC", k_misc},
                                                          {"RFCT", k_rfct}, {"GDRY", k_gdry}, {"SOUN", k_soun}};
  for (const auto& [signature, id] : others) { out.push_back({signature, local | id, {edid}, 131}); }
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

inline std::vector<Bytes> payloads(const fo4::Plugin& plugin, const fo4::FormKey& record_key, const char* name) {
  std::vector<Bytes> out;
  const auto* record = fo4::find_record(plugin, record_key);
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
