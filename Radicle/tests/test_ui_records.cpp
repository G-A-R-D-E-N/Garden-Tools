#include "npc_visual_fixture.hpp"

#include "fo4/c_api_conditions.h"
#include "fo4/c_api_pet_write.h"
#include "fo4/c_api_records.h"
#include "fo4/c_api_ui_records.h"
#include "fo4/forms/ui_records.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using npc_visual_fixture::Bytes;
using npc_visual_fixture::Rec;
using npc_visual_fixture::build;
using npc_visual_fixture::key;
using npc_visual_fixture::make_env;
using npc_visual_fixture::words;
using npc_visual_fixture::zstr;

constexpr std::uint32_t k_workbench = 0x800;
constexpr std::uint32_t k_created = 0x801;
constexpr std::uint32_t k_cat_a = 0x802;
constexpr std::uint32_t k_cat_b = 0x803;
constexpr std::uint32_t k_misc_item = 0x810;
constexpr std::uint32_t k_cmpo_item = 0x811;
constexpr std::uint32_t k_cobj = 0x820;
constexpr std::uint32_t k_perk_one = 0x830;
constexpr std::uint32_t k_perk_two = 0x831;
constexpr std::uint32_t k_note = 0x840;
constexpr std::uint32_t k_note_bare = 0x841;
constexpr std::uint32_t k_term = 0x850;
constexpr std::uint32_t k_term_orphan = 0x851;
constexpr std::uint32_t k_term_sub = 0x852;
constexpr std::uint32_t k_holotape = 0x860;
constexpr std::uint32_t k_perk_effects = 0x870;
constexpr std::uint32_t k_perk_short_data = 0x871;
constexpr std::uint32_t k_perk_nul_icon = 0x872;
constexpr std::uint32_t k_note_short_data = 0x873;
constexpr std::uint32_t k_note_open_string = 0x874;
constexpr std::uint32_t k_cobj_bad_fnam = 0x875;
constexpr std::uint32_t k_cobj_null_component = 0x876;
constexpr std::uint32_t k_cobj_short_intv = 0x877;
constexpr std::uint32_t k_misc_short_data = 0x878;
constexpr std::uint32_t k_cmpo_short_data = 0x879;
constexpr std::uint32_t k_term_bad_cnto = 0x87A;
constexpr std::uint32_t k_term_short_body_ctda = 0x8B0;
constexpr std::uint32_t k_term_short_item_ctda = 0x8B1;
constexpr std::uint32_t k_term_split_item_lists = 0x8B2;
constexpr std::uint32_t k_term_ctda_after_cnto = 0x8B3;
constexpr std::uint32_t k_term_strings = 0x8B4;
constexpr std::uint32_t k_perk_data3 = 0x87B;
constexpr std::uint32_t k_perk_data4 = 0x87C;
constexpr std::uint32_t k_refr_locked = 0x880;
constexpr std::uint32_t k_refr_unlocked = 0x881;
constexpr std::uint32_t k_refr_short_lock = 0x882;
constexpr std::uint32_t k_acti = 0x890;
constexpr std::uint32_t k_cont = 0x891;
constexpr std::uint32_t k_door = 0x892;
constexpr std::uint32_t k_furn = 0x893;
constexpr std::uint32_t k_furn_old = 0x894;
constexpr std::uint32_t k_furn_bad_snam = 0x895;
constexpr std::uint32_t k_cont_bad_cnto = 0x896;
constexpr std::uint32_t k_furn_orphan_nam0 = 0x897;
constexpr std::uint32_t k_bare_acti = 0x898;
constexpr std::uint32_t k_cmpo_two = 0x812;
constexpr std::uint32_t k_cmpo_three = 0x813;
constexpr std::uint32_t k_cobj_gated = 0x8A0;
constexpr std::uint32_t k_misc_scrap = 0x8A1;
constexpr std::uint32_t k_bptd = 0x8C0;
constexpr std::uint32_t k_bptd_short_node = 0x8C1;
constexpr std::uint32_t k_bptd_orphan_node = 0x8C2;
constexpr std::uint32_t k_bptd_duplicate_target = 0x8C3;
constexpr std::uint32_t k_bptd_bare = 0x8C4;
constexpr std::uint32_t k_perk_barter = 0x8D0;
constexpr std::uint32_t k_perk_barter_short_ctda = 0x8D1;
constexpr std::uint32_t k_omod_props = 0x8E0;
constexpr std::uint32_t k_omod_empty = 0x8E1;
constexpr std::uint32_t k_omod_no_data = 0x8E2;
constexpr std::uint32_t k_omod_short_data = 0x8E3;
constexpr std::uint8_t k_entry_mod_buy_prices = 0x08;
constexpr std::uint8_t k_entry_mod_sell_prices = 0x3C;
constexpr std::uint16_t k_has_perk = 448;
constexpr std::uint8_t k_ctda_use_global = 0x04;

Bytes u8s(std::initializer_list<std::uint8_t> values) {
  fo4::test::ByteWriter out;
  for (const auto value : values) out.u8(value);
  return out.bytes;
}

Bytes u16le(std::uint16_t value) {
  fo4::test::ByteWriter out;
  out.u16(value);
  return out.bytes;
}

Bytes cobj_intv(std::uint16_t count, std::uint16_t priority) {
  fo4::test::ByteWriter out;
  out.u16(count);
  out.u16(priority);
  return out.bytes;
}

Bytes value_weight(std::uint32_t value, float weight) {
  fo4::test::ByteWriter out;
  out.u32(value);
  out.f32(weight);
  return out.bytes;
}

Bytes pair(std::uint32_t form, std::uint32_t count) {
  fo4::test::ByteWriter out;
  out.u32(form);
  out.u32(count);
  return out.bytes;
}

Bytes quest_stage(std::uint32_t form, std::uint16_t stage) {
  fo4::test::ByteWriter out;
  out.u32(form);
  out.u16(stage);
  return out.bytes;
}

Bytes ctda_row(std::uint8_t flags, float compare_or_zero, std::uint32_t compare_form, std::uint16_t function,
               std::uint32_t param1) {
  fo4::test::ByteWriter out;
  out.u8(flags);
  out.u8(std::uint8_t{0});
  out.u8(std::uint8_t{0});
  out.u8(std::uint8_t{0});
  if (compare_form != 0) out.u32(compare_form); else out.f32(compare_or_zero);
  out.u16(function);
  out.u16(std::uint16_t{0});
  out.u32(param1);
  out.u32(std::uint32_t{0});
  out.u32(std::uint32_t{0});
  out.u32(std::uint32_t{0});
  out.u32(0xFFFFFFFFu);
  return out.bytes;
}

Bytes four_pairs(std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d, std::uint32_t e, std::uint32_t f,
             std::uint32_t g, std::uint32_t h) {
  fo4::test::ByteWriter out;
  for (const auto value : {a, b, c, d, e, f, g, h}) out.u32(value);
  return out.bytes;
}

Bytes xloc(std::uint8_t level, std::uint32_t key_form, std::uint8_t flags) {
  fo4::test::ByteWriter out;
  out.u8(level);
  out.u8(std::uint8_t{0});
  out.u8(std::uint8_t{0});
  out.u8(std::uint8_t{0});
  out.u32(key_form);
  out.u8(flags);
  for (int i = 0; i < 7; ++i) out.u8(std::uint8_t{0});
  return out.bytes;
}

Bytes container_data(std::uint8_t flags, float weight) {
  fo4::test::ByteWriter out;
  out.u8(flags);
  out.f32(weight);
  return out.bytes;
}

Bytes marker_row(float x, float y, float z, float rotation, std::uint32_t keyword, std::uint8_t entry_types, bool with_entry_types) {
  fo4::test::ByteWriter out;
  out.f32(x);
  out.f32(y);
  out.f32(z);
  out.f32(rotation);
  out.u32(keyword);
  if (with_entry_types) {
    out.u8(entry_types);
    out.u8(std::uint8_t{0});
    out.u8(std::uint8_t{0});
    out.u8(std::uint8_t{0});
  }
  return out.bytes;
}

Bytes omod_row(std::uint8_t type, std::uint8_t function, std::uint16_t property, std::uint32_t value1,
               std::uint32_t value2, float step) {
  fo4::test::ByteWriter out;
  out.u8(type);
  for (int i = 0; i < 3; ++i) out.u8(std::uint8_t{0});
  out.u8(function);
  for (int i = 0; i < 3; ++i) out.u8(std::uint8_t{0});
  out.u16(property);
  out.u16(std::uint16_t{0});
  out.u32(value1);
  out.u32(value2);
  out.f32(step);
  return out.bytes;
}

Bytes omod_data(const std::vector<Bytes>& rows) {
  fo4::test::ByteWriter out;
  out.u32(std::uint32_t{0});
  out.u32(static_cast<std::uint32_t>(rows.size()));
  out.u16(std::uint16_t{0});
  out.u32(std::uint32_t{0});
  out.u16(std::uint16_t{0});
  out.u32(std::uint32_t{0});
  out.u32(std::uint32_t{0});
  out.u32(std::uint32_t{0});
  for (const auto& row : rows) out.bytes.insert(out.bytes.end(), row.begin(), row.end());
  return out.bytes;
}

Bytes join(Bytes first, const Bytes& second) {
  first.insert(first.end(), second.begin(), second.end());
  return first;
}

Bytes u16_pair(std::uint16_t first, std::uint16_t second) {
  fo4::test::ByteWriter out;
  out.u16(first);
  out.u16(second);
  return out.bytes;
}

struct BodyPartNodeSpec {
  float damage_mult{0.0f};
  std::uint8_t flags{0};
  std::uint8_t part_type{0};
  std::uint8_t health_percent{0};
  std::uint32_t actor_value{0};
  std::uint8_t to_hit_chance{0};
  std::uint8_t explosion_chance{0};
  std::uint8_t non_lethal_chance{0};
  std::uint8_t segment_index{0};
};

Bytes bptd_node(const BodyPartNodeSpec& spec, std::size_t size = 101) {
  Bytes out(size, std::byte{0});
  const auto put = [&](std::size_t offset, const void* value, std::size_t length) {
    if (offset + length <= size) std::memcpy(out.data() + offset, value, length);
  };
  put(0, &spec.damage_mult, sizeof(spec.damage_mult));
  put(64, &spec.flags, 1);
  put(65, &spec.part_type, 1);
  put(66, &spec.health_percent, 1);
  put(67, &spec.actor_value, sizeof(spec.actor_value));
  put(71, &spec.to_hit_chance, 1);
  put(72, &spec.explosion_chance, 1);
  put(73, &spec.non_lethal_chance, 1);
  put(78, &spec.segment_index, 1);
  return out;
}

