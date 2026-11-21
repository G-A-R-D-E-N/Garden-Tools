#include "dialogue_writer_fixture.hpp"

#include <gtest/gtest.h>

#include "fo4/schema/condition_scopes.hpp"

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

TEST(ScenePhaseEdit, RemovingAnUnreferencedPhaseErasesExactlyItsOwnSubrecords) {
  auto plugin = read(build());
  EXPECT_EQ(fo4::set_scene_scalar(plugin, key(k_scene), "SCEN\\Action\\StartPhase", 1, "0"), Scalar::updated);
  EXPECT_EQ(fo4::set_scene_scalar(plugin, key(k_scene), "SCEN\\Action\\EndPhase", 0, "0"), Scalar::updated);
  EXPECT_EQ(fo4::set_scene_scalar(plugin, key(k_scene), "SCEN\\Action\\EndPhase", 2, "0"), Scalar::updated);
  const auto before = snapshot(plugin, k_scene);
  ASSERT_GT(before.size(), 4u);

  std::vector<std::size_t> markers;
  for (std::size_t at = 0; at < before.size(); ++at) {
    if (before[at].first == "HNAM") markers.push_back(at);
  }
  ASSERT_EQ(markers.size(), 4u);
  const auto open = markers[2];
  const auto first_close = markers[3];
  const auto removed = first_close - open + 1;
  EXPECT_EQ(before[open].second.size(), 0u);
  EXPECT_EQ(before[open + 1].first, "NAM0");
  EXPECT_EQ(before[open + 1].second, zstr(""));
  EXPECT_EQ(before[first_close].second.size(), 0u);

  EXPECT_EQ(fo4::remove_scene_phase(plugin, key(k_scene), 1), Group::updated);

  const auto after = snapshot(reload(plugin), k_scene);
  ASSERT_EQ(after.size(), before.size() - removed);
  for (std::size_t at = 0; at < open; ++at) {
    EXPECT_EQ(after[at], before[at]) << at;
  }
  for (std::size_t at = open; at < after.size(); ++at) {
    EXPECT_EQ(after[at], before[at + removed]) << at;
  }
  const auto reloaded = reload(plugin);
  EXPECT_EQ(phase_count(reloaded), 1u);
  EXPECT_EQ(count_of(order(reloaded, k_scene), "NEXT"), 2u);
  EXPECT_EQ(fo4::scene_group_count(reloaded, key(k_scene), fo4::ScenePart::actor), 2u);
  EXPECT_EQ(fo4::scene_group_count(reloaded, key(k_scene), fo4::ScenePart::action), 3u);
}

