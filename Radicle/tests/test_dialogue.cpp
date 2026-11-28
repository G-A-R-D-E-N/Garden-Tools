#include "fo4/c_api_dialogue.h"
#include "fo4/c_api_vmad.h"
#include "fo4/environment/dialogue.hpp"
#include "fo4/environment/info_result.hpp"
#include "fo4/environment/scene_targets.hpp"
#include "fo4/environment/term_fragments.hpp"
#include "fo4/plugin/info_responses.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string_view>

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

template <class Call>
void expect_decode_error(Call&& call, std::string_view part) {
  try { call(); ADD_FAILURE() << "no WorldDecodeError, wanted: " << part; }
  catch (const fo4::env::WorldDecodeError& error) {
    EXPECT_NE(std::string_view(error.what()).find(part), std::string_view::npos) << error.what();
  }
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
    {"FNAM", words({16})}, {"SNAM", words({65537})}, {"ENAM", words({0xFFFFFFFF})},
    {"SNAM", words({0x41200000})}, {"PNAM", words({0x813})}, {"ZZZZ", words({7})}, {"ANAM", {}},
    {"PNAM", words({0x812})}, {"CTDA", condition(16)}}}});
  const auto env = load(); const auto scene = fo4::env::decode_scene(env, record(env));
  EXPECT_EQ(scene.flags, 0x80000001u); ASSERT_EQ(scene.phases.size(), 1); EXPECT_EQ(scene.phases[0].flags, 0x8001);
  EXPECT_EQ((*scene.phases[0].quest_stages)[0], -1); EXPECT_EQ(scene.phases[0].start_conditions[0].function, 14);
  EXPECT_EQ(scene.phases[0].completion_conditions[0].function, 15); ASSERT_EQ(scene.actors.size(), 1); EXPECT_EQ(scene.actors[0].alias, -1);
  ASSERT_EQ(scene.actions.size(), 1); EXPECT_EQ(scene.actions[0].actor_alias, 8); EXPECT_EQ(scene.actions[0].start_phase, 65537u);
  EXPECT_EQ(scene.actions[0].end_phase, 0xFFFFFFFFu);
  ASSERT_EQ(scene.actions[0].payload.size(), 3); EXPECT_EQ(scene.actions[0].payload[0].signature.to_string(), "SNAM");
  EXPECT_EQ(scene.parent_quest->local().value(), 0x812u); ASSERT_EQ(scene.conditions.size(), 1); EXPECT_EQ(scene.conditions[0].function, 16);
  EXPECT_EQ(fo4::write_plugin(env.plugin(0)), fo4::test::original_bytes(env.plugin(0)));
}

TEST_F(DialogueRead, ActionWithoutEndPhaseFollowsTheXEditHeaderOrder) {
  write({{"SCEN", 0x800, {{"ANAM", half(2)}, {"NAM0", text("")}, {"ALID", words({1})}, {"INAM", words({2})},
    {"SNAM", words({3})}, {"SNAM", words({0x40A00000})}, {"TNAM", words({0x3F800000})}, {"ANAM", {}},
    {"ANAM", half(0)}, {"ALID", words({4})}, {"INAM", words({5})}, {"DATA", words({0x810})}, {"ANAM", {}},
    {"PNAM", words({0x812})}}}});
  const auto env = load(); const auto scene = fo4::env::decode_scene(env, record(env));
  ASSERT_EQ(scene.actions.size(), 2u);
  EXPECT_EQ(scene.actions[0].start_phase, 3u); EXPECT_FALSE(scene.actions[0].end_phase);
  ASSERT_EQ(scene.actions[0].payload.size(), 2u); EXPECT_EQ(scene.actions[0].payload[0].signature.to_string(), "SNAM");
  EXPECT_FALSE(scene.actions[1].start_phase); EXPECT_FALSE(scene.actions[1].end_phase); EXPECT_FALSE(scene.actions[1].name);
  EXPECT_EQ(scene.actions[1].actor_alias, 4); ASSERT_EQ(scene.actions[1].payload.size(), 1u);
  EXPECT_EQ(scene.parent_quest->local().value(), 0x812u);
}

TEST_F(DialogueRead, MalformedKnownFieldsAndUnpairedGroupsFailClosed) {
  write({{"DIAL", 0x800, {{"DATA", half(1)}}}, {"INFO", 0x801, {{"TRDA", response(1)}}},
    {"SCEN", 0x802, {{"HNAM", {}}, {"NEXT", {}}, {"HNAM", {}}}},
    {"SCEN", 0x803, {{"ANAM", half(2)}, {"ENAM", words({1})}}},
    {"SCEN", 0x804, {{"ALID", words({1})}, {"LNAM", words({2})}}},
    {"SCEN", 0x805, {{"ANAM", half(2)}, {"SNAM", half(1)}, {"ENAM", words({2})}, {"ANAM", {}}}}});
  const auto env = load();
  EXPECT_THROW(fo4::env::decode_dialogue_topic(env, record(env, 0x800)), fo4::env::WorldDecodeError);
  EXPECT_THROW(fo4::env::decode_dialogue_info(env, record(env, 0x801)), fo4::env::WorldDecodeError);
  for (const auto id : {0x802u, 0x803u, 0x804u, 0x805u}) {
    EXPECT_THROW(fo4::env::decode_scene(env, record(env, id)), fo4::env::WorldDecodeError);
  }
}

// Detects: a response body that swallows the conditions or the prompt after it, and a header field read from the wrong offset.
TEST_F(DialogueRead, InfoResponseDecoderScopesBodiesAndExposesHeaderFields) {
  const std::vector<Sub> subs{{"ENAM", words({0xFFFF8001})},
    {"TRDA", response(1)}, {"NAM1", text("first")}, {"NAM2", text("notes")}, {"SNAM", words({0x813})},
    {"TRDA", response(2)}, {"NAM1", text("second")},
    {"CTDA", condition(14)}, {"CIS1", text("operand")}, {"RNAM", text("prompt")}};
  write({{"INFO", 0x800, subs}}); const auto env = load(); const auto& info = *record(env).record;
  const auto responses = fo4::read_info_responses(info);
  ASSERT_EQ(responses.size(), 2u);
  EXPECT_EQ(responses[0].number, 1); EXPECT_EQ(responses[0].emotion, 0x810u); EXPECT_EQ(responses[0].sound, 0x811u);
  EXPECT_EQ(responses[0].unknown, 0xAB); EXPECT_EQ(responses[0].interrupt_percent, 90);
  EXPECT_EQ(responses[0].camera_target_alias, -1); EXPECT_EQ(responses[0].camera_location_alias, 42);
  EXPECT_FALSE(responses[0].malformed); EXPECT_EQ(responses[1].number, 2);
  const auto nam = fo4::Signature::from_string("NAM1");
  const auto* first = fo4::info_response_subrecord(info, responses[0], nam);
  ASSERT_NE(first, nullptr); EXPECT_EQ(first->data, text("first"));
  const auto* second = fo4::info_response_subrecord(info, responses[1], nam);
  ASSERT_NE(second, nullptr); EXPECT_EQ(second->data, text("second"));
  // The second body must stop at CTDA, so the conditions and the prompt are not inside it.
  EXPECT_EQ(responses[1].last, 7u);
  EXPECT_EQ(fo4::info_response_subrecord(info, responses[1], fo4::Signature::from_string("RNAM")), nullptr);
  EXPECT_EQ(fo4::info_response_subrecord(info, responses[0], fo4::Signature::from_string("NAM2")),
            &info.subrecords[3]);
}

