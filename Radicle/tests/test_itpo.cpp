#include "fo4/c_api.h"
#include "fo4/c_api_itpo.h"
#include "fo4/plugin/itpo.hpp"
#include "fo4/plugin/maintenance.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Subspec {
  std::string_view signature;
  std::string_view data;
};

struct RecordSpec {
  std::string_view signature;
  std::uint32_t form_id;
  std::string_view editor_id;
  std::uint32_t keyword{0};
  std::uint32_t flags{0};
  std::vector<Subspec> extra{};
};

fo4::Plugin make_plugin(std::string_view name,
                        std::initializer_list<std::string_view> masters,
                        const std::vector<RecordSpec>& records) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
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
    for (const auto& extra : spec.extra) fo4::test::write_subrecord(out, extra.signature, extra.data);
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
  }
  return fo4::read_plugin(out.bytes, std::string(name));
}

struct Nested {
  std::string_view parent;
  std::int32_t children_type;
  std::int32_t inner_type;
  std::string_view child;
  std::string_view child_edid;
};

fo4::Plugin make_nested(std::string_view name, bool with_master, const Nested& spec, bool master_a = false) {
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
  if (master_a) {
    fo4::test::ByteWriter data;
    data.u32(0);
    data.u32(0);
    fo4::test::write_subrecord(out, "MAST", std::string_view{"A.esp\0", 6});
    fo4::test::write_subrecord(out, "DATA", data.bytes);
  }
  fo4::test::end_record(out, tes4);
  const auto top = fo4::test::begin_group(out, spec.parent, 0);
  const auto parent = fo4::test::begin_record(out, spec.parent, 0, 0x00000800);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Parent\0", 7});
  fo4::test::end_record(out, parent);
  const auto children = fo4::test::begin_group(out, spec.parent, spec.children_type);
  out.patch_u32(children + 8, 0x800);
  const auto child = fo4::test::begin_record(out, spec.child, 0, 0x00000801);
  fo4::test::write_subrecord(out, "EDID", spec.child_edid);
  fo4::test::end_record(out, child);
  fo4::test::end_group(out, children);
  fo4::test::end_group(out, top);
  return fo4::read_plugin(out.bytes, std::string(name));
}

constexpr Nested k_topic{"DIAL", 7, -1, "INFO", std::string_view{"Info\0", 5}};
constexpr Nested k_moved{"DIAL", 7, -1, "INFO", std::string_view{"Moved\0", 6}};

}

TEST(Itpo, OverrideIdenticalToPreviousOverrideIsFound) {
  const auto base = make_plugin("Base.esm", {}, {{"KYWD", 0x000800, std::string_view{"Item\0", 5}}});
  const auto first = make_plugin("A.esp", {"Base.esm"}, {{"KYWD", 0x00000800, std::string_view{"Changed\0", 8}}});
  const auto second = make_plugin("B.esp", {"Base.esm", "A.esp"}, {{"KYWD", 0x00000800, std::string_view{"Changed\0", 8}}});
  const std::vector<const fo4::Plugin*> load_order{&base, &first, &second};
  const auto found = fo4::find_identical_to_previous_override(load_order, 2);
  ASSERT_EQ(found.size(), 1u);
  EXPECT_EQ(found[0].key, fo4::FormKey::parse("000800:Base.esm"));
  EXPECT_EQ(found[0].previous.filename(), "A.esp");
}

TEST(Itpo, RemoveDropsIdenticalPreviousOverride) {
  const auto base = make_plugin("Base.esm", {}, {{"KYWD", 0x000800, std::string_view{"Item\0", 5}}});
  const auto first = make_plugin("A.esp", {"Base.esm"}, {{"KYWD", 0x00000800, std::string_view{"Changed\0", 8}}});
  auto second = make_plugin("B.esp", {"Base.esm", "A.esp"}, {{"KYWD", 0x00000800, std::string_view{"Changed\0", 8}}});
  const std::vector<const fo4::Plugin*> load_order{&base, &first, &second};
  EXPECT_EQ(fo4::remove_identical_to_previous_override(second, load_order, 2), 1u);
  const auto reread = fo4::read_plugin(fo4::write_plugin(second), "B.esp");
  EXPECT_EQ(fo4::find_record(reread, fo4::FormKey::parse("000800:Base.esm")), nullptr);
  EXPECT_TRUE(reread.groups.empty());
}

