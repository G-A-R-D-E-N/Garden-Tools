#include "dialogue_writer_fixture.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <map>

namespace {

using namespace fo4::test::dialogue;
using Scalar = fo4::ScalarMutationStatus;
using Link = fo4::FormLinkFieldMutationStatus;

std::uint32_t u32_at(const Bytes& data, std::size_t offset) {
  std::uint32_t value = 0;
  std::memcpy(&value, data.data() + offset, sizeof(value));
  return value;
}

std::map<fo4::FormKey, Bytes> others(const fo4::Plugin& plugin, std::uint32_t edited) {
  std::map<fo4::FormKey, Bytes> out;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    if (record.form_key == key(edited) || record.header.signature == fo4::sig_tes4) { return; }
    out[record.form_key] = fo4::test::record_original_bytes(plugin, record);
  });
  return out;
}

fo4::schema::SubrecordGroupDescriptor response_group() {
  return fo4::schema::subrecord_group_descriptor(sig("INFO"), "INFO\\Response").value();
}

}

TEST(DialogueWriter, TopicFieldsAndLinksWriteInPlaceAndKeepOtherRecords) {
  const auto source = read(build());
  auto plugin = read(build());
  EXPECT_EQ(scalar(plugin, k_topic, "DIAL\\Priority", 0, "75.5"), Scalar::updated);
  EXPECT_EQ(scalar(plugin, k_topic, "DIAL\\TopicFlags", 0, "3"), Scalar::updated);
  EXPECT_EQ(scalar(plugin, k_topic, "DIAL\\Category", 0, "7"), Scalar::updated);
  EXPECT_EQ(scalar(plugin, k_topic, "DIAL\\Subtype", 0, "88"), Scalar::updated);
  EXPECT_EQ(scalar(plugin, k_topic, "DIAL\\SubtypeName", 0, "1330398536"), Scalar::updated);
  EXPECT_EQ(scalar(plugin, k_topic, "DIAL\\Priority", 0, "75.5"), Scalar::unchanged);
  EXPECT_TRUE(refused(plugin, [&] { scalar(plugin, k_topic, "DIAL\\Category", 0, "8"); }));
  EXPECT_TRUE(refused(plugin, [&] { scalar(plugin, k_topic, "DIAL\\Subtype", 0, "65536"); }));
  EXPECT_EQ(text(plugin, k_topic, "DIAL\\Name", 0, "Greeting"), Scalar::updated);
  EXPECT_EQ(link(plugin, k_topic, "DIAL\\Branch", 0, key(k_dlbr)), Link::unchanged);
  EXPECT_EQ(link(plugin, k_topic, "DIAL\\Keyword", 0, key(k_kywd)), Link::unchanged);
  EXPECT_TRUE(refused(plugin, [&] { link(plugin, k_topic, "DIAL\\Quest", 0, key(k_misc)); }));
  EXPECT_TRUE(refused(plugin, [&] { link(plugin, k_topic, "DIAL\\Quest", 0, fo4::FormKey{}); }));
  EXPECT_EQ(link(plugin, k_bare, "DIAL\\Branch", 0, key(k_dlbr)), Link::subrecord_missing);
  EXPECT_EQ(scalar(plugin, k_bare, "DIAL\\Category", 0, "1"), Scalar::subrecord_missing);
  EXPECT_EQ(count(plugin, k_topic, "DIAL\\Quest"), 1u);
  EXPECT_TRUE(refused(plugin, [&] { scalar(plugin, k_info, "DIAL\\Priority", 0, "1"); }));

  const auto reloaded = reload(plugin);
  EXPECT_EQ(others(reloaded, k_topic), others(source, k_topic));
  const SavedEnvironment saved(reloaded);
  const auto topic = fo4::env::decode_dialogue_topic(saved.env(), saved.record(k_topic));
  EXPECT_EQ(topic.priority, 75.5f);
  ASSERT_TRUE(topic.data.has_value());
  EXPECT_EQ(topic.data->flags, 3);
  EXPECT_EQ(topic.data->category, 7);
  EXPECT_EQ(topic.data->subtype, 88);
  ASSERT_TRUE(topic.subtype_name.has_value());
  EXPECT_EQ((*topic.subtype_name)[0], 'H');
  EXPECT_EQ(topic.info_count, 2u);
  EXPECT_EQ(payloads(reloaded, k_topic, "FULL").at(0), zstr("Greeting"));
}