// Detects: a decoder that throws or drops the row on a short header, which a read tool must still be able to show.
TEST_F(DialogueRead, InfoResponseDecoderReportsShortHeadersWithoutThrowing) {
  write({{"INFO", 0x800, {{"TRDA", std::vector<std::byte>(19)}, {"NAM1", text("a")}, {"TRDA", words({0x810, 7})},
    {"NAM1", text("b")}}}});
  const auto env = load();
  const auto responses = fo4::read_info_responses(*record(env).record);
  ASSERT_EQ(responses.size(), 2u);
  EXPECT_TRUE(responses[0].malformed); EXPECT_EQ(responses[0].emotion, 0u); EXPECT_EQ(responses[0].sound, 0u);
  EXPECT_TRUE(responses[1].malformed); EXPECT_EQ(responses[1].number, 7);
  const auto* body = fo4::info_response_subrecord(*record(env).record, responses[1],
                                                 fo4::Signature::from_string("NAM1"));
  ASSERT_NE(body, nullptr);
  EXPECT_EQ(body->data, text("b"));
}

TEST_F(DialogueRead, InfoResponseDataOfAnotherSizeFailsClosed) {
  write({{"INFO", 0x800, {{"TRDA", std::vector<std::byte>(19)}, {"NAM1", text("a")}}},
    {"INFO", 0x801, {{"TRDA", std::vector<std::byte>(21)}, {"NAM1", text("a")}}}});
  const auto env = load();
  expect_decode_error([&] { fo4::env::decode_dialogue_info(env, record(env, 0x800)); },
                      "response data must be twenty bytes");
  expect_decode_error([&] { fo4::env::decode_dialogue_info(env, record(env, 0x801)); },
                      "response data must be twenty bytes");
}

TEST_F(DialogueRead, LocalizedInfoTextIdsOfAnotherSizeFailClosed) {
  write({{"INFO", 0x800, {{"TRDA", response(1)}, {"NAM1", std::vector<std::byte>(3)}}},
    {"INFO", 0x801, {{"RNAM", std::vector<std::byte>(5)}}}}, 0x81);
  const auto localized = load();
  expect_decode_error([&] { fo4::env::decode_dialogue_info(localized, record(localized, 0x800)); },
                      "string ID must be four bytes");
  expect_decode_error([&] { fo4::env::decode_dialogue_info(localized, record(localized, 0x801)); },
                      "string ID must be four bytes");

  write({{"INFO", 0x802, {{"TRDA", response(1)}, {"NAM1", std::vector<std::byte>{std::byte{'a'}, std::byte{'b'}}}}},
    {"INFO", 0x803, {{"RNAM", std::vector<std::byte>{std::byte{'a'}, std::byte{0}, std::byte{'b'}, std::byte{0}}}}}});
  const auto inline_env = load();
  expect_decode_error([&] { fo4::env::decode_dialogue_info(inline_env, record(inline_env, 0x802)); },
                      "invalid inline text");
  expect_decode_error([&] { fo4::env::decode_dialogue_info(inline_env, record(inline_env, 0x803)); },
                      "invalid inline text");
}

TEST_F(DialogueRead, ResponseScopeRefusesDuplicateAndWrongSizedFields) {
  write({{"INFO", 0x800, {{"TRDA", response(1)}, {"NAM1", text("a")}, {"NAM1", text("b")}}},
    {"INFO", 0x801, {{"TRDA", response(1)}, {"NAM1", text("a")}, {"SNAM", words({1, 2})}}},
    {"INFO", 0x802, {{"TRDA", response(1)}, {"NAM1", text("a")}, {"LNAM", half(1)}}}});
  const auto env = load();
  expect_decode_error([&] { fo4::env::decode_dialogue_info(env, record(env, 0x800)); }, "duplicate scoped field");
  expect_decode_error([&] { fo4::env::decode_dialogue_info(env, record(env, 0x801)); },
                      "invalid scoped field size");
  expect_decode_error([&] { fo4::env::decode_dialogue_info(env, record(env, 0x802)); },
                      "invalid scoped field size");
}

TEST_F(DialogueRead, ConditionScopeErrorsBecomeDecodeErrors) {
  write({{"INFO", 0x800, {{"CTDA", condition(14)}, {"RNAM", text("p")}, {"CTDA", condition(15)}}},
    {"INFO", 0x801, {{"CTDA", std::vector<std::byte>(3)}}},
    {"SCEN", 0x802, {{"HNAM", {}}, {"CTDA", condition(14)}, {"NAM0", text("n")}, {"CTDA", condition(15)},
      {"NEXT", {}}, {"NEXT", {}}, {"HNAM", {}}}}});
  const auto env = load();
  expect_decode_error([&] { fo4::env::decode_dialogue_info(env, record(env, 0x800)); }, "more than one list");
  expect_decode_error([&] { fo4::env::decode_dialogue_info(env, record(env, 0x801)); }, "CTDA");
  expect_decode_error([&] { fo4::env::decode_scene(env, record(env, 0x802)); }, "CTDA");
}

