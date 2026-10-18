#include "dialogue_writer_fixture.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace fo4::test::dialogue;
using Group = fo4::SubrecordGroupMutationStatus;
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

TEST(SceneActorEdit, RemovingAnActorErasesExactlyItsTriplet) {
  auto plugin = read(build());
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
  EXPECT_FALSE(scene.actions[1].index.has_value());
  EXPECT_FALSE(scene.actions[1].flags.has_value());
  EXPECT_TRUE(scene.actions[1].payload.empty());
  EXPECT_EQ(scene.actions[2].type, 2);
  ASSERT_TRUE(scene.actions[2].actor_alias.has_value());
  EXPECT_EQ(*scene.actions[2].actor_alias, 0xFFFFFFFF);
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

  const auto after = snapshot(reload(plugin), k_scene);
  ASSERT_EQ(after.size(), before.size() - (end - open + 1));
  for (std::size_t at = 0; at < open; ++at) {
    EXPECT_EQ(after[at], before[at]) << at;
  }
  for (std::size_t at = open; at < after.size(); ++at) {
    EXPECT_EQ(after[at], before[at + end - open + 1]) << at;
  }
  const auto reloaded = reload(plugin);
  EXPECT_EQ(part_count(reloaded, fo4::ScenePart::action), 2u);
  const SavedEnvironment saved(reloaded);
  const auto scene = fo4::env::decode_scene(saved.env(), saved.record(k_scene));
  ASSERT_EQ(scene.actions.size(), 2u);
  EXPECT_EQ(scene.actions[0].type, 2);
  EXPECT_EQ(scene.actions[1].type, 1);
  EXPECT_EQ(scene.actors.size(), 2u);
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
    std::erase_if(record.subrecords, [&](const fo4::Subrecord& sub) {
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
