#include "fo4/c_api_behavior.h"
#include "fo4/environment/furniture.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

namespace {

using Sub = std::pair<std::string, std::vector<std::byte>>;
struct Rec { std::string signature; std::uint32_t id; std::vector<Sub> subs; };

std::vector<std::byte> words(std::initializer_list<std::uint32_t> values) {
  fo4::test::ByteWriter b;
  for (const auto value : values) b.u32(value);
  return b.bytes;
}
std::vector<std::byte> text(const char* value) {
  fo4::test::ByteWriter b;
  b.raw(value);
  b.u8(0);
  return b.bytes;
}
std::vector<std::byte> bare(const char* value) {
  fo4::test::ByteWriter b;
  b.raw(value);
  return b.bytes;
}
std::vector<std::byte> flags(std::uint16_t role, std::uint16_t perspective) {
  fo4::test::ByteWriter b;
  b.u16(role);
  b.u16(perspective);
  return b.bytes;
}

class RaceSubgraphRead : public testing::Test {
protected:
  void SetUp() override {
    root = std::filesystem::temp_directory_path() /
        ("radicle_race_subgraph_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    path = root / "Furn.esm";
  }
  void TearDown() override { std::error_code error; std::filesystem::remove_all(root, error); }
  void write(const std::vector<Rec>& records) {
    fo4::test::ByteWriter out;
    const auto h = fo4::test::begin_record(out, "TES4", 1, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1);
    hedr.u32(static_cast<std::uint32_t>(records.size()));
    hedr.u32(0x900);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, h);
    for (const auto& rec : records) {
      const auto group = fo4::test::begin_group(out, rec.signature, 0);
      const auto at = fo4::test::begin_record(out, rec.signature, 0, rec.id, 131);
      for (const auto& [tag, bytes] : rec.subs) fo4::test::write_subrecord(out, tag, bytes);
      fo4::test::end_record(out, at);
      fo4::test::end_group(out, group);
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(out.bytes.data()), static_cast<std::streamsize>(out.bytes.size()));
  }
  fo4::env::Environment load() {
    const std::filesystem::path paths[] = {path};
    return fo4::env::Environment::load(paths);
  }
  const fo4::env::RecordRef& record(const fo4::env::Environment& env, std::uint32_t id) {
    return *env.winner(fo4::FormKey{fo4::PluginKey::from_filename("Furn.esm"), fo4::LocalFormId{id}});
  }
  std::filesystem::path root, path;
};

TEST_F(RaceSubgraphRead, BlocksCloseAtSrafAndKeepKeywordsGraphPrefixesAndFurnitureRole) {
  write({{"RACE", 0x800,
          {{"SAKD", words({0x810})}, {"SAKD", words({0x811})}, {"STKD", words({0x812})},
           {"SGNM", text("Actors/Character/Behaviors/FurnitureBehavior.hkx")},
           {"SAPT", text("Actors/Character/Animations/Furniture/Chair/Neutral")},
           {"SAPT", text("Actors/Character/Animations/Furniture/Chair")}, {"SRAF", flags(2, 0)},
           {"SAPT", text("Actors/Character/Animations")}, {"SRAF", flags(0, 1)}}}});
  const auto env = load();
  const auto rows = fo4::env::decode_race_subgraphs(env, record(env, 0x800));
  ASSERT_EQ(rows.size(), 2u);
  EXPECT_EQ(rows[0].role, fo4::env::k_subgraph_role_furniture);
  EXPECT_EQ(rows[0].perspective, 0u);
  ASSERT_EQ(rows[0].actor_keywords.size(), 2u);
  EXPECT_EQ(rows[0].actor_keywords[1].local().value(), 0x811u);
  ASSERT_EQ(rows[0].target_keywords.size(), 1u);
  EXPECT_EQ(rows[0].target_keywords[0].local().value(), 0x812u);
  ASSERT_TRUE(rows[0].behavior_graph.has_value());
  EXPECT_EQ(*rows[0].behavior_graph, "Actors/Character/Behaviors/FurnitureBehavior.hkx");
  ASSERT_EQ(rows[0].path_prefixes.size(), 2u);
  EXPECT_EQ(rows[0].path_prefixes[0], "Actors/Character/Animations/Furniture/Chair/Neutral");
  EXPECT_EQ(rows[1].role, 0u);
  EXPECT_EQ(rows[1].perspective, 1u);
  EXPECT_FALSE(rows[1].behavior_graph.has_value());
  EXPECT_TRUE(rows[1].actor_keywords.empty());
  EXPECT_TRUE(rows[1].target_keywords.empty());
}

TEST_F(RaceSubgraphRead, MalformedBlocksFailClosed) {
  write({{"RACE", 0x800, {{"SGNM", text("a.hkx")}}}, {"RACE", 0x801, {{"STKD", words({1, 2})}, {"SRAF", flags(2, 0)}}},
         {"RACE", 0x802, {{"SGNM", text("a.hkx")}, {"SGNM", text("b.hkx")}, {"SRAF", flags(2, 0)}}},
         {"RACE", 0x803, {{"SRAF", std::vector<std::byte>(2)}}}, {"RACE", 0x804, {}},
         {"RACE", 0x805, {{"SGNM", bare("a.hkx")}, {"SRAF", flags(2, 0)}}}, {"RACE", 0x806, {{"SAKD", std::vector<std::byte>(8)}, {"SRAF", flags(2, 0)}}},
         {"RACE", 0x807, {{"SAKD", words({1})}}}, {"RACE", 0x808, {{"SAPT", bare("a/b")}, {"SRAF", flags(2, 0)}}}});
  const auto env = load();
  EXPECT_THROW(fo4::env::decode_race_subgraphs(env, record(env, 0x800)), fo4::env::WorldDecodeError);
  EXPECT_THROW(fo4::env::decode_race_subgraphs(env, record(env, 0x801)), fo4::env::WorldDecodeError);
  EXPECT_THROW(fo4::env::decode_race_subgraphs(env, record(env, 0x802)), fo4::env::WorldDecodeError);
  EXPECT_THROW(fo4::env::decode_race_subgraphs(env, record(env, 0x803)), fo4::env::WorldDecodeError);
  EXPECT_TRUE(fo4::env::decode_race_subgraphs(env, record(env, 0x804)).empty());
  EXPECT_THROW(fo4::env::decode_race_subgraphs(env, record(env, 0x805)), fo4::env::WorldDecodeError);
  EXPECT_THROW(fo4::env::decode_race_subgraphs(env, record(env, 0x806)), fo4::env::WorldDecodeError);
  EXPECT_THROW(fo4::env::decode_race_subgraphs(env, record(env, 0x807)), fo4::env::WorldDecodeError);
  EXPECT_THROW(fo4::env::decode_race_subgraphs(env, record(env, 0x808)), fo4::env::WorldDecodeError);
}

TEST_F(RaceSubgraphRead, FurnitureKeywordsKeepStoredOrderAndAgreeWithKsiz) {
  write({{"FURN", 0x800, {{"KSIZ", words({2})}, {"KWDA", words({0x820, 0x821})}}}, {"FURN", 0x801, {}},
         {"FURN", 0x802, {{"KSIZ", words({3})}, {"KWDA", words({0x820})}}}, {"FURN", 0x803, {{"KSIZ", words({1})}}},
         {"RACE", 0x804, {}}, {"FURN", 0x805, {{"KWDA", bare("abcdef")}}}, {"FURN", 0x806, {{"KSIZ", words({1})}, {"KWDA", words({0x820, 0x821})}}},
         {"FURN", 0x807, {{"KSIZ", words({0})}}}});
  const auto env = load();
  const auto keywords = fo4::env::decode_furniture_keywords(env, record(env, 0x800));
  ASSERT_EQ(keywords.size(), 2u);
  EXPECT_EQ(keywords[0].local().value(), 0x820u);
  EXPECT_EQ(keywords[1].local().value(), 0x821u);
  EXPECT_TRUE(fo4::env::decode_furniture_keywords(env, record(env, 0x801)).empty());
  EXPECT_THROW(fo4::env::decode_furniture_keywords(env, record(env, 0x802)), fo4::env::WorldDecodeError);
  EXPECT_THROW(fo4::env::decode_furniture_keywords(env, record(env, 0x803)), fo4::env::WorldDecodeError);
  EXPECT_THROW(fo4::env::decode_furniture_keywords(env, record(env, 0x804)), fo4::env::WorldDecodeError);
  EXPECT_THROW(fo4::env::decode_furniture_keywords(env, record(env, 0x805)), fo4::env::WorldDecodeError);
  EXPECT_THROW(fo4::env::decode_furniture_keywords(env, record(env, 0x806)), fo4::env::WorldDecodeError);
  EXPECT_TRUE(fo4::env::decode_furniture_keywords(env, record(env, 0x807)).empty());
}

TEST_F(RaceSubgraphRead, CApiReturnsTheSameRowsAndRejectsTheWrongSignature) {
  write({{"RACE", 0x800, {{"STKD", words({0x812})}, {"SGNM", text("graph.hkx")}, {"SAPT", text("a/b")}, {"SRAF", flags(2, 0)},
                         {"SRAF", flags(0, 1)}}},
         {"FURN", 0x801, {{"KSIZ", words({1})}, {"KWDA", words({0x812})}}}});
  const auto raw_path = path.string();
  const char* paths[]{raw_path.c_str()};
  radicle_env* raw_env = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 1, &raw_env), RADICLE_STATUS_OK);
  const std::unique_ptr<radicle_env, decltype(&radicle_env_destroy)> env(raw_env, radicle_env_destroy);
  const radicle_record* race = nullptr;
  const radicle_record* furn = nullptr;
  ASSERT_EQ(radicle_env_winner(raw_env, radicle_form_key{"Furn.esm", 0x800}, &race), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_env_winner(raw_env, radicle_form_key{"Furn.esm", 0x801}, &furn), RADICLE_STATUS_OK);
  const radicle_race_subgraphs* rows = nullptr;
  ASSERT_EQ(radicle_race_subgraphs_decode(raw_env, race, &rows), RADICLE_STATUS_OK);
  const std::unique_ptr<const radicle_race_subgraphs, decltype(&radicle_race_subgraphs_free)> owned_rows(rows, radicle_race_subgraphs_free);
  ASSERT_EQ(rows->count, 2u);
  EXPECT_EQ(rows->rows[1].behavior_graph, nullptr);
  EXPECT_EQ(rows->rows[0].role, 2u);
  EXPECT_STREQ(rows->rows[0].behavior_graph, "graph.hkx");
  ASSERT_EQ(rows->rows[0].path_prefix_count, 1u);
  EXPECT_STREQ(rows->rows[0].path_prefixes[0], "a/b");
  ASSERT_EQ(rows->rows[0].target_keyword_count, 1u);
  EXPECT_EQ(rows->rows[0].target_keywords[0].local_id, 0x812u);
  EXPECT_EQ(rows->rows[0].actor_keyword_count, 0u);
  const radicle_furniture_keywords* keywords = nullptr;
  ASSERT_EQ(radicle_furniture_keywords_decode(raw_env, furn, &keywords), RADICLE_STATUS_OK);
  const std::unique_ptr<const radicle_furniture_keywords, decltype(&radicle_furniture_keywords_free)> owned_keywords(keywords, radicle_furniture_keywords_free);
  ASSERT_EQ(keywords->count, 1u);
  EXPECT_EQ(keywords->keywords[0].local_id, rows->rows[0].target_keywords[0].local_id);
  const radicle_race_subgraphs* wrong = nullptr;
  EXPECT_NE(radicle_race_subgraphs_decode(raw_env, furn, &wrong), RADICLE_STATUS_OK);
  EXPECT_EQ(wrong, nullptr);
  const radicle_furniture_keywords* wrong_keywords = nullptr;
  EXPECT_NE(radicle_furniture_keywords_decode(raw_env, race, &wrong_keywords), RADICLE_STATUS_OK);
  EXPECT_EQ(wrong_keywords, nullptr);
}

}  // namespace
