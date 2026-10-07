#include "fo4/c_api_sound.h"
#include "fo4/environment/sound.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>

namespace {

using Sub = std::pair<std::string, std::vector<std::byte>>;
struct Rec { std::string signature; std::uint32_t id; std::vector<Sub> subs; std::uint16_t version{131}; };

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

class SoundTest : public testing::Test {
protected:
  void SetUp() override {
    root = std::filesystem::temp_directory_path() /
           ("radicle_sound_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    path = root / "Sound.esm";
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
      const auto rec = fo4::test::begin_record(out, record.signature, 0, record.id, record.version);
      for (const auto& [tag, bytes] : record.subs) fo4::test::write_subrecord(out, tag, bytes);
      fo4::test::end_record(out, rec); fo4::test::end_group(out, group);
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(out.bytes.data()), static_cast<std::streamsize>(out.bytes.size()));
  }
  fo4::env::Environment load() { const std::filesystem::path paths[] = {path}; return fo4::env::Environment::load(paths); }
  const fo4::env::RecordRef& record(const fo4::env::Environment& env, std::uint32_t id) {
    return *env.winner(fo4::FormKey{fo4::PluginKey::from_filename("Sound.esm"), fo4::LocalFormId{id}});
  }
  std::filesystem::path root, path;
};

TEST_F(SoundTest, StandardAndMarkerExposeEncodedParametersAndFiles) {
  const std::vector<std::byte> bnam{std::byte{0xFE}, std::byte{0xFD}, std::byte{7}, std::byte{9}, std::byte{0x34}, std::byte{0x12}};
  write({{"SNDR", 0x800, {{"CNAM", words({0x1EEF540A})}, {"BNAM", bnam}, {"LNAM", words({0x44332211})},
         {"ANAM", text("sound\\one.xwm")}, {"ANAM", text("sound\\two.wav")}, {"GNAM", words({0x810})},
         {"SNAM", words({0x811})}, {"ONAM", words({0x812})}}},
         {"SOUN", 0x801, {{"SDSC", words({0x800})}}}});
  const auto env = load();
  const auto d = fo4::env::decode_sound_descriptor(env, record(env, 0x800));
  ASSERT_TRUE(d.playback); EXPECT_EQ(d.playback->frequency_shift, -2); EXPECT_EQ(d.playback->frequency_variance, -3);
  EXPECT_EQ(d.playback->priority, 7); EXPECT_EQ(d.playback->db_variance, 9); EXPECT_EQ(d.playback->attenuation_hundredths, 0x1234);
  ASSERT_TRUE(d.controls); EXPECT_EQ((*d.controls)[1], 0x22); EXPECT_EQ((*d.controls)[3], 0x44);
  ASSERT_EQ(d.files.size(), 2); EXPECT_EQ(d.files[0], "sound\\one.xwm"); EXPECT_EQ(d.files[1], "sound\\two.wav");
  ASSERT_TRUE(d.category); EXPECT_EQ(d.category->local().value(), 0x810u);
  EXPECT_EQ(d.alternate->local().value(), 0x811u); EXPECT_EQ(d.output_model->local().value(), 0x812u);
  const auto m = fo4::env::decode_sound_marker(env, record(env, 0x801));
  EXPECT_EQ(m.descriptor->local().value(), 0x800u); EXPECT_FALSE(m.repeat);
}

TEST_F(SoundTest, CompoundAndAutoWeaponKeepTheirDistinctLinksAndRateFiles) {
  write({{"SNDR", 0x800, {{"CNAM", words({0x54651A43})}, {"DNAM", words({0x810})}, {"DNAM", words({0x811})}}},
         {"SNDR", 0x801, {{"CNAM", words({0xED157AE3})}, {"BNAM", words({0x812})}, {"ITMC", words({1})},
         {"ITMS", {}}, {"INTV", words({750})}, {"FNAM", text("sound\\auto.xwm")}, {"ITME", {}}}}});
  const auto env = load(); const auto compound = fo4::env::decode_sound_descriptor(env, record(env, 0x800));
  ASSERT_EQ(compound.descriptors.size(), 2); EXPECT_EQ(compound.descriptors[1].local().value(), 0x811u); EXPECT_FALSE(compound.playback);
  const auto weapon = fo4::env::decode_sound_descriptor(env, record(env, 0x801));
  EXPECT_EQ(weapon.base_descriptor->local().value(), 0x812u); EXPECT_FALSE(weapon.playback);
  ASSERT_EQ(weapon.rate_files.size(), 1); EXPECT_EQ(weapon.rate_files[0].rpm, 750u); EXPECT_EQ(weapon.rate_files[0].file, "sound\\auto.xwm");
}

TEST_F(SoundTest, LastControlsAndRepeatStackableFollowPayloadSizeNotFormVersion) {
  fo4::test::ByteWriter repeat; repeat.f32(2); repeat.f32(8); repeat.u8(0x80);
  for (const auto version : {112u, 113u}) {
    write({{"SNDR", 0x800, {{"CNAM", words({0x1EEF540A})}, {"LNAM", words({0})}, {"LNAM", words({0x44332211})}}},
           {"SOUN", 0x801, {{"REPT", repeat.bytes}}, static_cast<std::uint16_t>(version)}});
    const auto env = load(); const auto d = fo4::env::decode_sound_descriptor(env, record(env, 0x800));
    ASSERT_TRUE(d.controls); EXPECT_EQ((*d.controls)[3], 0x44);
    const auto m = fo4::env::decode_sound_marker(env, record(env, 0x801)); ASSERT_TRUE(m.repeat);
    ASSERT_TRUE(m.repeat->stackable.has_value()); EXPECT_EQ(*m.repeat->stackable, 0x80);
    EXPECT_EQ(record(env, 0x801).record->subrecords[0].data, repeat.bytes);
  }
}

TEST_F(SoundTest, MissingKindUsesTheValuesLayoutAndExposesNoKind) {
  const std::vector<std::byte> bnam{std::byte{0xF6}, std::byte{0x05}, std::byte{0xC8}, std::byte{3}, std::byte{0x10}, std::byte{0x27}};
  write({{"SNDR", 0x800, {{"EDID", text("NoKind")}, {"BNAM", bnam}}}, {"SNDR", 0x801, {{"EDID", text("Bare")}}}});
  const auto env = load();
  const auto d = fo4::env::decode_sound_descriptor(env, record(env, 0x800));
  EXPECT_FALSE(d.kind.has_value());
  ASSERT_TRUE(d.playback); EXPECT_EQ(d.playback->frequency_shift, -10); EXPECT_EQ(d.playback->frequency_variance, 5);
  EXPECT_EQ(d.playback->priority, 200); EXPECT_EQ(d.playback->attenuation_hundredths, 10000);
  EXPECT_FALSE(d.base_descriptor);
  const auto bare = fo4::env::decode_sound_descriptor(env, record(env, 0x801));
  EXPECT_FALSE(bare.kind.has_value()); EXPECT_FALSE(bare.playback);
  const radicle_sound_descriptor* view = nullptr;
  const auto name = path.string(); const char* names[] = {name.c_str()}; radicle_env* abi = nullptr;
  ASSERT_EQ(radicle_env_load(names, 1, &abi), RADICLE_STATUS_OK);
  const radicle_record* handle = nullptr;
  ASSERT_EQ(radicle_env_winner(abi, radicle_form_key{"Sound.esm", 0x800}, &handle), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_sound_descriptor_decode(abi, handle, &view), RADICLE_STATUS_OK);
  EXPECT_EQ(view->has_kind, 0); EXPECT_EQ(view->has_playback, 1); EXPECT_EQ(view->playback.frequency_shift, -10);
  radicle_sound_descriptor_free(view); radicle_env_destroy(abi);
}

TEST_F(SoundTest, RateGroupFilenameIsNeverReadAsTopLevelFlags) {
  write({{"SNDR", 0x800, {{"CNAM", words({0x1EEF540A})}, {"BNAM", std::vector<std::byte>(6)}, {"ITMC", words({1})},
         {"ITMS", {}}, {"INTV", words({600})}, {"FNAM", text("abcd")}, {"ITME", {}}}},
         {"SNDR", 0x801, {{"CNAM", words({0x1EEF540A})}, {"FNAM", words({0x06000010})}}}});
  const auto env = load();
  const auto d = fo4::env::decode_sound_descriptor(env, record(env, 0x800));
  ASSERT_EQ(d.rate_files.size(), 1u); EXPECT_EQ(d.rate_files[0].rpm, 600u); EXPECT_EQ(d.rate_files[0].file, "abcd");
  EXPECT_THROW(fo4::env::decode_sound_descriptor(env, record(env, 0x801)), fo4::env::WorldDecodeError);
}

TEST_F(SoundTest, UnknownKindsSizesAndIncompleteRateGroupsFailClosed) {
  for (const auto& subs : std::vector<std::vector<Sub>>{
       {{"CNAM", words({0x1234})}}, {{"CNAM", words({0x1EEF540A})}, {"BNAM", words({1})}},
       {{"CNAM", words({0x1EEF540A})}, {"CNAM", words({0x1EEF540A})}},
       {{"CNAM", words({0xED157AE3})}, {"ITMC", words({1})}, {"ITMS", {}}},
       {{"CNAM", words({0xED157AE3})}, {"ITMC", words({0})}, {"ITMS", {}}, {"INTV", words({10})},
        {"FNAM", text("a.wav")}, {"ITME", {}}}}) {
    write({{"SNDR", 0x800, subs}}); const auto env = load();
    EXPECT_THROW(fo4::env::decode_sound_descriptor(env, record(env, 0x800)), fo4::env::WorldDecodeError);
  }
}

TEST_F(SoundTest, RepeatOptionalTailAndFootstepCategoriesPreservePresenceAndOrder) {
  fo4::test::ByteWriter times; times.f32(2.5f); times.f32(8.0f);
  for (const bool tail : {false, true}) {
    auto bytes = times.bytes; if (tail) bytes.push_back(std::byte{0x80});
    write({{"SOUN", 0x800, {{"REPT", bytes}}}, {"FSTS", 0x801,
           {{"XCNT", words({1, 2, 0, 1, 1})}, {"DATA", words({0x815, 0x814, 0x812, 0x813, 0x811})}}}});
    const auto env = load(); const auto m = fo4::env::decode_sound_marker(env, record(env, 0x800));
    ASSERT_TRUE(m.repeat); EXPECT_EQ(m.repeat->min_time, 2.5f); EXPECT_EQ(m.repeat->max_time, 8.0f);
    EXPECT_EQ(m.repeat->stackable.has_value(), tail);
    if (tail) { EXPECT_EQ(*m.repeat->stackable, 0x80); }
    const auto f = fo4::env::decode_footstep_set(env, record(env, 0x801));
    EXPECT_EQ(f.categories[0][0].local().value(), 0x811u); ASSERT_EQ(f.categories[1].size(), 2);
    EXPECT_EQ(f.categories[1][1].local().value(), 0x813u); EXPECT_TRUE(f.categories[2].empty());
    EXPECT_EQ(f.categories[3][0].local().value(), 0x814u); EXPECT_EQ(f.categories[4][0].local().value(), 0x815u);
  }
  write({{"FSTS", 0x800, {{"XCNT", words({0xFFFFFFFF, 0, 0, 0, 0})}, {"DATA", {}}}}}); const auto env = load();
  EXPECT_THROW(fo4::env::decode_footstep_set(env, record(env, 0x800)), fo4::env::WorldDecodeError);
}

}
