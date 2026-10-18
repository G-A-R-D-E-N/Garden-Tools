#include "dialogue_writer_fixture.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
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

std::size_t last_index_of(const Order& names, const char* signature) {
  std::size_t found = names.size();
  for (std::size_t at = 0; at < names.size(); ++at) {
    if (names[at] == signature) found = at;
  }
  return found;
}

std::size_t first_index_of(const Order& names, const char* signature) {
  const auto found = std::find(names.begin(), names.end(), signature);
  return found == names.end() ? names.size()
                              : static_cast<std::size_t>(std::distance(names.begin(), found));
}

std::size_t phase_count(const fo4::Plugin& plugin, std::uint32_t local = k_scene) {
  return fo4::scene_group_count(plugin, key(local), fo4::ScenePart::phase).value_or(9999);
}

}

TEST(ScenePhaseEdit, InsertingAPhaseKeepsEveryOtherGroupAndReadsBack) {
  auto plugin = read(build());

  EXPECT_EQ(fo4::insert_scene_phase(plugin, key(k_scene), 1, "Middle"), Group::updated);

  const auto reloaded = reload(plugin);
  EXPECT_EQ(phase_count(reloaded), 3u);
  EXPECT_EQ(fo4::scene_group_count(reloaded, key(k_scene), fo4::ScenePart::actor), 2u);
  EXPECT_EQ(fo4::scene_group_count(reloaded, key(k_scene), fo4::ScenePart::action), 3u);

  const auto names = order(reloaded, k_scene);
  EXPECT_EQ(count_of(names, "HNAM"), 6u);
  EXPECT_EQ(count_of(names, "NEXT"), 6u);
  EXPECT_EQ(count_of(names, "WNAM"), 3u);

  const SavedEnvironment saved(reloaded);
  const auto scene = fo4::env::decode_scene(saved.env(), saved.record(k_scene));
  ASSERT_EQ(scene.phases.size(), 3u);
  ASSERT_TRUE(scene.phases[0].name.has_value());
  EXPECT_EQ(*scene.phases[0].name, "Start");
  ASSERT_TRUE(scene.phases[1].name.has_value());
  EXPECT_EQ(*scene.phases[1].name, "Middle");
  EXPECT_EQ(scene.phases[1].editor_width, 350u);
  EXPECT_FALSE(scene.phases[1].flags.has_value());
  EXPECT_FALSE(scene.phases[1].quest_stages.has_value());
  ASSERT_TRUE(scene.phases[2].name.has_value());
  EXPECT_EQ(*scene.phases[2].name, "");
  EXPECT_EQ(scene.actors.size(), 2u);
  EXPECT_EQ(scene.actions.size(), 3u);
}

TEST(ScenePhaseEdit, APhaseAppendedLastLandsBeforeTheActors) {
  auto plugin = read(build());

  EXPECT_EQ(fo4::insert_scene_phase(plugin, key(k_scene), phase_count(plugin), "End"), Group::updated);

  const auto names = order(plugin, k_scene);
  const auto actors = first_index_of(names, "ALID");
  const auto actions = first_index_of(names, "ANAM");
  ASSERT_LT(actors, names.size());
  ASSERT_LT(actions, names.size());
  EXPECT_LT(last_index_of(names, "HNAM"), actors);
  EXPECT_LT(last_index_of(names, "NEXT"), actors);
  EXPECT_EQ(count_of(names, "HNAM"), 6u);

  const auto reloaded = reload(plugin);
  EXPECT_EQ(fo4::scene_group_count(reloaded, key(k_scene), fo4::ScenePart::actor), 2u);
  EXPECT_EQ(fo4::scene_group_count(reloaded, key(k_scene), fo4::ScenePart::action), 3u);
  const SavedEnvironment saved(reloaded);
  const auto scene = fo4::env::decode_scene(saved.env(), saved.record(k_scene));
  ASSERT_EQ(scene.phases.size(), 3u);
  ASSERT_TRUE(scene.phases[2].name.has_value());
  EXPECT_EQ(*scene.phases[2].name, "End");
}

