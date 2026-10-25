#include "fo4/plugin/vmad_writer.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/vmad.hpp"
#include "plugin_bytes.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <set>

namespace {

std::vector<const fo4::Record*> records(const fo4::Plugin& plugin) {
  std::vector<const fo4::Record*> result;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    if (&record != &plugin.tes4) result.push_back(&record);
  });
  return result;
}

class VmadWriterOfficial : public ::testing::TestWithParam<const char*> {};

TEST_P(VmadWriterOfficial, PropertyOrderAndCappedMutationPreserveOtherRecords) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR for read-only official coverage";
  record_game_build(detect_game_build(dir));
  const auto path = std::filesystem::path(dir) / GetParam();
  ASSERT_TRUE(std::filesystem::exists(path));
  auto plugin = fo4::read_plugin_file(path);
  std::set<fo4::FormKey> changed;
  std::map<fo4::FormKey, std::vector<fo4::Subrecord>> expected;
  std::array<bool, 5> edited{};
  std::size_t checked = 0, opaque = 0;
  for (const auto* record : records(plugin)) {
    for (const auto& sub : record->subrecords) {
      if (sub.signature.to_string() != "VMAD") continue;
      ++checked;
      const auto coverage = fo4::vmad_coverage(sub.data, record->header.signature);
      if (!coverage.complete()) { ++opaque; continue; }
      const auto scripts = fo4::read_vmad_scripts(sub.data);
      if ((scripts.version != 5 && scripts.version != 6) ||
          (scripts.object_format != 1 && scripts.object_format != 2)) continue;
      std::size_t previous_end = 6;
      for (std::size_t si = 0; si < scripts.scripts.size(); ++si) {
        for (std::size_t pi = 0; pi < scripts.scripts[si].properties.size(); ++pi) {
          const auto& property = scripts.scripts[si].properties[pi];
          EXPECT_GE(property.value_offset, previous_end);
          EXPECT_LE(property.value_offset + property.value_size, sub.data.size());
          previous_end = property.value_offset + property.value_size;
          const bool array = property.type >= 11 && property.type <= 15;
          const auto type = array ? property.type - 10 : property.type;
          if (type < 1 || type > 5 || edited[static_cast<std::size_t>(type - 1)] ||
              (array && property.element_count == 0) || changed.contains(record->form_key)) continue;
          fo4::VmadValue value;
          if (type == 1) value = fo4::VmadObjectValue{fo4::FormKey{}, -1};
          if (type == 2) value = std::string("Radicle writer official proof");
          if (type == 3) value = std::int32_t{-2147483647};
          if (type == 4) value = 1.25f;
          if (type == 5) {
            const auto at = property.value_offset + (array ? 4 : 0);
            if (std::to_integer<std::uint8_t>(sub.data[at]) > 1) continue;
            value = sub.data[at] == std::byte{0};
          }
          const auto before = sub.data;
          const auto status = fo4::set_vmad_property(plugin, record->form_key, si, pi, value);
          if (status != fo4::ScalarMutationStatus::updated) continue;
          edited[static_cast<std::size_t>(type - 1)] = true;
          changed.insert(record->form_key);
          expected.emplace(record->form_key, record->subrecords);
          const auto updated = fo4::read_vmad_scripts(sub.data).scripts[si].properties[pi];
          const auto at = property.value_offset + (array ? 4 : 0);
          const auto updated_at = updated.value_offset + (array ? 4 : 0);
          const auto old_width = type == 2 ? (array ? 2u + std::to_integer<unsigned>(before[at]) +
            (std::to_integer<unsigned>(before[at + 1]) << 8) : property.value_size) : type == 1 ? 8u : type == 5 ? 1u : 4u;
          const auto new_width = type == 2 ? 2u + std::get<std::string>(value).size() : old_width;
          EXPECT_EQ(std::vector<std::byte>(before.begin(), before.begin() + at),
                    std::vector<std::byte>(sub.data.begin(), sub.data.begin() + updated_at));
          EXPECT_EQ(std::vector<std::byte>(before.begin() + at + old_width, before.end()),
                    std::vector<std::byte>(sub.data.begin() + updated_at + new_width, sub.data.end()));
          break;
        }
      }
    }
  }
  ASSERT_GT(checked, 0u);
  ASSERT_FALSE(changed.empty());
  const auto saved = fo4::write_plugin(plugin);
  const auto reparsed = fo4::read_plugin(saved, GetParam());
  std::map<fo4::FormKey, const fo4::Record*> after;
  for (const auto* record : records(reparsed)) after.emplace(record->form_key, record);
  for (const auto* record : records(plugin)) {
    const auto it = after.find(record->form_key);
    ASSERT_NE(it, after.end());
    if (!changed.contains(record->form_key)) {
      EXPECT_EQ(fo4::test::record_original_bytes(plugin, *record),
                fo4::test::record_original_bytes(reparsed, *it->second));
    } else {
      const auto& subs = expected.at(record->form_key);
      ASSERT_EQ(it->second->subrecords.size(), subs.size());
      for (std::size_t i = 0; i < subs.size(); ++i) {
        EXPECT_EQ(it->second->subrecords[i].signature, subs[i].signature);
        EXPECT_EQ(it->second->subrecords[i].data, subs[i].data);
      }
    }
  }
  RecordProperty("vmad_records_checked", checked);
  RecordProperty("vmad_opaque_preserved", opaque);
  RecordProperty("records_mutated", changed.size());
}

