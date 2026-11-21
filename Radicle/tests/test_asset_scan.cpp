#include "fo4/archive/asset_scan.hpp"
#include "fo4/plugin/reader.hpp"
#include "ba2_fixture.hpp"
#include "plugin_bytes.hpp"
#include "temp_data_dir.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

fo4::Subrecord path_subrecord(std::string_view signature, std::string_view path) {
  fo4::Subrecord subrecord;
  subrecord.signature = fo4::Signature::from_string(signature);
  for (const char ch : path) subrecord.data.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  subrecord.data.push_back(std::byte{0});
  return subrecord;
}

fo4::Record model_record() {
  fo4::Record record;
  record.header.signature = fo4::Signature::from_string("STAT");
  record.form_key = fo4::FormKey::parse("000800:Scan.esp");
  record.subrecords.push_back(path_subrecord("MODL", "Weapons\\gone.nif"));
  record.subrecords.push_back(path_subrecord("MODL", "Weapons\\gone.nif"));
  record.subrecords.push_back(path_subrecord("ICON", "Interface\\here.dds"));
  return record;
}

fo4::Plugin scan_plugin() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  const auto group = fo4::test::begin_group(out, "STAT", 0);
  const auto record = fo4::test::begin_record(out, "STAT", 0, 0x000800);
  fo4::test::write_subrecord(out, "MODL", std::string_view{"Weapons\\gone.nif\0", 17});
  fo4::test::write_subrecord(out, "ICON", std::string_view{"Interface\\here.dds\0", 19});
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return fo4::read_plugin(out.bytes, "Scan.esp");
}

// Detects: a Data-relative scan called every real model and icon missing, so paths must be rooted.
TEST(AssetScan, RecordAssetPathsCollectsModelAndIconOnce) {
  const auto paths = fo4::archive::record_asset_paths(model_record());
  ASSERT_EQ(paths.size(), 2u);
  EXPECT_EQ(paths[0], "meshes/weapons/gone.nif");
  EXPECT_EQ(paths[1], "textures/interface/here.dds");

  fo4::Record rooted;
  rooted.header.signature = fo4::Signature::from_string("STAT");
  rooted.form_key = fo4::FormKey::parse("000802:Scan.esp");
  rooted.subrecords.push_back(path_subrecord("MODL", "Meshes\\Weapons\\x.nif"));
  const auto rooted_paths = fo4::archive::record_asset_paths(rooted);
  ASSERT_EQ(rooted_paths.size(), 1u);
  EXPECT_EQ(rooted_paths[0], "meshes/weapons/x.nif");

  // Detects: a leading separator kept the path from matching its root, so the root was added twice.
  fo4::Record slashed;
  slashed.header.signature = fo4::Signature::from_string("STAT");
  slashed.form_key = fo4::FormKey::parse("000803:Scan.esp");
  slashed.subrecords.push_back(path_subrecord("MODL", "\\Meshes\\Weapons\\x.nif"));
  const auto slashed_paths = fo4::archive::record_asset_paths(slashed);
  ASSERT_EQ(slashed_paths.size(), 1u);
  EXPECT_EQ(slashed_paths[0], "meshes/weapons/x.nif");
}

TEST(AssetScan, EmptyAndUnterminatedPayloadsAreIgnored) {
  fo4::Record record;
  record.header.signature = fo4::Signature::from_string("STAT");
  record.form_key = fo4::FormKey::parse("000800:Scan.esp");
  fo4::Subrecord empty;
  empty.signature = fo4::Signature::from_string("MODL");
  empty.data.push_back(std::byte{0});
  record.subrecords.push_back(std::move(empty));
  fo4::Subrecord unterminated;
  unterminated.signature = fo4::Signature::from_string("ICON");
  unterminated.data = {std::byte{'x'}};
  record.subrecords.push_back(std::move(unterminated));
  fo4::Subrecord other;
  other.signature = fo4::Signature::from_string("EDID");
  other.data = {std::byte{'E'}, std::byte{0}};
  record.subrecords.push_back(std::move(other));
  EXPECT_TRUE(fo4::archive::record_asset_paths(record).empty());
}

