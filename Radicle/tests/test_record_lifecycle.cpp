#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
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

std::vector<std::byte> make_child_group_plugin(std::string_view parent_type,
                                               std::string_view child_type,
                                               std::int32_t child_group_type) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(0x802);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto top = fo4::test::begin_group(out, parent_type, 0);
  const auto parent = fo4::test::begin_record(out, parent_type, 0, 0x00000800);
  fo4::test::end_record(out, parent);
  const auto children =
      fo4::test::begin_group(out, std::string_view{"\0\x08\0\0", 4}, child_group_type);
  const auto child = fo4::test::begin_record(out, child_type, 0, 0x00000801);
  fo4::test::end_record(out, child);
  fo4::test::end_group(out, children);
  fo4::test::end_group(out, top);
  return out.bytes;
}

const fo4::Group* group_of_type(const fo4::Plugin& plugin, std::int32_t type) {
  const fo4::Group* found = nullptr;
  fo4::visit_groups(plugin, [&](const fo4::Group& group) {
    if (found == nullptr && group.header.group_type == type) found = &group;
  });
  return found;
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

std::vector<std::byte> make_inject_plugin() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(0x802);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Master.esp\0", 11});
  fo4::test::ByteWriter data;
  data.u32(0);
  data.u32(0);
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::end_record(out, tes4);

  const auto misc_group = fo4::test::begin_group(out, "MISC", 0);
  const auto target = fo4::test::begin_record(out, "MISC", 0, 0x01000800);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Target\0", 7});
  fo4::test::end_record(out, target);

  const auto referrer = fo4::test::begin_record(out, "MISC", 0, 0x01000801);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Referrer\0", 9});
  fo4::test::ByteWriter name;
  name.u32(0x01000800);
  fo4::test::write_subrecord(out, "NAME", name.bytes);
  fo4::test::end_record(out, referrer);
  fo4::test::end_group(out, misc_group);
  return out.bytes;
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
  const auto original = fo4::test::original_bytes(plugin);

  EXPECT_EQ(fo4::delete_local_record(plugin, key), fo4::RecordDeleteStatus::referenced);
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
  EXPECT_NE(fo4::find_record(plugin, key), nullptr);
}

TEST(RecordLifecycle, RenumberRefusesExistingTargetWithoutMutation) {
  auto plugin = fo4::read_plugin(make_renumber_plugin(), "Lifecycle.esp");
  const auto original = fo4::test::original_bytes(plugin);
  EXPECT_EQ(
      fo4::renumber_local_record(
          plugin,
          fo4::FormKey::parse("000800:Lifecycle.esp"),
          fo4::LocalFormId{0x801}),
      fo4::RecordRenumberStatus::target_exists);
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
}

TEST(RecordLifecycle, RenumberFailsClosedOnMalformedMixedFormLinkSurface) {
  auto plugin = fo4::read_plugin(make_renumber_plugin(true), "Lifecycle.esp");
  const auto original = fo4::test::original_bytes(plugin);
  EXPECT_THROW(
      static_cast<void>(fo4::renumber_local_record(
          plugin,
          fo4::FormKey::parse("000800:Lifecycle.esp"),
          fo4::LocalFormId{0x900})),
      fo4::FormIdError);
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
  EXPECT_NE(fo4::find_record(plugin, fo4::FormKey::parse("000800:Lifecycle.esp")), nullptr);
}

TEST(RecordLifecycle, RenumberLightPluginWithinTheProvenRange) {
  auto plugin = fo4::read_plugin(make_renumber_plugin(false, true), "Lifecycle.esl");
  EXPECT_EQ(fo4::renumber_local_record(plugin,
                                       fo4::FormKey::parse("000800:Lifecycle.esl"),
                                       fo4::LocalFormId{0x900}),
            fo4::RecordRenumberStatus::renumbered);
  EXPECT_NE(fo4::find_record(plugin, fo4::FormKey::parse("000900:Lifecycle.esl")), nullptr);
  EXPECT_EQ(plugin.header.next_form_id, 0x901u);
}

// Above 0xFFF the engine masks the local down and the record aliases onto a
// different one, so renumbering out of range must fail closed.
TEST(RecordLifecycle, RenumberRefusesLightPluginAboveTwelveBits) {
  auto plugin = fo4::read_plugin(make_renumber_plugin(false, true), "Lifecycle.esl");
  const auto original = fo4::test::original_bytes(plugin);
  EXPECT_THROW(
      static_cast<void>(fo4::renumber_local_record(
          plugin,
          fo4::FormKey::parse("000800:Lifecycle.esl"),
          fo4::LocalFormId{0x1000})),
      fo4::FormIdError);
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
  EXPECT_EQ(plugin.header.next_form_id, 0x802u);
}

TEST(RecordLifecycle, RenumberRelabelsDialTopicChildren) {
  auto plugin = fo4::read_plugin(make_child_group_plugin("DIAL", "INFO", 7), "Lifecycle.esp");
  const auto key = fo4::FormKey::parse("000800:Lifecycle.esp");
  const auto moved = fo4::FormKey::parse("000900:Lifecycle.esp");

  ASSERT_EQ(fo4::renumber_local_record(plugin, key, fo4::LocalFormId{0x900}),
            fo4::RecordRenumberStatus::renumbered);
  EXPECT_EQ(fo4::find_record(plugin, key), nullptr);
  ASSERT_NE(fo4::find_record(plugin, moved), nullptr);
  EXPECT_EQ(fo4::find_record(plugin, moved)->header.form_id, fo4::RawFormId{0x00000900});

  const auto* children = group_of_type(plugin, 7);
  ASSERT_NE(children, nullptr);
  EXPECT_EQ(children->header.label_as_u32(), 0x00000900u);
  EXPECT_NE(fo4::find_record(plugin, fo4::FormKey::parse("000801:Lifecycle.esp")), nullptr);

  const auto reloaded = fo4::read_plugin(fo4::write_plugin(plugin), "Lifecycle.esp");
  ASSERT_NE(fo4::find_record(reloaded, moved), nullptr);
  const auto* reloaded_children = group_of_type(reloaded, 7);
  ASSERT_NE(reloaded_children, nullptr);
  EXPECT_EQ(reloaded_children->header.label_as_u32(), 0x00000900u);
}

