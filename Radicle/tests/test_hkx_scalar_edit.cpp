#include "fo4/c_api_hkx.h"
#include "fo4/hkx/behavior_graph.hpp"
#include "fo4/hkx/packfile.hpp"
#include "fo4/hkx/layout.hpp"
#include "fo4/hkx/scalar_edit.hpp"
#include "hkx_test_fixture.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

using fo4::hkx::HkxGraphModel;
using fo4::hkx::HkxPackfile;
using fo4::hkx::HkxScalarKind;
using fo4::hkx::HkxScalarMember;
using fo4::hkx::HkxScalarStatus;
using fo4::hkx::HkxStatus;
using fo4::test::hkx::TestSection;
using fo4::test::hkx::add_class_name;
using fo4::test::hkx::build_packfile;
using fo4::test::hkx::link;
using fo4::test::hkx::member_at;
using fo4::test::hkx::patch_u32;
using fo4::test::hkx::single_object_packfile;

namespace {

constexpr const char* kLocomotion = "Character/Behaviors/Locomotion_8wayBlend.hkx";

std::filesystem::path repository_root() {
  std::error_code ec;
  std::filesystem::path dir = std::filesystem::current_path(ec);
  for (int level = 0; level < 8 && !dir.empty(); ++level) {
    if (std::filesystem::is_directory(dir / "Radicle" / "tests" / "fixtures" / "hkx", ec)) return dir;
    if (dir == dir.root_path()) break;
    dir = dir.parent_path();
  }
  return {};
}

std::vector<std::byte> read_binary_file(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  std::vector<std::byte> bytes;
  char buffer[4096];
  while (stream.read(buffer, static_cast<std::streamsize>(sizeof buffer)) || stream.gcount() > 0) {
    for (std::streamsize i = 0; i < stream.gcount(); ++i) {
      bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(buffer[i])));
    }
  }
  return bytes;
}

// Constraint: CI restores the committed HKX fixture, so a missing file there fails instead of skipping; the caller returns when bytes stays empty.
void load_fixture(const char* under_meshes, std::vector<std::byte>& bytes) {
  const auto root = repository_root();
  const auto file = root / "Radicle" / "tests" / "fixtures" / "hkx" / "Meshes" / "Actors" / under_meshes;
  if (root.empty() || !std::filesystem::exists(file)) {
    if (std::getenv("CI") != nullptr) {
      ADD_FAILURE() << "CI is missing the committed " << file.string();
      return;
    }
    GTEST_SKIP() << "no committed " << file.string();
  }
  bytes = read_binary_file(file);
}

void read_model(const std::vector<std::byte>& bytes, HkxPackfile& packfile, HkxGraphModel& model) {
  ASSERT_EQ(fo4::hkx::read_packfile(bytes, packfile), HkxStatus::Ok);
  ASSERT_EQ(fo4::hkx::read_behavior_model(packfile, model), HkxStatus::Ok);
}

std::vector<HkxScalarMember> list_of(const std::vector<std::byte>& bytes, std::size_t object) {
  std::vector<HkxScalarMember> rows;
  std::string why;
  EXPECT_EQ(fo4::hkx::list_scalar_members(bytes, object, rows, why), HkxScalarStatus::Ok) << why;
  return rows;
}

const HkxScalarMember* find(const std::vector<HkxScalarMember>& rows, std::string_view name) {
  for (const HkxScalarMember& row : rows) {
    if (row.name == name) return &row;
  }
  return nullptr;
}

std::vector<std::byte> write_ok(const std::vector<std::byte>& bytes, std::size_t object, std::string_view member,
                                std::string_view value) {
  std::vector<std::byte> out;
  std::string why;
  EXPECT_EQ(fo4::hkx::set_scalar_member(bytes, object, member, value, out, why), HkxScalarStatus::Ok) << why;
  return out;
}

HkxScalarStatus write_status(const std::vector<std::byte>& bytes, std::size_t object, std::string_view member,
                             std::string_view value) {
  std::vector<std::byte> out;
  std::string why;
  const HkxScalarStatus status = fo4::hkx::set_scalar_member(bytes, object, member, value, out, why);
  if (status != HkxScalarStatus::Ok) {
    EXPECT_TRUE(out.empty());
    EXPECT_FALSE(why.empty());
  }
  return status;
}

std::size_t first_clip_object(const HkxGraphModel& model) { return model.nodes[model.clips.at(0).node].object; }

}  // namespace

