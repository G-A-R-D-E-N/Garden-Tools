#include "fo4/c_api_dialogue.h"
#include "fo4/environment/dialogue.hpp"
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
  fo4::test::ByteWriter out; for (const auto value : values) out.u32(value); return out.bytes;
}
std::vector<std::byte> text(const char* value) { fo4::test::ByteWriter out; out.raw(value); out.u8(0); return out.bytes; }
std::vector<std::byte> half(std::uint16_t value) { fo4::test::ByteWriter out; out.u16(value); return out.bytes; }
std::vector<std::byte> response(std::uint8_t number) {
  fo4::test::ByteWriter out; out.u32(0x810); out.u8(number); out.u32(0x811); out.u8(0xAB); out.u16(90);
  out.u32(0xFFFFFFFF); out.u32(42); return out.bytes;
}
std::vector<std::byte> condition(std::uint16_t function) {
  fo4::CtdaCondition c; c.function = function; c.flags_byte = 0xE1; c.comparison_raw = 0x80000000;
  c.param1 = -7; c.param2 = 99; return fo4::serialize_ctda_condition(c);
}

class DialogueRead : public testing::Test {
protected:
  void SetUp() override {
    root = std::filesystem::temp_directory_path() /
        ("radicle_dialogue_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root); path = root / "Dialogue.esm";
  }
  void TearDown() override { std::error_code error; std::filesystem::remove_all(root, error); }
  void write(const std::vector<Rec>& records, std::uint32_t flags = 1) {
    fo4::test::ByteWriter out; const auto h = fo4::test::begin_record(out, "TES4", flags, 0);
    fo4::test::ByteWriter hedr; hedr.f32(1); hedr.u32(static_cast<std::uint32_t>(records.size())); hedr.u32(0x900);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes); fo4::test::end_record(out, h);
    for (const auto& rec : records) {
      const auto group = fo4::test::begin_group(out, rec.signature, 0);
      const auto at = fo4::test::begin_record(out, rec.signature, 0, rec.id, 131);
      for (const auto& [tag, bytes] : rec.subs) fo4::test::write_subrecord(out, tag, bytes);
      fo4::test::end_record(out, at); fo4::test::end_group(out, group);
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(out.bytes.data()), static_cast<std::streamsize>(out.bytes.size()));
  }
  fo4::env::Environment load() { const std::filesystem::path paths[] = {path}; return fo4::env::Environment::load(paths); }
  const fo4::env::RecordRef& record(const fo4::env::Environment& env, std::uint32_t id = 0x800) {
    return *env.winner(fo4::FormKey{fo4::PluginKey::from_filename("Dialogue.esm"), fo4::LocalFormId{id}});
  }
  std::filesystem::path root, path;
};

TEST_F(DialogueRead, TopicFieldsKeepUnknownFlagAndSubtypeValuesAndOriginalBytes) {
  fo4::test::ByteWriter priority; priority.f32(7.5f);
  write({{"DIAL", 0x800, {{"DATA", words({0xFEFF80A5})}, {"PNAM", priority.bytes}, {"TIFC", words({3})},
    {"SNAM", words({0x00414243})}, {"QNAM", words({0x810})}, {"BNAM", words({0})}, {"ZZZZ", words({55})}}}});
  const auto env = load(); const auto topic = fo4::env::decode_dialogue_topic(env, record(env));
  ASSERT_TRUE(topic.data); EXPECT_EQ(topic.data->flags, 0xA5); EXPECT_EQ(topic.data->category, 0x80);
  EXPECT_EQ(topic.data->subtype, 0xFEFF); EXPECT_EQ(topic.priority, 7.5f); EXPECT_EQ(topic.info_count, 3u);
  ASSERT_TRUE(topic.subtype_name); EXPECT_EQ((*topic.subtype_name)[3], 0);
  ASSERT_TRUE(topic.quest); EXPECT_EQ(topic.quest->local().value(), 0x810u); EXPECT_TRUE(topic.branch->owner().empty());
  EXPECT_EQ(fo4::write_plugin(env.plugin(0)), fo4::test::original_bytes(env.plugin(0)));
}

