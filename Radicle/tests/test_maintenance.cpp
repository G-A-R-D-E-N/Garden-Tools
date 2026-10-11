#include "fo4/plugin/maintenance.hpp"
#include "fo4/plugin/reader.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <initializer_list>
#include <string_view>
#include <vector>

namespace {

struct RecordSpec {
  std::string_view signature;
  std::uint32_t form_id;
  std::string_view editor_id;
  std::uint32_t keyword{0};
  std::uint32_t flags{0};
};

fo4::Plugin make_plugin(std::string_view name,
                        std::initializer_list<std::string_view> masters,
                        const std::vector<RecordSpec>& records,
                        float version = 1.0f) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(version);
  hedr.u32(static_cast<std::uint32_t>(records.size()));
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::ByteWriter data;
  data.u32(0);
  data.u32(0);
  for (const auto master : masters) {
    std::vector<std::byte> bytes;
    for (const char ch : master) bytes.push_back(std::byte{static_cast<std::uint8_t>(ch)});
    bytes.push_back(std::byte{0});
    fo4::test::write_subrecord(out, "MAST", bytes);
    fo4::test::write_subrecord(out, "DATA", data.bytes);
  }
  fo4::test::end_record(out, tes4);
  for (const auto& spec : records) {
    const auto group = fo4::test::begin_group(out, spec.signature, 0);
    const auto record = fo4::test::begin_record(out, spec.signature, spec.flags, spec.form_id);
    fo4::test::write_subrecord(out, "EDID", spec.editor_id);
    if (spec.keyword != 0) {
      fo4::test::ByteWriter ksiz;
      ksiz.u32(1);
      fo4::test::write_subrecord(out, "KSIZ", ksiz.bytes);
      fo4::test::ByteWriter kwda;
      kwda.u32(spec.keyword);
      fo4::test::write_subrecord(out, "KWDA", kwda.bytes);
    }
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
  }
  return fo4::read_plugin(out.bytes, std::string(name));
}

}  // namespace

TEST(Maintenance, FindsIdenticalOverrideAndIgnoresChangedOne) {
  const auto base = make_plugin("Base.esm", {}, {{"KYWD", 0x000800, std::string_view{"Kw\0", 3}},
                                                 {"KYWD", 0x000801, std::string_view{"Other\0", 6}}});
  const auto patch = make_plugin("Patch.esp", {"Base.esm"},
                                 {{"KYWD", 0x00000800, std::string_view{"Kw\0", 3}},
                                  {"KYWD", 0x00000801, std::string_view{"Changed\0", 8}}});
  const std::vector<const fo4::Plugin*> load_order{&base, &patch};
  const auto itm = fo4::find_identical_to_master(load_order, 1);
  ASSERT_EQ(itm.size(), 1u);
  EXPECT_EQ(itm[0].key, fo4::FormKey::parse("000800:Base.esm"));
  EXPECT_EQ(itm[0].previous.filename(), "Base.esm");
}

TEST(Maintenance, IdenticalAcrossDifferentMasterOrder) {
  const auto base = make_plugin("Base.esm", {}, {{"KYWD", 0x000800, std::string_view{"Kw\0", 3}}});
  const auto mod = make_plugin("Mod.esp", {"Base.esm"},
                               {{"MISC", 0x01000900, std::string_view{"Item\0", 5}, 0x00000800}});
  const auto patch = make_plugin("Patch.esp", {"Mod.esp", "Base.esm"},
                                 {{"MISC", 0x00000900, std::string_view{"Item\0", 5}, 0x01000800}});
  const std::vector<const fo4::Plugin*> load_order{&base, &mod, &patch};
  const auto itm = fo4::find_identical_to_master(load_order, 2);
  ASSERT_EQ(itm.size(), 1u);
  EXPECT_EQ(itm[0].key, fo4::FormKey::parse("000900:Mod.esp"));
  EXPECT_EQ(itm[0].previous.filename(), "Mod.esp");
}

TEST(Maintenance, CountsDeletedRecordsAndNavmeshes) {
  const auto plugin = make_plugin(
      "Dirty.esp", {}, {{"REFR", 0x000800, std::string_view{"A\0", 2}, 0, fo4::RecordFlags::deleted},
                        {"NAVM", 0x000801, std::string_view{"B\0", 2}, 0, fo4::RecordFlags::deleted},
                        {"MISC", 0x000802, std::string_view{"C\0", 2}}});
  const auto counts = fo4::count_deleted_records(plugin);
  EXPECT_EQ(counts.records, 2u);
  EXPECT_EQ(counts.references, 1u);
  EXPECT_EQ(counts.navmeshes, 1u);
}

