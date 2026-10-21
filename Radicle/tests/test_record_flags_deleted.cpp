#include "fo4/forms/placed_reference.hpp"
#include "fo4/plugin/record_flags.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/signature.hpp"
#include "plugin_bytes.hpp"
#include "record_flags_fixture.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace fo4::test;

namespace {

Bytes weapon_plugin() {
  fo4::test::ByteWriter out;
  write_header(out, {"Base.esm"});
  const auto top = fo4::test::begin_group(out, "WEAP", 0);
  put_record(out, "WEAP", 0, 0x800,
             {{"EDID", std::string("Blade", 6)}, {"DATA", std::string(4, '\0')}});
  fo4::test::end_group(out, top);
  return out.bytes;
}

}  // namespace

TEST(RecordFlags, MarkOverrideDeletedKeepsOnlyNameOnMasterReferenceAndRoundTrips) {
  const auto layout = CellPlugin{
      .name = "Patch.esp",
      .masters = {"Base.esm"},
      .cell_form = 0x01000900,
      .ref_flags = fo4::RecordFlags::compressed,
      .ref_subs =
          {
              {"NAME", std::string{'\x01', '\x08', '\0', '\0'}},
              {"DATA", std::string(24, '\0')},
              {"XSCL", std::string{'\0', '\0', '\x80', '\x3f'}},
          },
  };
  auto plugin = load(layout);
  const auto ref = fo4::FormKey::parse("000800:Base.esm");
  ASSERT_EQ(fo4::mark_override_deleted(plugin, ref), fo4::MarkDeletedStatus::marked);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* record = fo4::find_record(reread, ref);
  ASSERT_NE(record, nullptr);
  EXPECT_TRUE(record->header.flags.is_deleted());
  EXPECT_FALSE(record->header.flags.is_compressed());
  ASSERT_EQ(record->subrecords.size(), 1u);
  EXPECT_EQ(record->subrecords[0].signature, fo4::Signature::from_string("NAME"));
  ASSERT_EQ(record->subrecords[0].data.size(), 4u);
  EXPECT_EQ(std::to_integer<int>(record->subrecords[0].data[1]), 0x08);
}

TEST(RecordFlags, MarkOverrideDeletedLeavesNoSubrecordsOnNonReference) {
  auto plugin = fo4::read_plugin(weapon_plugin(), "Patch.esp");
  const auto weapon = fo4::FormKey::parse("000800:Base.esm");
  ASSERT_EQ(fo4::mark_override_deleted(plugin, weapon), fo4::MarkDeletedStatus::marked);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* record = fo4::find_record(reread, weapon);
  ASSERT_NE(record, nullptr);
  EXPECT_TRUE(record->header.flags.is_deleted());
  EXPECT_TRUE(record->subrecords.empty());
}

TEST(RecordFlags, LocalRecordAndAlreadyDeletedReturnWithoutChange) {
  auto local = load({});
  const auto local_before = fo4::write_plugin(local);
  const auto local_state = fo4::test::plugin_state(local);
  EXPECT_EQ(fo4::mark_override_deleted(local, fo4::FormKey::parse("000800:Cells.esp")),
            fo4::MarkDeletedStatus::local_record);
  EXPECT_EQ(fo4::write_plugin(local), local_before);
  EXPECT_EQ(fo4::test::plugin_state(local), local_state);

  auto deleted = load(CellPlugin{
      .masters = {"Base.esm"},
      .cell_form = 0x01000900,
      .ref_flags = fo4::RecordFlags::deleted,
  });
  const auto deleted_before = fo4::write_plugin(deleted);
  const auto deleted_state = fo4::test::plugin_state(deleted);
  EXPECT_EQ(fo4::mark_override_deleted(deleted, fo4::FormKey::parse("000800:Base.esm")),
            fo4::MarkDeletedStatus::already_deleted);
  EXPECT_EQ(fo4::write_plugin(deleted), deleted_before);
  EXPECT_EQ(fo4::test::plugin_state(deleted), deleted_state);
}

