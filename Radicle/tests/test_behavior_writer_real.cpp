#include "fo4/environment/behavior.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/behavior_layout.hpp"
#include "fo4/schema/field_address.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/preflight.hpp"
#include "fo4/schema/record_constraints.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <set>

namespace {
using Bytes = std::vector<std::byte>;
fo4::Signature sig(std::string_view value) { return fo4::Signature::from_string(value); }
bool behavior(fo4::Signature type) { return type == sig("PACK") || type == sig("CSTY") || type == sig("IDLE"); }
class BehaviorWriterReal : public testing::TestWithParam<const char*> {};

TEST_P(BehaviorWriterReal, EveryOfficialRecordHasOrderedKnownSectionsAndValidWriterShapes) {
  const auto* dir = std::getenv("FO4_DATA_DIR");
  if (!dir) GTEST_SKIP() << "set FO4_DATA_DIR to installed Fallout 4 data";
  record_game_build(detect_game_build(dir));
  const auto path = std::filesystem::path{dir} / GetParam();
  if (!std::filesystem::exists(path)) GTEST_SKIP() << "missing official master";
  const auto plugin = fo4::read_plugin_file(path);
  std::map<std::string, int> totals;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    if (!behavior(record.header.signature)) return;
    SCOPED_TRACE(record.form_key.to_string());
    ++totals[record.header.signature.to_string()];
    for (const auto& issue : fo4::schema::preflight_record(record)) {
      EXPECT_NE(issue.severity, fo4::schema::PreflightSeverity::error) << issue.path << ' ' << issue.message;
    }
    const auto rules = fo4::schema::authoring_subrecords(*fo4::schema::record_authoring_constraint(record.header.signature));
    std::size_t last_rule = 0;
    for (std::size_t rank = 0; rank < rules.size(); ++rank) {
      const auto count = static_cast<std::size_t>(std::count_if(record.subrecords.begin(),record.subrecords.end(),
        [&](const fo4::Subrecord& sub) { return sub.signature == rules[rank].signature; }));
      EXPECT_GE(count,rules[rank].minimum); EXPECT_LE(count,rules[rank].maximum);
    }
    for (const auto& sub : record.subrecords) {
      for (std::size_t rank = 0; rank < rules.size(); ++rank) {
        if (sub.signature != rules[rank].signature) continue;
        EXPECT_GE(rank,last_rule); last_rule = rank;
      }
    }
    if (record.header.signature == sig("PACK")) {
      const auto idle_slots = fo4::behavior_field_candidates(record, sig("INAM"), false);
      ASSERT_TRUE(idle_slots); EXPECT_EQ(idle_slots->size(), 3u);
      const std::vector<std::string> prefix{"EDID","VMAD","PKDT","PSDT","IDLF","IDLC","IDLT","IDLA","IDLB","CNAM","QNAM","PKCU"};
      std::size_t last = 0;
      for (const auto& sub : record.subrecords) {
        const auto tag = sub.signature.to_string();
        const auto found = std::find(prefix.begin(), prefix.end(), tag);
        if (found == prefix.end()) continue;
        const auto rank = static_cast<std::size_t>(found - prefix.begin());
        EXPECT_GE(rank, last); last = rank;
        if (tag == "PKCU") break;
      }
      for (const auto& sub : record.subrecords) {
        if (sub.signature == sig("PLDT") || sub.signature == sig("PTDA")) {
          EXPECT_TRUE(fo4::behavior_field_payload_valid(record, sub));
        }
      }
    }
    if (record.header.signature == sig("IDLE")) {
      bool body = false;
      for (const auto& sub : record.subrecords) {
        const auto tag = sub.signature.to_string();
        if (tag == "DNAM" || tag == "ENAM" || tag == "ANAM") body = true;
        if (tag == "CTDA" || tag == "CIS1" || tag == "CIS2") { EXPECT_FALSE(body); }
      }
    }
  });
  for (const auto& [type, count] : totals) RecordProperty(type, count);
}

