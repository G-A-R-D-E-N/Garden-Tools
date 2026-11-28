#include "fo4/archive/asset_scan.hpp"
#include "fo4/hkx/animation_index.hpp"
#include "fo4/hkx/animation_sources.hpp"
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
#include <optional>
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

void put_file(const std::filesystem::path& file, std::string_view text = "x") {
  std::filesystem::create_directories(file.parent_path());
  std::ofstream out{file, std::ios::binary};
  out << text;
}

void put_ba2(const std::filesystem::path& file, const std::vector<std::string>& paths) {
  ba2_fixture::Ba2GeneralArchive archive;
  archive.has_string_table = true;
  for (const auto& path : paths) archive.entries.push_back(ba2_fixture::general(path, "ba2"));
  std::filesystem::create_directories(file.parent_path());
  fo4::archive::write_ba2_general_file(archive, file);
}

struct Mo2Tree {
  fo4::test::TempDataDir dir;

  explicit Mo2Tree(const std::string& modlist) {
    put_file(dir.path() / "ModOrganizer.ini",
             "selected_profile=Test\ngamePath=" + (dir.path() / "Game").string() + "\n");
    put_file(data() / "Fallout4.esm", "");
    put_file(dir.path() / "profiles" / "Test" / "modlist.txt", modlist);
    put_file(dir.path() / "profiles" / "Test" / "plugins.txt", "");
  }

  std::filesystem::path data() const { return dir.path() / "Game" / "Data"; }
  std::filesystem::path mod(const std::string& name) const { return dir.path() / "mods" / name; }
};

fo4::hkx::AnimationSources open_mo2(const Mo2Tree& tree, const std::string& profile = "") {
  fo4::hkx::AnimationSourceRequest request;
  request.mo2_instance = tree.dir.path();
  request.profile = profile;
  return fo4::hkx::open_animation_sources(request);
}

fo4::hkx::AnimationSources open_data(const std::filesystem::path& data) {
  fo4::hkx::AnimationSourceRequest request;
  request.data_dir = data;
  return fo4::hkx::open_animation_sources(request);
}

const fo4::hkx::AnimationIndexEntry* find_entry(const fo4::hkx::AnimationIndex& index, std::string_view path) {
  for (const auto& entry : index.entries) {
    if (entry.path == path) return &entry;
  }
  return nullptr;
}

std::vector<std::string> labels_of(const std::vector<fo4::hkx::AnimationProvider>& providers) {
  std::vector<std::string> labels;
  for (const auto& provider : providers) labels.push_back(provider.label);
  return labels;
}

constexpr std::string_view k_walk = "meshes/actors/character/animations/walk.hkx";

void layer_walk(const Mo2Tree& tree) {
  put_file(tree.data() / k_walk, "data");
  put_file(tree.mod("Low") / k_walk, "low");
  put_file(tree.mod("High") / k_walk, "high");
  put_file(tree.dir.path() / "overwrite" / k_walk, "overwrite!");
  put_file(tree.mod("Off") / k_walk, "off");
  put_file(tree.mod("Off") / "meshes/actors/ghost/animations/boo.hkx", "off");
  put_file(tree.mod("Low") / "meshes/actors/character/animations/dance.hkt", "low");
  put_ba2(tree.data() / "Base.ba2", {std::string(k_walk), "meshes/actors/wolf/animations/howl.hkx"});
  put_ba2(tree.mod("High") / "Pack.ba2", {std::string(k_walk)});
}

// Detects: the Data file beating the overwrite and mod files; roots must reach the resolver highest priority first.
TEST(AnimationIndex, Mo2WinnerIsOverwriteThenHighThenLowThenDataThenArchivesLastOpenedFirst) {
  Mo2Tree tree("+High\n+Low\n-Off\n+Tools_separator\n");
  layer_walk(tree);

  const auto sources = open_mo2(tree);
  const auto index = fo4::hkx::build_animation_index(sources.resolver);

  EXPECT_EQ(sources.manager, "mo2");
  EXPECT_EQ(sources.profile, "Test");
  const auto* walk = find_entry(index, k_walk);
  ASSERT_NE(walk, nullptr);
  EXPECT_EQ(walk->winner.label, "overwrite");
  EXPECT_FALSE(walk->winner.archive);
  EXPECT_EQ(walk->size, 10u);
  EXPECT_EQ(walk->winner.file, tree.dir.path() / "overwrite" / k_walk);
  EXPECT_EQ(labels_of(walk->shadowed),
            (std::vector<std::string>{"High", "Low", "Data", "Pack.ba2", "Base.ba2"}));
  EXPECT_EQ(labels_of(fo4::hkx::animation_index_sources(sources.resolver)),
            (std::vector<std::string>{"overwrite", "High", "Low", "Data", "Pack.ba2", "Base.ba2"}));
}

