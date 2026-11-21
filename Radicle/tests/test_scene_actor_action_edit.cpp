#include "dialogue_writer_fixture.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace fo4::test::dialogue;
using Group = fo4::SubrecordGroupMutationStatus;
using Link = fo4::FormLinkFieldMutationStatus;
using Scalar = fo4::ScalarMutationStatus;
using Sub = std::pair<std::string, Bytes>;

std::vector<Sub> snapshot(const fo4::Plugin& plugin, std::uint32_t local) {
  std::vector<Sub> out;
  const auto* record = fo4::find_record(plugin, key(local));
  if (record == nullptr) return out;
  for (const auto& subrecord : record->subrecords) {
    out.emplace_back(subrecord.signature.to_string(), subrecord.data);
  }
  return out;
}

std::size_t count_of(const Order& names, const char* signature) {
  return static_cast<std::size_t>(std::count(names.begin(), names.end(), signature));
}

std::size_t first_index_of(const Order& names, const char* signature) {
  const auto found = std::find(names.begin(), names.end(), signature);
  return found == names.end() ? names.size()
                              : static_cast<std::size_t>(std::distance(names.begin(), found));
}

std::size_t last_index_of(const Order& names, const char* signature) {
  std::size_t found = names.size();
  for (std::size_t at = 0; at < names.size(); ++at) {
    if (names[at] == signature) found = at;
  }
  return found;
}

std::size_t nth_index_of(const Order& names, const char* signature, std::size_t nth) {
  std::size_t seen = 0;
  for (std::size_t at = 0; at < names.size(); ++at) {
    if (names[at] != signature) continue;
    if (seen == nth) return at;
    ++seen;
  }
  return names.size();
}

void detach_actions(fo4::Plugin& plugin) {
  for (std::size_t action = 0; action < 3; ++action) {
    const auto status = fo4::set_scene_scalar(plugin, key(k_scene), "SCEN\\Action\\ActorAlias", action, "-1");
    EXPECT_TRUE(status == Scalar::updated || status == Scalar::unchanged) << action;
  }
}

std::size_t part_count(const fo4::Plugin& plugin, fo4::ScenePart part) {
  return fo4::scene_group_count(plugin, key(k_scene), part).value_or(9999);
}

}

TEST(SceneActorEdit, InsertingAnActorKeepsEveryOtherGroupAndReadsBack) {
  auto plugin = read(build());

  EXPECT_EQ(fo4::insert_scene_actor(plugin, key(k_scene), 1), Group::updated);

  const auto reloaded = reload(plugin);
  EXPECT_EQ(part_count(reloaded, fo4::ScenePart::actor), 3u);
  EXPECT_EQ(part_count(reloaded, fo4::ScenePart::phase), 2u);
  EXPECT_EQ(part_count(reloaded, fo4::ScenePart::action), 3u);

  const auto names = order(reloaded, k_scene);
  EXPECT_EQ(count_of(names, "ALID"), 6u);
  EXPECT_EQ(count_of(names, "LNAM"), 3u);
  EXPECT_EQ(count_of(names, "DNAM"), 3u);

  const SavedEnvironment saved(reloaded);
  const auto scene = fo4::env::decode_scene(saved.env(), saved.record(k_scene));
  ASSERT_EQ(scene.actors.size(), 3u);
  EXPECT_EQ(scene.actors[0].alias, 0);
  EXPECT_EQ(scene.actors[1].alias, -1);
  EXPECT_EQ(scene.actors[1].flags, 0u);
  EXPECT_EQ(scene.actors[1].behavior_flags, 26u);
  EXPECT_EQ(scene.actors[2].alias, 1);
  EXPECT_EQ(scene.actions.size(), 3u);
  EXPECT_EQ(scene.phases.size(), 2u);
}