TEST(Itpo, IndependentEarlierPluginIsNotAPreviousOverride) {
  const auto base = make_plugin("Base.esm", {}, {{"KYWD", 0x000800, std::string_view{"Item\0", 5}}});
  const auto first = make_plugin("A.esp", {"Base.esm"}, {{"KYWD", 0x00000800, std::string_view{"Changed\0", 8}}});
  auto second = make_plugin("B.esp", {"Base.esm"}, {{"KYWD", 0x00000800, std::string_view{"Changed\0", 8}}});
  const std::vector<const fo4::Plugin*> load_order{&base, &first, &second};
  EXPECT_TRUE(fo4::find_identical_to_previous_override(load_order, 2).empty());
  EXPECT_EQ(fo4::remove_identical_to_previous_override(second, load_order, 2), 0u);
  EXPECT_NE(fo4::find_record(second, fo4::FormKey::parse("000800:Base.esm")), nullptr);
}

TEST(Itpo, NearestHolderThatIsNotAMasterBlocksTheReport) {
  const auto base = make_plugin("Base.esm", {}, {{"KYWD", 0x000800, std::string_view{"Item\0", 5}}});
  const auto first = make_plugin("A.esp", {"Base.esm"}, {{"KYWD", 0x00000800, std::string_view{"Changed\0", 8}}});
  for (const std::string_view other_edid : {std::string_view{"Other\0", 6}, std::string_view{"Changed\0", 8}}) {
    const auto other = make_plugin("X.esp", {"Base.esm"}, {{"KYWD", 0x00000800, other_edid}});
    auto second = make_plugin("B.esp", {"Base.esm", "A.esp"}, {{"KYWD", 0x00000800, std::string_view{"Changed\0", 8}}});
    const std::vector<const fo4::Plugin*> load_order{&base, &first, &other, &second};
    EXPECT_TRUE(fo4::find_identical_to_previous_override(load_order, 3).empty());
    EXPECT_EQ(fo4::remove_identical_to_previous_override(second, load_order, 3), 0u);
    EXPECT_NE(fo4::find_record(second, fo4::FormKey::parse("000800:Base.esm")), nullptr);
  }
}

TEST(Itpo, MasterNamesMatchIgnoringCase) {
  const auto base = make_plugin("Base.esm", {}, {{"KYWD", 0x000800, std::string_view{"Item\0", 5}}});
  const auto first = make_plugin("A.esp", {"Base.esm"}, {{"KYWD", 0x00000800, std::string_view{"Changed\0", 8}}});
  const auto second = make_plugin("B.esp", {"Base.esm", "a.ESP"}, {{"KYWD", 0x00000800, std::string_view{"Changed\0", 8}}});
  const std::vector<const fo4::Plugin*> load_order{&base, &first, &second};
  EXPECT_EQ(fo4::find_identical_to_previous_override(load_order, 2).size(), 1u);
}

