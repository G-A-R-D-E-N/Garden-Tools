#include "fo4/environment/cell_view.hpp"
#include "fo4/forms/cell.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"
#include "temp_data_dir.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace {

void write_tes4(fo4::test::ByteWriter& out, std::uint32_t flags, bool with_master) {
  const auto tes4 = fo4::test::begin_record(out, "TES4", flags, 0, 131);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  if (with_master) {
    fo4::test::write_subrecord(out, "MAST", std::string_view{"Base.esm\0", 9});
    fo4::test::write_subrecord(out, "DATA", std::string_view{"\0\0\0\0\0\0\0\0", 8});
  }
  fo4::test::end_record(out, tes4);
}

std::vector<std::byte> worldspace_plugin(std::uint32_t tes4_flags, bool with_master) {
  fo4::test::ByteWriter out;
  write_tes4(out, tes4_flags, with_master);

  const auto wrld_group = fo4::test::begin_group(out, "WRLD", 0);
  const auto wrld = fo4::test::begin_record(out, "WRLD", 0, 0x00000800, 131);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"TestWorld\0", 10});
  fo4::test::end_record(out, wrld);
  fo4::test::end_group(out, wrld_group);
  return out.bytes;
}

std::vector<std::byte> one_byte_data_interior_plugin() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto cell_group = fo4::test::begin_group(out, "CELL", 0);
  const auto block = fo4::test::begin_group(out, std::string_view{"\0\0\0\0", 4}, 2);
  const auto sub_block = fo4::test::begin_group(out, std::string_view{"\0\0\0\0", 4}, 3);
  const auto cell = fo4::test::begin_record(out, "CELL", 0, 0x900, 131);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"OneByteCell\0", 12});
  fo4::test::write_subrecord(out, "DATA", std::string_view{"\x01", 1});
  fo4::test::end_record(out, cell);
  fo4::test::end_group(out, sub_block);
  fo4::test::end_group(out, block);
  fo4::test::end_group(out, cell_group);
  return out.bytes;
}

struct RefSpec {
  std::string_view signature;
  std::uint32_t form_id;
  std::uint32_t base;
  float x;
  std::uint32_t flags;
};

struct CellSpec {
  std::uint32_t form_id;
  std::vector<RefSpec> refs;
};

constexpr std::int32_t k_cell_children = static_cast<std::int32_t>(fo4::GroupType::cell_children);
constexpr std::int32_t k_cell_temporary =
    static_cast<std::int32_t>(fo4::GroupType::cell_temporary_children);

void write_zstring(fo4::test::ByteWriter& out, std::string_view signature, std::string text) {
  text.push_back('\0');
  fo4::test::write_subrecord(out, signature, std::string_view{text});
}

void write_model_record(fo4::test::ByteWriter& out,
                        std::string_view type,
                        std::uint32_t form_id,
                        std::string_view model_signature,
                        const std::string& model) {
  const auto record = fo4::test::begin_record(out, type, 0, form_id);
  if (!model.empty()) write_zstring(out, model_signature, model);
  fo4::test::end_record(out, record);
}

void write_model_base(fo4::test::ByteWriter& out,
                      std::string_view type,
                      std::uint32_t form_id,
                      std::string_view model_signature,
                      const std::string& model) {
  const auto top = fo4::test::begin_group(out, type, 0);
  write_model_record(out, type, form_id, model_signature, model);
  fo4::test::end_group(out, top);
}

std::size_t begin_children(fo4::test::ByteWriter& out, std::int32_t type, std::uint32_t owner) {
  const auto group = fo4::test::begin_group(out, "CELL", type);
  out.patch_u32(group + 8, owner);
  return group;
}

