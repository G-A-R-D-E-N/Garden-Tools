#pragma once

#include "fo4/environment/dialogue.hpp"
#include "fo4/environment/environment.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/scene_writer.hpp"
#include "fo4/plugin/subrecord_authoring.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/ctda.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/preflight.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::test::dialogue {

using Bytes = std::vector<std::byte>;
using Sub = std::pair<std::string, Bytes>;
using Order = std::vector<std::string>;

inline constexpr const char* k_plugin = "Dlg.esp";
inline constexpr std::uint32_t k_quest = 0x800, k_topic = 0x801, k_info = 0x802, k_scene = 0x803, k_group = 0x804;
inline constexpr std::uint32_t k_template = 0x805, k_bare = 0x806;
inline constexpr std::uint32_t k_dlbr = 0x820, k_kywd = 0x821, k_sndr = 0x822, k_npc = 0x823, k_sopm = 0x824;
inline constexpr std::uint32_t k_glob = 0x825, k_pack = 0x826, k_idle = 0x827, k_misc = 0x828;

inline Bytes words(std::initializer_list<std::uint32_t> values) {
  ByteWriter out;
  for (const auto value : values) { out.u32(value); }
  return out.bytes;
}

inline Bytes halves(std::initializer_list<std::uint16_t> values) {
  ByteWriter out;
  for (const auto value : values) { out.u16(value); }
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

inline Bytes response(std::uint32_t emotion, std::uint8_t number, std::uint32_t sound) {
  ByteWriter out;
  out.u32(emotion);
  out.u8(number);
  out.u32(sound);
  out.u8(0);
  out.u16(50);
  out.i32(-1);
  out.i32(-1);
  return out.bytes;
}

inline Bytes condition() {
  fo4::CtdaCondition c;
  c.function = 14;
  return fo4::serialize_ctda_condition(c);
}

inline Bytes text_or_id(bool localized, std::string_view text, std::uint32_t id) {
  return localized ? words({id}) : zstr(text);
}

inline void record(ByteWriter& out, const char* signature, std::uint32_t id, const std::vector<Sub>& subs) {
  const auto at = begin_record(out, signature, 0, id);
  for (const auto& [name, data] : subs) { write_subrecord(out, name, data); }
  end_record(out, at);
}

inline std::size_t child_group(ByteWriter& out, std::uint32_t parent, std::int32_t type) {
  const auto at = begin_group(out, "QUST", type);
  out.patch_u32(at + 8, parent);
  return at;
}

inline std::vector<Sub> topic(bool localized) {
  return {{"EDID", zstr("Topic")}, {"FULL", text_or_id(localized, "Hello", 7)}, {"PNAM", f32s({50.0f})},
          {"BNAM", words({k_dlbr})}, {"QNAM", words({k_quest})}, {"KNAM", words({k_kywd})},
          {"DATA", words({0x000E0201})}, {"SNAM", words({0x4E454353})}, {"TIFC", words({2})}};
}

inline std::vector<Sub> info(bool localized) {
  return {{"ENAM", halves({1, 0x200})}, {"PNAM", words({0})},
          {"TRDA", response(k_kywd, 1, 0)}, {"NAM1", text_or_id(localized, "Line one", 8)}, {"NAM2", zstr("")},
          {"NAM3", zstr("")}, {"NAM4", zstr("")}, {"NAM9", Bytes(8, std::byte{0x11})},
          {"TRDA", response(0xFFFFFFFF, 2, k_sndr)}, {"NAM1", text_or_id(localized, "Line two", 9)},
          {"NAM2", zstr("note")}, {"NAM3", zstr("")}, {"NAM4", zstr("")}, {"TNAM", halves({30})},
          {"CTDA", condition()}, {"RNAM", text_or_id(localized, "Prompt", 10)}, {"ANAM", words({k_npc})},
          {"TSCE", words({k_scene})}, {"ALFA", words({5})}, {"ONAM", words({k_sopm})}, {"GREE", words({150})},
          {"TIQS", halves({0xFFFF, 10})}, {"NAM0", zstr("Start")}, {"INCC", words({2})},
          {"MODQ", words({k_glob})}, {"INAM", words({1})}};
}

inline std::vector<Sub> scene() {
  return {{"EDID", zstr("Scene")}, {"FNAM", words({0x24})},
          {"HNAM", {}}, {"NAM0", zstr("Start")}, {"NEXT", {}}, {"NEXT", {}}, {"WNAM", words({350})},
          {"FNAM", halves({1})}, {"SCQS", halves({0xFFFF, 10})}, {"HNAM", {}},
          {"HNAM", {}}, {"NAM0", zstr("")}, {"CTDA", condition()}, {"NEXT", {}}, {"NEXT", {}}, {"WNAM", words({350})},
          {"HNAM", {}},
          {"ALID", words({0})}, {"LNAM", words({2})}, {"DNAM", words({26})},
          {"ALID", words({1})}, {"LNAM", words({0})}, {"DNAM", words({26})},
          {"ANAM", halves({0})}, {"NAM0", zstr("")}, {"ALID", words({0})}, {"INAM", words({1})},
          {"FNAM", words({0x8000})}, {"SNAM", words({0})}, {"ENAM", words({1})}, {"DATA", words({k_topic})},
          {"DMAX", f32s({1.0f})}, {"PNAM", words({k_kywd})}, {"CRIS", f32s({1.0f, 2.0f})}, {"ANAM", {}},
          {"ANAM", halves({2})}, {"NAM0", zstr("")}, {"ALID", words({0xFFFFFFFF})}, {"INAM", words({2})},
          {"SNAM", words({1})}, {"SNAM", f32s({5.0f})}, {"TNAM", f32s({1.0f})}, {"ANAM", {}},
          {"ANAM", halves({1})}, {"NAM0", zstr("")}, {"ALID", words({1})}, {"INAM", words({3})},
          {"SNAM", words({0})}, {"ENAM", words({1})}, {"PNAM", words({k_pack})}, {"ANAM", {}},
          {"PNAM", words({k_quest})}, {"INAM", words({3})}, {"VNAM", words({3, 3, 3, 3})},
          {"CRIS", f32s({60.0f})}, {"KSIZ", words({1})}, {"KWDA", words({k_kywd})}, {"CTDA", condition()},
          {"SCQS", halves({0xFFFF, 0xFFFF})}, {"NNAM", zstr("note")}, {"TNAM", words({k_template})},
          {"XNAM", words({0})}};
}

inline Bytes build(bool localized = false) {
  ByteWriter out;
  const auto tes4 = begin_record(out, "TES4", localized ? 0x80u : 0u, 0);
  ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x900);
  write_subrecord(out, "HEDR", hedr.bytes);
  end_record(out, tes4);
  const auto quests = begin_group(out, "QUST", 0);
  record(out, "QUST", k_quest, {{"EDID", zstr("Quest")}});
  const auto children = child_group(out, k_quest, 10);
  record(out, "DIAL", k_topic, topic(localized));
  const auto infos = child_group(out, k_topic, 7);
  record(out, "INFO", k_info, info(localized));
  record(out, "INFO", k_group, {{"ENAM", halves({0x40, 0})}, {"INAM", words({1})}});
  end_group(out, infos);
  record(out, "SCEN", k_scene, scene());
  record(out, "SCEN", k_template, {{"EDID", zstr("Template")}, {"PNAM", words({k_quest})}, {"VNAM", words({0, 0, 0, 0})}});
  record(out, "DIAL", k_bare, {{"EDID", zstr("Bare")}, {"PNAM", f32s({50.0f})}});
  end_group(out, children);
  end_group(out, quests);
  const std::pair<const char*, std::uint32_t> targets[] = {{"DLBR", k_dlbr}, {"KYWD", k_kywd}, {"SNDR", k_sndr},
                                                           {"NPC_", k_npc}, {"SOPM", k_sopm}, {"GLOB", k_glob},
                                                           {"PACK", k_pack}, {"IDLE", k_idle}, {"MISC", k_misc}};
  for (const auto& [signature, id] : targets) {
    const auto group = begin_group(out, signature, 0);
    record(out, signature, id, {{"EDID", zstr(signature)}});
    end_group(out, group);
  }
  return out.bytes;
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

inline std::vector<Bytes> payloads(const fo4::Plugin& plugin, std::uint32_t local, const char* name) {
  std::vector<Bytes> out;
  const auto* record = fo4::find_record(plugin, key(local));
  if (record == nullptr) { return out; }
  for (const auto& subrecord : record->subrecords) {
    if (subrecord.signature == sig(name)) { out.push_back(subrecord.data); }
  }
  return out;
}

inline fo4::ScalarMutationStatus scalar(fo4::Plugin& plugin, std::uint32_t id, const char* path, std::size_t index,
                                        const char* value) {
  const auto field = fo4::schema::scalar_field_descriptor(path);
  EXPECT_TRUE(field.has_value()) << path;
  return fo4::set_record_scalar(plugin, key(id), field.value(), index, value);
}

inline fo4::ScalarMutationStatus text(fo4::Plugin& plugin, std::uint32_t id, const char* path, std::size_t index,
                                      const char* value) {
  return fo4::set_record_string(plugin, key(id), path, index, value);
}

inline fo4::FormLinkFieldMutationStatus link(fo4::Plugin& plugin, std::uint32_t id, const char* path, std::size_t index,
                                             const fo4::FormKey& target) {
  const auto field = fo4::schema::formlink_field_descriptor(path);
  EXPECT_TRUE(field.has_value()) << path;
  return fo4::set_record_formlink_field(plugin, key(id), field.value(), index, target);
}

inline std::size_t count(const fo4::Plugin& plugin, std::uint32_t id, const char* path) {
  return fo4::record_field_count(plugin, key(id), path).value_or(9999);
}

template <typename Edit>
bool refused(fo4::Plugin& plugin, Edit&& edit) {
  const auto before = fo4::write_plugin(plugin);
  bool threw = false;
  try {
    edit();
  } catch (const std::exception&) {
    threw = true;
  }
  return threw && fo4::write_plugin(plugin) == before;
}

inline bool authoring_clean(const fo4::Plugin& plugin, std::uint32_t id) {
  for (const auto& issue : fo4::schema::preflight_authoring_record(plugin, key(id))) {
    if (issue.severity == fo4::schema::PreflightSeverity::error) { return false; }
  }
  return true;
}

struct TempRoot {
  std::filesystem::path path{std::filesystem::temp_directory_path() /
                             ("radicle_dialogue_writer_" +
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))};
  TempRoot() = default;
  TempRoot(const TempRoot&) = delete;
  TempRoot& operator=(const TempRoot&) = delete;
  ~TempRoot() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
};

class SavedEnvironment {
public:
  explicit SavedEnvironment(const fo4::Plugin& plugin) : env_(load(plugin, root_.path)) {}
  const fo4::env::Environment& env() const { return env_; }
  const fo4::env::RecordRef& record(std::uint32_t id) const { return *env_.winner(key(id)); }

private:
  static fo4::env::Environment load(const fo4::Plugin& plugin, const std::filesystem::path& root) {
    std::filesystem::create_directories(root);
    fo4::write_plugin_file(plugin, root / k_plugin);
    const std::filesystem::path paths[] = {root / k_plugin};
    return fo4::env::Environment::load(paths);
  }
  TempRoot root_;
  fo4::env::Environment env_;
};

}