TEST(HkxScalarEdit, ClipMembersListWhatTheGraphReaderDecodes) {
  std::vector<std::byte> bytes;
  load_fixture(kLocomotion, bytes);
  if (bytes.empty()) return;
  HkxPackfile packfile;
  HkxGraphModel model;
  read_model(bytes, packfile, model);
  const auto rows = list_of(bytes, first_clip_object(model));

  const HkxScalarMember* speed = find(rows, "playbackSpeed");
  ASSERT_NE(speed, nullptr);
  EXPECT_EQ(speed->kind, HkxScalarKind::Real);
  EXPECT_EQ(speed->offset, 176U);
  EXPECT_EQ(speed->size, 4U);
  EXPECT_FLOAT_EQ(std::stof(speed->value), model.clips[0].playback_speed);

  const HkxScalarMember* mode = find(rows, "mode");
  ASSERT_NE(mode, nullptr);
  EXPECT_EQ(mode->kind, HkxScalarKind::Int8);
  EXPECT_TRUE(mode->is_enum);
  EXPECT_EQ(mode->value, std::to_string(model.clips[0].mode));
  EXPECT_EQ(mode->enum_name, "MODE_LOOPING");

  EXPECT_EQ(find(rows, "animationName"), nullptr);
  EXPECT_EQ(find(rows, "triggers"), nullptr);
  EXPECT_EQ(find(rows, "extractedMotion"), nullptr);
}

TEST(HkxScalarEdit, SettingARealChangesOnlyItsFourBytesAndTheGraphReaderSeesIt) {
  std::vector<std::byte> bytes;
  load_fixture(kLocomotion, bytes);
  if (bytes.empty()) return;
  HkxPackfile packfile;
  HkxGraphModel model;
  read_model(bytes, packfile, model);
  ASSERT_NE(model.clips[0].playback_speed, 2.5f);
  const std::size_t object = first_clip_object(model);

  const auto written = write_ok(bytes, object, "playbackSpeed", "2.5");
  ASSERT_EQ(written.size(), bytes.size());
  const std::size_t begin = packfile.sections[packfile.data_section].file_offset + packfile.objects[object].offset + 176;
  std::size_t changed = 0;
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    if (bytes[i] == written[i]) continue;
    EXPECT_GE(i, begin);
    EXPECT_LT(i, begin + 4);
    ++changed;
  }
  EXPECT_GT(changed, 0U);

  HkxPackfile again;
  HkxGraphModel after;
  read_model(written, again, after);
  ASSERT_EQ(after.clips.size(), model.clips.size());
  EXPECT_FLOAT_EQ(after.clips[0].playback_speed, 2.5f);
  for (std::size_t i = 1; i < model.clips.size(); ++i) {
    EXPECT_FLOAT_EQ(after.clips[i].playback_speed, model.clips[i].playback_speed);
    EXPECT_EQ(after.clips[i].animation, model.clips[i].animation);
  }
  EXPECT_EQ(after.clips[0].animation, model.clips[0].animation);
  ASSERT_EQ(after.nodes.size(), model.nodes.size());
  for (std::size_t i = 0; i < model.nodes.size(); ++i) EXPECT_EQ(after.nodes[i].name, model.nodes[i].name);
}

