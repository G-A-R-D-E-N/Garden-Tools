#include "fo4/c_api_vegetation.h"
#include "fo4/environment/vegetation.hpp"
#include "plugin_bytes.hpp"
#include "fo4/plugin/writer.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>

namespace {

using Sub = std::pair<std::string, std::vector<std::byte>>;
struct Rec { std::string signature; std::uint32_t id; std::vector<Sub> subs; };

std::vector<std::byte> words(std::initializer_list<std::uint32_t> values) {
  fo4::test::ByteWriter out;
  for (const auto value : values) out.u32(value);
  return out.bytes;
}

std::vector<std::byte> text(const char* value) {
  fo4::test::ByteWriter out;
  out.raw(value);
  out.u8(0);
  return out.bytes;
}

class VegetationRead : public testing::Test {
protected:
  void SetUp() override {
    root = std::filesystem::temp_directory_path() /
           ("radicle_vegetation_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    path = root / "Vegetation.esm";
  }
  void TearDown() override { std::error_code error; std::filesystem::remove_all(root, error); }
  void write(const std::vector<Rec>& records) {
    fo4::test::ByteWriter out;
    const auto h = fo4::test::begin_record(out, "TES4", 1, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1); hedr.u32(static_cast<std::uint32_t>(records.size())); hedr.u32(0x900);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, h);
    for (const auto& record : records) {
      const auto group = fo4::test::begin_group(out, record.signature, 0);
      const auto rec = fo4::test::begin_record(out, record.signature, 0, record.id, 131);
      for (const auto& [tag, bytes] : record.subs) fo4::test::write_subrecord(out, tag, bytes);
      fo4::test::end_record(out, rec); fo4::test::end_group(out, group);
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(out.bytes.data()), static_cast<std::streamsize>(out.bytes.size()));
  }
  fo4::env::Environment load() { const std::filesystem::path paths[] = {path}; return fo4::env::Environment::load(paths); }
  const fo4::env::RecordRef& record(const fo4::env::Environment& env, std::uint32_t id) {
    return *env.winner(fo4::FormKey{fo4::PluginKey::from_filename("Vegetation.esm"), fo4::LocalFormId{id}});
  }
  std::filesystem::path root, path;
};

std::vector<std::byte> grass_payload() {
  fo4::test::ByteWriter b;
  b.u8(0xFE); b.u8(4); b.u8(90); b.u8(0xAA); b.u16(500); b.u16(0xBBCC); b.u32(7);
  b.f32(32); b.f32(0.2f); b.f32(0.5f); b.f32(10); b.u32(0xDDCCBB81);
  return b.bytes;
}

TEST_F(VegetationRead, StoredFieldsAndRepeatedGrassLinksPreserveValuesAndBytes) {
  fo4::test::ByteWriter wind; for (int i = 0; i < 12; ++i) wind.f32(static_cast<float>(i));
  const auto seasons = words({0x04030201});
  write({{"GRAS", 0x800, {{"DATA", grass_payload()}, {"MODL", text("grass.nif")}}},
         {"TREE", 0x801, {{"CNAM", wind.bytes}, {"PFPC", seasons}, {"PFIG", words({0x810})}, {"SNAM", words({0x811})}}},
         {"FLOR", 0x802, {{"PFPC", seasons}, {"PFIG", words({0x812})}, {"FNAM", {std::byte{0x34}, std::byte{0x80}}}}},
         {"LTEX", 0x803, {{"GNAM", words({0x800})}, {"GNAM", words({0})}, {"GNAM", words({0x800})}}}});
  const auto env = load(); const auto g = fo4::env::decode_grass(env, record(env, 0x800));
  ASSERT_TRUE(g); EXPECT_EQ(g->density, 0xFE); EXPECT_EQ(g->min_slope, 4); EXPECT_EQ(g->max_slope, 90);
  EXPECT_EQ(g->units_from_water, 500); EXPECT_EQ(g->water_type, 7u); EXPECT_EQ(g->position_range, 32);
  EXPECT_EQ(g->height_range, 0.2f); EXPECT_EQ(g->color_range, 0.5f); EXPECT_EQ(g->wave_period, 10); EXPECT_EQ(g->flags, 0x81);
  EXPECT_EQ(env.metadata(record(env, 0x800)).model, "grass.nif");
  const auto t = fo4::env::decode_tree(env, record(env, 0x801)); ASSERT_TRUE(t.wind); EXPECT_EQ((*t.wind)[11], 11);
  EXPECT_EQ(t.harvest.ingredient->local().value(), 0x810u); EXPECT_EQ((*t.harvest.seasons)[3], 4);
  const auto f = fo4::env::decode_flora(env, record(env, 0x802)); EXPECT_EQ(f.flags, 0x8034);
  EXPECT_EQ(f.harvest.ingredient->local().value(), 0x812u);
  const auto links = fo4::env::decode_landscape_grasses(env, record(env, 0x803)); ASSERT_EQ(links.size(), 3);
  EXPECT_EQ(links[0].local().value(), 0x800u); EXPECT_TRUE(links[1].owner().empty()); EXPECT_EQ(links[2], links[0]);
  EXPECT_EQ(fo4::write_plugin(env.plugin(0)), fo4::test::original_bytes(env.plugin(0)));
}

TEST_F(VegetationRead, AbsentFieldsStayAbsentAndMalformedKnownFieldsFailClosed) {
  write({{"GRAS", 0x800, {}}, {"TREE", 0x801, {}}, {"FLOR", 0x802, {}}, {"LTEX", 0x803, {}}});
  const auto empty = load(); EXPECT_FALSE(fo4::env::decode_grass(empty, record(empty, 0x800)));
  EXPECT_FALSE(fo4::env::decode_tree(empty, record(empty, 0x801)).wind);
  EXPECT_FALSE(fo4::env::decode_flora(empty, record(empty, 0x802)).flags);
  EXPECT_TRUE(fo4::env::decode_landscape_grasses(empty, record(empty, 0x803)).empty());
  write({{"GRAS", 0x800, {{"DATA", words({1})}}}, {"TREE", 0x801, {{"CNAM", words({1})}}},
         {"FLOR", 0x802, {{"PFPC", {std::byte{1}}}}}, {"LTEX", 0x803, {{"GNAM", words({0x800, 0x800})}}}});
  const auto bad = load();
  EXPECT_THROW(fo4::env::decode_grass(bad, record(bad, 0x800)), fo4::env::WorldDecodeError);
  EXPECT_THROW(fo4::env::decode_tree(bad, record(bad, 0x801)), fo4::env::WorldDecodeError);
  EXPECT_THROW(fo4::env::decode_flora(bad, record(bad, 0x802)), fo4::env::WorldDecodeError);
  EXPECT_THROW(fo4::env::decode_landscape_grasses(bad, record(bad, 0x803)), fo4::env::WorldDecodeError);
  write({{"GRAS", 0x800, {{"DATA", grass_payload()}, {"DATA", grass_payload()}}}});
  const auto duplicate = load();
  EXPECT_THROW(fo4::env::decode_grass(duplicate, record(duplicate, 0x800)), fo4::env::WorldDecodeError);
}

}