void write_reference(fo4::test::ByteWriter& out, const RefSpec& ref) {
  const auto record = fo4::test::begin_record(out, ref.signature, ref.flags, ref.form_id);
  if (ref.base != 0) {
    fo4::test::ByteWriter name;
    name.u32(ref.base);
    fo4::test::write_subrecord(out, "NAME", name.bytes);
  }
  fo4::test::ByteWriter transform;
  transform.f32(ref.x);
  for (int index = 1; index < 6; ++index) transform.f32(0.0f);
  fo4::test::write_subrecord(out, "DATA", transform.bytes);
  fo4::test::end_record(out, record);
}

void write_cells(fo4::test::ByteWriter& out, const std::vector<CellSpec>& cells) {
  const auto top = fo4::test::begin_group(out, "CELL", 0);
  const auto block = fo4::test::begin_group(out, std::string_view{"\0\0\0\0", 4}, 2);
  const auto sub_block = fo4::test::begin_group(out, std::string_view{"\0\0\0\0", 4}, 3);
  for (const auto& cell : cells) {
    const auto record = fo4::test::begin_record(out, "CELL", 0, cell.form_id);
    fo4::test::write_subrecord(out, "DATA", std::string_view{"\x01\0", 2});
    fo4::test::end_record(out, record);
    const auto children = begin_children(out, k_cell_children, cell.form_id);
    const auto temporary = begin_children(out, k_cell_temporary, cell.form_id);
    for (const auto& ref : cell.refs) write_reference(out, ref);
    fo4::test::end_group(out, temporary);
    fo4::test::end_group(out, children);
  }
  fo4::test::end_group(out, sub_block);
  fo4::test::end_group(out, block);
  fo4::test::end_group(out, top);
}

void write_scol_part(fo4::test::ByteWriter& out, std::uint32_t static_id, std::size_t placements) {
  fo4::test::ByteWriter onam;
  onam.u32(static_id);
  fo4::test::write_subrecord(out, "ONAM", onam.bytes);
  if (placements == 0) return;
  fo4::test::ByteWriter data;
  for (std::size_t index = 0; index < placements; ++index) {
    for (int field = 0; field < 7; ++field) data.f32(static_cast<float>(index));
  }
  fo4::test::write_subrecord(out, "DATA", data.bytes);
}

void write_texture_set(fo4::test::ByteWriter& out,
                       std::uint32_t form_id,
                       const std::array<std::int16_t, 6>& bounds,
                       const std::string& diffuse,
                       float min_width,
                       float min_height) {
  const auto record = fo4::test::begin_record(out, "TXST", 0, form_id);
  fo4::test::ByteWriter obnd;
  for (const auto value : bounds) obnd.u16(static_cast<std::uint16_t>(value));
  fo4::test::write_subrecord(out, "OBND", obnd.bytes);
  if (!diffuse.empty()) write_zstring(out, "TX00", diffuse);
  fo4::test::ByteWriter dodt;
  dodt.f32(min_width);
  dodt.f32(min_width);
  dodt.f32(min_height);
  dodt.f32(min_height);
  for (int index = 0; index < 20; ++index) dodt.u8(0);
  fo4::test::write_subrecord(out, "DODT", dodt.bytes);
  fo4::test::end_record(out, record);
}

struct LoadedCells {
  fo4::env::Environment env;
  fo4::FormKey world;
  fo4::FormKey negative;
  fo4::FormKey low;
  fo4::FormKey high;
  fo4::FormKey vault;
};

