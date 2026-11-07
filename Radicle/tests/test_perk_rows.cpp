#include "fo4/forms/perk.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/subrecord_authoring.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/preflight.hpp"
#include "fo4/schema/record_constraints.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using fo4::test::ByteWriter;

constexpr auto k_perk = "PERK";
constexpr auto k_key = "000D01:Patch.esp";
constexpr auto k_sound_key = "000D02:Patch.esp";
constexpr auto k_next_key = "000D03:Patch.esp";

constexpr std::uint32_t raw_id(std::uint32_t local_id) { return 0x01000000u | local_id; }

Bytes zstr(std::string_view text) {
  ByteWriter out;
  out.raw(text);
  out.u8(0);
  return out.bytes;
}

Bytes u32_bytes(std::uint32_t value) {
  ByteWriter out;
  out.u32(value);
  return out.bytes;
}

std::uint32_t raw_at(const Bytes& data, std::size_t offset) {
  std::uint32_t value = 0;
  for (std::size_t index = 0; index < 4; ++index) {
    value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[offset + index]))
             << (8 * index);
  }
  return value;
}

void append_record(ByteWriter& out, std::string_view signature, std::uint32_t form_id,
                   std::string_view editor_id) {
  const auto group = fo4::test::begin_group(out, signature, 0);
  const auto record = fo4::test::begin_record(out, signature, 0, form_id);
  fo4::test::write_subrecord(out, "EDID", zstr(editor_id));
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
}

Bytes perk_data() {
  return Bytes{std::byte{0x01}, std::byte{0x02}, std::byte{0x04}, std::byte{0x01},
               std::byte{0x00}};
}

Bytes effect_data() {
  return Bytes{std::byte{0x00}, std::byte{0x00}, std::byte{0x00}};
}

Bytes header_data() {
  return Bytes{std::byte{0x00}, std::byte{0x00}, std::byte{0x01}, std::byte{0x01},
               std::byte{0x00}};
}

Bytes make_plugin(Bytes data, bool with_data = true) {
  ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(4);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Fallout4.esm\0", 13});
  fo4::test::write_subrecord(out, "DATA", Bytes(8));
  fo4::test::end_record(out, tes4);

  append_record(out, "SNDR", raw_id(0x0D02), "RadicleSound");
  append_record(out, "PERK", raw_id(0x0D03), "RadicleNextPerk");

  const auto group = fo4::test::begin_group(out, "PERK", 0);
  const auto record = fo4::test::begin_record(out, "PERK", 0, raw_id(0x0D01));
  fo4::test::write_subrecord(out, "EDID", zstr("RadiclePerk"));
  fo4::test::write_subrecord(out, "ICON", zstr("Interface/perk.swf"));
  if (with_data) {
    fo4::test::write_subrecord(out, "DATA", data);
  }
  fo4::test::write_subrecord(out, "SNAM", u32_bytes(0x00000A20));
  fo4::test::write_subrecord(out, "NNAM", u32_bytes(0x00000A21));
  fo4::test::write_subrecord(out, "FNAM", zstr("PerkMenu"));
  if (with_data) {
    fo4::test::write_subrecord(out, "DATA", effect_data());
  }
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

Bytes sub_bytes(const fo4::Plugin& plugin, std::string_view subrecord) {
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(k_key));
  if (record == nullptr) {
    ADD_FAILURE() << k_key;
    return {};
  }
  for (const auto& sub : record->subrecords) {
    if (sub.signature.to_string() == subrecord) return sub.data;
  }
  ADD_FAILURE() << subrecord;
  return {};
}

std::optional<fo4::schema::ScalarFieldDescriptor> scalar_field(std::string_view path) {
  return fo4::schema::scalar_field_descriptor(fo4::Signature::from_string(k_perk), path);
}

std::optional<fo4::schema::FormLinkFieldDescriptor> formlink_field(std::string_view path) {
  return fo4::schema::formlink_field_descriptor(fo4::Signature::from_string(k_perk), path);
}

}  // namespace