// Detects: a disabled mod, separator or dropped .hkt leaking into the index; each path must appear once, sorted.
TEST(AnimationIndex, DisabledModsAreExcludedAndEachPathAppearsOnceInSortedOrder) {
  Mo2Tree tree("+High\n+Low\n-Off\n+Tools_separator\n");
  layer_walk(tree);

  const auto sources = open_mo2(tree);
  const auto index = fo4::hkx::build_animation_index(sources.resolver);

  std::vector<std::string> paths;
  for (const auto& entry : index.entries) paths.push_back(entry.path);
  EXPECT_EQ(paths, (std::vector<std::string>{"meshes/actors/character/animations/dance.hkt",
                                             "meshes/actors/character/animations/walk.hkx",
                                             "meshes/actors/wolf/animations/howl.hkx"}));
}

// Detects: an archive-only file shown as loose; the provider must name the archive that supplies it.
TEST(AnimationIndex, ArchiveOnlyEntryNamesItsArchiveAsProvider) {
  Mo2Tree tree("+High\n+Low\n");
  layer_walk(tree);

  const auto sources = open_mo2(tree);
  const auto index = fo4::hkx::build_animation_index(sources.resolver);

  const auto* howl = find_entry(index, "meshes/actors/wolf/animations/howl.hkx");
  ASSERT_NE(howl, nullptr);
  EXPECT_TRUE(howl->winner.archive);
  EXPECT_EQ(howl->winner.label, "Base.ba2");
  EXPECT_TRUE(howl->shadowed.empty());
  EXPECT_EQ(howl->project, "wolf");
}

// Detects: the profile argument ignored; the requested profile must supply the modlist.
TEST(AnimationIndex, ExplicitProfileSelectsItsOwnModlist) {
  Mo2Tree tree("+High\n");
  put_file(tree.dir.path() / "profiles" / "Other" / "modlist.txt", "+Low\n");
  put_file(tree.dir.path() / "profiles" / "Other" / "plugins.txt", "");
  put_file(tree.mod("High") / "meshes/actors/character/animations/high_only.hkx");
  put_file(tree.mod("Low") / "meshes/actors/character/animations/low_only.hkx");

  const auto sources = open_mo2(tree, "Other");
  const auto index = fo4::hkx::build_animation_index(sources.resolver);

  EXPECT_EQ(sources.profile, "Other");
  EXPECT_NE(find_entry(index, "meshes/actors/character/animations/low_only.hkx"), nullptr);
  EXPECT_EQ(find_entry(index, "meshes/actors/character/animations/high_only.hkx"), nullptr);
}

// Detects: the absent overwrite folder reported as a problem; a missing enabled mod folder must be noted.
TEST(AnimationIndex, MissingEnabledModFolderIsNotedButAnAbsentOverwriteIsNot) {
  Mo2Tree tree("+Gone\n+Here\n");
  put_file(tree.mod("Here") / k_walk);

  const auto sources = open_mo2(tree);

  ASSERT_EQ(sources.notes.size(), 1u);
  EXPECT_NE(sources.notes[0].find("Gone"), std::string::npos);
}

