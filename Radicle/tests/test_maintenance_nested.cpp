#include "fo4/plugin/maintenance.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string_view>
#include <vector>

namespace {

struct Nested {
  std::string_view parent;
  std::int32_t children_type;
  std::int32_t inner_type;
  std::string_view child;
  std::string_view child_edid;
};

fo4::Plugin make_nested(std::string_view name, bool with_master, const Nested& spec) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  if (with_master) {
    fo4::test::ByteWriter data;
    data.u32(0);
    data.u32(0);
    fo4::test::write_subrecord(out, "MAST", std::string_view{"Base.esm\0", 9});
    fo4::test::write_subrecord(out, "DATA", data.bytes);
  }
  fo4::test::end_record(out, tes4);
  const auto top = fo4::test::begin_group(out, spec.parent, 0);
  const auto parent = fo4::test::begin_record(out, spec.parent, 0, 0x00000800);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Parent\0", 7});
  fo4::test::end_record(out, parent);
  const auto children = fo4::test::begin_group(out, spec.parent, spec.children_type);
  out.patch_u32(children + 8, 0x800);
  const auto inner = spec.inner_type < 0 ? 0 : fo4::test::begin_group(out, spec.parent, spec.inner_type);
  if (spec.inner_type >= 0) out.patch_u32(inner + 8, 0x800);
  const auto child = fo4::test::begin_record(out, spec.child, 0, 0x00000801);
  fo4::test::write_subrecord(out, "EDID", spec.child_edid);
  fo4::test::end_record(out, child);
  if (spec.inner_type >= 0) fo4::test::end_group(out, inner);
  fo4::test::end_group(out, children);
  fo4::test::end_group(out, top);
  return fo4::read_plugin(out.bytes, std::string(name));
}

constexpr Nested k_cell{"CELL", 6, 9, "REFR", std::string_view{"Ref\0", 4}};
constexpr Nested k_topic{"DIAL", 7, -1, "INFO", std::string_view{"Info\0", 5}};

std::vector<fo4::FormKey> itm_keys(const fo4::Plugin& base, const fo4::Plugin& patch) {
  const std::vector<const fo4::Plugin*> load_order{&base, &patch};
  std::vector<fo4::FormKey> keys;
  for (const auto& item : fo4::find_identical_to_master(load_order, 1)) keys.push_back(item.key);
  return keys;
}

}  // namespace

TEST(MaintenanceNested, ItmReferenceAndItsCellAreBothFoundChildFirst) {
  const auto base = make_nested("Base.esm", false, k_cell);
  const auto patch = make_nested("Patch.esp", true, k_cell);
  EXPECT_EQ(itm_keys(base, patch), (std::vector<fo4::FormKey>{
                                       fo4::FormKey::parse("000801:Base.esm"),
                                       fo4::FormKey::parse("000800:Base.esm")}));
}

TEST(MaintenanceNested, ItmInfoAndItsTopicAreBothFound) {
  const auto base = make_nested("Base.esm", false, k_topic);
  const auto patch = make_nested("Patch.esp", true, k_topic);
  EXPECT_EQ(itm_keys(base, patch), (std::vector<fo4::FormKey>{
                                       fo4::FormKey::parse("000801:Base.esm"),
                                       fo4::FormKey::parse("000800:Base.esm")}));
}

TEST(MaintenanceNested, ChangedReferenceKeepsItsIdenticalCell) {
  const auto base = make_nested("Base.esm", false, k_cell);
  auto changed = k_cell;
  changed.child_edid = std::string_view{"Moved\0", 6};
  const auto patch = make_nested("Patch.esp", true, changed);
  EXPECT_TRUE(itm_keys(base, patch).empty());
}

TEST(MaintenanceNested, RemoveRecordsDropsNestedItmAndTheGroupsItEmptied) {
  const auto base = make_nested("Base.esm", false, k_cell);
  auto patch = make_nested("Patch.esp", true, k_cell);
  const auto keys = itm_keys(base, patch);
  ASSERT_EQ(fo4::remove_records(patch, keys), 2u);
  EXPECT_TRUE(patch.groups.empty());
  const auto reread = fo4::read_plugin(fo4::write_plugin(patch), "Patch.esp");
  EXPECT_TRUE(reread.groups.empty());
}

