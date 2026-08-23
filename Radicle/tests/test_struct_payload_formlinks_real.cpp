// The PROJ/EXPL/HAZD payload FormID slots, checked against shipped data.
//
// These offsets come from CommonLibF4's runtime structs. The check that makes
// them trustworthy is semantic: every slot must resolve to the record type the
// struct says it holds. A wrong offset would land on a float or count and
// resolve to nothing, or to an incoherent mix of types.
//
// Gated on FO4_DATA_DIR; CI has no game files and will skip.

#include "fo4/plugin/reader.hpp"
#include "fo4/schema/form_links.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <map>
#include <set>
#include <string>

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
      {"PROJ", "DNAM", 16, {"LIGH"}},
      {"PROJ", "DNAM", 20, {"LIGH"}},
      {"PROJ", "DNAM", 32, {"EXPL"}},
      {"PROJ", "DNAM", 36, {"SNDR"}},
      {"PROJ", "DNAM", 84, {"COLL"}},
      {"HAZD", "DNAM", 24, {"SPEL", "ENCH"}},
  };
  return value;
}

}  // namespace

TEST(StructPayloadFormLinksRealData, EverySlotResolvesToItsDocumentedType) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to the Fallout 4 Data folder to run this";
  }
  const auto path = std::filesystem::path{dir} / "Fallout4.esm";
  if (!std::filesystem::exists(path)) {
    GTEST_SKIP() << "Fallout4.esm not found under " << dir;
  }

  const auto plugin = fo4::read_plugin_file(path);

  // FormKey -> record signature, for resolving what each slot names.
  std::map<std::string, std::string> kinds;
  for (const auto* record : fo4::collect_records(plugin)) {
    kinds[record->form_key.to_string()] = record->header.signature.to_string();
  }

  std::map<std::string, std::size_t> resolved;
  std::vector<std::string> wrong;

  for (const auto& hit : fo4::collect_form_links(plugin)) {
    // A zero slot is an absent link, not a target. Its FormKey is empty, which
    // would otherwise collide with the TES4 record's empty key.
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

  for (std::size_t i = 0; i < wrong.size() && i < 20; ++i) ADD_FAILURE() << wrong[i];
  EXPECT_TRUE(wrong.empty()) << wrong.size() << " slot(s) resolved to an unexpected type";

  // Every slot must actually have been exercised, or the test proves nothing.
  for (const auto& slot : slots()) {
    const auto key = std::string{slot.record} + " " + slot.subrecord + "+" +
                     std::to_string(slot.offset);
    EXPECT_GT(resolved[key], 0u) << key << " was never exercised";
  }
}