TEST(VmadWriterOfficial, AddsAScriptToAVanillaActiAndQuestOverrideAndRereads) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR for read-only official coverage";
  record_game_build(detect_game_build(dir));
  const auto path = std::filesystem::path(dir) / "Fallout4.esm";
  ASSERT_TRUE(std::filesystem::exists(path));
  auto plugin = fo4::read_plugin_file(path);
  const auto has_vmad = [](const fo4::Record& record) {
    return std::any_of(record.subrecords.begin(), record.subrecords.end(),
                       [](const fo4::Subrecord& sub) { return sub.signature.to_string() == "VMAD"; });
  };
  const auto complete_vmad = [](const fo4::Record& record, fo4::Signature signature) {
    for (const auto& sub : record.subrecords) {
      if (sub.signature.to_string() == "VMAD") {
        return fo4::vmad_coverage(sub.data, signature).complete();
      }
    }
    return false;
  };
  const auto acti_signature = fo4::Signature::from_string("ACTI");
  const fo4::Record* acti = nullptr;
  const fo4::Record* quest = nullptr;
  fo4::visit_records(plugin, [&](fo4::Record& record) {
    if (acti == nullptr && record.header.signature == acti_signature && !has_vmad(record)) {
      acti = &record;
    }
    if (quest == nullptr && record.header.signature == fo4::sig_qust &&
        complete_vmad(record, fo4::sig_qust)) {
      quest = &record;
    }
  });
  ASSERT_NE(acti, nullptr);
  ASSERT_NE(quest, nullptr);
  struct Target {
    const fo4::Record* record;
    const char* script;
    bool needs_vmad;
  };
  const Target targets[] = {{acti, "RadicleCAbiActi", true}, {quest, "RadicleCAbiQuest", false}};
  for (const auto& target : targets) {
    const auto key = target.record->form_key;
    std::size_t index = 0;
    if (target.needs_vmad) {
      ASSERT_EQ(fo4::add_vmad(plugin, key), fo4::VmadStructureMutationStatus::updated);
    } else {
      for (const auto& sub : target.record->subrecords) {
        if (sub.signature.to_string() != "VMAD") continue;
        index = fo4::read_vmad_scripts(sub.data).scripts.size();
      }
    }
    ASSERT_EQ(fo4::add_vmad_script(plugin, key, index, target.script, 1),
              fo4::VmadStructureMutationStatus::updated);
  }
  const auto saved = fo4::write_plugin(plugin);
  auto reparsed = fo4::read_plugin(saved, "Fallout4.esm");
  for (const auto& target : targets) {
    const auto* after = fo4::find_record(reparsed, target.record->form_key);
    ASSERT_NE(after, nullptr);
    const fo4::Subrecord* vmad = nullptr;
    for (const auto& sub : after->subrecords) {
      if (sub.signature.to_string() == "VMAD") vmad = &sub;
    }
    ASSERT_NE(vmad, nullptr);
    const auto scripts = fo4::read_vmad_scripts(vmad->data);
    EXPECT_TRUE(std::any_of(scripts.scripts.begin(), scripts.scripts.end(),
                            [&](const fo4::VmadScript& script) { return script.name == target.script; }));
    EXPECT_EQ(after->subrecords.size(), target.record->subrecords.size() + (target.needs_vmad ? 1 : 0));
    std::size_t other = 0;
    for (const auto& sub : after->subrecords) {
      if (sub.signature.to_string() == "VMAD") continue;
      ASSERT_LT(other, target.record->subrecords.size());
      EXPECT_EQ(sub.signature, target.record->subrecords[other].signature);
      EXPECT_EQ(sub.data, target.record->subrecords[other].data);
      ++other;
    }
  }
}

INSTANTIATE_TEST_SUITE_P(Official, VmadWriterOfficial, ::testing::Values(
  "Fallout4.esm", "DLCRobot.esm", "DLCworkshop01.esm", "DLCCoast.esm",
  "DLCworkshop02.esm", "DLCworkshop03.esm", "DLCNukaWorld.esm"));

}