TEST(DialogueWriter, InfoTopLevelFieldsAndLinksCheckTargetsAndNullability) {
  auto plugin = read(build());
  EXPECT_EQ(scalar(plugin, k_info, "INFO\\Flags", 0, "3"), Scalar::updated);
  EXPECT_EQ(scalar(plugin, k_info, "INFO\\ResetHours", 0, "5461"), Scalar::updated);
  EXPECT_EQ(scalar(plugin, k_info, "INFO\\ForcedAlias", 0, "-3"), Scalar::updated);
  EXPECT_EQ(scalar(plugin, k_info, "INFO\\GreetDistance", 0, "300"), Scalar::updated);
  EXPECT_EQ(scalar(plugin, k_info, "INFO\\ParentQuestStageOnEnd", 0, "20"), Scalar::updated);
  EXPECT_EQ(scalar(plugin, k_info, "INFO\\Challenge", 0, "7"), Scalar::updated);
  EXPECT_EQ(scalar(plugin, k_info, "INFO\\SubtitlePriority", 0, "3"), Scalar::updated);
  EXPECT_TRUE(refused(plugin, [&] { scalar(plugin, k_info, "INFO\\Challenge", 0, "8"); }));
  EXPECT_TRUE(refused(plugin, [&] { scalar(plugin, k_info, "INFO\\SubtitlePriority", 0, "4"); }));
  EXPECT_EQ(text(plugin, k_info, "INFO\\StartScenePhase", 0, "Second"), Scalar::updated);
  EXPECT_EQ(text(plugin, k_info, "INFO\\Prompt", 0, "Ask"), Scalar::updated);
  EXPECT_EQ(text(plugin, k_info, "INFO\\OverrideFileName", 0, "x"), Scalar::subrecord_missing);
  EXPECT_EQ(link(plugin, k_info, "INFO\\PreviousInfo", 0, key(k_group)), Link::updated);
  EXPECT_EQ(link(plugin, k_info, "INFO\\StartScene", 0, key(k_template)), Link::updated);
  EXPECT_EQ(link(plugin, k_info, "INFO\\Speaker", 0, key(k_npc)), Link::unchanged);
  EXPECT_EQ(link(plugin, k_info, "INFO\\AudioOutputOverride", 0, key(k_sopm)), Link::unchanged);
  EXPECT_EQ(link(plugin, k_info, "INFO\\ResetGlobal", 0, key(k_glob)), Link::unchanged);
  EXPECT_EQ(link(plugin, k_info, "INFO\\PreviousTopic", 0, key(k_topic)), Link::subrecord_missing);
  EXPECT_EQ(link(plugin, k_info, "INFO\\SharedInfo", 0, key(k_group)), Link::subrecord_missing);
  EXPECT_EQ(link(plugin, k_info, "INFO\\InfoGroup", 0, key(k_group)), Link::subrecord_missing);
  EXPECT_TRUE(refused(plugin, [&] { link(plugin, k_info, "INFO\\Speaker", 0, key(k_kywd)); }));
  EXPECT_TRUE(refused(plugin, [&] { link(plugin, k_info, "INFO\\Speaker", 0, fo4::FormKey{}); }));
  EXPECT_TRUE(refused(plugin, [&] { link(plugin, k_info, "INFO\\StartScene", 0, key(k_topic)); }));

  const auto reloaded = reload(plugin);
  const SavedEnvironment saved(reloaded);
  const auto info = fo4::env::decode_dialogue_info(saved.env(), saved.record(k_info));
  ASSERT_TRUE(info.data.has_value());
  EXPECT_EQ((*info.data)[0], 3);
  EXPECT_EQ((*info.data)[1], 5461);
  EXPECT_EQ(info.previous_info, key(k_group));
  EXPECT_EQ(info.start_scene, key(k_template));
  EXPECT_EQ(info.prompt.text, "Ask");
  EXPECT_EQ(payloads(reloaded, k_info, "ALFA").at(0), words({0xFFFFFFFD}));
  EXPECT_EQ(payloads(reloaded, k_info, "TIQS").at(0), halves({0xFFFF, 20}));
  EXPECT_EQ(payloads(reloaded, k_info, "NAM0").at(0), zstr("Second"));

  EXPECT_EQ(link(plugin, k_info, "INFO\\PreviousInfo", 0, fo4::FormKey{}), Link::updated);
  EXPECT_EQ(payloads(plugin, k_info, "PNAM").at(0), words({0}));
}

