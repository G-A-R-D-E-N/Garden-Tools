#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/subrecord_authoring.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/localized_fields.hpp"
#include "fo4/schema/record_constraints.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Sub = std::pair<std::string, std::vector<std::byte>>;
using Subs = std::vector<Sub>;

constexpr const char* k_plugin = "Name.esp";
constexpr std::uint32_t k_localized_flag = 0x80;

std::vector<std::byte> bytes_of(std::string_view text) {
  std::vector<std::byte> out;
  for (const char ch : text) out.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  return out;
}

std::vector<std::byte> zstr(std::string_view text) {
  auto out = bytes_of(text);
  out.push_back(std::byte{0});
  return out;
}

std::vector<std::byte> id_bytes(std::uint32_t id) {
  std::vector<std::byte> out(4);
  std::memcpy(out.data(), &id, 4);
  return out;
}

std::vector<std::byte> filled(std::size_t size, std::uint8_t value) {
  return std::vector<std::byte>(size, std::byte{value});
}

struct Entry {
  std::string signature;
  std::uint32_t form_id;
  Subs subs;
};

std::vector<std::byte> build(const std::vector<Entry>& entries, std::uint32_t tes4_flags = 0) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", tes4_flags, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  std::vector<std::string> done;
  for (const auto& top : entries) {
    if (std::find(done.begin(), done.end(), top.signature) != done.end()) continue;
    done.push_back(top.signature);
    const auto group = fo4::test::begin_group(out, top.signature, 0);
    for (const auto& entry : entries) {
      if (entry.signature != top.signature) continue;
      const auto record = fo4::test::begin_record(out, entry.signature, 0, entry.form_id);
      for (const auto& [name, data] : entry.subs) fo4::test::write_subrecord(out, name, data);
      fo4::test::end_record(out, record);
    }
    fo4::test::end_group(out, group);
  }
  return out.bytes;
}

fo4::FormKey key(std::uint32_t local) {
  char text[64];
  std::snprintf(text, sizeof(text), "%06X:%s", local, k_plugin);
  return fo4::FormKey::parse(text);
}

std::vector<std::string> order(const fo4::Plugin& plugin, std::uint32_t local) {
  std::vector<std::string> out;
  const auto* record = fo4::find_record(plugin, key(local));
  if (record == nullptr) return out;
  for (const auto& sub : record->subrecords) out.push_back(sub.signature.to_string());
  return out;
}

const std::vector<std::byte>* payload(const fo4::Plugin& plugin, std::uint32_t local, std::string_view sig) {
  const auto* record = fo4::find_record(plugin, key(local));
  if (record == nullptr) return nullptr;
  for (const auto& sub : record->subrecords) {
    if (sub.signature == fo4::Signature::from_string(sig)) return &sub.data;
  }
  return nullptr;
}

fo4::schema::LocalizedString text_name(std::string_view text) {
  fo4::schema::LocalizedString name;
  name.text = std::string(text);
  return name;
}

fo4::schema::LocalizedString id_name(std::uint32_t id) {
  fo4::schema::LocalizedString name;
  name.id = id;
  return name;
}

}  // namespace

// Detects: set_record_full_name must insert an absent FULL after the last listed subrecord before it and before the next listed one, so a WEAP without a name keeps the Creation Kit order.
TEST(RecordName, InsertsAnAbsentFullAtItsAuthoringPosition) {
  auto plugin = fo4::read_plugin(build({{"WEAP", 0x800,
                                         {{"EDID", zstr("Rifle")},
                                          {"OBND", filled(12, 0)},
                                          {"STCP", filled(4, 0)},
                                          {"MODL", zstr("a.nif")},
                                          {"DNAM", filled(4, 0)}}}}),
                                 k_plugin);

  EXPECT_EQ(fo4::set_record_full_name(plugin, key(0x800), text_name("Plasma Rifle")),
            fo4::RecordNameStatus::updated);
  EXPECT_EQ(order(plugin, 0x800),
            (std::vector<std::string>{"EDID", "OBND", "STCP", "FULL", "MODL", "DNAM"}));
  EXPECT_EQ(*payload(plugin, 0x800, "FULL"), zstr("Plasma Rifle"));
  EXPECT_EQ(fo4::set_record_full_name(plugin, key(0x800), text_name("Plasma Rifle")),
            fo4::RecordNameStatus::unchanged);

  const auto reloaded = fo4::read_plugin(fo4::write_plugin(plugin), k_plugin);
  EXPECT_EQ(*payload(reloaded, 0x800, "FULL"), zstr("Plasma Rifle"));
  EXPECT_EQ(order(reloaded, 0x800), order(plugin, 0x800));
}