// Detects: the base skeleton winning over a mod override; the match must follow the resolver winner.
TEST(AnimationIndex, SkeletonOverriddenByAModIsTheOneMatched) {
  Mo2Tree tree("+High\n");
  const std::string assets = "meshes/actors/character/characterassets/";
  put_file(tree.data() / k_walk);
  put_file(tree.data() / (assets + "skeleton.hkx"), "base");
  put_file(tree.data() / (assets + "skeleton.nif"));
  put_file(tree.data() / "meshes/actors/character/bodyparts/malebody.nif");
  put_file(tree.mod("High") / (assets + "skeleton.hkx"), "mod");

  const auto sources = open_mo2(tree);
  const auto index = fo4::hkx::build_animation_index(sources.resolver);
  const auto actor = fo4::hkx::resolve_animation_actor(index, k_walk);

  EXPECT_EQ(actor.actor_root, "meshes/actors/character");
  ASSERT_TRUE(actor.skeleton.has_value());
  EXPECT_EQ(*actor.skeleton, assets + "skeleton.hkx");
  ASSERT_TRUE(actor.skeleton_provider.has_value());
  EXPECT_EQ(actor.skeleton_provider->label, "High");
  ASSERT_TRUE(actor.skeleton_nif.has_value());
  EXPECT_EQ(*actor.skeleton_nif, assets + "skeleton.nif");
  ASSERT_TRUE(actor.skeleton_nif_provider.has_value());
  EXPECT_EQ(actor.skeleton_nif_provider->label, "Data");
  ASSERT_TRUE(actor.body.has_value());
  EXPECT_EQ(*actor.body, "meshes/actors/character/bodyparts/malebody.nif");
  EXPECT_EQ(actor.body_candidate_total, 1u);
}

// Detects: a guessed skeleton or body among several; ambiguity must pick none and an exact skeleton name must win.
TEST(AnimationIndex, AmbiguousSkeletonOrBodyPicksNoneAndExactSkeletonNameWins) {
  fo4::test::TempDataDir dir;
  const auto data = dir.path() / "Data";
  put_file(data / "meshes/actors/wolf/characterassets/skeleton_a.hkx");
  put_file(data / "meshes/actors/wolf/characterassets/skeleton_b.hkx");
  put_file(data / "meshes/actors/wolf/animations/howl.hkx");
  put_file(data / "meshes/actors/wolf/body1.nif");
  put_file(data / "meshes/actors/wolf/body2.nif");
  put_file(data / "meshes/actors/dog/characterassets/skeleton.hkx");
  put_file(data / "meshes/actors/dog/characterassets/skeleton_old.hkx");
  put_file(data / "meshes/actors/cat/animations/purr.hkx");

  const auto sources = open_data(data);
  const auto index = fo4::hkx::build_animation_index(sources.resolver);

  const auto wolf = fo4::hkx::resolve_animation_actor(index, "meshes/actors/wolf/animations/howl.hkx");
  EXPECT_FALSE(wolf.skeleton.has_value());
  EXPECT_EQ(wolf.skeleton_candidates.size(), 2u);
  EXPECT_FALSE(wolf.body.has_value());
  EXPECT_EQ(wolf.body_candidate_total, 2u);
  EXPECT_EQ(wolf.body_candidates.size(), 2u);
  EXPECT_EQ(wolf.notes.size(), 2u);

  const auto dog = fo4::hkx::resolve_animation_actor(index, "meshes/actors/dog/animations/x.hkx");
  ASSERT_TRUE(dog.skeleton.has_value());
  EXPECT_EQ(*dog.skeleton, "meshes/actors/dog/characterassets/skeleton.hkx");
  EXPECT_EQ(dog.skeleton_candidates.size(), 2u);

  const auto cat = fo4::hkx::resolve_animation_actor(index, "meshes/actors/cat/animations/purr.hkx");
  EXPECT_FALSE(cat.skeleton.has_value());
  EXPECT_FALSE(cat.body.has_value());
  EXPECT_EQ(cat.notes.size(), 2u);

  const auto outside = fo4::hkx::resolve_animation_actor(index, "meshes/other/x.hkx");
  EXPECT_TRUE(outside.actor_root.empty());
  EXPECT_EQ(outside.notes.size(), 1u);
}

// Detects: a non-MO2 folder reading as an unmodded game; it must be refused and name Vortex as unsupported.
TEST(AnimationSources, FolderWithoutModOrganizerIniIsRefusedAndNamesVortexAsUnsupported) {
  fo4::test::TempDataDir dir;
  put_file(dir.path() / "Data" / "Fallout4.esm", "");
  fo4::hkx::AnimationSourceRequest request;
  request.mo2_instance = dir.path();

  try {
    fo4::hkx::open_animation_sources(request);
    FAIL() << "expected AnimationSourceError";
  } catch (const fo4::hkx::AnimationSourceError& error) {
    EXPECT_NE(std::string(error.what()).find("Vortex"), std::string::npos);
  }
}

