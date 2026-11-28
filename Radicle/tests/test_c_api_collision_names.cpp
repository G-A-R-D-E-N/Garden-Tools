#include "fo4/c_api_collision_names.h"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::uint32_t k_wood_crate_crc = 341181474U;
constexpr std::uint32_t k_metal_crc = 0x064003D4U;

void layer_record(fo4::test::ByteWriter& out, std::uint32_t form_id, std::uint32_t index, std::string_view name) {
  const auto record = fo4::test::begin_record(out, "COLL", 0, form_id);
  fo4::test::ByteWriter bnam;
  bnam.u32(index);
  fo4::test::write_subrecord(out, "BNAM", bnam.bytes);
  const std::string text = std::string(name) + '\0';
  fo4::test::write_subrecord(out, "MNAM", std::string_view{text});
  fo4::test::end_record(out, record);
}

void listing_layer_record(fo4::test::ByteWriter& out, std::uint32_t form_id, std::uint32_t index, std::string_view name,
                          const std::vector<std::uint32_t>& collides_with) {
  const auto record = fo4::test::begin_record(out, "COLL", 0, form_id);
  fo4::test::ByteWriter bnam;
  bnam.u32(index);
  fo4::test::write_subrecord(out, "BNAM", bnam.bytes);
  const std::string text = std::string(name) + '\0';
  fo4::test::write_subrecord(out, "MNAM", std::string_view{text});
  if (!collides_with.empty()) {
    fo4::test::ByteWriter cnam;
    for (const auto id : collides_with) cnam.u32(id);
    fo4::test::write_subrecord(out, "CNAM", cnam.bytes);
  }
  fo4::test::end_record(out, record);
}

void material_record(fo4::test::ByteWriter& out, std::uint32_t form_id, std::string_view editor_id) {
  const auto record = fo4::test::begin_record(out, "MATT", 0, form_id);
  const std::string text = std::string(editor_id) + '\0';
  fo4::test::write_subrecord(out, "EDID", std::string_view{text});
  fo4::test::end_record(out, record);
}

std::vector<std::byte> base_plugin() {
  fo4::test::ByteWriter out;
  out.bytes = fo4::test::make_tes4_plugin(0, 1.0f, {});
  const auto layers = fo4::test::begin_group(out, "COLL", 0);
  layer_record(out, 0x800, 0, "L_UNIDENTIFIED");
  layer_record(out, 0x801, 1, "L_STATIC");
  layer_record(out, 0x802, 3, "L_CLUTTER");
  fo4::test::end_group(out, layers);
  const auto materials = fo4::test::begin_group(out, "MATT", 0);
  material_record(out, 0x803, "MaterialWoodCrate");
  material_record(out, 0x804, "Metal");
  fo4::test::end_group(out, materials);
  return out.bytes;
}

std::vector<std::byte> collides_plugin() {
  fo4::test::ByteWriter out;
  out.bytes = fo4::test::make_tes4_plugin(0, 1.0f, {});
  const auto layers = fo4::test::begin_group(out, "COLL", 0);
  listing_layer_record(out, 0x800, 0, "L_UNIDENTIFIED", {0x801});
  listing_layer_record(out, 0x801, 1, "L_STATIC", {0x800, 0x802});
  listing_layer_record(out, 0x802, 3, "L_CLUTTER", {});
  listing_layer_record(out, 0x805, 5, "L_NAVCUT", {});
  fo4::test::end_group(out, layers);
  return out.bytes;
}

std::vector<std::byte> collides_patch_plugin() {
  fo4::test::ByteWriter out;
  out.bytes = fo4::test::make_tes4_plugin(0, 1.0f, {"Base.esm"});
  const auto layers = fo4::test::begin_group(out, "COLL", 0);
  listing_layer_record(out, 0x00000805, 5, "L_NAVCUT", {0x00000801});
  fo4::test::end_group(out, layers);
  return out.bytes;
}

std::vector<std::byte> messy_plugin() {
  fo4::test::ByteWriter out;
  out.bytes = fo4::test::make_tes4_plugin(0, 1.0f, {});
  const auto layers = fo4::test::begin_group(out, "COLL", 0);
  listing_layer_record(out, 0x800, 0, "L_A", {0x801, 0x899, 0x800, 0x801});
  listing_layer_record(out, 0x801, 1, "L_B", {});
  fo4::test::end_group(out, layers);
  return out.bytes;
}

std::vector<std::byte> patch_plugin() {
  fo4::test::ByteWriter out;
  out.bytes = fo4::test::make_tes4_plugin(0, 1.0f, {"Base.esm"});
  const auto layers = fo4::test::begin_group(out, "COLL", 0);
  layer_record(out, 0x00000801, 1, "L_STATIC_PATCHED");
  fo4::test::end_group(out, layers);
  return out.bytes;
}

std::vector<std::byte> empty_plugin() {
  fo4::test::ByteWriter out;
  out.bytes = fo4::test::make_tes4_plugin(0, 1.0f, {});
  return out.bytes;
}