TEST(ScenePhaseEdit, RemovingAReferencedPhaseRefusesAndChangesNoByte) {
  auto plugin = read(build());
  const auto before = fo4::write_plugin(plugin);
  EXPECT_THROW(static_cast<void>(fo4::remove_scene_phase(plugin, key(k_scene), 0)),
               fo4::schema::ScalarError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(ScenePhaseEdit, InsertingAPhaseShiftsEveryActionPhaseReference) {
  auto plugin = read(build());
  EXPECT_EQ(fo4::insert_scene_phase(plugin, key(k_scene), 1, "Middle"), Group::updated);
  const auto reloaded = reload(plugin);
  const SavedEnvironment saved(reloaded);
  const auto scene = fo4::env::decode_scene(saved.env(), saved.record(k_scene));
  ASSERT_EQ(scene.phases.size(), 3u);
  ASSERT_EQ(scene.actions.size(), 3u);
  EXPECT_EQ(scene.actions[0].start_phase, 0u);
  EXPECT_EQ(scene.actions[0].end_phase, 2u);
  EXPECT_EQ(scene.actions[1].start_phase, 2u);
  EXPECT_FALSE(scene.actions[1].end_phase.has_value());
  EXPECT_EQ(scene.actions[2].start_phase, 0u);
  EXPECT_EQ(scene.actions[2].end_phase, 2u);
}

TEST(ScenePhaseEdit, MovingAPhaseRemapsEveryActionPhaseReference) {
  auto plugin = read(build());
  EXPECT_EQ(fo4::move_scene_phase(plugin, key(k_scene), 0, 1), Group::updated);
  const auto reloaded = reload(plugin);
  const SavedEnvironment saved(reloaded);
  const auto scene = fo4::env::decode_scene(saved.env(), saved.record(k_scene));
  ASSERT_EQ(scene.phases.size(), 2u);
  ASSERT_TRUE(scene.phases[0].name.has_value());
  EXPECT_EQ(*scene.phases[0].name, "");
  ASSERT_TRUE(scene.phases[1].name.has_value());
  EXPECT_EQ(*scene.phases[1].name, "Start");
  ASSERT_EQ(scene.actions.size(), 3u);
  EXPECT_EQ(scene.actions[0].start_phase, 1u);
  EXPECT_EQ(scene.actions[0].end_phase, 0u);
  EXPECT_EQ(scene.actions[1].start_phase, 0u);
  EXPECT_EQ(scene.actions[2].start_phase, 1u);
  EXPECT_EQ(scene.actions[2].end_phase, 0u);
  EXPECT_EQ(fo4::move_scene_phase(plugin, key(k_scene), 0, 1), Group::updated);
  EXPECT_EQ(fo4::move_scene_phase(plugin, key(k_scene), 1, 1),
            fo4::SubrecordGroupMutationStatus::unchanged);
  EXPECT_EQ(fo4::move_scene_phase(plugin, key(k_scene), 2, 0),
            fo4::SubrecordGroupMutationStatus::index_out_of_range);
}

TEST(ScenePhaseEdit, APhaseEditRefusesAnActionReferenceTheSceneCannotFollow) {
  auto plugin = read(build());
  fo4::visit_records(plugin, [&](fo4::Record& record) {
    if (record.form_key != key(k_scene)) return;
    for (auto& sub : record.subrecords) {
      if (sub.signature != sig("ENAM")) continue;
      sub.data = words({7});
    }
    record.original_range.clear();
    record.decompressed_payload.clear();
    plugin.original_range.clear();
  });
  const auto before = fo4::write_plugin(plugin);
  EXPECT_THROW(static_cast<void>(fo4::insert_scene_phase(plugin, key(k_scene), 1, "Middle")),
               fo4::schema::ScalarError);
  EXPECT_THROW(static_cast<void>(fo4::move_scene_phase(plugin, key(k_scene), 0, 1)),
               fo4::schema::ScalarError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
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

// Detects: phase condition edits must preserve other phases and the scene root through save and reload.
TEST(ScenePhaseEdit, ScopedConditionsStayInTheirOwnWindows) {
  auto plugin = read(build());
  fo4::visit_records(plugin, [&](fo4::Record& record) {
    if (record.form_key != key(k_scene)) return;
    auto parent = record.subrecords.size();
    for (std::size_t at = record.subrecords.size(); at-- > 0;) {
      if (record.subrecords[at].signature != sig("PNAM")) continue;
      parent = at;
      break;
    }
    ASSERT_NE(parent, record.subrecords.size());
    record.subrecords.insert(record.subrecords.begin() + static_cast<std::ptrdiff_t>(parent),
                             fo4::Subrecord{sig("ZZZZ"), words({0x1234}), false});
    record.original_range.clear(); record.decompressed_payload.clear(); plugin.original_range.clear();
  });
  const auto unaffected = [&](const fo4::Plugin& source) {
    const auto all = snapshot(source, k_scene);
    std::vector<Sub> out;
    int phase = -1, section = 0;
    bool inside = false;
    for (const auto& sub : all) {
      if (sub.first == "HNAM") { inside = !inside; if (inside) { ++phase; section = 0; } }
      else if (inside && sub.first == "NEXT") ++section;
      const bool edited_window = inside && ((phase == 0 && section <= 1) || (phase == 1 && section == 1));
      if (edited_window && (sub.first == "CTDA" || sub.first == "CIS1" || sub.first == "CIS2")) continue;
      out.push_back(sub);
    }
    return out;
  };
  const auto baseline = unaffected(plugin);
  ASSERT_TRUE(std::any_of(baseline.begin(), baseline.end(), [](const auto& sub) {
    return sub.first == "ZZZZ" && sub.second == words({0x1234});
  }));
  const auto start = fo4::schema::scoped_condition_path(sig("SCEN"), "Phases[0].Start Conditions");
  const auto completion = fo4::schema::scoped_condition_path(sig("SCEN"), "Phases[1].Completion Conditions");
  ASSERT_TRUE(start.has_value());
  ASSERT_TRUE(completion.has_value());
  fo4::CtdaCondition first; first.function = 7;
  fo4::CtdaCondition second; second.function = 8;
  fo4::CtdaCondition replacement; replacement.function = 9;
  const std::vector<fo4::CtdaCondition> first_list{first}, second_list{second}, replacement_list{replacement};
  using Status = fo4::schema::ScopedConditionStatus;
  EXPECT_EQ(fo4::schema::set_record_scoped_conditions(plugin, key(k_scene), start->descriptor, 0, first_list), Status::updated);
  EXPECT_EQ(unaffected(plugin), baseline);
  EXPECT_EQ(fo4::schema::set_record_scoped_conditions(plugin, key(k_scene), start->descriptor, 0, replacement_list), Status::updated);
  EXPECT_EQ(unaffected(plugin), baseline);
  EXPECT_EQ(fo4::schema::set_record_scoped_conditions(plugin, key(k_scene), completion->descriptor, 0, second_list), Status::updated);
  EXPECT_EQ(unaffected(plugin), baseline);
  EXPECT_EQ(fo4::schema::set_record_scoped_conditions(plugin, key(k_scene), completion->descriptor, 0, {}), Status::updated);
  EXPECT_EQ(unaffected(plugin), baseline);
  EXPECT_EQ(fo4::schema::set_record_scoped_conditions(plugin, key(k_scene), completion->descriptor, 1, second_list), Status::updated);
  EXPECT_EQ(unaffected(plugin), baseline);
  const auto reread = reload(plugin);
  EXPECT_EQ(unaffected(reread), baseline);
  const auto* record = fo4::find_record(reread, key(k_scene));
  ASSERT_NE(record, nullptr);
  const auto start_first = fo4::schema::read_scoped_conditions(*record, start->descriptor, 0);
  const auto start_second = fo4::schema::read_scoped_conditions(*record, start->descriptor, 1);
  const auto end_first = fo4::schema::read_scoped_conditions(*record, completion->descriptor, 0);
  const auto end_second = fo4::schema::read_scoped_conditions(*record, completion->descriptor, 1);
  ASSERT_TRUE(start_first.ok); ASSERT_EQ(start_first.conditions.size(), 1u); EXPECT_EQ(start_first.conditions[0].function, 9u);
  ASSERT_TRUE(start_second.ok); ASSERT_EQ(start_second.conditions.size(), 1u); EXPECT_EQ(start_second.conditions[0].function, 14u);
  ASSERT_TRUE(end_first.ok); EXPECT_TRUE(end_first.conditions.empty());
  ASSERT_TRUE(end_second.ok); ASSERT_EQ(end_second.conditions.size(), 1u); EXPECT_EQ(end_second.conditions[0].function, 8u);
  const auto root = fo4::schema::read_root_conditions(*record);
  ASSERT_TRUE(root.ok); ASSERT_EQ(root.conditions.size(), 1u); EXPECT_EQ(root.conditions[0].function, 14u);
  EXPECT_EQ(fo4::schema::set_record_scoped_conditions(plugin, key(k_scene), start->descriptor, 2, first_list), Status::container_index_out_of_range);
}

// Detects: malformed markers or unknown bytes in a phase window must not be moved by condition editing.
TEST(ScenePhaseEdit, ScopedConditionsRefuseMalformedAndUnknownWindows) {
  for (const int kind : {0, 1, 2}) {
    auto plugin = read(build());
    fo4::visit_records(plugin, [&](fo4::Record& record) {
      if (record.form_key != key(k_scene)) return;
      auto first = std::find_if(record.subrecords.begin(), record.subrecords.end(),
                                [](const auto& sub) { return sub.signature == sig("NEXT"); });
      ASSERT_NE(first, record.subrecords.end());
      if (kind == 1) record.subrecords.erase(first);
      else if (kind == 2) record.subrecords.insert(first, fo4::Subrecord{sig("CIS1"), zstr("orphan"), false});
      else record.subrecords.insert(first, fo4::Subrecord{sig("ZZZZ"), words({9}), false});
      record.original_range.clear(); record.decompressed_payload.clear(); plugin.original_range.clear();
    });
    const auto scope = fo4::schema::scoped_condition_path(sig("SCEN"), "Phases[0].Start Conditions");
    ASSERT_TRUE(scope.has_value());
    const auto before = fo4::write_plugin(plugin);
    fo4::CtdaCondition condition; condition.function = 7;
    EXPECT_EQ(fo4::schema::set_record_scoped_conditions(plugin, key(k_scene), scope->descriptor, 0,
              std::vector<fo4::CtdaCondition>{condition}), fo4::schema::ScopedConditionStatus::alias_structure_invalid);
    EXPECT_EQ(fo4::write_plugin(plugin), before);
  }
}

// Detects: the native scoped editor must preserve nonzero CTDA padding supplied by its caller.
TEST(ScenePhaseEdit, ScopedConditionsPreserveNativePadding) {
  auto plugin = read(build());
  fo4::visit_records(plugin, [&](fo4::Record& record) {
    if (record.form_key != key(k_scene)) return;
    auto at = std::find_if(record.subrecords.begin(), record.subrecords.end(),
                           [](const auto& sub) { return sub.signature == sig("CTDA"); });
    ASSERT_NE(at, record.subrecords.end());
    at->data[1] = std::byte{0x7f};
    record.original_range.clear(); record.decompressed_payload.clear(); plugin.original_range.clear();
  });
  const auto scope = fo4::schema::scoped_condition_path(sig("SCEN"), "Phases[1].Start Conditions");
  ASSERT_TRUE(scope.has_value());
  const auto* record = fo4::find_record(plugin, key(k_scene)); ASSERT_NE(record, nullptr);
  auto read_result = fo4::schema::read_scoped_conditions(*record, scope->descriptor, 1);
  ASSERT_TRUE(read_result.ok); ASSERT_EQ(read_result.conditions.size(), 1u);
  read_result.conditions[0].function = 42;
  EXPECT_EQ(fo4::schema::set_record_scoped_conditions(plugin, key(k_scene), scope->descriptor, 1,
            read_result.conditions), fo4::schema::ScopedConditionStatus::updated);
  const auto reloaded = reload(plugin);
  const auto* saved = fo4::find_record(reloaded, key(k_scene)); ASSERT_NE(saved, nullptr);
  const auto result = fo4::schema::read_scoped_conditions(*saved, scope->descriptor, 1);
  ASSERT_TRUE(result.ok); ASSERT_EQ(result.conditions.size(), 1u);
  EXPECT_EQ(result.conditions[0].function, 42u);
  EXPECT_EQ(result.conditions[0].padding[0], std::byte{0x7f});
}

// Detects: phase-condition editing must not claim to validate required phase names or editor widths.
TEST(ScenePhaseEdit, ScopedConditionsDoNotValidateOtherRequiredPhaseMembers) {
  for (const auto* missing : {"NAM0", "WNAM"}) {
    auto plugin = read(build());
    fo4::visit_records(plugin, [&](fo4::Record& record) {
      if (record.form_key != key(k_scene)) return;
      auto at = std::find_if(record.subrecords.begin(), record.subrecords.end(),
                             [&](const auto& sub) { return sub.signature == sig(missing); });
      ASSERT_NE(at, record.subrecords.end());
      record.subrecords.erase(at);
      record.original_range.clear(); record.decompressed_payload.clear(); plugin.original_range.clear();
    });
    const auto scope = fo4::schema::scoped_condition_path(sig("SCEN"), "Phases[0].Start Conditions");
    ASSERT_TRUE(scope.has_value());
    fo4::CtdaCondition value; value.function = 7;
    EXPECT_EQ(fo4::schema::set_record_scoped_conditions(plugin, key(k_scene), scope->descriptor, 0,
              std::vector<fo4::CtdaCondition>{value}), fo4::schema::ScopedConditionStatus::updated);
  }
}