TEST(RecordLifecycle, RenumberRelabelsWrldWorldChildren) {
  auto plugin = fo4::read_plugin(make_child_group_plugin("WRLD", "CELL", 1), "Lifecycle.esp");
  const auto key = fo4::FormKey::parse("000800:Lifecycle.esp");
  const auto moved = fo4::FormKey::parse("000900:Lifecycle.esp");

  ASSERT_EQ(fo4::renumber_local_record(plugin, key, fo4::LocalFormId{0x900}),
            fo4::RecordRenumberStatus::renumbered);
  ASSERT_NE(fo4::find_record(plugin, moved), nullptr);
  EXPECT_EQ(fo4::find_record(plugin, key), nullptr);

  const auto* children = group_of_type(plugin, 1);
  ASSERT_NE(children, nullptr);
  EXPECT_EQ(children->header.label_as_u32(), 0x00000900u);
}

TEST(RecordLifecycle, InjectMovesOwnRecordsIntoMasterIdSpaceAndRoundTrips) {
  auto plugin = fo4::read_plugin(make_inject_plugin(), "Inject.esp");
  const auto old_key = fo4::FormKey::parse("000800:Inject.esp");
  const auto new_key = fo4::FormKey::parse("000850:Master.esp");
  const auto referrer_key = fo4::FormKey::parse("000801:Inject.esp");

  ASSERT_EQ(fo4::inject_local_record(plugin, old_key, new_key),
            fo4::RecordRenumberStatus::renumbered);
  EXPECT_EQ(plugin.header.next_form_id, 0x802u);
  EXPECT_EQ(fo4::find_record(plugin, old_key), nullptr);

  const auto* moved = fo4::find_record(plugin, new_key);
  ASSERT_NE(moved, nullptr);
  EXPECT_EQ(moved->header.form_id, fo4::RawFormId{0x00000850});
  EXPECT_EQ(fo4::resolve_plugin_form_id(moved->header.form_id,
                                        fo4::plugin_form_id_context(plugin)),
            new_key);

  const auto links = fo4::collect_form_links(plugin);
  const auto link = std::find_if(links.begin(), links.end(), [&](const auto& hit) {
    return hit.record == referrer_key && hit.subrecord == fo4::sig_name;
  });
  ASSERT_NE(link, links.end());
  EXPECT_EQ(link->key, new_key);
  EXPECT_EQ(link->raw, fo4::RawFormId{0x00000850});

  const auto reloaded = fo4::read_plugin(fo4::write_plugin(plugin), "Inject.esp");
  const auto* reloaded_moved = fo4::find_record(reloaded, new_key);
  ASSERT_NE(reloaded_moved, nullptr);
  EXPECT_EQ(reloaded_moved->header.form_id, fo4::RawFormId{0x00000850});
  EXPECT_EQ(editor_id(*reloaded_moved), "Target");
  const auto reloaded_links = fo4::collect_form_links(reloaded);
  const auto reloaded_link =
      std::find_if(reloaded_links.begin(), reloaded_links.end(), [&](const auto& hit) {
        return hit.record == referrer_key && hit.subrecord == fo4::sig_name;
      });
  ASSERT_NE(reloaded_link, reloaded_links.end());
  EXPECT_EQ(reloaded_link->key, new_key);
}

TEST(RecordLifecycle, InjectRefusesForeignRecordAndNonMasterOwnerWithoutMutation) {
  auto plugin = fo4::read_plugin(make_inject_plugin(), "Inject.esp");
  const auto original = fo4::test::original_bytes(plugin);
  EXPECT_EQ(fo4::inject_local_record(plugin,
                                     fo4::FormKey::parse("000800:Master.esp"),
                                     fo4::FormKey::parse("000850:Master.esp")),
            fo4::RecordRenumberStatus::not_local);
  EXPECT_THROW(
      static_cast<void>(fo4::inject_local_record(plugin,
                                                 fo4::FormKey::parse("000800:Inject.esp"),
                                                 fo4::FormKey::parse("000850:Other.esp"))),
      fo4::FormIdError);
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
  EXPECT_NE(fo4::find_record(plugin, fo4::FormKey::parse("000800:Inject.esp")), nullptr);
}

TEST(RecordLifecycle, InjectRefusesTargetAlreadyUsedByPluginWithoutMutation) {
  auto plugin = fo4::read_plugin(make_inject_plugin(), "Inject.esp");
  const auto new_key = fo4::FormKey::parse("000850:Master.esp");
  ASSERT_EQ(fo4::inject_local_record(plugin, fo4::FormKey::parse("000800:Inject.esp"), new_key),
            fo4::RecordRenumberStatus::renumbered);
  const auto original = fo4::test::original_bytes(plugin);
  EXPECT_EQ(fo4::inject_local_record(plugin, fo4::FormKey::parse("000801:Inject.esp"), new_key),
            fo4::RecordRenumberStatus::target_exists);
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
  EXPECT_NE(fo4::find_record(plugin, fo4::FormKey::parse("000801:Inject.esp")), nullptr);
}