TEST(MaintenanceNested, RemoveRecordsKeepsParentWhoseChildStays) {
  auto patch = make_nested("Patch.esp", true, k_cell);
  const std::vector<fo4::FormKey> keys{fo4::FormKey::parse("000801:Base.esm")};
  ASSERT_EQ(fo4::remove_records(patch, keys), 1u);
  const auto reread = fo4::read_plugin(fo4::write_plugin(patch), "Patch.esp");
  EXPECT_NE(fo4::find_record(reread, fo4::FormKey::parse("000800:Base.esm")), nullptr);
  EXPECT_EQ(fo4::find_record(reread, fo4::FormKey::parse("000801:Base.esm")), nullptr);
}

TEST(MaintenanceNested, RemoveRecordsDropsEmptyChildGroupOfTheParentItRemoves) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  const auto top = fo4::test::begin_group(out, "CELL", 0);
  const auto parent = fo4::test::begin_record(out, "CELL", 0, 0x00000800);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Parent\0", 7});
  fo4::test::end_record(out, parent);
  const auto children = fo4::test::begin_group(out, "CELL", 6);
  out.patch_u32(children + 8, 0x800);
  fo4::test::end_group(out, children);
  fo4::test::end_group(out, top);
  auto base = fo4::read_plugin(out.bytes, "Base.esm");
  const std::vector<fo4::FormKey> keys{fo4::FormKey::parse("000800:Base.esm")};
  ASSERT_EQ(fo4::remove_records(base, keys), 1u);
  EXPECT_TRUE(base.groups.empty());
  const auto reread = fo4::read_plugin(fo4::write_plugin(base), "Base.esm");
  EXPECT_TRUE(reread.groups.empty());
}

namespace {

fo4::Plugin two_parents(std::string_view name, bool with_master, std::string_view parent, std::int32_t children_type,
                        std::string_view child, std::uint32_t child_parent) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(3);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  if (with_master) {
    fo4::test::ByteWriter data;
    data.u32(0);
    data.u32(0);
    fo4::test::write_subrecord(out, "MAST", std::string_view{"Base.esm\0", 9});
    fo4::test::write_subrecord(out, "DATA", data.bytes);
  }
  fo4::test::end_record(out, tes4);
  const auto top = fo4::test::begin_group(out, parent, 0);
  for (const std::uint32_t id : {0x800u, 0x802u}) {
    const auto record = fo4::test::begin_record(out, parent, 0, id);
    fo4::test::end_record(out, record);
    if (id != child_parent) continue;
    const auto children = fo4::test::begin_group(out, parent, children_type);
    out.patch_u32(children + 8, id);
    const auto record_child = fo4::test::begin_record(out, child, 0, 0x801);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"Same\0", 5});
    fo4::test::end_record(out, record_child);
    fo4::test::end_group(out, children);
  }
  fo4::test::end_group(out, top);
  return fo4::read_plugin(out.bytes, std::string(name));
}

}  // namespace

TEST(MaintenanceNested, ReferenceMovedToAnotherCellIsNotItm) {
  const auto base = two_parents("Base.esm", false, "CELL", 6, "REFR", 0x800);
  const auto patch = two_parents("Patch.esp", true, "CELL", 6, "REFR", 0x802);
  for (const auto& key : itm_keys(base, patch)) {
    EXPECT_NE(key, fo4::FormKey::parse("000801:Base.esm"));
  }
}

TEST(MaintenanceNested, InfoMovedToAnotherTopicIsNotItm) {
  const auto base = two_parents("Base.esm", false, "DIAL", 7, "INFO", 0x800);
  const auto patch = two_parents("Patch.esp", true, "DIAL", 7, "INFO", 0x802);
  for (const auto& key : itm_keys(base, patch)) {
    EXPECT_NE(key, fo4::FormKey::parse("000801:Base.esm"));
  }
}
