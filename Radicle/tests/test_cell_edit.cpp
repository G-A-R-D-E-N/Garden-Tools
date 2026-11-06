#include "fo4/environment/environment.hpp"
#include "fo4/environment/world.hpp"
#include "fo4/forms/cell.hpp"
#include "fo4/forms/cell_edit.hpp"
#include "fo4/forms/placed_reference.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/signature.hpp"
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

struct Sub {
  std::string signature;
  std::vector<std::byte> data;
};

struct Node {
  bool group{false};
  std::string signature;
  std::uint32_t form_id{0};
  std::uint32_t flags{0};
  std::vector<Sub> subrecords;
  std::uint32_t label{0};
  std::int32_t group_type{0};
  std::vector<Node> children;
};

Node rec(std::string signature, std::uint32_t form_id, std::vector<Sub> subrecords = {},
         std::uint32_t flags = 0) {
  Node node;
  node.signature = std::move(signature);
  node.form_id = form_id;
  node.flags = flags;
  node.subrecords = std::move(subrecords);
  return node;
}

Node grp(std::uint32_t label, std::int32_t type, std::vector<Node> children) {
  Node node;
  node.group = true;
  node.label = label;
  node.group_type = type;
  node.children = std::move(children);
  return node;
}

Node top(std::string_view signature, std::vector<Node> children) {
  std::uint32_t label = 0;
  std::memcpy(&label, signature.data(), 4);
  return grp(label, 0, std::move(children));
}

std::vector<std::byte> bytes_of(std::initializer_list<std::uint32_t> words) {
  fo4::test::ByteWriter out;
  for (const auto word : words) out.u32(word);
  return out.bytes;
}

std::vector<std::byte> floats(std::initializer_list<float> values) {
  fo4::test::ByteWriter out;
  for (const auto value : values) out.f32(value);
  return out.bytes;
}

std::vector<std::byte> zstring(std::string_view text) {
  std::vector<std::byte> bytes;
  for (const char ch : text) bytes.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  bytes.push_back(std::byte{0});
  return bytes;
}

std::vector<std::byte> xtel_bytes() {
  fo4::test::ByteWriter out;
  out.u32(0x820);
  for (int i = 0; i < 6; ++i) out.f32(0.0f);
  out.u32(0);
  return out.bytes;
}

std::vector<std::byte> xprm_bytes() {
  fo4::test::ByteWriter out;
  for (int i = 0; i < 3; ++i) out.f32(1.0f);
  for (int i = 0; i < 5; ++i) out.u32(0);
  return out.bytes;
}

std::vector<std::byte> lighting_bytes(std::size_t size) {
  std::vector<std::byte> bytes(size, std::byte{0});
  for (std::size_t i = 40; i < size; ++i) bytes[i] = std::byte{0xAB};
  return bytes;
}

void write_node(fo4::test::ByteWriter& out, const Node& node) {
  if (node.group) {
    std::string label(4, '\0');
    std::memcpy(label.data(), &node.label, 4);
    const auto header = fo4::test::begin_group(out, label, node.group_type);
    for (const auto& child : node.children) write_node(out, child);
    fo4::test::end_group(out, header);
    return;
  }
  const auto header = fo4::test::begin_record(out, node.signature, node.flags, node.form_id);
  for (const auto& sub : node.subrecords) fo4::test::write_subrecord(out, sub.signature, sub.data);
  fo4::test::end_record(out, header);
}

std::vector<std::byte> make_plugin(const std::vector<std::string>& masters,
                                   const std::vector<Node>& groups) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  for (const auto& master : masters) {
    fo4::test::write_subrecord(out, "MAST", zstring(master));
    fo4::test::ByteWriter data;
    data.u32(0);
    data.u32(0);
    fo4::test::write_subrecord(out, "DATA", data.bytes);
  }
  fo4::test::end_record(out, tes4);
  for (const auto& group : groups) write_node(out, group);
  return out.bytes;
}