TEST(SceneActorEdit, AnActorAppendedLastLandsAfterTheActorsAndBeforeTheActions) {
  auto plugin = read(build());

  EXPECT_EQ(fo4::insert_scene_actor(plugin, key(k_scene), part_count(plugin, fo4::ScenePart::actor)),
            Group::updated);

  const auto names = order(plugin, k_scene);
  const auto first_alid = first_index_of(names, "ALID");
  ASSERT_LT(first_alid, names.size());
  EXPECT_EQ(count_of(names, "ALID"), 6u);
  EXPECT_EQ(names[first_alid + 3], "ALID");
  EXPECT_EQ(names[first_alid + 6], "ALID");
  EXPECT_EQ(names[first_alid + 9], "ANAM");

  const auto reloaded = reload(plugin);
  const SavedEnvironment saved(reloaded);
  const auto scene = fo4::env::decode_scene(saved.env(), saved.record(k_scene));
  ASSERT_EQ(scene.actors.size(), 3u);
  EXPECT_EQ(scene.actors[2].alias, -1);
  EXPECT_EQ(scene.actions.size(), 3u);
  EXPECT_EQ(scene.actions[0].type, 0);
  ASSERT_TRUE(scene.actions[0].actor_alias.has_value());
  EXPECT_EQ(*scene.actions[0].actor_alias, 0);
}

TEST(SceneActorEdit, AnActorLandsAfterThePhasesWhenNoActorExists) {
  auto plugin = read(build());
  detach_actions(plugin);
  EXPECT_EQ(fo4::remove_scene_actor(plugin, key(k_scene), 1), Group::updated);
  EXPECT_EQ(fo4::remove_scene_actor(plugin, key(k_scene), 0), Group::updated);
  EXPECT_EQ(part_count(reload(plugin), fo4::ScenePart::actor), 0u);

  EXPECT_EQ(fo4::insert_scene_actor(plugin, key(k_scene), 0), Group::updated);

  const auto reloaded = reload(plugin);
  const auto names = order(reloaded, k_scene);
  EXPECT_EQ(count_of(names, "HNAM"), 4u);
  EXPECT_LT(last_index_of(names, "HNAM"), first_index_of(names, "ALID"));
  EXPECT_LT(first_index_of(names, "ALID"), first_index_of(names, "ANAM"));
  const SavedEnvironment saved(reloaded);
  const auto scene = fo4::env::decode_scene(saved.env(), saved.record(k_scene));
  ASSERT_EQ(scene.actors.size(), 1u);
  EXPECT_EQ(scene.actors[0].alias, -1);
  EXPECT_EQ(scene.phases.size(), 2u);
  EXPECT_EQ(scene.actions.size(), 3u);
}

TEST(SceneActorEdit, RemovingAnActorThatActionsStillReferenceIsRefusedAndChangesNoByte) {
  auto plugin = read(build());
  const auto before = fo4::write_plugin(plugin);
  EXPECT_THROW(static_cast<void>(fo4::remove_scene_actor(plugin, key(k_scene), 0)), fo4::schema::ScalarError);
  EXPECT_THROW(static_cast<void>(fo4::remove_scene_actor(plugin, key(k_scene), 1)), fo4::schema::ScalarError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);

  EXPECT_EQ(fo4::set_scene_scalar(plugin, key(k_scene), "SCEN\\Action\\ActorAlias", 0, "-1"), Scalar::updated);
  EXPECT_EQ(fo4::remove_scene_actor(plugin, key(k_scene), 0), Group::updated);
  EXPECT_THROW(static_cast<void>(fo4::remove_scene_actor(plugin, key(k_scene), 0)), fo4::schema::ScalarError);
}

TEST(SceneActorEdit, RemovingAnActorErasesExactlyItsTriplet) {
  auto plugin = read(build());
  detach_actions(plugin);
  const auto before = snapshot(plugin, k_scene);
  const auto open = first_index_of(order(plugin, k_scene), "ALID");
  ASSERT_LT(open + 2, before.size());
  EXPECT_EQ(before[open].second.size(), 4u);
  EXPECT_EQ(before[open + 1].first, "LNAM");
  EXPECT_EQ(before[open + 2].first, "DNAM");

  EXPECT_EQ(fo4::remove_scene_actor(plugin, key(k_scene), 0), Group::updated);

  const auto after = snapshot(reload(plugin), k_scene);
  ASSERT_EQ(after.size(), before.size() - 3);
  for (std::size_t at = 0; at < open; ++at) {
    EXPECT_EQ(after[at], before[at]) << at;
  }
  for (std::size_t at = open; at < after.size(); ++at) {
    EXPECT_EQ(after[at], before[at + 3]) << at;
  }
  const auto reloaded = reload(plugin);
  EXPECT_EQ(part_count(reloaded, fo4::ScenePart::actor), 1u);
  const SavedEnvironment saved(reloaded);
  const auto scene = fo4::env::decode_scene(saved.env(), saved.record(k_scene));
  ASSERT_EQ(scene.actors.size(), 1u);
  EXPECT_EQ(scene.actors[0].alias, 1);
}

