#include "fo4/c_api_materials.h"
#include "fo4/environment/material_swap.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Sub {
  std::string signature;
  std::vector<std::byte> data;
};

struct Rec {
  std::string signature;
  std::uint32_t form_id{0};
  std::vector<Sub> subrecords;
};

std::vector<std::byte> word(std::uint32_t value) {
  fo4::test::ByteWriter out;
  out.u32(value);
  return out.bytes;
}

std::vector<std::byte> real(float value) {
  fo4::test::ByteWriter out;
  out.f32(value);
  return out.bytes;
}

std::vector<std::byte> zstring(std::string_view text) {
  std::vector<std::byte> out;
  for (const char ch : text) out.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  out.push_back(std::byte{0});
  return out;
}

std::vector<std::byte> make_plugin(const std::vector<Rec>& records) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0x1, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  for (const auto& record : records) {
    const auto group = fo4::test::begin_group(out, record.signature, 0);
    const auto header = fo4::test::begin_record(out, record.signature, 0, record.form_id);
    for (const auto& sub : record.subrecords) fo4::test::write_subrecord(out, sub.signature, sub.data);
    fo4::test::end_record(out, header);
    fo4::test::end_group(out, group);
  }
  return out.bytes;
}

std::vector<Rec> fixture() {
  return {
      {"MSWP", 0x800,
       {{"EDID", zstring("RustSwap")}, {"FNAM", zstring("Props")},
        {"BNAM", zstring("Props\\Box.BGSM")}, {"SNAM", zstring("Props\\BoxRust.BGSM")}, {"CNAM", real(0.5f)},
        {"BNAM", zstring("Props\\Lid.BGSM")}, {"SNAM", zstring("Props\\LidRust.BGSM")},
        {"BNAM", zstring("Props\\Hinge.BGSM")}}},
      {"MSWP", 0x801, {{"SNAM", zstring("Props\\Orphan.BGSM")}}},
      {"MSWP", 0x802, {{"BNAM", zstring("Props\\Box.BGSM")}, {"CNAM", {std::byte{0}, std::byte{0}}}}},
      {"STAT", 0x810, {{"MODL", zstring("Props\\Box.nif")}, {"MODS", word(0x800)}}},
      {"STAT", 0x811, {{"MODL", zstring("Props\\Box.nif")}}},
      {"STAT", 0x812, {{"MODS", word(0x800)}}},
      {"ARMO", 0x820, {{"MOD2", zstring("Male.nif")}, {"MO2S", word(0x800)},
                       {"MOD4", zstring("Female.nif")}, {"MO4S", word(0x801)}}},
      {"ARMO", 0x821, {{"MOD4", zstring("Female.nif")}, {"MO4S", word(0x801)}}},
      {"ARMO", 0x822, {{"MOD2", zstring("Male.nif")}, {"MODC", real(0.75f)},
                       {"MOD4", zstring("Female.nif")}, {"MODC", real(0.125f)}}},
      {"ARMO", 0x823, {{"MOD2", zstring("Male.nif")}, {"MOD4", zstring("Female.nif")}, {"MODC", real(0.125f)}}},
      {"ARMO", 0x824, {{"MOD4", zstring("Female.nif")}, {"MODC", real(0.125f)}}},
      {"STAT", 0x813, {{"MODL", zstring("Props\\Box.nif")}, {"MODC", real(0.25f)}, {"MODS", word(0x800)}}},
      {"STAT", 0x814, {{"MODL", zstring("Props\\Box.nif")}, {"MODC", {std::byte{0}, std::byte{0}}}}},
      {"STAT", 0x815, {{"MODC", real(0.5f)}}},
      {"SCOL", 0x816, {{"MODL", zstring("SCOL\\Swaps.esm\\CM00000816.NIF")}, {"MODC", real(0.375f)}, {"MODS", word(0x800)}}},
      {"REFR", 0x830, {{"NAME", word(0x811)}, {"XMSP", word(0x800)}}},
      {"REFR", 0x831, {{"NAME", word(0x810)}}},
      {"REFR", 0x832, {{"NAME", word(0x810)}, {"XMSP", word(0x800)}, {"XMSP", word(0x801)}}},
  };
}

