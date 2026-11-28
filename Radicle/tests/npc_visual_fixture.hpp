#pragma once

#include "fo4/c_api_env.h"
#include "fo4/c_api_npc_visual.h"
#include "fo4/environment/environment.hpp"
#include "fo4/forms/npc_visual.hpp"
#include "fo4/plugin/reader.hpp"
#include "plugin_bytes.hpp"
#include "byte_helpers.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace npc_visual_fixture {

using Bytes = std::vector<std::byte>;
using Subs = std::vector<std::pair<std::string, Bytes>>;
using fo4::forms::NpcVisual;
using fo4::forms::NpcVisualError;
using fo4::forms::NpcVisualPart;
using fo4::forms::NpcVisualSource;

inline constexpr std::uint32_t k_human = 0x800;
inline constexpr std::uint32_t k_bot = 0x801;
inline constexpr std::uint32_t k_skin = 0x810;
inline constexpr std::uint32_t k_raider_armor = 0x830;
inline constexpr std::uint32_t k_gloves_armor = 0x833;
inline constexpr std::uint32_t k_empty_armor = 0x836;
inline constexpr std::uint32_t k_leveled_item = 0x837;
inline constexpr std::uint32_t k_outfit_raider = 0x840;
inline constexpr std::uint32_t k_outfit_mixed = 0x841;
inline constexpr std::uint32_t k_lvln = 0x870;
inline constexpr std::uint32_t k_bot_skin = 0x880;
inline constexpr std::uint32_t k_bot_outfit = 0x885;
inline constexpr std::uint32_t k_extra_outfit = 0x892;
inline constexpr std::uint32_t k_lvli_armor = 0x838;
inline constexpr std::uint32_t k_lvli_silent = 0x839;
inline constexpr std::uint32_t k_outfit_leveled = 0x842;
inline constexpr std::uint32_t k_outfit_silent = 0x843;
inline constexpr std::uint32_t k_lvln_template = 0x871;
inline constexpr std::uint32_t k_lvln_self = 0x87A;
inline constexpr std::uint32_t k_lvln_tier = 0x87B;
inline constexpr std::uint32_t k_lvln_use_all = 0x8C1;
inline constexpr std::uint32_t k_lvln_top_rolled = 0x8C2;
inline constexpr std::uint32_t k_lvli_self_all = 0x8C3;
inline constexpr std::uint32_t k_lvli_missing = 0x8C4;
inline constexpr std::uint32_t k_lvli_wrong = 0x8C5;
inline constexpr std::uint32_t k_lvln_cycle_a = 0x8C6;
inline constexpr std::uint32_t k_lvln_cycle_b = 0x8C7;
inline constexpr std::uint32_t k_outfit_wrong = 0x8C8;
inline constexpr std::uint32_t k_npc_wrong_outfit = 0x8C9;
inline constexpr std::uint32_t k_lvlg = 0x8CA;
inline constexpr std::uint32_t k_glob = 0x8D0;
inline constexpr std::uint32_t k_npc_chain_a = 0x8D1;
inline constexpr std::uint32_t k_npc_chain_b = 0x8D2;
inline constexpr std::uint32_t k_npc_chain_c = 0x8D3;
inline constexpr std::uint32_t k_npc_inventory_obts = 0x8D4;
inline constexpr std::uint32_t k_npc_inventory_host = 0x8D5;
inline constexpr std::uint32_t k_npc_truncated = 0x8D6;
inline constexpr std::uint32_t k_omod_cycle_a = 0x8D7;
inline constexpr std::uint32_t k_omod_cycle_b = 0x8D8;
inline constexpr std::uint32_t k_npc_omod_cycle = 0x8D9;
inline constexpr std::uint32_t k_npc_non_omod = 0x8DA;
inline constexpr std::uint32_t k_npc_human_obts = 0x8DC;
inline constexpr std::uint32_t k_npc_robot_skin = 0x8DD;
inline constexpr std::uint32_t k_npc_robot_outfit = 0x8DE;
inline constexpr std::uint32_t k_omod_bad_include = 0x8DF;
inline constexpr std::uint32_t k_npc_omod_bad_include = 0x8E0;
inline constexpr std::uint32_t k_face_bot = 0x8E1;
inline constexpr std::uint32_t k_npc_face_only = 0x8E2;
inline constexpr std::uint32_t k_lvln_use_all_tiers = 0x8E3;
inline constexpr std::uint32_t k_lvln_twin_rows = 0x8E4;
inline constexpr std::uint32_t k_npc_low = 0x875;
inline constexpr std::uint32_t k_npc_high = 0x876;
inline constexpr std::uint32_t k_lvli_all = 0x8A5;
inline constexpr std::uint32_t k_outfit_all = 0x8A6;
inline constexpr std::uint32_t k_npc_all = 0x8A7;
inline constexpr std::uint32_t k_omod_torso = 0x8B0;
inline constexpr std::uint32_t k_omod_arm = 0x8B1;
inline constexpr std::uint32_t k_omod_head = 0x8B3;
inline constexpr std::uint32_t k_npc_robot_template = 0x8B5;
inline constexpr std::uint32_t k_npc_robot_own = 0x8B6;
inline constexpr std::uint32_t k_npc_robot_inherit = 0x8B7;
inline constexpr std::uint32_t k_npc_robot_rich = 0x8B8;
inline constexpr std::uint32_t k_npc_robot = 0x8B2;
inline constexpr std::uint32_t k_npc_robot_bad = 0x8B4;
inline constexpr std::uint32_t k_npc_robot_dressed = 0x8B9;
inline constexpr std::uint32_t k_npc_robot_badform = 0x8BA;
inline constexpr std::uint32_t k_npc_robot_combos = 0x8BB;
inline constexpr std::uint32_t k_lvln_nested_none = 0x8E7;
inline constexpr std::uint32_t k_npc_nested_none = 0x8E8;
inline constexpr std::uint32_t k_outfit_lvlg = 0x8E9;
inline constexpr std::uint32_t k_npc_lvlg = 0x8EA;
inline constexpr std::string_view k_robot_skeleton = "meshes/actors/robot/characterassets/skeleton.hkx";

