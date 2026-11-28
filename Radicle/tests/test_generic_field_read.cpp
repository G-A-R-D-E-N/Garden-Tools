#include "fo4/c_api_env.h"
#include "fo4/c_api_pet_write.h"
#include "fo4/c_api_records.h"
#include "fo4/plugin/field_access.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/localized_fields.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr const char* kPlugin = "Generic.esp";

using EnvPtr = std::unique_ptr<radicle_env, decltype(&radicle_env_destroy)>;
using Bytes = std::vector<std::byte>;
using fo4::test::ByteWriter;

std::string le_hex(std::initializer_list<std::uint32_t> words) {
  static constexpr char k_digits[] = "0123456789abcdef";
  std::string out;
  for (const auto word : words) {
    for (int shift = 0; shift < 32; shift += 8) {
      const auto byte = (word >> shift) & 0xFFu;
      out.push_back(k_digits[byte >> 4]);
      out.push_back(k_digits[byte & 0x0Fu]);
    }
  }
  return out;
}

Bytes floats(std::initializer_list<float> values) {
  ByteWriter out;
  for (const auto value : values) out.f32(value);
  return out.bytes;
}

Bytes u32_pair(std::uint32_t first, std::uint32_t second) {
  ByteWriter out;
  out.u32(first);
  out.u32(second);
  return out.bytes;
}

Bytes counted(std::uint32_t value) {
  ByteWriter out;
  out.u32(value);
  return out.bytes;
}

Bytes properties() {
  ByteWriter out;
  out.u32(0x2A);
  out.f32(1.5f);
  out.u32(0x2B);
  out.f32(2.0f);
  return out.bytes;
}

Bytes modt() {
  ByteWriter out;
  out.u32(4);
  out.u32(1);
  out.u32(2);
  out.u32(0);
  out.u32(1);
  out.u32(0x0A0B0C0Du);
  out.sig("dds");
  out.u32(0x11121314u);
  out.u32(42);
  out.u32(7);
  out.u32(0x71727374u);
  out.sig("bgsm");
  out.u32(0x81828384u);
  return out.bytes;
}

Bytes text_bytes(std::string_view text) {
  ByteWriter out;
  out.raw(text);
  out.u8(0);
  return out.bytes;
}

Bytes components(std::initializer_list<std::pair<std::uint32_t, std::uint32_t>> rows) {
  ByteWriter out;
  for (const auto& [component, count] : rows) {
    out.u32(component);
    out.u32(count);
  }
  return out.bytes;
}

Bytes lock_data(std::uint8_t level, std::uint32_t key) {
  ByteWriter out;
  out.u8(level);
  out.u8(0);
  out.u8(0);
  out.u8(0);
  out.u32(key);
  out.u32(0);
  out.u32(0);
  return out.bytes;
}

Bytes unmodelled_modt() {
  auto out = modt();
  out.insert(out.end(), 4, std::byte{0});
  return out;
}

Bytes coed() {
  Bytes out;
  for (std::uint8_t value = 1; value <= 12; ++value) out.push_back(std::byte{value});
  return out;
}

struct Piece {
  std::string_view signature;
  Bytes data;
};

void write_record(ByteWriter& out, std::string_view signature, std::uint32_t form_id, const std::vector<Piece>& pieces,
                  std::uint32_t flags = 0) {
  const auto group = fo4::test::begin_group(out, signature, 0);
  const auto record = fo4::test::begin_record(out, signature, flags, form_id);
  for (const auto& piece : pieces) fo4::test::write_subrecord(out, piece.signature, piece.data);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
}

std::vector<std::byte> fixture() {
  ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(7);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto refrs = fo4::test::begin_group(out, "REFR", 0);
  const auto refr_a = fo4::test::begin_record(out, "REFR", 0, 0x800);
  fo4::test::write_subrecord(out, "XWCN", counted(3));
  fo4::test::write_subrecord(out, "XWCU", floats({1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}));
  fo4::test::end_record(out, refr_a);
  const auto refr_b = fo4::test::begin_record(out, "REFR", 0, 0x801);
  fo4::test::end_record(out, refr_b);
  const auto refr_c = fo4::test::begin_record(out, "REFR", 0, 0x802);
  fo4::test::write_subrecord(out, "XWCN", counted(4));
  fo4::test::write_subrecord(out, "XWCU", floats({1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}));
  fo4::test::end_record(out, refr_c);
  const auto refr_d = fo4::test::begin_record(out, "REFR", 0, 0x803);
  fo4::test::write_subrecord(out, "XLOC", lock_data(255, 0x890));
  fo4::test::end_record(out, refr_d);
  fo4::test::end_group(out, refrs);

  write_record(out, "ACTI", 0x810, {{"PRPS", properties()}, {"MODT", modt()}});
  write_record(out, "ACTI", 0x811, {{"MODT", unmodelled_modt()}});
  const auto old_acti = fo4::test::begin_group(out, "ACTI", 0);
  const auto old_record = fo4::test::begin_record(out, "ACTI", 0, 0x812, 39);
  fo4::test::write_subrecord(out, "MODT", modt());
  fo4::test::end_record(out, old_record);
  fo4::test::end_group(out, old_acti);
  write_record(out, "CONT", 0x820, {{"COCT", counted(2)},
                                    {"CNTO", u32_pair(0x800, 5)},
                                    {"COED", coed()},
                                    {"CNTO", u32_pair(0x801, 1)},
                                    {"DATA", Bytes(4)}});
  write_record(out, "ACTI", 0x830, {{"ATTX", text_bytes("Pull the lever")}});
  write_record(out, "PERK", 0x840, {{"DESC", text_bytes("Better aim")}, {"SNAM", counted(0x850)}, {"NNAM", counted(0x841)}});
  write_record(out, "PERK", 0x841, {{"DESC", text_bytes("Best aim")}});
  write_record(out, "NOTE", 0x860, {{"DNAM", Bytes{std::byte{3}}}, {"SNAM", counted(0x870)}});
  write_record(out, "NOTE", 0x861, {{"DNAM", Bytes{std::byte{0}}}, {"SNAM", counted(0x871)}});
  write_record(out, "MISC", 0x8A0, {{"CVPA", components({{0x8B0, 3}, {0x8B1, 5}})}, {"CDIX", Bytes(2)}});
  write_record(out, "COBJ", 0x8C0, {{"FVPA", components({{0x8B1, 2}})}, {"DESC", text_bytes("Craft it at the bench")}, {"FNAM", u32_pair(0x8D0, 0x8D1)}});
  write_record(out, "LIGH", 0x8E0, {}, 0x00010400u);
  write_record(out, "LIGH", 0x8E1, {}, 0x00000400u);
  return out.bytes;
}