std::vector<std::byte> make_base() {
  const auto cell_802 = rec("CELL", 0x802, {{"DATA", {std::byte{0x02}, std::byte{0}}}, {"XCLC", bytes_of({1, 2, 0})}});
  const auto cell_803 = rec("CELL", 0x803, {{"DATA", {std::byte{0x02}, std::byte{0}}}, {"XCLC", bytes_of({2, 2, 0})}});
  const auto refs = grp(0x802, 6,
                        {grp(0x802, 8,
                             {rec("REFR", 0x815,
                                  {{"NAME", bytes_of({0x820})},
                                   {"DATA", floats({4190.0f, 8290.0f, 0.0f, 0.0f, 0.0f, 0.0f})}},
                                  0x400)}),
                         grp(0x802, 9,
                             {rec("REFR", 0x814,
                                  {{"NAME", bytes_of({0x820})},
                                   {"DATA", floats({4196.0f, 8292.0f, 0.0f, 0.0f, 0.0f, 0.0f})}},
                                  0x400),
                              rec("REFR", 0x811,
                                  {{"EDID", zstring("Crate")},
                                   {"NAME", bytes_of({0x820})},
                                   {"DATA", floats({4196.0f, 8292.0f, 0.0f, 0.0f, 0.0f, 0.0f})},
                                   {"XTEL", xtel_bytes()},
                                   {"XPRM", xprm_bytes()}}),
                              rec("REFR", 0x812,
                                  {{"NAME", bytes_of({0x820})},
                                   {"DATA", floats({4200.0f, 8300.0f, 0.0f, 0.0f, 0.0f, 0.0f})}}),
                              rec("REFR", 0x813,
                                  {{"NAME", bytes_of({0x820})},
                                   {"DATA", floats({4300.0f, 8300.0f, 0.0f, 0.0f, 0.0f, 0.0f})}})})});
  const auto interior_804 = rec("CELL", 0x804, {{"DATA", {std::byte{0x01}, std::byte{0}}}, {"XCLL", lighting_bytes(72)}});
  const auto interior_805 = rec("CELL", 0x805, {{"DATA", {std::byte{0x01}, std::byte{0}}}, {"XCLL", lighting_bytes(40)}});
  return make_plugin(
      {},
      {top("WRLD",
           {rec("WRLD", 0x800, {{"EDID", zstring("TestWorld")}}),
            grp(0x800, 1, {rec("CELL", 0x801, {{"DATA", {std::byte{0x02}, std::byte{0}}}}), grp(0, 4, {grp(0, 5, {cell_802, refs, cell_803})})})}),
       top("CELL", {interior_804, interior_805})});
}

std::vector<std::byte> make_patch() {
  const auto cell_802 = rec("CELL", 0x802, {{"DATA", {std::byte{0x02}, std::byte{0}}}, {"XCLC", bytes_of({1, 2, 0})}});
  return make_plugin(
      {"Base.esm"},
      {top("WRLD",
           {rec("WRLD", 0x800, {{"EDID", zstring("TestWorld")}}),
            grp(0x800, 1,
                {grp(0, 4,
                     {grp(0, 5,
                          {cell_802,
                           grp(0x802, 6,
                               {grp(0x802, 9,
                                    {rec("REFR", 0x812,
                                         {{"NAME", bytes_of({0x820})},
                                          {"DATA", floats({99.0f, 8300.0f, 0.0f, 0.0f, 0.0f, 0.0f})}}),
                                     rec("REFR", 0x813, {}, 0x20)})})})})})})});
}

const fo4::Plugin& plugin_named(const fo4::env::Environment& env, std::string_view name) {
  return env.plugin(*env.find_plugin(name));
}

fo4::Plugin empty_edit() {
  const auto bytes = make_plugin({"Base.esm"}, {});
  return fo4::read_plugin(bytes, "Edit.esp");
}

bool has_subrecord(const fo4::Record& record, std::string_view signature) {
  const auto wanted = fo4::Signature::from_string(signature);
  return std::any_of(record.subrecords.begin(), record.subrecords.end(),
                     [&](const fo4::Subrecord& sub) { return sub.signature == wanted; });
}

const fo4::Subrecord* subrecord_of(const fo4::Record& record, std::string_view signature) {
  const auto wanted = fo4::Signature::from_string(signature);
  const auto found = std::find_if(record.subrecords.begin(), record.subrecords.end(),
                                  [&](const fo4::Subrecord& sub) { return sub.signature == wanted; });
  return found == record.subrecords.end() ? nullptr : &*found;
}

bool has_reference(const fo4::Plugin& plugin, const fo4::FormKey& cell, const fo4::FormKey& reference) {
  for (const auto& view : fo4::forms::placed_references_for_cell(plugin, cell)) {
    if (view.record->form_key == reference) return true;
  }
  return false;
}

std::optional<std::int32_t> bucket_type_of(const fo4::Group& group, const fo4::FormKey& key) {
  for (const auto& child : group.children) {
    if (const auto* record = std::get_if<fo4::Record>(&child); record != nullptr && record->form_key == key) {
      return group.header.group_type;
    }
    if (const auto* nested = std::get_if<fo4::Group>(&child)) {
      if (const auto type = bucket_type_of(*nested, key)) return type;
    }
  }
  return std::nullopt;
}

