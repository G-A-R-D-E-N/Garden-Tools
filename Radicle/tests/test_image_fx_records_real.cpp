#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/record_constraints.hpp"
#include "fo4/schema/scalar.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view k_batch[] = {"EFSH", "IMGS", "IMAD", "ZOOM", "CAMS", "SPGD", "GDRY"};

bool in_batch(fo4::Signature signature) {
  return std::any_of(std::begin(k_batch), std::end(k_batch), [&](std::string_view name) {
    return signature == fo4::Signature::from_string(name);
  });
}

bool printable(const std::string& text) {
  return std::all_of(text.begin(), text.end(), [](char ch) { return ch >= 0x20 && ch <= 0x7E; });
}

class ImageFxRecordsReal : public testing::TestWithParam<const char*> {};

TEST_P(ImageFxRecordsReal, EveryBatchRecordMatchesItsAuthoringAndDecodesEveryRow) {
  const auto* directory = std::getenv("FO4_DATA_DIR");
  if (!directory) {
    GTEST_SKIP() << "set FO4_DATA_DIR to run official-master coverage";
  }
  record_game_build(detect_game_build(directory));
  const auto path = std::filesystem::path(directory) / GetParam();
  ASSERT_TRUE(std::filesystem::is_regular_file(path));
  auto plugin = fo4::read_plugin_file(path);
  std::size_t batch_records = 0;
  std::map<std::string, std::size_t> sampled;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    const auto signature = record.header.signature;
    if (!in_batch(signature)) return;
    ++batch_records;
    const auto constraint = fo4::schema::record_authoring_constraint(signature);
    ASSERT_TRUE(constraint.has_value()) << signature.to_string();
    const auto rules = fo4::schema::authoring_subrecords(*constraint);
    std::size_t last_index = 0;
    for (const auto& subrecord : record.subrecords) {
      if (!printable(subrecord.signature.to_string())) continue;
      const auto rule = std::find_if(rules.begin(), rules.end(), [&](const auto& candidate) {
        return candidate.signature == subrecord.signature;
      });
      if (rule == rules.end()) continue;
      const auto index = static_cast<std::size_t>(std::distance(rules.begin(), rule));
      EXPECT_GE(index, last_index)
          << record.form_key.to_string() << ' ' << subrecord.signature.to_string();
      last_index = index;
    }
    for (const auto& rule : rules) {
      const auto count = static_cast<std::size_t>(std::count_if(
          record.subrecords.begin(), record.subrecords.end(),
          [&](const auto& subrecord) { return subrecord.signature == rule.signature; }));
      EXPECT_GE(count, static_cast<std::size_t>(rule.minimum))
          << record.form_key.to_string() << ' ' << rule.signature.to_string();
      EXPECT_LE(count, static_cast<std::size_t>(rule.maximum))
          << record.form_key.to_string() << ' ' << rule.signature.to_string();
    }
    for (const auto& field : fo4::schema::scalar_fields(signature)) {
      for (const auto& subrecord : record.subrecords) {
        if (subrecord.signature != field.scalar.subrecord) continue;
        const auto bytes = fo4::schema::scalar_field_bytes(subrecord.data, field.scalar);
        if (!bytes.empty()) static_cast<void>(fo4::schema::decode_scalar(bytes, field.scalar));
      }
    }
    if (sampled[signature.to_string()]++ < 64) {
      for (const auto& field : fo4::schema::formlink_fields(signature)) {
        static_cast<void>(fo4::record_field_count(plugin, record.form_key, field));
      }
    }
  });
  if (std::string_view(GetParam()) == "Fallout4.esm") {
    EXPECT_GT(batch_records, 0u);
  }
}