std::vector<Rec> records() {
  return {
      Rec{"KYWD", k_workbench, {{"EDID", zstr("WorkbenchWeapons")}}},
      Rec{"ARMO", k_created, {{"EDID", zstr("Created")}}},
      Rec{"KYWD", k_cat_a, {{"EDID", zstr("CatA")}}},
      Rec{"KYWD", k_cat_b, {{"EDID", zstr("CatB")}}},
      Rec{"MISC", k_misc_item, {{"EDID", zstr("Screw")}, {"FULL", zstr("Screw")}, {"DATA", value_weight(0xFFFFFFFEu, 1.5f)}}},
      Rec{"CMPO", k_cmpo_item, {{"EDID", zstr("Steel")}, {"FULL", zstr("Steel")}, {"DATA", words({3})},
                               {"MNAM", words({k_misc_item})}}},
      Rec{"COBJ", k_cobj, {{"EDID", zstr("Recipe")}, {"DESC", zstr("Make it")}, {"BNAM", words({k_workbench})},
                          {"CNAM", words({k_created})}, {"FNAM", words({k_cat_a, 0, k_cat_b})},
                          {"INTV", cobj_intv(3, 7)}, {"FVPA", pair(k_cmpo_item, 4)}}},
      Rec{"PERK", k_perk_one, {{"EDID", zstr("PerkOne")}, {"FULL", zstr("Rank One")}, {"DESC", zstr("Does a thing")},
                              {"DATA", u8s({0, 2, 3, 1, 0})}, {"NNAM", words({k_perk_two})}}},
      Rec{"PERK", k_perk_two, {{"EDID", zstr("PerkTwo")}, {"FULL", zstr("Rank Two")}, {"NNAM", words({0})}}},
      Rec{"NOTE", k_note, {{"EDID", zstr("Tape")}, {"FULL", zstr("A Tape")}, {"DNAM", u8s({2})},
                          {"PNAM", zstr("game.swf")}, {"SNAM", words({k_term})}, {"DATA", value_weight(5, 0.5f)}}},
      Rec{"NOTE", k_note_bare, {{"EDID", zstr("Bare")}}},
      Rec{"TERM", k_term,
          {{"EDID", zstr("Term")}, {"FULL", zstr("Terminal")}, {"NAM0", zstr("Header")}, {"WNAM", zstr("Welcome")},
           {"BTXT", zstr("Body one")}, {"CTDA", ctda_row(0, 1.0f, 0, k_has_perk, k_perk_one)}, {"BTXT", zstr("Body two")},
           {"ITXT", zstr("Open")}, {"RNAM", zstr("Opening")}, {"ANAM", u8s({4})}, {"ITID", u16le(9)},
           {"TNAM", words({k_term_sub})}, {"CTDA", ctda_row(0, 1.0f, 0, k_has_perk, k_perk_two)},
           {"CTDA", ctda_row(k_ctda_use_global, 0.0f, k_cat_a, k_has_perk, k_perk_one)},
           {"ITXT", zstr("Show")}, {"ANAM", u8s({8})}, {"UNAM", zstr("Shown text")}, {"VNAM", zstr("pic.dds")},
           {"CNTO", pair(k_holotape, 2)}}},
      Rec{"TERM", k_term_orphan, {{"EDID", zstr("Orphan")}, {"RNAM", zstr("No item")}}},
      Rec{"TERM", k_term_sub, {{"EDID", zstr("Sub")}}},
      Rec{"NOTE", k_holotape, {{"EDID", zstr("Holo")}}},
      Rec{"PERK", k_perk_effects, {{"EDID", zstr("PerkFx")}, {"FULL", zstr("Effects")}, {"DATA", u8s({0, 2, 3, 1, 0})},
                                  {"NNAM", words({k_perk_two})},
                                  {"PRKE", u8s({2, 0, 0})}, {"DATA", u8s({1, 2, 3})}, {"EPFT", u8s({1})},
                                  {"EPFD", words({0x3F800000})}, {"PRKF", Bytes{}},
                                  {"PRKE", u8s({0, 1, 0})}, {"DATA", quest_stage(k_term, 10)}, {"PRKF", Bytes{}}}},
      Rec{"PERK", k_perk_barter, {{"EDID", zstr("PerkBarter")}, {"DATA", u8s({0, 2, 3, 1, 0})},
                                 {"PRKE", u8s({2, 1, 5})}, {"DATA", u8s({k_entry_mod_buy_prices, 3, 1})},
                                 {"PRKC", u8s({0})}, {"CTDA", ctda_row(0, 1.0f, 0, k_has_perk, k_perk_one)},
                                 {"EPFT", u8s({1})}, {"EPFD", words({0x3F000000})}, {"PRKF", Bytes{}},
                                 {"PRKE", u8s({2, 0, 2})}, {"DATA", u8s({k_entry_mod_sell_prices, 2, 0})},
                                 {"EPFT", u8s({1})}, {"EPFD", words({0x3FC00000})}, {"PRKF", Bytes{}},
                                 {"PRKE", u8s({0, 0, 0})}, {"DATA", quest_stage(k_term, 10)}, {"PRKF", Bytes{}},
                                 {"PRKE", u8s({2, 0, 1})}, {"DATA", u8s({k_entry_mod_buy_prices, 4, 0})},
                                 {"EPFT", u8s({2})}, {"EPFD", words({0x3F800000, 0x40000000})}, {"PRKF", Bytes{}}}},
      Rec{"OMOD", k_omod_props, {{"EDID", zstr("OmodProps")},
                                 {"DATA", omod_data({omod_row(1, 0, 3, 0x3FC00000, 0, 0.25f),
                                                     omod_row(4, 2, 9, k_workbench, 7, 0.0f),
                                                     omod_row(0, 1, 0x0102, 5, 0, 0.0f)})}}},
      Rec{"OMOD", k_omod_empty, {{"EDID", zstr("OmodEmpty")}, {"DATA", omod_data({})}}},
      Rec{"OMOD", k_omod_no_data, {{"EDID", zstr("OmodNoData")}}},
      Rec{"OMOD", k_omod_short_data, {{"EDID", zstr("OmodShort")}, {"DATA", u8s({1, 2, 3})}}},
      Rec{"PERK", k_perk_barter_short_ctda,
          {{"EDID", zstr("PerkBarterShort")}, {"DATA", u8s({0, 2, 3, 1, 0})}, {"PRKE", u8s({2, 0, 0})},
           {"DATA", u8s({k_entry_mod_buy_prices, 3, 1})}, {"PRKC", u8s({0})}, {"CTDA", Bytes(31, std::byte{0})},
           {"EPFT", u8s({1})}, {"EPFD", words({0x3F000000})}, {"PRKF", Bytes{}}}},
      Rec{"PERK", k_perk_short_data, {{"EDID", zstr("PerkShort")}, {"DATA", u8s({0, 2})}}},
      Rec{"PERK", k_perk_data3, {{"EDID", zstr("PerkData3")}, {"DATA", u8s({1, 4, 6})}}},
      Rec{"PERK", k_perk_data4, {{"EDID", zstr("PerkData4")}, {"DATA", u8s({0, 5, 7, 1})}}},
      Rec{"PERK", k_perk_nul_icon, {{"EDID", zstr("PerkNul")}, {"ICON", u8s({'a', 0, 'b', 0})}}},
      Rec{"NOTE", k_note_short_data, {{"EDID", zstr("NoteShort")}, {"DATA", words({5})}}},
      Rec{"NOTE", k_note_open_string, {{"EDID", zstr("NoteOpen")}, {"PNAM", u8s({'g', 'a', 'm', 'e'})}}},
      Rec{"COBJ", k_cobj_bad_fnam, {{"EDID", zstr("BadFnam")}, {"FNAM", u8s({1, 2, 3, 4, 5, 6})}}},
      Rec{"COBJ", k_cobj_null_component, {{"EDID", zstr("NullComp")}, {"FVPA", pair(0, 2)}}},
      Rec{"COBJ", k_cobj_short_intv, {{"EDID", zstr("ShortIntv")}, {"INTV", u8s({1})}}},
      Rec{"MISC", k_misc_short_data, {{"EDID", zstr("MiscShort")}, {"DATA", words({1})}}},
      Rec{"CMPO", k_cmpo_short_data, {{"EDID", zstr("CmpoShort")}, {"DATA", u8s({1, 0})}}},
      Rec{"TERM", k_term_bad_cnto, {{"EDID", zstr("BadCnto")}, {"CNTO", words({k_holotape})}}},
      Rec{"TERM", k_term_short_body_ctda,
          {{"EDID", zstr("ShortBodyCtda")}, {"BTXT", zstr("Body")}, {"CTDA", Bytes(31, std::byte{0})}}},
      Rec{"TERM", k_term_short_item_ctda,
          {{"EDID", zstr("ShortItemCtda")}, {"ITXT", zstr("Item")}, {"CTDA", Bytes(31, std::byte{0})}}},
      Rec{"TERM", k_term_split_item_lists,
          {{"EDID", zstr("SplitItem")}, {"ITXT", zstr("Item")}, {"CTDA", ctda_row(0, 1.0f, 0, k_has_perk, k_perk_one)},
           {"RNAM", zstr("Response")}, {"CTDA", ctda_row(0, 1.0f, 0, k_has_perk, k_perk_two)}}},
      Rec{"TERM", k_term_ctda_after_cnto,
          {{"EDID", zstr("AfterCnto")}, {"BTXT", zstr("Body")}, {"CTDA", ctda_row(0, 1.0f, 0, k_has_perk, k_perk_one)},
           {"CNTO", pair(k_holotape, 1)}, {"CTDA", ctda_row(0, 1.0f, 0, k_has_perk, k_perk_two)}}},
      Rec{"TERM", k_term_strings,
          {{"EDID", zstr("Strings")}, {"ITXT", zstr("Item")}, {"CTDA", ctda_row(0, 1.0f, 0, k_has_perk, k_perk_one)},
           {"CIS1", zstr("first text")}, {"CIS2", zstr("second text")}}},
      Rec{"CMPO", k_cmpo_two, {{"EDID", zstr("Plastic")}, {"FULL", zstr("Plastic")}, {"DATA", words({1})}}},
      Rec{"CMPO", k_cmpo_three, {{"EDID", zstr("Adhesive")}, {"FULL", zstr("Adhesive")}, {"DATA", words({1})}}},
      Rec{"COBJ", k_cobj_gated, {{"EDID", zstr("Gated")}, {"BNAM", words({k_workbench})}, {"CNAM", words({k_created})},
                                {"FVPA", pair(k_cmpo_item, 4)},
                                {"CTDA", ctda_row(0, 1.0f, 0, k_has_perk, k_perk_one)},
                                {"CTDA", ctda_row(0, 1.0f, 0, k_has_perk, k_perk_two)},
                                {"CTDA", ctda_row(k_ctda_use_global, 0.0f, k_cat_a, k_has_perk, k_perk_one)}}},
      Rec{"BPTD", k_bptd,
          {{"EDID", zstr("Body")}, {"BPTN", zstr("Torso")}, {"BPNN", zstr("Torso_Node")}, {"BPNT", zstr("TorsoVATS")},
           {"BPND", bptd_node({1.5f, 0x09, 0, 100, k_perk_one, 35, 10, 20, 3})}, {"NAM1", zstr("limb.nif")},
           {"BPNN", zstr("LookAt_Node")}, {"BPNT", zstr("LookAtVATS")},
           {"BPND", bptd_node({0.25f, 0x00, 3, 0, 0, 0, 0, 0, 0})},
           {"BPTN", zstr("Head")}, {"BPNN", zstr("Head_Node")}}},
      Rec{"BPTD", k_bptd_short_node, {{"EDID", zstr("ShortNode")}, {"BPTN", zstr("Head")}, {"BPND", bptd_node({}, 100)}}},
      Rec{"BPTD", k_bptd_orphan_node, {{"EDID", zstr("OrphanNode")}, {"BPND", bptd_node({})}}},
      Rec{"BPTD", k_bptd_duplicate_target,
          {{"EDID", zstr("DupTarget")}, {"BPTN", zstr("Head")}, {"BPNT", zstr("A")}, {"BPNT", zstr("B")}}},
      Rec{"BPTD", k_bptd_bare, {{"EDID", zstr("Bare")}}},
      Rec{"MISC", k_misc_scrap, {{"EDID", zstr("DeskFan")}, {"FULL", zstr("Desk fan")}, {"DATA", value_weight(4, 3.0f)},
                                {"CVPA", four_pairs(k_cmpo_item, 2, k_cmpo_three, 2, 0, 1, k_cmpo_two, 2)}}},
      Rec{"REFR", k_refr_locked, {{"NAME", words({k_misc_item})}, {"XLOC", xloc(75, k_misc_item, 0x04)}}},
      Rec{"REFR", k_refr_unlocked, {{"NAME", words({k_misc_item})}}},
      Rec{"REFR", k_refr_short_lock, {{"NAME", words({k_misc_item})}, {"XLOC", u8s({75, 0, 0, 0})}}},
      Rec{"ACTI", k_acti,
          {{"EDID", zstr("Lever")}, {"FULL", zstr("Lever")}, {"SNAM", words({k_created})}, {"VNAM", words({k_cat_a})},
           {"WNAM", words({k_cat_b})}, {"ATTX", zstr("Pull")}, {"FNAM", u16le(0x0002)}, {"KNAM", words({k_workbench})}}},
      Rec{"ACTI", k_bare_acti, {{"EDID", zstr("Bare")}, {"KNAM", words({0})}}},
      Rec{"CONT", k_cont,
          {{"EDID", zstr("Footlocker")}, {"FULL", zstr("Footlocker")}, {"COCT", words({3})},
           {"CNTO", pair(k_misc_item, 3)}, {"CNTO", pair(0, 9)}, {"COED", Bytes(12, std::byte{0})},
           {"CNTO", pair(k_cmpo_item, 0xFFFFFFFFu)}, {"DATA", container_data(0x02, 12.5f)},
           {"SNAM", words({k_created})}, {"QNAM", words({k_cat_a})}, {"TNAM", words({k_cat_b})},
           {"ONAM", words({k_perk_one})}}},
      Rec{"CONT", k_cont_bad_cnto, {{"EDID", zstr("BadCnto")}, {"CNTO", words({k_misc_item})}}},
      Rec{"DOOR", k_door,
          {{"EDID", zstr("Door")}, {"FULL", zstr("Door")}, {"SNAM", words({k_created})}, {"ANAM", words({k_cat_a})},
           {"BNAM", words({k_cat_b})}, {"FNAM", u8s({0x0A})}, {"ONAM", zstr("Unlock")}, {"CNAM", zstr("Lock")},
           {"TNAM", words({k_misc_item})}, {"TNAM", words({0})}, {"TNAM", words({k_cmpo_item})}}},
      Rec{"FURN", k_furn,
          {{"EDID", zstr("Chair")}, {"FULL", zstr("Chair")}, {"WNAM", words({k_cat_a})}, {"ATTX", zstr("Sit")},
           {"FNAM", u16le(0x0002)}, {"COCT", words({1})}, {"CNTO", pair(k_misc_item, 1)},
           {"MNAM", words({0x80400003u})}, {"WBDT", u8s({7, 0})}, {"NAM1", words({k_perk_one})},
           {"ENAM", words({0})}, {"NAM0", u16_pair(0, 0x0003)}, {"ENAM", words({1})},
           {"FNPR", u16_pair(1, 0x0009)}, {"FNPR", u16_pair(4, 0x0010)}, {"XMRK", zstr("Chair.nif")},
           {"OBTE", words({1})}, {"FULL", zstr("Template")},
           {"SNAM", join(marker_row(1.0f, 2.0f, 3.0f, 1.5f, k_workbench, 0x05, true),
                         marker_row(-1.0f, 0.0f, 0.5f, 0.0f, 0, 0x01, true))}}},
      Rec{"FURN", k_furn_old,
          {{"EDID", zstr("OldChair")},
           {"SNAM", join(marker_row(4.0f, 5.0f, 6.0f, 2.0f, k_workbench, 0, false),
                         marker_row(0.0f, 0.0f, 0.0f, 0.0f, 0, 0, false))}},
          0, 120},
      Rec{"FURN", k_furn_bad_snam, {{"EDID", zstr("BadSnam")}, {"SNAM", Bytes(30, std::byte{0})}}},
      Rec{"FURN", k_furn_orphan_nam0, {{"EDID", zstr("Orphan")}, {"NAM0", u16_pair(0, 1)}}},
  };
}