std::optional<std::int32_t> bucket_type_of(const fo4::Plugin& plugin, const fo4::FormKey& key) {
  for (const auto& group : plugin.groups) {
    if (const auto type = bucket_type_of(group, key)) return type;
  }
  return std::nullopt;
}

std::vector<std::byte> make_targets() {
  return make_plugin({}, {top("WATR", {rec("WATR", 0x900)}), top("IMGS", {rec("IMGS", 0x901)}),
                          top("ECZN", {rec("ECZN", 0x902)}), top("LCTN", {rec("LCTN", 0x903)}),
                          top("LGTM", {rec("LGTM", 0x904)}), top("REGN", {rec("REGN", 0x905), rec("REGN", 0x907)}),
                          top("FACT", {rec("FACT", 0x906)})});
}

fo4::Plugin props_patch() {
  return fo4::read_plugin(make_plugin({"Base.esm", "Targets.esm"}, {}), "Props.esp");
}

fo4::env::Environment load_plugins(fo4::test::TempDataDir& dir, const std::vector<std::string>& names) {
  std::vector<std::filesystem::path> paths;
  for (const auto& name : names) paths.push_back(dir.path() / name);
  return fo4::env::Environment::load(paths);
}

fo4::env::Environment base_and_targets(fo4::test::TempDataDir& dir) {
  dir.write("Targets.esm", make_targets(), 1);
  return load_plugins(dir, {"Base.esm", "Targets.esm"});
}

fo4::env::Environment reread_patched(fo4::test::TempDataDir& dir, const fo4::Plugin& patch) {
  dir.write("Readback.esp", fo4::write_plugin(patch), 2);
  return load_plugins(dir, {"Base.esm", "Targets.esm", "Readback.esp"});
}

std::vector<std::string> signatures_of(const fo4::Record& record) {
  std::vector<std::string> signatures;
  for (const auto& sub : record.subrecords) signatures.push_back(sub.signature.to_string());
  return signatures;
}

class CellEditTest : public ::testing::Test {
protected:
  void SetUp() override {
    dir_.write("Base.esm", make_base(), 0);
    dir_.write("Patch.esp", make_patch(), 1);
    paths_ = {dir_.path() / "Base.esm", dir_.path() / "Patch.esp"};
  }

  fo4::test::TempDataDir dir_;
  std::vector<std::filesystem::path> paths_;
};

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

TEST_F(CellEditTest, EditCellLightingReadsBackThroughDecodeCell) {
  const auto env = base_and_targets(dir_);
  const auto cell = fo4::FormKey::parse("000802:Base.esm");
  auto patch = props_patch();

  fo4::forms::CellLightingEdit lighting;
  lighting.ambient = std::array<std::uint8_t, 3>{10, 20, 30};
  lighting.directional = std::array<std::uint8_t, 3>{40, 50, 60};
  lighting.fog_near_color = std::array<std::uint8_t, 3>{70, 80, 90};
  lighting.fog_near = 1.5f;
  lighting.fog_far = 2500.0f;
  lighting.directional_rotation_xy = 45;
  lighting.directional_rotation_z = -90;
  lighting.directional_fade = 0.25f;
  lighting.fog_clip_distance = 3000.0f;
  lighting.fog_power = 2.0f;
  fo4::forms::CellEdit edit;
  edit.lighting = lighting;

  const auto result = fo4::forms::edit_cell(patch, env, cell, edit);
  ASSERT_EQ(result.status, fo4::forms::CellEditStatus::edited);
  EXPECT_EQ(result.copied_from, "Base.esm");

  const auto reread = reread_patched(dir_, patch);
  const auto* ref = reread.winner(cell);
  ASSERT_NE(ref, nullptr);
  const auto data = fo4::env::decode_cell(reread, *ref);
  ASSERT_TRUE(data.lighting.has_value());
  const auto& light = *data.lighting;
  EXPECT_EQ(light.ambient.red, 10);
  EXPECT_EQ(light.ambient.blue, 30);
  EXPECT_EQ(light.directional.green, 50);
  EXPECT_EQ(light.fog_near_color.blue, 90);
  EXPECT_FLOAT_EQ(light.fog_near, 1.5f);
  EXPECT_FLOAT_EQ(light.fog_far, 2500.0f);
  EXPECT_EQ(light.directional_rotation_xy, 45);
  EXPECT_EQ(light.directional_rotation_z, -90);
  EXPECT_FLOAT_EQ(light.directional_fade, 0.25f);
  EXPECT_FLOAT_EQ(light.fog_clip_distance, 3000.0f);
  EXPECT_FLOAT_EQ(light.fog_power, 2.0f);
}