TEST(Itpo, OverridesDifferingOnlyInAnIgnoredSubrecordAreReportedButNotRemoved) {
  const auto base = make_plugin("Base.esm", {}, {{"TACT", 0x000800, std::string_view{"Base\0", 5}}});
  const auto first = make_plugin("A.esp", {"Base.esm"},
                                 {{"TACT", 0x00000800, std::string_view{"Item\0", 5}, 0, 0,
                                   {{"PNAM", std::string_view{"\x01\0\0\0", 4}}}}});
  auto second = make_plugin("B.esp", {"Base.esm", "A.esp"},
                            {{"TACT", 0x00000800, std::string_view{"Item\0", 5}, 0, 0,
                              {{"PNAM", std::string_view{"\x02\0\0\0", 4}}}}});
  const std::vector<const fo4::Plugin*> load_order{&base, &first, &second};
  EXPECT_EQ(fo4::find_identical_to_previous_override(load_order, 2).size(), 1u);
  EXPECT_TRUE(fo4::find_identical_to_previous_override(load_order, 2, 0, true).empty());
  EXPECT_EQ(fo4::remove_identical_to_previous_override(second, load_order, 2), 0u);
  EXPECT_NE(fo4::find_record(second, fo4::FormKey::parse("000800:Base.esm")), nullptr);
}

TEST(Itpo, ByteIdenticalOverridesWithAnIgnoredSubrecordAreRemoved) {
  const auto base = make_plugin("Base.esm", {}, {{"TACT", 0x000800, std::string_view{"Base\0", 5}}});
  const auto first = make_plugin("A.esp", {"Base.esm"},
                                 {{"TACT", 0x00000800, std::string_view{"Item\0", 5}, 0, 0,
                                   {{"PNAM", std::string_view{"\x01\0\0\0", 4}}}}});
  auto second = make_plugin("B.esp", {"Base.esm", "A.esp"},
                            {{"TACT", 0x00000800, std::string_view{"Item\0", 5}, 0, 0,
                              {{"PNAM", std::string_view{"\x01\0\0\0", 4}}}}});
  const std::vector<const fo4::Plugin*> load_order{&base, &first, &second};
  EXPECT_EQ(fo4::remove_identical_to_previous_override(second, load_order, 2), 1u);
  EXPECT_EQ(fo4::find_record(second, fo4::FormKey::parse("000800:Base.esm")), nullptr);
}

TEST(Itpo, DifferingOverrideIsKept) {
  const auto base = make_plugin("Base.esm", {}, {{"KYWD", 0x000800, std::string_view{"Item\0", 5}}});
  const auto first = make_plugin("A.esp", {"Base.esm"}, {{"KYWD", 0x00000800, std::string_view{"Changed\0", 8}}});
  auto second = make_plugin("B.esp", {"Base.esm", "A.esp"}, {{"KYWD", 0x00000800, std::string_view{"Other\0", 6}}});
  const std::vector<const fo4::Plugin*> load_order{&base, &first, &second};
  EXPECT_TRUE(fo4::find_identical_to_previous_override(load_order, 2).empty());
  EXPECT_EQ(fo4::remove_identical_to_previous_override(second, load_order, 2), 0u);
}

TEST(Itpo, IdenticalToMasterButNotToPreviousOverrideIsKept) {
  const auto base = make_plugin("Base.esm", {}, {{"KYWD", 0x000800, std::string_view{"Item\0", 5}}});
  const auto first = make_plugin("A.esp", {"Base.esm"}, {{"KYWD", 0x00000800, std::string_view{"Changed\0", 8}}});
  const auto second = make_plugin("B.esp", {"Base.esm", "A.esp"}, {{"KYWD", 0x00000800, std::string_view{"Item\0", 5}}});
  const std::vector<const fo4::Plugin*> load_order{&base, &first, &second};
  EXPECT_TRUE(fo4::find_identical_to_previous_override(load_order, 2).empty());
}

TEST(Itpo, FirstOverrideIdenticalToMasterIsNotItpo) {
  const auto base = make_plugin("Base.esm", {}, {{"KYWD", 0x000800, std::string_view{"Item\0", 5}}});
  const auto first = make_plugin("A.esp", {"Base.esm"}, {{"KYWD", 0x00000800, std::string_view{"Item\0", 5}}});
  const std::vector<const fo4::Plugin*> load_order{&base, &first};
  EXPECT_TRUE(fo4::find_identical_to_previous_override(load_order, 1).empty());
  EXPECT_EQ(fo4::find_identical_to_master(load_order, 1).size(), 1u);
}