TEST(PerkRows, HeaderDataScalarsResolveOnTheHeaderDataOnly) {
  const auto trait = scalar_field("PERK\\Trait");
  const auto level = scalar_field("PERK\\Level");
  const auto ranks = scalar_field("PERK\\NumRanks");
  const auto playable = scalar_field("PERK\\Playable");
  const auto hidden = scalar_field("PERK\\Hidden");
  ASSERT_TRUE(trait.has_value());
  ASSERT_TRUE(level.has_value());
  ASSERT_TRUE(ranks.has_value());
  ASSERT_TRUE(playable.has_value());
  ASSERT_TRUE(hidden.has_value());
  EXPECT_EQ(trait->scalar.subrecord, fo4::Signature::from_string("DATA"));
  EXPECT_EQ(trait->scalar.offset, std::size_t{0});
  EXPECT_EQ(level->scalar.offset, std::size_t{1});
  EXPECT_EQ(ranks->scalar.offset, std::size_t{2});
  EXPECT_EQ(playable->scalar.offset, std::size_t{3});
  EXPECT_EQ(hidden->scalar.offset, std::size_t{4});
  EXPECT_EQ(playable->scalar.minimum_payload_size, std::size_t{4});
  EXPECT_EQ(hidden->scalar.minimum_payload_size, std::size_t{5});

  auto plugin = fo4::read_plugin(make_plugin(perk_data()), "Patch.esp");
  const auto key = fo4::FormKey::parse(k_key);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *trait, "0"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *level, "7"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *ranks, "3"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *playable, "0"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *hidden, "1"),
            fo4::ScalarMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* record = fo4::find_record(reread, key);
  ASSERT_NE(record, nullptr);
  std::vector<const fo4::Subrecord*> data;
  for (const auto& sub : record->subrecords) {
    if (sub.signature == fo4::Signature::from_string("DATA")) data.push_back(&sub);
  }
  ASSERT_EQ(data.size(), std::size_t{2});
  EXPECT_EQ(data[0]->data,
            (Bytes{std::byte{0x00}, std::byte{0x07}, std::byte{0x03}, std::byte{0x00},
                   std::byte{0x01}}));
  EXPECT_EQ(data[1]->data, effect_data());
}

TEST(PerkRows, HeaderStringsAndLinksRoundTrip) {
  auto plugin = fo4::read_plugin(make_plugin(perk_data()), "Patch.esp");
  const auto key = fo4::FormKey::parse(k_key);
  const auto image = scalar_field("PERK\\Image");
  const auto swf = scalar_field("PERK\\SWF");
  const auto sound = formlink_field("PERK\\Sound");
  const auto next = formlink_field("PERK\\NextPerk");
  ASSERT_TRUE(image.has_value());
  ASSERT_TRUE(swf.has_value());
  ASSERT_TRUE(sound.has_value());
  ASSERT_TRUE(next.has_value());
  EXPECT_EQ(image->scalar.subrecord, fo4::Signature::from_string("ICON"));
  EXPECT_EQ(swf->scalar.subrecord, fo4::Signature::from_string("FNAM"));
  EXPECT_EQ(image->scalar.storage, fo4::schema::ScalarStorage::zstring);
  EXPECT_EQ(sound->subrecord, fo4::Signature::from_string("SNAM"));
  EXPECT_EQ(next->subrecord, fo4::Signature::from_string("NNAM"));
  EXPECT_FALSE(sound->nullable);
  EXPECT_TRUE(next->nullable);
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(
      *next, fo4::Signature::from_string("PERK")));
  EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(
      *sound, fo4::Signature::from_string("PERK")));

  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key, *sound, 0,
                                           fo4::FormKey::parse(k_sound_key)),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_THROW(fo4::set_record_formlink_field(plugin, key, *sound, 0,
                                              fo4::FormKey::parse(k_next_key)),
               fo4::schema::FormLinkFieldError);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key, *next, 0,
                                           fo4::FormKey::parse(k_next_key)),
            fo4::FormLinkFieldMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(raw_at(sub_bytes(reread, "SNAM"), 0), 0x01000D02u);
  EXPECT_EQ(raw_at(sub_bytes(reread, "NNAM"), 0), 0x01000D03u);
  EXPECT_EQ(std::string(reinterpret_cast<const char*>(sub_bytes(reread, "ICON").data())),
            "Interface/perk.swf");
  EXPECT_EQ(std::string(reinterpret_cast<const char*>(sub_bytes(reread, "FNAM").data())),
            "PerkMenu");
}