inline constexpr std::string_view k_human_skeleton = "meshes/actors/character/characterassets/skeleton.hkx";
inline constexpr std::string_view k_human_idle = "meshes/actors/character/animations/h2h/idle.hkx";

struct Rec {
  std::string signature;
  std::uint32_t id{0};
  Subs subs;
  std::uint32_t flags{0};
  std::uint16_t form_version{131};
};

using fo4::test::zstr;

inline Bytes words(std::initializer_list<std::uint32_t> values) {
  fo4::test::ByteWriter out;
  for (const auto value : values) out.u32(value);
  return out.bytes;
}

inline Bytes floats(std::initializer_list<float> values) {
  fo4::test::ByteWriter out;
  for (const auto value : values) out.f32(value);
  return out.bytes;
}

inline Bytes acbs(std::uint32_t flags, std::uint16_t template_flags) {
  fo4::test::ByteWriter out;
  out.u32(flags);
  for (int i = 0; i < 5; ++i) out.u16(0);
  out.u16(template_flags);
  out.u16(0);
  out.u16(0);
  return out.bytes;
}

inline Bytes race_data(std::uint32_t flags) {
  fo4::test::ByteWriter out;
  for (int i = 0; i < 8; ++i) out.u32(0);
  out.u32(flags);
  while (out.size() < 200) out.u8(std::uint8_t{0});
  return out.bytes;
}

inline Rec race(std::uint32_t id,
         std::string_view edid,
         std::uint32_t flags,
         std::string_view male_skeleton,
         std::string_view female_skeleton,
         std::uint32_t skin = 0,
         std::uint32_t armor_race = 0) {
  Rec rec{"RACE", id, {{"EDID", zstr(edid)}, {"DATA", race_data(flags)}}};
  if (skin != 0) rec.subs.push_back({"WNAM", words({skin})});
  if (armor_race != 0) rec.subs.push_back({"RNAM", words({armor_race})});
  rec.subs.push_back({"MNAM", {}});
  rec.subs.push_back({"ANAM", zstr(male_skeleton)});
  rec.subs.push_back({"FNAM", {}});
  rec.subs.push_back({"ANAM", zstr(female_skeleton)});
  rec.subs.push_back({"NAM1", {}});
  rec.subs.push_back({"MNAM", {}});
  rec.subs.push_back({"INDX", words({0})});
  rec.subs.push_back({"MODL", zstr("Actors\\Character\\BodyParts\\MaleHead.nif")});
  rec.subs.push_back({"FNAM", {}});
  rec.subs.push_back({"INDX", words({0})});
  rec.subs.push_back({"MODL", zstr("Actors\\Character\\BodyParts\\FemaleHead.nif")});
  return rec;
}

inline Rec arma(std::uint32_t id,
         std::uint32_t race_id,
         std::uint32_t slots,
         std::string_view male,
         std::string_view female,
         std::initializer_list<std::uint32_t> extra_races = {}) {
  Rec rec{"ARMA", id, {{"EDID", zstr("Addon")}, {"BOD2", words({slots})}, {"RNAM", words({race_id})}}};
  for (const auto extra : extra_races) rec.subs.push_back({"MODL", words({extra})});
  if (!male.empty()) rec.subs.push_back({"MOD2", zstr(male)});
  if (!female.empty()) rec.subs.push_back({"MOD3", zstr(female)});
  return rec;
}

inline Rec armo(std::uint32_t id, std::uint32_t slots, std::initializer_list<std::uint32_t> addons) {
  Rec rec{"ARMO", id, {{"EDID", zstr("Armor")}, {"BOD2", words({slots})}}};
  std::uint16_t index = 0;
  for (const auto addon : addons) {
    fo4::test::ByteWriter indx;
    indx.u16(index++);
    rec.subs.push_back({"INDX", indx.bytes});
    rec.subs.push_back({"MODL", words({addon})});
  }
  return rec;
}