TEST_F(DialogueRead, ResponsesRemainScopedAndConditionsAndLocalizationStayExplicit) {
  const std::vector<Sub> subs{{"ENAM", words({0xFFFF8001})}, {"TPIC", words({0x812})},
    {"TRDA", response(1)}, {"NAM1", text("first")}, {"SNAM", words({0x813})},
    {"TRDA", response(2)}, {"NAM1", text("second")}, {"LNAM", words({0x814})},
    {"CTDA", condition(14)}, {"CIS1", text("operand")}, {"RNAM", text("prompt")}};
  write({{"INFO", 0x800, subs}}); const auto env = load(); const auto info = fo4::env::decode_dialogue_info(env, record(env));
  ASSERT_EQ(info.responses.size(), 2); EXPECT_EQ(info.responses[0].text.text, "first"); EXPECT_EQ(info.responses[1].text.text, "second");
  EXPECT_EQ(info.responses[0].speaker_idle->local().value(), 0x813u); EXPECT_FALSE(info.responses[0].listener_idle);
  EXPECT_FALSE(info.responses[1].speaker_idle); EXPECT_EQ(info.responses[1].listener_idle->local().value(), 0x814u);
  EXPECT_EQ(info.responses[0].camera_target_alias, -1); EXPECT_EQ(info.responses[0].camera_location_alias, 42);
  EXPECT_EQ(info.responses[0].unknown, 0xAB); EXPECT_EQ(info.responses[0].interrupt_percent, 90);
  EXPECT_EQ((*info.data)[0], 0x8001); EXPECT_EQ((*info.data)[1], 0xFFFF); EXPECT_EQ(info.prompt.text, "prompt");
  ASSERT_EQ(info.conditions.size(), 1); EXPECT_EQ(info.conditions[0].flags_byte, 0xE1); EXPECT_EQ(info.conditions[0].param1, -7);
  EXPECT_EQ(info.conditions[0].comparison_raw, 0x80000000u); EXPECT_EQ(info.conditions[0].parameter1_string, "operand");
  write({{"INFO", 0x800, {{"TRDA", response(1)}, {"NAM1", words({123})}, {"RNAM", words({456})}}}}, 0x81);
  const auto localized = load(); const auto ids = fo4::env::decode_dialogue_info(localized, record(localized));
  EXPECT_EQ(ids.responses[0].text.kind, fo4::env::NameKind::string_id); EXPECT_EQ(ids.responses[0].text.string_id, 123u);
  EXPECT_EQ(ids.prompt.string_id, 456u); EXPECT_EQ(fo4::write_plugin(localized.plugin(0)), fo4::test::original_bytes(localized.plugin(0)));
}

TEST_F(DialogueRead, SceneScopesSeparatePhaseActorActionAndTailIncludingOpaqueTimerFields) {
  write({{"SCEN", 0x800, {{"FNAM", words({0x80000001})}, {"HNAM", {}}, {"NAM0", text("phase")},
    {"CTDA", condition(14)}, {"NEXT", {}}, {"CTDA", condition(15)}, {"NEXT", {}},
    {"FNAM", half(0x8001)}, {"SCQS", words({0x000AFFFF})}, {"HNAM", {}},
    {"ALID", words({0xFFFFFFFF})}, {"LNAM", words({2})}, {"DNAM", words({4})},
    {"ANAM", half(2)}, {"NAM0", text("action")}, {"ALID", words({8})}, {"INAM", words({9})},
    {"FNAM", words({16})}, {"SNAM", half(1)}, {"ENAM", half(65535)},
    {"SNAM", words({0x41200000})}, {"PNAM", words({0x813})}, {"ZZZZ", words({7})}, {"ANAM", {}},
    {"PNAM", words({0x812})}, {"CTDA", condition(16)}}}});
  const auto env = load(); const auto scene = fo4::env::decode_scene(env, record(env));
  EXPECT_EQ(scene.flags, 0x80000001u); ASSERT_EQ(scene.phases.size(), 1); EXPECT_EQ(scene.phases[0].flags, 0x8001);
  EXPECT_EQ((*scene.phases[0].quest_stages)[0], -1); EXPECT_EQ(scene.phases[0].start_conditions[0].function, 14);
  EXPECT_EQ(scene.phases[0].completion_conditions[0].function, 15); ASSERT_EQ(scene.actors.size(), 1); EXPECT_EQ(scene.actors[0].alias, -1);
  ASSERT_EQ(scene.actions.size(), 1); EXPECT_EQ(scene.actions[0].actor_alias, 8); EXPECT_EQ(scene.actions[0].start_phase, 1u);
  EXPECT_EQ(scene.actions[0].end_phase, 65535u);
  ASSERT_EQ(scene.actions[0].payload.size(), 3); EXPECT_EQ(scene.actions[0].payload[0].signature.to_string(), "SNAM");
  EXPECT_EQ(scene.parent_quest->local().value(), 0x812u); ASSERT_EQ(scene.conditions.size(), 1); EXPECT_EQ(scene.conditions[0].function, 16);
  EXPECT_EQ(fo4::write_plugin(env.plugin(0)), fo4::test::original_bytes(env.plugin(0)));
}

TEST_F(DialogueRead, MalformedKnownFieldsAndUnpairedGroupsFailClosed) {
  write({{"DIAL", 0x800, {{"DATA", half(1)}}}, {"INFO", 0x801, {{"TRDA", response(1)}}},
    {"SCEN", 0x802, {{"HNAM", {}}, {"NEXT", {}}, {"HNAM", {}}}},
    {"SCEN", 0x803, {{"ANAM", half(2)}, {"ENAM", words({1})}}},
    {"SCEN", 0x804, {{"ALID", words({1})}, {"LNAM", words({2})}}}});
  const auto env = load();
  EXPECT_THROW(fo4::env::decode_dialogue_topic(env, record(env, 0x800)), fo4::env::WorldDecodeError);
  EXPECT_THROW(fo4::env::decode_dialogue_info(env, record(env, 0x801)), fo4::env::WorldDecodeError);
  for (const auto id : {0x802u, 0x803u, 0x804u}) {
    EXPECT_THROW(fo4::env::decode_scene(env, record(env, id)), fo4::env::WorldDecodeError);
  }
}

}