// Detects: set_record_full_name must refuse a type with no FULL authoring row and an insertion the authoring marks coupled, without touching the record.
TEST(RecordName, RefusesTypesWithoutAFullRowAndCoupledInsertions) {
  auto plugin = fo4::read_plugin(build({{"GLOB", 0x800, {{"EDID", zstr("G")}}},
                                         {"REFR", 0x801, {{"NAME", filled(4, 0)}}}}),
                                 k_plugin);
  const auto before = order(plugin, 0x800);
  EXPECT_EQ(fo4::set_record_full_name(plugin, key(0x800), text_name("x")),
            fo4::RecordNameStatus::not_named_type);
  EXPECT_EQ(fo4::set_record_full_name(plugin, key(0x801), text_name("x")),
            fo4::RecordNameStatus::not_insertable);
  EXPECT_EQ(fo4::set_record_full_name(plugin, key(0x802), text_name("x")),
            fo4::RecordNameStatus::record_missing);
  EXPECT_EQ(order(plugin, 0x800), before);
  EXPECT_EQ(order(plugin, 0x801), (std::vector<std::string>{"NAME"}));
}

// Detects: set_record_full_name must write the header FULL of a WEAP with object templates and leave the template-run FULL untouched.
TEST(RecordName, WritesTheHeaderFullBeforeAnObjectTemplateRun) {
  auto plugin = fo4::read_plugin(build({{"WEAP", 0x800,
                                         {{"EDID", zstr("Rifle")},
                                          {"OBND", filled(12, 0)},
                                          {"OBTE", filled(4, 1)},
                                          {"OBTF", zstr("Template")},
                                          {"FULL", zstr("Combo")},
                                          {"OBTS", filled(4, 0)},
                                          {"STOP", filled(4, 0)}}}}),
                                 k_plugin);

  EXPECT_EQ(fo4::set_record_full_name(plugin, key(0x800), text_name("Header")),
            fo4::RecordNameStatus::updated);
  EXPECT_EQ(order(plugin, 0x800), (std::vector<std::string>{"EDID", "OBND", "FULL", "OBTE", "OBTF",
                                                             "FULL", "OBTS", "STOP"}));
  EXPECT_EQ(fo4::set_record_full_name(plugin, key(0x800), text_name("Renamed")),
            fo4::RecordNameStatus::updated);

  const auto* record = fo4::find_record(plugin, key(0x800));
  ASSERT_NE(record, nullptr);
  std::vector<std::vector<std::byte>> fulls;
  for (const auto& sub : record->subrecords) {
    if (sub.signature == fo4::Signature::from_string("FULL")) fulls.push_back(sub.data);
  }
  ASSERT_EQ(fulls.size(), 2u);
  EXPECT_EQ(fulls[0], zstr("Renamed"));
  EXPECT_EQ(fulls[1], zstr("Combo"));
}

// Detects: set_record_full_name must refuse an existing FULL that is not a NUL-terminated string in a plain plugin and leave its bytes unchanged.
TEST(RecordName, RefusesAnUndecodableExistingFull) {
  auto plugin = fo4::read_plugin(build({{"WEAP", 0x800,
                                         {{"EDID", zstr("Rifle")}, {"FULL", filled(4, 0x78)}}}}),
                                 k_plugin);
  EXPECT_EQ(fo4::set_record_full_name(plugin, key(0x800), text_name("Named")),
            fo4::RecordNameStatus::opaque_existing);
  EXPECT_EQ(*payload(plugin, 0x800, "FULL"), filled(4, 0x78));
}

