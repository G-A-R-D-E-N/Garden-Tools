#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

const auto k_dial_key = fo4::FormKey::parse("000800:Base.esm");
const auto k_first_info = fo4::FormKey::parse("000801:Base.esm");
const auto k_second_info = fo4::FormKey::parse("000802:Base.esm");
const auto k_third_info = fo4::FormKey::parse("000803:Base.esm");
const auto k_gone_key = fo4::FormKey::parse("000900:Base.esm");

void write_edid(fo4::test::ByteWriter& out, std::string_view text) {
  std::string value(text);
  value.push_back('\0');
  fo4::test::write_subrecord(out, "EDID", std::string_view{value});
}

std::vector<std::byte> make_plugin_bytes(
    const std::vector<std::string_view>& masters,
    bool with_dial,
    const std::vector<std::pair<std::uint32_t, std::string_view>>& infos,
    bool with_misc = false) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(static_cast<std::uint32_t>(masters.size()));
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  for (const auto master : masters) {
    std::string name(master);
    name.push_back('\0');
    fo4::test::write_subrecord(out, "MAST", std::string_view{name});
    fo4::test::ByteWriter data;
    data.u32(0);
    data.u32(0);
    fo4::test::write_subrecord(out, "DATA", data.bytes);
  }
  fo4::test::end_record(out, tes4);

  if (with_dial) {
    const auto top = fo4::test::begin_group(out, "DIAL", 0);
    const auto dial = fo4::test::begin_record(out, "DIAL", 0, 0x00000800);
    write_edid(out, "Greeting");
    fo4::test::end_record(out, dial);
    const auto children = fo4::test::begin_group(out, std::string_view{"\0\x08\0\0", 4}, 7);
    for (const auto& info : infos) {
      const auto record = fo4::test::begin_record(out, "INFO", 0, info.first);
      write_edid(out, info.second);
      fo4::test::end_record(out, record);
    }
    fo4::test::end_group(out, children);
    fo4::test::end_group(out, top);
  }

  if (with_misc) {
    const auto top = fo4::test::begin_group(out, "MISC", 0);
    const auto record = fo4::test::begin_record(out, "MISC", 0, 0x00000810);
    write_edid(out, "Item");
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, top);
  }
  return out.bytes;
}

fo4::Plugin base_plugin(const std::vector<std::pair<std::uint32_t, std::string_view>>& infos) {
  return fo4::read_plugin(make_plugin_bytes({}, true, infos), "Base.esm");
}

fo4::Plugin empty_patch() {
  return fo4::read_plugin(make_plugin_bytes({}, false, {}), "Patch.esp");
}

std::string editor_id(const fo4::Record& record) {
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature != fo4::sig_edid) continue;
    const auto* begin = reinterpret_cast<const char*>(subrecord.data.data());
    const auto* end = begin + subrecord.data.size();
    const auto* nul = std::find(begin, end, '\0');
    return std::string(begin, nul);
  }
  return {};
}

std::string editor_id_of(const fo4::Plugin& plugin, const fo4::FormKey& key) {
  const auto* record = fo4::find_record(plugin, key);
  return record == nullptr ? std::string{} : editor_id(*record);
}

const fo4::Group* topic_children(const fo4::Plugin& plugin) {
  const fo4::Group* found = nullptr;
  fo4::visit_groups(plugin, [&](const fo4::Group& group) {
    if (found == nullptr && group.header.group_type == 7) found = &group;
  });
  return found;
}

}  // namespace

TEST(RecordDeepCopy, CopiesTheDialogAndItsInfoRecords) {
  const auto source = base_plugin({{0x801, "Hello"}, {0x802, "Bye"}});
  auto destination = empty_patch();

  const auto result = fo4::copy_record_deep(destination, source, k_dial_key, false);

  EXPECT_EQ(result.status, fo4::RecordCopyStatus::inserted);
  EXPECT_EQ(result.key, k_dial_key);
  ASSERT_EQ(result.children.size(), 2u);
  EXPECT_EQ(result.children[0], k_first_info);
  EXPECT_EQ(result.children[1], k_second_info);
  EXPECT_EQ(editor_id_of(destination, k_dial_key), "Greeting");
  EXPECT_EQ(editor_id_of(destination, k_first_info), "Hello");
  EXPECT_EQ(editor_id_of(destination, k_second_info), "Bye");

  ASSERT_EQ(destination.header.masters.size(), 1u);
  EXPECT_EQ(destination.header.masters[0].key.filename(), "Base.esm");

  const auto reloaded = fo4::read_plugin(fo4::write_plugin(destination), "Patch.esp");
  const auto* children = topic_children(reloaded);
  ASSERT_NE(children, nullptr);
  EXPECT_EQ(children->header.label_as_u32(), 0x00000800u);
  ASSERT_EQ(children->children.size(), 2u);
  EXPECT_EQ(editor_id_of(reloaded, k_first_info), "Hello");
}

