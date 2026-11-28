#include "fo4/environment/environment.hpp"
#include "fo4/environment/world.hpp"
#include "fo4/forms/cell.hpp"
#include "fo4/forms/cell_edit.hpp"
#include "fo4/forms/placed_reference.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/signature.hpp"
#include "byte_helpers.hpp"
#include "cell_edit_fixture.hpp"
#include "plugin_bytes.hpp"
#include "temp_data_dir.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace fo4::test::cell_edit;

}  // namespace

TEST_F(CellEditTest, CopyCellAsOverrideCreatesCellAndWorldspaceChain) {
  const auto env = fo4::env::Environment::load(paths_);
  const auto cell = fo4::FormKey::parse("000803:Base.esm");
  auto patch = empty_edit();

  EXPECT_EQ(fo4::forms::copy_cell_as_override(patch, plugin_named(env, "Base.esm"), cell),
            fo4::forms::CellOverrideStatus::inserted);

  const auto reread = fo4::read_plugin(fo4::write_plugin(patch), "Edit.esp");
  const auto* reread_cell = fo4::find_record(reread, cell);
  ASSERT_NE(reread_cell, nullptr);
  EXPECT_NE(fo4::find_record(reread, fo4::FormKey::parse("000800:Base.esm")), nullptr);
  const auto grid = fo4::forms::cell_grid(*reread_cell);
  ASSERT_TRUE(grid.has_value());
  EXPECT_EQ(grid->first, 2);
  EXPECT_EQ(grid->second, 2);
}

TEST_F(CellEditTest, CopyCellAsOverrideIsIdempotent) {
  const auto env = fo4::env::Environment::load(paths_);
  const auto cell = fo4::FormKey::parse("000803:Base.esm");
  auto patch = empty_edit();

  ASSERT_EQ(fo4::forms::copy_cell_as_override(patch, plugin_named(env, "Base.esm"), cell),
            fo4::forms::CellOverrideStatus::inserted);
  const auto first = fo4::write_plugin(patch);

  EXPECT_EQ(fo4::forms::copy_cell_as_override(patch, plugin_named(env, "Base.esm"), cell),
            fo4::forms::CellOverrideStatus::already_exists);
  EXPECT_TRUE(fo4::write_plugin(patch) == first);
}

TEST_F(CellEditTest, CopyCellAsOverrideRejectsMissingAndNonCellSources) {
  const auto env = fo4::env::Environment::load(paths_);
  auto patch = empty_edit();
  const auto before = fo4::write_plugin(patch);

  EXPECT_EQ(fo4::forms::copy_cell_as_override(patch, plugin_named(env, "Base.esm"), fo4::FormKey::parse("000999:Base.esm")),
            fo4::forms::CellOverrideStatus::source_missing);
  EXPECT_EQ(fo4::forms::copy_cell_as_override(patch, plugin_named(env, "Base.esm"), fo4::FormKey::parse("000812:Base.esm")),
            fo4::forms::CellOverrideStatus::not_a_cell);
  EXPECT_TRUE(fo4::write_plugin(patch) == before);
}

TEST_F(CellEditTest, EnsureOverrideCopiesFromWinningPlugin) {
  const auto env = fo4::env::Environment::load(paths_);
  const auto reference = fo4::FormKey::parse("000812:Base.esm");
  auto patch = empty_edit();

  const auto result = fo4::forms::ensure_reference_override(patch, env, reference);
  EXPECT_EQ(result.status, fo4::forms::EnsureOverrideStatus::copied);
  EXPECT_EQ(result.copied_from, "Patch.esp");

  const auto* copied = fo4::find_record(patch, reference);
  ASSERT_NE(copied, nullptr);
  const auto view = fo4::forms::decode_placed_reference(patch, *copied);
  ASSERT_TRUE(view.has_value());
  EXPECT_FLOAT_EQ(view->transform.x, 99.0f);

  EXPECT_EQ(fo4::forms::ensure_reference_override(patch, env, reference).status,
            fo4::forms::EnsureOverrideStatus::present);
}