TEST(RecordFlags, UndeleteClearsDeletedDisablesAndParentsToPlayerReference) {
  const CellPlugin master{
      .name = "Fallout4.esm",
      .ref_subs =
          {
              {"NAME", std::string{'\x01', '\x08', '\0', '\0'}},
              {"DATA", position_data(1.0f, 2.0f, 3.0f)},
              {"XSCL", std::string{'\0', '\0', '\x80', '\x3f'}},
          },
  };
  const CellPlugin patch{
      .name = "Patch.esp",
      .masters = {"Fallout4.esm"},
      .cell_form = 0x01000900,
      .ref_flags = fo4::RecordFlags::deleted | fo4::RecordFlags::compressed,
      .ref_subs =
          {
              {"NAME", std::string{'\x01', '\x08', '\0', '\0'}},
              {"DATA", position_data(1.0f, 2.0f, 3.0f)},
              {"XESP", std::string{'\x00', '\x08', '\0', '\0', '\0', '\0', '\0', '\0'}},
              {"XTEL", std::string(32, '\0')},
              {"XSCL", std::string{'\0', '\0', '\x80', '\x3f'}},
          },
  };
  auto plugin = load(patch);
  const auto ref = fo4::FormKey::parse("000800:Fallout4.esm");
  ASSERT_EQ(fo4::undelete_and_disable_reference(plugin, load_order(master, patch), ref),
            fo4::UndeleteStatus::undeleted);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* record = fo4::find_record(reread, ref);
  ASSERT_NE(record, nullptr);
  EXPECT_FALSE(record->header.flags.is_deleted());
  EXPECT_TRUE(record->header.flags.is_compressed());
  EXPECT_TRUE(record->header.flags.test(fo4::RecordFlags::initially_disabled));
  const auto* xesp = subrecord_named(*record, "XESP");
  ASSERT_NE(xesp, nullptr);
  EXPECT_EQ(as_text(xesp->data), std::string("\x14\0\0\0\x01\0\0\0", 8));
  EXPECT_EQ(subrecord_named(*record, "XTEL"), nullptr);
  EXPECT_NE(subrecord_named(*record, "XSCL"), nullptr);
  const auto transform = fo4::forms::decode_placed_reference(reread, *record)->transform;
  EXPECT_FLOAT_EQ(transform.x, 1.0f);
  EXPECT_FLOAT_EQ(transform.y, 2.0f);
  EXPECT_FLOAT_EQ(transform.z, -30000.0f);
  const auto* bucket = parent_group(reread, ref);
  ASSERT_NE(bucket, nullptr);
  EXPECT_EQ(bucket->header.group_type, static_cast<std::int32_t>(fo4::GroupType::cell_temporary_children));
}

TEST(RecordFlags, UndeleteKeepsPersistentReferenceZAndBucket) {
  auto plugin = load(CellPlugin{
      .bucket = fo4::GroupType::cell_persistent_children,
      .ref_flags = fo4::RecordFlags::deleted | fo4::RecordFlags::persistent,
      .ref_subs = {{"NAME", std::string{'\x01', '\x08', '\0', '\0'}},
                   {"DATA", position_data(1.0f, 2.0f, 3.0f)}},
  });
  const auto ref = fo4::FormKey::parse("000800:Cells.esp");
  ASSERT_EQ(fo4::undelete_and_disable_reference(plugin, fo4::env::Environment{}, ref),
            fo4::UndeleteStatus::undeleted);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Cells.esp");
  const auto* record = fo4::find_record(reread, ref);
  ASSERT_NE(record, nullptr);
  EXPECT_FALSE(record->header.flags.is_deleted());
  EXPECT_TRUE(record->header.flags.is_persistent());
  EXPECT_TRUE(record->header.flags.test(fo4::RecordFlags::initially_disabled));
  EXPECT_FLOAT_EQ(fo4::forms::decode_placed_reference(reread, *record)->transform.z, 3.0f);
  const auto* bucket = parent_group(reread, ref);
  ASSERT_NE(bucket, nullptr);
  EXPECT_EQ(bucket->header.group_type, static_cast<std::int32_t>(fo4::GroupType::cell_persistent_children));
}