TEST(DialogueWriter, ResponsesAddressByResponseIndexIncludingTheUnalignedSoundFile) {
  auto plugin = read(build());
  for (const char* path : {"INFO\\ResponseNumber", "INFO\\CameraTargetAlias", "INFO\\Emotion", "INFO\\SoundFile",
                           "INFO\\ResponseText", "INFO\\ScriptNotes", "INFO\\Edits", "INFO\\AlternateLipText"}) {
    EXPECT_EQ(count(plugin, k_info, path), 2u) << path;
  }
  EXPECT_EQ(count(plugin, k_group, "INFO\\SoundFile"), 0u);
  EXPECT_EQ(scalar(plugin, k_info, "INFO\\ResponseNumber", 1, "5"), Scalar::updated);
  EXPECT_EQ(scalar(plugin, k_info, "INFO\\ResponseUnknown", 1, "9"), Scalar::updated);
  EXPECT_EQ(scalar(plugin, k_info, "INFO\\InterruptPercentage", 0, "75"), Scalar::updated);
  EXPECT_EQ(scalar(plugin, k_info, "INFO\\CameraTargetAlias", 1, "7"), Scalar::updated);
  EXPECT_EQ(scalar(plugin, k_info, "INFO\\CameraLocationAlias", 0, "-2"), Scalar::updated);
  EXPECT_EQ(scalar(plugin, k_info, "INFO\\ResponseNumber", 2, "1"), Scalar::subrecord_missing);
  EXPECT_EQ(link(plugin, k_info, "INFO\\SoundFile", 0, key(k_sndr)), Link::updated);
  EXPECT_EQ(link(plugin, k_info, "INFO\\SoundFile", 1, fo4::FormKey{}), Link::updated);
  EXPECT_EQ(link(plugin, k_info, "INFO\\SoundFile", 2, key(k_sndr)), Link::subrecord_missing);
  EXPECT_EQ(link(plugin, k_info, "INFO\\Emotion", 1, key(k_kywd)), Link::updated);
  EXPECT_TRUE(refused(plugin, [&] { link(plugin, k_info, "INFO\\SoundFile", 0, key(k_kywd)); }));
  EXPECT_TRUE(refused(plugin, [&] { link(plugin, k_info, "INFO\\Emotion", 0, fo4::FormKey{}); }));
  EXPECT_EQ(text(plugin, k_info, "INFO\\ResponseText", 1, "Changed"), Scalar::updated);
  EXPECT_EQ(text(plugin, k_info, "INFO\\ScriptNotes", 0, "n0"), Scalar::updated);
  EXPECT_EQ(text(plugin, k_info, "INFO\\AlternateLipText", 1, "lip"), Scalar::updated);

  const auto trda = payloads(plugin, k_info, "TRDA");
  ASSERT_EQ(trda.size(), 2u);
  EXPECT_EQ(u32_at(trda[0], 0), k_kywd);
  EXPECT_EQ(trda[0][4], std::byte{1});
  EXPECT_EQ(u32_at(trda[0], 5), k_sndr);
  EXPECT_EQ(trda[0][9], std::byte{0});
  EXPECT_EQ(u32_at(trda[0], 16), 0xFFFFFFFEu);
  EXPECT_EQ(u32_at(trda[1], 0), k_kywd);
  EXPECT_EQ(trda[1][4], std::byte{5});
  EXPECT_EQ(u32_at(trda[1], 5), 0u);
  EXPECT_EQ(trda[1][9], std::byte{9});
  EXPECT_EQ(u32_at(trda[1], 12), 7u);

  const SavedEnvironment saved(reload(plugin));
  const auto info = fo4::env::decode_dialogue_info(saved.env(), saved.record(k_info));
  ASSERT_EQ(info.responses.size(), 2u);
  EXPECT_EQ(info.responses[0].sound, key(k_sndr));
  EXPECT_EQ(info.responses[0].interrupt_percent, 75);
  EXPECT_EQ(info.responses[0].camera_location_alias, -2);
  EXPECT_EQ(info.responses[1].emotion, key(k_kywd));
  EXPECT_EQ(info.responses[1].number, 5);
  EXPECT_EQ(info.responses[1].text.text, "Changed");
  EXPECT_EQ(payloads(plugin, k_info, "NAM2").at(0), zstr("n0"));
  EXPECT_EQ(payloads(plugin, k_info, "NAM4").at(1), zstr("lip"));
  EXPECT_EQ(payloads(plugin, k_info, "TNAM").at(0), halves({30}));
}