TEST(ScenePhaseEdit, RemovingAPhaseErasesExactlyItsOwnSubrecords) {
  auto plugin = read(build());
  const auto before = snapshot(plugin, k_scene);
  ASSERT_GT(before.size(), 4u);

  const auto open = std::find_if(before.begin(), before.end(), [](const Sub& sub) {
    return sub.first == "HNAM";
  });
  ASSERT_NE(open, before.end());
  const auto first_open = static_cast<std::size_t>(std::distance(before.begin(), open));
  const auto close = std::find_if(before.begin() + static_cast<std::ptrdiff_t>(first_open + 1),
                                  before.end(),
                                  [](const Sub& sub) { return sub.first == "HNAM"; });
  ASSERT_NE(close, before.end());
  const auto first_close = static_cast<std::size_t>(std::distance(before.begin(), close));
  const auto removed = first_close - first_open + 1;
  EXPECT_EQ(before[first_open].second.size(), 0u);
  EXPECT_EQ(before[first_open + 1].first, "NAM0");
  EXPECT_EQ(before[first_open + 1].second, zstr("Start"));
  EXPECT_EQ(before[first_close].second.size(), 0u);

  EXPECT_EQ(fo4::remove_scene_phase(plugin, key(k_scene), 0), Group::updated);

  const auto after = snapshot(reload(plugin), k_scene);
  ASSERT_EQ(after.size(), before.size() - removed);
  for (std::size_t at = 0; at < first_open; ++at) {
    EXPECT_EQ(after[at], before[at]) << at;
  }
  for (std::size_t at = first_open; at < after.size(); ++at) {
    EXPECT_EQ(after[at], before[at + removed]) << at;
  }
  const auto reloaded = reload(plugin);
  EXPECT_EQ(phase_count(reloaded), 1u);
  EXPECT_EQ(count_of(order(reloaded, k_scene), "NEXT"), 2u);
  EXPECT_EQ(fo4::scene_group_count(reloaded, key(k_scene), fo4::ScenePart::actor), 2u);
  EXPECT_EQ(fo4::scene_group_count(reloaded, key(k_scene), fo4::ScenePart::action), 3u);
}

TEST(ScenePhaseEdit, PhaseNamesAreSettableAndIdempotent) {
  auto plugin = read(build());

  EXPECT_EQ(fo4::set_scene_phase_name(plugin, key(k_scene), 0, "Opening"), Scalar::updated);
  EXPECT_EQ(fo4::set_scene_phase_name(plugin, key(k_scene), 0, "Opening"), Scalar::unchanged);
  EXPECT_EQ(fo4::set_scene_phase_name(plugin, key(k_scene), 2, "Ghost"), Scalar::subrecord_missing);
  EXPECT_EQ(fo4::set_scene_phase_name(plugin, key(0x8FF), 0, "Ghost"), Scalar::record_missing);

  const auto reloaded = reload(plugin);
  const SavedEnvironment saved(reloaded);
  const auto scene = fo4::env::decode_scene(saved.env(), saved.record(k_scene));
  ASSERT_EQ(scene.phases.size(), 2u);
  ASSERT_TRUE(scene.phases[0].name.has_value());
  EXPECT_EQ(*scene.phases[0].name, "Opening");
  ASSERT_TRUE(scene.phases[1].name.has_value());
  EXPECT_EQ(*scene.phases[1].name, "");
  EXPECT_EQ(scene.actions.size(), 3u);
}

TEST(ScenePhaseEdit, RefusalsChangeNoByteAndForeignRecordsThrow) {
  auto plugin = read(build());
  const auto before = fo4::write_plugin(plugin);

  EXPECT_EQ(fo4::insert_scene_phase(plugin, key(k_scene), 3, "TooFar"), Group::index_out_of_range);
  EXPECT_EQ(fo4::insert_scene_phase(plugin, key(0x8FF), 0, "Ghost"), Group::record_missing);
  EXPECT_EQ(fo4::remove_scene_phase(plugin, key(k_scene), 2), Group::index_out_of_range);
  EXPECT_EQ(fo4::remove_scene_phase(plugin, key(0x8FF), 0), Group::record_missing);
  EXPECT_EQ(fo4::write_plugin(plugin), before);

  EXPECT_THROW(static_cast<void>(fo4::insert_scene_phase(plugin, key(k_quest), 0, "Outside")),
               fo4::schema::ScalarError);
  EXPECT_THROW(static_cast<void>(fo4::remove_scene_phase(plugin, key(k_quest), 0)),
               fo4::schema::ScalarError);
  EXPECT_THROW(static_cast<void>(fo4::set_scene_phase_name(plugin, key(k_quest), 0, "Outside")),
               fo4::schema::ScalarError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}