inline Rec otft(std::uint32_t id, std::initializer_list<std::uint32_t> items) {
  return Rec{"OTFT", id, {{"EDID", zstr("Outfit")}, {"INAM", words(items)}}};
}

// Constraint: an OMOD DATA header is 20 bytes, then slot and item counts, then 7-byte includes.
inline Rec omod(std::uint32_t id, std::string_view model, std::initializer_list<std::uint32_t> includes = {}) {
  fo4::test::ByteWriter data;
  data.u32(static_cast<std::uint32_t>(includes.size()));
  data.u32(0);
  for (int i = 0; i < 12; ++i) data.u8(std::uint8_t{0});
  data.u32(0);
  data.u32(0);
  for (const auto include : includes) {
    data.u32(include);
    data.u8(std::uint8_t{0});
    data.u8(std::uint8_t{0});
    data.u8(std::uint8_t{0});
  }
  Rec rec{"OMOD", id, {{"EDID", zstr("Omod")}, {"DATA", data.bytes}}};
  if (!model.empty()) rec.subs.push_back({"MODL", zstr(model)});
  return rec;
}

inline Bytes obts_payload(const std::vector<std::uint32_t>& includes,
                   const std::vector<std::uint32_t>& keywords = {},
                   std::uint32_t properties = 0,
                   std::array<std::uint8_t, 7> header = {}) {
  fo4::test::ByteWriter out;
  out.u32(static_cast<std::uint32_t>(includes.size()));
  out.u32(properties);
  for (int i = 0; i < 7; ++i) out.u8(static_cast<std::uint8_t>(header[i]));
  out.u8(static_cast<std::uint8_t>(keywords.size()));
  for (const auto keyword : keywords) out.u32(keyword);
  out.u8(std::uint8_t{0});
  out.u8(std::uint8_t{0});
  for (const auto include : includes) {
    out.u32(include);
    out.u8(std::uint8_t{0});
    out.u8(std::uint8_t{0});
    out.u8(std::uint8_t{0});
  }
  for (std::uint32_t row = 0; row < properties; ++row) {
    for (int i = 0; i < 24; ++i) out.u8(std::uint8_t{0});
  }
  return out.bytes;
}

inline Rec leveled(std::string signature,
            std::uint32_t id,
            std::uint8_t flags,
            std::uint8_t chance_none,
            std::initializer_list<std::tuple<std::uint16_t, std::uint32_t, std::uint8_t>> entries,
            std::uint32_t use_global = 0) {
  Rec rec{std::move(signature),
          id,
          {{"EDID", zstr("Leveled")},
           {"LVLD", {std::byte{chance_none}}},
           {"LVLM", {std::byte{0}}},
           {"LVLF", {std::byte{flags}}}}};
  if (use_global != 0) rec.subs.push_back({"LVLG", words({use_global})});
  fo4::test::ByteWriter count;
  count.u8(static_cast<std::uint8_t>(entries.size()));
  rec.subs.push_back({"LLCT", count.bytes});
  for (const auto& [entry_level, item, entry_chance] : entries) {
    fo4::test::ByteWriter lvlo;
    lvlo.u16(entry_level);
    lvlo.u16(0);
    lvlo.u32(item);
    lvlo.u16(1);
    lvlo.u8(entry_chance);
    lvlo.u8(0);
    rec.subs.push_back({"LVLO", lvlo.bytes});
  }
  return rec;
}

struct NpcSpec {
  std::uint32_t id{0};
  std::uint32_t flags{0};
  std::uint16_t template_flags{0};
  std::uint32_t race{k_human};
  std::uint32_t tplt{0};
  std::uint32_t skin{0};
  std::uint32_t outfit{0};
  std::optional<std::array<float, 4>> tint{};
  std::optional<std::uint32_t> tpta_traits{};
  std::vector<std::uint32_t> obts{};
  std::vector<std::uint32_t> obts_keywords{};
  std::uint32_t obts_properties{0};
  std::vector<Bytes> obts_rows{};
  std::optional<Bytes> obts_raw{};
};

inline Rec npc(const NpcSpec& spec) {
  Rec rec{"NPC_", spec.id, {{"EDID", zstr("Npc")}, {"ACBS", acbs(spec.flags, spec.template_flags)}}};
  if (spec.race != 0) rec.subs.push_back({"RNAM", words({spec.race})});
  if (spec.tplt != 0) rec.subs.push_back({"TPLT", words({spec.tplt})});
  if (spec.tpta_traits) {
    fo4::test::ByteWriter tpta;
    for (int i = 0; i < 13; ++i) tpta.u32(i == 0 ? *spec.tpta_traits : 0u);
    rec.subs.push_back({"TPTA", tpta.bytes});
  }
  if (spec.skin != 0) rec.subs.push_back({"WNAM", words({spec.skin})});
  if (spec.outfit != 0) rec.subs.push_back({"DOFT", words({spec.outfit})});
  if (spec.tint) rec.subs.push_back({"QNAM", floats({(*spec.tint)[0], (*spec.tint)[1], (*spec.tint)[2], (*spec.tint)[3]})});
  if (spec.obts_raw) {
    rec.subs.push_back({"OBTS", *spec.obts_raw});
  } else if (!spec.obts_rows.empty()) {
    for (const auto& row : spec.obts_rows) rec.subs.push_back({"OBTS", row});
  } else if (!spec.obts.empty()) {
    rec.subs.push_back({"OBTS", obts_payload(spec.obts, spec.obts_keywords, spec.obts_properties)});
  }
  return rec;
}