class GenericFieldRead : public ::testing::Test {
protected:
  void SetUp() override {
    root_ = std::filesystem::temp_directory_path() /
            ("radicle_generic_read_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root_);
    const auto path = root_ / kPlugin;
    const auto bytes = fixture();
    {
      std::ofstream stream(path, std::ios::binary);
      stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    const auto text = path.string();
    const char* paths[] = {text.c_str()};
    radicle_env* env = nullptr;
    ASSERT_EQ(radicle_env_load(paths, 1, &env), RADICLE_STATUS_OK) << radicle_last_error();
    env_.reset(env);
  }

  void TearDown() override {
    env_.reset();
    std::error_code error;
    std::filesystem::remove_all(root_, error);
  }

  const radicle_record* winner(std::uint32_t local) const {
    const radicle_record* record = nullptr;
    EXPECT_EQ(radicle_env_winner(env_.get(), radicle_form_key{kPlugin, local}, &record), RADICLE_STATUS_OK);
    return record;
  }

  radicle_status field(std::uint32_t local, const char* path, size_t index, std::string& text) const {
    char* raw = nullptr;
    const auto status = radicle_record_get_field(env_.get(), winner(local), path, index, &raw);
    text = raw == nullptr ? std::string{} : std::string(raw);
    radicle_record_scalar_free(raw);
    return status;
  }

  size_t size(std::uint32_t local, const char* path, radicle_status expected = RADICLE_STATUS_OK) const {
    size_t out = 99;
    EXPECT_EQ(radicle_record_list_size(env_.get(), winner(local), path, &out), expected) << path;
    return out;
  }

  struct Edit {
    radicle_plugin* plugin{nullptr};
    ~Edit() { radicle_plugin_destroy(plugin); }
  };

  radicle_status set(radicle_plugin* plugin, std::uint32_t local, const char* path, size_t index, const std::string& text) const {
    return radicle_plugin_set_record_field(plugin, radicle_form_key{kPlugin, local}, path, index, text.c_str());
  }

  size_t plugin_size(radicle_plugin* plugin, std::uint32_t local, const char* path) const {
    size_t out = 99;
    EXPECT_EQ(radicle_plugin_list_size(plugin, radicle_form_key{kPlugin, local}, path, &out), RADICLE_STATUS_OK) << path;
    return out;
  }

  EnvPtr save_and_reload(radicle_plugin* plugin) const {
    const auto path = (root_ / "Edited" / kPlugin).string();
    std::filesystem::create_directories(root_ / "Edited");
    EXPECT_EQ(radicle_plugin_write_file(plugin, path.c_str()), RADICLE_STATUS_OK) << radicle_last_error();
    const char* paths[] = {path.c_str()};
    radicle_env* env = nullptr;
    EXPECT_EQ(radicle_env_load(paths, 1, &env), RADICLE_STATUS_OK) << radicle_last_error();
    return EnvPtr(env, radicle_env_destroy);
  }

  std::string read(const EnvPtr& env, std::uint32_t local, const char* path, size_t index) const {
    const radicle_record* record = nullptr;
    EXPECT_EQ(radicle_env_winner(env.get(), radicle_form_key{kPlugin, local}, &record), RADICLE_STATUS_OK);
    char* raw = nullptr;
    const auto status = radicle_record_get_field(env.get(), record, path, index, &raw);
    std::string text = status == RADICLE_STATUS_OK && raw != nullptr ? std::string(raw) : "<" + std::to_string(status) + ">";
    radicle_record_scalar_free(raw);
    return text;
  }

  size_t read_size(const EnvPtr& env, std::uint32_t local, const char* path) const {
    const radicle_record* record = nullptr;
    EXPECT_EQ(radicle_env_winner(env.get(), radicle_form_key{kPlugin, local}, &record), RADICLE_STATUS_OK);
    size_t out = 99;
    EXPECT_EQ(radicle_record_list_size(env.get(), record, path, &out), RADICLE_STATUS_OK) << path;
    return out;
  }

  radicle_plugin* open_plugin() const {
    radicle_plugin* plugin = nullptr;
    const auto path = (root_ / kPlugin).string();
    EXPECT_EQ(radicle_plugin_read_file(path.c_str(), &plugin), RADICLE_STATUS_OK) << radicle_last_error();
    return plugin;
  }

  std::filesystem::path root_;
  EnvPtr env_{nullptr, radicle_env_destroy};
};

std::string not_found() { return "<" + std::to_string(RADICLE_STATUS_NOT_FOUND) + ">"; }

}  // namespace

// Constraint: detects a counted-array container path that stops resolving through the env read.
TEST_F(GenericFieldRead, CountedArrayReadsEachElementAsHexAndCountsThem) {
  std::string text;
  ASSERT_EQ(field(0x800, "REFR\\WaterCurrentVelocities", 0, text), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(text, le_hex({0x3F800000u, 0x40000000u, 0x40400000u, 0x40800000u}));
  ASSERT_EQ(field(0x800, "REFR\\WaterCurrentVelocities", 2, text), RADICLE_STATUS_OK);
  EXPECT_EQ(text, le_hex({0x41100000u, 0x41200000u, 0x41300000u, 0x41400000u}));
  EXPECT_EQ(field(0x800, "REFR\\WaterCurrentVelocities", 3, text), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(size(0x800, "REFR\\WaterCurrentVelocities"), 3u);
}

// Constraint: detects struct members of a repeated element reading the wrong occurrence or count.
TEST_F(GenericFieldRead, CountedArrayStructMembersReadByElementIndex) {
  std::string text;
  ASSERT_EQ(field(0x800, "REFR\\WaterCurrentVelocityX", 0, text), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(text, "1");
  ASSERT_EQ(field(0x800, "REFR\\WaterCurrentVelocityY", 1, text), RADICLE_STATUS_OK);
  EXPECT_EQ(text, "6");
  ASSERT_EQ(field(0x800, "REFR\\WaterCurrentVelocityZ", 2, text), RADICLE_STATUS_OK);
  EXPECT_EQ(text, "11");
  EXPECT_EQ(size(0x800, "REFR\\WaterCurrentVelocityX"), 3u);
}

TEST_F(GenericFieldRead, CountedArrayStructMemberSetRoundTripsThroughPublicApi) {
  Edit edit{open_plugin()};
  ASSERT_EQ(set(edit.plugin, 0x800, "REFR\\WaterCurrentVelocityY", 1, "17.5"), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(plugin_size(edit.plugin, 0x800, "REFR\\WaterCurrentVelocities"), 3u);

  const auto reloaded = save_and_reload(edit.plugin);
  EXPECT_EQ(read(reloaded, 0x800, "REFR\\WaterCurrentVelocityX", 1), "5");
  EXPECT_EQ(read(reloaded, 0x800, "REFR\\WaterCurrentVelocityY", 0), "2");
  EXPECT_EQ(read(reloaded, 0x800, "REFR\\WaterCurrentVelocityY", 1), "17.5");
  EXPECT_EQ(read(reloaded, 0x800, "REFR\\WaterCurrentVelocityY", 2), "10");
  EXPECT_EQ(read(reloaded, 0x800, "REFR\\WaterCurrentVelocityZ", 1), "7");
  EXPECT_EQ(read_size(reloaded, 0x800, "REFR\\WaterCurrentVelocities"), 3u);
}

// Constraint: detects a record-flag read that takes the wrong header bit, reads a row of another record type or an unknown path as a clear flag, or leaves a stale *out_value after a refusal.
TEST_F(GenericFieldRead, RecordFlagReadsOneHeaderBitOfAnEnvironmentRecord) {
  constexpr const char* kRandomAnimStart = "LIGH\\RandomAnimStart";
  int32_t value = 7;
  ASSERT_EQ(radicle_record_get_flag(env_.get(), winner(0x8E0), kRandomAnimStart, &value), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(value, 1);
  ASSERT_EQ(radicle_record_get_flag(env_.get(), winner(0x8E1), kRandomAnimStart, &value), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(value, 0);

  value = 7;
  EXPECT_EQ(radicle_record_get_flag(env_.get(), winner(0x8E0), "STAT\\HasDistantLOD", &value), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(value, 0);
  value = 7;
  EXPECT_EQ(radicle_record_get_flag(env_.get(), winner(0x8E0), "LIGH\\NoSuchFlag", &value), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(value, 0);
  EXPECT_EQ(radicle_record_get_flag(env_.get(), winner(0x8E0), nullptr, &value), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_record_get_flag(env_.get(), winner(0x8E0), kRandomAnimStart, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
}

// Constraint: detects an absent array erroring instead of reading empty, or a corrupt count being read.
TEST_F(GenericFieldRead, AbsentArrayIsEmptyAndMismatchedCountIsRefused) {
  std::string text;
  EXPECT_EQ(field(0x801, "REFR\\WaterCurrentVelocities", 0, text), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(size(0x801, "REFR\\WaterCurrentVelocities"), 0u);
  EXPECT_EQ(field(0x802, "REFR\\WaterCurrentVelocities", 0, text), RADICLE_STATUS_INVALID_ARGUMENT);
  size(0x802, "REFR\\WaterCurrentVelocities", RADICLE_STATUS_INVALID_ARGUMENT);
}

// Constraint: detects an array without a count subrecord failing to read its elements from the payload length.
TEST_F(GenericFieldRead, UncountedArrayReadsElementsFromThePayloadLength) {
  std::string text;
  ASSERT_EQ(field(0x810, "ACTI\\Properties", 0, text), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(text, le_hex({0x2Au, 0x3FC00000u}));
  ASSERT_EQ(field(0x810, "ACTI\\Properties", 1, text), RADICLE_STATUS_OK);
  EXPECT_EQ(text, le_hex({0x2Bu, 0x40000000u}));
  EXPECT_EQ(field(0x810, "ACTI\\Properties", 2, text), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(size(0x810, "ACTI\\Properties"), 2u);
}

// Constraint: detects header-counted MODT segment arrays (117 of the 210 counted-array rows) throwing on read and size.
TEST_F(GenericFieldRead, HeaderCountedSegmentArraysReadAtTheirSegmentBase) {
  std::string text;
  EXPECT_EQ(size(0x810, "ACTI\\ModelInfoTextures"), 1u);
  EXPECT_EQ(size(0x810, "ACTI\\ModelInfoAddonNodes"), 2u);
  EXPECT_EQ(size(0x810, "ACTI\\ModelInfoMaterials"), 1u);
  ASSERT_EQ(field(0x810, "ACTI\\ModelInfoTextures", 0, text), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(text, le_hex({0x0A0B0C0Du, 0x00736464u, 0x11121314u}));
  ASSERT_EQ(field(0x810, "ACTI\\ModelInfoAddonNodes", 1, text), RADICLE_STATUS_OK);
  EXPECT_EQ(text, le_hex({7u}));
  ASSERT_EQ(field(0x810, "ACTI\\ModelInfoMaterials", 0, text), RADICLE_STATUS_OK);
  EXPECT_EQ(text, le_hex({0x71727374u, 0x6D736762u, 0x81828384u}));
  EXPECT_EQ(field(0x810, "ACTI\\ModelInfoAddonNodes", 2, text), RADICLE_STATUS_NOT_FOUND);
}

// Constraint: an unmodelled MODT payload must read as absent.
TEST_F(GenericFieldRead, SegmentArrayOnAnUnmodelledPayloadReadsAsAbsent) {
  std::string text;
  EXPECT_EQ(size(0x811, "ACTI\\ModelInfoTextures"), 0u);
  EXPECT_EQ(size(0x811, "ACTI\\ModelInfoAddonNodes"), 0u);
  EXPECT_EQ(field(0x811, "ACTI\\ModelInfoMaterials", 0, text), RADICLE_STATUS_NOT_FOUND);
}

// Constraint: a form version below the member rows' minimum must read as absent.
TEST_F(GenericFieldRead, SegmentArrayBelowTheModelInfoFormVersionReadsAsAbsent) {
  std::string text;
  EXPECT_EQ(size(0x812, "ACTI\\ModelInfoAddonNodes"), 0u);
  EXPECT_EQ(field(0x812, "ACTI\\ModelInfoTextures", 0, text), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(field(0x812, "ACTI\\ModelInfoAddonNode", 0, text), RADICLE_STATUS_NOT_FOUND);
}

// Constraint: detects a subrecord group run losing its optional COED member or its leader when read as one element.
TEST_F(GenericFieldRead, SubrecordGroupReadsEachRunAndCountsThem) {
  std::string text;
  ASSERT_EQ(field(0x820, "CONT\\Items", 0, text), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(text, le_hex({0x800u, 5u}) + "0102030405060708090a0b0c");
  ASSERT_EQ(field(0x820, "CONT\\Items", 1, text), RADICLE_STATUS_OK);
  EXPECT_EQ(text, le_hex({0x801u, 1u}));
  EXPECT_EQ(field(0x820, "CONT\\Items", 2, text), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(size(0x820, "CONT\\Items"), 2u);
}

// Constraint: detects a container path applying to a record of another type instead of being refused.
TEST_F(GenericFieldRead, ContainerPathsRefuseARecordOfAnotherType) {
  std::string text;
  EXPECT_EQ(field(0x810, "CONT\\Items", 0, text), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(field(0x820, "REFR\\WaterCurrentVelocities", 0, text), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(field(0x820, "CONT\\NoSuchRow", 0, text), RADICLE_STATUS_INVALID_ARGUMENT);
}

// Constraint: detects the bulk read dropping container paths or one bad record aborting its neighbours' rows.
TEST_F(GenericFieldRead, BulkReadsProjectContainerPathsAcrossWinners) {
  const char* paths[] = {"REFR\\WaterCurrentVelocities", "REFR\\WaterCurrentVelocityY"};
  radicle_winner_fields_row* rows = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_env_read_fields(env_.get(), nullptr, "REFR", paths, 2, 0, &rows, &count), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_EQ(count, 4u);
  EXPECT_EQ(rows[0].key.local_id, 0x800u);
  ASSERT_NE(rows[0].texts[0], nullptr);
  EXPECT_EQ(std::string(rows[0].texts[0]), le_hex({0x3F800000u, 0x40000000u, 0x40400000u, 0x40800000u}));
  ASSERT_NE(rows[0].texts[1], nullptr);
  EXPECT_EQ(std::string(rows[0].texts[1]), "2");
  EXPECT_EQ(rows[1].key.local_id, 0x801u);
  EXPECT_EQ(rows[1].texts[0], nullptr);
  EXPECT_EQ(rows[1].texts[1], nullptr);
  EXPECT_EQ(rows[2].key.local_id, 0x802u);
  EXPECT_EQ(rows[2].texts[0], nullptr);
  EXPECT_EQ(rows[3].key.local_id, 0x803u);
  EXPECT_EQ(rows[3].texts[0], nullptr);
  radicle_env_read_fields_free(rows, count, 2);

  radicle_winner_field* winners = nullptr;
  ASSERT_EQ(radicle_env_read_winners(env_.get(), nullptr, "CONT\\Items", 1, &winners, &count), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_EQ(count, 1u);
  ASSERT_NE(winners[0].text, nullptr);
  EXPECT_EQ(std::string(winners[0].text), le_hex({0x801u, 1u}));
  radicle_env_read_winners_free(winners, count);
}

// Constraint: detects ACTI activate text or PERK description losing its generic row, or falling out of the localized-field table.
TEST_F(GenericFieldRead, ActivateTextAndPerkDescriptionReadThroughTheirRows) {
  std::string text;
  ASSERT_EQ(field(0x830, "ACTI\\ActivateText", 0, text), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(text, "Pull the lever");
  ASSERT_EQ(field(0x840, "PERK\\Description", 0, text), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(text, "Better aim");
  EXPECT_EQ(field(0x810, "ACTI\\ActivateText", 0, text), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(field(0x830, "PERK\\Description", 0, text), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_TRUE(fo4::schema::is_localized_field(fo4::Signature::from_string("ACTI"), fo4::Signature::from_string("ATTX")));
  EXPECT_TRUE(fo4::schema::is_localized_field(fo4::Signature::from_string("PERK"), fo4::Signature::from_string("DESC")));
}

// Constraint: detects the COBJ description losing its generic row (NOT_FOUND) or dropping out of the localized-field table.
TEST_F(GenericFieldRead, CobjDescriptionReadsInlineThroughItsRow) {
  std::string text;
  ASSERT_EQ(field(0x8C0, "COBJ\\Description", 0, text), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(text, "Craft it at the bench");
  EXPECT_TRUE(fo4::schema::is_localized_field(fo4::Signature::from_string("COBJ"), fo4::Signature::from_string("DESC")));
}

// Constraint: detects the PERK sound or next-perk link reading the wrong subrecord, or an absent link not reading as missing.
TEST_F(GenericFieldRead, PerkSoundAndNextPerkReadAsFormKeys) {
  std::string text;
  ASSERT_EQ(field(0x840, "PERK\\Sound", 0, text), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(text, "000850:Generic.esp");
  ASSERT_EQ(field(0x840, "PERK\\NextPerk", 0, text), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(text, "000841:Generic.esp");
  EXPECT_EQ(field(0x841, "PERK\\Sound", 0, text), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(field(0x841, "PERK\\NextPerk", 0, text), RADICLE_STATUS_NOT_FOUND);
}

// Constraint: detects the NOTE target union reading through the wrong branch for its type byte.
TEST_F(GenericFieldRead, NoteTargetReadsOnlyThroughTheBranchTheTypeSelects) {
  std::string text;
  ASSERT_EQ(field(0x860, "NOTE\\TerminalTarget", 0, text), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(text, "000870:Generic.esp");
  EXPECT_EQ(field(0x860, "NOTE\\SoundTarget", 0, text), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(field(0x860, "NOTE\\VoiceTarget", 0, text), RADICLE_STATUS_NOT_FOUND);
  ASSERT_EQ(field(0x861, "NOTE\\SoundTarget", 0, text), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(text, "000871:Generic.esp");
  EXPECT_EQ(field(0x861, "NOTE\\TerminalTarget", 0, text), RADICLE_STATUS_NOT_FOUND);
}

// Constraint: detects the placed-reference lock key reading the wrong XLOC offset or a reference with no lock reading as set.
TEST_F(GenericFieldRead, ReferenceLockKeyReadsFromTheLockData) {
  std::string text;
  ASSERT_EQ(field(0x803, "REFR\\LockKey", 0, text), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(text, "000890:Generic.esp");
  EXPECT_EQ(field(0x800, "REFR\\LockKey", 0, text), RADICLE_STATUS_NOT_FOUND);
}

// Constraint: detects the MISC and COBJ component lists reading the count from the wrong word or dropping entries, and the COBJ category keywords.
TEST_F(GenericFieldRead, MiscAndCobjComponentListsReadEachEntryAndCategoryKeywords) {
  std::string text;
  ASSERT_EQ(field(0x8A0, "MISC\\ComponentObject", 0, text), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(text, "0008B0:Generic.esp");
  ASSERT_EQ(field(0x8A0, "MISC\\ComponentObject", 1, text), RADICLE_STATUS_OK);
  EXPECT_EQ(text, "0008B1:Generic.esp");
  ASSERT_EQ(field(0x8A0, "MISC\\ComponentCount", 0, text), RADICLE_STATUS_OK);
  EXPECT_EQ(text, "3");
  ASSERT_EQ(field(0x8A0, "MISC\\ComponentCount", 1, text), RADICLE_STATUS_OK);
  EXPECT_EQ(text, "5");
  ASSERT_EQ(field(0x8A0, "MISC\\Components", 1, text), RADICLE_STATUS_OK);
  EXPECT_EQ(text, le_hex({0x8B1u, 5u}));
  EXPECT_EQ(field(0x8A0, "MISC\\ComponentObject", 2, text), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(size(0x8A0, "MISC\\Components"), 2u);
  EXPECT_EQ(size(0x8A0, "MISC\\ComponentObject"), 2u);

  ASSERT_EQ(field(0x8C0, "COBJ\\ComponentObject", 0, text), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(text, "0008B1:Generic.esp");
  ASSERT_EQ(field(0x8C0, "COBJ\\ComponentCount", 0, text), RADICLE_STATUS_OK);
  EXPECT_EQ(text, "2");
  EXPECT_EQ(size(0x8C0, "COBJ\\Components"), 1u);
  EXPECT_EQ(size(0x8C0, "COBJ\\Category"), 2u);
  ASSERT_EQ(field(0x8C0, "COBJ\\Category", 1, text), RADICLE_STATUS_OK);
  EXPECT_EQ(text, "0008D1:Generic.esp");
}

// Constraint: detects the schema export no longer describing container rows (kind, stride, count subrecord).
TEST_F(GenericFieldRead, SchemaExportDescribesCountedArraysAndGroups) {
  const auto find = [](radicle_field_schema_ex_row* rows, size_t count, const char* path) {
    for (size_t i = 0; i < count; ++i) {
      if (std::strcmp(rows[i].path, path) == 0) return &rows[i];
    }
    return static_cast<radicle_field_schema_ex_row*>(nullptr);
  };
  radicle_field_schema_ex_row* rows = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_record_field_schema_ex("REFR", &rows, &count), RADICLE_STATUS_OK) << radicle_last_error();
  const auto* velocities = find(rows, count, "REFR\\WaterCurrentVelocities");
  ASSERT_NE(velocities, nullptr);
  EXPECT_EQ(velocities->kind, RADICLE_FIELD_COUNTED_ARRAY);
  EXPECT_STREQ(velocities->subrecord, "XWCU");
  EXPECT_STREQ(velocities->count_subrecord, "XWCN");
  EXPECT_EQ(velocities->stride, 16u);
  radicle_field_schema_ex_free(rows);

  ASSERT_EQ(radicle_record_field_schema_ex("CONT", &rows, &count), RADICLE_STATUS_OK) << radicle_last_error();
  const auto* items = find(rows, count, "CONT\\Items");
  ASSERT_NE(items, nullptr);
  EXPECT_EQ(items->kind, RADICLE_FIELD_SUBRECORD_GROUP);
  EXPECT_STREQ(items->count_subrecord, "COCT");
  radicle_field_schema_ex_free(rows);

  ASSERT_EQ(radicle_record_field_schema_ex("ACTI", &rows, &count), RADICLE_STATUS_OK) << radicle_last_error();
  const auto* addon = find(rows, count, "ACTI\\ModelInfoAddonNodes");
  ASSERT_NE(addon, nullptr);
  EXPECT_EQ(addon->kind, RADICLE_FIELD_COUNTED_ARRAY);
  EXPECT_EQ(addon->stride, 4u);
  radicle_field_schema_ex_free(rows);
}

// Constraint: detects a counted-array element write being refused, landing on the wrong element, or disturbing the count.
TEST_F(GenericFieldRead, CountedArrayElementSetRoundTripsThroughSaveAndReload) {
  Edit edit{open_plugin()};
  const auto replaced = le_hex({0x41800000u, 0x41880000u, 0x41900000u, 0x41980000u});
  ASSERT_EQ(set(edit.plugin, 0x800, "REFR\\WaterCurrentVelocities", 1, replaced), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(plugin_size(edit.plugin, 0x800, "REFR\\WaterCurrentVelocities"), 3u);
  const auto reloaded = save_and_reload(edit.plugin);
  EXPECT_EQ(read(reloaded, 0x800, "REFR\\WaterCurrentVelocities", 1), replaced);
  EXPECT_EQ(read(reloaded, 0x800, "REFR\\WaterCurrentVelocities", 0), le_hex({0x3F800000u, 0x40000000u, 0x40400000u, 0x40800000u}));
  EXPECT_EQ(read(reloaded, 0x800, "REFR\\WaterCurrentVelocities", 2), le_hex({0x41100000u, 0x41200000u, 0x41300000u, 0x41400000u}));
  EXPECT_EQ(read(reloaded, 0x800, "REFR\\WaterCurrentVelocityY", 1), "17");
  EXPECT_EQ(read_size(reloaded, 0x800, "REFR\\WaterCurrentVelocities"), 3u);
}

// Constraint: detects append (index == count) and remove leaving the XWCN count and the XWCU payload out of step.
TEST_F(GenericFieldRead, CountedArrayAppendAndRemoveKeepTheCountConsistent) {
  Edit edit{open_plugin()};
  const auto added = le_hex({1u, 2u, 3u, 4u});
  ASSERT_EQ(set(edit.plugin, 0x800, "REFR\\WaterCurrentVelocities", 3, added), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(plugin_size(edit.plugin, 0x800, "REFR\\WaterCurrentVelocities"), 4u);
  EXPECT_EQ(set(edit.plugin, 0x800, "REFR\\WaterCurrentVelocities", 5, added), RADICLE_STATUS_NOT_FOUND);
  ASSERT_EQ(radicle_plugin_list_remove(edit.plugin, radicle_form_key{kPlugin, 0x800}, "REFR\\WaterCurrentVelocities", 0),
            RADICLE_STATUS_OK);
  const auto reloaded = save_and_reload(edit.plugin);
  EXPECT_EQ(read_size(reloaded, 0x800, "REFR\\WaterCurrentVelocities"), 3u);
  EXPECT_EQ(read(reloaded, 0x800, "REFR\\WaterCurrentVelocities", 0), le_hex({0x40A00000u, 0x40C00000u, 0x40E00000u, 0x41000000u}));
  EXPECT_EQ(read(reloaded, 0x800, "REFR\\WaterCurrentVelocities", 2), added);
  EXPECT_EQ(read(reloaded, 0x800, "REFR\\WaterCurrentVelocities", 3), not_found());
}

// Constraint: detects malformed element text being accepted and corrupting the array payload.
TEST_F(GenericFieldRead, CountedArrayElementSetRefusesMalformedHexAndLeavesTheBytes) {
  Edit edit{open_plugin()};
  for (const char* bad : {"", "00", "zz000000000000000000000000000000", "3f80000040000000404000004080000000"}) {
    EXPECT_EQ(set(edit.plugin, 0x800, "REFR\\WaterCurrentVelocities", 0, bad), RADICLE_STATUS_INVALID_ARGUMENT) << bad;
  }
  EXPECT_EQ(plugin_size(edit.plugin, 0x800, "REFR\\WaterCurrentVelocities"), 3u);
  const auto reloaded = save_and_reload(edit.plugin);
  EXPECT_EQ(read(reloaded, 0x800, "REFR\\WaterCurrentVelocities", 0), le_hex({0x3F800000u, 0x40000000u, 0x40400000u, 0x40800000u}));
}

// Constraint: detects an uncounted array (no count subrecord) or a header-counted segment array refusing an in-place element write.
TEST_F(GenericFieldRead, UncountedAndSegmentArrayElementsAreWritableInPlace) {
  Edit edit{open_plugin()};
  const auto property = le_hex({0x2Cu, 0x40400000u});
  ASSERT_EQ(set(edit.plugin, 0x810, "ACTI\\Properties", 1, property), RADICLE_STATUS_OK) << radicle_last_error();
  ASSERT_EQ(set(edit.plugin, 0x810, "ACTI\\ModelInfoAddonNodes", 1, le_hex({9u})), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(set(edit.plugin, 0x810, "ACTI\\ModelInfoAddonNodes", 2, le_hex({9u})), RADICLE_STATUS_NOT_FOUND);
  const auto reloaded = save_and_reload(edit.plugin);
  EXPECT_EQ(read(reloaded, 0x810, "ACTI\\Properties", 1), property);
  EXPECT_EQ(read(reloaded, 0x810, "ACTI\\Properties", 0), le_hex({0x2Au, 0x3FC00000u}));
  EXPECT_EQ(read(reloaded, 0x810, "ACTI\\ModelInfoAddonNodes", 1), le_hex({9u}));
  EXPECT_EQ(read(reloaded, 0x810, "ACTI\\ModelInfoAddonNodes", 0), le_hex({42u}));
  EXPECT_EQ(read_size(reloaded, 0x810, "ACTI\\ModelInfoAddonNodes"), 2u);
}

// Constraint: detects a subrecord-group element write splitting across the wrong subrecords or losing the optional COED member.
TEST_F(GenericFieldRead, SubrecordGroupElementSetRoundTripsThroughSaveAndReload) {
  Edit edit{open_plugin()};
  const auto first = le_hex({0x802u, 7u}) + "ffeeddccbbaa998877665544";
  const auto second = le_hex({0x803u, 2u});
  ASSERT_EQ(set(edit.plugin, 0x820, "CONT\\Items", 0, first), RADICLE_STATUS_OK) << radicle_last_error();
  ASSERT_EQ(set(edit.plugin, 0x820, "CONT\\Items", 1, second), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(set(edit.plugin, 0x820, "CONT\\Items", 1, first), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(set(edit.plugin, 0x820, "CONT\\Items", 0, second), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(set(edit.plugin, 0x820, "CONT\\Items", 3, second), RADICLE_STATUS_NOT_FOUND);
  const auto reloaded = save_and_reload(edit.plugin);
  EXPECT_EQ(read(reloaded, 0x820, "CONT\\Items", 0), first);
  EXPECT_EQ(read(reloaded, 0x820, "CONT\\Items", 1), second);
  EXPECT_EQ(read_size(reloaded, 0x820, "CONT\\Items"), 2u);
}

// Constraint: detects group append and remove leaving the COCT count out of step with the CNTO runs.
TEST_F(GenericFieldRead, SubrecordGroupAppendAndRemoveKeepTheCountConsistent) {
  Edit edit{open_plugin()};
  const auto added = le_hex({0x804u, 3u});
  ASSERT_EQ(set(edit.plugin, 0x820, "CONT\\Items", 2, added), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(plugin_size(edit.plugin, 0x820, "CONT\\Items"), 3u);
  EXPECT_EQ(set(edit.plugin, 0x820, "CONT\\Items", 3, added + "00"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(plugin_size(edit.plugin, 0x820, "CONT\\Items"), 3u);
  ASSERT_EQ(radicle_plugin_list_remove(edit.plugin, radicle_form_key{kPlugin, 0x820}, "CONT\\Items", 0), RADICLE_STATUS_OK);
  const auto reloaded = save_and_reload(edit.plugin);
  EXPECT_EQ(read_size(reloaded, 0x820, "CONT\\Items"), 2u);
  EXPECT_EQ(read(reloaded, 0x820, "CONT\\Items", 0), le_hex({0x801u, 1u}));
  EXPECT_EQ(read(reloaded, 0x820, "CONT\\Items", 1), added);
  EXPECT_EQ(read(reloaded, 0x820, "CONT\\Items", 2), not_found());
}

// Constraint: detects an appended group element whose hex carries an optional member (CNTO plus COED) being refused or losing the COED, so the hex get_field returns could not be appended back.
TEST_F(GenericFieldRead, SubrecordGroupAppendCreatesTheOptionalMembersTheHexImplies) {
  Edit edit{open_plugin()};
  const auto with_coed = le_hex({0x804u, 3u}) + "0a0b0c0d0e0f101112131415";
  ASSERT_EQ(set(edit.plugin, 0x820, "CONT\\Items", 2, with_coed), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(plugin_size(edit.plugin, 0x820, "CONT\\Items"), 3u);
  const auto plain = le_hex({0x805u, 1u});
  ASSERT_EQ(set(edit.plugin, 0x820, "CONT\\Items", 3, plain), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(set(edit.plugin, 0x820, "CONT\\Items", 4, with_coed + "00"), RADICLE_STATUS_INVALID_ARGUMENT);
  const auto reloaded = save_and_reload(edit.plugin);
  EXPECT_EQ(read_size(reloaded, 0x820, "CONT\\Items"), 4u);
  EXPECT_EQ(read(reloaded, 0x820, "CONT\\Items", 2), with_coed);
  EXPECT_EQ(read(reloaded, 0x820, "CONT\\Items", 3), plain);
  EXPECT_EQ(read(reloaded, 0x820, "CONT\\Items", 0), le_hex({0x800u, 5u}) + "0102030405060708090a0b0c");
}

// Constraint: detects a refused append that leaves the half-built element, a bumped count, or a record marked modified (its stored bytes dropped).
TEST(GenericFieldWrite, RefusedGroupAppendLeavesTheRecordExactlyAsItWas) {
  auto plugin = fo4::read_plugin(fixture(), kPlugin);
  const auto key = fo4::FormKey::parse(std::string("000820:") + kPlugin);
  const auto before = fo4::find_record(plugin, key)->subrecords;
  ASSERT_FALSE(fo4::find_record(plugin, key)->original_range.empty());
  ASSERT_FALSE(plugin.original_range.empty());
  EXPECT_THROW(fo4::write_record_field(plugin, key, "CONT\\Items", 2, le_hex({0x804u, 3u}) + "00"),
               fo4::schema::SubrecordGroupError);
  const auto& after = *fo4::find_record(plugin, key);
  ASSERT_EQ(after.subrecords.size(), before.size());
  for (std::size_t i = 0; i < before.size(); ++i) {
    EXPECT_EQ(after.subrecords[i].signature, before[i].signature) << i;
    EXPECT_EQ(after.subrecords[i].data, before[i].data) << i;
  }
  EXPECT_FALSE(after.original_range.empty());
  EXPECT_FALSE(plugin.original_range.empty());
}

namespace {

using fo4::Signature;
using fo4::schema::SubrecordGroupMemberDescriptor;

const Signature k_data_successor = Signature::from_string("DATA");

fo4::schema::SubrecordGroupDescriptor adoption_group(std::span<const SubrecordGroupMemberDescriptor> members) {
  fo4::schema::SubrecordGroupDescriptor group;
  group.record = Signature::from_string("CONT");
  group.path = "CONT\\AdoptionProbe";
  group.members = members;
  group.successors = std::span<const Signature>(&k_data_successor, 1);
  return group;
}

std::string repeated_hex(std::size_t bytes) {
  std::string out;
  for (std::size_t i = 0; i < bytes; ++i) out += "ab";
  return out;
}

void expect_record_unchanged(const fo4::Plugin& plugin, const fo4::FormKey& key, const std::vector<fo4::Subrecord>& before) {
  const auto& after = fo4::find_record(plugin, key)->subrecords;
  ASSERT_EQ(after.size(), before.size());
  for (std::size_t i = 0; i < before.size(); ++i) {
    EXPECT_EQ(after[i].signature, before[i].signature) << i;
    EXPECT_EQ(after[i].data, before[i].data) << i;
  }
}

}  // namespace

// Constraint: detects an element hex that two different optional member sets could explain being resolved to one of them instead of refused.
TEST(GenericFieldWrite, GroupAdoptionRefusesHexThatSeveralOptionalMemberSetsFit) {
  static const SubrecordGroupMemberDescriptor members[] = {
      {Signature::from_string("CNTO"), true, false, 8},
      {Signature::from_string("COED"), false, false, 12},
      {Signature::from_string("XAAA"), false, false, 12},
  };
  const auto group = adoption_group(members);
  auto plugin = fo4::read_plugin(fixture(), kPlugin);
  const auto key = fo4::FormKey::parse(std::string("000820:") + kPlugin);
  const auto before = fo4::find_record(plugin, key)->subrecords;
  EXPECT_THROW(fo4::set_record_subrecord_group_element_hex(plugin, key, group, 1, repeated_hex(20), nullptr, true),
               fo4::schema::SubrecordGroupError);
  expect_record_unchanged(plugin, key, before);
}

// Constraint: detects hex that no optional member set explains adding a member or changing the record instead of being refused.
TEST(GenericFieldWrite, GroupAdoptionRefusesHexThatNoOptionalMemberSetFits) {
  static const SubrecordGroupMemberDescriptor members[] = {
      {Signature::from_string("CNTO"), true, false, 8},
      {Signature::from_string("COED"), false, false, 12},
  };
  const auto group = adoption_group(members);
  auto plugin = fo4::read_plugin(fixture(), kPlugin);
  const auto key = fo4::FormKey::parse(std::string("000820:") + kPlugin);
  const auto before = fo4::find_record(plugin, key)->subrecords;
  EXPECT_THROW(fo4::set_record_subrecord_group_element_hex(plugin, key, group, 1, repeated_hex(15), nullptr, true),
               fo4::schema::SubrecordGroupError);
  expect_record_unchanged(plugin, key, before);
}

// Constraint: detects an optional member being adopted next to a present variable-length member, whose bytes the variable member must absorb instead.
TEST(GenericFieldWrite, GroupAdoptionLeavesOptionalMembersAbsentWhenAVariableMemberIsPresent) {
  static const SubrecordGroupMemberDescriptor members[] = {
      {Signature::from_string("CNTO"), true, false, 8},
      {Signature::from_string("COED"), false, false, 0},
      {Signature::from_string("XAAA"), false, false, 4},
  };
  const auto group = adoption_group(members);
  auto plugin = fo4::read_plugin(fixture(), kPlugin);
  const auto key = fo4::FormKey::parse(std::string("000820:") + kPlugin);
  const auto before = fo4::find_record(plugin, key)->subrecords;
  ASSERT_EQ(fo4::set_record_subrecord_group_element_hex(plugin, key, group, 0, repeated_hex(12), nullptr, true),
            fo4::SubrecordGroupMutationStatus::updated);
  const auto& after = fo4::find_record(plugin, key)->subrecords;
  ASSERT_EQ(after.size(), before.size());
  for (std::size_t i = 0; i < after.size(); ++i) EXPECT_EQ(after[i].signature, before[i].signature) << i;
  EXPECT_EQ(after[1].data.size(), 8u);
  EXPECT_EQ(after[2].data.size(), 4u);
  EXPECT_THROW(fo4::set_record_subrecord_group_element_hex(plugin, key, group, 0, repeated_hex(4), nullptr, true),
               fo4::schema::SubrecordGroupError);
  EXPECT_EQ(fo4::find_record(plugin, key)->subrecords[2].data.size(), 4u);
}

TEST(GenericFieldWrite, GroupAdoptionOfZeroDefaultsReportsUpdatedAndPersistsMember) {
  static const SubrecordGroupMemberDescriptor members[] = {
      {Signature::from_string("CNTO"), true, false, 8},
      {Signature::from_string("COED"), false, false, 12},
  };
  const auto group = adoption_group(members);
  auto plugin = fo4::read_plugin(fixture(), kPlugin);
  const auto key = fo4::FormKey::parse(std::string("000820:") + kPlugin);
  const auto text = le_hex({0x801u, 1u}) + std::string(24, '0');

  ASSERT_FALSE(plugin.original_range.empty());
  EXPECT_EQ(fo4::set_record_subrecord_group_element_hex(plugin, key, group, 1, text, nullptr, true),
            fo4::SubrecordGroupMutationStatus::updated);
  EXPECT_TRUE(plugin.original_range.empty());

  const auto saved = fo4::read_plugin(fo4::write_plugin(plugin), kPlugin);
  const auto* record = fo4::find_record(saved, key);
  ASSERT_NE(record, nullptr);
  const auto spans = fo4::subrecord_group_spans(*record, group);
  ASSERT_EQ(spans.size(), 2u);
  ASSERT_EQ(spans[1].end - spans[1].first, 2u);
  EXPECT_EQ(record->subrecords[spans[1].first + 1].signature, Signature::from_string("COED"));
  EXPECT_EQ(record->subrecords[spans[1].first + 1].data, Bytes(12, std::byte{0}));
}

// Constraint: detects a refused adoption leaving the members it had already added, so a caller that did not snapshot keeps a half-adopted element.
TEST(GenericFieldWrite, GroupAdoptionRestoresTheRecordWhenALaterMemberIsRefused) {
  static const SubrecordGroupMemberDescriptor members[] = {
      {Signature::from_string("CNTO"), true, false, 8},
      {Signature::from_string("XAAA"), false, false, 4},
      {Signature::from_string("XBBB"), false, false, 4, {}, true},
  };
  const auto group = adoption_group(members);
  auto plugin = fo4::read_plugin(fixture(), kPlugin);
  plugin.header.tes4_flags = fo4::RecordFlags(fo4::RecordFlags::localized);
  const auto key = fo4::FormKey::parse(std::string("000820:") + kPlugin);
  const auto before = fo4::find_record(plugin, key)->subrecords;
  ASSERT_FALSE(fo4::find_record(plugin, key)->original_range.empty());
  ASSERT_FALSE(plugin.original_range.empty());
  EXPECT_THROW(fo4::set_record_subrecord_group_element_hex(plugin, key, group, 1, repeated_hex(16), nullptr, true),
               fo4::schema::SubrecordGroupError);
  expect_record_unchanged(plugin, key, before);
  EXPECT_FALSE(fo4::find_record(plugin, key)->original_range.empty());
  EXPECT_FALSE(plugin.original_range.empty());
}

// Constraint: detects self_restoring false still restoring the record, which would copy the record twice on the append path that already holds its own snapshot.
TEST(GenericFieldWrite, GroupAdoptionLeavesTheRestoreToTheCallerWhenNotSelfRestoring) {
  static const SubrecordGroupMemberDescriptor members[] = {
      {Signature::from_string("CNTO"), true, false, 8},
      {Signature::from_string("XAAA"), false, false, 4},
      {Signature::from_string("XBBB"), false, false, 4, {}, true},
  };
  const auto group = adoption_group(members);
  auto plugin = fo4::read_plugin(fixture(), kPlugin);
  plugin.header.tes4_flags = fo4::RecordFlags(fo4::RecordFlags::localized);
  const auto key = fo4::FormKey::parse(std::string("000820:") + kPlugin);
  const auto before = fo4::find_record(plugin, key)->subrecords.size();
  EXPECT_THROW(fo4::set_record_subrecord_group_element_hex(plugin, key, group, 1, repeated_hex(16), nullptr, true, false),
               fo4::schema::SubrecordGroupError);
  EXPECT_GT(fo4::find_record(plugin, key)->subrecords.size(), before);
}