TEST_F(CellEditTest, EditWaterAndWaterHeightReadsBackThroughResolveWater) {
  const auto env = base_and_targets(dir_);
  const auto cell = fo4::FormKey::parse("000802:Base.esm");
  const auto water_key = fo4::FormKey::parse("000900:Targets.esm");
  auto patch = props_patch();

  fo4::forms::CellEdit edit;
  edit.water = std::optional<fo4::FormKey>{water_key};
  edit.water_height = 12.0f;
  ASSERT_EQ(fo4::forms::edit_cell(patch, env, cell, edit).status, fo4::forms::CellEditStatus::edited);

  const auto reread = reread_patched(dir_, patch);
  const auto* ref = reread.winner(cell);
  ASSERT_NE(ref, nullptr);
  const auto water = fo4::env::resolve_water(reread, *ref);
  EXPECT_EQ(water.water, water_key);
  EXPECT_FALSE(water.uses_default_water);
  ASSERT_TRUE(water.height.has_value());
  EXPECT_FLOAT_EQ(*water.height, 12.0f);
}

TEST_F(CellEditTest, EditLinksRegionsAndOwnerReadBackThroughDecodeCell) {
  const auto env = base_and_targets(dir_);
  const auto cell = fo4::FormKey::parse("000802:Base.esm");
  const auto location = fo4::FormKey::parse("000903:Targets.esm");
  const auto image_space = fo4::FormKey::parse("000901:Targets.esm");
  const auto encounter_zone = fo4::FormKey::parse("000902:Targets.esm");
  const auto region = fo4::FormKey::parse("000905:Targets.esm");
  const auto second_region = fo4::FormKey::parse("000907:Targets.esm");
  const auto owner = fo4::FormKey::parse("000906:Targets.esm");
  auto patch = props_patch();

  fo4::forms::CellEdit edit;
  edit.location = std::optional<fo4::FormKey>{location};
  edit.image_space = std::optional<fo4::FormKey>{image_space};
  edit.encounter_zone = std::optional<fo4::FormKey>{encounter_zone};
  edit.regions = std::vector<fo4::FormKey>{region, second_region};
  edit.owner = std::optional<fo4::FormKey>{owner};
  edit.owner_rank = 3;
  ASSERT_EQ(fo4::forms::edit_cell(patch, env, cell, edit).status, fo4::forms::CellEditStatus::edited);

  const auto reread = reread_patched(dir_, patch);
  const auto* ref = reread.winner(cell);
  ASSERT_NE(ref, nullptr);
  const auto data = fo4::env::decode_cell(reread, *ref);
  EXPECT_EQ(data.location, location);
  EXPECT_EQ(data.image_space, image_space);
  EXPECT_EQ(data.encounter_zone, encounter_zone);
  ASSERT_EQ(data.regions.size(), 2u);
  EXPECT_EQ(data.regions[0], region);
  EXPECT_EQ(data.regions[1], second_region);
  EXPECT_EQ(data.owner, owner);
  EXPECT_EQ(data.owner_rank, 3);
}

TEST_F(CellEditTest, EditCreatesMissingSubrecordsInSchemaOrder) {
  const auto env = base_and_targets(dir_);
  const auto cell = fo4::FormKey::parse("000802:Base.esm");
  auto patch = props_patch();

  fo4::forms::CellLightingEdit lighting;
  lighting.ambient = std::array<std::uint8_t, 3>{1, 2, 3};
  fo4::forms::CellEdit edit;
  edit.lighting = lighting;
  edit.lighting_template = std::optional<fo4::FormKey>{fo4::FormKey::parse("000904:Targets.esm")};
  edit.water_height = 4.0f;
  edit.location = std::optional<fo4::FormKey>{fo4::FormKey::parse("000903:Targets.esm")};
  ASSERT_EQ(fo4::forms::edit_cell(patch, env, cell, edit).status, fo4::forms::CellEditStatus::edited);

  const auto* record = fo4::find_record(patch, cell);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(signatures_of(*record), (std::vector<std::string>{"DATA", "XCLC", "XCLL", "LTMP", "XCLW", "XLCN"}));

  const auto reread = reread_patched(dir_, patch);
  const auto* ref = reread.winner(cell);
  ASSERT_NE(ref, nullptr);
  const auto decoded = fo4::env::decode_cell(reread, *ref).lighting;
  ASSERT_TRUE(decoded.has_value());
  EXPECT_FLOAT_EQ(decoded->directional_fade, 1.0f);
  EXPECT_FLOAT_EQ(decoded->fog_power, 1.0f);
}