inline Bytes build(const std::vector<std::string>& masters, std::uint32_t flags, const std::vector<Rec>& records) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", flags, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  for (const auto& master : masters) {
    fo4::test::write_subrecord(out, "MAST", zstr(master));
    fo4::test::write_subrecord(out, "DATA", words({0, 0}));
  }
  fo4::test::end_record(out, tes4);
  std::vector<std::string> order;
  for (const auto& rec : records) {
    if (std::find(order.begin(), order.end(), rec.signature) == order.end()) order.push_back(rec.signature);
  }
  for (const auto& signature : order) {
    const auto group = fo4::test::begin_group(out, signature, 0);
    for (const auto& rec : records) {
      if (rec.signature != signature) continue;
      const auto header = fo4::test::begin_record(out, rec.signature, rec.flags, rec.id, rec.form_version);
      for (const auto& [name, data] : rec.subs) fo4::test::write_subrecord(out, name, data);
      fo4::test::end_record(out, header);
    }
    fo4::test::end_group(out, group);
  }
  return out.bytes;
}

inline std::vector<Rec> base_records() {
  return {
      race(k_human, "HumanRaceT", 0x2, "Actors\\Character\\CharacterAssets\\skeleton.nif",
           "Actors\\Character\\CharacterAssets\\skeleton.nif", k_skin),
      race(k_bot, "BotRace", 0x0, "Actors\\Robot\\CharacterAssets\\skeleton.nif",
           "Actors\\CreateABot\\CharacterAssets\\Skeleton.NIF"),
      armo(k_skin, 0x8, {0x820, 0x821, 0x822, 0x823}),
      arma(0x820, k_human, 0x8, "Actors\\Character\\CharacterAssets\\MaleBody.nif",
           "Actors\\Character\\CharacterAssets\\FemaleBody.nif"),
      arma(0x821, k_human, 0x30, "Actors\\Character\\CharacterAssets\\MaleHands.nif",
           "Actors\\Character\\CharacterAssets\\FemaleHands.nif"),
      arma(0x822, k_human, 0x4, "Actors\\Character\\CharacterAssets\\MaleHead.nif",
           "Actors\\Character\\CharacterAssets\\FemaleHead.nif"),
      arma(0x823, k_bot, 0x30, "Actors\\Robot\\CharacterAssets\\Hands.nif", ""),
      armo(k_raider_armor, 0x8, {0x831}),
      arma(0x831, k_human, 0x8, "Armor\\Raider\\RaiderMale.nif", "Armor\\Raider\\RaiderFemale.NIF"),
      armo(k_gloves_armor, 0x30, {0x834}),
      arma(0x834, k_human, 0x30, "Armor\\Raider\\GlovesMale.nif", ""),
      armo(k_empty_armor, 0x800, {0x835}),
      arma(0x835, k_human, 0x800, "", ""),
      Rec{"LVLI", k_leveled_item, {{"EDID", zstr("LeveledItem")}}},
      otft(k_outfit_raider, {k_raider_armor}),
      otft(k_outfit_mixed, {k_gloves_armor, k_empty_armor, k_leveled_item}),
      Rec{"LVLN", k_lvln, {{"EDID", zstr("LeveledNpc")}}},
      armo(k_bot_skin, 0x8, {0x881, 0x882}),
      arma(0x881, k_bot, 0x8, "Actors\\Robot\\Body.nif", ""),
      arma(0x882, k_bot, 0x4, "Actors\\Robot\\Head.nif", ""),
      armo(0x883, 0x40, {0x884}),
      arma(0x884, k_bot, 0x40, "ACTORS\\ROBOT\\body.NIF", ""),
      otft(k_bot_outfit, {0x883}),
      armo(0x890, 0x40, {0x891}),
      arma(0x891, k_bot, 0x40, "Meshes\\Armor\\Extra.nif", "", {k_human}),
      otft(k_extra_outfit, {0x890}),
      npc({.id = 0x850, .flags = 1, .outfit = k_outfit_raider, .tint = std::array<float, 4>{0.1f, 0.2f, 0.3f, 1.0f}}),
      npc({.id = 0x851}),
      npc({.id = 0x852, .flags = 1, .outfit = k_outfit_mixed}),
      npc({.id = 0x860, .outfit = k_outfit_raider, .tint = std::array<float, 4>{0.5f, 0.25f, 0.125f, 1.0f}}),
      npc({.id = 0x861, .flags = 1, .template_flags = 0x101, .race = k_bot, .tplt = 0x860,
           .outfit = k_outfit_mixed, .tint = std::array<float, 4>{9.0f, 9.0f, 9.0f, 9.0f}}),
      npc({.id = 0x862, .template_flags = 0x101, .tplt = k_lvln}),
      npc({.id = 0x863, .template_flags = 0x1, .tplt = 0x860, .tpta_traits = 0x865}),
      npc({.id = 0x864, .template_flags = 0x1, .tplt = 0x860, .outfit = k_outfit_mixed}),
      npc({.id = 0x865, .flags = 1, .tint = std::array<float, 4>{1.0f, 0.0f, 0.0f, 1.0f}}),
      npc({.id = 0x866, .template_flags = 0x1, .tplt = 0x867}),
      npc({.id = 0x867, .template_flags = 0x1, .tplt = 0x866}),
      npc({.id = 0x886, .race = k_bot, .skin = k_bot_skin, .outfit = k_bot_outfit}),
      npc({.id = 0x887, .flags = 1, .race = k_bot, .skin = k_bot_skin, .outfit = k_bot_outfit}),
      npc({.id = 0x893, .outfit = k_extra_outfit}),
      leveled("LVLI", k_lvli_armor, 0x01, 0,
              {{1, k_raider_armor, 0}, {30, k_gloves_armor, 0}}),
      leveled("LVLI", k_lvli_silent, 0x01, 100, {{1, k_raider_armor, 0}}),
      leveled("LVLN", k_lvln_template, 0x01, 0, {{1, k_npc_low, 0}, {20, k_npc_high, 0}}),
      leveled("LVLN", k_lvln_tier, 0x00, 0, {{1, k_npc_low, 0}, {20, k_npc_high, 0}}),
      leveled("LVLN", k_lvln_self, 0x01, 0, {{1, k_lvln_self, 0}}),
      leveled("LVLI", k_lvli_all, 0x04, 0, {{1, k_raider_armor, 0}, {5, k_gloves_armor, 0}}),
      leveled("LVLN", k_lvln_use_all, 0x04, 0, {{1, k_npc_low, 0}, {1, k_npc_high, 0}}),
      leveled("LVLN", k_lvln_use_all_tiers, 0x04, 0, {{1, k_npc_low, 0}, {20, k_npc_high, 0}}),
      leveled("LVLN", k_lvln_twin_rows, 0x01, 0, {{1, k_npc_low, 50}, {1, k_npc_low, 50}}),
      leveled("LVLN", k_lvln_top_rolled, 0x00, 0, {{1, k_npc_low, 0}, {20, k_npc_high, 100}}),
      leveled("LVLI", k_lvli_self_all, 0x04, 0,
              {{1, k_lvli_self_all, 0}, {1, k_lvli_self_all, 0}, {1, k_lvli_self_all, 0}}),
      leveled("LVLI", k_lvli_missing, 0x01, 0, {{1, 0x8CF, 0}}),
      leveled("LVLI", k_lvli_wrong, 0x01, 0, {{1, k_human, 0}}),
      leveled("LVLN", k_lvln_cycle_a, 0x01, 0, {{1, k_lvln_cycle_b, 0}}),
      leveled("LVLN", k_lvln_cycle_b, 0x01, 0, {{1, k_lvln_cycle_a, 0}}),
      leveled("LVLI", k_lvlg, 0x01, 0, {{1, k_raider_armor, 0}}, k_glob),
      Rec{"GLOB", k_glob, {{"EDID", zstr("ChanceGlobal")}, {"FLTV", floats({50.0f})}}},
      otft(k_outfit_wrong, {k_lvli_wrong}),
      npc({.id = k_npc_wrong_outfit, .outfit = k_outfit_wrong}),
      otft(k_outfit_leveled, {k_lvli_armor}),
      otft(k_outfit_all, {k_lvli_all}),
      otft(k_outfit_silent, {k_lvli_silent}),
      npc({.id = k_npc_low, .tint = std::array<float, 4>{0.25f, 0.0f, 0.0f, 1.0f}}),
      npc({.id = k_npc_high, .tint = std::array<float, 4>{0.75f, 0.0f, 0.0f, 1.0f}}),
      npc({.id = 0x877, .template_flags = 0x1, .tplt = k_lvln_template}),
      npc({.id = 0x878, .outfit = k_outfit_leveled}),
      npc({.id = 0x879, .outfit = k_outfit_silent}),
      npc({.id = 0x87C, .template_flags = 0x1, .tplt = k_lvln_tier}),
      npc({.id = k_npc_all, .outfit = k_outfit_all}),
      leveled("LVLN", 0x8E5, 0x01, 50, {{1, k_npc_low, 0}}),
      npc({.id = 0x8E6, .template_flags = 0x1, .tplt = 0x8E5}),
      leveled("LVLN", k_lvln_nested_none, 0x01, 0, {{1, 0x8E5, 0}}),
      npc({.id = k_npc_nested_none, .template_flags = 0x1, .tplt = k_lvln_nested_none}),
      otft(k_outfit_lvlg, {k_lvlg}),
      npc({.id = k_npc_lvlg, .outfit = k_outfit_lvlg}),
      omod(k_omod_torso, "actors\\robot\\parts\\torsohandy.nif"),
      omod(k_omod_arm, "Actors\\Robot\\Parts\\ArmHandy.nif", {k_omod_torso}),
      npc({.id = k_npc_robot, .race = k_bot, .obts = {k_omod_torso, k_omod_arm}}),
      npc({.id = k_npc_robot_bad, .race = k_bot, .obts = {0x8BF}}),
      omod(k_omod_head, "actors\\robot\\parts\\headhandy.nif"),
      npc({.id = k_npc_robot_template, .race = k_bot, .obts = {k_omod_torso}}),
      npc({.id = k_npc_robot_own,
           .template_flags = 0x1,
           .tplt = k_npc_robot_template,
           .obts = {k_omod_head}}),
      npc({.id = k_npc_robot_inherit, .template_flags = 0x1, .tplt = k_npc_robot_template}),
      npc({.id = k_npc_chain_a, .template_flags = 0x1, .race = k_bot, .tplt = k_npc_chain_b}),
      npc({.id = k_npc_chain_b,
           .template_flags = 0x1,
           .race = k_bot,
           .tplt = k_npc_chain_c,
           .obts = {k_omod_head}}),
      npc({.id = k_npc_chain_c, .race = k_bot, .obts = {k_omod_torso}}),
      npc({.id = k_npc_inventory_host, .race = k_bot, .obts = {k_omod_torso}}),
      npc({.id = k_npc_inventory_obts,
           .template_flags = 0x100,
           .race = k_bot,
           .tplt = k_npc_inventory_host}),
      npc({.id = k_npc_truncated,
           .race = k_bot,
           .obts_raw = Bytes{std::byte{2}, std::byte{0}, std::byte{0}, std::byte{0},
                             std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0},
                             std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0},
                             std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0},
                             std::byte{0}, std::byte{0},
                             std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0},
                             std::byte{0}, std::byte{0}, std::byte{0}}}),
      omod(k_omod_cycle_a, "actors\\robot\\parts\\cyclea.nif", {k_omod_cycle_b}),
      omod(k_omod_cycle_b, "actors\\robot\\parts\\cycleb.nif", {k_omod_cycle_a}),
      npc({.id = k_npc_omod_cycle, .race = k_bot, .obts = {k_omod_cycle_a}}),
      npc({.id = k_npc_non_omod, .race = k_bot, .obts = {k_raider_armor}}),
      npc({.id = k_npc_human_obts, .obts = {k_omod_torso}}),
      npc({.id = k_npc_robot_skin, .race = k_bot, .skin = k_bot_skin, .obts = {k_omod_torso}}),
      npc({.id = k_npc_robot_outfit, .race = k_bot, .outfit = k_bot_outfit, .obts = {k_omod_torso}}),
      omod(k_omod_bad_include, "actors\\robot\\parts\\badinclude.nif", {0x7F000801}),
      npc({.id = k_npc_omod_bad_include, .race = k_bot, .obts = {k_omod_bad_include}}),
      race(k_face_bot, "FaceBotRace", 0x2, "Actors\\Robot\\CharacterAssets\\skeleton.nif",
           "Actors\\Robot\\CharacterAssets\\skeleton.nif"),
      npc({.id = k_npc_face_only, .race = k_face_bot, .obts = {k_omod_torso}}),
      npc({.id = k_npc_robot_dressed,
           .race = k_bot,
           .skin = k_bot_skin,
           .outfit = k_bot_outfit,
           .obts = {k_omod_torso}}),
      npc({.id = k_npc_robot_badform, .race = k_bot, .obts = {0x7F000800}}),
      npc({.id = k_npc_robot_combos,
           .race = k_bot,
           .obts_rows = {obts_payload({k_omod_torso}, {}, 0, {0, 0, 0, 0, 1, 0, 0}),
                         obts_payload({k_omod_head}, {}, 0, {0, 0, 0, 0, 0xFF, 0xFF, 1})}}),
      npc({.id = k_npc_robot_rich,
           .race = k_bot,
           .obts = {k_omod_head},
           .obts_keywords = {0x8C0},
           .obts_properties = 1}),
  };
}