TEST(AssetScan, FindMissingAssetsReportsAbsentOnly) {
  fo4::test::TempDataDir dir;
  dir.write("textures/interface/here.dds", {std::byte{0}}, 0);
  const std::vector<std::filesystem::path> roots{dir.path()};
  const std::vector<std::filesystem::path> no_archives{};
  const auto resolver = fo4::archive::AssetResolver::open_roots(roots, no_archives);
  const auto missing = fo4::archive::find_missing_assets(scan_plugin(), resolver);
  ASSERT_EQ(missing.size(), 1u);
  EXPECT_EQ(missing[0].record, fo4::FormKey::parse("000800:Scan.esp"));
  EXPECT_EQ(missing[0].path, "meshes/weapons/gone.nif");
}

// Detects: archive-only assets must be treated as present while genuinely absent paths remain reported.
TEST(AssetScan, FindMissingAssetsResolvesArchiveOnlyPaths) {
  fo4::test::TempDataDir dir;
  fo4::archive::Ba2GeneralArchive archive;
  archive.has_string_table = true;
  archive.entries.push_back(ba2_fixture::general("meshes\\weapons\\gone.nif", "archived model bytes"));
  const auto archive_path = dir.path() / "Assets.ba2";
  fo4::archive::write_ba2_general_file(archive, archive_path);

  const std::vector<std::filesystem::path> no_roots{};
  const std::vector<std::filesystem::path> archives{archive_path};
  const auto resolver = fo4::archive::AssetResolver::open_roots(no_roots, archives);
  const auto missing = fo4::archive::find_missing_assets(scan_plugin(), resolver);
  ASSERT_EQ(missing.size(), 1u);
  EXPECT_EQ(missing[0].record, fo4::FormKey::parse("000800:Scan.esp"));
  EXPECT_EQ(missing[0].path, "textures/interface/here.dds");
}

TEST(AssetScan, RecordAssetPathsSkipsArmorModelFormIds) {
  fo4::Record armor;
  armor.header.signature = fo4::Signature::from_string("ARMO");
  armor.form_key = fo4::FormKey::parse("000801:Scan.esp");
  fo4::Subrecord modl;
  modl.signature = fo4::Signature::from_string("MODL");
  modl.data = {std::byte{0x01}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}};
  armor.subrecords.push_back(modl);
  armor.subrecords.push_back(path_subrecord("MOD2", "Armor\\body.nif"));
  const auto paths = fo4::archive::record_asset_paths(armor);
  ASSERT_EQ(paths.size(), 1u);
  EXPECT_EQ(paths[0], "meshes/armor/body.nif");

  fo4::Record arma = armor;
  arma.header.signature = fo4::Signature::from_string("ARMA");
  EXPECT_EQ(fo4::archive::record_asset_paths(arma).size(), 1u);

  fo4::Record stature = armor;
  stature.header.signature = fo4::Signature::from_string("STAT");
  EXPECT_EQ(fo4::archive::record_asset_paths(stature).size(), 2u);
}

TEST(AssetScan, ArmorAddonFemaleModelsAreRootedAndScannedOnlyOnArma) {
  fo4::Record arma;
  arma.header.signature = fo4::Signature::from_string("ARMA");
  arma.form_key = fo4::FormKey::parse("000804:Scan.esp");
  arma.subrecords.push_back(path_subrecord("MOD3", "Armor\\female.nif"));
  arma.subrecords.push_back(path_subrecord("MOD5", "Armor\\fp_female.nif"));
  const auto paths = fo4::archive::record_asset_paths(arma);
  ASSERT_EQ(paths.size(), 2u);
  EXPECT_EQ(paths[0], "meshes/armor/female.nif");
  EXPECT_EQ(paths[1], "meshes/armor/fp_female.nif");

  fo4::Record stature = arma;
  stature.header.signature = fo4::Signature::from_string("STAT");
  EXPECT_TRUE(fo4::archive::record_asset_paths(stature).empty());
}