LoadedCells load_cells() {
  fo4::test::TempDataDir dir;

  auto base = fo4::read_plugin(worldspace_plugin(0, false), "Base.esm");
  const auto world = fo4::FormKey::parse("000800:Base.esm");
  const auto negative = fo4::forms::create_exterior_cell(
      base,
      fo4::forms::ExteriorCellCreation{
          .worldspace = world,
          .x = -1,
          .y = -1,
          .editor_id = std::string_view{"NegativeCell"},
      });
  const auto low = fo4::forms::create_exterior_cell(
      base,
      fo4::forms::ExteriorCellCreation{
          .worldspace = world,
          .x = 1,
          .y = 2,
          .editor_id = std::string_view{"LowCell"},
      });
  const auto vault = fo4::forms::create_interior_cell(base, "VaultCell");
  dir.write("Base.esm", fo4::write_plugin(base), 0);

  auto patch = fo4::read_plugin(worldspace_plugin(0x80, true), "Patch.esp");
  const auto high = fo4::forms::create_exterior_cell(
      patch,
      fo4::forms::ExteriorCellCreation{
          .worldspace = world,
          .x = 1,
          .y = 2,
          .editor_id = std::string_view{"HighCell"},
      });
  dir.write("Patch.esp", fo4::write_plugin(patch), 1);

  const std::vector<std::filesystem::path> paths{dir.path() / "Base.esm", dir.path() / "Patch.esp"};
  return {fo4::env::Environment::load(paths), world, negative, low, high, vault};
}

}  // namespace

TEST(CellView, ResolvesFormKeyAndEditorIdCaseInsensitively) {
  const auto cells = load_cells();

  EXPECT_EQ(fo4::env::resolve_cell_id(cells.env, cells.vault.to_string()), cells.vault);
  EXPECT_EQ(fo4::env::resolve_cell_id(cells.env, "vaultcell"), cells.vault);
  EXPECT_EQ(fo4::env::resolve_cell_id(cells.env, "VAULTCELL"), cells.vault);
  EXPECT_EQ(fo4::env::resolve_cell_id(cells.env, cells.high.to_string()), cells.high);
  EXPECT_EQ(fo4::env::resolve_worldspace_id(cells.env, "testworld"), cells.world);
  EXPECT_EQ(fo4::env::resolve_worldspace_id(cells.env, cells.world.to_string()), cells.world);
}

TEST(CellView, MalformedFormKeyReturnsNullopt) {
  const auto cells = load_cells();

  EXPECT_FALSE(fo4::env::resolve_cell_id(cells.env, "").has_value());
  EXPECT_FALSE(fo4::env::resolve_cell_id(cells.env, "000902:").has_value());
  EXPECT_FALSE(fo4::env::resolve_cell_id(cells.env, ":Base.esm").has_value());
  EXPECT_FALSE(fo4::env::resolve_cell_id(cells.env, "ZZZZZZ:Base.esm").has_value());
  EXPECT_FALSE(fo4::env::resolve_cell_id(cells.env, "000902:Missing.esm").has_value());
  EXPECT_FALSE(fo4::env::resolve_cell_id(cells.env, "0FFFFF:Base.esm").has_value());
  EXPECT_FALSE(fo4::env::resolve_worldspace_id(cells.env, "000800:").has_value());
}

TEST(CellView, WrongSignatureIsNotACell) {
  const auto cells = load_cells();

  EXPECT_FALSE(fo4::env::resolve_cell_id(cells.env, "TestWorld").has_value());
  EXPECT_FALSE(fo4::env::resolve_cell_id(cells.env, cells.world.to_string()).has_value());
  EXPECT_FALSE(fo4::env::resolve_worldspace_id(cells.env, "VaultCell").has_value());
  EXPECT_FALSE(fo4::env::resolve_worldspace_id(cells.env, cells.vault.to_string()).has_value());
  EXPECT_FALSE(fo4::env::describe_cell(cells.env, cells.world).has_value());
}

TEST(CellView, FindsExteriorCellAtNegativeGrid) {
  const auto cells = load_cells();

  EXPECT_EQ(fo4::env::find_cell_at_grid(cells.env, cells.world, -1, -1), cells.negative);
  EXPECT_FALSE(fo4::env::find_cell_at_grid(cells.env, cells.world, -2, -2).has_value());
  EXPECT_FALSE(fo4::env::find_cell_at_grid(cells.env, cells.world, 111, 0).has_value());
  EXPECT_FALSE(fo4::env::find_cell_at_grid(cells.env, cells.world, -111, 0).has_value());
}