TEST(Itpo, IgnoredSubrecordDifferenceStillIdentical) {
  const auto base = make_plugin("Base.esm", {}, {{"TACT", 0x000800, std::string_view{"Base\0", 5}}});
  const auto first = make_plugin("A.esp", {"Base.esm"},
                                 {{"TACT", 0x00000800, std::string_view{"Item\0", 5}, 0, 0,
                                   {{"PNAM", std::string_view{"\x01\0\0\0", 4}}}}});
  const auto second = make_plugin("B.esp", {"Base.esm", "A.esp"},
                                  {{"TACT", 0x00000800, std::string_view{"Item\0", 5}, 0, 0,
                                    {{"PNAM", std::string_view{"\x02\0\0\0", 4}}}}});
  const std::vector<const fo4::Plugin*> load_order{&base, &first, &second};
  const auto found = fo4::find_identical_to_previous_override(load_order, 2);
  ASSERT_EQ(found.size(), 1u);
  EXPECT_EQ(found[0].previous.filename(), "A.esp");
}

TEST(Itpo, DeletedFlagDifferenceIsNotIdentical) {
  const auto base = make_plugin("Base.esm", {}, {{"MISC", 0x000800, std::string_view{"Item\0", 5}}});
  const auto first = make_plugin("A.esp", {"Base.esm"},
                                 {{"MISC", 0x00000800, std::string_view{"Item\0", 5}, 0, fo4::RecordFlags::deleted}});
  const auto second = make_plugin("B.esp", {"Base.esm", "A.esp"}, {{"MISC", 0x00000800, std::string_view{"Item\0", 5}}});
  const std::vector<const fo4::Plugin*> load_order{&base, &first, &second};
  EXPECT_TRUE(fo4::find_identical_to_previous_override(load_order, 2).empty());
}

TEST(Itpo, IdenticalChildGroupIsFoundChildFirst) {
  const auto base = make_nested("Base.esm", false, k_topic);
  const auto first = make_nested("A.esp", true, k_moved);
  const auto second = make_nested("B.esp", true, k_moved, true);
  const std::vector<const fo4::Plugin*> load_order{&base, &first, &second};
  const auto found = fo4::find_identical_to_previous_override(load_order, 2);
  ASSERT_EQ(found.size(), 2u);
  EXPECT_EQ(found[0].key, fo4::FormKey::parse("000801:Base.esm"));
  EXPECT_EQ(found[1].key, fo4::FormKey::parse("000800:Base.esm"));
  EXPECT_EQ(found[0].previous.filename(), "A.esp");
  EXPECT_EQ(found[1].previous.filename(), "A.esp");
}

TEST(Itpo, ParentOfDifferingChildIsKept) {
  const auto base = make_nested("Base.esm", false, k_topic);
  const auto first = make_nested("A.esp", true, k_moved);
  const auto second = make_nested("B.esp", true, k_topic, true);
  const std::vector<const fo4::Plugin*> load_order{&base, &first, &second};
  EXPECT_TRUE(fo4::find_identical_to_previous_override(load_order, 2).empty());
}

TEST(Itpo, RemoveNestedDropsItpoRecordsAndTheirGroups) {
  const auto base = make_nested("Base.esm", false, k_topic);
  const auto first = make_nested("A.esp", true, k_moved);
  auto second = make_nested("B.esp", true, k_moved, true);
  const std::vector<const fo4::Plugin*> load_order{&base, &first, &second};
  EXPECT_EQ(fo4::remove_identical_to_previous_override(second, load_order, 2), 2u);
  const auto reread = fo4::read_plugin(fo4::write_plugin(second), "B.esp");
  EXPECT_TRUE(reread.groups.empty());
}

