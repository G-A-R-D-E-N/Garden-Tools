#include "fo4/c_api_env.h"
#include "fo4/environment/environment.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace {

struct Sub {
  std::string signature;
  std::vector<std::byte> data;
};

struct Rec {
  std::string signature;
  std::uint32_t form_id{0};
  std::uint32_t flags{0};
  std::vector<Sub> subrecords;
};

std::vector<std::byte> zstring(std::string_view text) {
  std::vector<std::byte> bytes;
  for (const char ch : text) bytes.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  bytes.push_back(std::byte{0});
  return bytes;
}

std::vector<std::byte> u32_bytes(std::uint32_t value) {
  fo4::test::ByteWriter out;
  out.u32(value);
  return out.bytes;
}

Sub edid(std::string_view text) { return {"EDID", zstring(text)}; }

std::vector<std::byte> make_plugin(std::uint32_t tes4_flags,
                                   const std::vector<std::string>& masters,
                                   const std::vector<Rec>& records) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", tes4_flags, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(static_cast<std::uint32_t>(records.size()));
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  for (const auto& master : masters) {
    fo4::test::write_subrecord(out, "MAST", zstring(master));
    fo4::test::ByteWriter data;
    data.u32(0);
    data.u32(0);
    fo4::test::write_subrecord(out, "DATA", data.bytes);
  }
  fo4::test::end_record(out, tes4);

  // One top group per signature, in first-seen order.
  std::vector<std::string> order;
  for (const auto& record : records) {
    if (std::find(order.begin(), order.end(), record.signature) == order.end()) {
      order.push_back(record.signature);
    }
  }
  for (const auto& signature : order) {
    const auto group = fo4::test::begin_group(out, signature, 0);
    for (const auto& record : records) {
      if (record.signature != signature) continue;
      const auto header = fo4::test::begin_record(out, record.signature, record.flags,
                                                  record.form_id);
      for (const auto& sub : record.subrecords) {
        fo4::test::write_subrecord(out, sub.signature, sub.data);
      }
      fo4::test::end_record(out, header);
    }
    fo4::test::end_group(out, group);
  }
  return out.bytes;
}

class EnvironmentTest : public ::testing::Test {
protected:
  void SetUp() override {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    root_ = std::filesystem::temp_directory_path() / ("radicle_env_" + std::to_string(id));
    std::filesystem::create_directories(root_);
  }

  void TearDown() override {
    std::error_code ec;
    std::filesystem::remove_all(root_, ec);
  }

  std::filesystem::path write(std::string_view name, const std::vector<std::byte>& bytes) {
    const auto path = root_ / std::string(name);
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    return path;
  }

  // Patch.esp overrides Base.esm 000800 with a renamed EDID and adds its own 000900.
  std::vector<std::filesystem::path> write_base_and_patch() {
    const auto base = write(
        "Base.esm",
        make_plugin(0x1, {},
                    {{"STAT", 0x00000800, 0,
                      {edid("RockBase"), {"MODL", zstring("Rocks\\Rock01.nif")}}},
                     {"MISC", 0x00000801, 0, {edid("Wrench"), {"FULL", zstring("Wrench")}}}}));
    const auto patch = write(
        "Patch.esp",
        make_plugin(0, {"Base.esm"},
                    {{"STAT", 0x00000800, 0,
                      {edid("RockPatched"), {"MODL", zstring("Rocks\\Rock02.nif")}}},
                     {"STAT", 0x01000900, 0, {edid("PatchRock")}}}));
    return {base, patch};
  }

  std::filesystem::path root_;
};

fo4::FormKey key(std::string_view text) {
  return fo4::FormKey::parse(text);
}

}  // namespace

TEST_F(EnvironmentTest, WinnerAndVersionsFollowLoadOrder) {
  const auto paths = write_base_and_patch();
  const auto env = fo4::env::Environment::load(paths);

  const auto versions = env.versions(key("000800:Base.esm"));
  ASSERT_EQ(versions.size(), 2u);
  EXPECT_EQ(versions[0].plugin, 0u);
  EXPECT_EQ(versions[1].plugin, 1u);

  const auto* winner = env.winner(key("000800:Base.esm"));
  ASSERT_NE(winner, nullptr);
  EXPECT_EQ(winner, &versions[1]);
  const auto meta = env.metadata(*winner);
  EXPECT_EQ(meta.editor_id, "RockPatched");
  EXPECT_EQ(meta.model, "Rocks\\Rock02.nif");
  EXPECT_EQ(meta.signature, fo4::Signature::from_string("STAT"));

  EXPECT_EQ(env.winner(key("000900:Patch.esp"))->plugin, 1u);
  EXPECT_EQ(env.winner(key("000FFF:Base.esm")), nullptr);
  EXPECT_TRUE(env.versions(key("000FFF:Base.esm")).empty());
}