inline fo4::PluginKey base_key() { return fo4::PluginKey::from_filename("Base.esm"); }

inline fo4::FormKey key(std::uint32_t local) {
  return fo4::FormKey{base_key(), fo4::LocalFormId{local}};
}

inline fo4::env::Environment make_env(std::vector<std::pair<std::string, Bytes>> files) {
  std::vector<fo4::Plugin> plugins;
  for (const auto& [name, bytes] : files) plugins.push_back(fo4::read_plugin(bytes, name));
  return fo4::env::Environment::from_plugins(std::move(plugins));
}

inline fo4::env::Environment base_env() {
  return make_env({{"Base.esm", build({}, 0x1, base_records())}});
}

inline fo4::env::Environment chain_env(std::size_t lists) {
  std::vector<Rec> records;
  for (std::size_t index = 0; index < lists; ++index) {
    const auto id = static_cast<std::uint32_t>(0x900 + index);
    const std::uint32_t next =
        index + 1 < lists ? static_cast<std::uint32_t>(0x900 + index + 1) : 0x910u;
    records.push_back(leveled("LVLI", id, 0x01, 0, {{1, next, 0}}));
  }
  records.push_back(Rec{"ARMO", 0x910, {{"EDID", zstr("Armor")}, {"BOD2", words({0x8})}}});
  return make_env({{"Chain.esm", build({}, 0x1, records)}});
}