TEST(RecordFlags, UndeleteRefusesNonDeletedReferenceWithoutChange) {
  auto plugin = load({});
  const auto ref = fo4::FormKey::parse("000800:Cells.esp");
  const auto before = fo4::write_plugin(plugin);
  const auto state = fo4::test::plugin_state(plugin);
  EXPECT_EQ(fo4::undelete_and_disable_reference(plugin, fo4::env::Environment{}, ref),
            fo4::UndeleteStatus::not_deleted);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
  EXPECT_EQ(fo4::test::plugin_state(plugin), state);
}

TEST(RecordFlags, UndeleteRefusesDeletedStubWithoutDataWithoutChange) {
  auto plugin = load(CellPlugin{
      .ref_flags = fo4::RecordFlags::deleted,
      .ref_subs = {{"NAME", std::string{'\x01', '\x08', '\0', '\0'}}},
  });
  const auto ref = fo4::FormKey::parse("000800:Cells.esp");
  const auto before = fo4::write_plugin(plugin);
  const auto state = fo4::test::plugin_state(plugin);
  EXPECT_EQ(fo4::undelete_and_disable_reference(plugin, fo4::env::Environment{}, ref),
            fo4::UndeleteStatus::incomplete_reference);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
  EXPECT_EQ(fo4::test::plugin_state(plugin), state);
}

TEST(RecordFlags, UndeleteRefusesNonReferenceWithoutChange) {
  auto plugin = fo4::read_plugin(weapon_plugin(), "Patch.esp");
  const auto weapon = fo4::FormKey::parse("000800:Base.esm");
  const auto before = fo4::write_plugin(plugin);
  const auto state = fo4::test::plugin_state(plugin);
  EXPECT_EQ(fo4::undelete_and_disable_reference(plugin, fo4::env::Environment{}, weapon),
            fo4::UndeleteStatus::not_a_reference);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
  EXPECT_EQ(fo4::test::plugin_state(plugin), state);
}

TEST(RecordFlags, UndeleteRefusesMissingFormKeyWithoutChange) {
  auto plugin = load({});
  const auto missing = fo4::FormKey::parse("000999:Cells.esp");
  const auto before = fo4::write_plugin(plugin);
  const auto state = fo4::test::plugin_state(plugin);
  EXPECT_EQ(fo4::undelete_and_disable_reference(plugin, fo4::env::Environment{}, missing),
            fo4::UndeleteStatus::record_missing);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
  EXPECT_EQ(fo4::test::plugin_state(plugin), state);
}

TEST(RecordFlags, UndeleteRestoresStrippedContentFromTheWinningMasterVersion) {
  const CellPlugin master{
      .name = "Fallout4.esm",
      .ref_subs =
          {
              {"NAME", std::string{'\x01', '\x08', '\0', '\0'}},
              {"DATA", position_data(1.0f, 2.0f, 3.0f)},
              {"XESP", std::string{'\x00', '\x08', '\0', '\0', '\0', '\0', '\0', '\0'}},
              {"XTEL", std::string(32, '\0')},
              {"XSCL", std::string{'\0', '\0', '\x80', '\x3f'}},
          },
  };
  const CellPlugin patch{
      .name = "Patch.esp",
      .masters = {"Fallout4.esm"},
      .cell_form = 0x01000900,
      .ref_flags = fo4::RecordFlags::deleted,
      .ref_subs = {{"NAME", std::string{'\x01', '\x08', '\0', '\0'}}},
  };
  auto plugin = load(patch);
  const auto ref = fo4::FormKey::parse("000800:Fallout4.esm");
  ASSERT_EQ(fo4::undelete_and_disable_reference(plugin, load_order(master, patch), ref),
            fo4::UndeleteStatus::undeleted);

  const auto bytes = fo4::write_plugin(plugin);
  const auto reread = fo4::read_plugin(bytes, "Patch.esp");
  const auto* record = fo4::find_record(reread, ref);
  ASSERT_NE(record, nullptr);
  EXPECT_FALSE(record->header.flags.is_deleted());
  EXPECT_TRUE(record->header.flags.test(fo4::RecordFlags::initially_disabled));
  EXPECT_NE(subrecord_named(*record, "XSCL"), nullptr);
  EXPECT_EQ(subrecord_named(*record, "XTEL"), nullptr);
  const auto* xesp = subrecord_named(*record, "XESP");
  ASSERT_NE(xesp, nullptr);
  EXPECT_EQ(as_text(xesp->data), std::string("\x14\0\0\0\x01\0\0\0", 8));
  const auto transform = fo4::forms::decode_placed_reference(reread, *record)->transform;
  EXPECT_FLOAT_EQ(transform.x, 1.0f);
  EXPECT_FLOAT_EQ(transform.y, 2.0f);
  EXPECT_FLOAT_EQ(transform.z, -30000.0f);
  EXPECT_EQ(fo4::write_plugin(reread), bytes);
}