TEST_F(CellEditTest, EnsureOverrideRefusesDeletedWinner) {
  const auto env = fo4::env::Environment::load(paths_);
  const auto reference = fo4::FormKey::parse("000813:Base.esm");
  auto patch = empty_edit();

  const auto result = fo4::forms::ensure_reference_override(patch, env, reference);
  EXPECT_EQ(result.status, fo4::forms::EnsureOverrideStatus::reference_deleted);
  EXPECT_NE(result.message.find("Patch.esp"), std::string::npos);
  EXPECT_EQ(fo4::find_record(patch, reference), nullptr);
}

TEST_F(CellEditTest, EnsureOverrideReportsMissingReference) {
  const auto env = fo4::env::Environment::load(paths_);
  const auto reference = fo4::FormKey::parse("000999:Base.esm");
  auto patch = empty_edit();

  const auto result = fo4::forms::ensure_reference_override(patch, env, reference);
  EXPECT_EQ(result.status, fo4::forms::EnsureOverrideStatus::reference_missing);
  EXPECT_FALSE(result.message.empty());
  EXPECT_EQ(fo4::find_record(patch, reference), nullptr);
}

TEST_F(CellEditTest, EditTransformCreatesOverrideAndMovesAcrossExteriorBorder) {
  const auto env = fo4::env::Environment::load(paths_);
  const auto reference = fo4::FormKey::parse("000811:Base.esm");
  const auto source_cell = fo4::FormKey::parse("000802:Base.esm");
  const auto target_cell = fo4::FormKey::parse("000803:Base.esm");
  auto patch = empty_edit();

  fo4::forms::PlacedReferenceEdit edit;
  edit.transform = fo4::forms::PositionRotation{.x = 8292.0f, .y = 8292.0f};
  const auto result = fo4::forms::edit_placed_reference(patch, env, reference, edit);

  EXPECT_EQ(result.status, fo4::forms::PlacedReferenceEditStatus::edited);
  EXPECT_TRUE(result.relocated);
  EXPECT_EQ(result.copied_from, "Base.esm");
  EXPECT_TRUE(has_reference(patch, target_cell, reference));
  EXPECT_FALSE(has_reference(patch, source_cell, reference));

  const auto* moved = fo4::find_record(patch, reference);
  ASSERT_NE(moved, nullptr);
  const auto view = fo4::forms::decode_placed_reference(patch, *moved);
  ASSERT_TRUE(view.has_value());
  EXPECT_FLOAT_EQ(view->transform.x, 8292.0f);
}

TEST_F(CellEditTest, EditAppliesEveryFieldInOneCall) {
  const auto env = fo4::env::Environment::load(paths_);
  const auto reference = fo4::FormKey::parse("000811:Base.esm");
  auto patch = empty_edit();

  fo4::forms::PlacedReferenceEdit edit;
  edit.scale = std::optional<float>{2.0f};
  edit.editor_id = std::string{"CrateMoved"};
  edit.record_flags = 0x00000C00u;
  edit.comments = std::optional<std::string>{"moved by test"};
  edit.location_reference = std::optional<fo4::FormKey>{fo4::FormKey::parse("000820:Base.esm")};
  edit.ownership = fo4::forms::OwnershipEdit{
      .owner = fo4::FormKey::parse("000830:Base.esm"), .no_crime = true, .faction_rank = 2};
  edit.enable_parent = fo4::forms::EnableParentEdit{.parent = fo4::FormKey::parse("000840:Base.esm"), .flags = 1};
  edit.linked_references = std::vector<fo4::forms::LinkedReferenceEdit>{
      fo4::forms::LinkedReferenceEdit{.keyword = fo4::FormKey::parse("000850:Base.esm"),
                                      .reference = fo4::FormKey::parse("000812:Base.esm")}};
  edit.teleport = fo4::forms::TeleportEdit{.door = fo4::FormKey::parse("000860:Base.esm"),
                                           .destination = {.x = 1.0f, .y = 2.0f, .z = 3.0f}};
  edit.primitive = fo4::forms::PrimitiveEdit{.bounds = {1.0f, 2.0f, 3.0f}, .type = 2};

  const auto result = fo4::forms::edit_placed_reference(patch, env, reference, edit);
  EXPECT_EQ(result.status, fo4::forms::PlacedReferenceEditStatus::edited);
  EXPECT_FALSE(result.relocated);
  EXPECT_EQ(result.copied_from, "Base.esm");

  const auto reread = fo4::read_plugin(fo4::write_plugin(patch), "Edit.esp");
  const auto* record = fo4::find_record(reread, reference);
  ASSERT_NE(record, nullptr);
  const auto view = fo4::forms::decode_placed_reference(reread, *record);
  ASSERT_TRUE(view.has_value());
  EXPECT_EQ(view->editor_id, "CrateMoved");
  EXPECT_FLOAT_EQ(view->scale, 2.0f);
  EXPECT_TRUE(record->header.flags.test(fo4::RecordFlags::initially_disabled));
  EXPECT_FALSE(record->header.flags.is_persistent());
  for (const auto* signature :
       {"XSCL", "EDID", "MNAM", "XLRL", "XOWN", "XRNK", "XESP", "XLKR", "XTEL", "XPRM"}) {
    EXPECT_TRUE(has_subrecord(*record, signature)) << signature;
  }
}