TEST(PerkRows, AuthoringListsTheHeaderRulesAndKeepsTheRequiredData) {
  const auto constraint =
      fo4::schema::record_authoring_constraint(fo4::Signature::from_string(k_perk));
  ASSERT_TRUE(constraint.has_value());
  EXPECT_EQ(constraint->placement, fo4::schema::RecordPlacement::top_level);
  constexpr std::string_view k_order[] = {"EDID", "VMAD", "FULL", "DESC", "ICON", "DATA"};
  const auto rules = fo4::schema::authoring_subrecords(*constraint);
  ASSERT_EQ(rules.size(), 6u);
  for (std::size_t index = 0; index < rules.size(); ++index) {
    SCOPED_TRACE(index);
    EXPECT_EQ(rules[index].signature, fo4::Signature::from_string(k_order[index]));
  }
  EXPECT_EQ(rules[5].minimum, 1);
  EXPECT_EQ(rules[5].maximum, 65535);
  EXPECT_TRUE(rules[5].uncounted);
  EXPECT_TRUE(rules[5].has_default());
  EXPECT_EQ(rules[5].default_size, 5);

  auto plugin = fo4::read_plugin(make_plugin(perk_data()), "Patch.esp");
  const auto created =
      fo4::create_record(plugin, fo4::Signature::from_string(k_perk), "RadicleNewPerk");
  EXPECT_EQ(created.status, fo4::RecordCreateStatus::created);
  const auto* record = fo4::find_record(plugin, created.key);
  ASSERT_NE(record, nullptr);
  ASSERT_EQ(record->subrecords.size(), 2u);
  EXPECT_EQ(record->subrecords[0].signature, fo4::Signature::from_string("EDID"));
  EXPECT_EQ(record->subrecords[1].signature, fo4::Signature::from_string("DATA"));
  EXPECT_EQ(record->subrecords[1].data, header_data());
}

TEST(PerkRows, PreflightAcceptsAnEffectDataSubrecordAfterSwf) {
  auto plugin = fo4::read_plugin(make_plugin(perk_data()), "Patch.esp");
  const auto issues =
      fo4::schema::preflight_authoring_record(plugin, fo4::FormKey::parse(k_key));
  for (const auto& issue : issues) {
    ADD_FAILURE() << issue.path << ' ' << issue.code;
  }
}

TEST(PerkRows, PreflightRefusesAPerkWithoutAnyDataSubrecord) {
  auto plugin = fo4::read_plugin(make_plugin(perk_data(), false), "Patch.esp");
  const auto issues =
      fo4::schema::preflight_authoring_record(plugin, fo4::FormKey::parse(k_key));
  ASSERT_EQ(issues.size(), 1u);
  EXPECT_EQ(issues[0].code, "required_subrecord_missing");
  EXPECT_EQ(issues[0].path, "PERK\\DATA");
}