class UiRecordDecode : public ::testing::Test {
protected:
  UiRecordDecode() : env_(make_env({{"Base.esm", build({}, 0x1, records())}})) {}

  const fo4::env::RecordRef& ref(std::uint32_t local) const {
    const auto* found = env_.winner(key(local));
    if (found == nullptr) throw std::runtime_error("fixture record missing");
    return *found;
  }

  const fo4::Plugin& plugin(std::uint32_t local) const { return env_.plugin(ref(local).plugin); }
  const fo4::Record& record(std::uint32_t local) const { return *ref(local).record; }

  fo4::env::Environment env_;
};

class UiRecordsCApi : public ::testing::Test {
protected:
  void SetUp() override {
    root_ = std::filesystem::temp_directory_path() /
            ("radicle_ui_records_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root_);
    const auto path = (root_ / "Base.esm").string();
    const auto bytes = build({}, 0x1, records());
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    stream.close();
    const char* paths[] = {path.c_str()};
    ASSERT_EQ(radicle_env_load(paths, 1, &env_), RADICLE_STATUS_OK);
  }

  void TearDown() override {
    radicle_env_destroy(env_);
    std::error_code error;
    std::filesystem::remove_all(root_, error);
  }

  const radicle_record* winner(std::uint32_t local) {
    const radicle_record* record = nullptr;
    EXPECT_EQ(radicle_env_winner(env_, radicle_form_key{"Base.esm", local}, &record), RADICLE_STATUS_OK);
    return record;
  }

  radicle_status read_field(std::uint32_t local, const char* path, std::size_t index, std::string& out) {
    char* text = nullptr;
    const auto status = radicle_record_get_field(env_, winner(local), path, index, &text);
    out = text == nullptr ? std::string{} : std::string(text);
    radicle_record_scalar_free(text);
    return status;
  }

  std::string field(std::uint32_t local, const char* path, std::size_t index = 0) {
    std::string text;
    EXPECT_EQ(read_field(local, path, index, text), RADICLE_STATUS_OK) << path << ' ' << index;
    return text;
  }

  std::size_t list_size(std::uint32_t local, const char* path) {
    std::size_t size = 0;
    EXPECT_EQ(radicle_record_list_size(env_, winner(local), path, &size), RADICLE_STATUS_OK) << path;
    return size;
  }

  static std::string link(std::uint32_t local) {
    char text[32]{};
    std::snprintf(text, sizeof(text), "%06X:Base.esm", static_cast<unsigned>(local));
    return text;
  }

  std::filesystem::path root_;
  radicle_env* env_{nullptr};
};

}  // namespace

TEST_F(UiRecordDecode, ConstructibleObjectReadsBenchCreatedObjectCategoriesAndComponents) {
  const auto cobj = fo4::forms::decode_constructible_object(plugin(k_cobj), record(k_cobj));
  EXPECT_EQ(cobj.workbench_keyword, key(k_workbench));
  EXPECT_EQ(cobj.created_object, key(k_created));
  ASSERT_EQ(cobj.category_keywords.size(), 2u);
  EXPECT_EQ(cobj.category_keywords[1], key(k_cat_b));
  EXPECT_EQ(cobj.created_count, 3);
  EXPECT_EQ(cobj.priority, 7);
  ASSERT_EQ(cobj.components.size(), 1u);
  EXPECT_EQ(cobj.components[0].component, key(k_cmpo_item));
  EXPECT_EQ(cobj.components[0].count, 4u);
  ASSERT_TRUE(cobj.description.has_value());
  EXPECT_EQ(cobj.description->text, "Make it");
}