TEST(DialogueWriter, LocalizedTextIsRefusedInALocalizedPluginButPlainStringsStayWritable) {
  auto plugin = read(build(true));
  EXPECT_TRUE(refused(plugin, [&] { text(plugin, k_topic, "DIAL\\Name", 0, "x"); }));
  EXPECT_TRUE(refused(plugin, [&] { text(plugin, k_info, "INFO\\Prompt", 0, "x"); }));
  EXPECT_TRUE(refused(plugin, [&] { text(plugin, k_info, "INFO\\ResponseText", 1, "x"); }));
  EXPECT_EQ(text(plugin, k_info, "INFO\\ScriptNotes", 1, "plain"), Scalar::updated);
  EXPECT_EQ(text(plugin, k_info, "INFO\\StartScenePhase", 0, "plain"), Scalar::updated);
  EXPECT_EQ(payloads(plugin, k_info, "NAM1").at(1), words({9}));
  const SavedEnvironment saved(reload(plugin));
  const auto info = fo4::env::decode_dialogue_info(saved.env(), saved.record(k_info));
  EXPECT_EQ(info.responses[1].text.kind, fo4::env::NameKind::string_id);
  EXPECT_EQ(info.responses[1].text.string_id, 9u);
}

TEST(DialogueWriter, SceneTopLevelSchemaFieldsIgnoreGroupMembers) {
  auto plugin = read(build());
  EXPECT_EQ(scalar(plugin, k_scene, "SCEN\\DeathBehavior", 0, "0"), Scalar::updated);
  EXPECT_EQ(scalar(plugin, k_scene, "SCEN\\ObserveCombatBehavior", 0, "4294967295"), Scalar::updated);
  EXPECT_EQ(scalar(plugin, k_scene, "SCEN\\CameraDistanceOverride", 0, "1"), Scalar::subrecord_missing);
  EXPECT_EQ(scalar(plugin, k_scene, "SCEN\\Index", 0, "9"), Scalar::updated);
  EXPECT_EQ(text(plugin, k_scene, "SCEN\\Notes", 0, "changed"), Scalar::updated);
  EXPECT_EQ(payloads(plugin, k_scene, "VNAM").at(0), words({0, 3, 3, 0xFFFFFFFF}));
  EXPECT_EQ(payloads(plugin, k_scene, "XNAM").at(0), words({9}));
  EXPECT_FALSE(fo4::schema::scalar_field_descriptor("SCEN\\Flags").has_value());
  EXPECT_FALSE(fo4::schema::formlink_field_descriptor("SCEN\\ParentQuest").has_value());
  EXPECT_TRUE(fo4::schema::preflight_record(*fo4::find_record(plugin, key(k_scene))).empty());
}

TEST(DialogueWriter, MalformedResponseGroupsRefuseScalarStringAndLinkEditsWithoutChangingBytes) {
  for (int malformed = 0; malformed < 4; ++malformed) {
    SCOPED_TRACE(malformed);
    auto plugin = read(build());
    fo4::visit_records(plugin, [&](fo4::Record& record) {
      if (record.form_key != key(k_info)) { return; }
      auto& subs = record.subrecords;
      const auto first = std::find_if(subs.begin(), subs.end(),
          [](const fo4::Subrecord& sub) { return sub.signature == sig("TRDA"); });
      if (malformed == 0) {
        subs.erase(first + 2);
      } else if (malformed == 1) {
        std::iter_swap(first + 2, first + 3);
      } else if (malformed == 2) {
        subs.push_back({sig("NAM1"), zstr("stray"), false});
      } else {
        first->data.pop_back();
      }
      record.original_range.clear();
      record.decompressed_payload.clear();
      plugin.original_range.clear();
    });
    const auto before = fo4::write_plugin(plugin);
    EXPECT_THROW(scalar(plugin, k_info, "INFO\\ResponseNumber", 1, "5"), fo4::schema::ScalarError);
    EXPECT_THROW(text(plugin, k_info, "INFO\\ScriptNotes", 0, "changed"), fo4::schema::ScalarError);
    EXPECT_THROW(link(plugin, k_info, "INFO\\SoundFile", 0, key(k_sndr)), fo4::schema::FormLinkFieldError);
    EXPECT_THROW(count(plugin, k_info, "INFO\\ResponseText"), fo4::schema::ScalarError);
    EXPECT_THROW(count(plugin, k_info, "INFO\\Emotion"), fo4::schema::FormLinkFieldError);
    EXPECT_EQ(fo4::write_plugin(plugin), before);
    EXPECT_EQ(scalar(plugin, k_info, "INFO\\Flags", 0, "3"), Scalar::updated);
  }
}

