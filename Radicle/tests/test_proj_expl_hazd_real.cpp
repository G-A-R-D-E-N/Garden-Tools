// Checks the declared PROJ/EXPL/HAZD authoring schema against every shipped
// record of those families. The schema's cardinalities were measured from
// these files, so this is the regression that keeps them honest.
//
// Gated on FO4_DATA_DIR; CI has no game files and will skip.

#include "fo4/plugin/reader.hpp"
#include "fo4/schema/localized_fields.hpp"
#include "fo4/schema/preflight.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace {

struct Family {
  const char* signature;
  std::vector<const char*> required;   // present in every shipped record
  std::vector<const char*> optional;   // present in some
};

const std::vector<Family>& families() {
  static const std::vector<Family> value{
      {"PROJ",
       {"EDID", "OBND", "DATA", "DNAM", "NAM1", "VNAM"},
       {"FULL", "MODL", "MODT", "DEST", "DSTD", "DMDL", "DMDT", "DSTF", "NAM2"}},
      {"EXPL", {"EDID", "OBND", "DATA"}, {"FULL", "MODL", "MODT", "EITM", "MNAM"}},
      {"HAZD", {"EDID", "OBND", "MODL", "MODT", "DNAM"}, {"FULL", "MNAM"}},
  };
  return value;
}

}  // namespace

TEST(ProjExplHazdRealData, SchemaMatchesEveryShippedRecord) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to the Fallout 4 Data folder to run this";
  }
  record_game_build(detect_game_build(dir));

  std::map<std::string, std::size_t> seen;
  std::vector<std::string> failures;
  int files = 0;

  for (const char* name : {"Fallout4.esm", "DLCCoast.esm", "DLCNukaWorld.esm", "DLCRobot.esm"}) {
    const auto path = std::filesystem::path{dir} / name;
    if (!std::filesystem::exists(path)) continue;
    ++files;
    auto plugin = fo4::read_plugin_file(path);

    for (const auto& family : families()) {
      const auto signature = fo4::Signature::from_string(family.signature);
      for (const auto* record : fo4::collect_records(plugin)) {
        if (record->header.signature != signature) continue;
        ++seen[family.signature];

        std::map<std::string, std::size_t> counts;
        for (const auto& sub : record->subrecords) {
          counts[sub.signature.to_string()]++;
        }

        for (const char* required : family.required) {
          if (counts[required] != 1) {
            failures.push_back(std::string{name} + " " + record->form_key.to_string() + " " +
                               family.signature + ": " + required + " appears " +
                               std::to_string(counts[required]) + " times, expected exactly 1");
          }
        }
        // Every subrecord in these families has max cardinality 1.
        for (const char* optional : family.optional) {
          if (counts[optional] > 1) {
            failures.push_back(std::string{name} + " " + record->form_key.to_string() + " " +
                               family.signature + ": " + optional + " appears " +
                               std::to_string(counts[optional]) + " times, expected at most 1");
          }
        }
        // FULL is a localized string ID in all three families.
        for (const auto& sub : record->subrecords) {
          if (sub.signature.to_string() != "FULL") continue;
          EXPECT_TRUE(fo4::schema::is_localized_field(signature, sub.signature))
              << family.signature << " FULL should be declared localized";
          if (plugin.header.tes4_flags.is_localized() && sub.data.size() != 4) {
            failures.push_back(std::string{name} + " " + record->form_key.to_string() + " " +
                               family.signature + ": FULL is " +
                               std::to_string(sub.data.size()) + " bytes, expected 4");
          }
        }
      }
    }
  }

  if (files == 0) GTEST_SKIP() << "no official masters found under " << dir;
  for (std::size_t i = 0; i < failures.size() && i < 25; ++i) ADD_FAILURE() << failures[i];
  EXPECT_TRUE(failures.empty()) << failures.size() << " record(s) violated the declared schema";

  EXPECT_GT(seen["PROJ"], 200u);
  EXPECT_GT(seen["EXPL"], 300u);
  EXPECT_GT(seen["HAZD"], 70u);
}