TEST_F(CellEditTest, EditTransformOfPersistentReferenceCopiesWorldspaceTopCell) {
  const auto env = fo4::env::Environment::load(paths_);
  const auto reference = fo4::FormKey::parse("000814:Base.esm");
  const auto top_cell = fo4::FormKey::parse("000801:Base.esm");
  const auto source_cell = fo4::FormKey::parse("000802:Base.esm");
  auto patch = empty_edit();

  fo4::forms::PlacedReferenceEdit edit;
  edit.transform = fo4::forms::PositionRotation{.x = 4200.0f, .y = 8300.0f};
  const auto result = fo4::forms::edit_placed_reference(patch, env, reference, edit);

  EXPECT_EQ(result.status, fo4::forms::PlacedReferenceEditStatus::edited);
  EXPECT_TRUE(result.relocated);
  EXPECT_TRUE(has_reference(patch, top_cell, reference));
  EXPECT_FALSE(has_reference(patch, source_cell, reference));
}

TEST_F(CellEditTest, FailedEditLeavesPatchUnchanged) {
  const auto env = fo4::env::Environment::load(paths_);
  const auto reference = fo4::FormKey::parse("000812:Base.esm");
  auto patch = empty_edit();
  const auto before = fo4::write_plugin(patch);

  fo4::forms::PlacedReferenceEdit edit;
  edit.scale = std::optional<float>{3.0f};
  edit.teleport = fo4::forms::TeleportEdit{.door = fo4::FormKey::parse("000860:Base.esm")};
  const auto result = fo4::forms::edit_placed_reference(patch, env, reference, edit);

  EXPECT_EQ(result.status, fo4::forms::PlacedReferenceEditStatus::field_absent);
  EXPECT_EQ(result.copied_from, "Patch.esp");
  EXPECT_TRUE(fo4::write_plugin(patch) == before);
  EXPECT_EQ(fo4::find_record(patch, reference), nullptr);
}

TEST_F(CellEditTest, EditReportsMissingAndDeletedReferencesWithoutChangingPatch) {
  const auto env = fo4::env::Environment::load(paths_);
  auto patch = empty_edit();
  const auto before = fo4::write_plugin(patch);

  fo4::forms::PlacedReferenceEdit edit;
  edit.scale = std::optional<float>{3.0f};
  const auto missing = fo4::forms::edit_placed_reference(patch, env, fo4::FormKey::parse("000999:Base.esm"), edit);
  EXPECT_EQ(missing.status, fo4::forms::PlacedReferenceEditStatus::reference_missing);
  EXPECT_FALSE(missing.message.empty());

  const auto deleted = fo4::forms::edit_placed_reference(patch, env, fo4::FormKey::parse("000813:Base.esm"), edit);
  EXPECT_EQ(deleted.status, fo4::forms::PlacedReferenceEditStatus::reference_deleted);
  EXPECT_NE(deleted.message.find("Patch.esp"), std::string::npos);
  EXPECT_TRUE(fo4::write_plugin(patch) == before);
}