TEST(SceneActionEdit, InsertingAHeaderOnlyActionReadsBackWithTheStoredDefaults) {
  auto plugin = read(build());

  EXPECT_EQ(fo4::insert_scene_action(plugin, key(k_scene), 1, 2), Group::updated);

  const auto reloaded = reload(plugin);
  EXPECT_EQ(part_count(reloaded, fo4::ScenePart::action), 4u);
  EXPECT_EQ(part_count(reloaded, fo4::ScenePart::actor), 2u);
  EXPECT_EQ(part_count(reloaded, fo4::ScenePart::phase), 2u);
  EXPECT_EQ(count_of(order(reloaded, k_scene), "ANAM"), 8u);

  const SavedEnvironment saved(reloaded);
  const auto scene = fo4::env::decode_scene(saved.env(), saved.record(k_scene));
  ASSERT_EQ(scene.actions.size(), 4u);
  EXPECT_EQ(scene.actions[0].type, 0);
  EXPECT_EQ(scene.actions[1].type, 2);
  EXPECT_FALSE(scene.actions[1].name.has_value());
  ASSERT_TRUE(scene.actions[1].actor_alias.has_value());
  EXPECT_EQ(*scene.actions[1].actor_alias, -1);
  ASSERT_TRUE(scene.actions[1].index.has_value());
  EXPECT_EQ(*scene.actions[1].index, 2u);
  EXPECT_FALSE(scene.actions[1].flags.has_value());
  EXPECT_TRUE(scene.actions[1].payload.empty());
  EXPECT_EQ(scene.actions[2].type, 2);
  ASSERT_TRUE(scene.actions[2].actor_alias.has_value());
  EXPECT_EQ(*scene.actions[2].actor_alias, 0xFFFFFFFF);
  ASSERT_TRUE(scene.actions[2].index.has_value());
  EXPECT_EQ(*scene.actions[2].index, 3u);
  ASSERT_TRUE(scene.actions[3].index.has_value());
  EXPECT_EQ(*scene.actions[3].index, 4u);
  const auto inams = payloads(reloaded, k_scene, "INAM");
  ASSERT_EQ(inams.size(), 5u);
  EXPECT_EQ(inams.back(), words({4}));
}