inline constexpr std::uint32_t k_ghoul = 0x8F0;
inline constexpr std::uint32_t k_npc_ghoul_suit = 0x8F5;
inline constexpr std::uint32_t k_npc_human_suit = 0x8F6;
inline constexpr std::uint32_t k_npc_ghoul_own = 0x8FA;
inline constexpr std::uint32_t k_npc_human_ghoul_own = 0x8FB;
inline constexpr std::uint32_t k_npc_human_unmatched = 0x8FE;
inline constexpr std::uint32_t k_npc_race_cycle = 0x9A5;
inline constexpr std::uint32_t k_npc_blocker = 0x9B3;
inline constexpr std::uint32_t k_npc_mixed_ghoul = 0x9B8;
inline constexpr std::uint32_t k_npc_mixed_human = 0x9B9;
inline constexpr std::uint32_t k_hop_first = 0x9C0;
inline constexpr std::uint32_t k_npc_hops = 0x9D4;
inline constexpr std::uint32_t k_race_bad_rnam = 0x9E0;
inline constexpr std::uint32_t k_npc_bad_rnam = 0x9E2;
inline constexpr std::uint32_t k_npc_self_rnam = 0x9E8;
inline constexpr std::uint32_t k_npc_dead_rnam = 0x9EE;

inline fo4::env::Environment armor_race_env() {
  constexpr auto skeleton = "Actors\\Character\\CharacterAssets\\skeleton.nif";
  auto records = base_records();
  records.push_back(race(k_ghoul, "GhoulRaceT", 0x2, skeleton, skeleton, 0, k_human));
  records.push_back(armo(0x8F1, 0x38, {0x8F2, 0x8F3}));
  records.push_back(arma(0x8F2, k_human, 0x8, "Armor\\Suit\\SuitM.nif", ""));
  records.push_back(arma(0x8F3, k_human, 0x30, "Armor\\Suit\\GlovesM.nif", "", {k_ghoul}));
  records.push_back(otft(0x8F4, {0x8F1}));
  records.push_back(npc({.id = k_npc_ghoul_suit, .race = k_ghoul, .outfit = 0x8F4}));
  records.push_back(npc({.id = k_npc_human_suit, .outfit = 0x8F4}));
  records.push_back(armo(0x8F7, 0x8, {0x8F8}));
  records.push_back(arma(0x8F8, k_ghoul, 0x8, "Armor\\Ghoul\\Body.nif", ""));
  records.push_back(otft(0x8F9, {0x8F7}));
  records.push_back(npc({.id = k_npc_ghoul_own, .race = k_ghoul, .outfit = 0x8F9}));
  records.push_back(npc({.id = k_npc_human_ghoul_own, .outfit = 0x8F9}));
  records.push_back(armo(0x8FC, 0x8, {0x8FD}));
  records.push_back(arma(0x8FD, k_bot, 0x8, "Armor\\Bot\\Body.nif", ""));
  records.push_back(otft(0x8FF, {0x8FC}));
  records.push_back(npc({.id = k_npc_human_unmatched, .outfit = 0x8FF}));
  records.push_back(race(0x9A0, "CycleRaceA", 0x0, skeleton, skeleton, 0, 0x9A1));
  records.push_back(race(0x9A1, "CycleRaceB", 0x0, skeleton, skeleton, 0, 0x9A0));
  records.push_back(armo(0x9A2, 0x8, {0x9A3}));
  records.push_back(arma(0x9A3, 0x9A1, 0x8, "Armor\\Cycle\\Body.nif", ""));
  records.push_back(otft(0x9A4, {0x9A2}));
  records.push_back(npc({.id = k_npc_race_cycle, .race = 0x9A0, .outfit = 0x9A4}));
  records.push_back(armo(0x9B0, 0x8, {0x9B1}));
  records.push_back(arma(0x9B1, k_human, 0x8, "", ""));
  records.push_back(otft(0x9B2, {0x9B0}));
  records.push_back(npc({.id = k_npc_blocker, .outfit = 0x9B2}));
  records.push_back(armo(0x9B4, 0x8, {0x9B5, 0x9B6}));
  records.push_back(arma(0x9B5, k_human, 0x8, "Armor\\Mixed\\Human.nif", ""));
  records.push_back(arma(0x9B6, k_ghoul, 0x8, "Armor\\Mixed\\Ghoul.nif", ""));
  records.push_back(otft(0x9B7, {0x9B4}));
  records.push_back(npc({.id = k_npc_mixed_ghoul, .race = k_ghoul, .outfit = 0x9B7}));
  records.push_back(npc({.id = k_npc_mixed_human, .outfit = 0x9B7}));
  for (std::uint32_t hop = 0; hop < 10; ++hop) {
    records.push_back(race(k_hop_first + hop, "HopRace", 0x0, skeleton, skeleton, 0,
                           hop + 1 < 10 ? k_hop_first + hop + 1 : 0));
  }
  records.push_back(armo(0x9D2, 0x18, {0x9D0, 0x9D1}));
  records.push_back(arma(0x9D0, k_hop_first + 8, 0x8, "Armor\\Hop\\Eight.nif", ""));
  records.push_back(arma(0x9D1, k_hop_first + 9, 0x10, "Armor\\Hop\\Nine.nif", ""));
  records.push_back(otft(0x9D3, {0x9D2}));
  records.push_back(npc({.id = k_npc_hops, .race = k_hop_first, .outfit = 0x9D3}));
  auto bad_rnam = race(k_race_bad_rnam, "BadRnamRace", 0x0, skeleton, skeleton);
  bad_rnam.subs.push_back({"RNAM", Bytes{std::byte{1}, std::byte{2}, std::byte{3}}});
  records.push_back(std::move(bad_rnam));
  records.push_back(npc({.id = k_npc_bad_rnam, .race = k_race_bad_rnam}));
  records.push_back(race(0x9E3, "SelfRace", 0x0, skeleton, skeleton, 0, 0x9E3));
  records.push_back(armo(0x9E5, 0x8, {0x9E6}));
  records.push_back(arma(0x9E6, 0x9E3, 0x8, "Armor\\Self\\Body.nif", ""));
  records.push_back(otft(0x9E7, {0x9E5}));
  records.push_back(npc({.id = k_npc_self_rnam, .race = 0x9E3, .outfit = 0x9E7}));
  records.push_back(race(0x9EA, "DeadRnamRace", 0x0, skeleton, skeleton, 0, 0x9EF));
  records.push_back(armo(0x9EB, 0x8, {0x9EC}));
  records.push_back(arma(0x9EC, 0x9EF, 0x8, "Armor\\Dead\\Body.nif", ""));
  records.push_back(otft(0x9ED, {0x9EB}));
  records.push_back(npc({.id = k_npc_dead_rnam, .race = 0x9EA, .outfit = 0x9ED}));
  return make_env({{"Base.esm", build({}, 0x1, records)}});
}