TEST_F(CellEditTest, ClearingALinkRemovesItsSubrecord) {
  const auto env = base_and_targets(dir_);
  const auto cell = fo4::FormKey::parse("000802:Base.esm");
  auto patch = props_patch();

  fo4::forms::CellEdit set;
  set.image_space = std::optional<fo4::FormKey>{fo4::FormKey::parse("000901:Targets.esm")};
  set.owner = std::optional<fo4::FormKey>{fo4::FormKey::parse("000906:Targets.esm")};
  set.owner_rank = 3;
  set.regions = std::vector<fo4::FormKey>{fo4::FormKey::parse("000905:Targets.esm")};
  ASSERT_EQ(fo4::forms::edit_cell(patch, env, cell, set).status, fo4::forms::CellEditStatus::edited);

  fo4::forms::CellEdit clear;
  clear.image_space = std::optional<fo4::FormKey>{};
  clear.owner = std::optional<fo4::FormKey>{};
  clear.regions = std::vector<fo4::FormKey>{};
  ASSERT_EQ(fo4::forms::edit_cell(patch, env, cell, clear).status, fo4::forms::CellEditStatus::edited);

  const auto* record = fo4::find_record(patch, cell);
  ASSERT_NE(record, nullptr);
  EXPECT_FALSE(has_subrecord(*record, "XCIM"));
  EXPECT_FALSE(has_subrecord(*record, "XOWN"));
  EXPECT_FALSE(has_subrecord(*record, "XRNK"));
  EXPECT_FALSE(has_subrecord(*record, "XCLR"));
  const auto reread = reread_patched(dir_, patch);
  const auto* ref = reread.winner(cell);
  ASSERT_NE(ref, nullptr);
  EXPECT_FALSE(fo4::env::decode_cell(reread, *ref).image_space.has_value());
}

TEST_F(CellEditTest, EditOfMasterCellKeepsItsChildrenAndSetsFlags) {
  const auto env = base_and_targets(dir_);
  const auto cell = fo4::FormKey::parse("000802:Base.esm");
  const auto children_before = env.cell_children(cell).size();
  EXPECT_GT(children_before, 0u);
  auto patch = props_patch();

  fo4::forms::CellEdit edit;
  edit.flags = std::uint16_t{0x0082};
  ASSERT_EQ(fo4::forms::edit_cell(patch, env, cell, edit).status, fo4::forms::CellEditStatus::edited);

  const auto reread = reread_patched(dir_, patch);
  EXPECT_EQ(reread.cell_children(cell).size(), children_before);
  const auto* ref = reread.winner(cell);
  ASSERT_NE(ref, nullptr);
  EXPECT_EQ(fo4::env::decode_cell(reread, *ref).flags, std::optional<std::uint16_t>{0x0082});
}

TEST_F(CellEditTest, RefusedCellEditLeavesPatchUnchanged) {
  const auto env = base_and_targets(dir_);
  const auto cell = fo4::FormKey::parse("000802:Base.esm");
  auto patch = props_patch();
  const auto before = fo4::write_plugin(patch);

  fo4::forms::CellEdit wrong_type;
  wrong_type.water = std::optional<fo4::FormKey>{fo4::FormKey::parse("000903:Targets.esm")};
  EXPECT_EQ(fo4::forms::edit_cell(patch, env, cell, wrong_type).status,
            fo4::forms::CellEditStatus::target_type_not_allowed);

  fo4::forms::CellEdit interior;
  interior.flags = std::uint16_t{0x0003};
  EXPECT_EQ(fo4::forms::edit_cell(patch, env, cell, interior).status,
            fo4::forms::CellEditStatus::interior_flag_locked);

  fo4::forms::CellEdit not_finite;
  not_finite.water_height = std::numeric_limits<float>::quiet_NaN();
  EXPECT_EQ(fo4::forms::edit_cell(patch, env, cell, not_finite).status,
            fo4::forms::CellEditStatus::invalid_value);

  fo4::forms::CellEdit missing;
  missing.flags = std::uint16_t{0x0002};
  EXPECT_EQ(fo4::forms::edit_cell(patch, env, fo4::FormKey::parse("000999:Base.esm"), missing).status,
            fo4::forms::CellEditStatus::cell_missing);
  EXPECT_TRUE(fo4::write_plugin(patch) == before);
}