TEST_F(UiRecordDecode, PerkReadsNextRankNamesAndData) {
  const auto perk = fo4::forms::decode_perk_info(plugin(k_perk_one), record(k_perk_one));
  EXPECT_EQ(perk.next_perk, key(k_perk_two));
  ASSERT_TRUE(perk.data.has_value());
  EXPECT_EQ(perk.data->level, 2);
  EXPECT_EQ(perk.data->num_ranks, 3);
  EXPECT_EQ(perk.data->playable, 1);
  EXPECT_EQ(perk.full_name->text, "Rank One");
  const auto last = fo4::forms::decode_perk_info(plugin(k_perk_two), record(k_perk_two));
  EXPECT_FALSE(last.next_perk.has_value());
  EXPECT_FALSE(last.data.has_value());
}

TEST_F(UiRecordDecode, PerkWithEffectsKeepsItsFiveByteHeaderDataAndIgnoresEffectData) {
  const auto perk = fo4::forms::decode_perk_info(plugin(k_perk_effects), record(k_perk_effects));
  ASSERT_TRUE(perk.data.has_value());
  EXPECT_EQ(perk.data->level, 2);
  EXPECT_EQ(perk.data->num_ranks, 3);
  EXPECT_EQ(perk.data->playable, 1);
  EXPECT_EQ(perk.next_perk, key(k_perk_two));
  EXPECT_EQ(perk.full_name->text, "Effects");
}

TEST_F(UiRecordDecode, PerkEntryPointsListsOnlyEntryPointEffectsWithRawBytesFloatValueAndTabConditions) {
  const auto perk = fo4::forms::decode_perk_entry_points(plugin(k_perk_barter), record(k_perk_barter));
  EXPECT_EQ(perk.key, key(k_perk_barter));
  ASSERT_EQ(perk.effects.size(), 3u);
  const auto& buy = perk.effects[0];
  EXPECT_EQ(buy.entry_point, k_entry_mod_buy_prices);
  EXPECT_EQ(buy.function, 3);
  EXPECT_EQ(buy.rank, 1);
  EXPECT_EQ(buy.priority, 5);
  EXPECT_EQ(buy.parameter_type, std::optional<std::uint8_t>(1));
  EXPECT_EQ(buy.value, std::optional<float>(0.5f));
  ASSERT_EQ(buy.tabs.size(), 1u);
  EXPECT_EQ(buy.tabs[0].run_on, 0);
  ASSERT_EQ(buy.tabs[0].conditions.size(), 1u);
  EXPECT_EQ(buy.tabs[0].conditions[0].function, k_has_perk);
  ASSERT_EQ(buy.tabs[0].condition_links.size(), 1u);
  EXPECT_EQ(buy.tabs[0].condition_links[0].param1, key(k_perk_one));
  const auto& sell = perk.effects[1];
  EXPECT_EQ(sell.entry_point, k_entry_mod_sell_prices);
  EXPECT_EQ(sell.function, 2);
  EXPECT_EQ(sell.value, std::optional<float>(1.5f));
  EXPECT_TRUE(sell.tabs.empty());
  const auto& range = perk.effects[2];
  EXPECT_EQ(range.function, 4);
  EXPECT_EQ(range.parameter_type, std::optional<std::uint8_t>(2));
  EXPECT_FALSE(range.value.has_value());
}

TEST_F(UiRecordDecode, PerkEntryPointsIsEmptyWithoutEffectsAndRejectsANonPerk) {
  EXPECT_TRUE(fo4::forms::decode_perk_entry_points(plugin(k_perk_two), record(k_perk_two)).effects.empty());
  EXPECT_EQ(fo4::forms::decode_perk_entry_points(plugin(k_perk_effects), record(k_perk_effects)).effects.size(), 1u);
  EXPECT_THROW(fo4::forms::decode_perk_entry_points(plugin(k_cobj), record(k_cobj)), fo4::forms::UiRecordError);
  EXPECT_THROW(fo4::forms::decode_perk_entry_points(plugin(k_perk_barter_short_ctda), record(k_perk_barter_short_ctda)),
               fo4::forms::UiRecordError);
}

TEST_F(UiRecordDecode, PerkAcceptsThreeAndFourByteDataAndDefaultsTheMissingFlagsToZero) {
  const auto three = fo4::forms::decode_perk_info(plugin(k_perk_data3), record(k_perk_data3));
  ASSERT_TRUE(three.data.has_value());
  EXPECT_EQ(three.data->trait, 1);
  EXPECT_EQ(three.data->level, 4);
  EXPECT_EQ(three.data->num_ranks, 6);
  EXPECT_EQ(three.data->playable, 0);
  EXPECT_EQ(three.data->hidden, 0);
  const auto four = fo4::forms::decode_perk_info(plugin(k_perk_data4), record(k_perk_data4));
  ASSERT_TRUE(four.data.has_value());
  EXPECT_EQ(four.data->level, 5);
  EXPECT_EQ(four.data->num_ranks, 7);
  EXPECT_EQ(four.data->playable, 1);
  EXPECT_EQ(four.data->hidden, 0);
}

TEST_F(UiRecordDecode, PerkRejectsTwoByteDataAndAnIconWithAnEmbeddedNul) {
  EXPECT_THROW(fo4::forms::decode_perk_info(plugin(k_perk_short_data), record(k_perk_short_data)),
               fo4::forms::UiRecordError);
  EXPECT_THROW(fo4::forms::decode_perk_info(plugin(k_perk_nul_icon), record(k_perk_nul_icon)),
               fo4::forms::UiRecordError);
}

TEST_F(UiRecordDecode, NoteRejectsShortDataAndAProgramFileWithoutATerminator) {
  EXPECT_THROW(fo4::forms::decode_note_info(plugin(k_note_short_data), record(k_note_short_data)),
               fo4::forms::UiRecordError);
  EXPECT_THROW(fo4::forms::decode_note_info(plugin(k_note_open_string), record(k_note_open_string)),
               fo4::forms::UiRecordError);
}

TEST_F(UiRecordDecode, ConstructibleObjectRejectsBadFnamLengthAndShortIntv) {
  EXPECT_THROW(fo4::forms::decode_constructible_object(plugin(k_cobj_bad_fnam), record(k_cobj_bad_fnam)),
               fo4::forms::UiRecordError);
  EXPECT_THROW(fo4::forms::decode_constructible_object(plugin(k_cobj_short_intv), record(k_cobj_short_intv)),
               fo4::forms::UiRecordError);
}

TEST_F(UiRecordDecode, ConstructibleObjectSkipsNullComponentFormIds) {
  const auto cobj = fo4::forms::decode_constructible_object(plugin(k_cobj_null_component), record(k_cobj_null_component));
  EXPECT_TRUE(cobj.components.empty());
}

TEST_F(UiRecordDecode, MiscComponentAndTerminalRejectTruncatedSubrecords) {
  EXPECT_THROW(fo4::forms::decode_misc_info(plugin(k_misc_short_data), record(k_misc_short_data)),
               fo4::forms::UiRecordError);
  EXPECT_THROW(fo4::forms::decode_component_info(plugin(k_cmpo_short_data), record(k_cmpo_short_data)),
               fo4::forms::UiRecordError);
  EXPECT_THROW(fo4::forms::decode_terminal_info(plugin(k_term_bad_cnto), record(k_term_bad_cnto)),
               fo4::forms::UiRecordError);
}

TEST_F(UiRecordDecode, NoteReadsTypeProgramTargetAndValue) {
  const auto note = fo4::forms::decode_note_info(plugin(k_note), record(k_note));
  EXPECT_EQ(note.type, 2);
  EXPECT_EQ(note.program_file, "game.swf");
  EXPECT_EQ(note.target, key(k_term));
  EXPECT_EQ(note.value, 5u);
  EXPECT_FLOAT_EQ(*note.weight, 0.5f);
  const auto bare = fo4::forms::decode_note_info(plugin(k_note_bare), record(k_note_bare));
  EXPECT_FALSE(bare.type.has_value());
  EXPECT_FALSE(bare.target.has_value());
}

TEST_F(UiRecordDecode, BodyPartDataGroupsPartsByNameAndNodeAndReadsNodeData) {
  const auto data = fo4::forms::decode_body_part_data(plugin(k_bptd), record(k_bptd));
  ASSERT_EQ(data.parts.size(), 3u);
  const auto& torso = data.parts[0];
  EXPECT_EQ(torso.name->text, "Torso");
  EXPECT_EQ(torso.part_node, "Torso_Node");
  EXPECT_EQ(torso.vats_target, "TorsoVATS");
  ASSERT_TRUE(torso.data.has_value());
  EXPECT_FLOAT_EQ(torso.data->damage_mult, 1.5f);
  EXPECT_EQ(torso.data->flags, 0x09);
  EXPECT_EQ(torso.data->part_type, 0);
  EXPECT_EQ(torso.data->health_percent, 100);
  EXPECT_EQ(torso.data->actor_value, key(k_perk_one));
  EXPECT_EQ(torso.data->to_hit_chance, 35);
  EXPECT_EQ(torso.data->explodable_explosion_chance, 10);
  EXPECT_EQ(torso.data->non_lethal_dismemberment_chance, 20);
  EXPECT_EQ(torso.data->geometry_segment_index, 3);
  const auto& look_at = data.parts[1];
  EXPECT_FALSE(look_at.name.has_value());
  EXPECT_EQ(look_at.part_node, "LookAt_Node");
  EXPECT_EQ(look_at.vats_target, "LookAtVATS");
  EXPECT_EQ(look_at.data->part_type, 3);
  EXPECT_FALSE(look_at.data->actor_value.has_value());
  const auto& head = data.parts[2];
  EXPECT_EQ(head.name->text, "Head");
  EXPECT_EQ(head.part_node, "Head_Node");
  EXPECT_FALSE(head.vats_target.has_value());
  EXPECT_FALSE(head.data.has_value());
  EXPECT_TRUE(fo4::forms::decode_body_part_data(plugin(k_bptd_bare), record(k_bptd_bare)).parts.empty());
}

TEST_F(UiRecordDecode, BodyPartDataRejectsAShortNodeAnOrphanNodeAndADuplicateTarget) {
  for (const auto local : {k_bptd_short_node, k_bptd_orphan_node, k_bptd_duplicate_target}) {
    EXPECT_THROW(fo4::forms::decode_body_part_data(plugin(local), record(local)), fo4::forms::UiRecordError) << local;
  }
}