TEST_F(CellEditTest, DuplicateMasterReferenceCreatesCellOverrideAndNewKey) {
  const auto env = fo4::env::Environment::load(paths_);
  const auto reference = fo4::FormKey::parse("000811:Base.esm");
  const auto cell = fo4::FormKey::parse("000802:Base.esm");
  auto patch = empty_edit();

  const auto result = fo4::forms::duplicate_placed_reference(patch, env, reference, {});

  EXPECT_EQ(result.status, fo4::forms::DuplicateStatus::duplicated);
  EXPECT_TRUE(result.new_reference.owner() == patch.key);
  EXPECT_NE(fo4::find_record(patch, cell), nullptr);
  EXPECT_TRUE(has_reference(patch, cell, result.new_reference));
  EXPECT_EQ(fo4::find_record(patch, reference), nullptr);
  const auto* copy = fo4::find_record(patch, result.new_reference);
  ASSERT_NE(copy, nullptr);
  EXPECT_FALSE(has_subrecord(*copy, "EDID"));
}

TEST_F(CellEditTest, DuplicateOfPatchLocalReferenceDoesNotAlias) {
  const auto env = fo4::env::Environment::load(paths_);
  const auto reference = fo4::FormKey::parse("000812:Base.esm");
  auto patch = empty_edit();
  ASSERT_EQ(fo4::forms::ensure_reference_override(patch, env, reference).status,
            fo4::forms::EnsureOverrideStatus::copied);
  ASSERT_TRUE(fo4::forms::set_placed_reference_transform(
      patch, reference, fo4::forms::PositionRotation{.x = 123.0f, .y = 8300.0f}));

  const auto result = fo4::forms::duplicate_placed_reference(patch, env, reference, {});

  ASSERT_EQ(result.status, fo4::forms::DuplicateStatus::duplicated);
  EXPECT_FALSE(result.new_reference == reference);
  const auto* original = fo4::find_record(patch, reference);
  const auto* copy = fo4::find_record(patch, result.new_reference);
  ASSERT_NE(original, nullptr);
  ASSERT_NE(copy, nullptr);
  const auto original_view = fo4::forms::decode_placed_reference(patch, *original);
  const auto copy_view = fo4::forms::decode_placed_reference(patch, *copy);
  ASSERT_TRUE(original_view.has_value());
  ASSERT_TRUE(copy_view.has_value());
  EXPECT_FLOAT_EQ(original_view->transform.x, 123.0f);
  EXPECT_FLOAT_EQ(copy_view->transform.x, 123.0f);

  const auto* winner = env.winner(reference);
  ASSERT_NE(winner, nullptr);
  const auto winner_view = fo4::forms::decode_placed_reference(env.plugin(winner->plugin), *winner->record);
  ASSERT_TRUE(winner_view.has_value());
  EXPECT_FLOAT_EQ(winner_view->transform.x, 99.0f);
}

TEST_F(CellEditTest, DuplicateOfDuplicateCreatedLocalReferenceUsesPatchRecord) {
  const auto env = fo4::env::Environment::load(paths_);
  const auto cell = fo4::FormKey::parse("000802:Base.esm");
  auto patch = empty_edit();

  const auto first = fo4::forms::duplicate_placed_reference(
      patch, env, fo4::FormKey::parse("000811:Base.esm"), {});
  ASSERT_EQ(first.status, fo4::forms::DuplicateStatus::duplicated);
  const auto second = fo4::forms::duplicate_placed_reference(patch, env, first.new_reference, {});

  EXPECT_EQ(second.status, fo4::forms::DuplicateStatus::duplicated);
  EXPECT_FALSE(second.new_reference == first.new_reference);
  EXPECT_TRUE(has_reference(patch, cell, second.new_reference));
}

TEST_F(CellEditTest, DuplicateOfRelocatedOverrideStaysInPatchCell) {
  const auto env = fo4::env::Environment::load(paths_);
  const auto reference = fo4::FormKey::parse("000811:Base.esm");
  const auto target_cell = fo4::FormKey::parse("000803:Base.esm");
  auto patch = empty_edit();

  fo4::forms::PlacedReferenceEdit edit;
  edit.transform = fo4::forms::PositionRotation{.x = 8292.0f, .y = 8292.0f};
  ASSERT_EQ(fo4::forms::edit_placed_reference(patch, env, reference, edit).status,
            fo4::forms::PlacedReferenceEditStatus::edited);

  const auto result = fo4::forms::duplicate_placed_reference(patch, env, reference, {});

  ASSERT_EQ(result.status, fo4::forms::DuplicateStatus::duplicated);
  EXPECT_TRUE(has_reference(patch, target_cell, result.new_reference));
}