namespace {

struct RawSubrecord {
  std::string_view signature;
  Bytes data;
};

Bytes plugin_with_perk(std::vector<RawSubrecord> body) {
  ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(4);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Fallout4.esm\0", 13});
  fo4::test::write_subrecord(out, "DATA", Bytes(8));
  fo4::test::end_record(out, tes4);

  append_record(out, "QUST", raw_id(0x0D10), "RadicleQuest");
  append_record(out, "AVIF", raw_id(0x0D11), "RadicleActorValue");
  append_record(out, "SPEL", raw_id(0x0D12), "RadicleAbility");

  const auto group = fo4::test::begin_group(out, "PERK", 0);
  const auto record = fo4::test::begin_record(out, "PERK", 0, raw_id(0x0D01));
  fo4::test::write_subrecord(out, "EDID", zstr("RadiclePerk"));
  for (const auto& sub : body) fo4::test::write_subrecord(out, sub.signature, sub.data);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

Bytes prke(std::uint8_t type, std::uint8_t rank, std::uint8_t priority) {
  ByteWriter out;
  out.u8(type);
  out.u8(rank);
  out.u8(priority);
  return out.bytes;
}

Bytes prkc(std::int8_t run_on) {
  ByteWriter out;
  out.u8(static_cast<std::uint8_t>(run_on));
  return out.bytes;
}

Bytes ctda_run_bytes(std::uint16_t function) {
  ByteWriter out;
  out.u32(0);
  out.u32(0);
  out.u16(function);
  out.u16(0);
  out.i32(0);
  out.i32(0);
  out.u32(0);
  out.u32(0);
  out.i32(-1);
  return out.bytes;
}

Bytes quest_effect_data(std::uint32_t raw, std::uint16_t stage) {
  ByteWriter out;
  out.u32(raw);
  out.u16(stage);
  return out.bytes;
}

Bytes ability_effect_data(std::uint32_t raw) {
  ByteWriter out;
  out.u32(raw);
  return out.bytes;
}

Bytes entry_point_effect_data(std::uint8_t point, std::uint8_t function,
                              std::uint8_t tab_count) {
  ByteWriter out;
  out.u8(point);
  out.u8(function);
  out.u8(tab_count);
  return out.bytes;
}

Bytes epfd_float(float value) {
  ByteWriter out;
  out.f32(value);
  return out.bytes;
}

Bytes epfd_float_pair(float first, float second) {
  ByteWriter out;
  out.f32(first);
  out.f32(second);
  return out.bytes;
}

Bytes epfd_actor_value(std::uint32_t raw, float multiplier) {
  ByteWriter out;
  out.u32(raw);
  out.f32(multiplier);
  return out.bytes;
}

Bytes u16_payload(std::uint16_t value) {
  ByteWriter out;
  out.u16(value);
  return out.bytes;
}

Bytes u8_payload(std::uint8_t value) {
  ByteWriter out;
  out.u8(value);
  return out.bytes;
}

std::vector<std::string> signatures_of(const fo4::Record& record, std::size_t from) {
  std::vector<std::string> out;
  for (std::size_t i = from; i < record.subrecords.size(); ++i) {
    out.push_back(record.subrecords[i].signature.to_string());
  }
  return out;
}

const fo4::Record* perk_record(const fo4::Plugin& plugin) {
  return fo4::find_record(plugin, fo4::FormKey::parse(k_key));
}

}  // namespace

TEST(PerkEffects, DecodesEntryPointEffectSections) {
  auto plugin = fo4::read_plugin(
      plugin_with_perk({{"DATA", header_data()},
                        {"PRKE", prke(2, 1, 3)},
                        {"DATA", entry_point_effect_data(0x26, 1, 1)},
                        {"PRKC", prkc(0)},
                        {"CTDA", ctda_run_bytes(100)},
                        {"EPFT", u8_payload(1)},
                        {"EPFB", u16_payload(0x0102)},
                        {"EPFD", epfd_float(2.5f)},
                        {"PRKF", Bytes{}}}),
      "Patch.esp");
  const auto* record = perk_record(plugin);
  ASSERT_NE(record, nullptr);
  const auto effects = fo4::forms::read_perk_effects(plugin, *record);
  ASSERT_EQ(effects.size(), 1u);
  const auto& effect = effects.front();
  EXPECT_EQ(effect.type, fo4::forms::PerkEffectType::entry_point);
  EXPECT_EQ(effect.rank, 1);
  EXPECT_EQ(effect.priority, 3);
  EXPECT_EQ(effect.entry_point, 0x26);
  EXPECT_EQ(effect.function, 1);
  EXPECT_EQ(effect.condition_tab_count, 1);
  ASSERT_EQ(effect.tabs.size(), 1u);
  EXPECT_EQ(effect.tabs[0].run_on, 0);
  ASSERT_EQ(effect.tabs[0].conditions.size(), 1u);
  EXPECT_EQ(effect.tabs[0].conditions[0].function, 100);
  ASSERT_TRUE(effect.parameters.type.has_value());
  EXPECT_EQ(*effect.parameters.type,
            static_cast<std::uint8_t>(fo4::forms::PerkFunctionType::single_float));
  ASSERT_TRUE(effect.parameters.entry_id.has_value());
  EXPECT_EQ(*effect.parameters.entry_id, 0x0102);
  EXPECT_FALSE(effect.parameters.script_flags.has_value());
  EXPECT_FALSE(effect.parameters.button_label.has_value());
  const auto value = fo4::forms::perk_epfd_float(effect);
  ASSERT_TRUE(value.has_value());
  EXPECT_FLOAT_EQ(*value, 2.5f);
  EXPECT_FALSE(fo4::forms::perk_epfd_float_pair(effect).has_value());
  EXPECT_FALSE(fo4::forms::perk_epfd_form(plugin, effect).has_value());
  EXPECT_FALSE(fo4::forms::perk_epfd_text(effect).has_value());
  EXPECT_FALSE(fo4::forms::perk_epfd_actor_value(plugin, effect).has_value());
}

TEST(PerkEffects, DecodesQuestAbilityAndActorValueData) {
  auto plugin = fo4::read_plugin(
      plugin_with_perk({{"DATA", header_data()},
                        {"PRKE", prke(0, 1, 0)},
                        {"DATA", quest_effect_data(0x01000D10u, 42)},
                        {"PRKF", Bytes{}},
                        {"PRKE", prke(1, 2, 1)},
                        {"DATA", ability_effect_data(0x01000D12u)},
                        {"PRKF", Bytes{}},
                        {"PRKE", prke(2, 0, 0)},
                        {"DATA", entry_point_effect_data(0x3B, 12, 0)},
                        {"EPFT", u8_payload(2)},
                        {"EPFD", epfd_actor_value(0x01000D11u, 1.5f)},
                        {"PRKF", Bytes{}}}),
      "Patch.esp");
  const auto* record = perk_record(plugin);
  ASSERT_NE(record, nullptr);
  const auto effects = fo4::forms::read_perk_effects(plugin, *record);
  ASSERT_EQ(effects.size(), 3u);

  EXPECT_EQ(effects[0].type, fo4::forms::PerkEffectType::quest_stage);
  EXPECT_EQ(effects[0].quest.to_string(), "000D10:Patch.esp");
  EXPECT_EQ(effects[0].quest_stage, 42);
  EXPECT_TRUE(effects[0].tabs.empty());

  EXPECT_EQ(effects[1].type, fo4::forms::PerkEffectType::ability);
  EXPECT_EQ(effects[1].ability.to_string(), "000D12:Patch.esp");
  EXPECT_EQ(effects[1].rank, 2);

  const auto actor_value = fo4::forms::perk_epfd_actor_value(plugin, effects[2]);
  ASSERT_TRUE(actor_value.has_value());
  EXPECT_EQ(actor_value->first.to_string(), "000D11:Patch.esp");
  EXPECT_FLOAT_EQ(actor_value->second, 1.5f);
  EXPECT_FALSE(fo4::forms::perk_epfd_float(effects[2]).has_value());
}

TEST(PerkEffects, DecodesFloatPairAndTextParameterArms) {
  auto plugin = fo4::read_plugin(
      plugin_with_perk({{"DATA", header_data()},
                        {"PRKE", prke(2, 0, 0)},
                        {"DATA", entry_point_effect_data(0x01, 4, 0)},
                        {"EPFT", u8_payload(2)},
                        {"EPFD", epfd_float_pair(1.0f, 4.0f)},
                        {"PRKF", Bytes{}},
                        {"PRKE", prke(2, 0, 0)},
                        {"DATA", entry_point_effect_data(0x02, 11, 0)},
                        {"EPFT", u8_payload(6)},
                        {"EPFD", zstr("Push")},
                        {"PRKF", Bytes{}}}),
      "Patch.esp");
  const auto* record = perk_record(plugin);
  ASSERT_NE(record, nullptr);
  const auto effects = fo4::forms::read_perk_effects(plugin, *record);
  ASSERT_EQ(effects.size(), 2u);
  const auto pair = fo4::forms::perk_epfd_float_pair(effects[0]);
  ASSERT_TRUE(pair.has_value());
  EXPECT_FLOAT_EQ(pair->first, 1.0f);
  EXPECT_FLOAT_EQ(pair->second, 4.0f);
  const auto text = fo4::forms::perk_epfd_text(effects[1]);
  ASSERT_TRUE(text.has_value());
  EXPECT_EQ(*text, "Push");
  EXPECT_FALSE(fo4::forms::perk_epfd_actor_value(plugin, effects[1]).has_value());
}

TEST(PerkEffects, RoundTripThroughTheWriterKeepsEveryEffectByte) {
  const auto original = plugin_with_perk({{"DATA", header_data()},
                                          {"PRKE", prke(2, 1, 3)},
                                          {"DATA", entry_point_effect_data(0x26, 1, 1)},
                                          {"PRKC", prkc(0)},
                                          {"CTDA", ctda_run_bytes(100)},
                                          {"EPFT", u8_payload(4)},
                                          {"EPFB", u16_payload(0x0102)},
                                          {"EPF2", zstr("Press")},
                                          {"EPF3", u16_payload(1)},
                                          {"EPFD", ability_effect_data(0x01000D12u)},
                                          {"PRKF", Bytes{}}});
  auto plugin = fo4::read_plugin(original, "Patch.esp");
  const auto* before = perk_record(plugin);
  ASSERT_NE(before, nullptr);
  const auto effects = fo4::forms::read_perk_effects(plugin, *before);
  ASSERT_EQ(effects.size(), 1u);
  ASSERT_TRUE(effects[0].parameters.button_label.has_value());
  EXPECT_EQ(effects[0].parameters.button_label->inline_text(), "Press");
  ASSERT_TRUE(effects[0].parameters.script_flags.has_value());
  EXPECT_EQ(*effects[0].parameters.script_flags, 1);
  const auto spell = fo4::forms::perk_epfd_form(plugin, effects[0]);
  ASSERT_TRUE(spell.has_value());
  EXPECT_EQ(spell->to_string(), "000D12:Patch.esp");

  EXPECT_EQ(fo4::forms::set_perk_effects(plugin, fo4::FormKey::parse(k_key), effects),
            fo4::forms::PerkEffectWrite::written);
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* after = perk_record(reread);
  ASSERT_NE(after, nullptr);
  EXPECT_EQ(signatures_of(*after, 0),
            (std::vector<std::string>{"EDID", "DATA", "PRKE", "DATA", "PRKC", "CTDA", "EPFT",
                                      "EPFB", "EPF2", "EPF3", "EPFD", "PRKF"}));
  const auto round_tripped = fo4::forms::read_perk_effects(reread, *after);
  ASSERT_EQ(round_tripped.size(), 1u);
  ASSERT_EQ(round_tripped[0].tabs.size(), 1u);
  ASSERT_EQ(round_tripped[0].tabs[0].conditions.size(), 1u);
  EXPECT_EQ(round_tripped[0].tabs[0].conditions[0].function, 100);
  ASSERT_TRUE(round_tripped[0].parameters.button_label.has_value());
  EXPECT_EQ(round_tripped[0].parameters.button_label->inline_text(), "Press");
  EXPECT_EQ(raw_at(after->subrecords[10].data, 0), 0x01000D12u);
}

TEST(PerkEffects, SetReplacesAddsAndRemovesEffectSections) {
  auto plugin = fo4::read_plugin(
      plugin_with_perk({{"DATA", header_data()},
                        {"PRKE", prke(2, 1, 3)},
                        {"DATA", entry_point_effect_data(0x26, 1, 1)},
                        {"PRKC", prkc(0)},
                        {"CTDA", ctda_run_bytes(100)},
                        {"EPFT", u8_payload(1)},
                        {"EPFD", epfd_float(2.5f)},
                        {"PRKF", Bytes{}}}),
      "Patch.esp");
  const auto key = fo4::FormKey::parse(k_key);
  const auto* record = perk_record(plugin);
  ASSERT_NE(record, nullptr);
  const auto existing = fo4::forms::read_perk_effects(plugin, *record);
  ASSERT_EQ(existing.size(), 1u);

  auto edited = existing;
  edited[0].entry_point = 0x3B;
  edited[0].parameters.data = epfd_float(9.0f);
  fo4::forms::PerkEffectEntry added;
  added.type = fo4::forms::PerkEffectType::ability;
  added.rank = 1;
  added.ability = fo4::FormKey::parse("000D12:Patch.esp");
  edited.push_back(added);
  EXPECT_EQ(fo4::forms::set_perk_effects(plugin, key, edited),
            fo4::forms::PerkEffectWrite::written);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* written = perk_record(reread);
  ASSERT_NE(written, nullptr);
  EXPECT_EQ(signatures_of(*written, 0),
            (std::vector<std::string>{"EDID", "DATA", "PRKE", "DATA", "PRKC", "CTDA", "EPFT",
                                      "EPFD", "PRKF", "PRKE", "DATA", "PRKF"}));
  const auto effects = fo4::forms::read_perk_effects(reread, *written);
  ASSERT_EQ(effects.size(), 2u);
  EXPECT_EQ(effects[0].entry_point, 0x3B);
  ASSERT_EQ(effects[0].tabs.size(), 1u);
  const auto value = fo4::forms::perk_epfd_float(effects[0]);
  ASSERT_TRUE(value.has_value());
  EXPECT_FLOAT_EQ(*value, 9.0f);
  EXPECT_EQ(effects[1].type, fo4::forms::PerkEffectType::ability);
  EXPECT_EQ(raw_at(written->subrecords[10].data, 0), 0x01000D12u);

  auto mutable_plugin = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(fo4::forms::set_perk_effects(mutable_plugin, key, {}),
            fo4::forms::PerkEffectWrite::written);
  const auto* emptied = perk_record(mutable_plugin);
  ASSERT_NE(emptied, nullptr);
  EXPECT_TRUE(fo4::forms::read_perk_effects(mutable_plugin, *emptied).empty());
  EXPECT_EQ(signatures_of(*emptied, 0), (std::vector<std::string>{"EDID", "DATA"}));
  EXPECT_EQ(emptied->subrecords[1].data, header_data());
}

TEST(PerkEffects, SetRefusesForeignRecordsAndMissingKeys) {
  auto plugin = fo4::read_plugin(plugin_with_perk({{"DATA", header_data()}}), "Patch.esp");
  EXPECT_EQ(fo4::forms::set_perk_effects(plugin, fo4::FormKey::parse("000D10:Patch.esp"), {}),
            fo4::forms::PerkEffectWrite::unsupported_record);
  EXPECT_EQ(fo4::forms::set_perk_effects(plugin, fo4::FormKey::parse("000E01:Patch.esp"), {}),
            fo4::forms::PerkEffectWrite::record_missing);
  const auto* record = perk_record(plugin);
  ASSERT_NE(record, nullptr);
  EXPECT_TRUE(fo4::forms::read_perk_effects(plugin, *record).empty());
}

TEST(PerkEffects, RefusesMalformedEffectSections) {
  const auto key = fo4::FormKey::parse(k_key);
  auto decode = [key](std::vector<RawSubrecord> body) {
    auto plugin = fo4::read_plugin(plugin_with_perk(std::move(body)), "Patch.esp");
    const auto* found = fo4::find_record(plugin, key);
    EXPECT_NE(found, nullptr);
    if (found == nullptr) return std::vector<fo4::forms::PerkEffectEntry>{};
    return fo4::forms::read_perk_effects(plugin, *found);
  };

  EXPECT_THROW(decode({{"DATA", header_data()},
                       {"PRKE", Bytes{std::byte{2}, std::byte{1}}},
                       {"DATA", entry_point_effect_data(0x26, 1, 0)},
                       {"PRKF", Bytes{}}}),
               fo4::forms::PerkError);
  EXPECT_THROW(decode({{"DATA", header_data()},
                       {"PRKE", prke(3, 1, 0)},
                       {"DATA", entry_point_effect_data(0x26, 1, 0)},
                       {"PRKF", Bytes{}}}),
               fo4::forms::PerkError);
  EXPECT_THROW(decode({{"DATA", header_data()},
                       {"PRKE", prke(0, 1, 0)},
                       {"DATA", quest_effect_data(0x01000D10u, 42)}}),
               fo4::forms::PerkError);
  EXPECT_THROW(decode({{"DATA", header_data()},
                       {"PRKE", prke(0, 1, 0)},
                       {"DATA", Bytes{std::byte{0}, std::byte{0}, std::byte{0}}},
                       {"PRKF", Bytes{}}}),
               fo4::forms::PerkError);
  EXPECT_THROW(decode({{"DATA", header_data()},
                       {"PRKE", prke(2, 1, 0)},
                       {"DATA", entry_point_effect_data(0x26, 1, 0)},
                       {"EPFT", u8_payload(1)},
                       {"EPFD", epfd_float(1.0f)},
                       {"PRKC", prkc(0)},
                       {"PRKF", Bytes{}}}),
               fo4::forms::PerkError);
  EXPECT_THROW(decode({{"DATA", header_data()},
                       {"PRKE", prke(2, 1, 0)},
                       {"DATA", entry_point_effect_data(0x26, 1, 0)},
                       {"FNAM", zstr("PerkMenu")},
                       {"PRKF", Bytes{}}}),
               fo4::forms::PerkError);
}

TEST(PerkEffects, RefusesRecordsThatDoNotCarryEffects) {
  auto plugin = fo4::read_plugin(make_plugin(perk_data()), "Patch.esp");
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse("000D02:Patch.esp"));
  ASSERT_NE(record, nullptr);
  EXPECT_THROW(fo4::forms::read_perk_effects(plugin, *record), fo4::forms::PerkError);
}