fo4::Plugin addon_plugin() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  const auto group = fo4::test::begin_group(out, "ARMA", 0);
  const auto record = fo4::test::begin_record(out, "ARMA", 0, 0x000800);
  fo4::test::write_subrecord(out, "MOD3", std::string_view{"Armor\\female.nif\0", 17});
  fo4::test::write_subrecord(out, "MOD5", std::string_view{"Armor\\fp_female.nif\0", 20});
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return fo4::read_plugin(out.bytes, "Scan.esp");
}

TEST(AssetScan, FindMissingAssetsReportsAbsentArmorAddonFemaleModels) {
  fo4::test::TempDataDir dir;
  const std::vector<std::filesystem::path> roots{dir.path()};
  const std::vector<std::filesystem::path> no_archives{};
  const auto resolver = fo4::archive::AssetResolver::open_roots(roots, no_archives);
  const auto missing = fo4::archive::find_missing_assets(addon_plugin(), resolver);
  ASSERT_EQ(missing.size(), 2u);
  EXPECT_EQ(missing[0].record, fo4::FormKey::parse("000800:Scan.esp"));
  EXPECT_EQ(missing[0].path, "meshes/armor/female.nif");
  EXPECT_EQ(missing[1].path, "meshes/armor/fp_female.nif");
}

fo4::Plugin bad_path_plugin() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  const auto group = fo4::test::begin_group(out, "STAT", 0);
  const auto record = fo4::test::begin_record(out, "STAT", 0, 0x000800);
  fo4::test::write_subrecord(out, "MODL", std::string_view{"..\\evil.nif\0", 12});
  fo4::test::write_subrecord(out, "MODL", std::string_view{"Weapons\\gone.nif\0", 17});
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return fo4::read_plugin(out.bytes, "Scan.esp");
}

TEST(AssetScan, FindMissingAssetsSkipsPathsThatCannotNormalize) {
  fo4::test::TempDataDir dir;
  const std::vector<std::filesystem::path> roots{dir.path()};
  const std::vector<std::filesystem::path> no_archives{};
  const auto resolver = fo4::archive::AssetResolver::open_roots(roots, no_archives);
  const auto missing = fo4::archive::find_missing_assets(bad_path_plugin(), resolver);
  ASSERT_EQ(missing.size(), 1u);
  EXPECT_EQ(missing[0].path, "meshes/weapons/gone.nif");
}

TEST(AssetScan, FindMissingAssetsIsCleanWhenEverythingResolves) {
  fo4::test::TempDataDir dir;
  dir.write("meshes/weapons/gone.nif", {std::byte{0}}, 0);
  dir.write("textures/interface/here.dds", {std::byte{0}}, 0);
  const std::vector<std::filesystem::path> roots{dir.path()};
  const std::vector<std::filesystem::path> no_archives{};
  const auto resolver = fo4::archive::AssetResolver::open_roots(roots, no_archives);
  EXPECT_TRUE(fo4::archive::find_missing_assets(scan_plugin(), resolver).empty());
}

fo4::Record acti_record() {
  fo4::Record record;
  record.header.signature = fo4::Signature::from_string("ACTI");
  record.form_key = fo4::FormKey::parse("000800:Scan.esp");
  record.subrecords.push_back(path_subrecord("MODL", "Clutter\\Gone.NIF"));
  record.subrecords.push_back(path_subrecord("DMDL", "Clutter\\Gone.nif"));
  record.subrecords.push_back(path_subrecord("DMDL", "Clutter\\Broken.nif"));
  record.subrecords.push_back(path_subrecord("DMDL", "Clutter\\notes.txt"));
  record.subrecords.push_back(path_subrecord("EDID", "PlainEditorId"));
  return record;
}