TEST(HkxScalarEdit, EnumBoolAndFlagsMembersTakeNamesAndNumbers) {
  std::vector<std::byte> bytes;
  load_fixture(kLocomotion, bytes);
  if (bytes.empty()) return;
  HkxPackfile packfile;
  HkxGraphModel model;
  read_model(bytes, packfile, model);

  const std::size_t clip = first_clip_object(model);
  HkxPackfile again;
  HkxGraphModel after;
  read_model(write_ok(bytes, clip, "mode", "MODE_SINGLE_PLAY"), again, after);
  EXPECT_EQ(after.clips[0].mode, 0);
  const auto numeric = write_ok(bytes, clip, "mode", "3");
  HkxPackfile third;
  HkxGraphModel last;
  read_model(numeric, third, last);
  EXPECT_EQ(last.clips[0].mode, 3);
  EXPECT_EQ(find(list_of(numeric, clip), "mode")->enum_name, "MODE_PING_PONG");

  ASSERT_FALSE(model.blenders.empty());
  const bool subtract = model.blenders[0].subtract_last_child;
  const std::size_t blender = model.nodes[model.blenders[0].node].object;
  const auto blender_rows = list_of(bytes, blender);
  const HkxScalarMember* flag = find(blender_rows, "subtractLastChild");
  ASSERT_NE(flag, nullptr);
  EXPECT_EQ(flag->kind, HkxScalarKind::Bool);
  EXPECT_EQ(flag->value, subtract ? "true" : "false");
  HkxPackfile flipped_file;
  HkxGraphModel flipped;
  read_model(write_ok(bytes, blender, "subtractLastChild", subtract ? "0" : "true"), flipped_file, flipped);
  EXPECT_EQ(flipped.blenders[0].subtract_last_child, !subtract);
  EXPECT_EQ(flipped.blenders[0].parametric, model.blenders[0].parametric);
  EXPECT_EQ(flipped.blenders[0].flags, model.blenders[0].flags);

  ASSERT_FALSE(model.effects.empty());
  ASSERT_NE(model.effects[0].flags, 3);
  const std::size_t effect = model.nodes[model.effects[0].node].object;
  const auto effect_rows = list_of(bytes, effect);
  const HkxScalarMember* flags = find(effect_rows, "flags");
  ASSERT_NE(flags, nullptr);
  EXPECT_TRUE(flags->is_flags);
  EXPECT_EQ(flags->kind, HkxScalarKind::Uint16);
  EXPECT_EQ(flags->value, std::to_string(model.effects[0].flags));
  HkxPackfile effect_file;
  HkxGraphModel changed;
  read_model(write_ok(bytes, effect, "flags", "3"), effect_file, changed);
  EXPECT_EQ(changed.effects[0].flags, 3);
  EXPECT_FLOAT_EQ(changed.effects[0].duration, model.effects[0].duration);
}

TEST(HkxScalarEdit, ARequestThatIsNotASafeScalarWriteIsRefused) {
  std::vector<std::byte> bytes;
  load_fixture(kLocomotion, bytes);
  if (bytes.empty()) return;
  HkxPackfile packfile;
  HkxGraphModel model;
  read_model(bytes, packfile, model);
  const std::size_t clip = first_clip_object(model);

  EXPECT_EQ(write_status(bytes, clip, "noSuchMember", "1"), HkxScalarStatus::NoMember);
  EXPECT_EQ(write_status(bytes, clip, "animationName", "x"), HkxScalarStatus::NotScalar);
  EXPECT_EQ(write_status(bytes, clip, "triggers", "0"), HkxScalarStatus::NotScalar);
  EXPECT_EQ(write_status(bytes, clip, "extractedMotion", "0"), HkxScalarStatus::NotScalar);

  EXPECT_EQ(write_status(bytes, clip, "playbackSpeed", "abc"), HkxScalarStatus::BadValue);
  EXPECT_EQ(write_status(bytes, clip, "playbackSpeed", "nan"), HkxScalarStatus::BadValue);
  EXPECT_EQ(write_status(bytes, clip, "playbackSpeed", ""), HkxScalarStatus::BadValue);
  EXPECT_EQ(write_status(bytes, clip, "playbackSpeed", "3.5e38"), HkxScalarStatus::OutOfRange);
  EXPECT_EQ(write_status(bytes, clip, "playbackSpeed", "1e999"), HkxScalarStatus::OutOfRange);

  EXPECT_EQ(write_status(bytes, clip, "mode", "MODE_BOGUS"), HkxScalarStatus::BadValue);
  EXPECT_EQ(write_status(bytes, clip, "mode", "9"), HkxScalarStatus::BadValue);
  EXPECT_EQ(write_status(bytes, clip, "mode", "128"), HkxScalarStatus::OutOfRange);
  EXPECT_EQ(write_status(bytes, clip, "mode", "-129"), HkxScalarStatus::OutOfRange);

  ASSERT_FALSE(model.blenders.empty());
  const std::size_t blender = model.nodes[model.blenders[0].node].object;
  EXPECT_EQ(write_status(bytes, blender, "subtractLastChild", "2"), HkxScalarStatus::BadValue);
  EXPECT_EQ(write_status(bytes, blender, "subtractLastChild", "yes"), HkxScalarStatus::BadValue);

  ASSERT_FALSE(model.effects.empty());
  const std::size_t effect = model.nodes[model.effects[0].node].object;
  EXPECT_EQ(write_status(bytes, effect, "flags", "FLAG_X"), HkxScalarStatus::BadValue);
  EXPECT_EQ(write_status(bytes, effect, "flags", "65536"), HkxScalarStatus::OutOfRange);
  EXPECT_EQ(write_status(bytes, effect, "flags", "-1"), HkxScalarStatus::OutOfRange);

  EXPECT_EQ(write_status(bytes, packfile.objects.size(), "mode", "1"), HkxScalarStatus::NoObject);
  const std::vector<std::byte> truncated(bytes.begin(), bytes.begin() + 16);
  EXPECT_EQ(write_status(truncated, 0, "mode", "1"), HkxScalarStatus::BadPackfile);
  EXPECT_EQ(write_status({}, 0, "mode", "1"), HkxScalarStatus::BadPackfile);
}

