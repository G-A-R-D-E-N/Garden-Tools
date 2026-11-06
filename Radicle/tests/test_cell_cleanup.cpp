#include "fo4/environment/environment.hpp"
#include "fo4/forms/cell_edit.hpp"
#include "fo4/plugin/writer.hpp"
#include "env_node_fixture.hpp"
#include "temp_data_dir.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace fo4::test::envnode;
using fo4::FormKey;
using fo4::forms::apply_cell_cleanup;
using fo4::forms::CleanupApplyStatus;
using fo4::forms::CleanupMode;
using fo4::forms::CleanupPlan;
using fo4::forms::CleanupPlanStatus;
using fo4::forms::CleanupReason;
using fo4::forms::DeleteStatus;
using fo4::forms::plan_cell_cleanup;

constexpr std::uint32_t k_cell = 0x810;

using Planned = std::map<FormKey, std::pair<FormKey, CleanupReason>>;

Node statics() {
  return top("STAT", {rec("STAT", 0x900, {{"EDID", zstring("Crate")}, {"MODL", zstring("Props\\Crate.nif")}}),
                      rec("STAT", 0x901, {{"EDID", zstring("CrateAlt")}, {"MODL", zstring("Props\\Crate.nif")}}),
                      rec("STAT", 0x902, {{"EDID", zstring("Barrel")}, {"MODL", zstring("Props\\Barrel.nif")}})});
}

Node placed(std::uint32_t id, std::uint32_t base, float x, float y, float z, std::uint32_t flags = 0,
            std::vector<Sub> extra = {}) {
  extra.push_back({"NAME", bytes_of({base})});
  extra.push_back({"DATA", floats({x, y, z, 0.0f, 0.0f, 0.0f})});
  return rec("REFR", id, std::move(extra), flags);
}

std::vector<Node> cell_nodes(std::uint32_t id, std::vector<Node> temporary, std::vector<Node> persistent = {}) {
  std::vector<Node> groups;
  if (!persistent.empty()) groups.push_back(grp(id, 8, std::move(persistent)));
  groups.push_back(grp(id, 9, std::move(temporary)));
  return {rec("CELL", id, {{"DATA", {std::byte{0x01}, std::byte{0}}}}), grp(id, 6, std::move(groups))};
}

Node cell_block(std::vector<Node> cells) {
  return top("CELL", {grp(0, 2, {grp(0, 3, std::move(cells))})});
}

std::vector<std::byte> base_plugin(std::vector<Node> temporary, std::vector<Node> persistent = {}) {
  return make_plugin({}, {statics(), cell_block(cell_nodes(k_cell, std::move(temporary), std::move(persistent)))});
}

fo4::env::Environment load(fo4::test::TempDataDir& dir, const std::vector<std::string>& names) {
  std::vector<std::filesystem::path> paths;
  for (const auto& name : names) paths.push_back(dir.path() / name);
  return fo4::env::Environment::load(paths);
}

std::set<FormKey> survivors(const fo4::env::Environment& env, const FormKey& cell) {
  std::set<FormKey> keys;
  for (const auto& child : env.cell_children(cell)) keys.insert(child.ref->record->form_key);
  return keys;
}

void expect_plan(const CleanupPlan& plan, const Planned& expected) {
  EXPECT_EQ(plan.status, CleanupPlanStatus::planned);
  EXPECT_EQ(plan.entries.size(), expected.size());
  Planned actual;
  for (const auto& entry : plan.entries) actual[entry.reference] = {entry.kept, entry.reason};
  EXPECT_EQ(actual, expected);
}

}  // namespace

TEST(CellCleanup, DedupeKeepsSpecialAndRemovesPlainDuplicates) {
  fo4::test::TempDataDir dir;
  dir.write("Base.esm",
            base_plugin({placed(0x811, 0x900, 100, 200, 0),
                         placed(0x812, 0x900, 100, 200, 0, 0, {{"EDID", zstring("Gate")}}),
                         placed(0x813, 0x900, 100.4f, 199.6f, 0),
                         placed(0x814, 0x900, 300, 0, 0, 0, {{"EDID", zstring("Lever")}}),
                         placed(0x815, 0x900, 300, 0, 0, 0, {{"EDID", zstring("Lever2")}}),
                         placed(0x816, 0x900, 500, 0, 0),
                         placed(0x817, 0x900, 500, 0, 0)}),
            0);
  const auto env = load(dir, {"Base.esm"});
  const auto plan = plan_cell_cleanup(env, FormKey::parse("000810:Base.esm"), CleanupMode::dedupe, 0, false);
  expect_plan(plan, {
      {FormKey::parse("000811:Base.esm"), {FormKey::parse("000812:Base.esm"), CleanupReason::duplicate_of_special}},
      {FormKey::parse("000813:Base.esm"), {FormKey::parse("000812:Base.esm"), CleanupReason::duplicate_of_special}},
      {FormKey::parse("000817:Base.esm"), {FormKey::parse("000816:Base.esm"), CleanupReason::duplicate}},
  });
}