TEST_F(DialogueRead, SceneLayoutRefusesEachStructuralDefect) {
  write({{"SCEN", 0x800, {{"HNAM", words({1})}}},
    {"SCEN", 0x801, {{"NEXT", {}}}},
    {"SCEN", 0x802, {{"HNAM", {}}, {"ANAM", half(2)}, {"NEXT", {}}, {"NEXT", {}}, {"HNAM", {}}}},
    {"SCEN", 0x803, {{"HNAM", {}}, {"NEXT", {}}, {"NEXT", {}}, {"NEXT", {}}, {"HNAM", {}}}},
    {"SCEN", 0x804, {{"HNAM", {}}, {"NEXT", {}}, {"NEXT", {}}}},
    {"SCEN", 0x805, {{"HNAM", {}}, {"NEXT", {}}, {"NEXT", {}}, {"HNAM", words({1})}}},
    {"SCEN", 0x806, {{"HNAM", {}}, {"NEXT", words({1})}, {"NEXT", {}}, {"HNAM", {}}}},
    {"SCEN", 0x807, {{"ANAM", words({2})}, {"ANAM", {}}}},
    {"SCEN", 0x808, {{"ANAM", half(2)}, {"ANAM", half(2)}}},
    {"SCEN", 0x809, {{"ANAM", half(2)}}},
    {"SCEN", 0x80A, {{"ALID", words({1})}, {"LNAM", half(2)}, {"DNAM", words({4})}}}});
  const auto env = load();
  const std::pair<std::uint32_t, std::string_view> expected[] = {
      {0x800, "marker must be empty"},
      {0x801, "NEXT outside a phase"},
      {0x802, "phase crosses a later group"},
      {0x803, "phase requires two NEXT separators"},
      {0x804, "phase requires two NEXT separators"},
      {0x805, "marker must be empty"},
      {0x806, "marker must be empty"},
      {0x807, "action opener must be two bytes"},
      {0x808, "marker must be empty"},
      {0x809, "action requires an empty ANAM close"},
      {0x80A, "actor requires"}};
  for (const auto& [id, message] : expected) {
    expect_decode_error([&] { fo4::env::decode_scene(env, record(env, id)); }, message);
  }
}

TEST_F(DialogueRead, SceneScopedFieldsRefuseDuplicatesAndBadSizes) {
  write({{"SCEN", 0x800, {{"HNAM", {}}, {"NAM0", text("a")}, {"NAM0", text("b")}, {"NEXT", {}}, {"NEXT", {}},
      {"HNAM", {}}}},
    {"SCEN", 0x801, {{"HNAM", {}}, {"WNAM", half(1)}, {"NEXT", {}}, {"NEXT", {}}, {"HNAM", {}}}},
    {"SCEN", 0x802, {{"HNAM", {}}, {"NEXT", {}}, {"NEXT", {}}, {"SCQS", half(1)}, {"HNAM", {}}}},
    {"SCEN", 0x803, {{"ANAM", half(2)}, {"ALID", half(1)}, {"ANAM", {}}}},
    {"SCEN", 0x804, {{"FNAM", half(1)}}},
    {"SCEN", 0x805, {{"PNAM", half(1)}}},
    {"SCEN", 0x806, {{"HNAM", {}}, {"NAM0", std::vector<std::byte>{std::byte{'a'}, std::byte{'b'}}}, {"NEXT", {}},
      {"NEXT", {}}, {"HNAM", {}}}}});
  const auto env = load();
  expect_decode_error([&] { fo4::env::decode_scene(env, record(env, 0x800)); }, "duplicate scoped field");
  expect_decode_error([&] { fo4::env::decode_scene(env, record(env, 0x801)); }, "invalid scoped field size");
  expect_decode_error([&] { fo4::env::decode_scene(env, record(env, 0x802)); }, "invalid scoped field size");
  expect_decode_error([&] { fo4::env::decode_scene(env, record(env, 0x803)); }, "invalid scoped field size");
  expect_decode_error([&] { fo4::env::decode_scene(env, record(env, 0x804)); }, "invalid scoped field size");
  expect_decode_error([&] { fo4::env::decode_scene(env, record(env, 0x805)); }, "invalid scoped field size");
  expect_decode_error([&] { fo4::env::decode_scene(env, record(env, 0x806)); }, "invalid stored string");
}

TEST_F(DialogueRead, ConditionStringsReachTheAbiAsUtf8) {
  write({{"INFO", 0x800, {{"CTDA", condition(14)}, {"CIS1", text("caf\xE9")}, {"CIS2", text("\x80")}}}});
  const auto env = load();
  const auto info = fo4::env::decode_dialogue_info(env, record(env));
  ASSERT_EQ(info.conditions.size(), 1u);
  EXPECT_EQ(info.conditions[0].parameter1_string, "caf\xE9");
  const auto file = path.string();
  const char* paths[] = {file.c_str()};
  radicle_env* abi = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 1, &abi), RADICLE_STATUS_OK) << radicle_last_error();
  const radicle_record* handle = nullptr;
  ASSERT_EQ(radicle_env_winner(abi, radicle_form_key{"Dialogue.esm", 0x800}, &handle), RADICLE_STATUS_OK);
  ASSERT_NE(handle, nullptr);
  const radicle_dialogue_info* abi_info = nullptr;
  ASSERT_EQ(radicle_dialogue_info_decode(abi, handle, &abi_info), RADICLE_STATUS_OK);
  ASSERT_NE(abi_info, nullptr);
  ASSERT_EQ(abi_info->condition_count, 1u);
  EXPECT_STREQ(abi_info->conditions[0].parameter1_string, "caf\xC3\xA9");
  EXPECT_STREQ(abi_info->conditions[0].parameter2_string, "\xE2\x82\xAC");
  radicle_dialogue_info_free(abi_info); radicle_env_destroy(abi);
}

TEST_F(DialogueRead, InfoResetDerivesHoursFromTheRawDayFractionAndExposesItThroughTheAbi) {
  write({{"INFO", 0x800, {{"ENAM", words({0xFFFF8001})}}}, {"INFO", 0x801, {{"ENAM", words({0x00008001})}}},
    {"INFO", 0x802, {{"ENAM", words({0x80008001})}}}, {"INFO", 0x803, {}}});
  const auto env = load();
  EXPECT_EQ(fo4::env::decode_dialogue_info(env, record(env, 0x800)).reset_hours(), 24.0);
  EXPECT_EQ(fo4::env::decode_dialogue_info(env, record(env, 0x801)).reset_hours(), 0.0);
  const auto half_day = fo4::env::decode_dialogue_info(env, record(env, 0x802)).reset_hours();
  ASSERT_TRUE(half_day); EXPECT_NEAR(*half_day, 12.0002, 1e-3);
  EXPECT_FALSE(fo4::env::decode_dialogue_info(env, record(env, 0x803)).reset_hours());
  const auto file = path.string();
  const char* paths[] = {file.c_str()};
  radicle_env* abi = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 1, &abi), RADICLE_STATUS_OK) << radicle_last_error();
  const radicle_record* handle = nullptr;
  ASSERT_EQ(radicle_env_winner(abi, radicle_form_key{"Dialogue.esm", 0x800}, &handle), RADICLE_STATUS_OK);
  ASSERT_NE(handle, nullptr);
  const radicle_dialogue_info* info = nullptr;
  ASSERT_EQ(radicle_dialogue_info_decode(abi, handle, &info), RADICLE_STATUS_OK);
  ASSERT_NE(info, nullptr);
  EXPECT_EQ(info->has_reset_hours, 1); EXPECT_EQ(info->reset_hours, 24.0); EXPECT_EQ(info->reset_time_encoded, 0xFFFF);
  radicle_dialogue_info_free(info);
  ASSERT_EQ(radicle_env_winner(abi, radicle_form_key{"Dialogue.esm", 0x803}, &handle), RADICLE_STATUS_OK);
  ASSERT_NE(handle, nullptr);
  ASSERT_EQ(radicle_dialogue_info_decode(abi, handle, &info), RADICLE_STATUS_OK);
  ASSERT_NE(info, nullptr);
  EXPECT_EQ(info->has_reset_hours, 0); EXPECT_EQ(info->has_data, 0);
  radicle_dialogue_info_free(info); radicle_env_destroy(abi);
}