struct TempRoot {
  std::filesystem::path path;
  TempRoot() {
    path = std::filesystem::temp_directory_path() /
           ("radicle_c_collision_names_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(path);
  }
  ~TempRoot() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
  std::string write(const std::string& name, const std::vector<std::byte>& bytes) const {
    std::ofstream(path / name, std::ios::binary)
        .write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return (path / name).string();
  }
};

struct Env {
  radicle_env* value{nullptr};
  explicit Env(const std::vector<std::string>& files) {
    std::vector<const char*> paths;
    for (const auto& file : files) paths.push_back(file.c_str());
    EXPECT_EQ(radicle_env_load(paths.data(), paths.size(), &value), RADICLE_STATUS_OK) << radicle_last_error();
  }
  ~Env() { radicle_env_destroy(value); }
};

struct Names {
  radicle_collision_names* value{nullptr};
  explicit Names(const radicle_env* env) {
    EXPECT_EQ(radicle_collision_names_build(env, &value), RADICLE_STATUS_OK) << radicle_last_error();
  }
  ~Names() { radicle_collision_names_destroy(value); }
};

const char* layer(const Names& names, std::uint32_t id) {
  const char* name = nullptr;
  EXPECT_EQ(radicle_collision_names_layer(names.value, id, &name), RADICLE_STATUS_OK);
  return name;
}

bool collides_with_nothing(const Names& names, std::uint32_t id) {
  std::int32_t flag = -1;
  EXPECT_EQ(radicle_collision_names_layer_collides_with_nothing(names.value, id, &flag), RADICLE_STATUS_OK);
  return flag == 1;
}

std::vector<std::uint32_t> collides_with(const Names& names, std::uint32_t id) {
  const std::uint32_t* values = reinterpret_cast<const std::uint32_t*>(1);
  size_t count = 7;
  EXPECT_EQ(radicle_collision_names_layer_collides_with(names.value, id, &values, &count), RADICLE_STATUS_OK);
  if (count == 0) {
    EXPECT_EQ(values, nullptr);
    return {};
  }
  return {values, values + count};
}

const char* material(const Names& names, std::uint32_t id) {
  const char* name = nullptr;
  EXPECT_EQ(radicle_collision_names_material(names.value, id, &name), RADICLE_STATUS_OK);
  return name;
}

}  // namespace

TEST(CapiCollisionNames, NamesLayersByIndexAndLeavesGapsEmpty) {
  const TempRoot root;
  const Env env({root.write("Base.esm", base_plugin())});
  const Names names(env.value);
  ASSERT_NE(names.value, nullptr);
  EXPECT_STREQ(layer(names, 0), "L_UNIDENTIFIED");
  EXPECT_STREQ(layer(names, 1), "L_STATIC");
  EXPECT_EQ(layer(names, 2), nullptr);
  EXPECT_STREQ(layer(names, 3), "L_CLUTTER");
  EXPECT_EQ(layer(names, 4), nullptr);
  EXPECT_EQ(layer(names, 4000), nullptr);
  size_t layers = 0;
  size_t materials = 0;
  ASSERT_EQ(radicle_collision_names_counts(names.value, &layers, &materials), RADICLE_STATUS_OK);
  EXPECT_EQ(layers, 4u);
  EXPECT_EQ(materials, 2u);
}

// Detects (invariant): the collides-with-nothing flag being read from the name table, set for a layer with a CollidesWith list, or set for an empty-list layer another layer still lists; only an empty list that nobody lists (the L_NAVCUT shape) is flagged, a gap or an index past the table is not, and an override that gives the flagged layer a list clears it.
TEST(CapiCollisionNames, FlagsOnlyALayerWithAnEmptyListThatNoOtherLayerLists) {
  const TempRoot root;
  {
    const Env env({root.write("Base.esm", collides_plugin())});
    const Names names(env.value);
    EXPECT_FALSE(collides_with_nothing(names, 0));
    EXPECT_FALSE(collides_with_nothing(names, 1));
    EXPECT_FALSE(collides_with_nothing(names, 2));
    EXPECT_FALSE(collides_with_nothing(names, 3));
    EXPECT_FALSE(collides_with_nothing(names, 4));
    EXPECT_TRUE(collides_with_nothing(names, 5));
    EXPECT_FALSE(collides_with_nothing(names, 4000));
  }
  const Env patched({root.write("Base.esm", collides_plugin()), root.write("Patch.esp", collides_patch_plugin())});
  const Names names(patched.value);
  EXPECT_FALSE(collides_with_nothing(names, 5));
  EXPECT_STREQ(layer(names, 5), "L_NAVCUT");
}

