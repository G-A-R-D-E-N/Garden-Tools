#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/schema/scalar.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view k_group_path = "RACE\\TintLayerOption";

constexpr std::size_t family(std::string_view signature) {
  if (signature == "TETI") return 0;
  if (signature == "TTGP") return 1;
  if (signature == "TTEF") return 2;
  if (signature == "TTET") return 3;
  if (signature == "TTEB") return 4;
  if (signature == "TTEC") return 5;
  if (signature == "TTED") return 6;
  if (signature == "TTGE") return 7;
  return 99;
}

class RaceTintReal : public testing::TestWithParam<const char*> {};

}  // namespace

// Constraint: every RACE tint subrecord is grouped and sized before it decodes.
TEST_P(RaceTintReal, EveryTintSubrecordIsModelledAndSitsInsideItsOption) {
  const auto* directory = std::getenv("FO4_DATA_DIR");
  if (!directory) {
    GTEST_SKIP() << "set FO4_DATA_DIR to run official-master coverage";
  }
  const auto build = detect_game_build(directory);
  record_game_build(build);
  const auto path = std::filesystem::path(directory) / GetParam();
  ASSERT_TRUE(std::filesystem::is_regular_file(path));
  auto plugin = fo4::read_plugin_file(path);

  const auto race = fo4::Signature::from_string("RACE");
  const auto group =
      fo4::schema::subrecord_group_descriptor(race, k_group_path);
  ASSERT_TRUE(group.has_value());

  std::map<std::string, std::size_t> counts;
  std::size_t records = 0;
  std::size_t uncovered = 0;
  std::size_t unparsed = 0;

  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    if (record.header.signature != race) return;
    const bool tinted = std::any_of(record.subrecords.begin(), record.subrecords.end(),
                                    [](const auto& sub) {
                                      return family(sub.signature.to_string()) != 99;
                                    });
    if (!tinted) return;
    ++records;
    const auto spans = fo4::subrecord_group_spans(plugin, record.form_key, *group);
    if (!spans.has_value()) {
      ++unparsed;
      ADD_FAILURE() << GetParam() << ' ' << record.form_key.to_string()
                    << ": the tint option group does not parse";
      return;
    }

    std::size_t options = 0;
    std::size_t names = 0;
    std::size_t category_indexes = 0;
    for (std::size_t i = 0; i < record.subrecords.size(); ++i) {
      const auto& sub = record.subrecords[i];
      const std::string signature = sub.signature.to_string();
      const auto kind = family(signature);
      if (kind == 99) continue;
      ++counts[signature];

      const bool inside = std::any_of(
          spans->begin(), spans->end(),
          [i](const auto& span) { return i >= span.first && i < span.end; });
      if (kind == 1) {
        ++names;
      } else if (kind == 7) {
        ++category_indexes;
      } else {
        if (signature == "TETI") ++options;
        if (!inside) {
          ++uncovered;
          ADD_FAILURE() << GetParam() << ' ' << record.form_key.to_string() << ' ' << signature
                        << " at " << i << " is outside every tint option span";
        }
      }

      const auto size = sub.data.size();
      const bool sized = signature == "TETI"   ? size == 4u
                         : signature == "TTEF" ? size == 2u
                         : signature == "TTEB" ? size == 4u
                         : signature == "TTED" ? size == 4u
                         : signature == "TTGE" ? size == 4u
                         : signature == "TTEC" ? size % 14u == 0u
                                               : size != 0u;
      EXPECT_TRUE(sized) << GetParam() << ' ' << record.form_key.to_string() << ' ' << signature
                         << " is " << size << " bytes";
    }

    EXPECT_EQ(options, spans->size()) << GetParam() << ' ' << record.form_key.to_string();
    EXPECT_EQ(category_indexes, names - spans->size())
        << GetParam() << ' ' << record.form_key.to_string();

    for (const auto& span : *spans) {
      ASSERT_LT(span.first, span.end);
      ASSERT_LE(span.end, record.subrecords.size());
      EXPECT_EQ(record.subrecords[span.first].signature, fo4::Signature::from_string("TETI"));
      EXPECT_EQ(record.subrecords[span.first + 1].signature, fo4::Signature::from_string("TTGP"));
    }
  });

  EXPECT_EQ(unparsed, 0u) << GetParam();
  EXPECT_EQ(uncovered, 0u) << GetParam();
  EXPECT_GT(records, 0u) << GetParam();
  if (std::string_view{GetParam()} == "Fallout4.esm" && exact_counts_known(build)) {
    std::size_t total = 0;
    for (const auto& [_, count] : counts) total += count;
    EXPECT_EQ(total, 2335u) << GetParam();
    EXPECT_EQ(counts["TTGP"], 411u) << GetParam();
    EXPECT_EQ(counts["TETI"], 375u) << GetParam();
    EXPECT_EQ(counts["TTEF"], 286u) << GetParam();
    EXPECT_EQ(counts["TTET"], 873u) << GetParam();
    EXPECT_EQ(counts["TTEB"], 126u) << GetParam();
    EXPECT_EQ(counts["TTEC"], 112u) << GetParam();
    EXPECT_EQ(counts["TTED"], 116u) << GetParam();
    EXPECT_EQ(counts["TTGE"], 36u) << GetParam();
  }
}

INSTANTIATE_TEST_SUITE_P(OfficialMasters, RaceTintReal,
                         testing::Values("Fallout4.esm", "DLCCoast.esm", "DLCNukaWorld.esm"));