TEST_F(DialogueRead, SceneActionTypesStayRawForUnknownIds) {
  write({{"SCEN", 0x800, {{"ANAM", half(7)}, {"ANAM", {}}, {"ANAM", half(0xFFFF)}, {"ANAM", {}}, {"ANAM", half(3)}, {"ANAM", {}}}}});
  const auto env = load(); const auto scene = fo4::env::decode_scene(env, record(env));
  ASSERT_EQ(scene.actions.size(), 3u);
  EXPECT_EQ(scene.actions[0].type, 7); EXPECT_EQ(scene.actions[1].type, 0xFFFF); EXPECT_EQ(scene.actions[2].type, 3);
  EXPECT_FALSE(fo4::env::scene_action_type(scene.actions[0].type));
  EXPECT_EQ(fo4::env::scene_action_type(scene.actions[2].type), fo4::env::SceneActionType::player_dialogue);
  EXPECT_EQ(fo4::env::scene_action_type_name(scene.actions[1].type), "");
}

TEST(DialogueSceneActions, KnownIdsMapToNamedClassesAndUnknownIdsStayEmpty) {
  using fo4::env::SceneActionType;
  EXPECT_EQ(fo4::env::scene_action_type(0), SceneActionType::dialogue);
  EXPECT_EQ(fo4::env::scene_action_type(3), SceneActionType::player_dialogue);
  EXPECT_EQ(fo4::env::scene_action_type(6), SceneActionType::radio);
  EXPECT_FALSE(fo4::env::scene_action_type(7));
  EXPECT_FALSE(fo4::env::scene_action_type(0xFFFF));
  EXPECT_EQ(fo4::env::scene_action_type_name(2), "timer");
  EXPECT_EQ(fo4::env::scene_action_type_name(7), "");
  EXPECT_EQ(fo4::env::scene_action_type_name(0xFFFF), "");
  EXPECT_STREQ(radicle_scene_action_type_name(5), "npc_response_dialogue");
  EXPECT_STREQ(radicle_scene_action_type_name(0xFFFF), "");
}

std::vector<std::byte> floats(std::initializer_list<float> values) {
  fo4::test::ByteWriter out; for (const auto value : values) out.f32(value); return out.bytes;
}
std::vector<std::byte> halves(std::initializer_list<std::uint16_t> values) {
  fo4::test::ByteWriter out; for (const auto value : values) out.u16(value); return out.bytes;
}
std::vector<std::byte> wname(std::string_view value) {
  fo4::test::ByteWriter out; out.u16(static_cast<std::uint16_t>(value.size())); out.raw(value); return out.bytes;
}
std::vector<Sub> action(std::uint16_t type, std::vector<Sub> payload) {
  std::vector<Sub> out{{"ANAM", half(type)}, {"ALID", words({1})}, {"INAM", words({0})}, {"FNAM", words({0x10000})},
                       {"SNAM", words({0})}, {"ENAM", words({1})}};
  out.insert(out.end(), payload.begin(), payload.end()); out.push_back({"ANAM", {}}); return out;
}
std::vector<std::byte> info_vmad(std::uint8_t flags, std::initializer_list<std::pair<std::uint8_t, const char*>> rows) {
  fo4::test::ByteWriter out; out.u16(5); out.u16(2); out.u16(0); out.u8(3); out.u8(flags);
  out.raw(wname("PF_Info")); out.u8(0); out.u16(1);
  out.raw(wname("Caps")); out.u8(1); out.u8(1); out.u16(0); out.u16(0xFFFF); out.u32(0xF);
  for (const auto& [unknown, function] : rows) { out.u8(unknown); out.raw(wname("PF_Info")); out.raw(wname(function)); }
  return out.bytes;
}
std::uint32_t link_id(const fo4::env::SceneActionTarget& target, fo4::env::SceneLinkSlot wanted) {
  for (const auto& [at, key] : target.links) if (at == wanted) return key.local().value();
  return 0xFFFFFFFFu;
}