TEST(HkxScalarEdit, WritingEveryListedValueBackLeavesEveryFixtureByteIdentical) {
  const char* const files[] = {
      "Character/Behaviors/Locomotion_8wayBlend.hkx", "Character/Behaviors/RaiderRootBehavior.hkx",
      "Character/Behaviors/SingleAnimFurniture.hkx",  "Character/Behaviors/SyncedAnimBehavior.hkx",
      "Character/Behaviors/WeaponBehavior.hkx",
  };
  for (const char* file : files) {
    std::vector<std::byte> bytes;
    load_fixture(file, bytes);
    if (bytes.empty()) continue;
    HkxPackfile packfile;
    ASSERT_EQ(fo4::hkx::read_packfile(bytes, packfile), HkxStatus::Ok) << file;
    std::size_t written = 0;
    for (std::size_t object = 0; object < packfile.objects.size() && written < 200; ++object) {
      std::vector<HkxScalarMember> rows;
      std::string why;
      if (fo4::hkx::list_scalar_members(bytes, object, rows, why) != HkxScalarStatus::Ok) continue;
      for (const HkxScalarMember& row : rows) {
        if (row.value.find('n') != std::string::npos) continue;
        std::vector<std::byte> out;
        ASSERT_EQ(fo4::hkx::set_scalar_member(bytes, object, row.name, row.value, out, why), HkxScalarStatus::Ok)
            << file << " object " << object << " " << row.name << "=" << row.value << ": " << why;
        EXPECT_TRUE(out == bytes) << file << " object " << object << " " << row.name;
        ++written;
      }
    }
    EXPECT_GT(written, 0U) << file;
  }
}

namespace {

struct ListDeleter {
  void operator()(radicle_hkx_scalar_list* list) const { radicle_hkx_scalar_list_destroy(list); }
};
using ListHandle = std::unique_ptr<radicle_hkx_scalar_list, ListDeleter>;

std::string take_text(uint8_t* bytes, size_t size) {
  if (!bytes) return {};
  std::string text(reinterpret_cast<const char*>(bytes), size);
  radicle_free_bytes(bytes);
  return text;
}

std::string text_of(const radicle_hkx_scalar_list* list, size_t index, int32_t which) {
  uint8_t* bytes = nullptr;
  size_t size = 0;
  EXPECT_EQ(radicle_hkx_scalar_list_text(list, index, which, &bytes, &size), RADICLE_STATUS_OK);
  return take_text(bytes, size);
}

}  // namespace

