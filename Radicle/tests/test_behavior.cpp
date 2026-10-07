#include "fo4/environment/behavior.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>

namespace {

using Sub = std::pair<std::string, std::vector<std::byte>>;
struct Rec { std::string signature; std::uint32_t id; std::vector<Sub> subs; };
std::vector<std::byte> words(std::initializer_list<std::uint32_t> values) {
  fo4::test::ByteWriter b; for (const auto value : values) b.u32(value); return b.bytes;
}
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

}
