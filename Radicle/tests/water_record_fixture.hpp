#pragma once

#include "fo4/environment/base_records.hpp"
#include "fo4/environment/environment.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/preflight.hpp"
#include "plugin_bytes.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <initializer_list>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::test::water {

using Bytes = std::vector<std::byte>;
using Sub = std::pair<std::string, Bytes>;
using Order = std::vector<std::string>;
struct Entry { std::string signature; std::uint32_t id; std::vector<Sub> subs; std::uint16_t version; };

inline constexpr const char* k_plugin = "Water.esp";
inline constexpr std::uint32_t k_full = 0x800, k_short = 0x801, k_bare = 0x802;
inline constexpr std::uint32_t k_matt = 0x810, k_sndr = 0x811, k_spel = 0x812, k_imgs = 0x813, k_misc = 0x814;
inline constexpr std::uint32_t k_tes4_localized = 0x80;

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

inline void put_f32(Bytes& data, std::size_t offset, float value) {
  ByteWriter out;
  out.f32(value);
  std::copy(out.bytes.begin(), out.bytes.end(), data.begin() + static_cast<std::ptrdiff_t>(offset));
}

inline Bytes visual(std::size_t size) {
  Bytes out(size, std::byte{0});
  for (std::size_t offset = 0; offset + 4 <= std::min<std::size_t>(size, 200); offset += 4) {
    put_f32(out, offset, static_cast<float>(offset) + 0.5f);
  }
  for (const std::size_t color : {4u, 8u, 36u, 96u, 192u, 196u}) {
    if (color + 4 > size) { continue; }
    out[color] = std::byte{0x10};
    out[color + 1] = std::byte{0x20};
    out[color + 2] = std::byte{0x30};
    out[color + 3] = std::byte{0xFF};
  }
  if (size > 200) { out[200] = std::byte{1}; }
  return out;
}

inline Bytes build(const std::vector<Entry>& entries, std::uint32_t tes4_flags = 0) {
  ByteWriter out;
  std::vector<std::string> tops;
  for (const auto& entry : entries) {
    if (std::find(tops.begin(), tops.end(), entry.signature) == tops.end()) { tops.push_back(entry.signature); }
  }
  const auto tes4 = begin_record(out, "TES4", tes4_flags, 0);
  ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(static_cast<std::uint32_t>(tops.size() + entries.size()));
  hedr.u32(0x900);
  write_subrecord(out, "HEDR", hedr.bytes);
  end_record(out, tes4);
  for (const auto& top : tops) {
    const auto group = begin_group(out, top, 0);
    for (const auto& entry : entries) {
      if (entry.signature != top) { continue; }
      const auto record = begin_record(out, entry.signature, 0, entry.id, entry.version);
      for (const auto& [name, data] : entry.subs) { write_subrecord(out, name, data); }
      end_record(out, record);
    }
    end_group(out, group);
  }
  return out.bytes;
}

inline std::vector<Sub> full_water() {
  return {{"EDID", zstr("Lake")}, {"FULL", zstr("Lake water")}, {"ANAM", hex("50")}, {"FNAM", hex("01")},
          {"TNAM", words({k_matt})}, {"SNAM", words({k_sndr})}, {"XNAM", words({k_spel})}, {"YNAM", words({k_spel})},
          {"INAM", words({k_imgs})}, {"DATA", {}}, {"DNAM", visual(201)}, {"GNAM", filled(12, 0xAB)},
          {"NAM0", f32s({1.0f, 2.0f, 3.0f})}, {"NAM1", f32s({4.0f, 5.0f, 6.0f})}, {"NAM2", zstr("water\\n1.dds")},
          {"NAM3", zstr("water\\n2.dds")}, {"NAM4", zstr("water\\n3.dds")}};
}

inline std::vector<Sub> short_water() {
  return {{"EDID", zstr("Old")}, {"ANAM", hex("27")}, {"FNAM", hex("00")}, {"XNAM", words({k_spel})},
          {"INAM", words({k_imgs})}, {"DATA", {}}, {"DNAM", visual(188)}, {"GNAM", filled(12, 0)},
          {"NAM0", f32s({0.0f, 0.0f, 0.0f})}, {"NAM1", f32s({0.0f, 0.0f, 0.0f})}, {"NAM2", zstr("a.dds")},
          {"NAM3", zstr("b.dds")}, {"NAM4", zstr("c.dds")}};
}

inline std::vector<Entry> fixture() {
  std::vector<Entry> out{{"WATR", k_full, full_water(), 131},
                         {"WATR", k_short, short_water(), 122},
                         {"WATR", k_bare, {{"EDID", zstr("Bare")}, {"FNAM", hex("00")}, {"DNAM", visual(201)}}, 131}};
  const std::pair<const char*, std::uint32_t> others[] = {
      {"MATT", k_matt}, {"SNDR", k_sndr}, {"SPEL", k_spel}, {"IMGS", k_imgs}, {"MISC", k_misc}};
  for (const auto& [signature, id] : others) { out.push_back({signature, id, {{"EDID", zstr("Other")}}, 131}); }
  return out;
}

inline fo4::FormKey key(std::uint32_t local) {
  return fo4::FormKey{fo4::PluginKey::from_filename(k_plugin), fo4::LocalFormId{local & 0xFFFFFF}};
}
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

inline Bytes payload(const fo4::Plugin& plugin, std::uint32_t local, const char* name) {
  const auto* record = fo4::find_record(plugin, key(local));
  if (record == nullptr) { return {}; }
  for (const auto& subrecord : record->subrecords) {
    if (subrecord.signature == sig(name)) { return subrecord.data; }
  }
  return {};
}

inline std::map<fo4::FormKey, Bytes> snapshot(const fo4::Plugin& plugin) {
  std::map<fo4::FormKey, Bytes> out;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    if (record.header.signature == fo4::Signature::from_string("TES4")) { return; }
    out[record.form_key] = fo4::test::record_original_bytes(plugin, record);
  });
  return out;
}

class TempDir {
public:
  TempDir() : path_(std::filesystem::temp_directory_path() /
                    ("radicle_water_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {
    std::filesystem::create_directories(path_);
  }
  ~TempDir() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;
  const std::filesystem::path& path() const { return path_; }

private:
  std::filesystem::path path_;
};

template <typename Check>
void with_saved_water(const fo4::Plugin& plugin, std::uint32_t id, Check check) {
  const TempDir dir;
  const auto file = dir.path() / k_plugin;
  fo4::write_plugin_file(plugin, file);
  const std::filesystem::path paths[] = {file};
  const auto env = fo4::env::Environment::load(paths);
  check(fo4::env::decode_water(env, *env.winner(key(id))));
}

}