TEST(AssetScan, SchemaAssetStringsKeepModelAndDestructionPathsOnce) {
  const auto paths = fo4::archive::record_schema_asset_strings(acti_record());
  ASSERT_EQ(paths.size(), 2u);
  EXPECT_NE(std::find(paths.begin(), paths.end(), "clutter/gone.nif"), paths.end());
  EXPECT_NE(std::find(paths.begin(), paths.end(), "clutter/broken.nif"), paths.end());
}

TEST(AssetScan, SchemaAssetStringsIgnoreMalformedPayloads) {
  fo4::Record record;
  record.header.signature = fo4::Signature::from_string("ACTI");
  record.form_key = fo4::FormKey::parse("000800:Scan.esp");
  fo4::Subrecord unterminated;
  unterminated.signature = fo4::Signature::from_string("MODL");
  for (const char ch : std::string_view{"a.nif"}) unterminated.data.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  record.subrecords.push_back(std::move(unterminated));
  record.subrecords.push_back(path_subrecord("DMDL", "b.nif"));
  const auto paths = fo4::archive::record_schema_asset_strings(record);
  ASSERT_EQ(paths.size(), 1u);
  EXPECT_EQ(paths[0], "b.nif");
}

fo4::Plugin owned_and_master_plugin() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Base.esm\0", 9});
  fo4::test::ByteWriter data;
  data.u32(0);
  data.u32(0);
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::end_record(out, tes4);
  const auto group = fo4::test::begin_group(out, "ACTI", 0);
  const auto own = fo4::test::begin_record(out, "ACTI", 0, 0x01000800);
  fo4::test::write_subrecord(out, "MODL", std::string_view{"Clutter\\Own.nif\0", 16});
  fo4::test::end_record(out, own);
  const auto override_record = fo4::test::begin_record(out, "ACTI", 0, 0x00000801);
  fo4::test::write_subrecord(out, "MODL", std::string_view{"Clutter\\Inherited.nif\0", 22});
  fo4::test::end_record(out, override_record);
  fo4::test::end_group(out, group);
  return fo4::read_plugin(out.bytes, "Scan.esp");
}

TEST(AssetScan, CollectAssetReferencesCoversOnlyRecordsTheCallerOwns) {
  const auto plugin = owned_and_master_plugin();
  const auto references = fo4::archive::collect_asset_references(plugin);
  ASSERT_EQ(references.size(), 1u);
  EXPECT_EQ(references[0].path, "clutter/own.nif");
  EXPECT_EQ(references[0].record.local().value(), 0x800u);
}

TEST(AssetScan, AssetPathCandidatesAddTheExtensionRootOnlyWhenUnrooted) {
  using fo4::archive::asset_path_candidates;
  EXPECT_EQ(asset_path_candidates("Clutter/Rock.NIF"),
            (std::vector<std::string>{"clutter/rock.nif", "meshes/clutter/rock.nif"}));
  EXPECT_EQ(asset_path_candidates("Meshes\\Clutter\\Rock.nif"),
            (std::vector<std::string>{"meshes/clutter/rock.nif"}));
  EXPECT_EQ(asset_path_candidates("Data\\Textures\\a.dds"), (std::vector<std::string>{"textures/a.dds"}));
  EXPECT_EQ(asset_path_candidates("a.dds"), (std::vector<std::string>{"a.dds", "textures/a.dds"}));
  EXPECT_EQ(asset_path_candidates("x/a.bgsm").back(), "materials/x/a.bgsm");
  EXPECT_EQ(asset_path_candidates("Interface\\a.dds"),
            (std::vector<std::string>{"interface/a.dds", "textures/interface/a.dds"}));
  EXPECT_THROW(asset_path_candidates("..\\evil.nif"), fo4::archive::AssetPathError);
}

}  // namespace
