#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/subrecord_authoring.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/preflight.hpp"
#include "plugin_bytes.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Sub = std::pair<std::string, std::vector<std::byte>>;
using Subs = std::vector<Sub>;

constexpr const char* k_plugin = "Authoring.esp";

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

std::vector<std::byte> filled(std::size_t size, std::uint8_t value) {
  return std::vector<std::byte>(size, std::byte{value});
}

struct Entry {
  std::string signature;
  std::uint32_t form_id;
  Subs subs;
  bool compressed{false};
};

std::vector<std::byte> build(const std::vector<Entry>& entries) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
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
      if (entry.compressed) {
        fo4::test::ByteWriter inner;
        for (const auto& [name, data] : entry.subs) fo4::test::write_subrecord(inner, name, data);
        const auto packed = fo4::test::zlib_compress(inner.bytes);
        const auto record = fo4::test::begin_record(
            out, entry.signature, fo4::RecordFlags::compressed, entry.form_id);
        out.u32(static_cast<std::uint32_t>(inner.bytes.size()));
        out.raw(packed);
        fo4::test::end_record(out, record);
      } else {
        const auto record = fo4::test::begin_record(out, entry.signature, 0, entry.form_id);
        for (const auto& [name, data] : entry.subs) fo4::test::write_subrecord(out, name, data);
        fo4::test::end_record(out, record);
      }
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

fo4::Signature sig(const char* text) { return fo4::Signature::from_string(text); }

std::vector<std::string> order(const fo4::Plugin& plugin, std::uint32_t local) {
  std::vector<std::string> out;
  const auto* record = fo4::find_record(plugin, key(local));
  if (record == nullptr) return out;
  for (const auto& subrecord : record->subrecords) out.push_back(subrecord.signature.to_string());
  return out;
}

const fo4::Subrecord* find_sub(const fo4::Plugin& plugin, std::uint32_t local, const char* name,
                               std::size_t occurrence = 0) {
  const auto* record = fo4::find_record(plugin, key(local));
  if (record == nullptr) return nullptr;
  for (const auto& subrecord : record->subrecords) {
    if (subrecord.signature != sig(name)) continue;
    if (occurrence-- == 0) return &subrecord;
  }
  return nullptr;
}

bool authoring_clean(const fo4::Plugin& plugin, const fo4::FormKey& record) {
  for (const auto& issue : fo4::schema::preflight_authoring_record(plugin, record)) {
    if (issue.severity == fo4::schema::PreflightSeverity::error) return false;
  }
  return true;
}

fo4::Plugin reload(const fo4::Plugin& plugin) {
  auto out = fo4::read_plugin(fo4::write_plugin(plugin), k_plugin);
  fo4::assign_record_form_keys(out);
  return out;
}

Subs armo_base() {
  return {{"EDID", zstr("Armor")}, {"MOD2", zstr("a.nif")}, {"ICO2", zstr("b.dds")},
          {"DESC", zstr("Desc")}, {"DATA", filled(12, 7)}};
}

fo4::Plugin fixture() {
  auto plugin = fo4::read_plugin(
      build({{"ARMO", 0x800, armo_base()},
             {"ARMO", 0x801, {{"EDID", zstr("Odd")}, {"KWDA", filled(4, 1)}, {"DATA", filled(12, 0)}}},
             {"ARMO", 0x802, armo_base(), true},
             {"ARMO", 0x803, {{"DATA", filled(12, 0)}, {"EDID", zstr("Swapped")}}},
             {"ARMO", 0x804, {{"EDID", zstr("Plain")}, {"DATA", filled(12, 0)}}},
             {"MISC", 0x805, {{"EDID", zstr("Misc")}}},
             {"LVSP", 0x806,
              {{"EDID", zstr("List")}, {"OBND", filled(12, 0)}, {"LVLD", filled(1, 0)}, {"LVLM", filled(1, 0)},
               {"LVLF", filled(1, 0)}, {"LLCT", filled(1, 1)}, {"LVLO", filled(12, 5)}}}}),
      k_plugin);
  fo4::assign_record_form_keys(plugin);
  return plugin;
}

constexpr std::uint8_t k_default_a[] = {0x01};
constexpr std::uint8_t k_default_b[] = {0x02, 0x02};
constexpr std::uint8_t k_default_c[] = {0x03, 0x03, 0x03};

const fo4::schema::SubrecordAuthoringConstraint k_rules[] = {
    {fo4::Signature::from_string("EDID"), 0, 1, nullptr, 0, false},
    {fo4::Signature::from_string("AAAA"), 0, 1, k_default_a, 1, false},
    {fo4::Signature::from_string("BBBB"), 0, 3, k_default_b, 2, true},
    {fo4::Signature::from_string("CCCC"), 1, 1, k_default_c, 3, false},
    {fo4::Signature::from_string("DDDD"), 0, 1, nullptr, 0, false},
    {fo4::Signature::from_string("FFFF"), 0, 3, k_default_a, 1, false},
};

const fo4::schema::SubrecordAuthoringConstraint k_create_counted[] = {
    {fo4::Signature::from_string("EDID"), 0, 1, nullptr, 0, false},
    {fo4::Signature::from_string("GGGG"), 1, 2, k_default_a, 1, false},
};

const fo4::schema::SubrecordAuthoringConstraint k_create_rules[] = {
    {fo4::Signature::from_string("EDID"), 1, 1, nullptr, 0, false},
    {fo4::Signature::from_string("CCCC"), 1, 1, k_default_c, 3, false},
    {fo4::Signature::from_string("EEEE"), 2, 2, k_default_a, 1, true},
};

