#include "behavior_writer_fixture.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/subrecord_authoring.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/field_address.hpp"
#include "fo4/schema/form_links.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/scalar.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <map>

namespace {
using namespace behavior_writer_test;
fo4::Signature sig(std::string_view value) { return fo4::Signature::from_string(value); }
auto scalar(std::string_view path) { return *fo4::schema::scalar_field_descriptor(path); }
auto link(std::string_view path) { return *fo4::schema::formlink_field_descriptor(path); }

TEST(BehaviorAuthoring, EveryRegisteredFieldRoundTripsAndPreservesOtherPayloadBytes) {
  auto plugin = load();
  const auto original = plugin;
  std::map<fo4::FormKey, std::vector<Bytes>> expected;
  for (const auto& [tag, id] : {std::pair{"PACK", 0x800u}, {"CSTY", 0x801u}, {"IDLE", 0x802u}}) {
    const auto record_key = key(id);
    const auto* record = fo4::find_record(original, record_key);
    auto& want = expected[record_key];
    for (const auto& sub : record->subrecords) want.push_back(sub.data);
    for (const auto& field : fo4::schema::scalar_fields(sig(tag))) {
      SCOPED_TRACE(field.path);
      const auto slots = fo4::schema::resolve_field_slots(*record, fo4::schema::scalar_field_shape(field.scalar), field.scalar.address);
      for (std::size_t i = 0; i < slots.slots.size(); ++i) {
        const auto& slot = slots.slots[i];
        if (!slot.valid) {
          EXPECT_EQ(fo4::set_record_scalar(plugin, record_key, field, i, "1"), fo4::ScalarMutationStatus::subrecord_missing);
          continue;
        }
        const bool string = field.scalar.storage == fo4::schema::ScalarStorage::zstring;
        const auto status = string ? fo4::set_record_string(plugin, record_key, field, i, "edited")
                                   : fo4::set_record_scalar(plugin, record_key, field, i, "1");
        EXPECT_TRUE(status == fo4::ScalarMutationStatus::updated || status == fo4::ScalarMutationStatus::unchanged);
        const auto encoded = fo4::schema::encode_scalar(string ? "edited" : "1", field.scalar);
        if (string) want[slot.subrecord] = encoded;
        else std::copy(encoded.begin(), encoded.end(), want[slot.subrecord].begin() + static_cast<std::ptrdiff_t>(slot.offset));
      }
    }
    for (const auto& field : fo4::schema::formlink_fields(sig(tag))) {
      const fo4::schema::FieldShape shape{field.subrecord, field.offset, 4, 0, false};
      const auto slots = fo4::schema::resolve_field_slots(*record, shape, field.address);
      for (std::size_t i = 0; i < slots.slots.size(); ++i) {
        const auto& slot = slots.slots[i];
        if (!slot.valid || !field.nullable) continue;
        const auto status = fo4::set_record_formlink_field(plugin, record_key, field, i, {});
        EXPECT_TRUE(status == fo4::FormLinkFieldMutationStatus::updated || status == fo4::FormLinkFieldMutationStatus::unchanged);
        std::fill_n(want[slot.subrecord].begin() + static_cast<std::ptrdiff_t>(slot.offset), 4, std::byte{0});
      }
    }
  }
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Behavior.esp");
  fo4::visit_records(original, [&](const fo4::Record& before) {
    if (before.header.signature == sig("TES4")) return;
    const auto* after = fo4::find_record(reread, before.form_key);
    ASSERT_NE(after, nullptr);
    const auto it = expected.find(before.form_key);
    if (it == expected.end()) {
      EXPECT_EQ(fo4::test::record_original_bytes(original, before), fo4::test::record_original_bytes(reread, *after));
      return;
    }
    ASSERT_EQ(after->subrecords.size(), it->second.size());
    for (std::size_t i = 0; i < it->second.size(); ++i) {
      EXPECT_EQ(after->subrecords[i].signature, before.subrecords[i].signature);
      EXPECT_EQ(after->subrecords[i].data, it->second[i]);
    }
  });
}

TEST(BehaviorAuthoring, DiscriminatorsShortTailsAndMalformedWidthsRefuseWithoutMutation) {
  auto pack = package(); pack.emplace_back("PLDT", words({0xAABBCC06, 0x803, 3}));
  auto plugin = load(pack, style(9, 3));
  const auto before = fo4::write_plugin(plugin);
  EXPECT_EQ(fo4::record_field_count(plugin, key(), "PACK\\CombatStyle"), 1u);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key(), link("PACK\\CombatStyle"), 1, key(0x801)),
            fo4::FormLinkFieldMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key(), link("PACK\\LocationKeyword"), 4, key(0x803)),
            fo4::FormLinkFieldMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(), scalar("PACK\\LocationRadius"), 2, "1"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(), scalar("PACK\\LocationRadius"), 4, "1"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(0x801), scalar("CSTY\\AttackWhenStaggeredMult"), 0, "1"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(0x801), scalar("CSTY\\RangeMult"), 0, "1"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_THROW(fo4::set_record_formlink_field(plugin, key(), link("PACK\\LocationKeyword"), 0, key(0x805)),
               fo4::schema::FormLinkFieldError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
  EXPECT_FALSE(fo4::schema::scalar_field_descriptor("PACK\\LocationType"));
  EXPECT_FALSE(fo4::schema::scalar_field_descriptor("PACK\\InputCount"));
}