TEST_F(EnvironmentTest, EditorIdLookupMatchesAnyVersionByPriorityCaseInsensitively) {
  const auto env = fo4::env::Environment::load(write_base_and_patch());

  const auto* patched = env.find_by_editor_id("rockpatched");
  ASSERT_NE(patched, nullptr);
  EXPECT_EQ(patched->record->form_key, key("000800:Base.esm"));
  EXPECT_EQ(patched->plugin, 1u);
  // Mutagen's link cache indexes every version, so a renamed EDID still reaches the older version.
  const auto* renamed = env.find_by_editor_id("RockBase");
  ASSERT_NE(renamed, nullptr);
  EXPECT_EQ(renamed->plugin, 0u);
  EXPECT_EQ(env.find_by_editor_id("   "), nullptr);

  EXPECT_NE(env.find_by_editor_id("Wrench", fo4::Signature::from_string("MISC")), nullptr);
  EXPECT_EQ(env.find_by_editor_id("Wrench", fo4::Signature::from_string("STAT")), nullptr);
  EXPECT_EQ(env.find_by_editor_id(""), nullptr);
}

TEST_F(EnvironmentTest, WinnersOfGroupsByHighestPriorityPlugin) {
  const auto env = fo4::env::Environment::load(write_base_and_patch());
  const auto stats = env.winners_of(fo4::Signature::from_string("STAT"));
  ASSERT_EQ(stats.size(), 2u);
  EXPECT_EQ(stats[0]->record->form_key, key("000800:Base.esm"));
  EXPECT_EQ(stats[0]->plugin, 1u);
  EXPECT_EQ(stats[1]->record->form_key, key("000900:Patch.esp"));

  const auto misc = env.winners_of(fo4::Signature::from_string("MISC"));
  ASSERT_EQ(misc.size(), 1u);
  EXPECT_EQ(misc[0]->plugin, 0u);
}

TEST_F(EnvironmentTest, WinnerSkipsPartialFormOverrideOfPartialCapableRecord) {
  const auto base = write(
      "Base.esm", make_plugin(0x1, {}, {{"QUST", 0x00000800, 0, {edid("QuestBase")}}}));
  const auto mid = write(
      "Mid.esp", make_plugin(0, {"Base.esm"}, {{"QUST", 0x00000800, 0, {edid("QuestMid")}}}));
  const auto partial = write(
      "Partial.esp",
      make_plugin(0, {"Base.esm"}, {{"QUST", 0x00000800, 0x4000, {edid("QuestPartial")}}}));
  const std::vector<std::filesystem::path> ordered{base, mid, partial};
  const auto env = fo4::env::Environment::load(ordered);

  const auto versions = env.versions(key("000800:Base.esm"));
  ASSERT_EQ(versions.size(), 3u);
  const auto* winner = env.winner(key("000800:Base.esm"));
  ASSERT_NE(winner, nullptr);
  EXPECT_EQ(winner, &versions[1]);
  EXPECT_EQ(env.metadata(*winner).editor_id, "QuestMid");
}

TEST_F(EnvironmentTest, WinnerFallsBackToMasterWhenEveryOverrideIsPartialForm) {
  const auto base = write(
      "Base.esm", make_plugin(0x1, {}, {{"QUST", 0x00000800, 0, {edid("QuestBase")}}}));
  const auto mid = write(
      "Mid.esp",
      make_plugin(0, {"Base.esm"}, {{"QUST", 0x00000800, 0x4000, {edid("QuestMid")}}}));
  const auto partial = write(
      "Partial.esp",
      make_plugin(0, {"Base.esm"}, {{"QUST", 0x00000800, 0x4000, {edid("QuestPartial")}}}));
  const std::vector<std::filesystem::path> ordered{base, mid, partial};
  const auto env = fo4::env::Environment::load(ordered);

  const auto versions = env.versions(key("000800:Base.esm"));
  ASSERT_EQ(versions.size(), 3u);
  EXPECT_EQ(env.winner(key("000800:Base.esm")), &versions[0]);
}

TEST_F(EnvironmentTest, WinnerKeepsLastVersionOfRecordTypeWithoutPartialForm) {
  const auto base = write(
      "Base.esm", make_plugin(0x1, {}, {{"LCTN", 0x00000800, 0, {edid("LocBase")}}}));
  const auto patch = write(
      "Patch.esp",
      make_plugin(0, {"Base.esm"}, {{"LCTN", 0x00000800, 0x4000, {edid("LocLast")}}}));
  const std::vector<std::filesystem::path> ordered{base, patch};
  const auto env = fo4::env::Environment::load(ordered);

  const auto versions = env.versions(key("000800:Base.esm"));
  ASSERT_EQ(versions.size(), 2u);
  EXPECT_EQ(env.winner(key("000800:Base.esm")), &versions[1]);
}