TEST(RecordFlags, UndeleteParentsToTheLoadOrderPlayerWhenFallout4IsNotAMaster) {
  const CellPlugin master{.name = "Base.esm"};
  const CellPlugin patch{
      .name = "Patch.esp",
      .masters = {"Base.esm"},
      .cell_form = 0x01000900,
      .ref_flags = fo4::RecordFlags::deleted,
      .ref_subs = {{"NAME", std::string{'\x01', '\x08', '\0', '\0'}},
                   {"DATA", position_data(1.0f, 2.0f, 3.0f)}},
  };
  auto plugin = load(patch);
  const auto ref = fo4::FormKey::parse("000800:Base.esm");
  ASSERT_EQ(fo4::undelete_and_disable_reference(plugin, load_order(master, patch), ref),
            fo4::UndeleteStatus::undeleted);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  ASSERT_EQ(reread.header.masters.size(), 2u);
  EXPECT_EQ(reread.header.masters[0].key, fo4::PluginKey::from_filename("Fallout4.esm"));
  const auto* record = fo4::find_record(reread, ref);
  ASSERT_NE(record, nullptr);
  const auto* xesp = subrecord_named(*record, "XESP");
  ASSERT_NE(xesp, nullptr);
  EXPECT_EQ(resolved_link(reread, *xesp), fo4::FormKey::parse("000014:Fallout4.esm"));
  const auto* name = subrecord_named(*record, "NAME");
  ASSERT_NE(name, nullptr);
  EXPECT_EQ(resolved_link(reread, *name), fo4::FormKey::parse("000801:Base.esm"));
}

TEST(RecordFlags, UndeleteRemapsMasterFormLinksIntoTheTargetMasterSpace) {
  auto plugin = load(two_level_patch_plugin());
  const auto ref = fo4::FormKey::parse("000800:Machine.esm");
  ASSERT_EQ(fo4::undelete_and_disable_reference(plugin, two_level_load_order(), ref),
            fo4::UndeleteStatus::undeleted);
  const auto* name = subrecord_named(*fo4::find_record(plugin, ref), "NAME");
  ASSERT_NE(name, nullptr);
  EXPECT_EQ(resolved_link(plugin, *name), fo4::FormKey::parse("000801:Machine.esm"));
}