// Detects: a missing modlist.txt, bad profile name or missing Data folder giving an empty index; each must throw.
TEST(AnimationSources, MissingProfileModlistBadProfileNameAndMissingDataFolderAreRefused) {
  Mo2Tree tree("+High\n");
  for (const auto& escaped : {tree.dir.path() / "Test", tree.dir.path() / "profiles" / "a\\b"}) {
    put_file(escaped / "modlist.txt", "+High\n");
    put_file(escaped / "plugins.txt", "");
  }
  EXPECT_THROW(open_mo2(tree, "NoSuchProfile"), fo4::hkx::AnimationSourceError);
  EXPECT_THROW(open_mo2(tree, "../Test"), fo4::hkx::AnimationSourceError);
  EXPECT_THROW(open_mo2(tree, "a\\b"), fo4::hkx::AnimationSourceError);
  EXPECT_THROW(open_mo2(tree, "C:Test"), fo4::hkx::AnimationSourceError);
  Mo2Tree dotdot("+..\n");
  EXPECT_THROW(open_mo2(dotdot), fo4::hkx::AnimationSourceError);
  Mo2Tree separated("+a/b\n");
  EXPECT_THROW(open_mo2(separated), fo4::hkx::AnimationSourceError);
  Mo2Tree noPlugins("+High\n");
  std::filesystem::remove(noPlugins.dir.path() / "profiles" / "Test" / "plugins.txt");
  EXPECT_THROW(open_mo2(noPlugins), fo4::hkx::AnimationSourceError);

  fo4::test::TempDataDir dir;
  EXPECT_THROW(open_data(dir.path() / "NoData"), fo4::hkx::AnimationSourceError);
  EXPECT_THROW(open_data({}), fo4::hkx::AnimationSourceError);
}

// Detects: a silent Vortex deployment; the marker must set the manager and a note while plain Data must not.
TEST(AnimationSources, VortexMarkerInDataSetsManagerAndNoteWhilePlainDataDoesNot) {
  fo4::test::TempDataDir dir;
  const auto data = dir.path() / "Data";
  put_file(data / "meshes/actors/character/animations/walk.hkx");

  const auto plain = open_data(data);
  EXPECT_EQ(plain.manager, "none");
  EXPECT_TRUE(plain.notes.empty());

  put_file(data / "vortex.deployment.json", "{}");
  const auto vortex = open_data(data);
  const auto index = fo4::hkx::build_animation_index(vortex.resolver);
  EXPECT_EQ(vortex.manager, "vortex");
  ASSERT_EQ(vortex.notes.size(), 1u);
  EXPECT_NE(vortex.notes[0].find("Vortex"), std::string::npos);
  ASSERT_EQ(index.entries.size(), 1u);
  EXPECT_EQ(index.entries[0].winner.label, "Data");
}

// Detects: project, race, character or kind read from the wrong folder; classification must follow the role folder.
TEST(AnimationIndex, ClassifyHandlesNestedProjectsFirstPersonAndPathsWithoutARoleFolder) {
  using fo4::hkx::AnimationKind;
  using fo4::hkx::classify_animation_path;

  const auto first = classify_animation_path("Meshes\\Actors\\Character\\_1stPerson\\Animations\\Reload.hkx");
  EXPECT_EQ(first.path, "meshes/actors/character/_1stperson/animations/reload.hkx");
  EXPECT_EQ(first.kind, AnimationKind::animation);
  EXPECT_EQ(first.project, "character/_1stperson");
  EXPECT_EQ(first.race, "_1stperson");
  EXPECT_EQ(first.actor_root, "meshes/actors/character/_1stperson");
  EXPECT_TRUE(first.character.empty());

  const auto nested = classify_animation_path("meshes/actors/fx/deathclaw/behaviors/main.hkx");
  EXPECT_EQ(nested.kind, AnimationKind::behaviour);
  EXPECT_EQ(nested.project, "fx/deathclaw");
  EXPECT_EQ(nested.race, "deathclaw");

  const auto character = classify_animation_path("meshes/actors/character/characters/femalecharacter.hkx");
  EXPECT_EQ(character.kind, AnimationKind::character);
  EXPECT_EQ(character.project, "character");
  EXPECT_EQ(character.character, "femalecharacter");

  const auto skeleton = classify_animation_path("meshes/actors/character/characterassets/skeleton.hkx");
  EXPECT_EQ(skeleton.kind, AnimationKind::skeleton);
  EXPECT_EQ(skeleton.character, "skeleton");

  const auto bare = classify_animation_path("meshes/actors/character/misc/project.hkx");
  EXPECT_EQ(bare.kind, AnimationKind::project);
  EXPECT_EQ(bare.project, "character/misc");
  EXPECT_EQ(bare.race, "misc");

  const auto shallow = classify_animation_path("meshes/actors/loose.hkx");
  EXPECT_EQ(shallow.kind, AnimationKind::project);
  EXPECT_TRUE(shallow.project.empty());
  EXPECT_TRUE(shallow.actor_root.empty());

  const auto outside = classify_animation_path("meshes/animtextdata/thing.hkx");
  EXPECT_EQ(outside.kind, AnimationKind::other);
  EXPECT_TRUE(outside.project.empty());
  EXPECT_TRUE(outside.actor_root.empty());

  EXPECT_EQ(fo4::hkx::animation_kind_from_name("skeleton"), AnimationKind::skeleton);
  EXPECT_FALSE(fo4::hkx::animation_kind_from_name("nonsense").has_value());
}