TEST(PerkRows, HeaderDataScalarsAreMissingWithoutAHeaderData) {
  const auto key = fo4::FormKey::parse(k_key);
  const auto level = scalar_field("PERK\\Level");
  ASSERT_TRUE(level.has_value());

  auto effect_only = fo4::read_plugin(
      plugin_with_perk({{"PRKE", prke(2, 1, 0)},
                        {"DATA", entry_point_effect_data(0x26, 1, 0)},
                        {"PRKF", Bytes{}}}),
      "Patch.esp");
  EXPECT_EQ(fo4::set_record_scalar(effect_only, key, *level, "7"),
            fo4::ScalarMutationStatus::subrecord_missing);

  auto headerless = fo4::read_plugin(plugin_with_perk({{"SNAM", u32_bytes(0x00000A20)}}),
                                     "Patch.esp");
  EXPECT_EQ(fo4::set_record_scalar(headerless, key, *level, "7"),
            fo4::ScalarMutationStatus::subrecord_missing);

  auto with_header = fo4::read_plugin(make_plugin(perk_data()), "Patch.esp");
  EXPECT_THROW(fo4::set_record_scalar(with_header, key, *level, "300"),
               fo4::schema::ScalarError);
  EXPECT_EQ(fo4::set_record_scalar(with_header, key, *level, "255"),
            fo4::ScalarMutationStatus::updated);
}
