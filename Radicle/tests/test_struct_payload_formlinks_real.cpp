// The PROJ/EXPL/HAZD payload FormID slots, checked against shipped data.
//
// The field names come from CommonLibF4's runtime structs; the offsets are
// measured from the files, because the disk layout is not the memory layout
// (see docs/FORMAT_NOTES.md). The check that makes them trustworthy is
// semantic: every slot must resolve to the record type the struct says it
// holds. A wrong offset would land on a float or count and resolve to
// nothing, or to an incoherent mix of types.
//
// Several slots are sparse -- HAZD DNAM+36 is non-zero in exactly one shipped
// record, and that record is in DLCRobot -- so this walks every master and DLC
// rather than Fallout4.esm alone. A slot that is never exercised fails.
//
// Gated on FO4_DATA_DIR; CI has no game files and will skip.

#include "fo4/plugin/reader.hpp"
#include "fo4/schema/form_links.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

struct Slot {
  const char* record;
  const char* subrecord;
  std::size_t offset;
  std::set<std::string> expected;  // record types the slot may name
};

const std::vector<Slot>& slots() {
  static const std::vector<Slot> value{
      {"EXPL", "DATA", 0, {"LIGH"}},
      {"EXPL", "DATA", 4, {"SNDR"}},
      {"EXPL", "DATA", 8, {"SNDR"}},
      {"EXPL", "DATA", 12, {"IPDS"}},
      // Placed Object names any placeable base -- observed NPC_, LVLI, LIGH,
      // MSTT, HAZD and more -- so the discriminating check here is that it
      // resolves to a real record at all. An empty set means "any known type".
      {"EXPL", "DATA", 16, {}},
      {"EXPL", "DATA", 20, {"PROJ"}},
      {"PROJ", "DNAM", 16, {"LIGH"}},
      {"PROJ", "DNAM", 20, {"LIGH"}},
      {"PROJ", "DNAM", 32, {"EXPL"}},
      {"PROJ", "DNAM", 36, {"SNDR"}},
      {"PROJ", "DNAM", 52, {"SNDR"}},
      {"PROJ", "DNAM", 56, {"SNDR"}},
      {"PROJ", "DNAM", 60, {"WEAP"}},
      {"PROJ", "DNAM", 80, {"TXST"}},
      {"PROJ", "DNAM", 84, {"COLL"}},
      // vatsProjectile: the last field of a 93-byte DNAM, and unaligned.
      {"PROJ", "DNAM", 89, {"PROJ"}},
      {"HAZD", "DNAM", 24, {"SPEL", "ENCH"}},
      {"HAZD", "DNAM", 28, {"LIGH"}},
      {"HAZD", "DNAM", 32, {"IPDS"}},
      {"HAZD", "DNAM", 36, {"SNDR"}},
  };
  return value;
}

}  // namespace

TEST(StructPayloadFormLinksRealData, EverySlotResolvesToItsDocumentedType) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to the Fallout 4 Data folder to run this";
  }
  record_game_build(detect_game_build(dir));
  constexpr const char* k_masters[] = {"Fallout4.esm",      "DLCCoast.esm",
                                       "DLCNukaWorld.esm",  "DLCRobot.esm",
                                       "DLCworkshop01.esm", "DLCworkshop02.esm",
                                       "DLCworkshop03.esm"};
  std::vector<std::filesystem::path> present;
  std::vector<std::string> missing;
  for (const char* name : k_masters) {
    auto path = std::filesystem::path{dir} / name;
    if (!std::filesystem::exists(path)) {
      missing.push_back(name);
      continue;
    }
    present.push_back(std::move(path));
  }
  if (present.empty()) {
    GTEST_SKIP() << "no official masters found under " << dir;
  }

  // Two passes, one plugin resident at a time. Holding all seven parsed at
  // once costs several gigabytes -- Fallout4.esm is 330 MB on disk and larger
  // in memory -- and nothing here needs two files side by side. The only
  // cross-file state is this signature index, which is small.
  //
  // Pass one: FormKey -> record signature, so a DLC record can name a
  // Fallout4.esm target.
  std::map<std::string, std::string> kinds;
  for (const auto& path : present) {
    auto plugin = fo4::read_plugin_file(path);
    for (const auto* record : fo4::collect_records(plugin)) {
      kinds[record->form_key.to_string()] = record->header.signature.to_string();
    }
  }

  std::map<std::string, std::size_t> resolved;
  std::vector<std::string> wrong;

  // Pass two: the links themselves, again one file at a time.
  for (const auto& path : present) {
    auto plugin = fo4::read_plugin_file(path);
    for (const auto& hit : fo4::collect_form_links(plugin)) {
      // A zero slot is an absent link, not a target. Its FormKey is empty,
      // which would otherwise collide with the TES4 record's empty key.
      if (hit.raw.is_null()) continue;
      const auto record = kinds.count(hit.record.to_string())
                              ? kinds[hit.record.to_string()] : std::string{};
      for (const auto& slot : slots()) {
        if (record != slot.record) continue;
        if (hit.subrecord.to_string() != slot.subrecord) continue;
        if (hit.offset != slot.offset) continue;

        const auto key = std::string{slot.record} + " " + slot.subrecord + "+" +
                         std::to_string(slot.offset);
        const auto target = kinds.count(hit.key.to_string())
                                ? kinds[hit.key.to_string()] : std::string{};
        if (target.empty()) continue;  // points into a master we did not index
        ++resolved[key];
        if (!slot.expected.empty() && !slot.expected.count(target)) {
          wrong.push_back(key + " resolved to " + target + " (" + hit.key.to_string() + ")");
        }
      }
    }
  }

  for (std::size_t i = 0; i < wrong.size() && i < 20; ++i) ADD_FAILURE() << wrong[i];
  EXPECT_TRUE(wrong.empty()) << wrong.size() << " slot(s) resolved to an unexpected type";

  // Every slot must actually have been exercised, or the test proves nothing.
  // Some slots are attested only in a DLC -- HAZD DNAM+36 lives in exactly one
  // record and that record is in DLCRobot -- so a base-game-only install
  // cannot cover the table. There the type check above still runs; only this
  // coverage claim is withheld, because absence of the file is not evidence
  // that the slot is wrong.
  if (!missing.empty()) {
    GTEST_SKIP() << "coverage of every slot needs all seven masters; missing "
                 << missing.size() << " (" << missing.front() << (missing.size() > 1 ? ", ..." : "")
                 << ")";
  }
  for (const auto& slot : slots()) {
    const auto key = std::string{slot.record} + " " + slot.subrecord + "+" +
                     std::to_string(slot.offset);
    EXPECT_GT(resolved[key], 0u) << key << " was never exercised";
  }
}
