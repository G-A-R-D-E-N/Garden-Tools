#include "fo4/c_api_behavior.h"
#include "fo4/environment/behavior.hpp"
#include "fo4/environment/package_procedure.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>

namespace {

using Sub = std::pair<std::string, std::vector<std::byte>>;
struct Rec { std::string signature; std::uint32_t id; std::vector<Sub> subs; };
std::vector<std::byte> words(std::initializer_list<std::uint32_t> values) {
  fo4::test::ByteWriter b; for (const auto value : values) b.u32(value); return b.bytes;
}
std::vector<std::byte> one(std::uint8_t value) { return {std::byte{value}}; }
std::vector<std::byte> text(const char* value) { fo4::test::ByteWriter b; b.raw(value); b.u8(0); return b.bytes; }
std::vector<std::byte> floats(std::size_t count) {
  fo4::test::ByteWriter b; for (std::size_t i = 0; i < count; ++i) b.f32(static_cast<float>(i) + 0.5f); return b.bytes;
}
std::vector<std::byte> condition(std::uint16_t function) {
  fo4::CtdaCondition c; c.function = function; c.flags_byte = 0xE1; c.param1 = -7; return fo4::serialize_ctda_condition(c);
}

class BehaviorRead : public testing::Test {
protected:
  void SetUp() override {
    root = std::filesystem::temp_directory_path() /
        ("radicle_behavior_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root); path = root / "Behavior.esm";
  }
  void TearDown() override { std::error_code error; std::filesystem::remove_all(root, error); }
  void write(const std::vector<Rec>& records) {
    fo4::test::ByteWriter out; const auto h = fo4::test::begin_record(out, "TES4", 1, 0);
    fo4::test::ByteWriter hedr; hedr.f32(1); hedr.u32(static_cast<std::uint32_t>(records.size())); hedr.u32(0x900);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes); fo4::test::end_record(out, h);
    for (const auto& rec : records) {
      const auto group = fo4::test::begin_group(out, rec.signature, 0); const auto at = fo4::test::begin_record(out, rec.signature, 0, rec.id, 131);
      for (const auto& [tag, bytes] : rec.subs) fo4::test::write_subrecord(out, tag, bytes);
      fo4::test::end_record(out, at); fo4::test::end_group(out, group);
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(out.bytes.data()), static_cast<std::streamsize>(out.bytes.size()));
  }
  fo4::env::Environment load() { const std::filesystem::path paths[] = {path}; return fo4::env::Environment::load(paths); }
  const fo4::env::RecordRef& record(const fo4::env::Environment& env, std::uint32_t id = 0x800) {
    return *env.winner(fo4::FormKey{fo4::PluginKey::from_filename("Behavior.esm"), fo4::LocalFormId{id}});
  }
  std::filesystem::path root, path;
};

TEST_F(BehaviorRead, PackageKeepsEncodedScheduleAndScopedLinksAndOpaqueInputs) {
  fo4::test::ByteWriter data; data.u32(0x80000001); data.u8(0xFE); data.u8(7); data.u8(3); data.u8(0xAB); data.u16(0x8001); data.u16(0xDEAD);
  fo4::test::ByteWriter schedule; for (int i = 0; i < 5; ++i) schedule.u8(0xFF); schedule.u8(0); schedule.u16(0); schedule.u32(120);
  write({{"PACK", 0x800, {{"PKDT", data.bytes}, {"PSDT", schedule.bytes}, {"CNAM", words({0x810})}, {"QNAM", words({0x811})},
    {"CTDA", condition(14)}, {"CIS1", text("operand")}, {"PKCU", words({4, 0x812, 9})}, {"ANAM", text("unknown-input")},
    {"CNAM", words({0xAABBCCDD})}, {"CTDA", condition(15)}, {"POBA", {}}, {"INAM", words({0x813})},
    {"POEA", {}}, {"INAM", words({0x814})}, {"POCA", {}}, {"INAM", words({0})}}}});
  const auto env = load(); const auto p = fo4::env::decode_package(env, record(env));
  ASSERT_TRUE(p.data); EXPECT_EQ(p.data->type, 0xFE); EXPECT_EQ(p.data->interrupt_flags, 0x8001);
  ASSERT_TRUE(p.schedule); EXPECT_EQ(p.schedule->month, -1); EXPECT_EQ(p.schedule->minute, -1); EXPECT_EQ(p.schedule->duration_minutes, 120);
  EXPECT_EQ(p.data->trailing_value, 0xDEAD);
  ASSERT_TRUE(p.counter); EXPECT_EQ(p.counter->input_count, 4u); EXPECT_EQ(p.counter->template_package.local().value(), 0x812u);
  EXPECT_EQ(p.combat_style->local().value(), 0x810u); EXPECT_EQ(p.owner_quest->local().value(), 0x811u);
  EXPECT_EQ(p.begin_idle->local().value(), 0x813u); EXPECT_EQ(p.end_idle->local().value(), 0x814u); EXPECT_TRUE(p.change_idle->owner().empty());
  ASSERT_EQ(p.conditions.size(), 1); EXPECT_EQ(p.conditions[0].function, 14); EXPECT_EQ(p.conditions[0].parameter1_string, "operand");
  ASSERT_EQ(p.opaque.size(), 9); EXPECT_EQ(p.opaque[1].signature.to_string(), "CNAM"); EXPECT_EQ(p.opaque[1].data, words({0xAABBCCDD}));
  EXPECT_EQ(fo4::write_plugin(env.plugin(0)), fo4::test::original_bytes(env.plugin(0)));
}

TEST_F(BehaviorRead, CombatStyleAcceptsMeasuredOptionalTailsAndKeepsIntegerSlot) {
  auto close = floats(9); const auto max = words({0xFFFFFFFE}); const auto variance = floats(1);
  close.insert(close.end(), max.begin(), max.end()); close.insert(close.end(), variance.begin(), variance.end());
  for (const auto melee : {9u, 10u}) for (const auto range : {3u, 4u, 5u}) {
    write({{"CSTY", 0x800, {{"CSGD", floats(12)}, {"CSME", floats(melee)}, {"CSLR", floats(range)}, {"CSCR", close},
      {"CSFL", floats(8)}, {"CSRA", floats(1)}, {"CSCV", floats(1)}, {"DATA", words({0x80000001})}}}});
    const auto env = load(); const auto c = fo4::env::decode_combat_style(env, record(env));
    ASSERT_TRUE(c.general); EXPECT_EQ((*c.general)[11], 11.5f); EXPECT_EQ(c.melee.size(), melee); EXPECT_EQ(c.long_range.size(), range);
    ASSERT_TRUE(c.close_range); EXPECT_EQ(c.close_range->throw_max_targets, 0xFFFFFFFEu); EXPECT_EQ(c.close_range->flank_variance, 0.5f);
    ASSERT_TRUE(c.flight); EXPECT_EQ((*c.flight)[7], 7.5f); EXPECT_EQ(c.flags, 0x80000001u);
    EXPECT_EQ(fo4::write_plugin(env.plugin(0)), fo4::test::original_bytes(env.plugin(0)));
  }
}

TEST_F(BehaviorRead, LocationAndTargetViewsKeepVariantValuesAndOriginalPositions) {
  for (std::uint32_t type = 0; type <= 14; ++type) {
    for (const bool extended : {false, true}) {
      auto location = words({type, 0x810, 0xFFFFFFFFu});
      if (extended) { const auto index = words({0x80000001}); location.insert(location.end(), index.begin(), index.end()); }
      write({{"PACK", 0x800, {{"PKCU", words({0, 0, 9})}, {"PLDT", location}, {"PTDA", words({type, 0x810, 0xFFFFFFFFu})}}}});
      const auto env = load(); const auto p = fo4::env::decode_package(env, record(env));
      ASSERT_EQ(p.locations.size(), 1); const auto& l = p.locations[0];
      EXPECT_EQ(l.subrecord_index, 1u); EXPECT_EQ(l.type, type); EXPECT_EQ(l.value, 0x810u);
      EXPECT_EQ(l.radius, -1);
      EXPECT_EQ(l.item_index.has_value(), extended); if (extended) { EXPECT_EQ(*l.item_index, 0x80000001u); }
      EXPECT_EQ(l.form.has_value(), type == 0 || type == 1 || type == 4 || type == 6);
      ASSERT_EQ(p.targets.size(), type <= 8 ? 1u : 0u);
      if (!p.targets.empty()) {
        const auto& t = p.targets[0]; EXPECT_EQ(t.subrecord_index, 2u); EXPECT_EQ(t.count_or_distance, -1);
        EXPECT_EQ(t.form.has_value(), type == 0 || type == 1 || type == 3 || type == 7);
      }
      EXPECT_EQ(fo4::write_plugin(env.plugin(0)), fo4::test::original_bytes(env.plugin(0)));
    }
  }
  write({{"PACK", 0x800, {{"PKCU", words({0, 0, 9})}, {"PLDT", words({255, 1, 2})}, {"PTDA", words({99, 1, 2})}, {"PLDT", words({0})}}}});
  const auto env = load(); const auto p = fo4::env::decode_package(env, record(env));
  EXPECT_TRUE(p.locations.empty()); EXPECT_TRUE(p.targets.empty()); EXPECT_EQ(p.opaque.size(), 3u);
  EXPECT_EQ(fo4::write_plugin(env.plugin(0)), fo4::test::original_bytes(env.plugin(0)));
}

TEST_F(BehaviorRead, LocationRetainsEightBytePrefixAndRejectsUnknownFullWidthType) {
  write({{"PACK", 0x800, {{"PLDT", words({4, 0x810})}, {"PLDT", words({0xAABBCC06, 0x811, 0})},
    {"PLDT", words({0xFFFFFFFF, 0x812, 0})}, {"PTDA", words({0xFFFFFFFF, 0x813, 0})}}}});
  const auto env = load(); const auto p = fo4::env::decode_package(env, record(env));
  ASSERT_EQ(p.locations.size(), 1u);
  EXPECT_EQ(p.locations[0].type, 4);
  EXPECT_FALSE(p.locations[0].radius);
  ASSERT_TRUE(p.locations[0].form);
  EXPECT_EQ(p.locations[0].form->local().value(), 0x810u);
  EXPECT_TRUE(p.targets.empty());
  EXPECT_EQ(fo4::write_plugin(env.plugin(0)), fo4::test::original_bytes(env.plugin(0)));
}

TEST_F(BehaviorRead, IdleAnimationListKeepsEverySlotInOrderAndRefusesPartialWidth) {
  write({{"PACK", 0x800, {{"IDLA", words({0x810, 0, 0x811})}, {"PKCU", words({0, 0, 9})}}}});
  const auto env = load(); const auto p = fo4::env::decode_package(env, record(env));
  ASSERT_EQ(p.idle_animations.size(), 3u);
  EXPECT_EQ(p.idle_animations[0].local().value(), 0x810u); EXPECT_TRUE(p.idle_animations[1].owner().empty());
  EXPECT_EQ(p.idle_animations[2].local().value(), 0x811u);
  EXPECT_EQ(fo4::write_plugin(env.plugin(0)), fo4::test::original_bytes(env.plugin(0)));
  write({{"PACK", 0x800, {{"IDLA", std::vector<std::byte>(6)}}}}); const auto bad = load();
  EXPECT_THROW(fo4::env::decode_package(bad, record(bad)), fo4::env::WorldDecodeError);
}

TEST_F(BehaviorRead, IdlePreservesSelectionDataStringsAndRawConditions) {
  write({{"IDLE", 0x800, {{"ANAM", words({0x810, 0})}, {"DATA", {std::byte{255}, std::byte{254}, std::byte{0x81}, std::byte{9}, std::byte{0x34}, std::byte{0x12}}},
    {"DNAM", text("graph.hkx")}, {"ENAM", text("event")}, {"GNAM", text("animation.hkx")}, {"CTDA", condition(14)}}}});
  const auto env = load(); const auto idle = fo4::env::decode_idle(env, record(env)); ASSERT_TRUE(idle.data);
  EXPECT_EQ(idle.data->loop_min, 255); EXPECT_EQ(idle.data->loop_max, 254); EXPECT_EQ(idle.data->flags, 0x81); EXPECT_EQ(idle.data->replay_delay, 0x1234);
  EXPECT_EQ(idle.parent->local().value(), 0x810u); EXPECT_TRUE(idle.previous->owner().empty()); EXPECT_EQ(idle.behavior_graph, "graph.hkx");
  EXPECT_EQ(idle.animation_event, "event"); EXPECT_EQ(idle.animation_file, "animation.hkx"); ASSERT_EQ(idle.conditions.size(), 1);
  EXPECT_EQ(fo4::write_plugin(env.plugin(0)), fo4::test::original_bytes(env.plugin(0)));
}

TEST_F(BehaviorRead, AbsentFieldsRemainAbsentAndMalformedSizesFailClosed) {
  write({{"PACK", 0x800, {}}, {"CSTY", 0x801, {}}, {"IDLE", 0x802, {}}}); const auto empty = load();
  EXPECT_FALSE(fo4::env::decode_package(empty, record(empty)).data); EXPECT_FALSE(fo4::env::decode_combat_style(empty, record(empty, 0x801)).general);
  EXPECT_FALSE(fo4::env::decode_idle(empty, record(empty, 0x802)).data);
  write({{"PACK", 0x800, {{"PKDT", words({1})}}}, {"CSTY", 0x801, {{"CSLR", floats(2)}}}, {"IDLE", 0x802, {{"ANAM", words({1})}}}});
  const auto bad = load(); EXPECT_THROW(fo4::env::decode_package(bad, record(bad)), fo4::env::WorldDecodeError);
  EXPECT_THROW(fo4::env::decode_combat_style(bad, record(bad, 0x801)), fo4::env::WorldDecodeError);
  EXPECT_THROW(fo4::env::decode_idle(bad, record(bad, 0x802)), fo4::env::WorldDecodeError);
}

TEST_F(BehaviorRead, ProcedureTreeDecodesNestedContainersAndLeafPayloadRanges) {
  write({{"PACK", 0x800, {{"PKCU", words({0, 0, 9})}, {"ANAM", text("Sequence")}, {"CITC", words({0})}, {"PRCB", words({2, 5})},
    {"ANAM", text("Procedure")}, {"CITC", words({1})}, {"CTDA", condition(14)}, {"PNAM", text("Travel")}, {"FNAM", words({1})},
    {"PKC2", one(3)}, {"ANAM", text("Stacked")}, {"CITC", words({0})}, {"PRCB", words({1, 0})}, {"ANAM", text("Procedure")},
    {"CITC", words({0})}, {"PNAM", text("Sandbox")}, {"POBA", {}}, {"POEA", {}}, {"POCA", {}}}}});
  const auto env = load(); const auto tree = fo4::env::decode_package_procedure(*record(env).record);
  EXPECT_TRUE(tree.decoded); EXPECT_EQ(tree.region_begin, 1u); EXPECT_EQ(tree.region_end, 16u);
  EXPECT_TRUE(tree.inputs.empty()); EXPECT_FALSE(tree.marker); ASSERT_EQ(tree.nodes.size(), 4u);
  const auto& root_node = tree.nodes[0];
  EXPECT_EQ(root_node.kind, fo4::env::PackageProcedureKind::container); EXPECT_EQ(root_node.name, "Sequence");
  EXPECT_EQ(root_node.child_count, 2u); EXPECT_EQ(root_node.flags, 5u); EXPECT_EQ(root_node.children, (std::vector<std::size_t>{1, 2}));
  const auto& travel = tree.nodes[1];
  EXPECT_EQ(travel.kind, fo4::env::PackageProcedureKind::leaf); EXPECT_EQ(travel.name, "Travel"); EXPECT_EQ(travel.condition_count, 1u);
  EXPECT_FALSE(travel.flags); EXPECT_EQ(travel.opaque_begin, 8u); EXPECT_EQ(travel.opaque_end, 10u);
  EXPECT_EQ(tree.nodes[2].name, "Stacked"); EXPECT_EQ(tree.nodes[2].flags, 0u); EXPECT_EQ(tree.nodes[2].children, (std::vector<std::size_t>{3}));
  EXPECT_EQ(tree.nodes[3].name, "Sandbox"); EXPECT_EQ(tree.nodes[3].opaque_begin, 16u); EXPECT_EQ(tree.nodes[3].opaque_end, 16u);
  EXPECT_EQ(fo4::write_plugin(env.plugin(0)), fo4::test::original_bytes(env.plugin(0)));
}

TEST_F(BehaviorRead, ProcedureSimultaneousReadsFlagOverrideAfterItsChildren) {
  write({{"PACK", 0x800, {{"PKCU", words({0, 0, 9})}, {"ANAM", text("Simultaneous")}, {"CITC", words({0})}, {"PRCB", words({1, 0})},
    {"ANAM", text("Procedure")}, {"CITC", words({0})}, {"PNAM", text("Follow")}, {"PFO2", std::vector<std::byte>(16)}, {"POBA", {}}}}});
  const auto env = load(); const auto tree = fo4::env::decode_package_procedure(*record(env).record);
  EXPECT_TRUE(tree.decoded); EXPECT_EQ(tree.region_end, 8u); ASSERT_EQ(tree.nodes.size(), 2u);
  EXPECT_EQ(tree.nodes[0].name, "Simultaneous"); EXPECT_EQ(tree.nodes[0].children, (std::vector<std::size_t>{1}));
  EXPECT_EQ(tree.nodes[1].name, "Follow"); EXPECT_EQ(tree.nodes[1].opaque_begin, 7u); EXPECT_EQ(tree.nodes[1].opaque_end, 7u);
  EXPECT_EQ(fo4::write_plugin(env.plugin(0)), fo4::test::original_bytes(env.plugin(0)));
}

TEST_F(BehaviorRead, ProcedureLeafCountsEveryConditionAndItsOperands) {
  write({{"PACK", 0x800, {{"PKCU", words({0, 0, 9})}, {"ANAM", text("Procedure")}, {"CITC", words({2})}, {"CTDA", condition(14)},
    {"CIS1", text("operand")}, {"CTDA", condition(15)}, {"CIS2", text("second")}, {"PNAM", text("Sit")}, {"POBA", {}}}}});
  const auto env = load(); const auto tree = fo4::env::decode_package_procedure(*record(env).record);
  EXPECT_TRUE(tree.decoded); EXPECT_EQ(tree.region_end, 8u); ASSERT_EQ(tree.nodes.size(), 1u);
  EXPECT_EQ(tree.nodes[0].condition_count, 2u); EXPECT_EQ(tree.nodes[0].name, "Sit");
}

TEST_F(BehaviorRead, ProcedureTreeFallsBackToOneOpaqueRegionForUnknownNamesAndTruncatedCounters) {
  write({{"PACK", 0x800, {{"PKCU", words({0, 0, 9})}, {"ANAM", text("Sprint")}, {"CITC", words({0})}, {"POBA", {}}}},
    {"PACK", 0x801, {{"PKCU", words({0, 0, 9})}, {"ANAM", text("Procedure")}, {"CITC", words({0})}, {"PNAM", text("Dance")}, {"POBA", {}}}},
    {"PACK", 0x802, {{"PKCU", words({0, 0, 9})}, {"ANAM", text("Sequence")}, {"CITC", words({0})}, {"PRCB", words({1})}, {"POBA", {}}}}});
  const auto env = load();
  const std::pair<std::uint32_t, std::size_t> cases[] = {{0x800, 3}, {0x801, 4}, {0x802, 4}};
  for (const auto& [id, end] : cases) {
    const auto tree = fo4::env::decode_package_procedure(*record(env, id).record);
    EXPECT_FALSE(tree.decoded) << std::hex << id; EXPECT_TRUE(tree.nodes.empty()); EXPECT_TRUE(tree.inputs.empty());
    EXPECT_EQ(tree.region_begin, 1u); EXPECT_EQ(tree.region_end, end);
  }
  EXPECT_EQ(fo4::write_plugin(env.plugin(0)), fo4::test::original_bytes(env.plugin(0)));
}

TEST_F(BehaviorRead, ProcedureFlagOverrideAfterLeafInsideSequenceIsOptional) {
  write({{"PACK", 0x800, {{"PKCU", words({0, 0, 9})}, {"ANAM", text("Sequence")}, {"CITC", words({0})}, {"PRCB", words({2, 0})},
    {"ANAM", text("Procedure")}, {"CITC", words({0})}, {"PNAM", text("Travel")}, {"PFO2", std::vector<std::byte>(16)},
    {"ANAM", text("Procedure")}, {"CITC", words({0})}, {"PNAM", text("Sit")}, {"POBA", {}}}}});
  const auto env = load(); const auto tree = fo4::env::decode_package_procedure(*record(env).record);
  EXPECT_TRUE(tree.decoded); EXPECT_EQ(tree.region_end, 11u); ASSERT_EQ(tree.nodes.size(), 3u);
  EXPECT_EQ(tree.nodes[0].children, (std::vector<std::size_t>{1, 2}));
  EXPECT_EQ(tree.nodes[1].name, "Travel"); EXPECT_EQ(tree.nodes[1].opaque_begin, 7u); EXPECT_EQ(tree.nodes[1].opaque_end, 7u);
  EXPECT_EQ(tree.nodes[2].name, "Sit"); EXPECT_EQ(tree.nodes[2].opaque_begin, 11u); EXPECT_EQ(tree.nodes[2].opaque_end, 11u);
  EXPECT_EQ(fo4::write_plugin(env.plugin(0)), fo4::test::original_bytes(env.plugin(0)));
}

TEST_F(BehaviorRead, ProcedureSimultaneousWithoutFlagOverrideDecodes) {
  write({{"PACK", 0x800, {{"PKCU", words({0, 0, 9})}, {"ANAM", text("Simultaneous")}, {"CITC", words({0})}, {"PRCB", words({2, 0})},
    {"ANAM", text("Procedure")}, {"CITC", words({0})}, {"PNAM", text("UseWeapon")},
    {"ANAM", text("Procedure")}, {"CITC", words({0})}, {"PNAM", text("Travel")}, {"POBA", {}}}}});
  const auto env = load(); const auto tree = fo4::env::decode_package_procedure(*record(env).record);
  EXPECT_TRUE(tree.decoded); EXPECT_EQ(tree.region_end, 10u); ASSERT_EQ(tree.nodes.size(), 3u);
  EXPECT_EQ(tree.nodes[0].name, "Simultaneous"); EXPECT_EQ(tree.nodes[0].children, (std::vector<std::size_t>{1, 2}));
  EXPECT_EQ(fo4::write_plugin(env.plugin(0)), fo4::test::original_bytes(env.plugin(0)));
}

TEST_F(BehaviorRead, ProcedureDataInputsKeepPreTreeIndexesAndTheTrailingNameTable) {
  const auto inputs = std::vector<Sub>{{"PKCU", words({2, 0, 9})}, {"ANAM", text("Int")}, {"CNAM", words({5})}, {"ANAM", text("Bool")},
    {"CNAM", one(1)}, {"UNAM", one(0)}, {"UNAM", one(1)}};
  const auto tree = std::vector<Sub>{{"ANAM", text("Procedure")}, {"CITC", words({0})}, {"PNAM", text("Sit")}};
  const auto table = std::vector<Sub>{{"UNAM", one(0)}, {"BNAM", text("Target")}, {"PNAM", words({1})}, {"UNAM", one(1)}, {"POBA", {}}};
  auto with_marker = inputs; with_marker.push_back({"XNAM", one(7)});
  with_marker.insert(with_marker.end(), tree.begin(), tree.end()); with_marker.insert(with_marker.end(), table.begin(), table.end());
  auto without_marker = inputs; without_marker.insert(without_marker.end(), tree.begin(), tree.end());
  without_marker.insert(without_marker.end(), table.begin(), table.end());
  const std::vector<Sub> older = {{"PKCU", words({2, 0, 9})}, {"ANAM", text("Int")}, {"CNAM", words({5})}, {"ANAM", text("Bool")},
    {"CNAM", one(1)}, {"UNAM", one(0)}, {"BNAM", text("Target")}, {"PNAM", words({1})}, {"UNAM", one(1)},
    {"ANAM", text("Procedure")}, {"CITC", words({0})}, {"PNAM", text("Sit")}, {"POBA", {}}};
  const auto mismatched = std::vector<Sub>{{"PKCU", words({2, 0, 9})}, {"ANAM", text("Int")}, {"CNAM", words({5})}, {"ANAM", text("Bool")},
    {"CNAM", one(1)}, {"UNAM", one(0)}, {"POBA", {}}};
  write({{"PACK", 0x800, with_marker}, {"PACK", 0x801, without_marker}, {"PACK", 0x802, mismatched}, {"PACK", 0x803, older}});
  const auto env = load();
  const auto marked = fo4::env::decode_package_procedure(*record(env, 0x800).record);
  EXPECT_TRUE(marked.decoded); ASSERT_EQ(marked.inputs.size(), 2u);
  EXPECT_EQ(marked.inputs[0].type_name, "Int"); EXPECT_EQ(marked.inputs[0].subrecord_begin, 1u); EXPECT_EQ(marked.inputs[0].subrecord_end, 3u); EXPECT_EQ(marked.inputs[0].index, 0);
  EXPECT_EQ(marked.inputs[1].type_name, "Bool"); EXPECT_EQ(marked.inputs[1].subrecord_begin, 3u); EXPECT_EQ(marked.inputs[1].subrecord_end, 5u); EXPECT_EQ(marked.inputs[1].index, 1);
  EXPECT_EQ(marked.marker, 7u); EXPECT_EQ(marked.region_end, 15u); ASSERT_EQ(marked.nodes.size(), 1u); EXPECT_EQ(marked.nodes[0].name, "Sit");
  const auto unmarked = fo4::env::decode_package_procedure(*record(env, 0x801).record);
  EXPECT_TRUE(unmarked.decoded); EXPECT_EQ(unmarked.inputs.size(), 2u); EXPECT_FALSE(unmarked.marker); EXPECT_EQ(unmarked.region_end, 14u);
  const auto bad = fo4::env::decode_package_procedure(*record(env, 0x802).record);
  EXPECT_FALSE(bad.decoded); EXPECT_TRUE(bad.inputs.empty()); EXPECT_EQ(bad.region_end, 6u);
  const auto old = fo4::env::decode_package_procedure(*record(env, 0x803).record);
  EXPECT_TRUE(old.decoded); ASSERT_EQ(old.inputs.size(), 2u); EXPECT_EQ(old.inputs[1].index, 1); EXPECT_EQ(old.region_end, 12u);
  EXPECT_EQ(fo4::write_plugin(env.plugin(0)), fo4::test::original_bytes(env.plugin(0)));
}

TEST_F(BehaviorRead, ProcedureCAbiExposesFlatNodesInputsAndNameTables) {
  write({{"PACK", 0x800, {{"PKCU", words({1, 0, 9})}, {"ANAM", text("Int")}, {"CNAM", words({5})}, {"UNAM", one(0)},
    {"ANAM", text("Procedure")}, {"CITC", words({0})}, {"PNAM", text("Travel")}, {"POBA", {}}}}});
  const auto path_text = path.string(); const char* paths[] = {path_text.c_str()}; radicle_env* raw_env = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 1, &raw_env), RADICLE_STATUS_OK);
  const std::unique_ptr<radicle_env, decltype(&radicle_env_destroy)> env(raw_env, radicle_env_destroy);
  const radicle_record* record = nullptr;
  ASSERT_EQ(radicle_env_winner(raw_env, radicle_form_key{"Behavior.esm", 0x800}, &record), RADICLE_STATUS_OK);
  const radicle_package_procedure_tree* raw_tree = nullptr;
  ASSERT_EQ(radicle_package_procedure_decode(raw_env, record, &raw_tree), RADICLE_STATUS_OK);
  const std::unique_ptr<const radicle_package_procedure_tree, decltype(&radicle_package_procedure_free)> tree(raw_tree, radicle_package_procedure_free);
  EXPECT_EQ(tree->decoded, 1); EXPECT_EQ(tree->region_begin, 1u); EXPECT_EQ(tree->region_end, 7u); EXPECT_EQ(tree->has_marker, 0);
  ASSERT_EQ(tree->input_count, 1u); EXPECT_STREQ(tree->inputs[0].type_name, "Int"); EXPECT_EQ(tree->inputs[0].subrecord_end, 3u); EXPECT_EQ(tree->inputs[0].index, 0);
  ASSERT_EQ(tree->node_count, 1u); EXPECT_EQ(tree->nodes[0].is_leaf, 1); EXPECT_STREQ(tree->nodes[0].name, "Travel");
  EXPECT_EQ(tree->nodes[0].opaque_begin, 7u); EXPECT_EQ(tree->nodes[0].opaque_end, 7u); EXPECT_EQ(tree->nodes[0].children_count, 0u);
  EXPECT_STREQ(radicle_package_location_type_name(0), "near reference"); EXPECT_EQ(radicle_package_location_type_name(15), nullptr);
  EXPECT_STREQ(radicle_package_target_type_name(8), "alias collection item"); EXPECT_EQ(radicle_package_target_type_name(-1), nullptr);
}