TEST(CellView, HighestPluginWinsAtGrid) {
  const auto cells = load_cells();

  const auto* low = fo4::forms::find_exterior_cell(cells.env.plugin(0), cells.world, 1, 2);
  ASSERT_NE(low, nullptr);
  EXPECT_EQ(low->form_key, cells.low);
  EXPECT_EQ(fo4::env::find_cell_at_grid(cells.env, cells.world, 1, 2), cells.high);
}

TEST(CellView, SummaryReportsInteriorFlagGridAndWorldspace) {
  const auto cells = load_cells();

  const auto interior = fo4::env::describe_cell(cells.env, cells.vault);
  ASSERT_TRUE(interior.has_value());
  EXPECT_EQ(interior->key, cells.vault);
  EXPECT_EQ(interior->editor_id, "VaultCell");
  EXPECT_TRUE(interior->interior);
  EXPECT_FALSE(interior->grid.has_value());
  EXPECT_FALSE(interior->worldspace.has_value());
  EXPECT_EQ(interior->winning_plugin, 0u);
  EXPECT_FALSE(interior->localized);

  const auto exterior = fo4::env::describe_cell(cells.env, cells.negative);
  ASSERT_TRUE(exterior.has_value());
  EXPECT_FALSE(exterior->interior);
  ASSERT_TRUE(exterior->grid.has_value());
  EXPECT_EQ(exterior->grid->x, -1);
  EXPECT_EQ(exterior->grid->y, -1);
  EXPECT_EQ(exterior->worldspace, cells.world);
  EXPECT_FALSE(exterior->localized);

  const auto overridden = fo4::env::describe_cell(cells.env, cells.high);
  ASSERT_TRUE(overridden.has_value());
  EXPECT_EQ(overridden->editor_id, "HighCell");
  EXPECT_FALSE(overridden->interior);
  ASSERT_TRUE(overridden->grid.has_value());
  EXPECT_EQ(overridden->grid->x, 1);
  EXPECT_EQ(overridden->grid->y, 2);
  EXPECT_EQ(overridden->worldspace, cells.world);
  EXPECT_EQ(overridden->winning_plugin, 1u);
  EXPECT_TRUE(overridden->localized);
}

TEST(CellView, OneByteDataWithInteriorBitReadsAsInterior) {
  fo4::test::TempDataDir dir;
  dir.write("Interior.esm", one_byte_data_interior_plugin(), 0);
  const std::vector<std::filesystem::path> paths{dir.path() / "Interior.esm"};
  const auto env = fo4::env::Environment::load(paths);

  const auto key = fo4::env::resolve_cell_id(env, "OneByteCell");
  ASSERT_TRUE(key.has_value());
  const auto summary = fo4::env::describe_cell(env, *key);
  ASSERT_TRUE(summary.has_value());
  EXPECT_TRUE(summary->interior);
}

TEST(CellReferences, MergesAcrossPluginsLaterOverrideWins) {
  fo4::test::ByteWriter base;
  write_tes4(base, 0, false);
  write_model_base(base, "STAT", 0x810, "MODL", "Meshes\\Box.nif");
  write_cells(base, {{0x800, {{"REFR", 0x801, 0x810, 1.0f, 0}, {"REFR", 0x802, 0x810, 2.0f, 0}}}});

  fo4::test::ByteWriter patch;
  write_tes4(patch, 0, true);
  write_cells(patch, {{0x800, {{"REFR", 0x801, 0x810, 5.0f, 0}, {"REFR", 0x01000803, 0x810, 3.0f, 0}}}});

  fo4::test::TempDataDir dir;
  dir.write("Base.esm", base.bytes, 0);
  dir.write("Patch.esp", patch.bytes, 1);
  const std::vector<std::filesystem::path> paths{dir.path() / "Base.esm", dir.path() / "Patch.esp"};
  const auto env = fo4::env::Environment::load(paths);

  const auto listing = fo4::env::list_cell_references(env, fo4::FormKey::parse("000800:Base.esm"));
  ASSERT_EQ(listing.references.size(), 3u);
  EXPECT_EQ(listing.references[0].key, fo4::FormKey::parse("000801:Base.esm"));
  EXPECT_EQ(listing.references[0].transform.x, 5.0f);
  EXPECT_EQ(listing.references[0].winning_plugin, 1u);
  EXPECT_EQ(listing.references[1].key, fo4::FormKey::parse("000802:Base.esm"));
  EXPECT_EQ(listing.references[1].transform.x, 2.0f);
  EXPECT_EQ(listing.references[1].winning_plugin, 0u);
  EXPECT_EQ(listing.references[2].key, fo4::FormKey::parse("000803:Patch.esp"));
  EXPECT_EQ(listing.references[2].winning_plugin, 1u);
}

