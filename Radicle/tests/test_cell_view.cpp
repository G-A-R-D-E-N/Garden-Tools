#include "fo4/environment/cell_view.hpp"
#include "fo4/forms/cell.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"
#include "temp_data_dir.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

namespace {

std::vector<std::byte> worldspace_plugin(std::uint32_t tes4_flags, bool with_master) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", tes4_flags, 0, 131);
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
