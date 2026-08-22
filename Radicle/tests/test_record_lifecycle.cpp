#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/form_links.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::vector<std::byte> make_renumber_plugin(bool malformed_mnam = false,
                                             bool light = false) {
  fo4::test::ByteWriter out;
  const auto tes4_flags = light ? fo4::RecordFlags::small_file : 0u;
  const auto tes4 = fo4::test::begin_record(out, "TES4", tes4_flags, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(malformed_mnam ? 5u : 3u);
  hedr.u32(0x802);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto misc_group = fo4::test::begin_group(out, "MISC", 0);
  const auto target = fo4::test::begin_record(out, "MISC", 0, 0x00000800);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Target\0", 7});
  fo4::test::end_record(out, target);

  const auto referrer = fo4::test::begin_record(out, "MISC", 0, 0x00000801);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Referrer\0", 9});
  fo4::test::ByteWriter name;
  name.u32(0x00000800);
  fo4::test::write_subrecord(out, "NAME", name.bytes);
  fo4::test::end_record(out, referrer);
  fo4::test::end_group(out, misc_group);

  if (malformed_mnam) {
    const auto navm_group = fo4::test::begin_group(out, "NAVM", 0);
    const auto navm = fo4::test::begin_record(out, "NAVM", 0, 0x00000802);
    fo4::test::ByteWriter mnam;
    mnam.u32(0x00000800);
    mnam.u16(0xFFFFu);
    fo4::test::write_subrecord(out, "MNAM", mnam.bytes);
    fo4::test::end_record(out, navm);
    fo4::test::end_group(out, navm_group);
  }
  return out.bytes;
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

}

TEST(RecordLifecycle, RenumberRekeysKnownInternalFormLinksAndRoundTrips) {
  auto plugin = fo4::read_plugin(make_renumber_plugin(), "Lifecycle.esp");
  const auto old_key = fo4::FormKey::parse("000800:Lifecycle.esp");
  const auto new_key = fo4::FormKey::parse("000900:Lifecycle.esp");
  const auto referrer_key = fo4::FormKey::parse("000801:Lifecycle.esp");

  ASSERT_EQ(fo4::renumber_local_record(plugin, old_key, fo4::LocalFormId{0x900}),
            fo4::RecordRenumberStatus::renumbered);
  EXPECT_EQ(plugin.header.next_form_id, 0x901u);

  const auto bytes = fo4::write_plugin(plugin);
  const auto reloaded = fo4::read_plugin(bytes, "Lifecycle.esp");
  EXPECT_EQ(reloaded.header.next_form_id, 0x901u);
  EXPECT_EQ(reloaded.header.record_count, 3u);
  EXPECT_EQ(fo4::find_record(reloaded, old_key), nullptr);

  const auto* moved = fo4::find_record(reloaded, new_key);
  ASSERT_NE(moved, nullptr);
  EXPECT_EQ(moved->header.form_id, fo4::RawFormId{0x00000900});
  EXPECT_EQ(editor_id(*moved), "Target");

  const auto* referrer = fo4::find_record(reloaded, referrer_key);
  ASSERT_NE(referrer, nullptr);
  EXPECT_EQ(editor_id(*referrer), "Referrer");

  const auto links = fo4::collect_form_links(reloaded);
  const auto link = std::find_if(links.begin(), links.end(), [&](const auto& hit) {
    return hit.record == referrer_key && hit.subrecord == fo4::sig_name;
  });
  ASSERT_NE(link, links.end());
  EXPECT_EQ(link->key, new_key);
  EXPECT_EQ(link->raw, fo4::RawFormId{0x00000900});
}

TEST(RecordLifecycle, DeleteRefusesReferencedRecordWithoutMutation) {
  auto plugin = fo4::read_plugin(make_renumber_plugin(), "Lifecycle.esp");
  const auto key = fo4::FormKey::parse("000800:Lifecycle.esp");
  const auto original = plugin.original_bytes;

  EXPECT_EQ(fo4::delete_local_record(plugin, key), fo4::RecordDeleteStatus::referenced);
  EXPECT_EQ(plugin.original_bytes, original);
  EXPECT_NE(fo4::find_record(plugin, key), nullptr);
}

TEST(RecordLifecycle, RenumberRefusesExistingTargetWithoutMutation) {
  auto plugin = fo4::read_plugin(make_renumber_plugin(), "Lifecycle.esp");
  const auto original = plugin.original_bytes;
  EXPECT_EQ(
      fo4::renumber_local_record(
          plugin,
          fo4::FormKey::parse("000800:Lifecycle.esp"),
          fo4::LocalFormId{0x801}),
      fo4::RecordRenumberStatus::target_exists);
  EXPECT_EQ(plugin.original_bytes, original);
}

TEST(RecordLifecycle, RenumberFailsClosedOnMalformedMixedFormLinkSurface) {
  auto plugin = fo4::read_plugin(make_renumber_plugin(true), "Lifecycle.esp");
  const auto original = plugin.original_bytes;
  EXPECT_THROW(
      static_cast<void>(fo4::renumber_local_record(
          plugin,
          fo4::FormKey::parse("000800:Lifecycle.esp"),
          fo4::LocalFormId{0x900})),
      fo4::FormIdError);
  EXPECT_EQ(plugin.original_bytes, original);
  EXPECT_NE(fo4::find_record(plugin, fo4::FormKey::parse("000800:Lifecycle.esp")), nullptr);
}

TEST(RecordLifecycle, RenumberRefusesLightPluginWithoutMutation) {
  auto plugin = fo4::read_plugin(make_renumber_plugin(false, true), "Lifecycle.esl");
  const auto original = plugin.original_bytes;
  EXPECT_THROW(
      static_cast<void>(fo4::renumber_local_record(
          plugin,
          fo4::FormKey::parse("000800:Lifecycle.esl"),
          fo4::LocalFormId{0x900})),
      fo4::FormIdError);
  EXPECT_EQ(plugin.original_bytes, original);
  EXPECT_EQ(plugin.header.next_form_id, 0x802u);
}