TEST(CellReferences, DeletedInLaterPluginRemovesReference) {
  fo4::test::ByteWriter base;
  write_tes4(base, 0, false);
  write_model_base(base, "STAT", 0x810, "MODL", "Meshes\\Box.nif");
  write_cells(base, {{0x800, {{"REFR", 0x801, 0x810, 1.0f, 0}, {"REFR", 0x802, 0x810, 2.0f, 0}}}});

  fo4::test::ByteWriter patch;
  write_tes4(patch, 0, true);
  write_cells(patch, {{0x800, {{"REFR", 0x801, 0, 0.0f, fo4::RecordFlags::deleted}}}});

  fo4::test::TempDataDir dir;
  dir.write("Base.esm", base.bytes, 0);
  dir.write("Patch.esp", patch.bytes, 1);
  const std::vector<std::filesystem::path> paths{dir.path() / "Base.esm", dir.path() / "Patch.esp"};
  const auto env = fo4::env::Environment::load(paths);

  const auto listing = fo4::env::list_cell_references(env, fo4::FormKey::parse("000800:Base.esm"));
  ASSERT_EQ(listing.references.size(), 1u);
  EXPECT_EQ(listing.references[0].key, fo4::FormKey::parse("000802:Base.esm"));
}

TEST(CellReferences, ReferenceMovedToAnotherCellLeavesTheOldCell) {
  fo4::test::ByteWriter base;
  write_tes4(base, 0, false);
  write_model_base(base, "STAT", 0x810, "MODL", "Meshes\\Box.nif");
  write_cells(base, {{0x800, {{"REFR", 0x801, 0x810, 1.0f, 0}}}, {0x802, {}}});

  fo4::test::ByteWriter patch;
  write_tes4(patch, 0, true);
  write_cells(patch, {{0x802, {{"REFR", 0x801, 0x810, 4.0f, 0}}}});

  fo4::test::TempDataDir dir;
  dir.write("Base.esm", base.bytes, 0);
  dir.write("Patch.esp", patch.bytes, 1);
  const std::vector<std::filesystem::path> paths{dir.path() / "Base.esm", dir.path() / "Patch.esp"};
  const auto env = fo4::env::Environment::load(paths);

  const auto old_cell = fo4::env::list_cell_references(env, fo4::FormKey::parse("000800:Base.esm"));
  EXPECT_TRUE(old_cell.references.empty());

  const auto new_cell = fo4::env::list_cell_references(env, fo4::FormKey::parse("000802:Base.esm"));
  ASSERT_EQ(new_cell.references.size(), 1u);
  EXPECT_EQ(new_cell.references[0].key, fo4::FormKey::parse("000801:Base.esm"));
  EXPECT_EQ(new_cell.references[0].transform.x, 4.0f);
  EXPECT_EQ(new_cell.references[0].winning_plugin, 1u);
}