const fo4::schema::SubrecordAuthoringConstraint k_create_blocked[] = {
    {fo4::Signature::from_string("EDID"), 0, 1, nullptr, 0, false},
    {fo4::Signature::from_string("CCCC"), 1, 1, nullptr, 0, false},
};

const fo4::schema::SubrecordAuthoringConstraint k_coupled_rules[] = {
    {fo4::Signature::from_string("EDID"), 0, 1, nullptr, 0, false},
    {fo4::Signature::from_string("HHHH"), 1, 1, k_default_a, 1, false, true},
};

const fo4::schema::SubrecordAuthoringConstraint k_localized_rules[] = {
    {fo4::Signature::from_string("EDID"), 0, 1, nullptr, 0, false},
    {fo4::Signature::from_string("FULL"), 1, 1, k_default_a, 1, false},
};

fo4::Plugin generic_fixture() {
  auto plugin = fo4::read_plugin(
      build({{"XXXX", 0x800,
              {{"EDID", zstr("Gen")}, {"BBBB", filled(2, 0x11)}, {"BBBB", filled(2, 0x22)},
               {"CCCC", filled(3, 0x33)}}}}),
      k_plugin);
  fo4::assign_record_form_keys(plugin);
  return plugin;
}

}  // namespace

TEST(SubrecordAuthoring, AddsAtSchemaPositionAndLeavesNeighboursUntouched) {
  auto plugin = fixture();
  const auto before_odd = fo4::test::record_original_bytes(plugin, *fo4::find_record(plugin, key(0x801)));

  EXPECT_EQ(fo4::add_record_subrecord(plugin, key(0x800), sig("ICON")), fo4::SubrecordAddStatus::added);
  EXPECT_EQ(order(plugin, 0x800),
            (std::vector<std::string>{"EDID", "MOD2", "ICON", "ICO2", "DESC", "DATA"}));
  EXPECT_EQ(fo4::add_record_subrecord(plugin, key(0x800), sig("MICO")), fo4::SubrecordAddStatus::added);
  EXPECT_EQ(order(plugin, 0x800),
            (std::vector<std::string>{"EDID", "MOD2", "ICON", "MICO", "ICO2", "DESC", "DATA"}));
  EXPECT_EQ(find_sub(plugin, 0x800, "ICON")->data, filled(1, 0));
  EXPECT_EQ(find_sub(plugin, 0x800, "MOD2")->data, zstr("a.nif"));
  EXPECT_EQ(find_sub(plugin, 0x800, "DATA")->data, filled(12, 7));
  EXPECT_TRUE(authoring_clean(plugin, key(0x800)));

  const auto reloaded = reload(plugin);
  EXPECT_EQ(order(reloaded, 0x800),
            (std::vector<std::string>{"EDID", "MOD2", "ICON", "MICO", "ICO2", "DESC", "DATA"}));
  EXPECT_EQ(find_sub(reloaded, 0x800, "ICON")->data, filled(1, 0));
  EXPECT_TRUE(authoring_clean(reloaded, key(0x800)));
  EXPECT_EQ(fo4::test::record_original_bytes(plugin, *fo4::find_record(plugin, key(0x801))), before_odd);
}

TEST(SubrecordAuthoring, AddsBetweenNeighboursAndRefusesAnOutOfOrderRecord) {
  auto plugin = fixture();
  EXPECT_EQ(fo4::add_record_subrecord(plugin, key(0x803), sig("ICON")),
            fo4::SubrecordAddStatus::unknown_order);
  EXPECT_EQ(fo4::add_record_subrecord(plugin, key(0x804), sig("ICON")), fo4::SubrecordAddStatus::added);
  EXPECT_EQ(order(plugin, 0x804), (std::vector<std::string>{"EDID", "ICON", "DATA"}));
}

TEST(SubrecordAuthoring, RefusesPresentUnknownOrderNoDefaultAndUnauthorable) {
  auto plugin = fixture();
  EXPECT_EQ(fo4::add_record_subrecord(plugin, key(0x800), sig("DATA")),
            fo4::SubrecordAddStatus::already_present);
  EXPECT_EQ(fo4::add_record_subrecord(plugin, key(0x800), sig("DESC")),
            fo4::SubrecordAddStatus::already_present);
  EXPECT_EQ(fo4::add_record_subrecord(plugin, key(0x804), sig("DESC")), fo4::SubrecordAddStatus::no_default);
  EXPECT_EQ(fo4::add_record_subrecord(plugin, key(0x801), sig("ICON")),
            fo4::SubrecordAddStatus::unknown_order);
  EXPECT_EQ(fo4::add_record_subrecord(plugin, key(0x804), sig("KWDA")),
            fo4::SubrecordAddStatus::not_authorable);
  EXPECT_EQ(fo4::add_record_subrecord(plugin, key(0x805), sig("EDID")),
            fo4::SubrecordAddStatus::already_present);
  EXPECT_EQ(fo4::add_record_subrecord(plugin, key(0x8FF), sig("ICON")),
            fo4::SubrecordAddStatus::record_missing);
  EXPECT_EQ(order(plugin, 0x801), (std::vector<std::string>{"EDID", "KWDA", "DATA"}));
}

