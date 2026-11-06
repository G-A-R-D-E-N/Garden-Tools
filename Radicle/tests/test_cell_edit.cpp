#include "fo4/environment/environment.hpp"
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
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
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
                             {rec("REFR", 0x814,
                                  {{"NAME", bytes_of({0x820})},
                                   {"DATA", floats({4196.0f, 8292.0f, 0.0f, 0.0f, 0.0f, 0.0f})}},
                                  0x400)}),
                         grp(0x802, 9,
                             {rec("REFR", 0x811,
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
  return make_plugin(
      {},
      {top("WRLD",
           {rec("WRLD", 0x800, {{"EDID", zstring("TestWorld")}}),
            grp(0x800, 1, {rec("CELL", 0x801, {{"DATA", {std::byte{0x02}, std::byte{0}}}}), grp(0, 4, {grp(0, 5, {cell_802, refs, cell_803})})})})});
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

bool has_reference(const fo4::Plugin& plugin, const fo4::FormKey& cell, const fo4::FormKey& reference) {
  for (const auto& view : fo4::forms::placed_references_for_cell(plugin, cell)) {
    if (view.record->form_key == reference) return true;
  }
  return false;
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
  edit.record_flags = 0x00000400u;
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
  EXPECT_TRUE(record->header.flags.is_persistent());
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