TEST(CellCleanup, DedupeByModelTreatsDifferentBasesWithSameModelAsDuplicates) {
  fo4::test::TempDataDir dir;
  dir.write("Base.esm",
            base_plugin({placed(0x821, 0x900, 100, 200, 0), placed(0x822, 0x901, 100, 200, 0),
                         placed(0x823, 0x902, 100, 200, 0)}),
            0);
  const auto env = load(dir, {"Base.esm"});
  const auto cell = FormKey::parse("000810:Base.esm");
  expect_plan(plan_cell_cleanup(env, cell, CleanupMode::dedupe, 0, false), {});
  expect_plan(plan_cell_cleanup(env, cell, CleanupMode::dedupe, 0, true),
              {{FormKey::parse("000822:Base.esm"), {FormKey::parse("000821:Base.esm"), CleanupReason::duplicate}}});
}

TEST(CellCleanup, ExcessCapsNonPersistentReferences) {
  fo4::test::TempDataDir dir;
  dir.write("Base.esm",
            base_plugin({placed(0x831, 0x900, 20, 0, 0), placed(0x832, 0x900, 40, 0, 0),
                         placed(0x833, 0x900, 60, 0, 0), placed(0x834, 0x900, 80, 0, 0)},
                        {placed(0x821, 0x900, 0, 0, 0, 0x400), placed(0x822, 0x900, 10, 0, 0, 0x400)}),
            0);
  const auto env = load(dir, {"Base.esm"});
  const auto cell = FormKey::parse("000810:Base.esm");
  expect_plan(plan_cell_cleanup(env, cell, CleanupMode::excess, 2, false),
              {{FormKey::parse("000833:Base.esm"), {FormKey{}, CleanupReason::excess}},
               {FormKey::parse("000834:Base.esm"), {FormKey{}, CleanupReason::excess}}});
  expect_plan(plan_cell_cleanup(env, cell, CleanupMode::excess, 4, false), {});
}

TEST(CellCleanup, DryRunPlanMatchesApply) {
  fo4::test::TempDataDir dir;
  dir.write("Base.esm",
            base_plugin({placed(0x811, 0x900, 100, 200, 0),
                         placed(0x812, 0x900, 100, 200, 0, 0, {{"EDID", zstring("Gate")}})}),
            0);
  dir.write("Patch.esp",
            make_plugin({"Base.esm"},
                        {cell_block(cell_nodes(k_cell, {placed(0x01000813, 0x900, 500, 0, 0),
                                                       placed(0x01000814, 0x900, 500, 0, 0)}))}),
            1);
  const auto env = load(dir, {"Base.esm", "Patch.esp"});
  const auto cell = FormKey::parse("000810:Base.esm");
  const auto plan = plan_cell_cleanup(env, cell, CleanupMode::dedupe, 0, false);
  expect_plan(plan, {
      {FormKey::parse("000811:Base.esm"), {FormKey::parse("000812:Base.esm"), CleanupReason::duplicate_of_special}},
      {FormKey::parse("000814:Patch.esp"), {FormKey::parse("000813:Patch.esp"), CleanupReason::duplicate}},
  });

  auto patch = env.plugin(*env.find_plugin("Patch.esp"));
  const auto applied = apply_cell_cleanup(patch, env, plan);
  EXPECT_EQ(applied.status, CleanupApplyStatus::applied);
  EXPECT_EQ(applied.entry_status,
            (std::vector<DeleteStatus>{DeleteStatus::marked_deleted, DeleteStatus::deleted}));

  dir.write("Readback.esp", fo4::write_plugin(patch), 2);
  const auto after_env = load(dir, {"Base.esm", "Readback.esp"});
  const auto before = survivors(env, cell);
  const auto after = survivors(after_env, cell);
  EXPECT_EQ(before.size(), 4u);
  EXPECT_EQ(after, (std::set<FormKey>{FormKey::parse("000812:Base.esm"), FormKey::parse("000813:Readback.esp")}));
  EXPECT_EQ(before.size() - after.size(), plan.entries.size());
  expect_plan(plan_cell_cleanup(after_env, cell, CleanupMode::dedupe, 0, false), {});
}