class MaterialSwapTest : public ::testing::Test {
protected:
  void SetUp() override {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    root_ = std::filesystem::temp_directory_path() / ("radicle_material_swap_" + std::to_string(id));
    std::filesystem::create_directories(root_);
    path_ = root_ / "Swaps.esm";
    const auto data = make_plugin(fixture());
    std::ofstream stream(path_, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
  }

  void TearDown() override {
    std::error_code ec;
    std::filesystem::remove_all(root_, ec);
  }

  std::filesystem::path root_;
  std::filesystem::path path_;
};

fo4::FormKey key(std::uint32_t local) {
  return fo4::FormKey{fo4::PluginKey::from_filename("Swaps.esm"), fo4::LocalFormId{local}};
}

}  // namespace

TEST_F(MaterialSwapTest, DecodesSubstitutionsInOrder) {
  const std::vector<std::filesystem::path> paths{path_};
  const auto env = fo4::env::Environment::load(paths);
  const auto swap = fo4::env::decode_material_swap(env, *env.winner(key(0x800)));
  ASSERT_EQ(swap.size(), 3u);
  EXPECT_EQ(swap[0].original_material, "Props\\Box.BGSM");
  EXPECT_EQ(swap[0].replacement_material, std::optional<std::string_view>{"Props\\BoxRust.BGSM"});
  EXPECT_EQ(swap[0].color_remapping_index, std::optional<float>{0.5f});
  EXPECT_EQ(swap[1].replacement_material, std::optional<std::string_view>{"Props\\LidRust.BGSM"});
  EXPECT_FALSE(swap[1].color_remapping_index.has_value());
  EXPECT_EQ(swap[2].original_material, "Props\\Hinge.BGSM");
  EXPECT_FALSE(swap[2].replacement_material.has_value());

  EXPECT_THROW(fo4::env::decode_material_swap(env, *env.winner(key(0x801))), fo4::env::WorldDecodeError);
  EXPECT_THROW(fo4::env::decode_material_swap(env, *env.winner(key(0x802))), fo4::env::WorldDecodeError);
  EXPECT_THROW(fo4::env::decode_material_swap(env, *env.winner(key(0x810))), fo4::env::WorldDecodeError);
}

TEST_F(MaterialSwapTest, FindsReferenceAndModelSwaps) {
  const std::vector<std::filesystem::path> paths{path_};
  const auto env = fo4::env::Environment::load(paths);
  EXPECT_EQ(fo4::env::primary_model_material_swap(env, *env.winner(key(0x810))), key(0x800));
  EXPECT_FALSE(fo4::env::primary_model_material_swap(env, *env.winner(key(0x811))).has_value());
  EXPECT_FALSE(fo4::env::primary_model_material_swap(env, *env.winner(key(0x812))).has_value());
  EXPECT_EQ(fo4::env::primary_model_material_swap(env, *env.winner(key(0x820))), key(0x800));
  EXPECT_EQ(fo4::env::primary_model_material_swap(env, *env.winner(key(0x821))), key(0x801));

  EXPECT_EQ(fo4::env::placed_material_swap(env, *env.winner(key(0x830))), key(0x800));
  EXPECT_FALSE(fo4::env::placed_material_swap(env, *env.winner(key(0x831))).has_value());
  EXPECT_THROW(fo4::env::placed_material_swap(env, *env.winner(key(0x832))), fo4::env::WorldDecodeError);
  EXPECT_THROW(fo4::env::placed_material_swap(env, *env.winner(key(0x810))), fo4::env::WorldDecodeError);
}