TEST_F(EnvironmentTest, PluginInfoReportsKindsFlagsAndUnresolvedMasters) {
  const auto light = write("Light.esp", make_plugin(0x200, {"Missing.esm"}, {}));
  const auto paths = write_base_and_patch();
  const std::vector<std::filesystem::path> ordered{paths[0], paths[1], light};
  const auto env = fo4::env::Environment::load(ordered);

  const auto& plugins = env.plugins();
  ASSERT_EQ(plugins.size(), 3u);
  EXPECT_TRUE(plugins[0].master_flag);
  EXPECT_EQ(plugins[0].kind, fo4::PluginKind::full);
  EXPECT_TRUE(plugins[1].unresolved_masters.empty());
  EXPECT_EQ(plugins[2].kind, fo4::PluginKind::light);
  ASSERT_EQ(plugins[2].unresolved_masters.size(), 1u);
  EXPECT_EQ(plugins[2].unresolved_masters[0].filename(), "Missing.esm");
  EXPECT_EQ(env.find_plugin("patch.ESP"), std::optional<std::size_t>{1});
  EXPECT_FALSE(env.find_plugin("Other.esp").has_value());
}

TEST_F(EnvironmentTest, EslExtensionWithoutLightFlagIsLightInPluginInfo) {
  const auto extension = write("Extension.esl", make_plugin(0, {}, {}));
  const auto paths = write_base_and_patch();
  const std::vector<std::filesystem::path> ordered{paths[0], paths[1], extension};
  const auto env = fo4::env::Environment::load(ordered);

  ASSERT_EQ(env.plugins().size(), 3u);
  EXPECT_TRUE(env.plugins()[2].master_flag);
  EXPECT_EQ(env.plugins()[2].kind, fo4::PluginKind::light);
}

TEST_F(EnvironmentTest, MasterLoadedLaterIsUnresolved) {
  const auto paths = write_base_and_patch();
  const std::vector<std::filesystem::path> reversed{paths[1], paths[0]};
  const auto env = fo4::env::Environment::load(reversed);
  ASSERT_EQ(env.plugins()[0].unresolved_masters.size(), 1u);
}

TEST_F(EnvironmentTest, LoadFailsClosed) {
  const auto paths = write_base_and_patch();
  const std::vector<std::filesystem::path> duplicate{paths[0], paths[0]};
  EXPECT_THROW(fo4::env::Environment::load(duplicate), fo4::env::EnvironmentError);

  const std::vector<std::filesystem::path> missing{paths[0], root_ / "Absent.esp"};
  EXPECT_THROW(fo4::env::Environment::load(missing), fo4::env::EnvironmentError);

  const auto repeated = write(
      "Repeated.esp",
      make_plugin(0, {}, {{"MISC", 0x00000800, 0, {}}, {"MISC", 0x00000800, 0, {}}}));
  const std::vector<std::filesystem::path> repeat{repeated};
  EXPECT_THROW(fo4::env::Environment::load(repeat), fo4::env::EnvironmentError);
}

TEST_F(EnvironmentTest, RejectsRecordWhoseMasterIndexIsPastTheMasterTable) {
  const auto paths = write_base_and_patch();
  const auto broken = write("Broken.esp", make_plugin(0, {"Base.esm"}, {{"MISC", 0x02000801, 0, {}}}));
  const std::vector<std::filesystem::path> order{paths[0], broken};
  try {
    (void)fo4::env::Environment::load(order);
    FAIL() << "expected EnvironmentError";
  } catch (const fo4::env::EnvironmentError& error) {
    const std::string message = error.what();
    EXPECT_NE(message.find("Broken.esp"), std::string::npos) << message;
    EXPECT_NE(message.find("02000801"), std::string::npos) << message;
  }
}

TEST_F(EnvironmentTest, LoadErrorsNameNonAnsiPathsInUtf8) {
  const auto folder = root_ / std::filesystem::path(std::u16string{0x041C, 0x043E, 0x0434, 0x044B});
  std::filesystem::create_directories(folder);
  const auto broken = folder / "Broken.esp";
  std::ofstream(broken, std::ios::binary) << "not a plugin";
  const std::vector<std::filesystem::path> order{broken};
  try {
    (void)fo4::env::Environment::load(order);
    FAIL() << "expected EnvironmentError";
  } catch (const fo4::env::EnvironmentError& error) {
    const auto utf8 = broken.u8string();
    EXPECT_NE(std::string(error.what()).find(std::string(utf8.begin(), utf8.end())), std::string::npos);
  }
}