TEST(SubrecordAuthoring, RefusedAddRollsBackToTheOriginalBytes) {
  auto plugin = fixture();
  const auto before = fo4::write_plugin(plugin);
  EXPECT_EQ(fo4::add_record_subrecord(plugin, key(0x803), sig("MICO")),
            fo4::SubrecordAddStatus::unknown_order);
  EXPECT_EQ(order(plugin, 0x803), (std::vector<std::string>{"DATA", "EDID"}));
  EXPECT_EQ(fo4::write_plugin(plugin), before);
  EXPECT_FALSE(fo4::test::record_original_bytes(plugin, *fo4::find_record(plugin, key(0x803))).empty());
}

TEST(SubrecordAuthoring, RemovesPhysicalOccurrenceAndEnforcesTheMinimum) {
  auto plugin = fixture();
  EXPECT_EQ(fo4::remove_record_subrecord(plugin, key(0x800), sig("DESC"), 0),
            fo4::SubrecordRemoveStatus::removed);
  EXPECT_EQ(order(plugin, 0x800), (std::vector<std::string>{"EDID", "MOD2", "ICO2", "DATA"}));
  EXPECT_EQ(fo4::remove_record_subrecord(plugin, key(0x800), sig("DESC"), 0),
            fo4::SubrecordRemoveStatus::absent);
  EXPECT_EQ(fo4::remove_record_subrecord(plugin, key(0x800), sig("ICO2"), 1),
            fo4::SubrecordRemoveStatus::absent);
  EXPECT_EQ(fo4::remove_record_subrecord(plugin, key(0x800), sig("DATA"), 0),
            fo4::SubrecordRemoveStatus::below_minimum);
  EXPECT_EQ(fo4::remove_record_subrecord(plugin, key(0x800), sig("KWDA"), 0),
            fo4::SubrecordRemoveStatus::not_authorable);
  EXPECT_EQ(fo4::remove_record_subrecord(plugin, key(0x8FF), sig("DESC"), 0),
            fo4::SubrecordRemoveStatus::record_missing);
  EXPECT_EQ(fo4::remove_record_subrecord(plugin, key(0x805), sig("FULL"), 0),
            fo4::SubrecordRemoveStatus::absent);

  const auto reloaded = reload(plugin);
  EXPECT_EQ(order(reloaded, 0x800), (std::vector<std::string>{"EDID", "MOD2", "ICO2", "DATA"}));
  EXPECT_TRUE(authoring_clean(reloaded, key(0x800)));
}

TEST(SubrecordAuthoring, CompressedRecordRoundTripsAddAndRemove) {
  auto plugin = fixture();
  ASSERT_TRUE(fo4::find_record(plugin, key(0x802))->compressed);

  EXPECT_EQ(fo4::add_record_subrecord(plugin, key(0x802), sig("ICON")), fo4::SubrecordAddStatus::added);
  auto reloaded = reload(plugin);
  EXPECT_TRUE(fo4::find_record(reloaded, key(0x802))->compressed);
  EXPECT_EQ(order(reloaded, 0x802),
            (std::vector<std::string>{"EDID", "MOD2", "ICON", "ICO2", "DESC", "DATA"}));
  EXPECT_EQ(find_sub(reloaded, 0x802, "MOD2")->data, zstr("a.nif"));

  EXPECT_EQ(fo4::remove_record_subrecord(reloaded, key(0x802), sig("ICON"), 0),
            fo4::SubrecordRemoveStatus::removed);
  const auto again = reload(reloaded);
  EXPECT_TRUE(fo4::find_record(again, key(0x802))->compressed);
  EXPECT_EQ(order(again, 0x802), (std::vector<std::string>{"EDID", "MOD2", "ICO2", "DESC", "DATA"}));
  EXPECT_EQ(find_sub(again, 0x802, "DATA")->data, filled(12, 7));
}

TEST(SubrecordAuthoring, CountedRepeatedEntriesAreNeverEdited) {
  auto plugin = fixture();
  const auto before = fo4::write_plugin(plugin);
  EXPECT_EQ(fo4::add_record_subrecord(plugin, key(0x806), sig("LVLO")),
            fo4::SubrecordAddStatus::not_authorable);
  EXPECT_EQ(fo4::remove_record_subrecord(plugin, key(0x806), sig("LVLO"), 0),
            fo4::SubrecordRemoveStatus::not_authorable);
  EXPECT_EQ(fo4::remove_record_subrecord(plugin, key(0x806), sig("LLCT"), 0),
            fo4::SubrecordRemoveStatus::below_minimum);
  EXPECT_EQ(fo4::write_plugin(plugin), before);

  auto generic = generic_fixture();
  EXPECT_EQ(fo4::add_record_subrecord(generic, key(0x800), sig("FFFF"), k_rules),
            fo4::SubrecordAddStatus::not_authorable);
  EXPECT_EQ(fo4::remove_record_subrecord(generic, key(0x800), sig("FFFF"), 0, k_rules),
            fo4::SubrecordRemoveStatus::not_authorable);
}