TEST(DialogueWriter, PreflightUsesThePluginLocalizedFlagInsteadOfGuessingFromFourBytePayloads) {
  auto plugin = read(build());
  fo4::visit_records(plugin, [](fo4::Record& record) {
    if (record.form_key != key(k_topic)) { return; }
    for (auto& sub : record.subrecords) {
      if (sub.signature == sig("FULL")) { sub.data = words({0x12345678}); }
    }
  });
  const auto* record = fo4::find_record(plugin, key(k_topic));
  EXPECT_FALSE(fo4::schema::preflight_record(*record).empty());
  EXPECT_FALSE(fo4::schema::preflight_plugin(plugin).empty());
  EXPECT_FALSE(authoring_clean(plugin, k_topic));
  EXPECT_EQ(fo4::remove_record_subrecord(plugin, key(k_topic), sig("DATA"), 0), fo4::SubrecordRemoveStatus::removed);
  EXPECT_EQ(fo4::add_record_subrecord(plugin, key(k_topic), sig("DATA")), fo4::SubrecordAddStatus::unknown_order);
  plugin.header.tes4_flags = fo4::RecordFlags{fo4::RecordFlags::localized};
  EXPECT_TRUE(fo4::schema::preflight_plugin(plugin).empty());
  EXPECT_TRUE(authoring_clean(plugin, k_topic));
  EXPECT_EQ(fo4::add_record_subrecord(plugin, key(k_topic), sig("DATA")), fo4::SubrecordAddStatus::added);
}

TEST(DialogueWriter, ResponseGroupPairsEachTradaWithItsStringsAndNamesNoCount) {
  const auto group = response_group();
  EXPECT_EQ(group.record, sig("INFO"));
  EXPECT_EQ(group.count_subrecord, fo4::Signature{});
  ASSERT_EQ(group.members.size(), 11u);
  EXPECT_EQ(group.members[0].subrecord, sig("TRDA"));
  EXPECT_EQ(group.members[0].payload_size, 20u);
  EXPECT_EQ(group.members[1].subrecord, sig("NAM1"));
  EXPECT_TRUE(group.members[1].required);
  EXPECT_EQ(group.members[1].payload_size, 0u);
  EXPECT_TRUE(group.members[1].localized);
  EXPECT_EQ(group.members[4].subrecord, sig("NAM4"));
  EXPECT_FALSE(group.members[4].localized);
  EXPECT_EQ(group.members[8].subrecord, sig("NAM9"));
  EXPECT_EQ(group.members[10].subrecord, sig("WZMD"));
  EXPECT_EQ(fo4::schema::subrecord_group_for_member(sig("INFO"), sig("NAM1"))->path, "INFO\\Response");
  EXPECT_EQ(count(read(build()), k_info, "INFO\\IdleInterruptPercentage"), 1u);
  EXPECT_EQ(count(read(build()), k_info, "INFO\\CameraPath"), 0u);
  EXPECT_EQ(count(read(build()), k_info, "INFO\\IdleSpeakerAnimation"), 0u);
}