TEST_F(CellEditTest, DuplicateAppliesOffset) {
  const auto env = fo4::env::Environment::load(paths_);
  const auto reference = fo4::FormKey::parse("000811:Base.esm");
  const auto cell = fo4::FormKey::parse("000802:Base.esm");
  auto patch = empty_edit();

  fo4::forms::DuplicateOptions options;
  options.offset.x = 4000.0f;
  options.offset.z = 5.0f;
  options.offset.rz = 90.0f;
  const auto result = fo4::forms::duplicate_placed_reference(patch, env, reference, options);

  ASSERT_EQ(result.status, fo4::forms::DuplicateStatus::duplicated);
  EXPECT_TRUE(has_reference(patch, cell, result.new_reference));
  const auto* copy = fo4::find_record(patch, result.new_reference);
  ASSERT_NE(copy, nullptr);
  const auto view = fo4::forms::decode_placed_reference(patch, *copy);
  ASSERT_TRUE(view.has_value());
  EXPECT_FLOAT_EQ(view->transform.x, 8196.0f);
  EXPECT_FLOAT_EQ(view->transform.y, 8292.0f);
  EXPECT_FLOAT_EQ(view->transform.z, 5.0f);
  EXPECT_FLOAT_EQ(view->transform.rz, 90.0f);
}

TEST_F(CellEditTest, DuplicateKeepsCellContainmentPersistentVsTemporary) {
  const auto env = fo4::env::Environment::load(paths_);
  const auto cell = fo4::FormKey::parse("000802:Base.esm");
  auto patch = empty_edit();

  const auto temporary = fo4::forms::duplicate_placed_reference(
      patch, env, fo4::FormKey::parse("000811:Base.esm"), {});
  const auto persistent = fo4::forms::duplicate_placed_reference(
      patch, env, fo4::FormKey::parse("000815:Base.esm"), {});

  ASSERT_EQ(temporary.status, fo4::forms::DuplicateStatus::duplicated);
  ASSERT_EQ(persistent.status, fo4::forms::DuplicateStatus::duplicated);
  EXPECT_TRUE(has_reference(patch, cell, temporary.new_reference));
  EXPECT_TRUE(has_reference(patch, cell, persistent.new_reference));
  const auto* temporary_copy = fo4::find_record(patch, temporary.new_reference);
  const auto* persistent_copy = fo4::find_record(patch, persistent.new_reference);
  ASSERT_NE(temporary_copy, nullptr);
  ASSERT_NE(persistent_copy, nullptr);
  EXPECT_FALSE(temporary_copy->header.flags.is_persistent());
  EXPECT_TRUE(persistent_copy->header.flags.is_persistent());
  EXPECT_EQ(bucket_type_of(patch, temporary.new_reference),
            static_cast<std::int32_t>(fo4::GroupType::cell_temporary_children));
  EXPECT_EQ(bucket_type_of(patch, persistent.new_reference),
            static_cast<std::int32_t>(fo4::GroupType::cell_persistent_children));
}

TEST_F(CellEditTest, DeleteLocalReferenceRemovesRecord) {
  const auto env = fo4::env::Environment::load(paths_);
  auto patch = empty_edit();
  const auto copy = fo4::forms::duplicate_placed_reference(
      patch, env, fo4::FormKey::parse("000811:Base.esm"), {});
  ASSERT_EQ(copy.status, fo4::forms::DuplicateStatus::duplicated);

  const auto result = fo4::forms::delete_placed_reference(patch, env, copy.new_reference);

  EXPECT_EQ(result.status, fo4::forms::DeleteStatus::deleted);
  EXPECT_EQ(fo4::find_record(patch, copy.new_reference), nullptr);
}

TEST_F(CellEditTest, DeleteReferencedLocalReferenceReportsReferenced) {
  const auto env = fo4::env::Environment::load(paths_);
  auto patch = empty_edit();
  const auto copy = fo4::forms::duplicate_placed_reference(
      patch, env, fo4::FormKey::parse("000811:Base.esm"), {});
  ASSERT_EQ(copy.status, fo4::forms::DuplicateStatus::duplicated);
  fo4::forms::PlacedReferenceEdit edit;
  edit.location_reference = std::optional<fo4::FormKey>{copy.new_reference};
  ASSERT_EQ(fo4::forms::edit_placed_reference(patch, env, fo4::FormKey::parse("000811:Base.esm"), edit).status,
            fo4::forms::PlacedReferenceEditStatus::edited);

  const auto result = fo4::forms::delete_placed_reference(patch, env, copy.new_reference);

  EXPECT_EQ(result.status, fo4::forms::DeleteStatus::referenced);
  EXPECT_NE(fo4::find_record(patch, copy.new_reference), nullptr);
}