// Detects (invariant): the per-layer CollidesWith read resolving entries to the wrong layer index, leaking an empty list as non-null, keeping a gap, an unnamed layer or an index past the table non-empty, or not following a winning override.
TEST(CapiCollisionNames, ReturnsTheWinningCollidesWithListAsLayerIndexes) {
  const TempRoot root;
  {
    const Env env({root.write("Base.esm", collides_plugin())});
    const Names names(env.value);
    EXPECT_EQ(collides_with(names, 0), (std::vector<std::uint32_t>{1}));
    EXPECT_EQ(collides_with(names, 1), (std::vector<std::uint32_t>{0, 3}));
    EXPECT_TRUE(collides_with(names, 2).empty());
    EXPECT_TRUE(collides_with(names, 3).empty());
    EXPECT_TRUE(collides_with(names, 4).empty());
    EXPECT_TRUE(collides_with(names, 5).empty());
    EXPECT_TRUE(collides_with(names, 4000).empty());
  }
  const Env patched({root.write("Base.esm", collides_plugin()), root.write("Patch.esp", collides_patch_plugin())});
  const Names names(patched.value);
  EXPECT_EQ(collides_with(names, 5), (std::vector<std::uint32_t>{1}));
  EXPECT_EQ(collides_with(names, 1), (std::vector<std::uint32_t>{0, 3}));
}

// Detects (invariant): a duplicate entry returned twice, a self entry dropped, a list entry that names no named winning COLL record kept, or the list not ascending.
TEST(CapiCollisionNames, DropsDanglingEntriesAndSortsAndDeduplicatesTheList) {
  const TempRoot root;
  const Env env({root.write("Base.esm", messy_plugin())});
  const Names names(env.value);
  EXPECT_EQ(collides_with(names, 0), (std::vector<std::uint32_t>{0, 1}));
  EXPECT_TRUE(collides_with(names, 1).empty());
  EXPECT_FALSE(collides_with_nothing(names, 0));
  EXPECT_FALSE(collides_with_nothing(names, 1));
}

TEST(CapiCollisionNames, NamesMaterialsFromHashedEditorIds) {
  const TempRoot root;
  const Env env({root.write("Base.esm", base_plugin())});
  const Names names(env.value);
  EXPECT_STREQ(material(names, k_wood_crate_crc), "MaterialWoodCrate");
  EXPECT_STREQ(material(names, k_metal_crc), "Metal");
  EXPECT_EQ(material(names, 1u), nullptr);
  EXPECT_EQ(material(names, 0u), nullptr);
}

TEST(CapiCollisionNames, WinningOverrideRenamesALayer) {
  const TempRoot root;
  const Env env({root.write("Base.esm", base_plugin()), root.write("Patch.esp", patch_plugin())});
  const Names names(env.value);
  EXPECT_STREQ(layer(names, 1), "L_STATIC_PATCHED");
  EXPECT_STREQ(layer(names, 0), "L_UNIDENTIFIED");
}

TEST(CapiCollisionNames, AnEnvironmentWithoutCollOrMattHasEmptyTables) {
  const TempRoot root;
  const Env env({root.write("Empty.esm", empty_plugin())});
  const Names names(env.value);
  ASSERT_NE(names.value, nullptr);
  EXPECT_EQ(layer(names, 0), nullptr);
  EXPECT_EQ(material(names, k_metal_crc), nullptr);
  size_t layers = 1;
  size_t materials = 1;
  ASSERT_EQ(radicle_collision_names_counts(names.value, &layers, &materials), RADICLE_STATUS_OK);
  EXPECT_EQ(layers, 0u);
  EXPECT_EQ(materials, 0u);
}

TEST(CapiCollisionNames, RejectsMissingArguments) {
  const TempRoot root;
  const Env env({root.write("Base.esm", base_plugin())});
  const Names names(env.value);
  radicle_collision_names* out = reinterpret_cast<radicle_collision_names*>(1);
  EXPECT_EQ(radicle_collision_names_build(nullptr, &out), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(out, nullptr);
  EXPECT_EQ(radicle_collision_names_build(env.value, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  const char* name = reinterpret_cast<const char*>(1);
  EXPECT_EQ(radicle_collision_names_layer(nullptr, 0, &name), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(name, nullptr);
  EXPECT_EQ(radicle_collision_names_layer(names.value, 0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  std::int32_t flag = 7;
  EXPECT_EQ(radicle_collision_names_layer_collides_with_nothing(nullptr, 0, &flag), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(flag, 0);
  EXPECT_EQ(radicle_collision_names_layer_collides_with_nothing(names.value, 0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  const std::uint32_t* values = reinterpret_cast<const std::uint32_t*>(1);
  size_t count = 7;
  EXPECT_EQ(radicle_collision_names_layer_collides_with(nullptr, 0, &values, &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(values, nullptr);
  EXPECT_EQ(count, 0u);
  EXPECT_EQ(radicle_collision_names_layer_collides_with(names.value, 0, nullptr, &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_collision_names_layer_collides_with(names.value, 0, &values, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_collision_names_material(nullptr, 0, &name), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_collision_names_material(names.value, 0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  size_t layers = 0;
  size_t materials = 0;
  EXPECT_EQ(radicle_collision_names_counts(nullptr, &layers, &materials), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_collision_names_counts(names.value, nullptr, &materials), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_collision_names_counts(names.value, &layers, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_collision_names_destroy(nullptr);
}