TEST_F(DialogueRead, SceneTargetsDecodeEachActionTypeFromTheStoredTypeAndTheTailStage) {
  using fo4::env::SceneLinkSlot;
  std::vector<Sub> subs;
  const auto add = [&](std::uint16_t type, std::vector<Sub> payload) {
    const auto rows = action(type, std::move(payload)); subs.insert(subs.end(), rows.begin(), rows.end());
  };
  add(0, {{"DATA", words({0x810})}, {"DMAX", floats({5.0f})}, {"DMIN", floats({1.0f})}, {"CRIS", floats({60.0f, 2.0f})},
          {"DEMO", words({5})}, {"DEVA", words({40})}, {"HTID", words({2, 0xFFFFFFFF})}, {"VENC", words({0x811})},
          {"ONAM", words({0x812})}, {"PNAM", words({0x813})}});
  add(1, {{"PNAM", words({0x820})}, {"PNAM", words({0x821})}});
  add(2, {{"SNAM", floats({9.0f})}, {"SCQS", half(20)}, {"TNAM", floats({3.0f})}});
  add(3, {{"PTOP", words({0x830})}, {"NTOP", words({0x831})}, {"VENC", words({0x832})}, {"DTID", words({7})},
          {"ONAM", words({0x833})}, {"NPOT", words({0x834})}, {"NPOS", words({0x835})}, {"DTGT", words({4})}});
  add(4, {{"STSC", words({0x850})}, {"LCEP", words({0x851})}, {"INTT", half(3)}, {"SSPN", text("Phase2")},
          {"CITC", words({1})}, {"CTDA", condition(14)}, {"LCEP", words({0x852})}, {"HTID", {}}});
  add(5, {{"PTOP", words({0x840})}, {"QTOP", words({0x841})}, {"DALC", words({0x842})}, {"DTID", words({8, 9})}});
  add(6, {{"DATA", words({0x860})}, {"HTID", words({0x861})}, {"DMAX", floats({7.0f})}, {"VENC", words({0x862})}});
  add(9, {{"ZZZZ", words({1})}});
  subs.push_back({"PNAM", words({0x812})}); subs.push_back({"SCQS", halves({0xFFFF, 5})});
  write({{"SCEN", 0x800, subs}});
  const auto env = load(); const auto targets = fo4::env::decode_scene_targets(env, record(env));
  ASSERT_TRUE(targets.quest_stages); EXPECT_EQ((*targets.quest_stages)[0], -1); EXPECT_EQ((*targets.quest_stages)[1], 5);
  ASSERT_EQ(targets.actions.size(), 8u);
  const auto& dialogue = targets.actions[0];
  EXPECT_EQ(dialogue.type, 0); EXPECT_FALSE(dialogue.malformed); EXPECT_EQ(dialogue.links.size(), 4u);
  EXPECT_EQ(link_id(dialogue, SceneLinkSlot::topic), 0x810u);
  EXPECT_EQ(link_id(dialogue, SceneLinkSlot::dialogue_subtype), 0x811u);
  EXPECT_EQ(link_id(dialogue, SceneLinkSlot::audio_output_override), 0x812u);
  EXPECT_EQ(link_id(dialogue, SceneLinkSlot::anim_arch_type), 0x813u);
  EXPECT_EQ(dialogue.max_seconds, 5.0f); EXPECT_EQ(dialogue.min_seconds, 1.0f);
  ASSERT_TRUE(dialogue.camera); EXPECT_EQ((*dialogue.camera)[0], 60.0f); EXPECT_EQ((*dialogue.camera)[1], 2.0f);
  EXPECT_EQ(dialogue.emotion_type, 5u); EXPECT_EQ(dialogue.emotion_value, 40u);
  EXPECT_EQ(dialogue.head_tracking, (std::vector<std::int32_t>{2, -1}));
  const auto& package = targets.actions[1];
  ASSERT_EQ(package.packages.size(), 2u); EXPECT_EQ(package.packages[0].local().value(), 0x820u);
  EXPECT_EQ(package.packages[1].local().value(), 0x821u); EXPECT_TRUE(package.links.empty()); EXPECT_FALSE(package.malformed);
  const auto& timer = targets.actions[2];
  EXPECT_EQ(timer.max_seconds, 9.0f); EXPECT_EQ(timer.min_seconds, 3.0f); EXPECT_EQ(timer.quest_stage, 20);
  const auto& player = targets.actions[3];
  EXPECT_EQ(link_id(player, SceneLinkSlot::player_positive_response), 0x830u);
  EXPECT_EQ(link_id(player, SceneLinkSlot::player_negative_response), 0x831u);
  EXPECT_EQ(link_id(player, SceneLinkSlot::player_positive_subtype), 0x832u);
  EXPECT_EQ(link_id(player, SceneLinkSlot::audio_output_override), 0x833u);
  EXPECT_EQ(link_id(player, SceneLinkSlot::npc_positive_response), 0x834u);
  EXPECT_EQ(link_id(player, SceneLinkSlot::npc_positive_subtype), 0x835u);
  EXPECT_EQ(link_id(player, SceneLinkSlot::dialogue_subtype), 0xFFFFFFFFu);
  EXPECT_EQ(player.head_tracking, (std::vector<std::int32_t>{7})); EXPECT_EQ(player.dialogue_target_actor, 4);
  const auto& start = targets.actions[4];
  EXPECT_EQ(link_id(start, SceneLinkSlot::topic), 0x850u); EXPECT_TRUE(start.end_scene_greeting); EXPECT_FALSE(start.malformed);
  ASSERT_EQ(start.start_entries.size(), 2u);
  EXPECT_EQ(start.start_entries[0].scene.local().value(), 0x851u); EXPECT_EQ(start.start_entries[0].phase_index, 3);
  EXPECT_EQ(start.start_entries[0].phase_name, "Phase2"); ASSERT_EQ(start.start_entries[0].conditions.size(), 1u);
  EXPECT_EQ(start.start_entries[0].conditions[0].function, 14);
  EXPECT_EQ(start.start_entries[1].scene.local().value(), 0x852u); EXPECT_FALSE(start.start_entries[1].phase_name);
  EXPECT_TRUE(start.start_entries[1].conditions.empty());
  const auto& reply = targets.actions[5];
  EXPECT_EQ(link_id(reply, SceneLinkSlot::player_positive_response), 0x840u);
  EXPECT_EQ(link_id(reply, SceneLinkSlot::player_question_response), 0x841u);
  EXPECT_EQ(link_id(reply, SceneLinkSlot::player_question_subtype), 0x842u);
  EXPECT_EQ(reply.head_tracking, (std::vector<std::int32_t>{8, 9}));
  const auto& radio = targets.actions[6];
  EXPECT_EQ(link_id(radio, SceneLinkSlot::topic), 0x860u); EXPECT_EQ(link_id(radio, SceneLinkSlot::sound), 0x861u);
  EXPECT_EQ(radio.max_seconds, 7.0f); EXPECT_EQ(link_id(radio, SceneLinkSlot::dialogue_subtype), 0x862u);
  EXPECT_TRUE(radio.head_tracking.empty());
  const auto& unknown = targets.actions[7];
  EXPECT_EQ(unknown.type, 9); EXPECT_FALSE(unknown.malformed); EXPECT_TRUE(unknown.links.empty());

  const auto file = path.string();
  const char* paths[] = {file.c_str()};
  radicle_env* abi = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 1, &abi), RADICLE_STATUS_OK) << radicle_last_error();
  const radicle_record* handle = nullptr;
  ASSERT_EQ(radicle_env_winner(abi, radicle_form_key{"Dialogue.esm", 0x800}, &handle), RADICLE_STATUS_OK);
  const radicle_scene_targets* view = nullptr;
  ASSERT_EQ(radicle_scene_targets_decode(abi, handle, &view), RADICLE_STATUS_OK) << radicle_last_error();
  ASSERT_NE(view, nullptr);
  EXPECT_EQ(view->has_quest_stages, 1); EXPECT_EQ(view->quest_begin_stage, -1); EXPECT_EQ(view->quest_end_stage, 5);
  ASSERT_EQ(view->action_count, 8u);
  EXPECT_EQ(view->actions[0].link_count, 4u); EXPECT_EQ(view->actions[0].head_tracking_count, 2u);
  EXPECT_EQ(view->actions[0].head_tracking[1], -1); EXPECT_EQ(view->actions[0].has_camera, 1);
  EXPECT_EQ(view->actions[0].camera_fov, 60.0f); EXPECT_EQ(view->actions[0].emotion_type, 5u);
  ASSERT_EQ(view->actions[1].package_count, 2u); EXPECT_EQ(view->actions[1].packages[1].local_id, 0x821u);
  EXPECT_EQ(view->actions[2].has_quest_stage, 1); EXPECT_EQ(view->actions[2].quest_stage, 20);
  EXPECT_EQ(view->actions[3].has_dialogue_target_actor, 1); EXPECT_EQ(view->actions[3].dialogue_target_actor, 4);
  ASSERT_EQ(view->actions[4].start_entry_count, 2u); EXPECT_EQ(view->actions[4].end_scene_greeting, 1);
  EXPECT_EQ(view->actions[4].start_entries[0].phase_index, 3);
  EXPECT_STREQ(view->actions[4].start_entries[0].phase_name, "Phase2");
  ASSERT_EQ(view->actions[4].start_entries[0].condition_count, 1u);
  EXPECT_EQ(view->actions[4].start_entries[0].conditions[0].function, 14);
  EXPECT_EQ(view->actions[4].start_entries[1].has_phase_name, 0);
  EXPECT_EQ(view->actions[7].type, 9); EXPECT_EQ(view->actions[7].malformed, 0);
  bool found = false;
  for (std::size_t i = 0; i < view->actions[6].link_count; ++i) {
    if (view->actions[6].links[i].slot == RADICLE_SCENE_LINK_SOUND) {
      found = true; EXPECT_EQ(view->actions[6].links[i].key.local_id, 0x861u);
    }
  }
  EXPECT_TRUE(found);
  radicle_scene_targets_free(view); radicle_env_destroy(abi);
}