// Detects: case-sensitive filters or duplicate facets; each filter must narrow the result and facets must be unique.
TEST(AnimationIndex, FiltersNarrowByKindProjectRaceCharacterAndQueryAndFacetsAreUnique) {
  using fo4::hkx::AnimationIndexFilter;
  using fo4::hkx::AnimationKind;
  fo4::hkx::AnimationIndex index;
  const std::vector<std::pair<std::string, std::string>> rows{
      {"meshes/actors/character/animations/walk.hkx", "High"},
      {"meshes/actors/character/behaviors/main.hkx", "Data"},
      {"meshes/actors/character/characters/femalecharacter.hkx", "Data"},
      {"meshes/actors/wolf/animations/howl.hkx", "Data"}};
  for (const auto& [path, label] : rows) {
    auto entry = fo4::hkx::classify_animation_path(path);
    entry.winner.label = label;
    index.entries.push_back(std::move(entry));
  }
  const auto matched = [&](AnimationIndexFilter filter) { return fo4::hkx::filter_animation_index(index, filter); };
  using Hits = std::vector<std::size_t>;

  AnimationIndexFilter by_kind;
  by_kind.kind = AnimationKind::animation;
  EXPECT_EQ(matched(by_kind), (Hits{0, 3}));

  AnimationIndexFilter by_project;
  by_project.project = "CHARACTER";
  EXPECT_EQ(matched(by_project), (Hits{0, 1, 2}));

  AnimationIndexFilter by_race;
  by_race.race = "Wolf";
  EXPECT_EQ(matched(by_race), (Hits{3}));

  AnimationIndexFilter by_character;
  by_character.character = "FemaleCharacter";
  EXPECT_EQ(matched(by_character), (Hits{2}));

  AnimationIndexFilter by_provider;
  by_provider.query = "high";
  EXPECT_EQ(matched(by_provider), (Hits{0}));

  AnimationIndexFilter by_path;
  by_path.query = "BEHAVIORS";
  EXPECT_EQ(matched(by_path), (Hits{1}));

  AnimationIndexFilter combined;
  combined.kind = AnimationKind::animation;
  combined.project = "character";
  EXPECT_EQ(matched(combined), (Hits{0}));

  const auto facets = fo4::hkx::animation_index_facets(index);
  EXPECT_EQ(facets.projects, (std::vector<std::string>{"character", "wolf"}));
  EXPECT_EQ(facets.races, (std::vector<std::string>{"character", "wolf"}));
  EXPECT_EQ(facets.characters, (std::vector<std::string>{"femalecharacter"}));
}

// Detects: a plugin's archives opened in the wrong order or an unplaced archive beating a placed one, so this must hold.
TEST(AnimationIndex, PluginArchivesBeatUnplacedOnesAndKeepArchiveLoadOrderWithinAPlugin) {
  Mo2Tree tree("");
  put_file(tree.dir.path() / "profiles" / "Test" / "plugins.txt", "*Mod.esp\n");
  put_ba2(tree.data() / "Aaa.ba2", {std::string(k_walk)});
  put_ba2(tree.data() / "Mod - Main.ba2", {std::string(k_walk)});
  put_ba2(tree.data() / "Mod.ba2", {std::string(k_walk)});

  const auto sources = open_mo2(tree);
  const auto index = fo4::hkx::build_animation_index(sources.resolver);

  const auto* walk = find_entry(index, k_walk);
  ASSERT_NE(walk, nullptr);
  EXPECT_EQ(walk->winner.label, "Mod - Main.ba2");
  EXPECT_EQ(labels_of(walk->shadowed), (std::vector<std::string>{"Mod.ba2", "Aaa.ba2"}));
}