TEST(CellReferences, OrderIsByFormKey) {
  fo4::test::ByteWriter base;
  write_tes4(base, 0, false);
  write_model_base(base, "STAT", 0x810, "MODL", "Meshes\\Box.nif");
  write_cells(base, {{0x800, {{"REFR", 0x805, 0x810, 5.0f, 0},
                              {"REFR", 0x803, 0x810, 3.0f, 0},
                              {"REFR", 0x801, 0x810, 1.0f, 0}}}});

  fo4::test::ByteWriter patch;
  write_tes4(patch, 0, true);
  write_cells(patch, {{0x800, {{"REFR", 0x01000802, 0x810, 2.0f, 0}}}});

  fo4::test::TempDataDir dir;
  dir.write("Base.esm", base.bytes, 0);
  dir.write("Patch.esp", patch.bytes, 1);
  const std::vector<std::filesystem::path> paths{dir.path() / "Base.esm", dir.path() / "Patch.esp"};
  const auto env = fo4::env::Environment::load(paths);

  const auto listing = fo4::env::list_cell_references(env, fo4::FormKey::parse("000800:Base.esm"));
  ASSERT_EQ(listing.references.size(), 4u);
  EXPECT_EQ(listing.references[0].key, fo4::FormKey::parse("000801:Base.esm"));
  EXPECT_EQ(listing.references[1].key, fo4::FormKey::parse("000803:Base.esm"));
  EXPECT_EQ(listing.references[2].key, fo4::FormKey::parse("000805:Base.esm"));
  EXPECT_EQ(listing.references[3].key, fo4::FormKey::parse("000802:Patch.esp"));
}

TEST(CellReferences, ModelPathIsTypeAware) {
  fo4::test::ByteWriter base;
  write_tes4(base, 0, false);
  write_model_base(base, "STAT", 0x810, "MODL", "Meshes\\StatFixture.nif");
  write_model_base(base, "MSTT", 0x811, "MODL", "Meshes\\MsttFixture.nif");
  write_model_base(base, "FURN", 0x812, "MODL", "Meshes\\FurnFixture.nif");
  write_model_base(base, "ACTI", 0x813, "MODL", "Meshes\\ActiFixture.nif");
  write_model_base(base, "CONT", 0x814, "MODL", "");
  write_model_base(base, "SCOL", 0x815, "MODL", "Meshes\\ScolFixture.nif");
  write_model_base(base, "ARMO", 0x816, "MOD2", "Armor\\ArmoFixture.nif");
  write_cells(base, {{0x800, {{"REFR", 0x820, 0x810, 0.0f, 0},
                              {"REFR", 0x821, 0x811, 0.0f, 0},
                              {"REFR", 0x822, 0x812, 0.0f, 0},
                              {"REFR", 0x823, 0x813, 0.0f, 0},
                              {"REFR", 0x824, 0x814, 0.0f, 0},
                              {"REFR", 0x825, 0x815, 0.0f, 0},
                              {"REFR", 0x826, 0x816, 0.0f, 0},
                              {"PHZD", 0x830, 0, 0.0f, 0}}}});

  fo4::test::TempDataDir dir;
  dir.write("Base.esm", base.bytes, 0);
  const std::vector<std::filesystem::path> paths{dir.path() / "Base.esm"};
  const auto env = fo4::env::Environment::load(paths);

  const auto listing = fo4::env::list_cell_references(env, fo4::FormKey::parse("000800:Base.esm"));
  const auto& refs = listing.references;
  ASSERT_EQ(refs.size(), 8u);

  struct Expected {
    std::string_view type_name;
    std::string_view model_path;
  };
  constexpr std::array<Expected, 7> expected{{
      {"Static", "Meshes\\StatFixture.nif"},
      {"MovableStatic", "Meshes\\MsttFixture.nif"},
      {"Furniture", "Meshes\\FurnFixture.nif"},
      {"Activator", "Meshes\\ActiFixture.nif"},
      {"Container", ""},
      {"StaticCollection", ""},
      {"ARMO", "Armor\\ArmoFixture.nif"},
  }};
  for (std::size_t index = 0; index < expected.size(); ++index) {
    ASSERT_TRUE(refs[index].base_info.has_value()) << index;
    EXPECT_EQ(refs[index].base_info->type_name, expected[index].type_name) << index;
    EXPECT_EQ(refs[index].base_info->model_path, expected[index].model_path) << index;
  }
  EXPECT_EQ(refs[7].record_type, "PlacedTrap");
  EXPECT_FALSE(refs[7].base.has_value());
  EXPECT_FALSE(refs[7].base_info.has_value());
  EXPECT_EQ(listing.with_model_count, 5u);
}