TEST_F(DialogueRead, SceneTargetsFlagMalformedPayloadsWithoutFailingAndRefuseBadTailStages) {
  using fo4::env::SceneLinkSlot;
  write({{"SCEN", 0x800, action(0, {{"DATA", words({0x810})}, {"DMAX", half(1)}})},
         {"SCEN", 0x801, action(3, {{"NPOT", words({0x811})}, {"NPOT", words({0x812})}})},
         {"SCEN", 0x802, action(4, {{"LCEP", words({0x851})}, {"INTT", words({3})}})},
         {"SCEN", 0x803, action(2, {{"SNAM", floats({1.0f})}, {"SCQS", words({1})}})},
         {"SCEN", 0x804, {{"PNAM", words({0x812})}, {"SCQS", half(1)}}},
         {"SCEN", 0x805, {{"SCQS", halves({1, 2})}, {"SCQS", halves({3, 4})}}},
         {"SCEN", 0x806, action(1, {{"ZZZZ", words({1})}})},
         {"SCEN", 0x807, {}}});
  const auto env = load();
  const auto bad_size = fo4::env::decode_scene_targets(env, record(env, 0x800));
  ASSERT_EQ(bad_size.actions.size(), 1u); EXPECT_TRUE(bad_size.actions[0].malformed);
  EXPECT_EQ(link_id(bad_size.actions[0], SceneLinkSlot::topic), 0x810u); EXPECT_FALSE(bad_size.actions[0].max_seconds);
  const auto repeated = fo4::env::decode_scene_targets(env, record(env, 0x801));
  EXPECT_TRUE(repeated.actions[0].malformed); ASSERT_EQ(repeated.actions[0].links.size(), 1u);
  EXPECT_EQ(link_id(repeated.actions[0], SceneLinkSlot::npc_positive_response), 0x811u);
  const auto entry = fo4::env::decode_scene_targets(env, record(env, 0x802));
  EXPECT_TRUE(entry.actions[0].malformed); ASSERT_EQ(entry.actions[0].start_entries.size(), 1u);
  EXPECT_EQ(entry.actions[0].start_entries[0].phase_index, 0);
  const auto timer = fo4::env::decode_scene_targets(env, record(env, 0x803));
  EXPECT_TRUE(timer.actions[0].malformed); EXPECT_EQ(timer.actions[0].max_seconds, 1.0f);
  EXPECT_FALSE(timer.actions[0].quest_stage);
  expect_decode_error([&] { fo4::env::decode_scene_targets(env, record(env, 0x804)); }, "invalid scoped field size");
  expect_decode_error([&] { fo4::env::decode_scene_targets(env, record(env, 0x805)); }, "duplicate scoped field");
  const auto stray = fo4::env::decode_scene_targets(env, record(env, 0x806));
  EXPECT_TRUE(stray.actions[0].malformed); EXPECT_TRUE(stray.actions[0].packages.empty());
  const auto empty = fo4::env::decode_scene_targets(env, record(env, 0x807));
  EXPECT_FALSE(empty.quest_stages); EXPECT_TRUE(empty.actions.empty());
}