TEST_F(BehaviorRead, ProcedureCAbiRefusesANonPackRecordLikePackDecode) {
  write({{"IDLE", 0x800, {{"PKCU", words({1, 0, 9})}, {"ANAM", text("Procedure")}}}});
  const auto path_text = path.string(); const char* paths[] = {path_text.c_str()}; radicle_env* raw_env = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 1, &raw_env), RADICLE_STATUS_OK);
  const std::unique_ptr<radicle_env, decltype(&radicle_env_destroy)> env(raw_env, radicle_env_destroy);
  const radicle_record* record = nullptr;
  ASSERT_EQ(radicle_env_winner(raw_env, radicle_form_key{"Behavior.esm", 0x800}, &record), RADICLE_STATUS_OK);
  const radicle_package_info* package = nullptr;
  EXPECT_EQ(radicle_package_decode(raw_env, record, &package), RADICLE_STATUS_PARSE_ERROR);
  radicle_package_procedure_tree sentinel{};
  const radicle_package_procedure_tree* tree = &sentinel;
  EXPECT_EQ(radicle_package_procedure_decode(raw_env, record, &tree), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(tree, nullptr);
}

TEST(PackageProcedure, ExpectedInputCountsMatchTheVerifiedLeaves) {
  EXPECT_EQ(fo4::env::expected_input_count("Travel"), 8u); EXPECT_EQ(fo4::env::expected_input_count("Sandbox"), 16u);
  EXPECT_EQ(fo4::env::expected_input_count("Sit"), 1u); EXPECT_EQ(fo4::env::expected_input_count("Sleep"), 1u);
  EXPECT_EQ(fo4::env::expected_input_count("Follow"), 6u); EXPECT_EQ(fo4::env::expected_input_count("Wait"), 2u);
  EXPECT_EQ(fo4::env::expected_input_count("Say"), 5u); EXPECT_FALSE(fo4::env::expected_input_count("Eat"));
}

}