TEST_F(CellEditTest, CellPropertyDescriptorsCoverEveryEditableMember) {
  const std::vector<std::string> members = {"flags", "lighting", "lighting_template", "water_height", "water", "image_space",
                                            "encounter_zone", "location", "acoustic_space", "music_type", "sky_weather_region",
                                            "god_rays", "lock_list", "owner", "owner_rank", "regions"};
  const auto normalized = [](std::string text) {
    std::erase(text, '_');
    for (auto& ch : text) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return text;
  };
  const auto descriptors = fo4::forms::cell_property_descriptors();
  for (const auto& member : members) {
    const bool found = std::any_of(descriptors.begin(), descriptors.end(), [&](const auto& descriptor) {
      return normalized(descriptor.name).find(normalized(member)) != std::string::npos;
    });
    EXPECT_TRUE(found) << member;
  }
  for (const auto& descriptor : descriptors) {
    if (descriptor.kind == fo4::forms::CellPropertyKind::link) {
      EXPECT_FALSE(descriptor.targets.empty());
    }
  }
}

TEST_F(CellEditTest, InteriorCellEditKeepsLightingTailAndWritesFlags) {
  const auto env = base_and_targets(dir_);
  const auto cell = fo4::FormKey::parse("000804:Base.esm");
  auto patch = props_patch();
  fo4::forms::CellLightingEdit lighting;
  lighting.ambient = std::array<std::uint8_t, 3>{9, 8, 7};
  fo4::forms::CellEdit edit;
  edit.lighting = lighting;
  edit.flags = std::uint16_t{0x0003};
  ASSERT_EQ(fo4::forms::edit_cell(patch, env, cell, edit).status, fo4::forms::CellEditStatus::edited);
  const auto* record = fo4::find_record(patch, cell);
  ASSERT_NE(record, nullptr);
  const auto* xcll = subrecord_of(*record, "XCLL");
  const auto* data = subrecord_of(*record, "DATA");
  ASSERT_TRUE(xcll != nullptr && data != nullptr);
  ASSERT_EQ(xcll->data.size(), 72u);
  EXPECT_EQ(xcll->data[0], std::byte{9});
  EXPECT_TRUE(std::all_of(xcll->data.begin() + 40, xcll->data.end(),
                          [](std::byte value) { return value == std::byte{0xAB}; }));
  EXPECT_EQ(data->data, (std::vector<std::byte>{std::byte{0x03}, std::byte{0}}));
}

TEST_F(CellEditTest, ShortLightingSubrecordIsRefusedAndPatchUnchanged) {
  const auto env = base_and_targets(dir_);
  auto patch = props_patch();
  const auto before = fo4::write_plugin(patch);
  fo4::forms::CellLightingEdit lighting;
  lighting.ambient = std::array<std::uint8_t, 3>{1, 2, 3};
  fo4::forms::CellEdit edit;
  edit.lighting = lighting;
  EXPECT_EQ(fo4::forms::edit_cell(patch, env, fo4::FormKey::parse("000805:Base.esm"), edit).status,
            fo4::forms::CellEditStatus::malformed);
  EXPECT_TRUE(fo4::write_plugin(patch) == before);
}

TEST_F(CellEditTest, UnlistedOwnerAndPartialApplyLeavePatchUnchanged) {
  const auto env = base_and_targets(dir_);
  const auto cell = fo4::FormKey::parse("000802:Base.esm");
  auto patch = props_patch();
  const auto before = fo4::write_plugin(patch);
  fo4::forms::CellEdit edit;
  edit.flags = std::uint16_t{0x0082};
  edit.water_height = 3.0f;
  edit.location = std::optional<fo4::FormKey>{fo4::FormKey::parse("000903:Unrelated.esm")};
  EXPECT_EQ(fo4::forms::edit_cell(patch, env, cell, edit).status, fo4::forms::CellEditStatus::target_not_master);
  EXPECT_TRUE(fo4::write_plugin(patch) == before);
  edit.location = std::optional<fo4::FormKey>{fo4::FormKey::parse("000905:Targets.esm")};
  EXPECT_EQ(fo4::forms::edit_cell(patch, env, cell, edit).status, fo4::forms::CellEditStatus::target_type_not_allowed);
  EXPECT_TRUE(fo4::write_plugin(patch) == before);
}