TEST_F(DialogueRead, InfoResultReportsStagesPhaseAndEachFragmentStateAsData) {
  fo4::test::ByteWriter leading; leading.u16(5); leading.u16(2); leading.u16(0);
  fo4::test::ByteWriter unsupported; unsupported.u16(5); unsupported.u16(2); unsupported.u16(1);
  unsupported.raw(wname("S")); unsupported.u8(0); unsupported.u16(1); unsupported.raw(wname("P"));
  unsupported.u8(99); unsupported.u8(0);
  write({{"INFO", 0x800, {{"TIQS", halves({3, 0xFFFF})}, {"NAM0", text("Phase_A")},
                          {"VMAD", info_vmad(2, {{7, "Fragment_End"}})}}},
         {"INFO", 0x801, {}},
         {"INFO", 0x802, {{"VMAD", leading.bytes}}},
         {"INFO", 0x803, {{"VMAD", unsupported.bytes}}},
         {"INFO", 0x804, {{"TIQS", halves({57, 62})}, {"VMAD", info_vmad(3, {{1, "Fragment_Begin"}, {2, "Fragment_End"}})}}},
         {"INFO", 0x805, {{"VMAD", info_vmad(1, {})}}},
         {"INFO", 0x806, {{"TIQS", half(1)}}}});
  const auto env = load();
  using fo4::env::InfoFragmentKind;
  using fo4::env::InfoFragmentState;
  const auto end_only = fo4::env::decode_info_result(env, record(env, 0x800));
  ASSERT_TRUE(end_only.stages); EXPECT_EQ((*end_only.stages)[0], 3); EXPECT_EQ((*end_only.stages)[1], 0xFFFF);
  EXPECT_EQ(end_only.start_scene_phase, "Phase_A"); EXPECT_EQ(end_only.fragment_state, InfoFragmentState::present);
  EXPECT_EQ(end_only.fragment_version, 3); EXPECT_EQ(end_only.fragment_flags, 2);
  ASSERT_EQ(end_only.rows.size(), 1u); EXPECT_EQ(end_only.rows[0].kind, InfoFragmentKind::on_end);
  EXPECT_EQ(end_only.rows[0].unknown, 7); EXPECT_EQ(end_only.rows[0].script, "PF_Info");
  EXPECT_EQ(end_only.rows[0].function, "Fragment_End");
  EXPECT_EQ(end_only.fragment_script.name, "PF_Info"); ASSERT_EQ(end_only.fragment_script.properties.size(), 1u);
  EXPECT_EQ(end_only.fragment_script.properties[0].name, "Caps");
  ASSERT_TRUE(end_only.fragment_script.properties[0].object);
  EXPECT_EQ(end_only.fragment_script.properties[0].object->value(), 0xFu);
  const auto none = fo4::env::decode_info_result(env, record(env, 0x801));
  EXPECT_FALSE(none.stages); EXPECT_FALSE(none.start_scene_phase); EXPECT_EQ(none.fragment_state, InfoFragmentState::no_vmad);
  const auto bare = fo4::env::decode_info_result(env, record(env, 0x802));
  EXPECT_EQ(bare.fragment_state, InfoFragmentState::no_section); EXPECT_TRUE(bare.rows.empty());
  const auto early = fo4::env::decode_info_result(env, record(env, 0x803));
  EXPECT_EQ(early.fragment_state, InfoFragmentState::unreadable); EXPECT_FALSE(early.fragment_failure.empty());
  const auto both = fo4::env::decode_info_result(env, record(env, 0x804));
  ASSERT_EQ(both.rows.size(), 2u); EXPECT_EQ(both.rows[0].kind, InfoFragmentKind::on_begin);
  EXPECT_EQ(both.rows[0].function, "Fragment_Begin"); EXPECT_EQ(both.rows[1].kind, InfoFragmentKind::on_end);
  const auto cut = fo4::env::decode_info_result(env, record(env, 0x805));
  EXPECT_EQ(cut.fragment_state, InfoFragmentState::unreadable); EXPECT_TRUE(cut.rows.empty());
  expect_decode_error([&] { fo4::env::decode_info_result(env, record(env, 0x806)); }, "must be 4 bytes");

  const auto file = path.string();
  const char* paths[] = {file.c_str()};
  radicle_env* abi = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 1, &abi), RADICLE_STATUS_OK) << radicle_last_error();
  const radicle_record* handle = nullptr;
  ASSERT_EQ(radicle_env_winner(abi, radicle_form_key{"Dialogue.esm", 0x804}, &handle), RADICLE_STATUS_OK);
  const radicle_info_result* view = nullptr;
  ASSERT_EQ(radicle_info_result_decode(abi, handle, &view), RADICLE_STATUS_OK) << radicle_last_error();
  ASSERT_NE(view, nullptr);
  EXPECT_EQ(view->has_stages, 1); EXPECT_EQ(view->begin_stage, 57); EXPECT_EQ(view->end_stage, 62);
  EXPECT_EQ(view->has_start_scene_phase, 0); EXPECT_EQ(view->fragment_state, RADICLE_INFO_FRAGMENT_PRESENT);
  EXPECT_EQ(view->fragment_failure, nullptr); EXPECT_EQ(view->fragment_version, 3); EXPECT_EQ(view->fragment_flags, 3);
  ASSERT_EQ(view->row_count, 2u); EXPECT_EQ(view->rows[0].kind, 1); EXPECT_EQ(view->rows[1].kind, 2);
  EXPECT_STREQ(view->rows[0].function, "Fragment_Begin"); EXPECT_STREQ(view->rows[1].script, "PF_Info");
  radicle_vmad_scripts* scripts = nullptr;
  ASSERT_EQ(radicle_info_fragment_script(abi, handle, &scripts), RADICLE_STATUS_OK) << radicle_last_error();
  ASSERT_EQ(radicle_vmad_script_count(scripts), 1u);
  radicle_vmad_script script{}; ASSERT_EQ(radicle_vmad_scripts_script(scripts, 0, &script), RADICLE_STATUS_OK);
  EXPECT_STREQ(script.name, "PF_Info"); ASSERT_EQ(script.property_count, 1u);
  radicle_vmad_property property{}; ASSERT_EQ(radicle_vmad_scripts_property(scripts, 0, 0, &property), RADICLE_STATUS_OK);
  EXPECT_STREQ(property.name, "Caps"); EXPECT_EQ(property.value_kind, RADICLE_VMAD_OBJECT);
  EXPECT_EQ(property.object_form_id, 0xFu); EXPECT_EQ(property.alias, -1);
  radicle_vmad_scripts_destroy(scripts); radicle_info_result_free(view);
  ASSERT_EQ(radicle_env_winner(abi, radicle_form_key{"Dialogue.esm", 0x802}, &handle), RADICLE_STATUS_OK);
  scripts = nullptr;
  EXPECT_EQ(radicle_info_fragment_script(abi, handle, &scripts), RADICLE_STATUS_NOT_FOUND);
  ASSERT_EQ(radicle_env_winner(abi, radicle_form_key{"Dialogue.esm", 0x803}, &handle), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_info_result_decode(abi, handle, &view), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(view->fragment_state, RADICLE_INFO_FRAGMENT_UNREADABLE); EXPECT_NE(view->fragment_failure, nullptr);
  radicle_info_result_free(view); radicle_env_destroy(abi);
}

std::vector<std::byte> term_vmad(std::initializer_list<std::pair<std::uint32_t, const char*>> rows, std::uint16_t declared) {
  fo4::test::ByteWriter out; out.u16(5); out.u16(2); out.u16(0); out.u8(3);
  out.raw(wname("PF_Term")); out.u8(0); out.u16(1);
  out.raw(wname("Caps")); out.u8(1); out.u8(1); out.u16(0); out.u16(0xFFFF); out.u32(0xF);
  out.u16(declared);
  for (const auto& [index, function] : rows) { out.u32(index); out.u8(0); out.raw(wname("PF_Term")); out.raw(wname(function)); }
  return out.bytes;
}