std::string text_of(const std::vector<std::byte>& bytes) {
  return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

// Constraint: detects a BA2 winner that cannot be extracted, a loose mod winner replaced by an archive copy, or a lookup that serves other file types or paths outside the data tree.
TEST(AnimationIndex, ReadAnimationAssetExtractsArchiveAndLooseWinnersAndRefusesOtherTypesAndUnsafePaths) {
  Mo2Tree tree("+High\n+Low\n-Off\n+Tools_separator\n");
  layer_walk(tree);
  put_ba2(tree.data() / "Bodies.ba2", {"meshes/actors/wolf/body.nif", "meshes/actors/wolf/notes.txt"});

  const auto sources = open_mo2(tree);

  const auto loose = fo4::hkx::read_animation_asset(sources.resolver, "Meshes\\Actors\\Character\\Animations\\Walk.hkx");
  ASSERT_TRUE(loose.has_value());
  EXPECT_EQ(loose->path, k_walk);
  EXPECT_EQ(loose->provider.label, "overwrite");
  EXPECT_FALSE(loose->provider.archive);
  EXPECT_TRUE(loose->bytes.empty());
  EXPECT_EQ(loose->size, 10u);
  EXPECT_TRUE(std::filesystem::equivalent(loose->loose_path, tree.dir.path() / "overwrite" / k_walk));

  const auto packed = fo4::hkx::read_animation_asset(sources.resolver, "meshes/actors/wolf/animations/howl.hkx");
  ASSERT_TRUE(packed.has_value());
  EXPECT_EQ(packed->provider.label, "Base.ba2");
  EXPECT_TRUE(packed->provider.archive);
  EXPECT_EQ(text_of(packed->bytes), "ba2");
  EXPECT_EQ(packed->size, 3u);
  EXPECT_TRUE(packed->loose_path.empty());

  const auto body = fo4::hkx::read_animation_asset(sources.resolver, "meshes/actors/wolf/body.nif");
  ASSERT_TRUE(body.has_value());
  EXPECT_EQ(body->provider.label, "Bodies.ba2");

  EXPECT_FALSE(fo4::hkx::read_animation_asset(sources.resolver, "meshes/actors/wolf/missing.hkx").has_value());
  EXPECT_THROW(fo4::hkx::read_animation_asset(sources.resolver, "meshes/actors/wolf/notes.txt"),
               fo4::archive::AssetPathError);
  EXPECT_THROW(fo4::hkx::read_animation_asset(sources.resolver, "meshes/../../outside.hkx"),
               fo4::archive::AssetPathError);
  EXPECT_THROW(fo4::hkx::read_animation_asset(sources.resolver, "C:/Windows/system.nif"),
               fo4::archive::AssetPathError);
}

// Constraint: detects an extraction that serves a different archive than the one the index names when two BA2s hold the same inner path.
TEST(AnimationIndex, ReadAnimationAssetServesTheArchiveTheIndexNamesWhenTwoHoldTheSamePath) {
  Mo2Tree tree("");
  put_ba2(tree.data() / "Aaa.ba2", {std::string(k_walk)});
  put_ba2(tree.data() / "Zzz.ba2", {std::string(k_walk)});

  const auto sources = open_mo2(tree);
  const auto index = fo4::hkx::build_animation_index(sources.resolver);
  const auto* walk = find_entry(index, k_walk);
  ASSERT_NE(walk, nullptr);
  ASSERT_EQ(walk->shadowed.size(), 1u);

  const auto asset = fo4::hkx::read_animation_asset(sources.resolver, k_walk);
  ASSERT_TRUE(asset.has_value());
  EXPECT_TRUE(asset->provider.archive);
  EXPECT_EQ(asset->provider.label, walk->winner.label);
  EXPECT_NE(asset->provider.label, walk->shadowed.front().label);
}

}  // namespace