TEST(SubrecordAuthoring, CoupledEntriesAreNeverAddedOrRemoved) {
  auto plugin = fo4::read_plugin(
      build({{"ARMO", 0x800, armo_base()},
             {"ARMO", 0x801, {{"EDID", zstr("Plain")}, {"DATA", filled(12, 0)}}},
             {"EXPL", 0x802,
              {{"EDID", zstr("Boom")}, {"OBND", filled(12, 0)}, {"MODL", zstr("e.nif")},
               {"MODT", filled(12, 1)}, {"DATA", filled(84, 0)}}},
             {"PROJ", 0x803,
              {{"EDID", zstr("Shot")}, {"OBND", filled(12, 0)}, {"MODL", zstr("p.nif")},
               {"DEST", filled(8, 0)}, {"DSTD", filled(20, 0)}, {"DMDL", zstr("d.nif")},
               {"DMDT", filled(12, 2)}, {"DSTF", {}}, {"DATA", filled(92, 0)},
               {"DNAM", filled(4, 0)}, {"NAM1", zstr("m.nif")}, {"VNAM", filled(4, 0)}}}}),
      k_plugin);
  fo4::assign_record_form_keys(plugin);
  const auto before = fo4::write_plugin(plugin);

  const std::pair<std::uint32_t, const char*> coupled[] = {
      {0x800, "MOD2"}, {0x801, "MOD2"}, {0x801, "MOD4"}, {0x802, "MODL"}, {0x803, "MODL"},
      {0x803, "DEST"}, {0x803, "DSTD"}, {0x803, "DMDL"}, {0x803, "DMDT"}, {0x803, "DSTF"}};
  for (const auto& [local, name] : coupled) {
    EXPECT_EQ(fo4::add_record_subrecord(plugin, key(local), sig(name)),
              fo4::SubrecordAddStatus::not_authorable) << name;
    EXPECT_EQ(fo4::remove_record_subrecord(plugin, key(local), sig(name), 0),
              fo4::SubrecordRemoveStatus::not_authorable) << name;
  }
  EXPECT_EQ(fo4::write_plugin(plugin), before);
  EXPECT_EQ(fo4::remove_record_subrecord(plugin, key(0x802), sig("MODT"), 0),
            fo4::SubrecordRemoveStatus::removed);

  auto generic = generic_fixture();
  EXPECT_EQ(fo4::add_record_subrecord(generic, key(0x800), sig("HHHH"), k_coupled_rules),
            fo4::SubrecordAddStatus::not_authorable);
  EXPECT_EQ(fo4::create_record(generic, sig("XXXX"), "", k_coupled_rules).status,
            fo4::RecordCreateStatus::not_authorable);
}

TEST(SubrecordAuthoring, CountSubrecordsStayCoupledToTheirEntries) {
  auto plugin = fo4::read_plugin(
      build({{"NPC_", 0x800, {{"EDID", zstr("Npc")}, {"KSIZ", filled(4, 1)}, {"KWDA", filled(4, 2)}}},
             {"LVLI", 0x801, {{"EDID", zstr("List")}, {"LLCT", filled(1, 1)}, {"LVLO", filled(12, 0)}}}}),
      k_plugin);
  fo4::assign_record_form_keys(plugin);
  const auto before = fo4::write_plugin(plugin);
  EXPECT_EQ(fo4::remove_record_subrecord(plugin, key(0x800), sig("KSIZ"), 0),
            fo4::SubrecordRemoveStatus::not_authorable);
  EXPECT_EQ(fo4::remove_record_subrecord(plugin, key(0x801), sig("LLCT"), 0),
            fo4::SubrecordRemoveStatus::not_authorable);
  EXPECT_EQ(fo4::write_plugin(plugin), before);

  const auto npc = fo4::schema::record_authoring_constraint(sig("NPC_"));
  ASSERT_TRUE(npc.has_value());
  const auto rules = fo4::schema::authoring_subrecords(*npc);
  const auto nam7 = std::find_if(rules.begin(), rules.end(),
                                 [](const auto& rule) { return rule.signature == sig("NAM7"); });
  ASSERT_NE(nam7, rules.end());
  EXPECT_EQ(nam7->minimum, 0);
}

TEST(SubrecordAuthoring, GeneratedSchemaMarksSiblingOwnersCoupled) {
  const std::pair<const char*, std::vector<std::string>> expected[] = {
      {"ARMA", {"MOD2", "MOD3", "MOD4", "MOD5"}},
      {"ARMO", {"MOD2", "MOD4", "DEST"}},
      {"COBJ", {"FVPA"}},
      {"EXPL", {"MODL"}},
      {"HAZD", {"MODL"}},
      {"PROJ", {"MODL", "DEST"}},
      {"SCOL", {"MODL", "ONAM", "DATA"}}};
  for (const auto& [record, signatures] : expected) {
    const auto constraint = fo4::schema::record_authoring_constraint(sig(record));
    ASSERT_TRUE(constraint.has_value()) << record;
    std::vector<std::string> marked;
    for (const auto& rule : fo4::schema::authoring_subrecords(*constraint)) {
      if (rule.coupled) marked.push_back(rule.signature.to_string());
    }
    EXPECT_EQ(marked, signatures) << record;
  }
}