TEST(Itpo, ComparesOnlyAgainstNearestEarlierOverride) {
  const auto base = make_plugin("Base.esm", {}, {{"KYWD", 0x000800, std::string_view{"Item\0", 5}}});
  const auto first = make_plugin("A.esp", {"Base.esm"}, {{"KYWD", 0x00000800, std::string_view{"Changed\0", 8}}});
  const auto middle = make_plugin("B.esp", {"Base.esm", "A.esp"}, {{"KYWD", 0x00000800, std::string_view{"Other\0", 6}}});
  const auto last = make_plugin("C.esp", {"Base.esm", "A.esp", "B.esp"}, {{"KYWD", 0x00000800, std::string_view{"Changed\0", 8}}});
  const std::vector<const fo4::Plugin*> load_order{&base, &first, &middle, &last};
  EXPECT_TRUE(fo4::find_identical_to_previous_override(load_order, 3).empty());
}

TEST(Itpo, NearestEarlierOverrideIdenticalIsFound) {
  const auto base = make_plugin("Base.esm", {}, {{"KYWD", 0x000800, std::string_view{"Item\0", 5}}});
  const auto first = make_plugin("A.esp", {"Base.esm"}, {{"KYWD", 0x00000800, std::string_view{"Changed\0", 8}}});
  const auto middle = make_plugin("B.esp", {"Base.esm", "A.esp"}, {{"KYWD", 0x00000800, std::string_view{"Other\0", 6}}});
  const auto last = make_plugin("C.esp", {"Base.esm", "A.esp", "B.esp"}, {{"KYWD", 0x00000800, std::string_view{"Other\0", 6}}});
  const std::vector<const fo4::Plugin*> load_order{&base, &first, &middle, &last};
  const auto found = fo4::find_identical_to_previous_override(load_order, 3);
  ASSERT_EQ(found.size(), 1u);
  EXPECT_EQ(found[0].previous.filename(), "B.esp");
}

TEST(Itpo, IndexOutOfRangeIsRefused) {
  const auto base = make_plugin("Base.esm", {}, {{"KYWD", 0x000800, std::string_view{"Item\0", 5}}});
  const auto first = make_plugin("A.esp", {"Base.esm"}, {{"KYWD", 0x00000800, std::string_view{"Changed\0", 8}}});
  auto second = make_plugin("B.esp", {"Base.esm", "A.esp"}, {{"KYWD", 0x00000800, std::string_view{"Changed\0", 8}}});
  const std::vector<const fo4::Plugin*> load_order{&base, &first, &second};
  EXPECT_TRUE(fo4::find_identical_to_previous_override(load_order, 3).empty());
  EXPECT_EQ(fo4::remove_identical_to_previous_override(second, load_order, 3), 0u);
}

TEST(Itpo, NullLoadOrderEntryIsSkippedWhenLookingForHolder) {
  const auto base = make_plugin("Base.esm", {}, {{"KYWD", 0x000800, std::string_view{"Item\0", 5}}});
  const auto first = make_plugin("A.esp", {"Base.esm"}, {{"KYWD", 0x00000800, std::string_view{"Changed\0", 8}}});
  const auto second = make_plugin("B.esp", {"Base.esm", "A.esp"}, {{"KYWD", 0x00000800, std::string_view{"Changed\0", 8}}});
  const std::vector<const fo4::Plugin*> load_order{&base, &first, nullptr, &second};
  const auto found = fo4::find_identical_to_previous_override(load_order, 3);
  ASSERT_EQ(found.size(), 1u);
  EXPECT_EQ(found[0].previous.filename(), "A.esp");
}

TEST(Itpo, OwnerAbsentFromLoadOrderIsRefused) {
  const auto first = make_plugin("A.esp", {"Base.esm"}, {{"KYWD", 0x00000800, std::string_view{"Changed\0", 8}}});
  const auto second = make_plugin("B.esp", {"Base.esm", "A.esp"}, {{"KYWD", 0x00000800, std::string_view{"Changed\0", 8}}});
  const std::vector<const fo4::Plugin*> load_order{&first, &second};
  EXPECT_TRUE(fo4::find_identical_to_previous_override(load_order, 1).empty());
}