TEST(CellReferences, ScolPartsResolveThroughWinners) {
  fo4::test::ByteWriter base;
  write_tes4(base, 0, false);
  const auto statics = fo4::test::begin_group(base, "STAT", 0);
  write_model_record(base, "STAT", 0x820, "MODL", "Meshes\\PartA.nif");
  write_model_record(base, "STAT", 0x821, "MODL", "Meshes\\PartB.nif");
  write_model_record(base, "STAT", 0x822, "MODL", "Meshes\\PartC.nif");
  write_model_record(base, "STAT", 0x823, "MODL", "");
  fo4::test::end_group(base, statics);
  const auto scols = fo4::test::begin_group(base, "SCOL", 0);
  const auto scol = fo4::test::begin_record(base, "SCOL", 0, 0x830);
  write_scol_part(base, 0x820, 2);
  write_scol_part(base, 0x821, 0);
  write_scol_part(base, 0x822, 1);
  write_scol_part(base, 0x823, 1);
  fo4::test::end_record(base, scol);
  fo4::test::end_group(base, scols);

  fo4::test::ByteWriter patch;
  write_tes4(patch, 0, true);
  write_model_base(patch, "STAT", 0x820, "MODL", "Meshes\\PartAOverride.nif");

  fo4::test::TempDataDir dir;
  dir.write("Base.esm", base.bytes, 0);
  dir.write("Patch.esp", patch.bytes, 1);
  const std::vector<std::filesystem::path> paths{dir.path() / "Base.esm", dir.path() / "Patch.esp"};
  const auto env = fo4::env::Environment::load(paths);

  const auto info = fo4::env::describe_base_object(env, fo4::FormKey::parse("000830:Base.esm"));
  ASSERT_TRUE(info.has_value());
  ASSERT_EQ(info->scol_parts.size(), 2u);
  EXPECT_EQ(info->scol_parts[0].model_path, "Meshes\\PartAOverride.nif");
  EXPECT_EQ(info->scol_parts[0].placements.size(), 2u);
  EXPECT_EQ(info->scol_parts[1].model_path, "Meshes\\PartC.nif");
  EXPECT_EQ(info->scol_parts[1].placements.size(), 1u);
}

TEST(CellReferences, TextureSetBaseReportsDecalSizeFromObndThenDodt) {
  fo4::test::ByteWriter base;
  write_tes4(base, 0, false);
  const auto texture_sets = fo4::test::begin_group(base, "TXST", 0);
  write_texture_set(base, 0x840, {-50, -25, 0, 50, 25, 10}, "Textures\\DecalA.dds", 3.0f, 4.0f);
  write_texture_set(base, 0x841, {0, 0, 0, 50, 0, 0}, "Textures\\DecalB.dds", 7.0f, 9.0f);
  write_texture_set(base, 0x842, {0, 0, 0, 50, 50, 0}, "", 1.0f, 2.0f);
  fo4::test::end_group(base, texture_sets);

  fo4::test::TempDataDir dir;
  dir.write("Base.esm", base.bytes, 0);
  const std::vector<std::filesystem::path> paths{dir.path() / "Base.esm"};
  const auto env = fo4::env::Environment::load(paths);

  const auto obnd = fo4::env::describe_base_object(env, fo4::FormKey::parse("000840:Base.esm"));
  ASSERT_TRUE(obnd.has_value());
  ASSERT_TRUE(obnd->decal.has_value());
  EXPECT_EQ(obnd->decal->diffuse, "Textures\\DecalA.dds");
  EXPECT_EQ(obnd->decal->width, 100.0f);
  EXPECT_EQ(obnd->decal->height, 50.0f);

  const auto dodt = fo4::env::describe_base_object(env, fo4::FormKey::parse("000841:Base.esm"));
  ASSERT_TRUE(dodt.has_value());
  ASSERT_TRUE(dodt->decal.has_value());
  EXPECT_EQ(dodt->decal->diffuse, "Textures\\DecalB.dds");
  EXPECT_EQ(dodt->decal->width, 7.0f);
  EXPECT_EQ(dodt->decal->height, 9.0f);

  const auto no_diffuse = fo4::env::describe_base_object(env, fo4::FormKey::parse("000842:Base.esm"));
  ASSERT_TRUE(no_diffuse.has_value());
  EXPECT_FALSE(no_diffuse->decal.has_value());
}