TEST_F(CellEditTest, DeleteMasterReferenceWritesDeletedOverrideKeepingNameOnly) {
  const auto env = fo4::env::Environment::load(paths_);
  const auto reference = fo4::FormKey::parse("000811:Base.esm");
  auto patch = empty_edit();

  EXPECT_EQ(fo4::forms::delete_placed_reference(patch, env, reference).status,
            fo4::forms::DeleteStatus::marked_deleted);

  const auto* record = fo4::find_record(patch, reference);
  ASSERT_NE(record, nullptr);
  EXPECT_TRUE(record->header.flags.is_deleted());
  EXPECT_TRUE(has_subrecord(*record, "NAME"));
  EXPECT_FALSE(has_subrecord(*record, "DATA"));
  EXPECT_FALSE(has_subrecord(*record, "EDID"));
  EXPECT_EQ(fo4::forms::delete_placed_reference(patch, env, reference).status,
            fo4::forms::DeleteStatus::already_deleted);
}

TEST_F(CellEditTest, DuplicateOfMissingReferenceIsRefusedWithoutChangingPatch) {
  const auto env = fo4::env::Environment::load(paths_);
  auto patch = empty_edit();
  const auto before = fo4::write_plugin(patch);

  EXPECT_EQ(fo4::forms::duplicate_placed_reference(patch, env, fo4::FormKey::parse("000999:Base.esm"), {}).status,
            fo4::forms::DuplicateStatus::reference_missing);
  EXPECT_TRUE(fo4::write_plugin(patch) == before);
}

TEST_F(CellEditTest, DuplicateOfDeletedWinnerIsRefusedWithoutChangingPatch) {
  const auto env = fo4::env::Environment::load(paths_);
  auto patch = empty_edit();
  const auto before = fo4::write_plugin(patch);

  EXPECT_EQ(fo4::forms::duplicate_placed_reference(patch, env, fo4::FormKey::parse("000813:Base.esm"), {}).status,
            fo4::forms::DuplicateStatus::reference_deleted);
  EXPECT_TRUE(fo4::write_plugin(patch) == before);
}

TEST_F(CellEditTest, DuplicateOfDeletedOverrideStubIsRefusedInsteadOfMalformed) {
  const auto env = fo4::env::Environment::load(paths_);
  const auto reference = fo4::FormKey::parse("000811:Base.esm");
  auto patch = empty_edit();
  ASSERT_EQ(fo4::forms::delete_placed_reference(patch, env, reference).status,
            fo4::forms::DeleteStatus::marked_deleted);
  const auto before = fo4::write_plugin(patch);

  EXPECT_EQ(fo4::forms::duplicate_placed_reference(patch, env, reference, {}).status,
            fo4::forms::DuplicateStatus::reference_deleted);
  EXPECT_TRUE(fo4::write_plugin(patch) == before);
}

TEST_F(CellEditTest, DeleteOfMissingReferenceIsRefusedWithoutChangingPatch) {
  const auto env = fo4::env::Environment::load(paths_);
  auto patch = empty_edit();
  const auto before = fo4::write_plugin(patch);

  EXPECT_EQ(fo4::forms::delete_placed_reference(patch, env, fo4::FormKey::parse("000999:Base.esm")).status,
            fo4::forms::DeleteStatus::reference_missing);
  EXPECT_TRUE(fo4::write_plugin(patch) == before);
}

TEST_F(CellEditTest, DeleteOfMasterDeletedReferenceReportsAlreadyDeletedWithoutChangingPatch) {
  const auto env = fo4::env::Environment::load(paths_);
  auto patch = empty_edit();
  const auto before = fo4::write_plugin(patch);

  EXPECT_EQ(fo4::forms::delete_placed_reference(patch, env, fo4::FormKey::parse("000813:Base.esm")).status,
            fo4::forms::DeleteStatus::already_deleted);
  EXPECT_TRUE(fo4::write_plugin(patch) == before);
}