// Regression: a TERM fragment section was written but never read back, so the engine could not name the function behind a menu item; rows must keep stored order and the full 32-bit index.
TEST_F(DialogueRead, TermFragmentsReportEachStateAndKeepStoredOrderAndIndex) {
  fo4::test::ByteWriter leading; leading.u16(5); leading.u16(2); leading.u16(0);
  fo4::test::ByteWriter unsupported; unsupported.u16(5); unsupported.u16(2); unsupported.u16(1);
  unsupported.raw(wname("S")); unsupported.u8(0); unsupported.u16(1); unsupported.raw(wname("P"));
  unsupported.u8(99); unsupported.u8(0);
  const auto both = term_vmad({{8, "Fragment_Terminal_08"}, {0x10002, "Fragment_Wide"}, {2, "Fragment_Terminal_02"}}, 3);
  write({{"TERM", 0x800, {{"VMAD", both}}},
         {"TERM", 0x801, {}},
         {"TERM", 0x802, {{"VMAD", leading.bytes}}},
         {"TERM", 0x803, {{"VMAD", unsupported.bytes}}},
         {"TERM", 0x804, {{"VMAD", term_vmad({{1, "Fragment_Terminal_01"}}, 2)}}},
         {"PERK", 0x805, {{"VMAD", both}}}});
  const auto env = load();
  using fo4::env::TermFragmentState;
  const auto present = fo4::env::decode_term_fragments(env, record(env, 0x800));
  EXPECT_EQ(present.state, TermFragmentState::present); EXPECT_TRUE(present.failure.empty()); EXPECT_EQ(present.version, 3);
  ASSERT_EQ(present.rows.size(), 3u);
  EXPECT_EQ(present.rows[0].index, 8u); EXPECT_EQ(present.rows[0].function, "Fragment_Terminal_08");
  EXPECT_EQ(present.rows[1].index, 0x10002u); EXPECT_EQ(present.rows[1].function, "Fragment_Wide");
  EXPECT_EQ(present.rows[2].index, 2u); EXPECT_EQ(present.rows[2].script, "PF_Term");
  EXPECT_EQ(present.script.name, "PF_Term"); ASSERT_EQ(present.script.properties.size(), 1u);
  EXPECT_EQ(present.script.properties[0].name, "Caps"); ASSERT_TRUE(present.script.properties[0].object);
  EXPECT_EQ(present.script.properties[0].object->value(), 0xFu);
  EXPECT_EQ(fo4::env::decode_term_fragments(env, record(env, 0x801)).state, TermFragmentState::no_vmad);
  const auto bare = fo4::env::decode_term_fragments(env, record(env, 0x802));
  EXPECT_EQ(bare.state, TermFragmentState::no_section); EXPECT_TRUE(bare.rows.empty());
  const auto early = fo4::env::decode_term_fragments(env, record(env, 0x803));
  EXPECT_EQ(early.state, TermFragmentState::unreadable); EXPECT_FALSE(early.failure.empty());
  const auto cut = fo4::env::decode_term_fragments(env, record(env, 0x804));
  EXPECT_EQ(cut.state, TermFragmentState::unreadable); EXPECT_TRUE(cut.rows.empty());
  expect_decode_error([&] { fo4::env::decode_term_fragments(env, record(env, 0x805)); }, "expected a TERM record");

  const auto file = path.string();
  const char* paths[] = {file.c_str()};
  radicle_env* abi = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 1, &abi), RADICLE_STATUS_OK) << radicle_last_error();
  const radicle_record* handle = nullptr;
  ASSERT_EQ(radicle_env_winner(abi, radicle_form_key{"Dialogue.esm", 0x800}, &handle), RADICLE_STATUS_OK);
  const radicle_term_fragments* view = nullptr;
  ASSERT_EQ(radicle_term_fragments_decode(abi, handle, &view), RADICLE_STATUS_OK) << radicle_last_error();
  ASSERT_NE(view, nullptr);
  EXPECT_EQ(view->state, RADICLE_TERM_FRAGMENT_PRESENT); EXPECT_EQ(view->failure, nullptr);
  EXPECT_EQ(view->version, 3); EXPECT_STREQ(view->script_name, "PF_Term");
  ASSERT_EQ(view->row_count, 3u);
  EXPECT_EQ(view->rows[0].index, 8u); EXPECT_STREQ(view->rows[0].function, "Fragment_Terminal_08");
  EXPECT_EQ(view->rows[1].index, 0x10002u); EXPECT_STREQ(view->rows[2].script, "PF_Term");
  radicle_vmad_scripts* scripts = nullptr;
  ASSERT_EQ(radicle_term_fragment_script(abi, handle, &scripts), RADICLE_STATUS_OK) << radicle_last_error();
  ASSERT_EQ(radicle_vmad_script_count(scripts), 1u);
  radicle_vmad_script script{}; ASSERT_EQ(radicle_vmad_scripts_script(scripts, 0, &script), RADICLE_STATUS_OK);
  EXPECT_STREQ(script.name, "PF_Term"); ASSERT_EQ(script.property_count, 1u);
  radicle_vmad_property property{}; ASSERT_EQ(radicle_vmad_scripts_property(scripts, 0, 0, &property), RADICLE_STATUS_OK);
  EXPECT_STREQ(property.name, "Caps"); EXPECT_EQ(property.object_form_id, 0xFu);
  radicle_vmad_scripts_destroy(scripts); radicle_term_fragments_free(view);
  ASSERT_EQ(radicle_env_winner(abi, radicle_form_key{"Dialogue.esm", 0x802}, &handle), RADICLE_STATUS_OK);
  scripts = nullptr;
  EXPECT_EQ(radicle_term_fragment_script(abi, handle, &scripts), RADICLE_STATUS_NOT_FOUND);
  ASSERT_EQ(radicle_env_winner(abi, radicle_form_key{"Dialogue.esm", 0x803}, &handle), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_term_fragments_decode(abi, handle, &view), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(view->state, RADICLE_TERM_FRAGMENT_UNREADABLE); EXPECT_NE(view->failure, nullptr);
  radicle_term_fragments_free(view);
  ASSERT_EQ(radicle_env_winner(abi, radicle_form_key{"Dialogue.esm", 0x805}, &handle), RADICLE_STATUS_OK);
  EXPECT_NE(radicle_term_fragments_decode(abi, handle, &view), RADICLE_STATUS_OK);
  radicle_env_destroy(abi);
}

}