TEST_F(EnvironmentTest, MetadataDecodesNamesBoundsAndFlags) {
  fo4::test::ByteWriter obnd;
  for (const int value : {-10, -20, -30, 10, 20, 30}) {
    obnd.u16(static_cast<std::uint16_t>(value));
  }
  const auto inline_plugin = write(
      "Inline.esp",
      make_plugin(0, {},
                  {{"MISC", 0x00000800, 0x20, {{"FULL", zstring("Tin Can")}, {"OBND", obnd.bytes}}},
                   {"MISC", 0x00000801, 0, {{"FULL", u32_bytes(7)}}}}));
  const auto localized_plugin = write(
      "Localized.esp",
      make_plugin(0x80, {},
                  {{"MISC", 0x00000800, 0, {{"FULL", u32_bytes(0x1234)}}},
                   {"MISC", 0x00000801, 0, {{"FULL", zstring("x")}}}}));
  const std::vector<std::filesystem::path> ordered{inline_plugin, localized_plugin};
  const auto env = fo4::env::Environment::load(ordered);

  const auto can = env.metadata(*env.winner(key("000800:Inline.esp")));
  EXPECT_TRUE(can.deleted);
  EXPECT_EQ(can.flags.value(), 0x20u);
  EXPECT_EQ(can.name.kind, fo4::env::NameKind::inline_text);
  EXPECT_EQ(can.name.text, "Tin Can");
  ASSERT_TRUE(can.bounds.has_value());
  EXPECT_EQ(can.bounds->min[0], -10);
  EXPECT_EQ(can.bounds->max[2], 30);
  EXPECT_FALSE(can.editor_id.has_value());
  EXPECT_FALSE(can.model.has_value());

  EXPECT_EQ(env.metadata(*env.winner(key("000801:Inline.esp"))).name.kind,
            fo4::env::NameKind::malformed);

  const auto localized = env.metadata(*env.winner(key("000800:Localized.esp")));
  EXPECT_EQ(localized.name.kind, fo4::env::NameKind::string_id);
  EXPECT_EQ(localized.name.string_id, 0x1234u);
  EXPECT_EQ(env.metadata(*env.winner(key("000801:Localized.esp"))).name.kind,
            fo4::env::NameKind::malformed);
}

TEST_F(EnvironmentTest, NameIgnoresObjectTemplateCombinationFull) {
  auto plugin = write(
      "Templates.esp",
      make_plugin(0, {},
                  {{"ARMO", 0x00000800, 0,
                    {{"OBTE", u32_bytes(1)}, {"OBTF", {}}, {"FULL", zstring("Rusty")}, {"OBTS", u32_bytes(0)}, {"STOP", {}}}},
                   {"NPC_", 0x00000801, 0,
                    {{"OBTE", u32_bytes(1)}, {"OBTF", {}}, {"FULL", zstring("Rusty")}, {"OBTS", u32_bytes(0)},
                     {"STOP", {}}, {"FULL", zstring("Protectron")}}}}));
  const std::vector<std::filesystem::path> ordered{plugin};
  const auto env = fo4::env::Environment::load(ordered);

  EXPECT_EQ(env.metadata(*env.winner(key("000800:Templates.esp"))).name.kind, fo4::env::NameKind::absent);
  const auto npc = env.metadata(*env.winner(key("000801:Templates.esp")));
  EXPECT_EQ(npc.name.kind, fo4::env::NameKind::inline_text);
  EXPECT_EQ(npc.name.text, "Protectron");
}