// Detects: newly inserted actions must accept their typed payload without changing adjacent actions or the scene tail.
TEST(SceneActionEdit, NewlyInsertedTimerAndDialoguePayloadsRoundTrip) {
  auto plugin = read(build());
  const SavedEnvironment initial(plugin);
  const auto original = fo4::env::decode_scene(initial.env(), initial.record(k_scene));
  std::vector<Sub> first_payload;
  for (const auto& sub : original.actions[0].payload) first_payload.emplace_back(sub.signature.to_string(), sub.data);
  ASSERT_EQ(fo4::insert_scene_action(plugin, key(k_scene), 1, 2), Group::updated);
  const auto before = snapshot(plugin, k_scene);
  EXPECT_EQ(fo4::set_scene_scalar(plugin, key(k_scene), "SCEN\\Action\\TimerMax", 1, "8.5"), Scalar::subrecord_missing);
  EXPECT_EQ(snapshot(plugin, k_scene), before);
  EXPECT_TRUE(refused(plugin, [&] { fo4::set_scene_scalar(plugin, key(k_scene), "SCEN\\Action\\StartPhase", 1, "invalid"); }));
  ASSERT_EQ(fo4::set_scene_scalar(plugin, key(k_scene), "SCEN\\Action\\StartPhase", 1, "0"), Scalar::updated);
  ASSERT_EQ(fo4::set_scene_scalar(plugin, key(k_scene), "SCEN\\Action\\TimerMin", 1, "1.5"), Scalar::updated);
  ASSERT_EQ(fo4::set_scene_scalar(plugin, key(k_scene), "SCEN\\Action\\TimerMax", 1, "8.5"), Scalar::updated);
  ASSERT_EQ(fo4::insert_scene_action(plugin, key(k_scene), 2, 0), Group::updated);
  EXPECT_TRUE(refused(plugin, [&] { fo4::set_scene_action_formlink(plugin, key(k_scene), 2, "SCEN\\Action\\Topic", key(k_quest)); }));
  ASSERT_EQ(fo4::set_scene_action_formlink(plugin, key(k_scene), 2, "SCEN\\Action\\Topic", fo4::FormKey{}),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(payloads(reload(plugin), k_scene, "DATA").at(1), words({0}));
  ASSERT_EQ(fo4::set_scene_action_formlink(plugin, key(k_scene), 2, "SCEN\\Action\\Topic", key(k_topic)),
            fo4::FormLinkFieldMutationStatus::updated);
  const auto reloaded = reload(plugin);
  const SavedEnvironment saved(reloaded);
  const auto scene = fo4::env::decode_scene(saved.env(), saved.record(k_scene));
  ASSERT_EQ(scene.actions.size(), 5u);
  EXPECT_EQ(scene.actions[0].type, 0u);
  ASSERT_EQ(scene.actions[0].payload.size(), first_payload.size());
  for (std::size_t i = 0; i < first_payload.size(); ++i) {
    EXPECT_EQ(scene.actions[0].payload[i].signature.to_string(), first_payload[i].first);
    EXPECT_EQ(scene.actions[0].payload[i].data, first_payload[i].second);
  }
  EXPECT_EQ(scene.actions[1].type, 2u);
  ASSERT_TRUE(scene.actions[1].start_phase.has_value());
  EXPECT_EQ(*scene.actions[1].start_phase, 0u);
  ASSERT_EQ(scene.actions[1].payload.size(), 2u);
  EXPECT_EQ(scene.actions[1].payload[0].signature, sig("SNAM"));
  EXPECT_EQ(scene.actions[1].payload[0].data, f32s({8.5f}));
  EXPECT_EQ(scene.actions[1].payload[1].signature, sig("TNAM"));
  EXPECT_EQ(scene.actions[1].payload[1].data, f32s({1.5f}));
  EXPECT_EQ(scene.actions[2].type, 0u);
  ASSERT_EQ(scene.actions[2].payload.size(), 1u);
  EXPECT_EQ(scene.actions[2].payload[0].signature, sig("DATA"));
  EXPECT_EQ(scene.actions[2].payload[0].data, words({k_topic}));
  EXPECT_EQ(scene.parent_quest, key(k_quest));
}

// Detects: unknown action payloads must not be reordered or overwritten when a typed member is missing.
TEST(SceneActionEdit, NewPayloadMembersRefuseUnknownPayloads) {
  auto plugin = read(build());
  ASSERT_EQ(fo4::insert_scene_action(plugin, key(k_scene), 1, 2), Group::updated);
  fo4::visit_records(plugin, [&](fo4::Record& record) {
    if (record.form_key != key(k_scene)) return;
    std::size_t markers = 0;
    for (auto it = record.subrecords.begin(); it != record.subrecords.end(); ++it) {
      if (it->signature != sig("ANAM")) continue;
      if (++markers != 4) continue;
      record.subrecords.insert(it, fo4::Subrecord{sig("ZZZZ"), words({42}), false});
      record.original_range.clear();
      record.decompressed_payload.clear();
      plugin.original_range.clear();
      break;
    }
  });
  const auto before = snapshot(plugin, k_scene);
  EXPECT_EQ(fo4::set_scene_scalar(plugin, key(k_scene), "SCEN\\Action\\StartPhase", 1, "0"), Scalar::subrecord_missing);
  EXPECT_EQ(fo4::set_scene_scalar(plugin, key(k_scene), "SCEN\\Action\\TimerMin", 1, "1"), Scalar::subrecord_missing);
  EXPECT_EQ(snapshot(plugin, k_scene), before);
}

TEST(SceneActionEdit, RemovingAnActionErasesItsOpenCloseSpanWithPayload) {
  auto plugin = read(build());
  const auto before = snapshot(plugin, k_scene);
  const auto names = order(plugin, k_scene);
  const auto open = first_index_of(names, "ANAM");
  std::size_t end = open + 1;
  while (end < names.size() && names[end] != "ANAM") ++end;
  ASSERT_LT(end, names.size());
  EXPECT_EQ(before[end].second.size(), 0u);
  EXPECT_GT(end - open, 6u);

  EXPECT_EQ(fo4::remove_scene_action(plugin, key(k_scene), 0), Group::updated);

  const auto after_names = order(plugin, k_scene);
  ASSERT_EQ(after_names.size(), names.size() - (end - open + 1));
  for (std::size_t at = 0; at < open; ++at) {
    EXPECT_EQ(after_names[at], names[at]) << at;
  }
  for (std::size_t at = open; at < after_names.size(); ++at) {
    EXPECT_EQ(after_names[at], names[at + end - open + 1]) << at;
  }
  const auto reloaded = reload(plugin);
  EXPECT_EQ(part_count(reloaded, fo4::ScenePart::action), 2u);
  const SavedEnvironment saved(reloaded);
  const auto scene = fo4::env::decode_scene(saved.env(), saved.record(k_scene));
  ASSERT_EQ(scene.actions.size(), 2u);
  EXPECT_EQ(scene.actions[0].type, 2);
  EXPECT_EQ(scene.actions[1].type, 1);
  ASSERT_TRUE(scene.actions[0].index.has_value());
  EXPECT_EQ(*scene.actions[0].index, 1u);
  ASSERT_TRUE(scene.actions[1].index.has_value());
  EXPECT_EQ(*scene.actions[1].index, 2u);
  const auto inams = payloads(reloaded, k_scene, "INAM");
  ASSERT_EQ(inams.size(), 3u);
  EXPECT_EQ(inams.back(), words({2}));
  EXPECT_EQ(scene.actors.size(), 2u);
}

TEST(SceneActionEdit, MovingAnActionReordersTheGroupsAndRenumbersTheirIndices) {
  auto plugin = read(build());
  EXPECT_EQ(fo4::move_scene_action(plugin, key(k_scene), 2, 0), Group::updated);
  const auto reloaded = reload(plugin);
  const SavedEnvironment saved(reloaded);
  const auto scene = fo4::env::decode_scene(saved.env(), saved.record(k_scene));
  ASSERT_EQ(scene.actions.size(), 3u);
  EXPECT_EQ(scene.actions[0].type, 1);
  EXPECT_EQ(scene.actions[1].type, 0);
  EXPECT_EQ(scene.actions[2].type, 2);
  ASSERT_TRUE(scene.actions[0].index.has_value());
  ASSERT_TRUE(scene.actions[1].index.has_value());
  ASSERT_TRUE(scene.actions[2].index.has_value());
  EXPECT_EQ(*scene.actions[0].index, 1u);
  EXPECT_EQ(*scene.actions[1].index, 2u);
  EXPECT_EQ(*scene.actions[2].index, 3u);
  EXPECT_EQ(fo4::move_scene_action(plugin, key(k_scene), 1, 1),
            fo4::SubrecordGroupMutationStatus::unchanged);
  EXPECT_EQ(fo4::move_scene_action(plugin, key(k_scene), 3, 0),
            fo4::SubrecordGroupMutationStatus::index_out_of_range);
}

TEST(SceneActionEdit, ActionIndexEditsRefuseAConventionTheSceneCannotFollow) {
  auto plugin = read(build());
  fo4::visit_records(plugin, [&](fo4::Record& record) {
    if (record.form_key != key(k_scene)) return;
    bool first = true;
    for (auto& sub : record.subrecords) {
      if (sub.signature != sig("INAM")) continue;
      if (first) {
        first = false;
        continue;
      }
      sub.data = words({9});
    }
    record.original_range.clear();
    record.decompressed_payload.clear();
    plugin.original_range.clear();
  });
  const auto before = fo4::write_plugin(plugin);
  EXPECT_THROW(static_cast<void>(fo4::insert_scene_action(plugin, key(k_scene), 1, 2)),
               fo4::schema::ScalarError);
  EXPECT_THROW(static_cast<void>(fo4::remove_scene_action(plugin, key(k_scene), 0)),
               fo4::schema::ScalarError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(SceneActionEdit, AnActionAppendedLastLandsBeforeTheSceneTail) {
  auto plugin = read(build());

  EXPECT_EQ(fo4::insert_scene_action(plugin, key(k_scene), part_count(plugin, fo4::ScenePart::action), 4),
            Group::updated);

  const auto reloaded = reload(plugin);
  const auto names = order(reloaded, k_scene);
  EXPECT_EQ(count_of(names, "ANAM"), 8u);
  EXPECT_EQ(names[names.size() - 1], "XNAM");
  EXPECT_LT(last_index_of(names, "ANAM"), first_index_of(names, "VNAM"));

  const SavedEnvironment saved(reloaded);
  const auto scene = fo4::env::decode_scene(saved.env(), saved.record(k_scene));
  ASSERT_EQ(scene.actions.size(), 4u);
  EXPECT_EQ(scene.actions[3].type, 4);
  EXPECT_EQ(scene.actions[2].type, 1);
  ASSERT_TRUE(scene.actions[2].start_phase.has_value());
  EXPECT_EQ(*scene.actions[2].start_phase, 0u);
}

TEST(SceneActionEdit, ActionNamesAreSettableIdempotentAndRefusedOutsideTheAction) {
  auto plugin = read(build());

  EXPECT_EQ(fo4::set_scene_action_name(plugin, key(k_scene), 0, "Opening"), Scalar::updated);
  EXPECT_EQ(fo4::set_scene_action_name(plugin, key(k_scene), 0, "Opening"), Scalar::unchanged);
  EXPECT_EQ(fo4::set_scene_action_name(plugin, key(k_scene), 3, "Ghost"), Scalar::subrecord_missing);
  EXPECT_EQ(fo4::set_scene_action_name(plugin, key(0x8FF), 0, "Ghost"), Scalar::record_missing);
  EXPECT_THROW(static_cast<void>(fo4::set_scene_action_name(plugin, key(k_quest), 0, "Outside")),
               fo4::schema::ScalarError);

  const auto reloaded = reload(plugin);
  EXPECT_EQ(count_of(order(reloaded, k_scene), "ANAM"), 6u);
  const SavedEnvironment saved(reloaded);
  const auto scene = fo4::env::decode_scene(saved.env(), saved.record(k_scene));
  ASSERT_EQ(scene.actions.size(), 3u);
  ASSERT_TRUE(scene.actions[0].name.has_value());
  EXPECT_EQ(*scene.actions[0].name, "Opening");
  ASSERT_TRUE(scene.actions[1].name.has_value());
  EXPECT_EQ(*scene.actions[1].name, "");
  EXPECT_EQ(scene.actions[0].type, 0);
}

TEST(SceneActionEdit, SettingANameOnAnActionWithoutOneInsertsItInHeaderOrder) {
  auto plugin = read(build());
  fo4::visit_records(plugin, [&](fo4::Record& record) {
    if (record.form_key != key(k_scene)) return;
    std::size_t seen = 0;
    std::erase_if(static_cast<std::vector<fo4::Subrecord>&>(record.subrecords), [&](const fo4::Subrecord& sub) {
      return sub.signature == sig("NAM0") && seen++ == 3;
    });
  });
  EXPECT_EQ(count_of(order(plugin, k_scene), "NAM0"), 4u);

  EXPECT_EQ(fo4::set_scene_action_name(plugin, key(k_scene), 1, "Second"), Scalar::updated);

  const auto reloaded = reload(plugin);
  const auto names = order(reloaded, k_scene);
  EXPECT_EQ(count_of(names, "NAM0"), 5u);
  const auto opener = nth_index_of(names, "ANAM", 2);
  ASSERT_LT(opener + 1, names.size());
  EXPECT_EQ(names[opener + 1], "NAM0");

  const SavedEnvironment saved(reloaded);
  const auto scene = fo4::env::decode_scene(saved.env(), saved.record(k_scene));
  ASSERT_EQ(scene.actions.size(), 3u);
  ASSERT_TRUE(scene.actions[1].name.has_value());
  EXPECT_EQ(*scene.actions[1].name, "Second");
  EXPECT_EQ(scene.actions[1].type, 2);
  ASSERT_TRUE(scene.actions[1].start_phase.has_value());
  EXPECT_EQ(*scene.actions[1].start_phase, 1u);
  ASSERT_TRUE(scene.actions[1].actor_alias.has_value());
  EXPECT_EQ(*scene.actions[1].actor_alias, -1);
}

TEST(SceneActorActionEdit, RefusalsChangeNoByteAndForeignRecordsThrow) {
  auto plugin = read(build());
  const auto before = fo4::write_plugin(plugin);

  EXPECT_EQ(fo4::insert_scene_actor(plugin, key(k_scene), 3), Group::index_out_of_range);
  EXPECT_EQ(fo4::insert_scene_actor(plugin, key(0x8FF), 0), Group::record_missing);
  EXPECT_EQ(fo4::remove_scene_actor(plugin, key(k_scene), 2), Group::index_out_of_range);
  EXPECT_EQ(fo4::remove_scene_actor(plugin, key(0x8FF), 0), Group::record_missing);
  EXPECT_EQ(fo4::insert_scene_action(plugin, key(k_scene), 4, 0), Group::index_out_of_range);
  EXPECT_EQ(fo4::insert_scene_action(plugin, key(0x8FF), 0, 0), Group::record_missing);
  EXPECT_EQ(fo4::remove_scene_action(plugin, key(k_scene), 3), Group::index_out_of_range);
  EXPECT_EQ(fo4::remove_scene_action(plugin, key(0x8FF), 0), Group::record_missing);
  EXPECT_EQ(fo4::write_plugin(plugin), before);

  EXPECT_THROW(static_cast<void>(fo4::insert_scene_action(plugin, key(k_scene), 0, 7)),
               fo4::schema::ScalarError);
  EXPECT_THROW(static_cast<void>(fo4::insert_scene_actor(plugin, key(k_quest), 0)),
               fo4::schema::ScalarError);
  EXPECT_THROW(static_cast<void>(fo4::remove_scene_actor(plugin, key(k_quest), 0)),
               fo4::schema::ScalarError);
  EXPECT_THROW(static_cast<void>(fo4::insert_scene_action(plugin, key(k_quest), 0, 1)),
               fo4::schema::ScalarError);
  EXPECT_THROW(static_cast<void>(fo4::remove_scene_action(plugin, key(k_quest), 0)),
               fo4::schema::ScalarError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

// Detects: dialogue camera fields must be authored atomically and retain other action and scene bytes.
TEST(SceneWriter, DialogueCameraPairInsertEditClearRoundTrips) {
  auto plugin = read(build());
  fo4::visit_records(plugin, [&](fo4::Record& record) {
    if (record.form_key != key(k_scene)) return;
    const auto tail = std::find_if(record.subrecords.begin(), record.subrecords.end(),
                                   [](const auto& sub) { return sub.signature == sig("VNAM"); });
    ASSERT_NE(tail, record.subrecords.end());
    record.subrecords.insert(tail, fo4::Subrecord{sig("ZZZZ"), words({0x1234}), false});
    record.original_range.clear(); record.decompressed_payload.clear(); plugin.original_range.clear();
  });
  const auto unaffected = [&](const fo4::Plugin& source) {
    std::vector<Sub> out;
    const auto* record = fo4::find_record(source, key(k_scene));
    for (const auto& sub : record->subrecords)
      if (sub.signature != sig("CRIS")) out.emplace_back(sub.signature.to_string(), sub.data);
    return out;
  };
  const auto baseline = unaffected(plugin);
  const auto before_tail_camera = payloads(plugin, k_scene, "CRIS").back();
  const auto before_topic = payloads(plugin, k_scene, "DATA");
  const auto before_timer = payloads(plugin, k_scene, "TNAM");
  const auto before_camera = fo4::read_scene_action_camera(plugin, key(k_scene), 0);
  ASSERT_TRUE(before_camera.has_value()); ASSERT_TRUE(before_camera->value.has_value());
  EXPECT_FLOAT_EQ(before_camera->value->fov, 1.0f); EXPECT_FLOAT_EQ(before_camera->value->rate, 2.0f);
  EXPECT_EQ(fo4::set_scene_action_camera(plugin, key(k_scene), 0, std::nullopt), Link::updated);
  EXPECT_EQ(fo4::set_scene_action_camera(plugin, key(k_scene), 0, fo4::SceneActionCamera{55.0f, 0.75f}), Link::updated);
  EXPECT_EQ(fo4::set_scene_action_camera(plugin, key(k_scene), 0, fo4::SceneActionCamera{55.0f, 0.75f}), Link::unchanged);
  EXPECT_EQ(fo4::set_scene_action_camera(plugin, key(k_scene), 1, fo4::SceneActionCamera{1.0f, 1.0f}), Link::subrecord_missing);
  const auto saved = reload(plugin);
  const auto after = fo4::read_scene_action_camera(saved, key(k_scene), 0);
  ASSERT_TRUE(after.has_value()); ASSERT_TRUE(after->value.has_value());
  EXPECT_FLOAT_EQ(after->value->fov, 55.0f); EXPECT_FLOAT_EQ(after->value->rate, 0.75f);
  EXPECT_EQ(payloads(saved, k_scene, "DATA"), before_topic);
  EXPECT_EQ(payloads(saved, k_scene, "TNAM"), before_timer);
  EXPECT_EQ(unaffected(saved), baseline);
  EXPECT_EQ(payloads(saved, k_scene, "CRIS").back(), before_tail_camera);
  const auto tags = order(saved, k_scene);
  const auto maximum = std::find(tags.begin(), tags.end(), "DMAX");
  const auto camera = std::find(tags.begin(), tags.end(), "CRIS");
  const auto emotion = std::find(tags.begin(), tags.end(), "PNAM");
  ASSERT_NE(maximum, tags.end()); ASSERT_NE(camera, tags.end()); ASSERT_NE(emotion, tags.end());
  EXPECT_LT(maximum, camera); EXPECT_LT(camera, emotion);
  EXPECT_TRUE(refused(plugin, [&] { fo4::set_scene_action_camera(plugin, key(k_scene), 0,
      fo4::SceneActionCamera{std::numeric_limits<float>::infinity(), 1.0f}); }));
}

// Detects: malformed and duplicated dialogue camera payloads must be refused without changing the plugin.
TEST(SceneWriter, DialogueCameraRefusesMalformedAndDuplicatePayloads) {
  for (const int kind : {0, 1, 2}) {
    auto plugin = read(build());
    fo4::visit_records(plugin, [&](fo4::Record& record) {
      if (record.form_key != key(k_scene)) return;
      for (auto it = record.subrecords.begin(); it != record.subrecords.end(); ++it) {
        if (it->signature != sig("CRIS") || it->data.size() != 8) continue;
        if (kind == 1) { const auto copy = *it; record.subrecords.insert(it + 1, copy); }
        else if (kind == 2) it->data = f32s({std::numeric_limits<float>::quiet_NaN(), 1.0f});
        else it->data = halves({1});
        record.original_range.clear(); record.decompressed_payload.clear(); plugin.original_range.clear(); break;
      }
    });
    EXPECT_TRUE(refused(plugin, [&] { fo4::set_scene_action_camera(plugin, key(k_scene), 0,
        fo4::SceneActionCamera{1.0f, 1.0f}); }));
    EXPECT_TRUE(refused(plugin, [&] { fo4::read_scene_action_camera(plugin, key(k_scene), 0); }));
  }
}

// Detects: unknown dialogue payload bytes and ambiguous ordering must block camera insertion without mutation.
TEST(SceneWriter, DialogueCameraInsertionRefusesOpaqueOrAmbiguousPayload) {
  for (const int kind : {0, 1, 2}) {
    auto plugin = read(build());
    ASSERT_EQ(fo4::set_scene_action_camera(plugin, key(k_scene), 0, std::nullopt), Link::updated);
    fo4::visit_records(plugin, [&](fo4::Record& record) {
      if (record.form_key != key(k_scene)) return;
      if (kind == 2) {
        const auto data = std::find_if(record.subrecords.begin(), record.subrecords.end(),
                                       [](const auto& sub) { return sub.signature == sig("DATA"); });
        ASSERT_NE(data, record.subrecords.end()); record.subrecords.erase(data);
      }
      for (auto it = record.subrecords.begin(); it != record.subrecords.end(); ++it) {
        if (it->signature != sig("PNAM")) continue;
        record.subrecords.insert(it + 1, fo4::Subrecord{sig(kind == 0 ? "ZZZZ" : kind == 1 ? "DMAX" : "DATA"), words({9}), false});
        record.original_range.clear(); record.decompressed_payload.clear(); plugin.original_range.clear(); break;
      }
    });
    const auto before = fo4::write_plugin(plugin);
    EXPECT_EQ(fo4::set_scene_action_camera(plugin, key(k_scene), 0, fo4::SceneActionCamera{1.0f, 2.0f}), Link::subrecord_missing);
    EXPECT_EQ(fo4::write_plugin(plugin), before);
  }
}