TEST(Maintenance, EslEligibilityCountsNativeRecordsOutsideTheLightRange) {
  const auto plugin = make_plugin("Mod.esp", {"Base.esm"},
                                  {{"KYWD", 0x00000800, std::string_view{"Override\0", 9}},
                                   {"MISC", 0x01000900, std::string_view{"Small\0", 6}},
                                   {"MISC", 0x01001000, std::string_view{"Large\0", 6}}});
  const auto esl = fo4::esl_eligibility(plugin);
  EXPECT_EQ(esl.native_records, 2u);
  EXPECT_EQ(esl.out_of_range, 1u);
  EXPECT_EQ(esl.capacity, 0xFFEu);
  EXPECT_TRUE(esl.can_fit());
  EXPECT_FALSE(esl.ids_fit_light_range());
}

TEST(Maintenance, EslEligibilityFollowsTheXEditRecordLimitAndIdCeiling) {
  const auto native = [](std::uint32_t count, std::uint32_t first) {
    std::vector<RecordSpec> records;
    for (std::uint32_t i = 0; i < count; ++i) {
      records.push_back({"MISC", 0x01000000u | (first + i), std::string_view{"M\0", 2}});
    }
    return fo4::esl_eligibility(make_plugin("Mod.esp", {"Base.esm"}, records));
  };
  EXPECT_TRUE(native(2049, 0x1000).can_fit());
  EXPECT_TRUE(native(0xFFE, 0x1000).can_fit());
  EXPECT_FALSE(native(0xFFF, 0x1000).can_fit());

  const auto ceiling = native(1, 0xFFF);
  EXPECT_EQ(ceiling.out_of_range, 0u);
  EXPECT_TRUE(ceiling.ids_fit_light_range());
  const auto past = native(1, 0x1000);
  EXPECT_EQ(past.out_of_range, 1u);
  EXPECT_FALSE(past.ids_fit_light_range());
}

TEST(Maintenance, EslEligibilityKeepsTheFullFloorWithoutHedrOneAndAMaster) {
  const std::vector<RecordSpec> own{{"MISC", 0x00001000, std::string_view{"M\0", 2}}};
  EXPECT_EQ(fo4::esl_eligibility(make_plugin("Mod.esp", {}, own)).capacity, 0x800u);
  const std::vector<RecordSpec> mastered{{"MISC", 0x01001000, std::string_view{"M\0", 2}}};
  EXPECT_EQ(fo4::esl_eligibility(make_plugin("Mod.esp", {"Base.esm"}, mastered, 0.95f)).capacity, 0x800u);
  EXPECT_EQ(fo4::esl_eligibility(make_plugin("Mod.esp", {"Base.esm"}, mastered)).capacity, 0xFFEu);
}

TEST(Maintenance, EslEligibilityCountsNativeIdsBelowTheTargetFloor) {
  const auto masterless = [](std::uint32_t id) {
    return fo4::esl_eligibility(make_plugin("Mod.esp", {}, {{"MISC", id, std::string_view{"M\0", 2}}}));
  };
  EXPECT_EQ(masterless(0x7FF).out_of_range, 1u);
  EXPECT_EQ(masterless(0x7FF).capacity, 0x800u);
  EXPECT_EQ(masterless(0x800).out_of_range, 0u);
  EXPECT_EQ(masterless(0xFFF).out_of_range, 0u);
  EXPECT_EQ(masterless(0x1000).out_of_range, 1u);

  const auto mastered = [](std::uint32_t id, float version = 1.0f) {
    return fo4::esl_eligibility(
        make_plugin("Mod.esp", {"Base.esm"}, {{"MISC", 0x01000000u | id, std::string_view{"M\0", 2}}}, version));
  };
  EXPECT_EQ(mastered(0x000).out_of_range, 1u);
  EXPECT_EQ(mastered(0x001).out_of_range, 0u);
  EXPECT_EQ(mastered(0x7FF).out_of_range, 0u);
  EXPECT_EQ(mastered(0x7FF, 0.95f).out_of_range, 1u);
  EXPECT_EQ(mastered(0x800, 0.95f).out_of_range, 0u);
}