TEST(DialogueWriter, AppendsAnEmptyResponseBeforeTheConditionsAndEditsIt) {
  auto plugin = read(build());
  EXPECT_EQ(fo4::insert_record_subrecord_group(plugin, key(k_info), response_group(), 2),
            fo4::SubrecordGroupMutationStatus::updated);
  EXPECT_EQ(order(plugin, k_info),
            (Order{"ENAM", "PNAM", "TRDA", "NAM1", "NAM2", "NAM3", "NAM4", "NAM9", "TRDA", "NAM1", "NAM2",
                   "NAM3", "NAM4", "TNAM", "TRDA", "NAM1", "NAM2", "NAM3", "NAM4", "CTDA", "RNAM", "ANAM",
                   "TSCE", "ALFA", "ONAM", "GREE", "TIQS", "NAM0", "INCC", "MODQ", "INAM"}));
  EXPECT_EQ(scalar(plugin, k_info, "INFO\\ResponseNumber", 2, "7"), Scalar::updated);

  const auto after = reload(plugin);
  const auto trada = payloads(after, k_info, "TRDA");
  ASSERT_EQ(trada.size(), 3u);
  Bytes appended(20, std::byte{0});
  appended[4] = std::byte{7};
  EXPECT_EQ(trada.at(2), appended);
  EXPECT_EQ(payloads(after, k_info, "NAM1").at(2), zstr(""));
  EXPECT_EQ(payloads(after, k_info, "NAM4").at(2), zstr(""));
  const SavedEnvironment saved(after);
  const auto info = fo4::env::decode_dialogue_info(saved.env(), saved.record(k_info));
  ASSERT_EQ(info.responses.size(), 3u);
  EXPECT_EQ(info.responses[2].number, 7);
  EXPECT_TRUE(info.responses[2].text.text.empty());
  EXPECT_EQ(info.responses[1].text.text, "Line two");
}

TEST(DialogueWriter, InsertsAResponseAtTheFrontAndRemovesItAgain) {
  auto plugin = read(build());
  EXPECT_EQ(fo4::insert_record_subrecord_group(plugin, key(k_info), response_group(), 0),
            fo4::SubrecordGroupMutationStatus::updated);
  auto after = reload(plugin);
  EXPECT_EQ(payloads(after, k_info, "NAM1"), (std::vector<Bytes>{zstr(""), zstr("Line one"), zstr("Line two")}));
  EXPECT_EQ(payloads(after, k_info, "TRDA").at(0), Bytes(20, std::byte{0}));
  EXPECT_EQ(fo4::remove_record_subrecord_group(after, key(k_info), response_group(), 0),
            fo4::SubrecordGroupMutationStatus::updated);
  const auto restored = reload(after);
  EXPECT_EQ(payloads(restored, k_info, "NAM1"), (std::vector<Bytes>{zstr("Line one"), zstr("Line two")}));
  EXPECT_EQ(payloads(restored, k_info, "TRDA").at(0), response(k_kywd, 1, 0));
  EXPECT_EQ(payloads(restored, k_info, "NAM9").size(), 1u);
}

TEST(DialogueWriter, RemovingAResponseTakesItsOptionalIdleMembersWithIt) {
  auto plugin = read(build());
  EXPECT_EQ(fo4::remove_record_subrecord_group(plugin, key(k_info), response_group(), 1),
            fo4::SubrecordGroupMutationStatus::updated);
  const auto after = reload(plugin);
  EXPECT_EQ(payloads(after, k_info, "NAM1"), (std::vector<Bytes>{zstr("Line one")}));
  EXPECT_TRUE(payloads(after, k_info, "TNAM").empty());
  EXPECT_EQ(payloads(after, k_info, "NAM9").size(), 1u);
  EXPECT_EQ(payloads(after, k_info, "CTDA").size(), 1u);
}

TEST(DialogueWriter, ResponseInsertRefusesLocalizedPluginsAndOutOfRangeIndexes) {
  auto localized = read(build(true));
  const auto localized_before = fo4::write_plugin(localized);
  EXPECT_THROW(fo4::insert_record_subrecord_group(localized, key(k_info), response_group(), 2),
               fo4::schema::SubrecordGroupError);
  EXPECT_EQ(fo4::write_plugin(localized), localized_before);

  auto plugin = read(build());
  const auto before = fo4::write_plugin(plugin);
  EXPECT_EQ(fo4::insert_record_subrecord_group(plugin, key(k_info), response_group(), 3),
            fo4::SubrecordGroupMutationStatus::index_out_of_range);
  EXPECT_EQ(fo4::remove_record_subrecord_group(plugin, key(k_info), response_group(), 2),
            fo4::SubrecordGroupMutationStatus::index_out_of_range);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}