TEST(BehaviorAuthoring, PackScheduleDateRefusesDaysOutsideXEditRange) {
  auto plugin = load();
  const auto before = fo4::write_plugin(plugin);
  EXPECT_THROW(fo4::set_record_scalar(plugin, key(), scalar("PACK\\Date"), "40"), fo4::schema::ScalarError);
  EXPECT_THROW(fo4::set_record_scalar(plugin, key(), scalar("PACK\\Date"), "-5"), fo4::schema::ScalarError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(), scalar("PACK\\Date"), "15"), fo4::ScalarMutationStatus::updated);
  const auto* record = fo4::find_record(plugin, key());
  const auto psdt = std::find_if(record->subrecords.begin(), record->subrecords.end(),
                                 [](const auto& sub) { return sub.signature == sig("PSDT"); });
  ASSERT_NE(psdt, record->subrecords.end());
  EXPECT_EQ(std::to_integer<int>(psdt->data[2]), 15);
}

TEST(BehaviorAuthoring, CreateAddRemoveDefaultsAndCoupledRefusals) {
  auto plugin = load();
  EXPECT_EQ(fo4::create_record(plugin, sig("PACK"), "New").status, fo4::RecordCreateStatus::not_authorable);
  const auto style_key = fo4::create_record(plugin, sig("CSTY"), "NewStyle");
  ASSERT_EQ(style_key.status, fo4::RecordCreateStatus::created);
  const auto idle_key = fo4::create_record(plugin, sig("IDLE"), "NewIdle");
  ASSERT_EQ(idle_key.status, fo4::RecordCreateStatus::created);
  EXPECT_EQ(fo4::find_record(plugin, style_key.key)->subrecords[3].data,
            (Bytes{std::byte{0}, std::byte{0}, std::byte{0x40}, std::byte{0x3F}}));
  EXPECT_EQ(fo4::add_record_subrecord(plugin, idle_key.key, sig("DNAM")), fo4::SubrecordAddStatus::added);
  EXPECT_EQ(fo4::set_record_string(plugin, idle_key.key, "IDLE\\BehaviorGraph", 0, "new.hkx"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::remove_record_subrecord(plugin, idle_key.key, sig("DNAM"), 0), fo4::SubrecordRemoveStatus::removed);
  const auto before = fo4::write_plugin(plugin);
  for (const auto tag : {"PKCU", "POBA", "POEA", "POCA", "XNAM"}) {
    EXPECT_EQ(fo4::add_record_subrecord(plugin, key(), sig(tag)), fo4::SubrecordAddStatus::not_authorable);
    EXPECT_EQ(fo4::remove_record_subrecord(plugin, key(), sig(tag), 0), fo4::SubrecordRemoveStatus::not_authorable);
  }
  EXPECT_EQ(fo4::remove_record_subrecord(plugin, idle_key.key, sig("DATA"), 0), fo4::SubrecordRemoveStatus::below_minimum);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(BehaviorAuthoring, RemapOwnsOnlyDiscriminatedLinksAndPreservesNumericLookalikes) {
  auto plugin = load();
  const auto original = plugin;
  fo4::rekey_form_links(plugin, key(0x804), key(0x808));
  const auto* pack = fo4::find_record(plugin, key());
  const auto* before = fo4::find_record(original, key());
  for (std::size_t i = 0; i < pack->subrecords.size(); ++i) {
    if (i == 10) { EXPECT_EQ(pack->subrecords[i].data, words({0, 0x808})); }
    else { EXPECT_EQ(pack->subrecords[i].data, before->subrecords[i].data); }
  }
  fo4::rekey_form_links(plugin, key(0x807), key(0x802));
  EXPECT_EQ(fo4::find_record(plugin, key(0x802))->subrecords[3].data, words({0x802, 0x802}));
  auto bad = package(); bad.emplace_back("PLDT", words({99, 0x804, 0}));
  auto opaque = load(bad); const auto bytes = fo4::write_plugin(opaque);
  EXPECT_THROW(fo4::rekey_form_links(opaque, key(0x804), key(0x808)), fo4::FormIdError);
  EXPECT_EQ(fo4::write_plugin(opaque), bytes);
}

TEST(BehaviorAuthoring, AmbiguousCounterOwnershipRefusesStrictRemapTransactionally) {
  for (const bool duplicate : {false, true}) {
    auto pack = package();
    if (duplicate) pack.insert(pack.begin()+6,{"PKCU",words({3,0x800,9})});
    else pack[5].second = words({0x800});
    auto plugin = load(pack);
    const auto bytes = fo4::write_plugin(plugin);
    EXPECT_THROW(fo4::rekey_form_links(plugin,key(0x801),key(0x808)),fo4::FormIdError);
    EXPECT_EQ(fo4::write_plugin(plugin),bytes);
  }
}

TEST(BehaviorAuthoring, NonzeroUnknownVariantsRefuseStrictRemapWhileZeroAndLinkVariantsRemap) {
  for (const auto& [tag, kind] : std::vector<std::pair<std::string, std::uint32_t>>{
           {"PLDT", 12}, {"PLDT", 13}, {"PTDA", 6}, {"PTDA", 8}}) {
    auto pack = package(); pack.emplace_back(tag, words({kind, 0x804, 0}));
    auto plugin = load(pack);
    const auto bytes = fo4::write_plugin(plugin);
    EXPECT_THROW(fo4::rekey_form_links(plugin, key(0x804), key(0x808)), fo4::FormIdError);
    EXPECT_EQ(fo4::write_plugin(plugin), bytes);
    auto zero = package(); zero.emplace_back(tag, words({kind, 0, 0}));
    auto zero_plugin = load(zero);
    EXPECT_NO_THROW(fo4::rekey_form_links(zero_plugin, key(0x804), key(0x808)));
    EXPECT_EQ(fo4::find_record(zero_plugin, key())->subrecords.back().data, words({kind, 0, 0}));
  }
  auto pack = package(); pack.emplace_back("PLDT", words({1, 0x804, 0}));
  auto plugin = load(pack);
  fo4::rekey_form_links(plugin, key(0x804), key(0x808));
  EXPECT_EQ(fo4::find_record(plugin, key())->subrecords.back().data, words({1, 0x808, 0}));
}

TEST(BehaviorAuthoring, IdleAnimationListRemapsEveryEntryAndRefusesPartialWidth) {
  auto pack = package(); pack.insert(pack.begin() + 3, {"IDLA", words({0x804, 0, 0x803})});
  auto plugin = load(pack);
  fo4::rekey_form_links(plugin, key(0x804), key(0x808));
  EXPECT_EQ(fo4::find_record(plugin, key())->subrecords[3].data, words({0x808, 0, 0x803}));
  auto bad = package(); bad.insert(bad.begin() + 3, {"IDLA", Bytes(6)});
  auto opaque = load(bad); const auto bytes = fo4::write_plugin(opaque);
  EXPECT_THROW(fo4::rekey_form_links(opaque, key(0x804), key(0x808)), fo4::FormIdError);
  EXPECT_EQ(fo4::write_plugin(opaque), bytes);
}

}
