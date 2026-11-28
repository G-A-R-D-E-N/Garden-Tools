#include "dialogue_writer_fixture.hpp"

#include "fo4/schema/record_constraints.hpp"

#include <gtest/gtest.h>

#include <string>

namespace {

using namespace fo4::test::dialogue;
using Add = fo4::SubrecordAddStatus;
using Remove = fo4::SubrecordRemoveStatus;

Add add(fo4::Plugin& plugin, std::uint32_t id, const char* name) {
  return fo4::add_record_subrecord(plugin, key(id), sig(name));
}

Remove remove(fo4::Plugin& plugin, std::uint32_t id, const char* name, std::size_t index = 0) {
  return fo4::remove_record_subrecord(plugin, key(id), sig(name), index);
}

bool has_issue(const fo4::Plugin& plugin, std::uint32_t id, const std::string& code) {
  for (const auto& issue : fo4::schema::preflight_authoring_record(plugin, key(id))) {
    if (issue.code == code) { return true; }
  }
  return false;
}

}

TEST(DialogueSubrecords, TopicOptionalSubrecordsFollowTheXEditOrder) {
  auto plugin = read(build());
  EXPECT_EQ(add(plugin, k_bare, "DATA"), Add::added);
  EXPECT_EQ(add(plugin, k_bare, "FULL"), Add::added);
  EXPECT_EQ(add(plugin, k_bare, "PNAM"), Add::already_present);
  EXPECT_EQ(order(plugin, k_bare), (Order{"EDID", "FULL", "PNAM", "DATA"}));
  EXPECT_EQ(payloads(plugin, k_bare, "DATA").at(0), words({0}));
  EXPECT_EQ(payloads(plugin, k_bare, "FULL").at(0), zstr(""));
  for (const char* unproven : {"BNAM", "KNAM", "SNAM"}) {
    EXPECT_EQ(add(plugin, k_bare, unproven), Add::no_default) << unproven;
  }
  for (const char* coupled : {"QNAM", "TIFC"}) {
    EXPECT_EQ(add(plugin, k_bare, coupled), Add::not_authorable) << coupled;
    EXPECT_EQ(remove(plugin, k_topic, coupled), Remove::not_authorable) << coupled;
  }
  EXPECT_EQ(remove(plugin, k_bare, "PNAM"), Remove::below_minimum);
  EXPECT_EQ(remove(plugin, k_topic, "KNAM"), Remove::removed);
  EXPECT_EQ(remove(plugin, k_topic, "FULL"), Remove::removed);
  EXPECT_EQ(remove(plugin, k_topic, "FULL"), Remove::absent);
  EXPECT_EQ(scalar(plugin, k_bare, "DIAL\\Category", 0, "3"), fo4::ScalarMutationStatus::updated);

  const auto reloaded = reload(plugin);
  EXPECT_TRUE(authoring_clean(reloaded, k_topic));
  EXPECT_TRUE(authoring_clean(reloaded, k_bare));
  const SavedEnvironment saved(reloaded);
  const auto bare = fo4::env::decode_dialogue_topic(saved.env(), saved.record(k_bare));
  ASSERT_TRUE(bare.data.has_value());
  EXPECT_EQ(bare.data->category, 3);
  EXPECT_EQ(bare.priority, 50.0f);
  EXPECT_FALSE(fo4::env::decode_dialogue_topic(saved.env(), saved.record(k_topic)).keyword.has_value());
}

TEST(DialogueSubrecords, LocalizedTopicCannotAddAnInlineName) {
  auto plugin = read(build(true));
  EXPECT_EQ(add(plugin, k_bare, "FULL"), Add::no_default);
  EXPECT_EQ(add(plugin, k_bare, "DATA"), Add::added);
  EXPECT_TRUE(fo4::schema::preflight_record(*fo4::find_record(plugin, key(k_topic)), true).empty());
  EXPECT_EQ(remove(plugin, k_topic, "DATA"), Remove::removed);
  EXPECT_EQ(add(plugin, k_topic, "DATA"), Add::added);
  EXPECT_EQ(payloads(plugin, k_topic, "FULL").at(0), words({7}));
}