TEST(SubrecordAuthoring, DescriptorRulesAppendRepeatedSubrecordsAfterTheLastOne) {
  auto plugin = generic_fixture();
  const auto record = key(0x800);

  EXPECT_EQ(fo4::add_record_subrecord(plugin, record, sig("BBBB"), k_rules), fo4::SubrecordAddStatus::added);
  EXPECT_EQ(order(plugin, 0x800),
            (std::vector<std::string>{"EDID", "BBBB", "BBBB", "BBBB", "CCCC"}));
  EXPECT_EQ(find_sub(plugin, 0x800, "BBBB", 2)->data, filled(2, 2));
  EXPECT_EQ(find_sub(plugin, 0x800, "BBBB", 1)->data, filled(2, 0x22));
  EXPECT_EQ(fo4::add_record_subrecord(plugin, record, sig("BBBB"), k_rules),
            fo4::SubrecordAddStatus::already_present);

  EXPECT_EQ(fo4::add_record_subrecord(plugin, record, sig("AAAA"), k_rules), fo4::SubrecordAddStatus::added);
  EXPECT_EQ(order(plugin, 0x800),
            (std::vector<std::string>{"EDID", "AAAA", "BBBB", "BBBB", "BBBB", "CCCC"}));
  EXPECT_EQ(fo4::add_record_subrecord(plugin, record, sig("DDDD"), k_rules),
            fo4::SubrecordAddStatus::no_default);
  EXPECT_EQ(fo4::add_record_subrecord(plugin, record, sig("ZZZZ"), k_rules),
            fo4::SubrecordAddStatus::not_authorable);

  EXPECT_EQ(fo4::remove_record_subrecord(plugin, record, sig("BBBB"), 1, k_rules),
            fo4::SubrecordRemoveStatus::removed);
  EXPECT_EQ(find_sub(plugin, 0x800, "BBBB", 0)->data, filled(2, 0x11));
  EXPECT_EQ(find_sub(plugin, 0x800, "BBBB", 1)->data, filled(2, 2));
  EXPECT_EQ(fo4::remove_record_subrecord(plugin, record, sig("CCCC"), 0, k_rules),
            fo4::SubrecordRemoveStatus::below_minimum);

  const auto reloaded = reload(plugin);
  EXPECT_EQ(order(reloaded, 0x800),
            (std::vector<std::string>{"EDID", "AAAA", "BBBB", "BBBB", "CCCC"}));
}

TEST(SubrecordAuthoring, DescriptorAddRefusesAnUnlistedNeighbourAndRollsBackABadOrder) {
  auto plugin = fo4::read_plugin(
      build({{"XXXX", 0x800, {{"EDID", zstr("Gen")}, {"QQQQ", filled(1, 1)}, {"CCCC", filled(3, 3)}}},
             {"XXXX", 0x801, {{"CCCC", filled(3, 3)}, {"EDID", zstr("Late")}}}}),
      k_plugin);
  fo4::assign_record_form_keys(plugin);
  EXPECT_EQ(fo4::add_record_subrecord(plugin, key(0x800), sig("AAAA"), k_rules),
            fo4::SubrecordAddStatus::unknown_order);
  EXPECT_EQ(fo4::add_record_subrecord(plugin, key(0x801), sig("AAAA"), k_rules),
            fo4::SubrecordAddStatus::unknown_order);
  EXPECT_EQ(order(plugin, 0x801), (std::vector<std::string>{"CCCC", "EDID"}));
}

TEST(SubrecordAuthoring, CreatesRecordsFromGeneratedDefaultsInAuthoringOrder) {
  auto plugin = fo4::read_plugin(build({}), k_plugin);

  const auto armor = fo4::create_record(plugin, sig("ARMO"), "NewArmor");
  ASSERT_EQ(armor.status, fo4::RecordCreateStatus::created);
  EXPECT_EQ(armor.key, key(0x900));
  EXPECT_EQ(order(plugin, 0x900), (std::vector<std::string>{"EDID", "DATA"}));
  EXPECT_EQ(find_sub(plugin, 0x900, "EDID")->data, zstr("NewArmor"));
  EXPECT_EQ(find_sub(plugin, 0x900, "DATA")->data, filled(12, 0));

  const auto addon = fo4::create_record(plugin, sig("ARMA"), "");
  ASSERT_EQ(addon.status, fo4::RecordCreateStatus::created);
  EXPECT_EQ(order(plugin, 0x901), (std::vector<std::string>{"DNAM"}));
  EXPECT_EQ(find_sub(plugin, 0x901, "DNAM")->data, filled(12, 0));

  const auto spell_list = fo4::create_record(plugin, sig("LVSP"), "NewList");
  ASSERT_EQ(spell_list.status, fo4::RecordCreateStatus::created);
  EXPECT_EQ(order(plugin, 0x902), (std::vector<std::string>{"EDID", "OBND", "LVLD", "LVLM", "LVLF", "LLCT"}));

  for (const auto& created : {armor.key, addon.key, spell_list.key}) {
    EXPECT_TRUE(authoring_clean(plugin, created));
  }
  const auto reloaded = reload(plugin);
  EXPECT_EQ(reloaded.header.next_form_id, 0x903u);
  EXPECT_EQ(order(reloaded, 0x900), (std::vector<std::string>{"EDID", "DATA"}));
  EXPECT_EQ(order(reloaded, 0x902), (std::vector<std::string>{"EDID", "OBND", "LVLD", "LVLM", "LVLF", "LLCT"}));
  for (const auto& created : {armor.key, addon.key, spell_list.key}) {
    EXPECT_TRUE(authoring_clean(reloaded, created));
  }
}