TEST_F(MaterialSwapTest, ReadsThePrimaryModelColorRemappingIndex) {
  const std::vector<std::filesystem::path> paths{path_};
  const auto env = fo4::env::Environment::load(paths);
  EXPECT_EQ(fo4::env::primary_model_color_remapping_index(env, *env.winner(key(0x813))), std::optional<float>{0.25f});
  EXPECT_FALSE(fo4::env::primary_model_color_remapping_index(env, *env.winner(key(0x810))).has_value());
  EXPECT_FALSE(fo4::env::primary_model_color_remapping_index(env, *env.winner(key(0x815))).has_value());
  EXPECT_EQ(fo4::env::primary_model_color_remapping_index(env, *env.winner(key(0x822))), std::optional<float>{0.75f});
  EXPECT_FALSE(fo4::env::primary_model_color_remapping_index(env, *env.winner(key(0x821))).has_value());
  EXPECT_FALSE(fo4::env::primary_model_color_remapping_index(env, *env.winner(key(0x823))).has_value());
  EXPECT_EQ(fo4::env::primary_model_color_remapping_index(env, *env.winner(key(0x824))), std::optional<float>{0.125f});
  EXPECT_THROW(fo4::env::primary_model_color_remapping_index(env, *env.winner(key(0x814))), fo4::env::WorldDecodeError);
  EXPECT_EQ(fo4::env::primary_model_material_swap(env, *env.winner(key(0x816))), key(0x800));
  EXPECT_EQ(fo4::env::primary_model_color_remapping_index(env, *env.winner(key(0x816))), std::optional<float>{0.375f});
}

TEST_F(MaterialSwapTest, CApiDecodesAndFrees) {
  const auto path = path_.string();
  const char* plugins[] = {path.c_str()};
  radicle_env* env = nullptr;
  ASSERT_EQ(radicle_env_load(plugins, 1, &env), RADICLE_STATUS_OK);
  const radicle_record* record = nullptr;
  ASSERT_EQ(radicle_env_winner(env, {"Swaps.esm", 0x800}, &record), RADICLE_STATUS_OK);
  radicle_material_swap* swap = nullptr;
  ASSERT_EQ(radicle_material_swap_decode(env, record, &swap), RADICLE_STATUS_OK);
  ASSERT_EQ(swap->substitution_count, 3u);
  EXPECT_STREQ(swap->substitutions[0].original_material, "Props\\Box.BGSM");
  EXPECT_STREQ(swap->substitutions[0].replacement_material, "Props\\BoxRust.BGSM");
  EXPECT_EQ(swap->substitutions[0].has_color_remapping_index, 1);
  EXPECT_EQ(swap->substitutions[2].replacement_material, nullptr);
  radicle_material_swap_free(swap);

  radicle_form_key out{};
  ASSERT_EQ(radicle_env_winner(env, {"Swaps.esm", 0x830}, &record), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_placed_reference_material_swap(env, record, &out), RADICLE_STATUS_OK);
  EXPECT_STREQ(out.plugin, "Swaps.esm");
  EXPECT_EQ(out.local_id, 0x800u);
  ASSERT_EQ(radicle_env_winner(env, {"Swaps.esm", 0x811}, &record), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_record_primary_model_material_swap(env, record, &out), RADICLE_STATUS_OK);
  EXPECT_EQ(out.plugin, nullptr);

  float index = 0.0f;
  ASSERT_EQ(radicle_env_winner(env, {"Swaps.esm", 0x813}, &record), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_record_primary_model_color_remapping_index(env, record, &index), RADICLE_STATUS_OK);
  EXPECT_EQ(index, 0.25f);
  ASSERT_EQ(radicle_env_winner(env, {"Swaps.esm", 0x811}, &record), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_record_primary_model_color_remapping_index(env, record, &index), RADICLE_STATUS_NOT_FOUND);
  ASSERT_EQ(radicle_env_winner(env, {"Swaps.esm", 0x814}, &record), RADICLE_STATUS_OK);
  EXPECT_NE(radicle_record_primary_model_color_remapping_index(env, record, &index), RADICLE_STATUS_OK);
  radicle_env_destroy(env);
}