TEST_F(UiRecordsCApi, BodyPartDataDecodeExposesPartsAndFreeReleasesThem) {
  radicle_body_part_data_info* info = nullptr;
  ASSERT_EQ(radicle_body_part_data_decode(env_, winner(k_bptd), &info), RADICLE_STATUS_OK);
  ASSERT_NE(info, nullptr);
  EXPECT_EQ(info->record.local_id, k_bptd);
  ASSERT_EQ(info->part_count, 3u);
  EXPECT_STREQ(info->parts[0].name.text, "Torso");
  EXPECT_STREQ(info->parts[0].part_node, "Torso_Node");
  EXPECT_STREQ(info->parts[0].vats_target, "TorsoVATS");
  EXPECT_EQ(info->parts[0].has_data, 1);
  EXPECT_FLOAT_EQ(info->parts[0].damage_mult, 1.5f);
  EXPECT_EQ(info->parts[0].flags, 0x09);
  EXPECT_EQ(info->parts[0].part_type, RADICLE_BODY_PART_TORSO);
  EXPECT_EQ(info->parts[0].health_percent, 100);
  EXPECT_EQ(info->parts[0].actor_value.local_id, k_perk_one);
  EXPECT_EQ(info->parts[0].to_hit_chance, 35);
  EXPECT_EQ(info->parts[0].explodable_explosion_chance, 10);
  EXPECT_EQ(info->parts[0].non_lethal_dismemberment_chance, 20);
  EXPECT_EQ(info->parts[0].geometry_segment_index, 3);
  EXPECT_EQ(info->parts[1].name.kind, RADICLE_NAME_ABSENT);
  EXPECT_EQ(info->parts[1].part_type, RADICLE_BODY_PART_LOOK_AT);
  EXPECT_EQ(info->parts[1].actor_value.plugin, nullptr);
  EXPECT_EQ(info->parts[2].has_data, 0);
  EXPECT_EQ(info->parts[2].vats_target, nullptr);
  radicle_body_part_data_free(info);
  EXPECT_EQ(radicle_body_part_data_decode(env_, winner(k_bptd_bare), &info), RADICLE_STATUS_OK);
  EXPECT_EQ(info->part_count, 0u);
  EXPECT_EQ(info->parts, nullptr);
  radicle_body_part_data_free(info);
}