TEST(ImageFxRecordsReal, EveryImadRecordDecodesTheHdrMultInterpolatorRows) {
  const auto* directory = std::getenv("FO4_DATA_DIR");
  if (!directory) {
    GTEST_SKIP() << "set FO4_DATA_DIR to run official-master coverage";
  }
  record_game_build(detect_game_build(directory));
  const auto path = std::filesystem::path(directory) / "Fallout4.esm";
  ASSERT_TRUE(std::filesystem::is_regular_file(path));
  auto plugin = fo4::read_plugin_file(path);
  const auto time = fo4::schema::scalar_field_descriptor("IMAD\\HDREyeAdaptSpeedMultTime");
  const auto value = fo4::schema::scalar_field_descriptor("IMAD\\HDREyeAdaptSpeedMultValue");
  ASSERT_TRUE(time.has_value());
  ASSERT_TRUE(value.has_value());
  std::size_t records = 0;
  std::size_t elements = 0;
  std::string known_time;
  std::string known_value;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    if (record.header.signature != fo4::Signature::from_string("IMAD")) return;
    ++records;
    std::span<const std::byte> mult;
    for (const auto& subrecord : record.subrecords) {
      if (subrecord.signature == time->scalar.subrecord) mult = subrecord.data;
    }

    ASSERT_FALSE(mult.empty()) << record.form_key.to_string();
    ASSERT_EQ(mult.size() % 8, 0u) << record.form_key.to_string();
    const auto times = fo4::schema::scalar_field_bytes(mult, time->scalar);
    const auto values = fo4::schema::scalar_field_bytes(mult, value->scalar);
    const auto width = fo4::schema::scalar_storage_size(time->scalar.storage);
    EXPECT_EQ(times.size(), width) << record.form_key.to_string();
    EXPECT_EQ(values.size(), width) << record.form_key.to_string();
    EXPECT_EQ(values.size(), times.size()) << record.form_key.to_string();
    elements += mult.size() / 8;
    if (record.form_key.local().value() != 0x24A3C4u) return;
    known_time = fo4::schema::decode_scalar(fo4::schema::scalar_field_bytes(mult, time->scalar),
                                            time->scalar);
    known_value = fo4::schema::decode_scalar(fo4::schema::scalar_field_bytes(mult, value->scalar),
                                             value->scalar);
  });
  EXPECT_EQ(records, 247u);
  EXPECT_EQ(elements, 494u);
  EXPECT_EQ(known_time, "0");
  EXPECT_EQ(known_value, "1");
}

