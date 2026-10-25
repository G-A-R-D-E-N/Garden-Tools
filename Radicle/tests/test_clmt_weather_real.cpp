#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/scalar.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;

constexpr auto k_clmt = fo4::Signature::from_string("CLMT");
constexpr auto k_wlst = fo4::Signature::from_string("WLST");

std::optional<Bytes> payload(const fo4::Plugin& plugin, const fo4::FormKey& key) {
  const auto* record = fo4::find_record(plugin, key);
  if (record == nullptr) return std::nullopt;
  for (const auto& subrecord : record->subrecords) {
    if (subrecord.signature == k_wlst) return subrecord.data;
  }
  return std::nullopt;
}

std::vector<fo4::FormKey> climates_with_weather(const fo4::Plugin& plugin) {
  std::vector<fo4::FormKey> keys;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    if (record.header.signature != k_clmt) return;
    for (const auto& subrecord : record.subrecords) {
      if (subrecord.signature == k_wlst && !subrecord.data.empty()) {
        keys.push_back(record.form_key);
        return;
      }
    }
  });
  return keys;
}

}  // namespace

TEST(ClmtWeatherReal, EveryWeatherListIsAWholeNumberOfTwelveByteEntries) {
  const auto* directory = std::getenv("FO4_DATA_DIR");
  if (!directory) {
    GTEST_SKIP() << "set FO4_DATA_DIR to run official-master coverage";
  }
  record_game_build(detect_game_build(directory));
  const auto path = std::filesystem::path(directory) / "Fallout4.esm";
  ASSERT_TRUE(std::filesystem::is_regular_file(path));
  const auto plugin = fo4::read_plugin_file(path);

  std::size_t climates = 0;
  std::size_t lists = 0;
  std::size_t entries = 0;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    if (record.header.signature != k_clmt) return;
    ++climates;
    for (const auto& subrecord : record.subrecords) {
      if (subrecord.signature != k_wlst) continue;
      ++lists;
      EXPECT_EQ(subrecord.data.size() % 12u, 0u) << record.form_key.to_string();
      entries += subrecord.data.size() / 12u;
    }
  });
  EXPECT_GT(climates, 0u);
  EXPECT_GT(lists, 0u);
  EXPECT_GT(entries, 0u);
}

TEST(ClmtWeatherReal, InsertAndRemoveRoundTripsOneWeatherEntry) {
  const auto* directory = std::getenv("FO4_DATA_DIR");
  if (!directory) {
    GTEST_SKIP() << "set FO4_DATA_DIR to run official-master coverage";
  }
  const auto build = detect_game_build(directory);
  record_game_build(build);
  const auto path = std::filesystem::path(directory) / "Fallout4.esm";
  ASSERT_TRUE(std::filesystem::is_regular_file(path));
  const auto plugin = fo4::read_plugin_file(path);
  const auto keys = climates_with_weather(plugin);
  ASSERT_FALSE(keys.empty());
  const auto key = keys.front();
  const auto original = payload(plugin, key);
  ASSERT_TRUE(original.has_value());
  ASSERT_EQ(original->size() % 12u, 0u);

  auto edited = fo4::read_plugin_file(path);
  const auto array = fo4::schema::counted_array_descriptor(k_clmt, "CLMT\\WeatherTypes");
  ASSERT_TRUE(array.has_value());
  EXPECT_EQ(fo4::insert_record_counted_element(edited, key, *array, 0),
            fo4::CountedArrayMutationStatus::updated);
  EXPECT_EQ(fo4::remove_record_counted_element(edited, key, *array, 0),
            fo4::CountedArrayMutationStatus::updated);
  EXPECT_EQ(payload(edited, key), original);

  EXPECT_EQ(fo4::insert_record_counted_element(edited, key, *array, 0),
            fo4::CountedArrayMutationStatus::updated);
  const auto bytes = fo4::write_plugin(edited);
  const auto reread = fo4::read_plugin(bytes, path);
  const auto grown = payload(reread, key);
  ASSERT_TRUE(grown.has_value());
  ASSERT_EQ(grown->size(), original->size() + 12u);
  EXPECT_EQ(Bytes(grown->begin(), grown->begin() + 12), Bytes(12, std::byte{0}));
  EXPECT_EQ(Bytes(grown->begin() + 12, grown->end()), *original);
  EXPECT_EQ(fo4::remove_record_counted_element(edited, key, *array, 0),
            fo4::CountedArrayMutationStatus::updated);
  EXPECT_EQ(payload(edited, key), original);
  EXPECT_FALSE(fo4::write_plugin(edited).empty());
}