// Detects: an override copied from a localized master into a plain plugin carries a four-byte string ID in FULL; set_record_full_name must replace it with inline text when told the ID is carried over, and must still refuse any other unreadable FULL.
TEST(RecordName, ReplacesACarriedStringIdOnlyWhenAsked) {
  auto plugin = fo4::read_plugin(build({{"WEAP", 0x800, {{"EDID", zstr("Rifle")}, {"FULL", id_bytes(0xABCD)}}},
                                         {"WEAP", 0x801, {{"EDID", zstr("Odd")}, {"FULL", filled(3, 0x78)}}}}),
                                 k_plugin);

  EXPECT_EQ(fo4::set_record_full_name(plugin, key(0x800), text_name("Named")), fo4::RecordNameStatus::opaque_existing);
  EXPECT_EQ(fo4::set_record_full_name(plugin, key(0x800), text_name("Named"), fo4::CarriedStringId::replace),
            fo4::RecordNameStatus::updated);
  EXPECT_EQ(*payload(plugin, 0x800, "FULL"), zstr("Named"));
  EXPECT_EQ(fo4::set_record_full_name(plugin, key(0x801), text_name("Named"), fo4::CarriedStringId::replace),
            fo4::RecordNameStatus::opaque_existing);
  EXPECT_EQ(*payload(plugin, 0x801, "FULL"), filled(3, 0x78));
}

// Detects: set_record_full_name must store a localized FULL as a string ID in a localized plugin and refuse inline text there.
TEST(RecordName, LocalizedFullStoresTheStringIdAndRefusesText) {
  auto plugin = fo4::read_plugin(build({{"BOOK", 0x800,
                                         {{"EDID", zstr("Tome")},
                                          {"OBND", filled(12, 0)},
                                          {"DESC", id_bytes(9)}}}},
                                       k_localized_flag),
                                 k_plugin);

  EXPECT_EQ(fo4::set_record_full_name(plugin, key(0x800), text_name("Tome")),
            fo4::RecordNameStatus::form_mismatch);
  EXPECT_EQ(fo4::set_record_full_name(plugin, key(0x800), id_name(4242)),
            fo4::RecordNameStatus::updated);
  EXPECT_EQ(order(plugin, 0x800), (std::vector<std::string>{"EDID", "OBND", "FULL", "DESC"}));
  EXPECT_EQ(*payload(plugin, 0x800, "FULL"), id_bytes(4242));
}

// Detects: create_record must build a required localized row from a supplied string ID in a localized plugin, refuse it without one.
TEST(RecordName, CreatesRequiredLocalizedRowsFromSuppliedIds) {
  auto plugin = fo4::read_plugin(build({}, k_localized_flag), k_plugin);

  const auto refused = fo4::create_record(plugin, fo4::Signature::from_string("BOOK"), "NoDesc");
  EXPECT_EQ(refused.status, fo4::RecordCreateStatus::no_default);

  const std::vector<fo4::LocalizedRowId> ids{{fo4::Signature::from_string("DESC"), 77u}};
  const auto created = fo4::create_record_with_localized_rows(plugin, fo4::Signature::from_string("BOOK"), "WithDesc", ids);
  ASSERT_EQ(created.status, fo4::RecordCreateStatus::created);
  EXPECT_EQ(order(plugin, created.key.local().value()),
            (std::vector<std::string>{"EDID", "OBND", "DESC", "DATA", "DNAM"}));
  EXPECT_EQ(*payload(plugin, created.key.local().value(), "DESC"), id_bytes(77));
}

// Detects: adding the required OBND to a WEAP must write zero bounds in front of the existing DNAM (a WEAP cannot be created from nothing because its required DNAM has no default).
TEST(RecordName, AddsZeroObjectBoundsToAWeapon) {
  auto plugin = fo4::read_plugin(build({{"WEAP", 0x800, {{"EDID", zstr("Gun")}, {"DNAM", filled(4, 0)}}}}),
                                 k_plugin);

  EXPECT_EQ(fo4::add_record_subrecord(plugin, key(0x800), fo4::Signature::from_string("OBND")),
            fo4::SubrecordAddStatus::added);
  EXPECT_EQ(order(plugin, 0x800), (std::vector<std::string>{"EDID", "OBND", "DNAM"}));
  EXPECT_EQ(*payload(plugin, 0x800, "OBND"), filled(12, 0));
}

// Detects: set_record_full_name must refuse inline text with an embedded NUL and a raw-ID value in a plain plugin, which would write a FULL that is not a single NUL-terminated string.
TEST(RecordName, RefusesNamesThatCannotBeWrittenAsASingleString) {
  auto plugin = fo4::read_plugin(build({{"WEAP", 0x800, {{"EDID", zstr("Rifle")}, {"FULL", zstr("Old")}}}}),
                                 k_plugin);

  fo4::schema::LocalizedString embedded;
  embedded.text = std::string("a\0b", 3);
  fo4::schema::LocalizedString raw;
  raw.raw_id = filled(3, 1);

  EXPECT_EQ(fo4::set_record_full_name(plugin, key(0x800), embedded), fo4::RecordNameStatus::invalid_name);
  EXPECT_EQ(fo4::set_record_full_name(plugin, key(0x800), raw), fo4::RecordNameStatus::invalid_name);
  EXPECT_EQ(*payload(plugin, 0x800, "FULL"), zstr("Old"));
}