TEST(ImageFxRecordsReal, ImadMultAndAddCountsMatchTheDnamPairs) {
  const auto* directory = std::getenv("FO4_DATA_DIR");
  if (!directory) {
    GTEST_SKIP() << "set FO4_DATA_DIR to run official-master coverage";
  }
  record_game_build(detect_game_build(directory));
  const auto path = std::filesystem::path(directory) / "Fallout4.esm";
  ASSERT_TRUE(std::filesystem::is_regular_file(path));
  auto plugin = fo4::read_plugin_file(path);
  const std::array<std::string_view, 21> names = {
      "HDREyeAdaptSpeed", "HDRBloomBlurRadius", "HDRBloomThreshold", "HDRBloomScale",
      "HDRTargetLumMin", "HDRTargetLumMax", "HDRSunlightScale", "HDRSkyScale",
      "HDRUnused8", "HDRUnused9", "HDRUnused10", "HDRUnused11", "HDRUnused12",
      "HDRUnused13", "HDRUnused14", "HDRUnused15", "HDRUnused16",
      "CinematicSaturation", "CinematicBrightness", "CinematicContrast", "CinematicUnused"};
  struct Slot {
    fo4::schema::ScalarFieldDescriptor mult;
    fo4::schema::ScalarFieldDescriptor add;
  };
  std::vector<Slot> slots;
  for (std::size_t index = 0; index < names.size(); ++index) {
    const auto prefix = std::string("IMAD\\") + std::string(names[index]);
    const auto mult_time = fo4::schema::scalar_field_descriptor(prefix + "MultTime");
    const auto mult_value = fo4::schema::scalar_field_descriptor(prefix + "MultValue");
    const auto add_time = fo4::schema::scalar_field_descriptor(prefix + "AddTime");
    const auto add_value = fo4::schema::scalar_field_descriptor(prefix + "AddValue");
    ASSERT_TRUE(mult_time.has_value()) << prefix;
    ASSERT_TRUE(mult_value.has_value()) << prefix;
    ASSERT_TRUE(add_time.has_value()) << prefix;
    ASSERT_TRUE(add_value.has_value()) << prefix;
    EXPECT_EQ(mult_time->scalar.subrecord,
              fo4::Signature::from_bytes(static_cast<std::uint32_t>(0x44414900u + index))) << prefix;
    EXPECT_EQ(add_time->scalar.subrecord,
              fo4::Signature::from_bytes(static_cast<std::uint32_t>(0x44414940u + index))) << prefix;
    EXPECT_EQ(mult_value->scalar.subrecord, mult_time->scalar.subrecord) << prefix;
    EXPECT_EQ(add_value->scalar.subrecord, add_time->scalar.subrecord) << prefix;
    EXPECT_EQ(mult_time->scalar.offset, 0u) << prefix;
    EXPECT_EQ(mult_value->scalar.offset, 4u) << prefix;
    EXPECT_EQ(add_time->scalar.offset, 0u) << prefix;
    EXPECT_EQ(add_value->scalar.offset, 4u) << prefix;
    slots.push_back({*mult_time, *add_time});
  }
  ASSERT_EQ(slots.size(), names.size());
  const auto imad = fo4::Signature::from_string("IMAD");
  const auto dnam = fo4::Signature::from_string("DNAM");
  std::size_t records = 0;
  std::size_t pairs = 0;
  std::size_t mismatched = 0;
  std::size_t odd_sizes = 0;
  std::size_t first_slot_elements = 0;
  std::size_t missing = 0;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    if (record.header.signature != imad) return;
    ++records;
    std::span<const std::byte> counts;
    for (const auto& subrecord : record.subrecords) {
      if (subrecord.signature == dnam) counts = subrecord.data;
    }
    ASSERT_GE(counts.size(), 176u) << record.form_key.to_string();
    for (std::size_t slot = 0; slot < slots.size(); ++slot) {
      std::size_t mult_bytes = 0;
      std::size_t add_bytes = 0;
      for (const auto& subrecord : record.subrecords) {
        if (subrecord.signature == slots[slot].mult.scalar.subrecord) mult_bytes = subrecord.data.size();
        if (subrecord.signature == slots[slot].add.scalar.subrecord) add_bytes = subrecord.data.size();
      }
      std::uint32_t mult_count = 0;
      std::uint32_t add_count = 0;
      std::memcpy(&mult_count, counts.data() + 8 + 8 * slot, sizeof(mult_count));
      std::memcpy(&add_count, counts.data() + 12 + 8 * slot, sizeof(add_count));
      ++pairs;
      if (mult_bytes == 0 || add_bytes == 0) ++missing;
      if (mult_bytes % 8 != 0 || add_bytes % 8 != 0) ++odd_sizes;
      if (mult_count != mult_bytes / 8 || add_count != add_bytes / 8) ++mismatched;
      if (slot == 0) first_slot_elements += mult_bytes / 8;
    }
  });
  EXPECT_EQ(records, 247u);
  EXPECT_EQ(pairs, 247u * 21u);
  EXPECT_EQ(missing, 0u);
  EXPECT_EQ(odd_sizes, 0u);
  EXPECT_EQ(mismatched, 0u);
  EXPECT_EQ(first_slot_elements, 494u);
}

INSTANTIATE_TEST_SUITE_P(OfficialMasters, ImageFxRecordsReal, testing::Values(
    "Fallout4.esm", "DLCRobot.esm", "DLCworkshop01.esm", "DLCCoast.esm",
    "DLCworkshop02.esm", "DLCworkshop03.esm", "DLCNukaWorld.esm"));

}  // namespace