TEST(DialogueSubrecords, InfoTopLevelSinglesAreAuthorableOnlyWithoutResponseGroups) {
  auto plugin = read(build());
  for (const char* name : {"IOVR", "NAM0", "RNAM", "INCC", "PNAM"}) {
    EXPECT_EQ(add(plugin, k_group, name), Add::added) << name;
  }
  EXPECT_EQ(order(plugin, k_group), (Order{"ENAM", "PNAM", "IOVR", "RNAM", "NAM0", "INCC", "INAM"}));
  EXPECT_EQ(payloads(plugin, k_group, "PNAM").at(0), words({0}));
  EXPECT_EQ(payloads(plugin, k_group, "INCC").at(0), words({0}));
  for (const char* unproven : {"VMAD", "TPIC", "DNAM", "GNAM", "ANAM", "TSCE", "INTV", "ALFA", "ONAM", "GREE", "TIQS",
                               "MODQ"}) {
    EXPECT_EQ(add(plugin, k_group, unproven), Add::no_default) << unproven;
  }
  EXPECT_EQ(add(plugin, k_group, "CTDA"), Add::not_authorable);
  EXPECT_EQ(add(plugin, k_group, "TRDA"), Add::not_authorable);
  EXPECT_EQ(add(plugin, k_info, "IOVR"), Add::unknown_order);
  EXPECT_EQ(remove(plugin, k_group, "INAM"), Remove::removed);
  EXPECT_EQ(remove(plugin, k_group, "ENAM"), Remove::removed);
  EXPECT_EQ(add(plugin, k_group, "ENAM"), Add::added);
  EXPECT_EQ(remove(plugin, k_info, "ONAM"), Remove::removed);
  EXPECT_EQ(remove(plugin, k_info, "ALFA"), Remove::removed);
  for (const char* grouped : {"TRDA", "NAM1", "NAM2", "TNAM", "NAM9"}) {
    EXPECT_EQ(remove(plugin, k_info, grouped), Remove::not_authorable) << grouped;
  }
  EXPECT_EQ(remove(plugin, k_info, "CTDA"), Remove::not_authorable);

  const auto reloaded = reload(plugin);
  EXPECT_TRUE(authoring_clean(reloaded, k_info));
  EXPECT_TRUE(authoring_clean(reloaded, k_group));
  const SavedEnvironment saved(reloaded);
  const auto info = fo4::env::decode_dialogue_info(saved.env(), saved.record(k_info));
  EXPECT_EQ(info.responses.size(), 2u);
  EXPECT_EQ(info.conditions.size(), 1u);
  const auto group = fo4::env::decode_dialogue_info(saved.env(), saved.record(k_group));
  EXPECT_EQ(group.prompt.text, "");
  ASSERT_TRUE(group.data.has_value());
  EXPECT_EQ((*group.data)[0], 0);
}

TEST(DialogueSubrecords, SceneOnlyRemovesTopLevelSignaturesThatNoGroupReuses) {
  auto plugin = read(build());
  EXPECT_EQ(add(plugin, k_template, "NNAM"), Add::unknown_order);
  EXPECT_EQ(add(plugin, k_scene, "CNAM"), Add::no_default);
  EXPECT_EQ(remove(plugin, k_template, "VNAM"), Remove::removed);
  EXPECT_EQ(remove(plugin, k_scene, "XNAM"), Remove::removed);
  EXPECT_EQ(add(plugin, k_scene, "XNAM"), Add::unknown_order);
  EXPECT_EQ(remove(plugin, k_scene, "NNAM"), Remove::removed);
  EXPECT_EQ(remove(plugin, k_scene, "CNAM"), Remove::absent);
  for (const char* coupled : {"KSIZ", "KWDA"}) {
    EXPECT_EQ(remove(plugin, k_scene, coupled), Remove::not_authorable) << coupled;
  }
  for (const char* shared : {"FNAM", "PNAM", "INAM", "CRIS", "SCQS", "TNAM", "CTDA", "HNAM", "ALID", "ANAM"}) {
    EXPECT_EQ(remove(plugin, k_scene, shared), Remove::not_authorable) << shared;
  }
  const auto reloaded = reload(plugin);
  EXPECT_TRUE(authoring_clean(reloaded, k_scene));
  EXPECT_TRUE(authoring_clean(reloaded, k_template));
  const SavedEnvironment saved(reloaded);
  const auto scene = fo4::env::decode_scene(saved.env(), saved.record(k_scene));
  EXPECT_EQ(scene.actions.size(), 3u);
  EXPECT_EQ(scene.parent_quest, key(k_quest));
}

TEST(DialogueSubrecords, CreateIsRefusedBecauseDialogueRecordsLiveUnderQuestsAndTopics) {
  EXPECT_EQ(fo4::schema::record_authoring_constraint(sig("DIAL"))->placement, fo4::schema::RecordPlacement::quest_children);
  EXPECT_EQ(fo4::schema::record_authoring_constraint(sig("SCEN"))->placement, fo4::schema::RecordPlacement::quest_children);
  EXPECT_EQ(fo4::schema::record_authoring_constraint(sig("INFO"))->placement, fo4::schema::RecordPlacement::topic_children);
  auto plugin = read(build());
  const auto before = fo4::write_plugin(plugin);
  for (const char* signature : {"DIAL", "INFO", "SCEN"}) {
    EXPECT_EQ(fo4::create_record(plugin, sig(signature), "New").status, fo4::RecordCreateStatus::not_authorable) << signature;
    const auto rules = fo4::schema::authoring_subrecords(*fo4::schema::record_authoring_constraint(sig(signature)));
    EXPECT_EQ(fo4::create_record(plugin, sig(signature), "New", rules).status,
              fo4::RecordCreateStatus::not_authorable) << signature;
  }
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(DialogueSubrecords, PlacementOutsideQuestAndTopicChildrenIsReported) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  for (const char* signature : {"DIAL", "INFO", "SCEN"}) {
    const auto group = fo4::test::begin_group(out, signature, 0);
    const std::uint32_t id = signature[0] == 'D' ? 0x800u : signature[0] == 'I' ? 0x801u : 0x802u;
    record(out, signature, id, {{"EDID", zstr("Loose")}, {"PNAM", f32s({50.0f})}});
    fo4::test::end_group(out, group);
  }
  const auto loose = read(out.bytes);
  for (const std::uint32_t id : {0x800u, 0x801u, 0x802u}) {
    EXPECT_TRUE(has_issue(loose, id, "record_placement_invalid")) << id;
  }
  const auto placed = read(build());
  for (const auto id : {k_topic, k_info, k_scene}) {
    EXPECT_FALSE(has_issue(placed, id, "record_placement_invalid")) << id;
  }
}