// Detects: set_record_full_name must report unchanged for the same string ID in a localized plugin and leave the plugin's bytes untouched.
TEST(RecordName, SameStringIdInALocalizedPluginIsUnchanged) {
  const auto bytes = build({{"BOOK", 0x800,
                             {{"EDID", zstr("Tome")}, {"FULL", id_bytes(4242)}, {"DESC", id_bytes(9)}}}},
                           k_localized_flag);
  auto plugin = fo4::read_plugin(bytes, k_plugin);

  EXPECT_EQ(fo4::set_record_full_name(plugin, key(0x800), id_name(4242)), fo4::RecordNameStatus::unchanged);
  EXPECT_EQ(fo4::write_plugin(plugin), bytes);
}

// Detects: a map-marker name is a string ID in a localized plugin, so set_record_full_name must take the ID form for REFR FULL and refuse inline text that the game would read as an ID.
TEST(RecordName, MapMarkerFullTakesAStringIdInALocalizedPlugin) {
  auto plugin = fo4::read_plugin(build({{"REFR", 0x800, {{"NAME", filled(4, 0)}, {"FULL", id_bytes(0x1A2B3)}}}},
                                       k_localized_flag),
                                 k_plugin);

  EXPECT_TRUE(fo4::schema::is_localized_field(fo4::Signature::from_string("REFR"), fo4::Signature::from_string("FULL")));
  EXPECT_EQ(fo4::set_record_full_name(plugin, key(0x800), text_name("Renamed")), fo4::RecordNameStatus::form_mismatch);
  EXPECT_EQ(*payload(plugin, 0x800, "FULL"), id_bytes(0x1A2B3));
  EXPECT_EQ(fo4::set_record_full_name(plugin, key(0x800), id_name(5)), fo4::RecordNameStatus::updated);
  EXPECT_EQ(*payload(plugin, 0x800, "FULL"), id_bytes(5));
}

// Detects: every record type with a FULL authoring row must store its name as a string ID in a localized plugin; a type missing from the localized set would get inline text the game reads as an ID.
TEST(RecordName, EveryNamedTypeHasALocalizedFull) {
  const auto full = fo4::Signature::from_string("FULL");
  std::size_t named = 0;
  for (const auto& constraint : fo4::schema::k_generated_record_authoring_constraints) {
    const auto rules = fo4::schema::authoring_subrecords(constraint);
    const bool has_full = std::any_of(rules.begin(), rules.end(), [&](const auto& rule) { return rule.signature == full; });
    if (!has_full) continue;
    ++named;
    EXPECT_TRUE(fo4::schema::is_localized_field(constraint.record, full)) << constraint.record.to_string();
  }
  EXPECT_GE(named, 50u);
}

