#include "fo4/forms/perk.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace {

using Payload = std::vector<std::pair<std::string, std::vector<std::byte>>>;

Payload subrecord_payload(const fo4::Record& record) {
  Payload payload;
  for (const auto& subrecord : record.subrecords) {
    payload.emplace_back(subrecord.signature.to_string(), subrecord.data);
  }
  return payload;
}

}  // namespace

TEST(PerkEffectsReal, SetPerkEffectsRewritesEveryFalloutEsmPerkByteForByte) {
  const auto* directory = std::getenv("FO4_DATA_DIR");
  if (directory == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to run the official-master PERK effect round-trip";
  }
  record_game_build(detect_game_build(directory));
  const auto original = fo4::read_plugin_file(std::filesystem::path{directory} / "Fallout4.esm");

  std::vector<fo4::FormKey> perks;
  fo4::visit_records(original, [&](const fo4::Record& record) {
    if (record.header.signature.to_string() == "PERK") perks.push_back(record.form_key);
  });
  ASSERT_FALSE(perks.empty());

  auto copy = original;
  for (const auto& key : perks) {
    const auto* source = fo4::find_record(copy, key);
    ASSERT_NE(source, nullptr) << key.to_string();
    const auto effects = fo4::forms::read_perk_effects(copy, *source);
    ASSERT_EQ(fo4::forms::set_perk_effects(copy, key, effects),
              fo4::forms::PerkEffectWrite::written)
        << key.to_string();
    const auto* rewritten = fo4::find_record(copy, key);
    const auto* expected = fo4::find_record(original, key);
    ASSERT_NE(rewritten, nullptr) << key.to_string();
    ASSERT_NE(expected, nullptr) << key.to_string();
    EXPECT_EQ(subrecord_payload(*rewritten), subrecord_payload(*expected)) << key.to_string();
  }
}