inline fo4::FormKey chain_key(std::uint32_t local) {
  return fo4::FormKey{fo4::PluginKey::from_filename("Chain.esm"), fo4::LocalFormId{local}};
}

inline fo4::env::Environment omod_chain_env(std::size_t length) {
  std::vector<Rec> records;
  for (std::size_t index = 0; index < length; ++index) {
    const auto id = static_cast<std::uint32_t>(0x950 + index);
    const std::uint32_t next =
        index + 1 < length ? static_cast<std::uint32_t>(0x950 + index + 1) : 0u;
    const auto model = "actors\\robot\\parts\\chain" + std::to_string(index) + ".nif";
    records.push_back(next == 0 ? omod(id, model) : omod(id, model, {next}));
  }
  records.push_back(race(k_bot, "BotRace", 0x0, "Actors\\Robot\\CharacterAssets\\skeleton.nif",
                         "Actors\\CreateABot\\CharacterAssets\\Skeleton.NIF"));
  records.push_back(npc({.id = 0x95F, .race = k_bot, .obts = {0x950}}));
  return make_env({{"Chain.esp", build({}, 0x1, records)}});
}

inline NpcVisual decode_in(const fo4::env::Environment& env, std::string_view plugin,
                    std::uint32_t local) {
  const auto* ref = env.winner(
      fo4::FormKey{fo4::PluginKey::from_filename(plugin), fo4::LocalFormId{local}});
  EXPECT_NE(ref, nullptr);
  return fo4::forms::decode_npc_visual(env, *ref);
}

inline NpcVisual decode(const fo4::env::Environment& env,
                 std::uint32_t local,
                 std::uint16_t level = 1,
                 std::uint64_t seed = 0) {
  const auto* ref = env.winner(key(local));
  EXPECT_NE(ref, nullptr);
  return fo4::forms::decode_npc_visual(env, *ref, fo4::forms::NpcVisualOptions{level, seed});
}

inline NpcVisualPart part(std::string entry, std::uint32_t slots, NpcVisualSource source) {
  return NpcVisualPart{std::move(entry), slots, source};
}

inline constexpr std::string_view k_facegen_dir = "meshes/actors/character/facegendata/facegeom/base.esm/";
}  // namespace npc_visual_fixture