TEST(CellReferences, MarkerModelsAreFlagged) {
  fo4::test::ByteWriter base;
  write_tes4(base, 0, false);
  write_model_base(base, "STAT", 0x810, "MODL", "Markers\\Arrow.nif");
  write_model_base(base, "MSTT", 0x811, "MODL", "Meshes\\Debug\\SomeMarker.nif");
  write_model_base(base, "FURN", 0x812, "MODL", "Meshes\\Editor\\editor_marker_01.nif");
  write_model_base(base, "ACTI", 0x813, "MODL", "Meshes\\Props\\Box.nif");
  write_model_base(base, "CONT", 0x814, "MODL", "Meshes\\Markers\\Crate.nif");
  write_model_base(base, "NOTE", 0x815, "MODL", "");

  fo4::test::TempDataDir dir;
  dir.write("Base.esm", base.bytes, 0);
  const std::vector<std::filesystem::path> paths{dir.path() / "Base.esm"};
  const auto env = fo4::env::Environment::load(paths);

  const auto is_marker = [&env](std::string_view key) {
    const auto info = fo4::env::describe_base_object(env, fo4::FormKey::parse(key));
    return info.has_value() && info->editor_marker;
  };
  EXPECT_TRUE(is_marker("000810:Base.esm"));
  EXPECT_TRUE(is_marker("000811:Base.esm"));
  EXPECT_TRUE(is_marker("000812:Base.esm"));
  EXPECT_FALSE(is_marker("000813:Base.esm"));
  EXPECT_FALSE(is_marker("000814:Base.esm"));
  EXPECT_FALSE(is_marker("000815:Base.esm"));
}

TEST(CellReferences, UnknownBaseKeepsEmptyInfo) {
  fo4::test::ByteWriter base;
  write_tes4(base, 0, false);
  write_model_base(base, "STAT", 0x810, "MODL", "Meshes\\Box.nif");
  write_cells(base, {{0x800, {{"REFR", 0x801, 0x8F0, 0.0f, 0}, {"PHZD", 0x802, 0, 0.0f, 0}}}});

  fo4::test::TempDataDir dir;
  dir.write("Base.esm", base.bytes, 0);
  const std::vector<std::filesystem::path> paths{dir.path() / "Base.esm"};
  const auto env = fo4::env::Environment::load(paths);

  EXPECT_FALSE(fo4::env::describe_base_object(env, fo4::FormKey::parse("0008F0:Base.esm")).has_value());

  const auto listing = fo4::env::list_cell_references(env, fo4::FormKey::parse("000800:Base.esm"));
  ASSERT_EQ(listing.references.size(), 2u);
  EXPECT_EQ(listing.references[0].base, fo4::FormKey::parse("0008F0:Base.esm"));
  EXPECT_FALSE(listing.references[0].base_info.has_value());
  EXPECT_FALSE(listing.references[1].base.has_value());
  EXPECT_FALSE(listing.references[1].base_info.has_value());
  EXPECT_EQ(listing.references[1].record_type, "PlacedTrap");
  EXPECT_EQ(listing.with_model_count, 0u);
}