// Detects: on NPC_ the object-template run comes before the name, so set_record_full_name must rename the FULL after STOP, leave the combination FULL alone, and insert an absent name after the run rather than a second FULL ahead of OBTE.
TEST(RecordName, NpcNameSitsAfterTheObjectTemplateRun) {
  auto plugin = fo4::read_plugin(build({{"NPC_", 0x800,
                                         {{"EDID", zstr("Raider")},
                                          {"OBND", filled(12, 0)},
                                          {"ACBS", filled(20, 0)},
                                          {"AIDT", filled(24, 0)},
                                          {"OBTE", filled(4, 1)},
                                          {"OBTF", {}},
                                          {"FULL", zstr("Combo")},
                                          {"OBTS", filled(4, 0)},
                                          {"STOP", {}},
                                          {"CNAM", filled(4, 0)},
                                          {"FULL", zstr("Raider")},
                                          {"DATA", {}}}},
                                        {"NPC_", 0x801,
                                         {{"EDID", zstr("Nameless")},
                                          {"OBND", filled(12, 0)},
                                          {"ACBS", filled(20, 0)},
                                          {"OBTE", filled(4, 1)},
                                          {"OBTF", {}},
                                          {"FULL", zstr("Combo")},
                                          {"OBTS", filled(4, 0)},
                                          {"STOP", {}},
                                          {"CNAM", filled(4, 0)},
                                          {"DATA", {}}}},
                                        {"NPC_", 0x802,
                                         {{"EDID", zstr("Broken")},
                                          {"ACBS", filled(20, 0)},
                                          {"OBTE", filled(4, 1)},
                                          {"OBTF", {}},
                                          {"FULL", zstr("Combo")},
                                          {"OBTS", filled(4, 0)}}}}),
                                 k_plugin);
  const auto fulls = [&](std::uint32_t local) {
    std::vector<std::vector<std::byte>> out;
    for (const auto& sub : fo4::find_record(plugin, key(local))->subrecords) {
      if (sub.signature == fo4::Signature::from_string("FULL")) out.push_back(sub.data);
    }
    return out;
  };

  EXPECT_EQ(fo4::set_record_full_name(plugin, key(0x800), text_name("Scavenger")), fo4::RecordNameStatus::updated);
  EXPECT_EQ(order(plugin, 0x800), (std::vector<std::string>{"EDID", "OBND", "ACBS", "AIDT", "OBTE", "OBTF", "FULL",
                                                             "OBTS", "STOP", "CNAM", "FULL", "DATA"}));
  EXPECT_EQ(fulls(0x800), (std::vector<std::vector<std::byte>>{zstr("Combo"), zstr("Scavenger")}));

  EXPECT_EQ(fo4::set_record_full_name(plugin, key(0x801), text_name("Settler")), fo4::RecordNameStatus::updated);
  EXPECT_EQ(order(plugin, 0x801), (std::vector<std::string>{"EDID", "OBND", "ACBS", "OBTE", "OBTF", "FULL", "OBTS",
                                                             "STOP", "CNAM", "FULL", "DATA"}));
  EXPECT_EQ(fulls(0x801), (std::vector<std::vector<std::byte>>{zstr("Combo"), zstr("Settler")}));

  EXPECT_EQ(fo4::set_record_full_name(plugin, key(0x802), text_name("x")), fo4::RecordNameStatus::not_insertable);
  EXPECT_EQ(fulls(0x802), (std::vector<std::vector<std::byte>>{zstr("Combo")}));

  const auto read = fo4::record_full_name(plugin, *fo4::find_record(plugin, key(0x800)));
  EXPECT_EQ(read.state, fo4::RecordNameState::present);
  EXPECT_EQ(read.value.text, std::optional<std::string>{"Scavenger"});
  EXPECT_EQ(fo4::record_full_name(plugin, *fo4::find_record(plugin, key(0x802))).state,
            fo4::RecordNameState::absent_not_insertable);
}

// Detects: set_record_full_name must refuse an existing localized FULL that is not a four-byte string ID and leave its bytes unchanged.
TEST(RecordName, WrongSizeLocalizedFullIsOpaqueInALocalizedPlugin) {
  auto plugin = fo4::read_plugin(build({{"BOOK", 0x800, {{"EDID", zstr("Tome")}, {"FULL", filled(3, 0x41)}}}},
                                       k_localized_flag),
                                 k_plugin);

  EXPECT_EQ(fo4::set_record_full_name(plugin, key(0x800), id_name(7)), fo4::RecordNameStatus::opaque_existing);
  EXPECT_EQ(*payload(plugin, 0x800, "FULL"), filled(3, 0x41));
}

// Detects: set_record_full_name must refuse to insert a FULL into an LCTN that holds unlisted location-reference rows, whose vanilla position the authoring rows cannot place, and still insert into a bare one.
TEST(RecordName, RefusesToPlaceAFullAmongUnlistedLocationRows) {
  auto plugin = fo4::read_plugin(build({{"LCTN", 0x800, {{"EDID", zstr("Loc")}, {"LCSR", filled(16, 0)}}},
                                         {"LCTN", 0x801, {{"EDID", zstr("Bare")}}}}),
                                 k_plugin);

  EXPECT_EQ(fo4::set_record_full_name(plugin, key(0x800), text_name("Named")),
            fo4::RecordNameStatus::not_insertable);
  EXPECT_EQ(order(plugin, 0x800), (std::vector<std::string>{"EDID", "LCSR"}));
  EXPECT_EQ(fo4::set_record_full_name(plugin, key(0x801), text_name("Named")), fo4::RecordNameStatus::updated);
  EXPECT_EQ(order(plugin, 0x801), (std::vector<std::string>{"EDID", "FULL"}));
}
