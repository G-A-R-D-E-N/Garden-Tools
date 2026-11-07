#pragma once

#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::test::vegetation {

using Bytes = std::vector<std::byte>;
using Sub = std::pair<std::string, Bytes>;
struct Entry { std::string signature; std::uint32_t id; std::vector<Sub> subs; };

constexpr const char* k_plugin = "Vegetation.esp";
constexpr std::uint32_t k_gras = 0x800, k_tree = 0x801, k_tree_bare = 0x802, k_flor = 0x803, k_ltex = 0x804, k_ltex_bare = 0x805;
constexpr std::uint32_t k_gras_no_data = 0x806, k_sndr = 0x810, k_misc = 0x811, k_stat = 0x812;
constexpr std::uint32_t k_master_local = 0x01000000, k_master_ingredient = 0x900, k_master_sound = 0x901;

inline Bytes words(std::initializer_list<std::uint32_t> values) {
  fo4::test::ByteWriter out;
  for (const auto value : values) { out.u32(value); }
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

inline Bytes build(const std::vector<Entry>& entries, bool master, bool localized = false) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", localized ? 0x80u : 0u, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  if (master) {
    fo4::test::write_subrecord(out, "MAST", std::string_view("Master.esm\0", 11));
    fo4::test::write_subrecord(out, "DATA", words({0, 0}));
  }
  fo4::test::end_record(out, tes4);
  std::vector<std::string> done;
  for (const auto& top : entries) {
    if (std::find(done.begin(), done.end(), top.signature) != done.end()) { continue; }
    done.push_back(top.signature);
    const auto group = fo4::test::begin_group(out, top.signature, 0);
    for (const auto& entry : entries) {
      if (entry.signature != top.signature) { continue; }
      const auto record = fo4::test::begin_record(out, entry.signature, 0, entry.id);
      for (const auto& [name, data] : entry.subs) { fo4::test::write_subrecord(out, name, data); }
      fo4::test::end_record(out, record);
    }
    fo4::test::end_group(out, group);
  }
  return out.bytes;
}

inline Bytes gras_data() { return hex("1e005a00000000000000000000000042cdcc4c3e0000003f0000204100000000"); }
inline Bytes tree_cnam() { return hex("0000803f0000803f00000000000000000000000000000000000000000000000000000000000000000000803f0000803f"); }

inline std::vector<Entry> fixture(bool master_present = true) {
  const std::uint32_t k_local = master_present ? 0x01000000u : 0u;
  const Sub edid{"EDID", zstr("Veg")};
  const Sub obnd{"OBND", filled(12, 0)};
  const Sub modl{"MODL", zstr("m.nif")};
  const Sub modt{"MODT", filled(8, 3)};
  const Sub gnam_local{"GNAM", words({k_local | k_gras})};
  return {
      {"GRAS", k_local | k_gras, {edid, obnd, modl, modt, {"DATA", gras_data()}}},
      {"GRAS", k_local | k_gras_no_data, {edid, obnd, modl}},
      {"TREE", k_local | k_tree, {edid, obnd, modl, modt, {"PFPC", {std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}}}, {"CNAM", tree_cnam()}}},
      {"TREE", k_local | k_tree_bare, {edid, obnd, modl, {"CNAM", tree_cnam()}}},
      {"FLOR", k_local | k_flor,
       {edid, obnd, {"FULL", words({7})}, modl, {"KSIZ", words({1})}, {"KWDA", words({k_master_ingredient})}, {"PNAM", filled(4, 255)},
        {"FNAM", filled(2, 0)}, {"PFIG", words({k_master_ingredient})}, {"SNAM", words({k_master_sound})},
        {"PFPC", {std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}}}}},
      {"LTEX", k_local | k_ltex, {edid, {"TNAM", words({0})}, {"MNAM", words({0})}, {"HNAM", hex("1e1e")}, {"SNAM", hex("1e")}, gnam_local, {"GNAM", words({k_master_ingredient})}}},
      {"LTEX", k_local | k_ltex_bare, {edid, {"MNAM", words({0})}, {"HNAM", hex("1e1e")}, {"SNAM", hex("1e")}}},
      {"SNDR", k_local | k_sndr, {edid}},
      {"MISC", k_local | k_misc, {edid}},
      {"STAT", k_local | k_stat, {edid}},
  };
}

inline fo4::FormKey key(std::uint32_t local) { return fo4::FormKey{fo4::PluginKey::from_filename(k_plugin), fo4::LocalFormId{local & 0xFFFFFF}}; }
inline fo4::FormKey master(std::uint32_t local) { return fo4::FormKey{fo4::PluginKey::from_filename("Master.esm"), fo4::LocalFormId{local}}; }
inline fo4::Signature sig(const char* text) { return fo4::Signature::from_string(text); }

inline fo4::Plugin read(const Bytes& bytes) {
  auto plugin = fo4::read_plugin(bytes, k_plugin);
  fo4::assign_record_form_keys(plugin);
  return plugin;
}

inline fo4::Plugin reload(const fo4::Plugin& plugin) { return read(fo4::write_plugin(plugin)); }

inline std::vector<std::string> order(const fo4::Plugin& plugin, std::uint32_t local) {
  std::vector<std::string> out;
  const auto* record = fo4::find_record(plugin, key(local));
  if (record == nullptr) { return out; }
  for (const auto& subrecord : record->subrecords) { out.push_back(subrecord.signature.to_string()); }
  return out;
}

using Order = std::vector<std::string>;

inline std::vector<Bytes> payloads(const fo4::Plugin& plugin, const fo4::FormKey& record_key, const char* name) {
  std::vector<Bytes> out;
  const auto* record = fo4::find_record(plugin, record_key);
  if (record == nullptr) { return out; }
  for (const auto& subrecord : record->subrecords) {
    if (subrecord.signature == sig(name)) { out.push_back(subrecord.data); }
  }
  return out;
}

}