TEST(CApiHkxScalar, ListAndSetWorkThroughTheCInterface) {
  std::vector<std::byte> bytes;
  load_fixture(kLocomotion, bytes);
  if (bytes.empty()) return;
  HkxPackfile packfile;
  HkxGraphModel model;
  read_model(bytes, packfile, model);
  const std::size_t object = first_clip_object(model);
  const auto* raw = reinterpret_cast<const uint8_t*>(bytes.data());

  radicle_hkx_scalar_list* opened = nullptr;
  ASSERT_EQ(radicle_hkx_scalar_list_read(raw, bytes.size(), object, &opened), RADICLE_STATUS_OK);
  const ListHandle list(opened);
  size_t count = 0;
  ASSERT_EQ(radicle_hkx_scalar_list_count(list.get(), &count), RADICLE_STATUS_OK);
  ASSERT_GT(count, 0U);
  size_t speed = count;
  for (size_t i = 0; i < count; ++i) {
    if (text_of(list.get(), i, 0) == "playbackSpeed") speed = i;
  }
  ASSERT_LT(speed, count);
  radicle_hkx_scalar_info info{};
  ASSERT_EQ(radicle_hkx_scalar_list_read_info(list.get(), speed, &info), RADICLE_STATUS_OK);
  EXPECT_EQ(info.kind, 7);
  EXPECT_EQ(info.is_enum, 0);
  EXPECT_EQ(info.offset, 176U);
  EXPECT_EQ(info.size, 4U);
  EXPECT_EQ(text_of(list.get(), speed, 1), "real");
  EXPECT_FLOAT_EQ(std::stof(text_of(list.get(), speed, 2)), model.clips[0].playback_speed);
  EXPECT_EQ(text_of(list.get(), speed, 3), "");

  uint8_t* out = nullptr;
  size_t out_size = 0;
  ASSERT_EQ(radicle_hkx_set_scalar(raw, bytes.size(), object, "playbackSpeed", "2.5", &out, &out_size),
            RADICLE_STATUS_OK);
  ASSERT_EQ(out_size, bytes.size());
  const std::vector<std::byte> expected = write_ok(bytes, object, "playbackSpeed", "2.5");
  EXPECT_EQ(std::memcmp(out, expected.data(), out_size), 0);
  radicle_free_bytes(out);

  out = nullptr;
  EXPECT_EQ(radicle_hkx_set_scalar(raw, bytes.size(), object, "noSuchMember", "1", &out, &out_size),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(out, nullptr);
  EXPECT_NE(std::string(radicle_last_error()), "");
  EXPECT_EQ(radicle_hkx_set_scalar(raw, bytes.size(), object, "animationName", "x", &out, &out_size),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_hkx_set_scalar(raw, bytes.size(), object, "mode", "9", &out, &out_size),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_hkx_set_scalar(raw, bytes.size(), packfile.objects.size(), "mode", "1", &out, &out_size),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_hkx_set_scalar(raw, 16, 0, "mode", "1", &out, &out_size), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(radicle_hkx_set_scalar(raw, bytes.size(), object, nullptr, "1", &out, &out_size),
            RADICLE_STATUS_INVALID_ARGUMENT);

  ASSERT_NE(info.size, 0U);
  EXPECT_EQ(radicle_hkx_scalar_list_read_info(list.get(), count, &info), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(info.size, 0U);
  EXPECT_EQ(info.offset, 0U);
  uint8_t* text = nullptr;
  size_t text_size = 0;
  EXPECT_EQ(radicle_hkx_scalar_list_text(list.get(), speed, 4, &text, &text_size), RADICLE_STATUS_INVALID_ARGUMENT);
  text_size = 7;
  EXPECT_EQ(radicle_hkx_scalar_list_text(list.get(), count, 0, &text, &text_size), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(text, nullptr);
  EXPECT_EQ(text_size, 0U);
  size_t kept = 5;
  EXPECT_EQ(radicle_hkx_scalar_list_count(nullptr, &kept), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(kept, 0U);
  radicle_hkx_scalar_list* none = nullptr;
  EXPECT_EQ(radicle_hkx_scalar_list_read(raw, bytes.size(), packfile.objects.size(), &none), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(none, nullptr);
}

namespace {

constexpr const char* kSimple = "hkbCustomTestGeneratorSimpleTypes";

std::vector<std::byte> zero_object(const char* class_name, std::vector<std::uint32_t> local = {}, std::size_t size = 0) {
  const auto* layout = fo4::hkx::find_layout(class_name);
  return single_object_packfile(class_name, layout->signature,
                                std::vector<std::byte>(size == 0 ? layout->size : size, std::byte{0}), std::move(local));
}

std::size_t data_start(const std::vector<std::byte>& bytes) {
  HkxPackfile packfile;
  EXPECT_EQ(fo4::hkx::read_packfile(bytes, packfile), HkxStatus::Ok);
  return packfile.sections[packfile.data_section].file_offset;
}

void expect_write(const std::vector<std::byte>& bytes, const char* member, std::string_view value,
                  std::initializer_list<unsigned> expected) {
  const auto out = write_ok(bytes, 0, member, value);
  auto want = bytes;
  std::size_t at = data_start(bytes) + member_at(kSimple, member);
  for (const unsigned byte : expected) want[at++] = static_cast<std::byte>(byte);
  EXPECT_TRUE(out == want) << member << " = " << value;
}

std::string refusal(const std::vector<std::byte>& bytes, std::size_t object, std::string_view member,
                    std::string_view value, HkxScalarStatus expected) {
  std::vector<std::byte> out;
  std::string why;
  EXPECT_EQ(fo4::hkx::set_scalar_member(bytes, object, member, value, out, why), expected) << member << " = " << value;
  EXPECT_TRUE(out.empty());
  return why;
}

}  // namespace

TEST(HkxScalarEditSynthetic, EveryIntegerWidthWritesExactlyItsOwnLittleEndianBytes) {
  const auto bytes = zero_object(kSimple);
  expect_write(bytes, "simpleTypeHkInt8", "-128", {0x80});
  expect_write(bytes, "simpleTypeHkInt8", "127", {0x7F});
  expect_write(bytes, "simpleTypeHkInt8", "+5", {0x05});
  expect_write(bytes, "simpleTypeHkUint8", "255", {0xFF});
  expect_write(bytes, "simpleTypeHkInt16", "-2", {0xFE, 0xFF});
  expect_write(bytes, "simpleTypeHkInt16", "32767", {0xFF, 0x7F});
  expect_write(bytes, "simpleTypeHkUint16", "65535", {0xFF, 0xFF});
  expect_write(bytes, "simpleTypeHkUint16", "258", {0x02, 0x01});
  expect_write(bytes, "simpleTypeHkInt32", "-2", {0xFE, 0xFF, 0xFF, 0xFF});
  expect_write(bytes, "simpleTypeHkInt32", "-2147483648", {0x00, 0x00, 0x00, 0x80});
  expect_write(bytes, "simpleTypeHkInt32", "2147483647", {0xFF, 0xFF, 0xFF, 0x7F});
  expect_write(bytes, "simpleTypeHkUint32", "4294967295", {0xFF, 0xFF, 0xFF, 0xFF});
  expect_write(bytes, "simpleTypeHkUint32", "16909060", {0x04, 0x03, 0x02, 0x01});

  refusal(bytes, 0, "simpleTypeHkInt8", "128", HkxScalarStatus::OutOfRange);
  refusal(bytes, 0, "simpleTypeHkInt8", "-129", HkxScalarStatus::OutOfRange);
  refusal(bytes, 0, "simpleTypeHkUint8", "256", HkxScalarStatus::OutOfRange);
  refusal(bytes, 0, "simpleTypeHkUint8", "-1", HkxScalarStatus::OutOfRange);
  refusal(bytes, 0, "simpleTypeHkInt16", "32768", HkxScalarStatus::OutOfRange);
  refusal(bytes, 0, "simpleTypeHkUint16", "65536", HkxScalarStatus::OutOfRange);
  refusal(bytes, 0, "simpleTypeHkInt32", "2147483648", HkxScalarStatus::OutOfRange);
  refusal(bytes, 0, "simpleTypeHkUint32", "4294967296", HkxScalarStatus::OutOfRange);
  refusal(bytes, 0, "simpleTypeHkUint32", "-1", HkxScalarStatus::OutOfRange);
  refusal(bytes, 0, "simpleTypeHkUint32", "99999999999999999999", HkxScalarStatus::OutOfRange);
  refusal(bytes, 0, "simpleTypeHkInt16", "5x", HkxScalarStatus::BadValue);
  refusal(bytes, 0, "simpleTypeHkInt16", "0x10", HkxScalarStatus::BadValue);
  refusal(bytes, 0, "simpleTypeHkInt16", "", HkxScalarStatus::BadValue);

  const auto rows = list_of(bytes, 0);
  const std::pair<const char*, std::uint32_t> widths[] = {
      {"simpleTypeHkInt8", 1},   {"simpleTypeHkUint8", 1},  {"simpleTypeHkInt16", 2}, {"simpleTypeHkUint16", 2},
      {"simpleTypeHkInt32", 4},  {"simpleTypeHkUint32", 4}, {"simpleTypeHkReal", 4},  {"simpleTypeBool", 1},
  };
  for (const auto& [name, width] : widths) {
    const HkxScalarMember* row = find(rows, name);
    ASSERT_NE(row, nullptr) << name;
    EXPECT_EQ(row->size, width) << name;
    EXPECT_EQ(row->offset, member_at(kSimple, name)) << name;
  }
  EXPECT_EQ(find(rows, "simpleTypeHkInt64"), nullptr);
  EXPECT_EQ(find(rows, "simpleTypeCString"), nullptr);
}

TEST(HkxScalarEditSynthetic, RealsAtTheFloatBoundariesWriteAndReadBackByteIdentical) {
  auto bytes = zero_object(kSimple);
  const std::size_t at = data_start(bytes) + member_at(kSimple, "simpleTypeHkReal");
  const std::uint32_t patterns[] = {0x7F7FFFFFu, 0xFF7FFFFFu, 0x00000001u, 0x007FFFFFu,
                                    0x00800000u, 0x80000000u, 0x3F800000u};
  for (const std::uint32_t bits : patterns) {
    patch_u32(bytes, at, bits);
    const auto rows = list_of(bytes, 0);
    const HkxScalarMember* row = find(rows, "simpleTypeHkReal");
    ASSERT_NE(row, nullptr);
    const auto out = write_ok(bytes, 0, "simpleTypeHkReal", row->value);
    EXPECT_TRUE(out == bytes) << std::hex << bits << " listed as " << row->value;
  }
  patch_u32(bytes, at, 0x7F7FFFFFu);
  EXPECT_EQ(find(list_of(bytes, 0), "simpleTypeHkReal")->value, "3.40282347e+38");

  const auto zeros = zero_object(kSimple);
  expect_write(zeros, "simpleTypeHkReal", "3.40282347e+38", {0xFF, 0xFF, 0x7F, 0x7F});
  expect_write(zeros, "simpleTypeHkReal", "3.4028234663852886e38", {0xFF, 0xFF, 0x7F, 0x7F});
  expect_write(zeros, "simpleTypeHkReal", "-3.40282347e+38", {0xFF, 0xFF, 0x7F, 0xFF});
  expect_write(zeros, "simpleTypeHkReal", "+1.5", {0x00, 0x00, 0xC0, 0x3F});
  expect_write(zeros, "simpleTypeHkReal", "-0", {0x00, 0x00, 0x00, 0x80});
  auto nonzero = zeros;
  patch_u32(nonzero, at, 0x3F800000u);
  ASSERT_FALSE(nonzero == zeros);
  EXPECT_TRUE(write_ok(nonzero, 0, "simpleTypeHkReal", "0e-999") == zeros);
  EXPECT_TRUE(write_ok(nonzero, 0, "simpleTypeHkReal", "0") == zeros);

  refusal(zeros, 0, "simpleTypeHkReal", "3.4028236e38", HkxScalarStatus::OutOfRange);
  refusal(zeros, 0, "simpleTypeHkReal", "-3.4028236e38", HkxScalarStatus::OutOfRange);
  refusal(zeros, 0, "simpleTypeHkReal", "1e999", HkxScalarStatus::OutOfRange);
  refusal(zeros, 0, "simpleTypeHkReal", "1e-999", HkxScalarStatus::OutOfRange);
  refusal(zeros, 0, "simpleTypeHkReal", "-1e-999", HkxScalarStatus::OutOfRange);
  for (const char* bad : {"1e5e5", "1.2.3", "--1", "+-1", "e5", "1e", ".", "inf", "nan", "0x1p3", " 1", "1 "}) {
    refusal(zeros, 0, "simpleTypeHkReal", bad, HkxScalarStatus::BadValue);
  }

  for (const std::uint32_t bits : {0x7F800000u, 0xFF800000u, 0x7FC00000u}) {
    patch_u32(bytes, at, bits);
    const auto rows = list_of(bytes, 0);
    const HkxScalarMember* row = find(rows, "simpleTypeHkReal");
    ASSERT_NE(row, nullptr);
    refusal(bytes, 0, "simpleTypeHkReal", row->value, HkxScalarStatus::BadValue);
  }
}

TEST(HkxScalarEditSynthetic, AMemberUnderAPointerFixupIsRefusedAndItsNeighboursAreNot) {
  const std::size_t speed = member_at("hkbClipGenerator", "playbackSpeed");
  const auto rows_of = [&](std::size_t fixup_source) {
    std::vector<std::uint32_t> local;
    link(local, fixup_source, 0);
    return list_of(zero_object("hkbClipGenerator", local), 0);
  };

  const auto before = rows_of(speed - 8);
  EXPECT_NE(find(before, "cropStartAmountLocalTime"), nullptr);
  EXPECT_EQ(find(before, "cropEndAmountLocalTime"), nullptr);
  EXPECT_EQ(find(before, "startTime"), nullptr);
  EXPECT_NE(find(before, "playbackSpeed"), nullptr);

  const auto on = rows_of(speed);
  EXPECT_EQ(find(on, "playbackSpeed"), nullptr);
  EXPECT_EQ(find(on, "enforcedDuration"), nullptr);
  EXPECT_NE(find(on, "startTime"), nullptr);
  EXPECT_NE(find(on, "animationBindingIndex"), nullptr);
  EXPECT_NE(find(on, "mode"), nullptr);

  std::vector<std::uint32_t> local;
  link(local, speed, 0);
  const auto covered = zero_object("hkbClipGenerator", local);
  EXPECT_NE(refusal(covered, 0, "playbackSpeed", "2", HkxScalarStatus::NotScalar).find("fixup"), std::string::npos);
  refusal(covered, 0, "enforcedDuration", "2", HkxScalarStatus::NotScalar);
  write_ok(covered, 0, "startTime", "2");
}

TEST(HkxScalarEditSynthetic, AMemberUnderAGlobalFixupIsRefused) {
  const std::size_t speed = member_at("hkbClipGenerator", "playbackSpeed");
  const auto* layout = fo4::hkx::find_layout("hkbClipGenerator");
  std::vector<std::byte> names;
  const std::uint32_t name_at = add_class_name(names, layout->signature, "hkbClipGenerator");
  const auto bytes = build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", std::vector<std::byte>(layout->size, std::byte{0}), {},
                  {static_cast<std::uint32_t>(speed), 2, 0}, {0, 0, name_at}},
  });
  EXPECT_NE(refusal(bytes, 0, "playbackSpeed", "2", HkxScalarStatus::NotScalar).find("fixup"), std::string::npos);
  EXPECT_EQ(find(list_of(bytes, 0), "playbackSpeed"), nullptr);
  EXPECT_NE(find(list_of(bytes, 0), "mode"), nullptr);
}

TEST(HkxScalarEditSynthetic, AMemberUnderTheObjectsOwnVirtualFixupIsRefused) {
  const auto bytes = zero_object("hkbGeneratorSyncInfoSyncPoint");
  EXPECT_NE(refusal(bytes, 0, "id", "1", HkxScalarStatus::NotScalar).find("fixup"), std::string::npos);
  EXPECT_NE(refusal(bytes, 0, "time", "1", HkxScalarStatus::NotScalar).find("fixup"), std::string::npos);
  EXPECT_TRUE(list_of(bytes, 0).empty());
}

TEST(HkxScalarEditSynthetic, AnObjectTooShortForAMemberRefusesItAndALongEnoughOneAllowsIt) {
  const std::size_t speed = member_at("hkbClipGenerator", "playbackSpeed");
  const auto shorter = zero_object("hkbClipGenerator", {}, speed + 3);
  EXPECT_NE(refusal(shorter, 0, "playbackSpeed", "2", HkxScalarStatus::NotScalar).find("room"), std::string::npos);
  EXPECT_EQ(find(list_of(shorter, 0), "playbackSpeed"), nullptr);
  EXPECT_NE(find(list_of(shorter, 0), "startTime"), nullptr);

  const auto exact = zero_object("hkbClipGenerator", {}, speed + 4);
  const auto out = write_ok(exact, 0, "playbackSpeed", "1");
  ASSERT_EQ(out.size(), exact.size());
  EXPECT_EQ(out[data_start(exact) + speed + 3], std::byte{0x3F});
}

TEST(HkxScalarEditSynthetic, AnObjectWhoseSignatureDoesNotMatchItsClassLayoutIsRefusedWithNoLayout) {
  const auto* layout = fo4::hkx::find_layout(kSimple);
  const auto bytes = single_object_packfile(kSimple, layout->signature ^ 1u, std::vector<std::byte>(layout->size, std::byte{0}), {});
  std::vector<HkxScalarMember> rows;
  std::string why;
  EXPECT_EQ(fo4::hkx::list_scalar_members(bytes, 0, rows, why), HkxScalarStatus::NoLayout);
  EXPECT_NE(why.find("no layout"), std::string::npos);
  EXPECT_TRUE(rows.empty());
  EXPECT_NE(refusal(bytes, 0, "simpleTypeHkInt8", "1", HkxScalarStatus::NoLayout).find("no layout"), std::string::npos);
}

TEST(HkxScalarEditSynthetic, ABoolByteAboveOneIsUnlistedButStillWritable) {
  auto bytes = zero_object(kSimple);
  bytes[data_start(bytes) + member_at(kSimple, "simpleTypeBool")] = std::byte{2};
  EXPECT_EQ(find(list_of(bytes, 0), "simpleTypeBool"), nullptr);
  expect_write(bytes, "simpleTypeBool", "1", {0x01});
  expect_write(bytes, "simpleTypeBool", "false", {0x00});
}

TEST(HkxScalarEditSynthetic, AnUnnamedEnumValueCanBeRestatedButNotChangedToAnotherUnnamedOne) {
  auto bytes = zero_object("hkbClipGenerator");
  const std::size_t at = data_start(bytes) + member_at("hkbClipGenerator", "mode");
  bytes[at] = std::byte{77};
  const auto rows = list_of(bytes, 0);
  const HkxScalarMember* mode = find(rows, "mode");
  ASSERT_NE(mode, nullptr);
  EXPECT_EQ(mode->value, "77");
  EXPECT_TRUE(write_ok(bytes, 0, "mode", "77") == bytes);
  refusal(bytes, 0, "mode", "78", HkxScalarStatus::BadValue);
  EXPECT_EQ(write_ok(bytes, 0, "mode", "MODE_LOOPING")[at], std::byte{1});
}