TEST_F(EnvironmentTest, CApiQueriesMatchTheEnvironment) {
  const auto paths = write_base_and_patch();
  std::vector<std::string> utf8;
  for (const auto& path : paths) {
    const auto text = path.u8string();
    utf8.emplace_back(text.begin(), text.end());
  }
  const char* raw[] = {utf8[0].c_str(), utf8[1].c_str()};

  radicle_env* env = nullptr;
  ASSERT_EQ(radicle_env_load(raw, 2, &env), RADICLE_STATUS_OK) << radicle_last_error();
  ASSERT_NE(env, nullptr);
  EXPECT_EQ(radicle_env_plugin_count(env), 2u);

  radicle_env_plugin_info info{};
  ASSERT_EQ(radicle_env_get_plugin_info(env, 1, &info), RADICLE_STATUS_OK);
  EXPECT_STREQ(info.filename, "Patch.esp");
  EXPECT_EQ(std::string(info.path), utf8[1]);
  EXPECT_EQ(info.kind, RADICLE_PLUGIN_FULL);
  EXPECT_EQ(info.master_count, 1u);
  EXPECT_EQ(info.unresolved_master_count, 0u);
  const char* master = nullptr;
  int32_t resolved = 0;
  ASSERT_EQ(radicle_env_plugin_master(env, 1, 0, &master, &resolved), RADICLE_STATUS_OK);
  EXPECT_STREQ(master, "Base.esm");
  EXPECT_EQ(resolved, 1);
  EXPECT_EQ(radicle_env_get_plugin_info(env, 2, &info), RADICLE_STATUS_INVALID_ARGUMENT);

  size_t index = 99;
  EXPECT_EQ(radicle_env_find_plugin(env, "BASE.ESM", &index), RADICLE_STATUS_OK);
  EXPECT_EQ(index, 0u);
  EXPECT_EQ(radicle_env_find_plugin(env, "Nope.esp", &index), RADICLE_STATUS_NOT_FOUND);

  const radicle_record* winner = nullptr;
  ASSERT_EQ(radicle_env_winner(env, {"Base.esm", 0x800}, &winner), RADICLE_STATUS_OK);
  radicle_record_info record{};
  ASSERT_EQ(radicle_record_get_info(env, winner, &record), RADICLE_STATUS_OK);
  EXPECT_STREQ(record.owner_plugin, "Base.esm");
  EXPECT_EQ(record.local_id, 0x800u);
  EXPECT_STREQ(record.signature, "STAT");
  EXPECT_EQ(record.plugin_index, 1u);
  EXPECT_STREQ(record.editor_id, "RockPatched");
  EXPECT_STREQ(record.model_path, "Rocks\\Rock02.nif");
  EXPECT_EQ(record.name_kind, RADICLE_NAME_ABSENT);
  EXPECT_EQ(record.has_bounds, 0);
  EXPECT_EQ(radicle_env_winner(env, {"Base.esm", 0xFFF}, &winner), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(winner, nullptr);
  EXPECT_EQ(radicle_env_winner(env, {nullptr, 0x800}, &winner), RADICLE_STATUS_INVALID_ARGUMENT);

  const radicle_record** records = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_env_versions(env, {"Base.esm", 0x800}, &records, &count), RADICLE_STATUS_OK);
  ASSERT_EQ(count, 2u);
  ASSERT_EQ(radicle_record_get_info(env, records[0], &record), RADICLE_STATUS_OK);
  EXPECT_EQ(record.plugin_index, 0u);
  EXPECT_STREQ(record.editor_id, "RockBase");
  radicle_free_record_array(records);
  ASSERT_EQ(radicle_env_versions(env, {"Base.esm", 0xFFF}, &records, &count), RADICLE_STATUS_OK);
  EXPECT_EQ(records, nullptr);
  EXPECT_EQ(count, 0u);

  const radicle_record* found = nullptr;
  ASSERT_EQ(radicle_env_find_editor_id(env, "WRENCH", "MISC", &found), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_record_get_info(env, found, &record), RADICLE_STATUS_OK);
  EXPECT_EQ(record.name_kind, RADICLE_NAME_INLINE);
  EXPECT_STREQ(record.name_text, "Wrench");
  EXPECT_EQ(radicle_env_find_editor_id(env, "Wrench", "STAT", &found), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_env_find_editor_id(env, "Wrench", "MIS", &found),
            RADICLE_STATUS_INVALID_ARGUMENT);

  ASSERT_EQ(radicle_env_winners_of(env, "STAT", &records, &count), RADICLE_STATUS_OK);
  ASSERT_EQ(count, 2u);
  radicle_free_record_array(records);
  EXPECT_EQ(radicle_env_winners_of(env, "ST", &records, &count), RADICLE_STATUS_INVALID_ARGUMENT);

  radicle_env_destroy(env);
}

TEST_F(EnvironmentTest, CApiLoadReportsFailures) {
  radicle_env* env = reinterpret_cast<radicle_env*>(0x1);
  const auto missing = (root_ / "Absent.esp").string();
  const char* raw[] = {missing.c_str()};
  EXPECT_EQ(radicle_env_load(raw, 1, &env), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(env, nullptr);
  EXPECT_NE(std::strlen(radicle_last_error()), 0u);

  const char* null_entry[] = {nullptr};
  EXPECT_EQ(radicle_env_load(null_entry, 1, &env), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_env_load(nullptr, 0, &env), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_env_plugin_count(env), 0u);
  radicle_env_destroy(env);
}
