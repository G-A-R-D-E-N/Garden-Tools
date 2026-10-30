// Decode every shipped PROJ/EXPL/HAZD payload and re-encode it. The bytes must
// come back identical.
//
// This is the strongest oracle the write path has. It does not prove the game
// accepts a record authored from scratch -- nothing here does -- but it does
// prove the encoder's field order, widths and offsets match what Bethesda
// actually wrote, across every record they shipped. A single transposed field
// or wrong width shows up as a mismatch.
//
// Gated on FO4_DATA_DIR; CI has no game files and will skip.

#include "fo4/forms/proj_expl_hazd.hpp"
#include "fo4/plugin/reader.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace {

const fo4::Subrecord* find_subrecord(const fo4::Record& record, std::string_view signature) {
  for (const auto& sub : record.subrecords) {
    if (sub.signature.to_string() == signature) return &sub;
  }
  return nullptr;
}

}  // namespace

TEST(ProjExplHazdReencodeRealData, ReproducesEveryShippedPayloadByteForByte) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to the Fallout 4 Data folder to run this";
  }
  record_game_build(detect_game_build(dir));

  std::map<std::string, std::size_t> seen;
  std::vector<std::string> failures;
  int files = 0;

  for (const char* name : {"Fallout4.esm", "DLCCoast.esm", "DLCNukaWorld.esm", "DLCRobot.esm",
                           "DLCworkshop01.esm", "DLCworkshop02.esm", "DLCworkshop03.esm"}) {
    const auto path = std::filesystem::path{dir} / name;
    if (!std::filesystem::exists(path)) continue;
    ++files;
    auto plugin = fo4::read_plugin_file(path);

    for (const auto* record : fo4::collect_records(plugin)) {
      const auto signature = record->header.signature.to_string();
      const auto key = record->form_key;

      auto compare = [&](std::string_view subrecord, const std::vector<std::byte>& encoded) {
        const auto* original = find_subrecord(*record, subrecord);
        if (original == nullptr) return;
        ++seen[signature];

        // EXPL also ships truncated at 52/56/60/64 bytes. Creation always emits
        // the current 84-byte shape, so for a legacy body the claim is that the
        // re-encoded prefix matches -- the tail it grew is new, not wrong.
        const auto span = original->data.size();
        if (encoded.size() < span) {
          failures.push_back(std::string{name} + " " + key.to_string() + " " + signature + " " +
                             std::string{subrecord} + ": re-encoded " +
                             std::to_string(encoded.size()) + " bytes, original is " +
                             std::to_string(span));
          return;
        }
        for (std::size_t i = 0; i < span; ++i) {
          if (encoded[i] != original->data[i]) {
            failures.push_back(std::string{name} + " " + key.to_string() + " " + signature + " " +
                               std::string{subrecord} + ": byte " + std::to_string(i) +
                               " differs after re-encode");
            return;
          }
        }
      };

      if (signature == "PROJ") {
        const auto decoded = fo4::forms::decode_projectile(plugin, key);
        compare("DNAM", fo4::forms::encode_projectile_dnam(plugin, decoded));
      } else if (signature == "EXPL") {
        const auto decoded = fo4::forms::decode_explosion(plugin, key);
        compare("DATA", fo4::forms::encode_explosion_data(plugin, decoded));
      } else if (signature == "HAZD") {
        const auto decoded = fo4::forms::decode_hazard(plugin, key);
        compare("DNAM", fo4::forms::encode_hazard_dnam(plugin, decoded));
      }
    }
  }

  if (files == 0) GTEST_SKIP() << "no official masters found under " << dir;

  for (std::size_t i = 0; i < failures.size() && i < 25; ++i) ADD_FAILURE() << failures[i];
  EXPECT_TRUE(failures.empty()) << failures.size() << " payload(s) did not survive a re-encode";

  // Counts measured from these files. A sharp drop means the walk stopped
  // finding records and the test silently proved nothing.
  EXPECT_GE(seen["PROJ"], 200u);
  EXPECT_GE(seen["EXPL"], 300u);
  EXPECT_GE(seen["HAZD"], 70u);
}