TEST(RecordFlags, UndeleteRefusesInjectedReferenceWithoutChange) {
  const CellPlugin master{.name = "Fallout4.esm", .ref_form = 0x801};
  const CellPlugin patch{
      .name = "Patch.esp",
      .masters = {"Fallout4.esm"},
      .cell_form = 0x01000900,
      .ref_flags = fo4::RecordFlags::deleted,
      .ref_subs = {{"NAME", std::string{'\x01', '\x08', '\0', '\0'}},
                   {"DATA", position_data(1.0f, 2.0f, 3.0f)}},
  };
  auto plugin = load(patch);
  const auto ref = fo4::FormKey::parse("000800:Fallout4.esm");
  const auto before = fo4::write_plugin(plugin);
  EXPECT_EQ(fo4::undelete_and_disable_reference(plugin, load_order(master, patch), ref),
            fo4::UndeleteStatus::injected_record);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(RecordFlags, MarkOverrideDeletedClearsPartialFormOnlyWhereItCanBePartial) {
  auto plugin = load(CellPlugin{
      .name = "Patch.esp",
      .masters = {"Base.esm"},
      .cell_form = 0x900,
      .cell_flags = fo4::RecordFlags::partial_form,
      .ref_flags = fo4::RecordFlags::partial_form,
  });
  const auto cell = fo4::FormKey::parse("000900:Base.esm");
  const auto ref = fo4::FormKey::parse("000800:Base.esm");
  ASSERT_EQ(fo4::mark_override_deleted(plugin, cell), fo4::MarkDeletedStatus::marked);
  ASSERT_EQ(fo4::mark_override_deleted(plugin, ref), fo4::MarkDeletedStatus::marked);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* cell_record = fo4::find_record(reread, cell);
  ASSERT_NE(cell_record, nullptr);
  EXPECT_TRUE(cell_record->header.flags.is_deleted());
  EXPECT_FALSE(cell_record->header.flags.test(fo4::RecordFlags::partial_form));
  const auto* ref_record = fo4::find_record(reread, ref);
  ASSERT_NE(ref_record, nullptr);
  EXPECT_TRUE(ref_record->header.flags.is_deleted());
  EXPECT_TRUE(ref_record->header.flags.test(fo4::RecordFlags::partial_form));
}

TEST(RecordFlags, UndeleteRefusesUndecodableMasterVersionWithoutChange) {
  const CellPlugin master{.name = "Fallout4.esm"};
  const CellPlugin temporary{
      .name = "Patch.esp",
      .masters = {"Fallout4.esm"},
      .cell_form = 0x01000900,
      .ref_flags = fo4::RecordFlags::deleted,
      .ref_subs = {{"NAME", std::string{'\x01', '\x08', '\0', '\0'}}},
  };
  auto persistent = temporary;
  persistent.bucket = fo4::GroupType::cell_persistent_children;
  persistent.ref_flags = fo4::RecordFlags::deleted | fo4::RecordFlags::persistent;
  for (const auto& patch : {temporary, persistent}) {
    auto plugin = load(patch);
    const auto ref = fo4::FormKey::parse("000800:Fallout4.esm");
    const auto before = fo4::write_plugin(plugin);
    EXPECT_EQ(fo4::undelete_and_disable_reference(
                  plugin, load_order_with_undecodable_reference(master, patch), ref),
              fo4::UndeleteStatus::incomplete_reference);
    EXPECT_EQ(fo4::write_plugin(plugin), before);
  }
}

TEST(RecordFlags, UndeleteRefusesMasterVersionWithoutNameOrDataWithoutChange) {
  const CellPlugin master{.name = "Fallout4.esm",
                          .ref_subs = {{"XSCL", std::string{'\0', '\0', '\x80', '\x3f'}}}};
  const CellPlugin patch{
      .name = "Patch.esp",
      .masters = {"Fallout4.esm"},
      .cell_form = 0x01000900,
      .ref_flags = fo4::RecordFlags::deleted,
      .ref_subs = {{"NAME", std::string{'\x01', '\x08', '\0', '\0'}}},
  };
  auto plugin = load(patch);
  const auto ref = fo4::FormKey::parse("000800:Fallout4.esm");
  const auto before = fo4::write_plugin(plugin);
  EXPECT_EQ(fo4::undelete_and_disable_reference(plugin, load_order(master, patch), ref),
            fo4::UndeleteStatus::incomplete_reference);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(RecordFlags, UndeleteTreatsOverrideWhoseOwnerIsNotLoadedAsInjected) {
  auto plugin = load(CellPlugin{
      .name = "Patch.esp",
      .masters = {"Base.esm"},
      .cell_form = 0x01000900,
      .ref_flags = fo4::RecordFlags::deleted,
      .ref_subs = {{"NAME", std::string{'\x01', '\x08', '\0', '\0'}},
                   {"DATA", position_data(1.0f, 2.0f, 3.0f)}},
  });
  const auto ref = fo4::FormKey::parse("000800:Base.esm");
  const auto before = fo4::write_plugin(plugin);
  EXPECT_EQ(fo4::undelete_and_disable_reference(plugin, fo4::env::Environment{}, ref),
            fo4::UndeleteStatus::injected_record);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}