TEST_P(BehaviorWriterReal, CappedMutationSweepReparsesAndPreservesEveryUntouchedRecordAndPayloadByte) {
  const auto* dir = std::getenv("FO4_DATA_DIR");
  if (!dir) GTEST_SKIP() << "set FO4_DATA_DIR to installed Fallout 4 data";
  record_game_build(detect_game_build(dir));
  const auto path = std::filesystem::path{dir} / GetParam();
  if (!std::filesystem::exists(path)) GTEST_SKIP() << "missing official master";
  auto plugin = fo4::read_plugin_file(path);
  const auto original = plugin;
  std::map<std::string, int> counts;
  std::map<fo4::FormKey, std::vector<Bytes>> expected;
  std::set<std::string> paths;
  int edits = 0;
  fo4::visit_records(original, [&](const fo4::Record& record) {
    if (!behavior(record.header.signature) || counts[record.header.signature.to_string()]++ >= 12) return;
    auto& want = expected[record.form_key];
    for (const auto& sub : record.subrecords) want.push_back(sub.data);
    for (const auto& field : fo4::schema::scalar_fields(record.header.signature)) {
      SCOPED_TRACE(record.form_key.to_string() + ' ' + std::string(field.path));
      const auto slots = fo4::schema::resolve_field_slots(record, fo4::schema::scalar_field_shape(field.scalar), field.scalar.address);
      std::set<std::size_t> picks;
      if (!slots.slots.empty()) picks = {0, slots.slots.size()-1};
      for (const auto index : picks) {
        const auto& slot = slots.slots[index];
        if (!slot.valid) continue;
        const bool string = field.scalar.storage == fo4::schema::ScalarStorage::zstring;
        const auto existing = fo4::schema::scalar_slot_bytes(record.subrecords[slot.subrecord].data, slot, field.scalar);
        const auto value = string ? "radicle_edit" : fo4::schema::encode_scalar("1", field.scalar) == Bytes(existing.begin(),existing.end()) ? "0" : "1";
        const auto status = string ? fo4::set_record_string(plugin,record.form_key,field,index,value)
                                   : fo4::set_record_scalar(plugin,record.form_key,field,index,value);
        ASSERT_EQ(status,fo4::ScalarMutationStatus::updated);
        const auto encoded = fo4::schema::encode_scalar(value,field.scalar);
        if (string) want[slot.subrecord] = encoded;
        else std::copy(encoded.begin(),encoded.end(),want[slot.subrecord].begin()+static_cast<std::ptrdiff_t>(slot.offset));
        paths.emplace(field.path); ++edits;
      }
    }
    for (const auto& field : fo4::schema::formlink_fields(record.header.signature)) {
      if (!field.nullable) continue;
      const fo4::schema::FieldShape shape{field.subrecord,field.offset,4,0,false};
      const auto slots = fo4::schema::resolve_field_slots(record,shape,field.address);
      for (std::size_t index = 0; index < slots.slots.size(); ++index) {
        const auto& slot = slots.slots[index];
        if (!slot.valid) continue;
        const auto status = fo4::set_record_formlink_field(plugin,record.form_key,field,index,{});
        EXPECT_TRUE(status == fo4::FormLinkFieldMutationStatus::updated || status == fo4::FormLinkFieldMutationStatus::unchanged);
        std::fill_n(want[slot.subrecord].begin()+static_cast<std::ptrdiff_t>(slot.offset),4,std::byte{0});
        paths.emplace(field.path); ++edits;
      }
    }
  });
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin),path);
  std::map<fo4::FormKey,const fo4::Record*> reparsed;
  fo4::visit_records(reread,[&](const fo4::Record& record) {
    if (record.header.signature != sig("TES4")) reparsed.emplace(record.form_key,&record);
  });
  int unchanged = 0;
  fo4::visit_records(original,[&](const fo4::Record& before) {
    if (before.header.signature == sig("TES4")) return;
    const auto match = reparsed.find(before.form_key);
    ASSERT_NE(match,reparsed.end());
    const auto* after = match->second;
    const auto found = expected.find(before.form_key);
    if (found == expected.end()) {
      EXPECT_EQ(fo4::test::record_original_bytes(original,before),fo4::test::record_original_bytes(reread,*after));
      ++unchanged; return;
    }
    ASSERT_EQ(after->subrecords.size(),found->second.size());
    for (std::size_t i = 0; i < after->subrecords.size(); ++i) {
      EXPECT_EQ(after->subrecords[i].signature,before.subrecords[i].signature);
      EXPECT_EQ(after->subrecords[i].data,found->second[i]);
    }
  });
  RecordProperty("edited_records",static_cast<int>(expected.size()));
  RecordProperty("edits",edits); RecordProperty("paths",static_cast<int>(paths.size()));
  RecordProperty("untouched_records",unchanged);
}

INSTANTIATE_TEST_SUITE_P(OfficialMasters,BehaviorWriterReal,
  testing::Values("Fallout4.esm","DLCRobot.esm","DLCworkshop01.esm","DLCCoast.esm",
                  "DLCworkshop02.esm","DLCworkshop03.esm","DLCNukaWorld.esm"));
}