TEST(RecordDeepCopy, RefusesAnExistingDialogWithoutOverwrite) {
  const auto source = base_plugin({{0x801, "Hello"}});
  auto destination = empty_patch();
  ASSERT_EQ(fo4::copy_record_deep(destination, source, k_dial_key, false).status,
            fo4::RecordCopyStatus::inserted);
  const auto before = fo4::write_plugin(destination);

  const auto again = fo4::copy_record_deep(destination, source, k_dial_key, false);

  EXPECT_EQ(again.status, fo4::RecordCopyStatus::already_exists);
  EXPECT_TRUE(again.children.empty());
  EXPECT_EQ(fo4::write_plugin(destination), before);
}

TEST(RecordDeepCopy, ReplacesAndAddsChildrenUnderOverwrite) {
  const auto source = base_plugin({{0x801, "Hello"}, {0x802, "Bye"}});
  auto destination = empty_patch();
  ASSERT_EQ(fo4::copy_record_deep(destination, source, k_dial_key, false).status,
            fo4::RecordCopyStatus::inserted);

  const auto changed = base_plugin({{0x801, "Changed"}, {0x802, "Bye"}, {0x803, "Extra"}});
  const auto result = fo4::copy_record_deep(destination, changed, k_dial_key, true);

  EXPECT_EQ(result.status, fo4::RecordCopyStatus::replaced);
  EXPECT_EQ(result.children.size(), 3u);
  EXPECT_EQ(editor_id_of(destination, k_first_info), "Changed");
  EXPECT_EQ(editor_id_of(destination, k_second_info), "Bye");
  EXPECT_EQ(editor_id_of(destination, k_third_info), "Extra");
  const auto* children = topic_children(destination);
  ASSERT_NE(children, nullptr);
  EXPECT_EQ(children->children.size(), 3u);
}

// Detects: overwriting existing parent and child records must preserve an unrelated stale HEDR count.
TEST(RecordDeepCopy, ReplacingExistingHierarchyPreservesHedrCount) {
  auto destination = fo4::read_plugin(
      make_plugin_bytes({"Base.esm"}, true, {{0x801, "Old"}, {0x802, "Bye"}}), "Patch.esp");
  ASSERT_EQ(destination.header.record_count, 1u);
  const auto source = base_plugin({{0x801, "Changed"}, {0x802, "Bye"}});
  ASSERT_EQ(fo4::copy_record_deep(destination, source, k_dial_key, true).status,
            fo4::RecordCopyStatus::replaced);
  const auto reloaded = fo4::read_plugin(fo4::write_plugin(destination), "Patch.esp");
  EXPECT_EQ(reloaded.header.record_count, 1u);
  EXPECT_EQ(editor_id_of(reloaded, k_first_info), "Changed");
}

TEST(RecordDeepCopy, ARecordWithoutChildRecordsIsAPlainOverride) {
  const auto source = fo4::read_plugin(make_plugin_bytes({}, false, {}, true), "Base.esm");
  const auto misc = fo4::FormKey::parse("000810:Base.esm");
  auto destination = empty_patch();

  const auto result = fo4::copy_record_deep(destination, source, misc, false);

  EXPECT_EQ(result.status, fo4::RecordCopyStatus::inserted);
  EXPECT_TRUE(result.children.empty());
  EXPECT_EQ(editor_id_of(destination, misc), "Item");
  ASSERT_EQ(destination.header.masters.size(), 1u);
  EXPECT_EQ(destination.header.masters[0].key.filename(), "Base.esm");
}

TEST(RecordDeepCopy, RefusesANestedChildRecordAsSource) {
  const auto source = base_plugin({{0x801, "Hello"}});
  auto destination = empty_patch();
  const auto before = fo4::write_plugin(destination);

  const auto result = fo4::copy_record_deep(destination, source, k_first_info, false);

  EXPECT_EQ(result.status, fo4::RecordCopyStatus::source_containment_unsupported);
  EXPECT_TRUE(result.children.empty());
  EXPECT_EQ(fo4::write_plugin(destination), before);
}

TEST(RecordDeepCopy, AMissingRecordIsReported) {
  const auto source = base_plugin({{0x801, "Hello"}});
  auto destination = empty_patch();

  const auto result = fo4::copy_record_deep(destination, source, k_gone_key, false);

  EXPECT_EQ(result.status, fo4::RecordCopyStatus::source_missing);
  EXPECT_TRUE(result.children.empty());
}