TEST(Itpo, NewRecordWithOwnKeyIsNotReported) {
  const auto first = make_plugin("A.esp", {}, {{"KYWD", 0x000800, std::string_view{"Changed\0", 8}}});
  const auto second = make_plugin("B.esp", {}, {{"KYWD", 0x000800, std::string_view{"Changed\0", 8}}});
  const std::vector<const fo4::Plugin*> load_order{&first, &second};
  EXPECT_TRUE(fo4::find_identical_to_previous_override(load_order, 1).empty());
}

namespace {

std::vector<std::byte> plugin_bytes(std::initializer_list<std::string_view> masters,
                                    std::string_view editor_id,
                                    std::uint32_t form_id) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  for (const auto master : masters) {
    std::vector<std::byte> name;
    for (const char ch : master) name.push_back(std::byte{static_cast<std::uint8_t>(ch)});
    name.push_back(std::byte{0});
    fo4::test::write_subrecord(out, "MAST", name);
    fo4::test::ByteWriter data;
    data.u32(0);
    data.u32(0);
    fo4::test::write_subrecord(out, "DATA", data.bytes);
  }
  fo4::test::end_record(out, tes4);
  const auto group = fo4::test::begin_group(out, "KYWD", 0);
  const auto record = fo4::test::begin_record(out, "KYWD", 0, form_id);
  fo4::test::write_subrecord(out, "EDID", editor_id);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

std::string write_file(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
  std::ofstream(path, std::ios::binary)
      .write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  const auto text = path.u8string();
  return {text.begin(), text.end()};
}

std::vector<std::string> take(char** strings, size_t count) {
  std::vector<std::string> out(strings, strings + count);
  radicle_free_string_array(strings, count);
  return out;
}

}

TEST(CApiItpo, ReportsAndRemovesIdenticalToPreviousOverride) {
  const auto root = std::filesystem::temp_directory_path() /
                    ("radicle_c_itpo_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  const auto base = write_file(root / "Base.esm", plugin_bytes({}, std::string_view{"Item\0", 5}, 0x000800));
  const auto first =
      write_file(root / "A.esp", plugin_bytes({"Base.esm"}, std::string_view{"Changed\0", 8}, 0x00000800));
  const auto second =
      write_file(root / "B.esp", plugin_bytes({"Base.esm", "A.esp"}, std::string_view{"Changed\0", 8}, 0x00000800));
  const char* paths[] = {base.c_str(), first.c_str(), second.c_str()};
  radicle_env* env = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 3, &env), RADICLE_STATUS_OK) << radicle_last_error();

  char** strings = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_env_identical_to_previous_override(env, "B.esp", 0, &strings, &count), RADICLE_STATUS_OK);
  EXPECT_EQ(take(strings, count), std::vector<std::string>{"000800:Base.esm\tKYWD\tA.esp"});

  radicle_plugin* patch = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(second.c_str(), &patch), RADICLE_STATUS_OK) << radicle_last_error();
  size_t removed = 0;
  ASSERT_EQ(radicle_plugin_remove_identical_to_previous_override(patch, env, &removed), RADICLE_STATUS_OK);
  EXPECT_EQ(removed, 1u);
  ASSERT_EQ(radicle_plugin_collect_form_keys(patch, &strings, &count), RADICLE_STATUS_OK);
  const auto keys = take(strings, count);
  EXPECT_EQ(std::find(keys.begin(), keys.end(), std::string{"000800:Base.esm"}), keys.end());

  EXPECT_EQ(radicle_env_identical_to_previous_override(env, "Missing.esp", 0, &strings, &count),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_env_identical_to_previous_override(nullptr, "B.esp", 0, &strings, &count),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_remove_identical_to_previous_override(nullptr, env, &removed),
            RADICLE_STATUS_INVALID_ARGUMENT);

  radicle_plugin_destroy(patch);
  radicle_env_destroy(env);
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
}