TEST_F(UiRecordsCApi, BodyPartDataDecodeRejectsTheWrongSignatureAndMalformedRecords) {
  radicle_body_part_data_info* info = nullptr;
  EXPECT_EQ(radicle_body_part_data_decode(env_, winner(k_cobj), &info), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(info, nullptr);
  EXPECT_EQ(radicle_body_part_data_decode(env_, winner(k_bptd_short_node), &info), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(info, nullptr);
  radicle_body_part_data_free(nullptr);
}

TEST_F(UiRecordDecode, TerminalGroupsBodyTextsMenuItemsAndHolotapes) {
  const auto term = fo4::forms::decode_terminal_info(plugin(k_term), record(k_term));
  EXPECT_EQ(term.header_text->text, "Header");
  EXPECT_EQ(term.welcome_text->text, "Welcome");
  ASSERT_EQ(term.body_texts.size(), 2u);
  EXPECT_EQ(term.body_texts[0].condition_count, 1u);
  EXPECT_EQ(term.body_texts[1].condition_count, 0u);
  ASSERT_EQ(term.body_texts[0].conditions.size(), 1u);
  EXPECT_EQ(term.body_texts[0].conditions[0].function, k_has_perk);
  EXPECT_FLOAT_EQ(term.body_texts[0].conditions[0].comparison_float(), 1.0f);
  ASSERT_EQ(term.body_texts[0].condition_links.size(), 1u);
  EXPECT_EQ(term.body_texts[0].condition_links[0].param1, key(k_perk_one));
  EXPECT_TRUE(term.body_texts[1].conditions.empty());
  EXPECT_TRUE(term.body_texts[1].condition_links.empty());
  ASSERT_EQ(term.menu_items.size(), 2u);
  EXPECT_EQ(term.menu_items[0].item_text->text, "Open");
  EXPECT_EQ(term.menu_items[0].response_text->text, "Opening");
  EXPECT_EQ(term.menu_items[0].type, 4);
  EXPECT_EQ(term.menu_items[0].item_id, 9);
  EXPECT_EQ(term.menu_items[0].submenu, key(k_term_sub));
  EXPECT_EQ(term.menu_items[0].condition_count, 2u);
  ASSERT_EQ(term.menu_items[0].conditions.size(), 2u);
  ASSERT_EQ(term.menu_items[0].condition_links.size(), 2u);
  EXPECT_EQ(term.menu_items[0].condition_links[0].param1, key(k_perk_two));
  EXPECT_FALSE(term.menu_items[0].condition_links[0].global.has_value());
  EXPECT_TRUE(term.menu_items[0].conditions[1].use_global());
  EXPECT_EQ(term.menu_items[0].condition_links[1].global, key(k_cat_a));
  EXPECT_EQ(term.menu_items[0].condition_links[1].param1, key(k_perk_one));
  EXPECT_TRUE(term.menu_items[1].conditions.empty());
  EXPECT_EQ(term.menu_items[1].type, 8);
  EXPECT_EQ(term.menu_items[1].display_text->text, "Shown text");
  EXPECT_EQ(term.menu_items[1].image, "pic.dds");
  EXPECT_FALSE(term.menu_items[1].submenu.has_value());
  ASSERT_EQ(term.holotapes.size(), 1u);
  EXPECT_EQ(term.holotapes[0].holotape, key(k_holotape));
  EXPECT_EQ(term.holotapes[0].count, 2);
}

TEST_F(UiRecordDecode, TerminalRejectsAConditionThatDoesNotRoundTrip) {
  EXPECT_THROW(fo4::forms::decode_terminal_info(plugin(k_term_short_body_ctda), record(k_term_short_body_ctda)),
               fo4::forms::UiRecordError);
  EXPECT_THROW(fo4::forms::decode_terminal_info(plugin(k_term_short_item_ctda), record(k_term_short_item_ctda)),
               fo4::forms::UiRecordError);
}

TEST_F(UiRecordDecode, TerminalRejectsAMenuItemWithTwoSeparatedConditionLists) {
  EXPECT_THROW(fo4::forms::decode_terminal_info(plugin(k_term_split_item_lists), record(k_term_split_item_lists)),
               fo4::forms::UiRecordError);
}

TEST_F(UiRecordDecode, TerminalIgnoresAConditionAfterASubrecordOutsideTheBodyScope) {
  const auto term = fo4::forms::decode_terminal_info(plugin(k_term_ctda_after_cnto), record(k_term_ctda_after_cnto));
  ASSERT_EQ(term.body_texts.size(), 1u);
  EXPECT_EQ(term.body_texts[0].condition_count, 1u);
  ASSERT_EQ(term.body_texts[0].condition_links.size(), 1u);
  EXPECT_EQ(term.body_texts[0].condition_links[0].param1, key(k_perk_one));
  ASSERT_EQ(term.holotapes.size(), 1u);
}

TEST_F(UiRecordDecode, TerminalMenuItemKeepsConditionParameterStrings) {
  const auto term = fo4::forms::decode_terminal_info(plugin(k_term_strings), record(k_term_strings));
  ASSERT_EQ(term.menu_items.size(), 1u);
  ASSERT_EQ(term.menu_items[0].conditions.size(), 1u);
  EXPECT_EQ(term.menu_items[0].conditions[0].parameter1_string, "first text");
  EXPECT_EQ(term.menu_items[0].conditions[0].parameter2_string, "second text");
}

TEST_F(UiRecordDecode, TerminalRejectsAnItemSubrecordOutsideAMenuItem) {
  EXPECT_THROW(fo4::forms::decode_terminal_info(plugin(k_term_orphan), record(k_term_orphan)),
               fo4::forms::UiRecordError);
}

TEST_F(UiRecordDecode, MiscReadsSignedValueAndWeightAndComponentReadsItsLinks) {
  const auto misc = fo4::forms::decode_misc_info(plugin(k_misc_item), record(k_misc_item));
  EXPECT_EQ(misc.value, -2);
  EXPECT_FLOAT_EQ(*misc.weight, 1.5f);
  const auto cmpo = fo4::forms::decode_component_info(plugin(k_cmpo_item), record(k_cmpo_item));
  EXPECT_EQ(cmpo.auto_calc_value, 3u);
  EXPECT_EQ(cmpo.scrap_item, key(k_misc_item));
}

TEST_F(UiRecordDecode, ActivatorReadsActivateTextFlagsKeywordSoundsAndWater) {
  const auto acti = fo4::forms::decode_activator_info(plugin(k_acti), record(k_acti));
  EXPECT_EQ(acti.full_name->text, "Lever");
  EXPECT_EQ(acti.activate_text->text, "Pull");
  EXPECT_EQ(acti.flags, 2);
  EXPECT_EQ(acti.interaction_keyword, key(k_workbench));
  EXPECT_EQ(acti.looping_sound, key(k_created));
  EXPECT_EQ(acti.activation_sound, key(k_cat_a));
  EXPECT_EQ(acti.water_type, key(k_cat_b));
  const auto bare = fo4::forms::decode_activator_info(plugin(k_bare_acti), record(k_bare_acti));
  EXPECT_FALSE(bare.activate_text.has_value());
  EXPECT_FALSE(bare.flags.has_value());
  EXPECT_FALSE(bare.interaction_keyword.has_value());
}

TEST_F(UiRecordDecode, ContainerReadsItemsInOrderDropsNullItemsAndReadsDataAndSounds) {
  const auto cont = fo4::forms::decode_container_info(plugin(k_cont), record(k_cont));
  ASSERT_EQ(cont.items.size(), 2u);
  EXPECT_EQ(cont.items[0].item, key(k_misc_item));
  EXPECT_EQ(cont.items[0].count, 3);
  EXPECT_EQ(cont.items[1].item, key(k_cmpo_item));
  EXPECT_EQ(cont.items[1].count, -1);
  EXPECT_EQ(cont.flags, 0x02);
  EXPECT_FLOAT_EQ(*cont.weight, 12.5f);
  EXPECT_EQ(cont.open_sound, key(k_created));
  EXPECT_EQ(cont.close_sound, key(k_cat_a));
  EXPECT_EQ(cont.take_all_sound, key(k_cat_b));
  EXPECT_EQ(cont.filter_list, key(k_perk_one));
  EXPECT_THROW(fo4::forms::decode_container_info(plugin(k_cont_bad_cnto), record(k_cont_bad_cnto)),
               fo4::forms::UiRecordError);
}

TEST_F(UiRecordDecode, DoorReadsSoundsFlagsAlternateTextAndRandomDestinations) {
  const auto door = fo4::forms::decode_door_info(plugin(k_door), record(k_door));
  EXPECT_EQ(door.open_sound, key(k_created));
  EXPECT_EQ(door.close_sound, key(k_cat_a));
  EXPECT_EQ(door.loop_sound, key(k_cat_b));
  EXPECT_EQ(door.flags, 0x0A);
  EXPECT_EQ(door.open_text->text, "Unlock");
  EXPECT_EQ(door.close_text->text, "Lock");
  ASSERT_EQ(door.random_destinations.size(), 2u);
  EXPECT_EQ(door.random_destinations[0], key(k_misc_item));
  EXPECT_EQ(door.random_destinations[1], key(k_cmpo_item));
}

TEST_F(UiRecordDecode, FurnitureReadsBenchMarkersEntryPointsAndMarkerParametersByFormVersion) {
  const auto furn = fo4::forms::decode_furniture_info(plugin(k_furn), record(k_furn));
  EXPECT_EQ(furn.activate_text->text, "Sit");
  EXPECT_EQ(furn.full_name->text, "Chair");
  EXPECT_EQ(furn.flags, 2);
  EXPECT_EQ(furn.marker_flags, 0x80400003u);
  EXPECT_EQ(furn.bench_type, 7);
  EXPECT_EQ(furn.drinking_water_type, key(k_cat_a));
  EXPECT_EQ(furn.associated_form, key(k_perk_one));
  ASSERT_EQ(furn.items.size(), 1u);
  ASSERT_EQ(furn.markers.size(), 2u);
  EXPECT_EQ(furn.markers[0].index, 0);
  EXPECT_EQ(furn.markers[0].disabled_entry_points, 3);
  EXPECT_EQ(furn.markers[1].index, 1);
  EXPECT_FALSE(furn.markers[1].disabled_entry_points.has_value());
  ASSERT_EQ(furn.entry_points.size(), 2u);
  EXPECT_EQ(furn.entry_points[0].anim_type, 1);
  EXPECT_EQ(furn.entry_points[0].entry_points, 0x0009);
  EXPECT_EQ(furn.entry_points[1].anim_type, 4);
  EXPECT_EQ(furn.marker_model, "Chair.nif");
  ASSERT_EQ(furn.marker_params.size(), 2u);
  EXPECT_FLOAT_EQ(furn.marker_params[0].offset[2], 3.0f);
  EXPECT_FLOAT_EQ(furn.marker_params[0].rotation_z, 1.5f);
  EXPECT_EQ(furn.marker_params[0].keyword, key(k_workbench));
  EXPECT_EQ(furn.marker_params[0].entry_types, 0x05);
  EXPECT_FALSE(furn.marker_params[1].keyword.has_value());
  const auto old = fo4::forms::decode_furniture_info(plugin(k_furn_old), record(k_furn_old));
  ASSERT_EQ(old.marker_params.size(), 2u);
  EXPECT_FLOAT_EQ(old.marker_params[0].offset[0], 4.0f);
  EXPECT_EQ(old.marker_params[0].keyword, key(k_workbench));
  EXPECT_EQ(old.marker_params[0].entry_types, 0);
  EXPECT_THROW(fo4::forms::decode_furniture_info(plugin(k_furn_bad_snam), record(k_furn_bad_snam)),
               fo4::forms::UiRecordError);
  EXPECT_THROW(fo4::forms::decode_furniture_info(plugin(k_furn_orphan_nam0), record(k_furn_orphan_nam0)),
               fo4::forms::UiRecordError);
}

TEST(UiRecordLock, ClassifiesLevelBandsAndSpecialValues) {
  using fo4::forms::LockKind;
  using fo4::forms::classify_lock_level;
  EXPECT_EQ(classify_lock_level(0), LockKind::none);
  EXPECT_EQ(classify_lock_level(1), LockKind::novice);
  EXPECT_EQ(classify_lock_level(49), LockKind::novice);
  EXPECT_EQ(classify_lock_level(50), LockKind::advanced);
  EXPECT_EQ(classify_lock_level(75), LockKind::expert);
  EXPECT_EQ(classify_lock_level(100), LockKind::master);
  EXPECT_EQ(classify_lock_level(250), LockKind::master);
  EXPECT_EQ(classify_lock_level(251), LockKind::barred);
  EXPECT_EQ(classify_lock_level(254), LockKind::inaccessible);
  EXPECT_EQ(classify_lock_level(255), LockKind::requires_key);
}

TEST_F(UiRecordsCApi, TerminalConditionStringsAreExposedForMenuItemRows) {
  radicle_terminal_info* info = nullptr;
  ASSERT_EQ(radicle_terminal_decode(env_, winner(k_term_strings), &info), RADICLE_STATUS_OK);
  ASSERT_EQ(info->menu_item_count, 1u);
  ASSERT_EQ(info->menu_items[0].condition_count, 1u);
  EXPECT_STREQ(info->menu_items[0].conditions[0].parameter1_string, "first text");
  EXPECT_STREQ(info->menu_items[0].conditions[0].parameter2_string, "second text");
  radicle_terminal_free(info);
}

TEST_F(UiRecordsCApi, TerminalDecodeExposesItemsAndFreeReleasesThem) {
  radicle_terminal_info* info = nullptr;
  ASSERT_EQ(radicle_terminal_decode(env_, winner(k_term), &info), RADICLE_STATUS_OK);
  ASSERT_NE(info, nullptr);
  EXPECT_EQ(info->full_name.kind, RADICLE_NAME_INLINE);
  EXPECT_STREQ(info->full_name.text, "Terminal");
  ASSERT_EQ(info->menu_item_count, 2u);
  EXPECT_EQ(info->menu_items[0].type, RADICLE_TERMINAL_ITEM_SUBMENU);
  EXPECT_EQ(info->menu_items[0].submenu.local_id, k_term_sub);
  EXPECT_EQ(info->menu_items[0].condition_count, 2u);
  ASSERT_NE(info->menu_items[0].conditions, nullptr);
  EXPECT_EQ(info->menu_items[0].conditions[0].function, k_has_perk);
  EXPECT_EQ(info->menu_items[0].condition_links[0].param1.local_id, k_perk_two);
  EXPECT_STREQ(info->menu_items[0].condition_links[0].param1.plugin, "Base.esm");
  EXPECT_EQ(info->menu_items[0].condition_links[1].global.local_id, k_cat_a);
  EXPECT_EQ(info->menu_items[1].conditions, nullptr);
  EXPECT_EQ(info->menu_items[1].condition_links, nullptr);
  ASSERT_NE(info->body_texts[0].conditions, nullptr);
  EXPECT_EQ(info->body_texts[0].condition_links[0].param1.local_id, k_perk_one);
  EXPECT_EQ(info->body_texts[1].conditions, nullptr);
  EXPECT_EQ(info->menu_items[1].submenu.plugin, nullptr);
  ASSERT_EQ(info->holotape_count, 1u);
  EXPECT_EQ(info->holotapes[0].count, 2);
  radicle_terminal_free(info);
}

TEST_F(UiRecordDecode, ConstructibleObjectReadsPerkConditionsWithMasterResolvedKeys) {
  const auto cobj = fo4::forms::decode_constructible_object(plugin(k_cobj_gated), record(k_cobj_gated));
  ASSERT_EQ(cobj.conditions.size(), 3u);
  ASSERT_EQ(cobj.condition_links.size(), 3u);
  EXPECT_EQ(cobj.conditions[0].function, k_has_perk);
  EXPECT_EQ(cobj.conditions[0].flags_byte, 0);
  EXPECT_FLOAT_EQ(cobj.conditions[0].comparison_float(), 1.0f);
  EXPECT_EQ(cobj.conditions[0].param1, static_cast<std::int32_t>(k_perk_one));
  EXPECT_EQ(cobj.condition_links[0].param1, key(k_perk_one));
  EXPECT_FALSE(cobj.condition_links[0].global.has_value());
  EXPECT_EQ(cobj.condition_links[1].param1, key(k_perk_two));
  EXPECT_TRUE(cobj.conditions[2].use_global());
  EXPECT_EQ(cobj.condition_links[2].global, key(k_cat_a));
  EXPECT_TRUE(fo4::forms::decode_constructible_object(plugin(k_cobj), record(k_cobj)).conditions.empty());
}

TEST_F(UiRecordDecode, MiscReadsScrapComponentsInOrderAndDropsNullLinks) {
  const auto misc = fo4::forms::decode_misc_info(plugin(k_misc_scrap), record(k_misc_scrap));
  EXPECT_EQ(misc.value, 4);
  EXPECT_FLOAT_EQ(*misc.weight, 3.0f);
  const std::vector<fo4::forms::ComponentEntry> expected{
      {key(k_cmpo_item), 2}, {key(k_cmpo_three), 2}, {key(k_cmpo_two), 2}};
  EXPECT_EQ(misc.components, expected);
  EXPECT_TRUE(fo4::forms::decode_misc_info(plugin(k_misc_item), record(k_misc_item)).components.empty());
}

TEST_F(UiRecordsCApi, ConstructibleObjectDecodeExposesConditionRowsAndResolvedLinks) {
  radicle_constructible_object* object = nullptr;
  ASSERT_EQ(radicle_constructible_object_decode(env_, winner(k_cobj_gated), &object), RADICLE_STATUS_OK);
  ASSERT_EQ(object->condition_count, 3u);
  EXPECT_EQ(object->conditions[0].function, k_has_perk);
  EXPECT_EQ(static_cast<std::uint32_t>(object->conditions[1].param1), k_perk_two);
  EXPECT_EQ(object->condition_links[1].param1.local_id, k_perk_two);
  EXPECT_STREQ(object->condition_links[1].param1.plugin, "Base.esm");
  EXPECT_EQ(object->condition_links[2].global.local_id, k_cat_a);
  EXPECT_EQ(object->condition_links[0].reference.plugin, nullptr);
  radicle_constructible_object_free(object);
  ASSERT_EQ(radicle_constructible_object_decode(env_, winner(k_cobj), &object), RADICLE_STATUS_OK);
  EXPECT_EQ(object->condition_count, 0u);
  EXPECT_EQ(object->conditions, nullptr);
  radicle_constructible_object_free(object);
}

// Constraint: detects the MISC component rows dropping an entry or reading a link or count from the wrong CVPA pair; the null slot at index 2 stays counted, which the decoder dropped.
TEST_F(UiRecordsCApi, MiscRowsExposeScrapComponents) {
  ASSERT_EQ(list_size(k_misc_scrap, "MISC\\Components"), 4u);
  EXPECT_EQ(field(k_misc_scrap, "MISC\\ComponentObject", 0), link(k_cmpo_item));
  EXPECT_EQ(field(k_misc_scrap, "MISC\\ComponentObject", 1), link(k_cmpo_three));
  EXPECT_EQ(field(k_misc_scrap, "MISC\\ComponentObject", 3), link(k_cmpo_two));
  EXPECT_EQ(field(k_misc_scrap, "MISC\\ComponentCount", 3), "2");
}

TEST_F(UiRecordsCApi, ConstructibleObjectDecodeExposesLinksAndComponents) {
  radicle_constructible_object* object = nullptr;
  ASSERT_EQ(radicle_constructible_object_decode(env_, winner(k_cobj), &object), RADICLE_STATUS_OK);
  EXPECT_EQ(object->workbench_keyword.local_id, k_workbench);
  EXPECT_EQ(object->category_keyword_count, 2u);
  ASSERT_EQ(object->component_count, 1u);
  EXPECT_EQ(object->components[0].count, 4u);
  EXPECT_EQ(object->created_count, 3);
  radicle_constructible_object_free(object);
}

TEST_F(UiRecordsCApi, FormLinksReportEachLinkWithItsPerSignatureOrdinal) {
  radicle_form_links* links = nullptr;
  ASSERT_EQ(radicle_record_form_links(env_, winner(k_cobj), &links), RADICLE_STATUS_OK);
  size_t fnam = 0;
  size_t last_fnam = 0;
  for (size_t i = 0; i < links->link_count; ++i) {
    if (std::string(links->links[i].subrecord) != "FNAM") continue;
    ++fnam;
    if (links->links[i].offset == 8) last_fnam = links->links[i].key.local_id;
  }
  EXPECT_GE(fnam, 2u);
  EXPECT_EQ(last_fnam, k_cat_b);
  radicle_form_links_free(links);
}

// Constraint: detects MISC Value reading as unsigned, so -2 would come back as 4294967294, or Weight reading the wrong DATA word.
TEST_F(UiRecordsCApi, MiscRowsReportSignedValueAndWeight) {
  EXPECT_EQ(field(k_misc_item, "MISC\\Value"), "-2");
  EXPECT_EQ(field(k_misc_item, "MISC\\Weight"), "1.5");
}

// Constraint: detects the PERK header rows reading the DATA inside a PRKE effect run instead of the header DATA, or the next-perk link or inline FULL name going missing on a perk with effects.
TEST_F(UiRecordsCApi, PerkRowsExposeRankLinkAndDataEvenWithEffects) {
  EXPECT_EQ(field(k_perk_effects, "PERK\\Level"), "2");
  EXPECT_EQ(field(k_perk_effects, "PERK\\NumRanks"), "3");
  EXPECT_EQ(field(k_perk_effects, "PERK\\Playable"), "1");
  EXPECT_EQ(field(k_perk_effects, "PERK\\NextPerk"), link(k_perk_two));
  radicle_record_info info{};
  ASSERT_EQ(radicle_record_get_info(env_, winner(k_perk_effects), &info), RADICLE_STATUS_OK);
  EXPECT_EQ(info.name_kind, RADICLE_NAME_INLINE);
  EXPECT_STREQ(info.name_text, "Effects");
}

TEST_F(UiRecordsCApi, PerkEntryPointsDecodeExposesEffectsTabsAndFreeReleasesThem) {
  radicle_perk_entry_points_info* info = nullptr;
  ASSERT_EQ(radicle_perk_entry_points_decode(env_, winner(k_perk_barter), &info), RADICLE_STATUS_OK);
  EXPECT_EQ(info->record.local_id, k_perk_barter);
  ASSERT_EQ(info->effect_count, 3u);
  const auto& buy = info->effects[0];
  EXPECT_EQ(buy.entry_point, k_entry_mod_buy_prices);
  EXPECT_EQ(buy.function, 3);
  EXPECT_EQ(buy.rank, 1);
  EXPECT_EQ(buy.priority, 5);
  EXPECT_EQ(buy.has_parameter_type, 1);
  EXPECT_EQ(buy.parameter_type, 1);
  EXPECT_EQ(buy.has_value, 1);
  EXPECT_FLOAT_EQ(buy.value, 0.5f);
  ASSERT_EQ(buy.tab_count, 1u);
  EXPECT_EQ(buy.tabs[0].run_on, 0);
  ASSERT_EQ(buy.tabs[0].condition_count, 1u);
  EXPECT_EQ(buy.tabs[0].conditions[0].function, k_has_perk);
  EXPECT_EQ(buy.tabs[0].condition_links[0].param1.local_id, k_perk_one);
  EXPECT_STREQ(buy.tabs[0].condition_links[0].param1.plugin, "Base.esm");
  EXPECT_EQ(info->effects[1].entry_point, k_entry_mod_sell_prices);
  EXPECT_EQ(info->effects[1].tab_count, 0u);
  EXPECT_EQ(info->effects[1].tabs, nullptr);
  EXPECT_FLOAT_EQ(info->effects[1].value, 1.5f);
  EXPECT_EQ(info->effects[2].has_value, 0);
  radicle_perk_entry_points_free(info);
  ASSERT_EQ(radicle_perk_entry_points_decode(env_, winner(k_perk_two), &info), RADICLE_STATUS_OK);
  EXPECT_EQ(info->effect_count, 0u);
  EXPECT_EQ(info->effects, nullptr);
  radicle_perk_entry_points_free(info);
}

// Constraint: detects the generic PERK effect rows drifting from the typed entry-point decode, such as flat DATA rows counting the header DATA, or EPFT, EPFD and PRKC reads shifting when an effect omits its optional subrecords.
TEST_F(UiRecordsCApi, PerkEffectRowsReadEachEntryPointEffectLikeTheTypedDecode) {
  radicle_perk_entry_points_info* info = nullptr;
  ASSERT_EQ(radicle_perk_entry_points_decode(env_, winner(k_perk_barter), &info), RADICLE_STATUS_OK);
  ASSERT_EQ(info->effect_count, 3u);
  ASSERT_EQ(list_size(k_perk_barter, "PERK\\Effects"), 4u);
  std::size_t tabs_before = 0;
  std::size_t typed = 0;
  for (std::size_t effect = 0; effect < 4; ++effect) {
    const auto scope = "PERK\\Effects[" + std::to_string(effect) + "]\\";
    const auto tabs = list_size(k_perk_barter, (scope + "Tabs").c_str());
    if (field(k_perk_barter, "PERK\\EffectType", effect) != "2") {
      tabs_before += tabs;
      continue;
    }
    ASSERT_LT(typed, 3u);
    const auto& expected = info->effects[typed++];
    EXPECT_EQ(field(k_perk_barter, "PERK\\EffectRank", effect), std::to_string(expected.rank));
    EXPECT_EQ(field(k_perk_barter, "PERK\\EffectPriority", effect), std::to_string(expected.priority));
    EXPECT_EQ(field(k_perk_barter, "PERK\\EffectEntryPoint", effect), std::to_string(expected.entry_point));
    EXPECT_EQ(field(k_perk_barter, "PERK\\EffectFunction", effect), std::to_string(expected.function));
    EXPECT_EQ(field(k_perk_barter, "PERK\\EffectTabCount", effect), std::to_string(expected.tab_count));
    ASSERT_EQ(list_size(k_perk_barter, (scope + "ParameterTypes").c_str()), expected.has_parameter_type ? 1u : 0u);
    if (expected.has_parameter_type) {
      EXPECT_EQ(field(k_perk_barter, (scope + "ParameterTypes").c_str()), expected.parameter_type == 1 ? "01" : "02");
      const auto data = field(k_perk_barter, (scope + "ParameterData").c_str());
      if (expected.has_value) {
        std::uint32_t bits = 0;
        for (int byte = 3; byte >= 0; --byte) {
          bits = (bits << 8) | static_cast<std::uint32_t>(std::stoul(data.substr(byte * 2, 2), nullptr, 16));
        }
        float value = 0.0f;
        std::memcpy(&value, &bits, sizeof(value));
        EXPECT_FLOAT_EQ(value, expected.value);
      } else {
        EXPECT_EQ(data.size(), 16u);
      }
    }
    ASSERT_EQ(tabs, expected.tab_count);
    for (std::size_t tab = 0; tab < tabs; ++tab) {
      EXPECT_EQ(field(k_perk_barter, "PERK\\TabRunOn", tabs_before + tab), std::to_string(expected.tabs[tab].run_on));
      const auto condition_scope = "Tabs[" + std::to_string(tabs_before + tab) + "].Conditions";
      const radicle_condition_list* list = nullptr;
      ASSERT_EQ(radicle_record_conditions(env_, winner(k_perk_barter), condition_scope.c_str(), &list), RADICLE_STATUS_OK);
      ASSERT_EQ(list->count, expected.tabs[tab].condition_count);
      EXPECT_EQ(list->rows[0].function, expected.tabs[tab].conditions[0].function);
      radicle_condition_list_free(list);
    }
    tabs_before += tabs;
  }
  EXPECT_EQ(typed, 3u);
  radicle_perk_entry_points_free(info);
}

TEST_F(UiRecordsCApi, PerkEntryPointsDecodeRejectsTheWrongSignatureAndNullArguments) {
  radicle_perk_entry_points_info* info = nullptr;
  EXPECT_EQ(radicle_perk_entry_points_decode(env_, winner(k_cobj), &info), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(info, nullptr);
  EXPECT_EQ(radicle_perk_entry_points_decode(env_, winner(k_perk_barter_short_ctda), &info), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(info, nullptr);
  EXPECT_EQ(radicle_perk_entry_points_decode(env_, winner(k_perk_barter), nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_perk_entry_points_decode(nullptr, winner(k_perk_barter), &info), RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_perk_entry_points_free(nullptr);
}

TEST_F(UiRecordsCApi, OmodPropertiesDecodeExposesRawRowsFormValuesAndFreeReleasesThem) {
  radicle_omod_properties_info* info = nullptr;
  ASSERT_EQ(radicle_omod_properties_decode(env_, winner(k_omod_props), &info), RADICLE_STATUS_OK);
  EXPECT_EQ(info->record.local_id, k_omod_props);
  ASSERT_EQ(info->property_count, 3u);
  const auto& scalar = info->properties[0];
  EXPECT_EQ(scalar.value_type, 1);
  EXPECT_EQ(scalar.function_type, 0);
  EXPECT_EQ(scalar.property, 3);
  float value1 = 0.0f;
  std::memcpy(&value1, scalar.value1, sizeof(value1));
  EXPECT_FLOAT_EQ(value1, 1.5f);
  EXPECT_FLOAT_EQ(scalar.step, 0.25f);
  EXPECT_EQ(scalar.value1_form.plugin, nullptr);
  const auto& form = info->properties[1];
  EXPECT_EQ(form.function_type, 2);
  std::uint32_t value2 = 0;
  std::memcpy(&value2, form.value2, sizeof(value2));
  EXPECT_EQ(value2, 7u);
  EXPECT_EQ(form.value1_form.local_id, k_workbench);
  EXPECT_STREQ(form.value1_form.plugin, "Base.esm");
  const auto& integer = info->properties[2];
  EXPECT_EQ(integer.property, 0x0102);
  EXPECT_EQ(integer.value1_form.plugin, nullptr);
  radicle_omod_properties_free(info);
  ASSERT_EQ(radicle_omod_properties_decode(env_, winner(k_omod_empty), &info), RADICLE_STATUS_OK);
  EXPECT_EQ(info->property_count, 0u);
  EXPECT_EQ(info->properties, nullptr);
  radicle_omod_properties_free(info);
}

TEST_F(UiRecordsCApi, OmodPropertiesDecodeRejectsTheWrongSignatureMalformedDataAndNullArguments) {
  radicle_omod_properties_info* info = nullptr;
  EXPECT_EQ(radicle_omod_properties_decode(env_, winner(k_cobj), &info), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(info, nullptr);
  EXPECT_EQ(radicle_omod_properties_decode(env_, winner(k_omod_no_data), &info), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(radicle_omod_properties_decode(env_, winner(k_omod_short_data), &info), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(info, nullptr);
  EXPECT_EQ(radicle_omod_properties_decode(env_, winner(k_omod_props), nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_omod_properties_decode(nullptr, winner(k_omod_props), &info), RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_omod_properties_free(nullptr);
}

// Constraint: detects the NOTE type, program file, value and weight rows reading the wrong subrecord, and a NOTE row answering for a COBJ record.
TEST_F(UiRecordsCApi, NoteRowsExposeTypeProgramAndValue) {
  EXPECT_EQ(field(k_note, "NOTE\\Type"), "2");
  EXPECT_EQ(field(k_note, "NOTE\\ProgramFile"), "game.swf");
  EXPECT_EQ(field(k_note, "NOTE\\Value"), "5");
  EXPECT_EQ(field(k_note, "NOTE\\Weight"), "0.5");
  std::string text;
  EXPECT_EQ(read_field(k_cobj, "NOTE\\Type", 0, text), RADICLE_STATUS_INVALID_ARGUMENT);
}

// Constraint: detects the CMPO auto calc value or scrap item row reading the wrong subrecord, or an absent mod scrap scalar reading as present.
TEST_F(UiRecordsCApi, ComponentRowsExposeAutoCalcValueAndScrapItem) {
  EXPECT_EQ(field(k_cmpo_item, "CMPO\\AutoCalcValue"), "3");
  EXPECT_EQ(field(k_cmpo_item, "CMPO\\ScrapItem"), link(k_misc_item));
  std::string text;
  EXPECT_EQ(read_field(k_cmpo_item, "CMPO\\ModScrapScalar", 0, text), RADICLE_STATUS_NOT_FOUND);
}

TEST_F(UiRecordsCApi, ConstructibleObjectDecodeDropsNullComponentsAndRejectsBadFnam) {
  radicle_constructible_object* object = nullptr;
  ASSERT_EQ(radicle_constructible_object_decode(env_, winner(k_cobj_null_component), &object), RADICLE_STATUS_OK);
  EXPECT_EQ(object->component_count, 0u);
  radicle_constructible_object_free(object);
  EXPECT_EQ(radicle_constructible_object_decode(env_, winner(k_cobj_bad_fnam), &object), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(object, nullptr);
}

TEST_F(UiRecordsCApi, FormLinksFreeAcceptsNullAndAnEmptyResultAndRejectsNullArguments) {
  radicle_form_links_free(nullptr);
  radicle_form_links* links = nullptr;
  ASSERT_EQ(radicle_record_form_links(env_, winner(k_note_bare), &links), RADICLE_STATUS_OK);
  ASSERT_NE(links, nullptr);
  EXPECT_EQ(links->link_count, 0u);
  EXPECT_EQ(links->links, nullptr);
  radicle_form_links_free(links);
  EXPECT_EQ(radicle_record_form_links(nullptr, winner(k_note_bare), &links), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(links, nullptr);
}

TEST_F(UiRecordsCApi, LockInfoReadReportsLevelKindKeyAndLeveledFlag) {
  radicle_lock_info info{};
  ASSERT_EQ(radicle_lock_info_read(env_, winner(k_refr_locked), &info), RADICLE_STATUS_OK);
  EXPECT_EQ(info.has_lock_data, 1);
  EXPECT_EQ(info.level, 75);
  EXPECT_EQ(info.kind, RADICLE_LOCK_EXPERT);
  EXPECT_EQ(info.key.local_id, k_misc_item);
  EXPECT_EQ(info.flags, 0x04);
  EXPECT_EQ(info.leveled_lock, 1);
  ASSERT_EQ(radicle_lock_info_read(env_, winner(k_refr_unlocked), &info), RADICLE_STATUS_OK);
  EXPECT_EQ(info.has_lock_data, 0);
  EXPECT_EQ(info.kind, RADICLE_LOCK_NONE);
  ASSERT_EQ(radicle_lock_info_read(env_, winner(k_refr_short_lock), &info), RADICLE_STATUS_OK);
  EXPECT_EQ(info.has_lock_data, 0);
}

TEST_F(UiRecordsCApi, InteractionDecodesExposeItemsDestinationsMarkersAndTexts) {
  radicle_activator_info* acti = nullptr;
  ASSERT_EQ(radicle_activator_decode(env_, winner(k_acti), &acti), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_STREQ(acti->activate_text.text, "Pull");
  EXPECT_EQ(acti->has_flags, 1);
  EXPECT_EQ(acti->interaction_keyword.local_id, k_workbench);
  radicle_activator_free(acti);

  radicle_container_info* cont = nullptr;
  ASSERT_EQ(radicle_container_decode(env_, winner(k_cont), &cont), RADICLE_STATUS_OK) << radicle_last_error();
  ASSERT_EQ(cont->item_count, 2u);
  EXPECT_EQ(cont->items[0].item.local_id, k_misc_item);
  EXPECT_EQ(cont->items[1].count, -1);
  EXPECT_FLOAT_EQ(cont->weight, 12.5f);
  EXPECT_EQ(cont->open_sound.local_id, k_created);
  radicle_container_free(cont);

  radicle_door_info* door = nullptr;
  ASSERT_EQ(radicle_door_decode(env_, winner(k_door), &door), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(door->flags, 0x0A);
  EXPECT_STREQ(door->open_text.text, "Unlock");
  ASSERT_EQ(door->random_destination_count, 2u);
  EXPECT_EQ(door->random_destinations[1].local_id, k_cmpo_item);
  radicle_door_free(door);

  radicle_furniture_info* furn = nullptr;
  ASSERT_EQ(radicle_furniture_decode(env_, winner(k_furn), &furn), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(furn->bench_type, 7);
  EXPECT_EQ(furn->item_count, 1u);
  ASSERT_EQ(furn->marker_count, 2u);
  EXPECT_EQ(furn->markers[0].has_disabled_entry_points, 1);
  EXPECT_EQ(furn->markers[1].has_disabled_entry_points, 0);
  ASSERT_EQ(furn->entry_point_count, 2u);
  EXPECT_EQ(furn->entry_points[1].anim_type, 4);
  ASSERT_EQ(furn->marker_param_count, 2u);
  EXPECT_EQ(furn->marker_params[0].entry_types, 0x05);
  EXPECT_EQ(furn->marker_params[1].keyword.plugin, nullptr);
  EXPECT_STREQ(furn->marker_model, "Chair.nif");
  radicle_furniture_free(furn);
}

TEST_F(UiRecordsCApi, InteractionDecodesRejectTheWrongSignatureAndMalformedRecordsAndNullOutput) {
  radicle_door_info* door = nullptr;
  EXPECT_EQ(radicle_door_decode(env_, winner(k_cont), &door), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(door, nullptr);
  radicle_furniture_info* furn = nullptr;
  EXPECT_EQ(radicle_furniture_decode(env_, winner(k_furn_bad_snam), &furn), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(furn, nullptr);
  radicle_container_info* cont = nullptr;
  EXPECT_EQ(radicle_container_decode(env_, winner(k_cont_bad_cnto), &cont), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(radicle_activator_decode(env_, winner(k_acti), nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_activator_free(nullptr);
  radicle_container_free(nullptr);
  radicle_door_free(nullptr);
  radicle_furniture_free(nullptr);
}

TEST_F(UiRecordsCApi, LockInfoReadRejectsANonPlacedRecordAndNullArguments) {
  radicle_lock_info info{};
  EXPECT_EQ(radicle_lock_info_read(env_, winner(k_misc_item), &info), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_lock_info_read(env_, winner(k_refr_locked), nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_lock_info_read(nullptr, winner(k_refr_locked), &info), RADICLE_STATUS_INVALID_ARGUMENT);
}