TEST(SubrecordAuthoring, RefusedCreationConsumesNoFormId) {
  auto plugin = fo4::read_plugin(build({}), k_plugin);
  const auto before = fo4::write_plugin(plugin);

  EXPECT_EQ(fo4::create_record(plugin, sig("XXXX"), "X").status, fo4::RecordCreateStatus::not_authorable);
  EXPECT_EQ(fo4::create_record(plugin, sig("EYES"), "X").status, fo4::RecordCreateStatus::no_default);
  EXPECT_EQ(fo4::create_record(plugin, sig("ARMO"), std::string_view{"A\0B", 3}).status,
            fo4::RecordCreateStatus::invalid_editor_id);
  EXPECT_EQ(plugin.header.next_form_id, 0x900u);
  EXPECT_TRUE(plugin.groups.empty());
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(SubrecordAuthoring, DescriptorCreationRepeatsMinimumCountsAndRequiresEveryDefault) {
  auto plugin = fo4::read_plugin(build({}), k_plugin);

  const auto blocked = fo4::create_record(plugin, sig("XXXX"), "Gen", k_create_blocked);
  EXPECT_EQ(blocked.status, fo4::RecordCreateStatus::no_default);
  EXPECT_EQ(plugin.header.next_form_id, 0x900u);

  const auto created = fo4::create_record(plugin, sig("XXXX"), "Gen", k_create_rules);
  ASSERT_EQ(created.status, fo4::RecordCreateStatus::created);
  EXPECT_EQ(order(plugin, 0x900), (std::vector<std::string>{"EDID", "CCCC", "EEEE", "EEEE"}));
  EXPECT_EQ(find_sub(plugin, 0x900, "CCCC")->data, filled(3, 3));

  const auto reloaded = reload(plugin);
  EXPECT_EQ(order(reloaded, 0x900), (std::vector<std::string>{"EDID", "CCCC", "EEEE", "EEEE"}));
  EXPECT_EQ(fo4::create_record(plugin, sig("XXXX"), "Gen", {}).status,
            fo4::RecordCreateStatus::not_authorable);
}

TEST(SubrecordAuthoring, CreationRefusesRequiredCountedRepeatedEntries) {
  auto plugin = fo4::read_plugin(build({}), k_plugin);
  EXPECT_EQ(fo4::create_record(plugin, sig("XXXX"), "", k_create_counted).status,
            fo4::RecordCreateStatus::not_authorable);
  EXPECT_EQ(plugin.header.next_form_id, 0x900u);
}

TEST(SubrecordAuthoring, RefusesDefaultFieldsInLocalizedPlugins) {
  auto plugin = fo4::read_plugin(build({}), k_plugin);
  const auto created = fo4::create_record(plugin, sig("ARMO"), "X", k_localized_rules);
  ASSERT_EQ(created.status, fo4::RecordCreateStatus::created);
  ASSERT_EQ(order(plugin, 0x900), (std::vector<std::string>{"EDID", "FULL"}));
  EXPECT_EQ(find_sub(plugin, 0x900, "FULL")->data, filled(1, 1));
  plugin.header.tes4_flags = fo4::RecordFlags{plugin.header.tes4_flags.value() | fo4::RecordFlags::localized};
  ASSERT_TRUE(plugin.header.tes4_flags.is_localized());
  EXPECT_EQ(fo4::create_record(plugin, sig("ARMO"), "X", k_localized_rules).status,
            fo4::RecordCreateStatus::no_default);
  EXPECT_EQ(fo4::create_record(plugin, sig("ARMO"), "X").status, fo4::RecordCreateStatus::created);
}

TEST(SubrecordAuthoring, CreatesRecordsWhoseRequiredSubrecordsHaveAuthoredDefaults) {
  auto plugin = fo4::read_plugin(build({}), k_plugin);
  struct Case {
    const char* record;
    const char* editor_id;
    std::vector<std::string> order;
    std::vector<std::pair<std::string, std::size_t>> sizes;
  };
  const std::vector<Case> cases{
      {"NPC_", "NewNpc", {"EDID", "OBND", "ACBS", "RNAM", "AIDT", "CNAM", "DATA", "NAM5", "NAM6", "NAM8"},
       {{"RNAM", 4}, {"CNAM", 4}, {"DATA", 0}, {"NAM5", 2}, {"NAM6", 4}, {"NAM8", 4}}},
      {"ADDN", "", {"DATA", "DNAM"}, {{"DATA", 4}, {"DNAM", 4}}},
      {"MESG", "", {"DESC", "INAM", "DNAM"}, {{"DESC", 1}, {"INAM", 4}, {"DNAM", 4}}},
      {"LSCR", "", {"DESC"}, {{"DESC", 1}}},
      {"IMAD", "NewImad", {"EDID", "DNAM", "TNAM", "NAM3"}, {{"DNAM", 252}, {"TNAM", 40}, {"NAM3", 40}}},
      {"DOBJ", "", {"DNAM"}, {{"DNAM", 8}}},
      {"DEBR", "", {"DATA"}, {{"DATA", 3}}},
  };
  std::uint32_t local = 0x900;
  for (const auto& test : cases) {
    const auto created = fo4::create_record(plugin, sig(test.record), test.editor_id);
    ASSERT_EQ(created.status, fo4::RecordCreateStatus::created) << test.record;
    EXPECT_EQ(created.key, key(local)) << test.record;
    ++local;
  }
  const auto reloaded = reload(plugin);
  local = 0x900;
  for (const auto& test : cases) {
    for (const auto* target : {static_cast<const fo4::Plugin*>(&plugin), &reloaded}) {
      EXPECT_EQ(order(*target, local), test.order) << test.record;
      for (const auto& [name, size] : test.sizes) {
        const auto* sub = find_sub(*target, local, name.c_str());
        ASSERT_NE(sub, nullptr) << test.record << " " << name;
        EXPECT_EQ(sub->data.size(), size) << test.record << " " << name;
      }
      EXPECT_TRUE(authoring_clean(*target, key(local))) << test.record;
    }
    ++local;
  }
}

TEST(SubrecordAuthoring, LocalizedDescriptionsStillRefuseCreateDefaults) {
  auto plugin = fo4::read_plugin(build({}), k_plugin);
  plugin.header.tes4_flags = fo4::RecordFlags{plugin.header.tes4_flags.value() | fo4::RecordFlags::localized};
  EXPECT_EQ(fo4::create_record(plugin, sig("LSCR"), "").status, fo4::RecordCreateStatus::no_default);
  EXPECT_EQ(fo4::create_record(plugin, sig("MESG"), "").status, fo4::RecordCreateStatus::no_default);
  EXPECT_EQ(plugin.header.next_form_id, 0x900u);
}

TEST(SubrecordAuthoring, GameSettingStringDefaultIsInlineTextOutsideALocalizedPlugin) {
  auto plugin = fo4::read_plugin(build({}), k_plugin);

  const auto text = fo4::create_record(plugin, sig("GMST"), "sNewSetting");
  ASSERT_EQ(text.status, fo4::RecordCreateStatus::created);
  EXPECT_EQ(order(plugin, 0x900), (std::vector<std::string>{"EDID", "DATA"}));
  EXPECT_EQ(find_sub(plugin, 0x900, "DATA")->data, zstr(""));
  EXPECT_TRUE(authoring_clean(plugin, text.key));

  const auto count = fo4::create_record(plugin, sig("GMST"), "iNewSetting");
  ASSERT_EQ(count.status, fo4::RecordCreateStatus::created);
  EXPECT_EQ(find_sub(plugin, 0x901, "DATA")->data, filled(4, 0));

  plugin.header.tes4_flags = fo4::RecordFlags{plugin.header.tes4_flags.value() | fo4::RecordFlags::localized};
  const auto localized = fo4::create_record(plugin, sig("GMST"), "sLocalizedSetting");
  ASSERT_EQ(localized.status, fo4::RecordCreateStatus::created);
  EXPECT_EQ(find_sub(plugin, 0x902, "DATA")->data, filled(4, 0));
}

TEST(SubrecordAuthoring, HeadPartPartsRefuseGeneratedAddAndRemoveAsNotAuthorable) {
  const Subs head{{"EDID", zstr("Paired")}, {"DATA", filled(1, 0)}, {"PNAM", filled(4, 0)},
                   {"NAM0", filled(4, 0)}, {"NAM1", zstr("a.nif")}};
  Subs unpaired = head;
  unpaired.push_back({"NAM0", filled(4, 0)});
  Subs headless = head;
  headless.erase(headless.begin() + 1);
  Subs headless_unpaired = headless;
  headless_unpaired.insert(headless_unpaired.begin() + 3, {"NAM0", filled(4, 0)});
  auto plugin = fo4::read_plugin(build({{"HDPT", 0x810, head},
                                        {"HDPT", 0x811, unpaired},
                                        {"HDPT", 0x812, headless},
                                        {"HDPT", 0x813, headless_unpaired}}),
                                 k_plugin);
  fo4::assign_record_form_keys(plugin);

  EXPECT_TRUE(authoring_clean(plugin, key(0x810)));
  EXPECT_FALSE(authoring_clean(plugin, key(0x811)));
  EXPECT_FALSE(authoring_clean(plugin, key(0x813)));
  EXPECT_EQ(fo4::add_record_subrecord(plugin, key(0x810), sig("NAM0")),
            fo4::SubrecordAddStatus::not_authorable);
  EXPECT_EQ(fo4::remove_record_subrecord(plugin, key(0x810), sig("NAM0"), 0),
            fo4::SubrecordRemoveStatus::not_authorable);
  EXPECT_EQ(fo4::add_record_subrecord(plugin, key(0x812), sig("DATA")),
            fo4::SubrecordAddStatus::added);
  EXPECT_EQ(fo4::add_record_subrecord(plugin, key(0x813), sig("DATA")),
            fo4::SubrecordAddStatus::added);
  EXPECT_EQ(order(plugin, 0x812),
            (std::vector<std::string>{"EDID", "DATA", "PNAM", "NAM0", "NAM1"}));
  EXPECT_EQ(order(plugin, 0x813),
            (std::vector<std::string>{"EDID", "DATA", "PNAM", "NAM0", "NAM0", "NAM1"}));
  EXPECT_TRUE(authoring_clean(plugin, key(0x812)));
  EXPECT_FALSE(authoring_clean(plugin, key(0x813)));
}

TEST(SubrecordAuthoring, RemoveRefusesDebrDataWhoseModelTextureHasNoRule) {
  auto plugin = fo4::read_plugin(
      build({{"DEBR", 0x800,
              {{"EDID", zstr("Gen")}, {"DATA", filled(3, 0)}, {"MODT", filled(4, 1)}, {"DATA", filled(3, 0)},
               {"MODT", filled(4, 2)}}},
             {"DEBR", 0x801, {{"EDID", zstr("Two")}, {"DATA", filled(3, 0)}, {"DATA", filled(3, 0)}}}}),
      k_plugin);
  fo4::assign_record_form_keys(plugin);
  EXPECT_EQ(fo4::remove_record_subrecord(plugin, key(0x800), sig("DATA"), 1),
            fo4::SubrecordRemoveStatus::unknown_order);
  EXPECT_EQ(order(plugin, 0x800), (std::vector<std::string>{"EDID", "DATA", "MODT", "DATA", "MODT"}));
  EXPECT_EQ(fo4::remove_record_subrecord(plugin, key(0x801), sig("DATA"), 1), fo4::SubrecordRemoveStatus::removed);
  EXPECT_EQ(order(plugin, 0x801), (std::vector<std::string>{"EDID", "DATA"}));
}

TEST(SubrecordAuthoring, HazdCreateAuthorsEdidObndDnamWithoutModel) {
  auto plugin = fo4::read_plugin(build({}), k_plugin);
  const auto created = fo4::create_record(plugin, sig("HAZD"), "Gen");
  ASSERT_EQ(created.status, fo4::RecordCreateStatus::created);
  EXPECT_EQ(created.key, key(0x900));
  EXPECT_EQ(order(plugin, 0x900), (std::vector<std::string>{"EDID", "OBND", "DNAM"}));
  const auto* dnam = find_sub(plugin, 0x900, "DNAM");
  ASSERT_NE(dnam, nullptr);
  EXPECT_EQ(dnam->data.size(), 52u);
  EXPECT_TRUE(authoring_clean(plugin, key(0x900)));
}

TEST(SubrecordAuthoring, MiscCreatesFromItsGeneratedDefaults) {
  auto plugin = fo4::read_plugin(build({}), k_plugin);

  const auto misc = fo4::create_record(plugin, sig("MISC"), "NewMisc");
  ASSERT_EQ(misc.status, fo4::RecordCreateStatus::created);
  EXPECT_EQ(order(plugin, 0x900), (std::vector<std::string>{"EDID", "OBND", "DATA"}));
  EXPECT_EQ(find_sub(plugin, 0x900, "DATA")->data, filled(8, 0));
  EXPECT_TRUE(authoring_clean(plugin, misc.key));
}

TEST(SubrecordAuthoring, MiscDestructionStagesFollowTheXEditOrder) {
  auto plugin = fo4::read_plugin(
      build({{"MISC", 0x810,
              {{"EDID", zstr("Stages")}, {"OBND", filled(12, 0)}, {"DEST", filled(8, 0)},
               {"DSTD", filled(20, 0)}, {"DSTF", filled(0, 0)}, {"DATA", filled(8, 0)},
               {"CVPA", filled(8, 0)}}},
             {"MISC", 0x811,
              {{"EDID", zstr("Late")}, {"OBND", filled(12, 0)}, {"DATA", filled(8, 0)},
               {"DEST", filled(8, 0)}}}}),
      k_plugin);
  fo4::assign_record_form_keys(plugin);
  EXPECT_TRUE(authoring_clean(plugin, key(0x810)));
  EXPECT_FALSE(authoring_clean(plugin, key(0x811)));
}

TEST(SubrecordAuthoringRealData, AddRemoveKeepEveryShippedRecordConsistent) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR to the Fallout 4 Data folder to run this";
  record_game_build(detect_game_build(dir));
  const auto path = std::filesystem::path{dir} / "Fallout4.esm";
  if (!std::filesystem::exists(path)) GTEST_SKIP() << "Fallout4.esm not found";

  const auto started = std::chrono::steady_clock::now();
  auto plugin = fo4::read_plugin_file(path);
  const auto loaded = std::chrono::steady_clock::now();
  std::size_t calls = 0;
  std::chrono::steady_clock::duration call_time{};

  std::map<std::string, std::size_t> tally;
  for (const char* family : {"ARMO", "ARMA", "EYES", "LVSP"}) {
    const auto record_signature = sig(family);
    const auto constraint = fo4::schema::record_authoring_constraint(record_signature);
    ASSERT_TRUE(constraint.has_value());
    std::vector<fo4::FormKey> targets;
    for (const auto* record : fo4::collect_records(plugin)) {
      if (record->header.signature == record_signature && targets.size() < 6) {
        targets.push_back(record->form_key);
      }
    }

    if (targets.empty()) continue;
    for (const auto& target : targets) {
      const auto& tracked = *fo4::find_record(plugin, target);
      for (const auto& rule : fo4::schema::authoring_subrecords(*constraint)) {
        const auto before = tracked.subrecords;
        ++calls;
        const auto call_started = std::chrono::steady_clock::now();
        const auto status = fo4::add_record_subrecord(plugin, target, rule.signature);
        call_time += std::chrono::steady_clock::now() - call_started;
        const auto& after = tracked.subrecords;
        ++tally[std::string(family) + (status == fo4::SubrecordAddStatus::added            ? " added"
                                       : status == fo4::SubrecordAddStatus::already_present ? " present"
                                       : status == fo4::SubrecordAddStatus::no_default      ? " no_default"
                                       : status == fo4::SubrecordAddStatus::not_authorable  ? " not_authorable"
                                                                                            : " unknown_order")];
        if (rule.coupled) {
          EXPECT_EQ(status, fo4::SubrecordAddStatus::not_authorable);
        }
        if (status != fo4::SubrecordAddStatus::added) {
          EXPECT_EQ(after.size(), before.size());
          continue;
        }
        EXPECT_TRUE(authoring_clean(plugin, target)) << target.to_string();
        EXPECT_EQ(after.size(), before.size() + 1);
        EXPECT_EQ(fo4::remove_record_subrecord(plugin, target, rule.signature, 0),
                  fo4::SubrecordRemoveStatus::removed);
        const auto& restored = tracked.subrecords;
        ASSERT_EQ(restored.size(), before.size());
        for (std::size_t index = 0; index < before.size(); ++index) {
          EXPECT_EQ(restored[index].signature, before[index].signature);
          EXPECT_EQ(restored[index].data, before[index].data);
        }
      }
    }
  }
  const auto finished = std::chrono::steady_clock::now();
  std::printf("add only %lld ms; load %lld ms, %zu add calls %lld ms\n",
              static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(call_time).count()),
              static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(loaded - started).count()),
              calls,
              static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(finished - loaded).count()));
  for (const auto& [name, count] : tally) std::printf("%s %zu\n", name.c_str(), count);
}
