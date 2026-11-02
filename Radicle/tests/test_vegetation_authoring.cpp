#include "fo4/c_api_authoring.h"
#include "fo4/c_api_vegetation.h"
#include "fo4/environment/vegetation.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/subrecord_authoring.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/form_links.hpp"
#include "fo4/schema/preflight.hpp"
#include "vegetation_record_fixture.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace fo4::test::vegetation;

fo4::FormLinkFieldMutationStatus link(fo4::Plugin& plugin, std::uint32_t id, const char* path, std::size_t index, const fo4::FormKey& target) {
  const auto field = fo4::schema::formlink_field_descriptor(path);
  EXPECT_TRUE(field.has_value()) << path;
  return fo4::set_record_formlink_field(plugin, key(id), *field, index, target);
}

bool refused(fo4::Plugin& plugin, std::uint32_t id, const char* path, std::size_t index, const fo4::FormKey& target) {
  const auto before = fo4::write_plugin(plugin);
  bool threw = false;
  try {
    link(plugin, id, path, index, target);
  } catch (const std::exception&) {
    threw = true;
  }
  return threw && fo4::write_plugin(plugin) == before;
}

bool authoring_clean(const fo4::Plugin& plugin, const fo4::FormKey& record) {
  for (const auto& issue : fo4::schema::preflight_authoring_record(plugin, record)) {
    if (issue.severity == fo4::schema::PreflightSeverity::error) { return false; }
  }
  return true;
}

class VegetationAuthoring : public testing::Test {
protected:
  void SetUp() override {
    root = std::filesystem::temp_directory_path() /
           ("radicle_vegetation_authoring_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
  }
  void TearDown() override { std::error_code error; std::filesystem::remove_all(root, error); }

  std::filesystem::path save(const Bytes& bytes, const char* name = k_plugin) {
    const auto path = root / name;
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return path;
  }

  std::filesystem::path root;
};

}  // namespace

TEST_F(VegetationAuthoring, NativeLinksWriteTypedTargetsAndKeepEverythingElse) {
  auto plugin = read(build(fixture(), true));
  const auto before = fo4::write_plugin(plugin);

  EXPECT_EQ(link(plugin, k_tree, "TREE\\Ingredient", 0, master(k_master_ingredient)), fo4::FormLinkFieldMutationStatus::subrecord_missing);
  EXPECT_EQ(link(plugin, k_flor, "FLOR\\Ingredient", 0, master(k_master_ingredient)), fo4::FormLinkFieldMutationStatus::unchanged);
  EXPECT_EQ(link(plugin, k_flor, "FLOR\\Ingredient", 0, key(k_misc)), fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(payloads(plugin, key(k_flor), "PFIG").at(0), words({k_master_local | k_misc}));
  EXPECT_EQ(link(plugin, k_flor, "FLOR\\Ingredient", 0, fo4::FormKey{}), fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(payloads(plugin, key(k_flor), "PFIG").at(0), words({0}));
  EXPECT_EQ(link(plugin, k_flor, "FLOR\\HarvestSound", 0, key(k_sndr)), fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(link(plugin, k_flor, "FLOR\\HarvestSound", 1, key(k_sndr)), fo4::FormLinkFieldMutationStatus::subrecord_missing);
  EXPECT_EQ(link(plugin, k_tree_bare, "TREE\\HarvestSound", 0, key(k_sndr)), fo4::FormLinkFieldMutationStatus::subrecord_missing);
  EXPECT_EQ(link(plugin, k_flor, "FLOR\\HarvestSound", 0, key(k_sndr)), fo4::FormLinkFieldMutationStatus::unchanged);

  EXPECT_TRUE(refused(plugin, k_flor, "FLOR\\Ingredient", 0, key(k_stat)));
  EXPECT_TRUE(refused(plugin, k_flor, "FLOR\\Ingredient", 0, key(k_sndr)));
  EXPECT_TRUE(refused(plugin, k_flor, "FLOR\\HarvestSound", 0, fo4::FormKey{}));
  EXPECT_TRUE(refused(plugin, k_flor, "FLOR\\HarvestSound", 0, key(k_misc)));
  EXPECT_TRUE(refused(plugin, k_flor, "FLOR\\HarvestSound", 0, fo4::FormKey{fo4::PluginKey::from_filename("Other.esm"), fo4::LocalFormId{0x900}}));
  EXPECT_TRUE(refused(plugin, k_flor, "FLOR\\Ingredient", 0, fo4::FormKey{fo4::PluginKey::from_filename("Other.esm"), fo4::LocalFormId{0x900}}));

  const auto tree_before = fo4::write_plugin(plugin);
  EXPECT_EQ(link(plugin, k_tree, "TREE\\Ingredient", 0, master(0x905)), fo4::FormLinkFieldMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::write_plugin(plugin), tree_before);
  EXPECT_NE(fo4::write_plugin(plugin), before);
  const auto reloaded = reload(plugin);
  EXPECT_EQ(payloads(reloaded, key(k_flor), "SNAM").at(0), words({k_master_local | k_sndr}));
  EXPECT_EQ(payloads(reloaded, key(k_flor), "KWDA").at(0), words({k_master_ingredient}));
  EXPECT_EQ(order(reloaded, k_flor), order(read(build(fixture(), true)), k_flor));
}

TEST_F(VegetationAuthoring, TreeLinksAcceptBaseObjectsAndNull) {
  auto plugin = read(build(fixture(), true));
  ASSERT_EQ(fo4::add_record_subrecord(plugin, key(k_tree_bare), sig("PFIG")), fo4::SubrecordAddStatus::added);
  ASSERT_EQ(fo4::add_record_subrecord(plugin, key(k_tree_bare), sig("SNAM")), fo4::SubrecordAddStatus::added);
  EXPECT_EQ(order(plugin, k_tree_bare), (Order{"EDID", "OBND", "MODL", "PFIG", "SNAM", "CNAM"}));
  EXPECT_EQ(payloads(plugin, key(k_tree_bare), "PFIG").at(0), words({0}));
  EXPECT_EQ(link(plugin, k_tree_bare, "TREE\\Ingredient", 0, key(k_stat)), fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(link(plugin, k_tree_bare, "TREE\\Ingredient", 0, key(k_misc)), fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(link(plugin, k_tree_bare, "TREE\\Ingredient", 0, fo4::FormKey{}), fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_TRUE(refused(plugin, k_tree_bare, "TREE\\Ingredient", 0, key(k_sndr)));
  EXPECT_TRUE(refused(plugin, k_tree_bare, "TREE\\Ingredient", 0, key(k_gras)));
  EXPECT_EQ(link(plugin, k_tree_bare, "TREE\\HarvestSound", 0, key(k_sndr)), fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(link(plugin, k_tree_bare, "TREE\\HarvestSound", 0, fo4::FormKey{}), fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_TRUE(refused(plugin, k_tree_bare, "TREE\\HarvestSound", 0, key(k_misc)));
  EXPECT_EQ(fo4::remove_record_subrecord(plugin, key(k_tree_bare), sig("PFIG"), 0), fo4::SubrecordRemoveStatus::removed);
  EXPECT_EQ(order(plugin, k_tree_bare), (Order{"EDID", "OBND", "MODL", "SNAM", "CNAM"}));
}

TEST_F(VegetationAuthoring, MalformedLinkPayloadsRefuseWithoutChangingBytes) {
  struct Case { std::uint32_t record; const char* path; const char* subrecord; std::uint32_t target; };
  const Case cases[] = {
      {k_tree, "TREE\\Ingredient", "PFIG", k_misc},
      {k_tree, "TREE\\HarvestSound", "SNAM", k_sndr},
      {k_flor, "FLOR\\Ingredient", "PFIG", k_misc},
      {k_flor, "FLOR\\HarvestSound", "SNAM", k_sndr},
      {k_ltex, "LTEX\\Grasses", "GNAM", k_gras_no_data},
  };
  for (const auto& item : cases) {
    for (const auto size : {0u, 3u, 5u, 8u}) {
      SCOPED_TRACE(std::string(item.path) + " size=" + std::to_string(size));
      auto entries = fixture(false);
      auto& record = *std::find_if(entries.begin(), entries.end(), [&](const auto& entry) { return entry.id == item.record; });
      std::erase_if(record.subs, [&](const auto& sub) { return sub.first == item.subrecord; });
      record.subs.emplace_back(item.subrecord, filled(size, 0));
      const auto original = build(entries, false);
      auto plugin = read(original);
      if (item.record == k_ltex) {
        const auto list = fo4::schema::formlink_list_descriptor(sig("LTEX"), item.path);
        ASSERT_TRUE(list.has_value());
        EXPECT_THROW(fo4::schema::set_record_formlink_list_element(plugin, key(item.record), *list, 0, key(item.target)), fo4::schema::FormLinkListError);
      } else {
        EXPECT_THROW(link(plugin, item.record, item.path, 0, key(item.target)), fo4::schema::FormLinkFieldError);
      }
      EXPECT_EQ(fo4::write_plugin(plugin), original);

      const auto path = save(original).string();
      radicle_plugin* handle = nullptr;
      ASSERT_EQ(radicle_plugin_read_file(path.c_str(), &handle), RADICLE_STATUS_OK);
      const radicle_form_key target{k_plugin, item.target};
      const auto status = item.record == k_ltex
          ? radicle_plugin_formlink_list_set(handle, {k_plugin, item.record}, item.path, 0, &target)
          : radicle_plugin_set_record_formlink(handle, {k_plugin, item.record}, item.path, 0, &target);
      EXPECT_EQ(status, RADICLE_STATUS_INVALID_ARGUMENT);
      const auto saved = (root / "unchanged.esp").string();
      EXPECT_EQ(radicle_plugin_write_file(handle, saved.c_str()), RADICLE_STATUS_OK);
      radicle_plugin_destroy(handle);
      EXPECT_EQ(fo4::write_plugin(fo4::read_plugin_file(saved)), original);
    }
  }
}

TEST_F(VegetationAuthoring, LandscapeGrassesAreAnUncountedList) {
  auto plugin = read(build(fixture(), true));
  const auto list = fo4::schema::formlink_list_descriptor(sig("LTEX"), "LTEX\\Grasses");
  ASSERT_TRUE(list.has_value());
  const auto count = [&](std::uint32_t id) { return fo4::schema::record_formlink_list_size(plugin, key(id), *list); };
  EXPECT_EQ(count(k_ltex), std::optional<std::size_t>{2});
  EXPECT_EQ(count(k_ltex_bare), std::optional<std::size_t>{0});
  EXPECT_EQ(count(0x999), std::nullopt);

  const auto second_grass = key(k_gras_no_data);
  EXPECT_EQ(fo4::schema::set_record_formlink_list_element(plugin, key(k_ltex), *list, 1, second_grass), fo4::schema::FormLinkListMutationStatus::updated);
  const auto settled = fo4::write_plugin(plugin);
  EXPECT_EQ(fo4::schema::set_record_formlink_list_element(plugin, key(k_ltex), *list, 0, key(k_gras)), fo4::schema::FormLinkListMutationStatus::unchanged);
  EXPECT_EQ(fo4::schema::set_record_formlink_list_element(plugin, key(k_ltex), *list, 2, key(k_gras)), fo4::schema::FormLinkListMutationStatus::index_out_of_range);
  EXPECT_THROW(fo4::schema::set_record_formlink_list_element(plugin, key(k_ltex), *list, 0, fo4::FormKey{}), fo4::schema::FormLinkListError);
  EXPECT_EQ(fo4::schema::set_record_formlink_list_element(plugin, key(k_ltex), *list, 0, key(k_stat)), fo4::schema::FormLinkListMutationStatus::target_type_mismatch);
  EXPECT_EQ(fo4::write_plugin(plugin), settled);
  EXPECT_EQ(payloads(plugin, key(k_ltex), "GNAM"), (std::vector<Bytes>{words({k_master_local | k_gras}), words({k_master_local | k_gras_no_data})}));

  EXPECT_EQ(fo4::schema::record_formlink_list_size(plugin, key(k_ltex), *list), std::optional<std::size_t>{2});
  EXPECT_EQ(fo4::schema::insert_record_formlink_list_element(plugin, key(k_ltex), *list, 2, key(k_gras)), fo4::schema::FormLinkListMutationStatus::updated);
  EXPECT_EQ(fo4::schema::insert_record_formlink_list_element(plugin, key(k_ltex), *list, 1, key(k_stat)), fo4::schema::FormLinkListMutationStatus::target_type_mismatch);
  EXPECT_EQ(order(plugin, k_ltex), (Order{"EDID", "TNAM", "MNAM", "HNAM", "SNAM", "GNAM", "GNAM", "GNAM"}));
  EXPECT_EQ(count(k_ltex), std::optional<std::size_t>{3});
  EXPECT_EQ(fo4::remove_record_subrecord(plugin, key(k_ltex), sig("GNAM"), 1), fo4::SubrecordRemoveStatus::removed);
  EXPECT_EQ(payloads(plugin, key(k_ltex), "GNAM"), (std::vector<Bytes>{words({k_master_local | k_gras}), words({k_master_local | k_gras})}));
  EXPECT_EQ(fo4::remove_record_subrecord(plugin, key(k_ltex), sig("GNAM"), 5), fo4::SubrecordRemoveStatus::absent);
  EXPECT_EQ(fo4::add_record_subrecord(plugin, key(k_ltex), sig("GNAM")), fo4::SubrecordAddStatus::no_default);

  EXPECT_EQ(fo4::schema::insert_record_formlink_list_element(plugin, key(k_ltex_bare), *list, 0, key(k_gras)), fo4::schema::FormLinkListMutationStatus::updated);
  EXPECT_EQ(order(plugin, k_ltex_bare), (Order{"EDID", "MNAM", "HNAM", "SNAM", "GNAM"}));
  EXPECT_EQ(fo4::remove_record_subrecord(plugin, key(k_ltex_bare), sig("GNAM"), 0), fo4::SubrecordRemoveStatus::removed);
  EXPECT_EQ(order(plugin, k_ltex_bare), (Order{"EDID", "MNAM", "HNAM", "SNAM"}));
  EXPECT_TRUE(authoring_clean(plugin, key(k_ltex_bare)));
  EXPECT_TRUE(authoring_clean(reload(plugin), key(k_ltex)));
}

TEST_F(VegetationAuthoring, OptionalSubrecordsAddAtTheirSchemaPositionAndRequiredOnesStay) {
  auto plugin = read(build(fixture(), true));
  EXPECT_EQ(fo4::add_record_subrecord(plugin, key(k_tree_bare), sig("PFPC")), fo4::SubrecordAddStatus::added);
  EXPECT_EQ(order(plugin, k_tree_bare), (Order{"EDID", "OBND", "MODL", "PFPC", "CNAM"}));
  EXPECT_EQ(payloads(plugin, key(k_tree_bare), "PFPC").at(0), filled(4, 0));
  EXPECT_EQ(fo4::add_record_subrecord(plugin, key(k_tree_bare), sig("PFPC")), fo4::SubrecordAddStatus::already_present);
  EXPECT_EQ(fo4::remove_record_subrecord(plugin, key(k_tree), sig("PFPC"), 0), fo4::SubrecordRemoveStatus::removed);
  EXPECT_EQ(order(plugin, k_tree), (Order{"EDID", "OBND", "MODL", "MODT", "CNAM"}));
  EXPECT_EQ(fo4::remove_record_subrecord(plugin, key(k_tree), sig("CNAM"), 0), fo4::SubrecordRemoveStatus::below_minimum);
  EXPECT_EQ(fo4::remove_record_subrecord(plugin, key(k_tree), sig("OBND"), 0), fo4::SubrecordRemoveStatus::below_minimum);

  EXPECT_EQ(fo4::remove_record_subrecord(plugin, key(k_flor), sig("PFPC"), 0), fo4::SubrecordRemoveStatus::below_minimum);
  EXPECT_EQ(fo4::remove_record_subrecord(plugin, key(k_flor), sig("FULL"), 0), fo4::SubrecordRemoveStatus::below_minimum);
  for (const char* coupled : {"KSIZ", "KWDA", "MODL", "DEST"}) {
    EXPECT_EQ(fo4::remove_record_subrecord(plugin, key(k_flor), sig(coupled), 0), fo4::SubrecordRemoveStatus::not_authorable) << coupled;
    EXPECT_EQ(fo4::add_record_subrecord(plugin, key(k_flor), sig(coupled)), fo4::SubrecordAddStatus::not_authorable) << coupled;
  }
  EXPECT_EQ(fo4::remove_record_subrecord(plugin, key(k_flor), sig("PNAM"), 0), fo4::SubrecordRemoveStatus::removed);
  EXPECT_EQ(fo4::remove_record_subrecord(plugin, key(k_flor), sig("SNAM"), 0), fo4::SubrecordRemoveStatus::removed);
  EXPECT_EQ(fo4::add_record_subrecord(plugin, key(k_flor), sig("SNAM")), fo4::SubrecordAddStatus::no_default);
  EXPECT_EQ(fo4::remove_record_subrecord(plugin, key(k_flor), sig("PFIG"), 0), fo4::SubrecordRemoveStatus::removed);
  EXPECT_EQ(fo4::add_record_subrecord(plugin, key(k_flor), sig("PFIG")), fo4::SubrecordAddStatus::added);
  EXPECT_EQ(order(plugin, k_flor), (Order{"EDID", "OBND", "FULL", "MODL", "KSIZ", "KWDA", "FNAM", "PFIG", "PFPC"}));
  EXPECT_EQ(fo4::add_record_subrecord(plugin, key(k_flor), sig("PRPS")), fo4::SubrecordAddStatus::no_default);
  EXPECT_EQ(fo4::add_record_subrecord(plugin, key(k_flor), sig("MODC")), fo4::SubrecordAddStatus::no_default);

  EXPECT_EQ(fo4::add_record_subrecord(plugin, key(k_gras_no_data), sig("DATA")), fo4::SubrecordAddStatus::added);
  EXPECT_EQ(order(plugin, k_gras_no_data), (Order{"EDID", "OBND", "MODL", "DATA"}));
  EXPECT_EQ(payloads(plugin, key(k_gras_no_data), "DATA").at(0), gras_data());
  EXPECT_EQ(fo4::remove_record_subrecord(plugin, key(k_gras_no_data), sig("DATA"), 0), fo4::SubrecordRemoveStatus::below_minimum);
  EXPECT_EQ(fo4::remove_record_subrecord(plugin, key(k_gras), sig("MODT"), 0), fo4::SubrecordRemoveStatus::removed);
  EXPECT_EQ(fo4::remove_record_subrecord(plugin, key(k_ltex), sig("TNAM"), 0), fo4::SubrecordRemoveStatus::removed);
  EXPECT_EQ(fo4::remove_record_subrecord(plugin, key(k_ltex), sig("SNAM"), 0), fo4::SubrecordRemoveStatus::below_minimum);
  EXPECT_EQ(fo4::add_record_subrecord(plugin, key(k_ltex), sig("TNAM")), fo4::SubrecordAddStatus::no_default);
  EXPECT_TRUE(authoring_clean(reload(plugin), key(k_gras_no_data)));
}

TEST_F(VegetationAuthoring, CreatedRecordsCarryProvenDefaultsAndDecodeAfterReload) {
  auto plugin = read(build({}, false));
  const auto gras = fo4::create_record(plugin, sig("GRAS"), "NewGrass");
  const auto tree = fo4::create_record(plugin, sig("TREE"), "NewTree");
  const auto flor = fo4::create_record(plugin, sig("FLOR"), "NewFlora");
  const auto ltex = fo4::create_record(plugin, sig("LTEX"), "NewLand");
  for (const auto& result : {gras, tree, flor, ltex}) { ASSERT_EQ(result.status, fo4::RecordCreateStatus::created); }
  EXPECT_EQ(order(plugin, gras.key.local().value()), (Order{"EDID", "OBND", "DATA"}));
  EXPECT_EQ(order(plugin, tree.key.local().value()), (Order{"EDID", "OBND", "CNAM"}));
  EXPECT_EQ(order(plugin, flor.key.local().value()), (Order{"EDID", "OBND", "FULL", "PFPC"}));
  EXPECT_EQ(order(plugin, ltex.key.local().value()), (Order{"EDID", "MNAM", "HNAM", "SNAM"}));
  for (const auto& result : {gras, tree, flor, ltex}) { EXPECT_TRUE(authoring_clean(plugin, result.key)); }
  EXPECT_EQ(payloads(plugin, ltex.key, "HNAM").at(0), hex("1e1e"));
  EXPECT_EQ(payloads(plugin, ltex.key, "SNAM").at(0), hex("1e"));
  EXPECT_EQ(payloads(plugin, flor.key, "FULL").at(0), zstr(""));

  const auto path = save(fo4::write_plugin(plugin));
  const std::filesystem::path paths[] = {path};
  const auto env = fo4::env::Environment::load(paths);
  const auto grass = fo4::env::decode_grass(env, *env.winner(gras.key));
  ASSERT_TRUE(grass.has_value());
  EXPECT_EQ(grass->density, 30);
  EXPECT_EQ(grass->min_slope, 0);
  EXPECT_EQ(grass->max_slope, 90);
  EXPECT_EQ(grass->units_from_water, 0);
  EXPECT_EQ(grass->water_type, 0u);
  EXPECT_FLOAT_EQ(grass->position_range, 32.0f);
  EXPECT_FLOAT_EQ(grass->height_range, 0.2f);
  EXPECT_FLOAT_EQ(grass->color_range, 0.5f);
  EXPECT_FLOAT_EQ(grass->wave_period, 10.0f);
  EXPECT_EQ(grass->flags, 0);
  const auto tree_info = fo4::env::decode_tree(env, *env.winner(tree.key));
  ASSERT_TRUE(tree_info.wind.has_value());
  for (std::size_t i = 0; i < 12; ++i) {
    EXPECT_FLOAT_EQ((*tree_info.wind)[i], (i < 2 || i > 9) ? 1.0f : 0.0f) << i;
  }
  EXPECT_FALSE(tree_info.harvest.ingredient.has_value());
  const auto flora = fo4::env::decode_flora(env, *env.winner(flor.key));
  ASSERT_TRUE(flora.harvest.seasons.has_value());
  EXPECT_EQ(*flora.harvest.seasons, (std::array<std::uint8_t, 4>{0, 0, 0, 0}));
  EXPECT_FALSE(flora.flags.has_value());
  EXPECT_TRUE(fo4::env::decode_landscape_grasses(env, *env.winner(ltex.key)).empty());

  EXPECT_EQ(fo4::add_record_subrecord(plugin, flor.key, sig("FNAM")), fo4::SubrecordAddStatus::added);
  EXPECT_EQ(order(plugin, flor.key.local().value()), (Order{"EDID", "OBND", "FULL", "FNAM", "PFPC"}));
  EXPECT_EQ(fo4::set_record_scalar(plugin, flor.key, *fo4::schema::scalar_field_descriptor("FLOR\\Flags"), "513"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(payloads(plugin, flor.key, "FNAM").at(0), hex("0102"));
  EXPECT_TRUE(authoring_clean(reload(plugin), flor.key));
}

TEST_F(VegetationAuthoring, FloraCannotBeCreatedInALocalizedPlugin) {
  auto plugin = read(build({}, false, true));
  EXPECT_EQ(fo4::create_record(plugin, sig("FLOR"), "NewFlora").status, fo4::RecordCreateStatus::no_default);
  const auto before = fo4::write_plugin(plugin);
  EXPECT_EQ(fo4::create_record(plugin, sig("GRAS"), "G").status, fo4::RecordCreateStatus::created);
  EXPECT_EQ(fo4::create_record(plugin, sig("TREE"), "T").status, fo4::RecordCreateStatus::created);
  EXPECT_EQ(fo4::create_record(plugin, sig("LTEX"), "L").status, fo4::RecordCreateStatus::created);
  EXPECT_NE(fo4::write_plugin(plugin), before);
}

TEST_F(VegetationAuthoring, LinkSurfacesAreVisibleToTheRemapAndCheckScan) {
  auto plugin = read(build(fixture(), true));
  std::vector<std::string> seen;
  for (const auto& hit : fo4::collect_form_links(plugin)) {
    const auto record = hit.record.local().value();
    if (record == k_tree || record == k_flor || record == k_ltex) {
      seen.push_back(std::to_string(record) + hit.subrecord.to_string());
    }
  }
  for (const char* expected : {"PFIG", "SNAM"}) {
    EXPECT_NE(std::find(seen.begin(), seen.end(), std::to_string(k_flor) + expected), seen.end()) << expected;
  }
  EXPECT_EQ(std::count(seen.begin(), seen.end(), std::to_string(k_ltex) + "GNAM"), 2);
  for (const char* expected : {"MNAM", "TNAM"}) {
    EXPECT_EQ(std::count(seen.begin(), seen.end(), std::to_string(k_ltex) + expected), 1) << expected;
  }
}

TEST_F(VegetationAuthoring, CAbiMapsLinkAuthoringAndCreateStatusesAndSurvivesReload) {
  const auto path = save(build(fixture(false), false));
  const auto name = path.string();
  radicle_plugin* plugin = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(name.c_str(), &plugin), RADICLE_STATUS_OK);
  const auto rkey = [](std::uint32_t id) { return radicle_form_key{k_plugin, id & 0xFFFFFF}; };
  const radicle_form_key sound{k_plugin, k_sndr}, misc{k_plugin, k_misc}, stat{k_plugin, k_stat}, grass{k_plugin, k_gras};
  const radicle_form_key unknown_master{"Other.esm", 1};

  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin, rkey(k_flor), "FLOR\\HarvestSound", 0, &sound), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin, rkey(k_flor), "FLOR\\HarvestSound", 0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin, rkey(k_flor), "FLOR\\HarvestSound", 0, &misc), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin, rkey(k_flor), "FLOR\\HarvestSound", 0, &unknown_master), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin, rkey(k_flor), "FLOR\\Ingredient", 0, &misc), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin, rkey(k_flor), "FLOR\\Ingredient", 0, &stat), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin, rkey(k_flor), "FLOR\\Ingredient", 0, nullptr), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin, rkey(k_tree), "TREE\\HarvestSound", 0, &sound), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin, rkey(k_flor), "TREE\\HarvestSound", 0, &sound), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_add_subrecord(plugin, rkey(k_tree), "SNAM"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_add_subrecord(plugin, rkey(k_tree), "SNAM"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin, rkey(k_tree), "TREE\\HarvestSound", 0, &sound), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin, rkey(k_tree), "TREE\\HarvestSound", 0, nullptr), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_add_subrecord(plugin, rkey(k_tree), "KSIZ"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_remove_subrecord(plugin, rkey(k_flor), "KWDA", 0), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_remove_subrecord(plugin, rkey(k_flor), "PFPC", 0), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_remove_subrecord(plugin, rkey(k_tree), "PFPC", 0), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_remove_subrecord(plugin, rkey(k_tree), "PFPC", 0), RADICLE_STATUS_NOT_FOUND);

  size_t count = 0;
  EXPECT_EQ(radicle_plugin_formlink_list_size(plugin, rkey(k_ltex), "LTEX\\Grasses", &count), RADICLE_STATUS_OK);
  EXPECT_EQ(count, 2u);
  EXPECT_EQ(radicle_plugin_formlink_list_size(plugin, rkey(k_ltex_bare), "LTEX\\Grasses", &count), RADICLE_STATUS_OK);
  EXPECT_EQ(count, 0u);
  count = 99;
  EXPECT_EQ(radicle_plugin_formlink_list_size(plugin, rkey(0x999), "LTEX\\Grasses", &count), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(count, 0u);
  EXPECT_EQ(radicle_plugin_formlink_list_set(plugin, rkey(k_ltex), "LTEX\\Grasses", 1, &grass), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_formlink_list_set(plugin, rkey(k_ltex), "LTEX\\Grasses", 1, &grass), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_formlink_list_set(plugin, rkey(k_ltex), "LTEX\\Grasses", 0, &stat), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_formlink_list_set(plugin, rkey(k_ltex), "LTEX\\Grasses", 2, &grass), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_formlink_list_set(plugin, rkey(k_ltex), "LTEX\\Grasses", 0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_formlink_list_insert(plugin, rkey(k_ltex), "LTEX\\Grasses", 2, &grass), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_formlink_list_insert(plugin, rkey(k_ltex), "LTEX\\Grasses", 0, &stat), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_formlink_list_size(plugin, rkey(k_ltex), "LTEX\\Grasses", &count), RADICLE_STATUS_OK);
  EXPECT_EQ(count, 3u);
  EXPECT_EQ(radicle_plugin_formlink_list_remove(plugin, rkey(k_ltex), "LTEX\\Grasses", 0), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_remove_subrecord(plugin, rkey(k_ltex), "GNAM", 0), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_formlink_list_size(plugin, rkey(k_ltex), "LTEX\\Grasses", &count), RADICLE_STATUS_OK);
  EXPECT_EQ(count, 1u);

  radicle_form_key created_gras{}, created_tree{}, created_flor{}, created_ltex{}, refused_key{};
  EXPECT_EQ(radicle_plugin_create_record(plugin, "GRAS", "CGrass", &created_gras), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_create_record(plugin, "TREE", "CTree", &created_tree), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_create_record(plugin, "FLOR", "CFlora", &created_flor), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_create_record(plugin, "LTEX", "CLand", &created_ltex), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_create_record(plugin, "BPTD", "CBody", &refused_key), RADICLE_STATUS_INVALID_ARGUMENT);
  ASSERT_EQ(radicle_plugin_set_record_scalar(plugin, created_gras, "GRAS\\Density", "44"), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_plugin_set_record_formlink(plugin, created_flor, "FLOR\\HarvestSound", 0, &sound), RADICLE_STATUS_NOT_FOUND);
  ASSERT_EQ(radicle_plugin_add_subrecord(plugin, created_flor, "PFIG"), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_plugin_set_record_formlink(plugin, created_flor, "FLOR\\Ingredient", 0, &misc), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_plugin_formlink_list_insert(plugin, created_ltex, "LTEX\\Grasses", 0, &grass), RADICLE_STATUS_OK);
  const std::uint32_t gras_id = created_gras.local_id, tree_id = created_tree.local_id, flor_id = created_flor.local_id, ltex_id = created_ltex.local_id;
  std::filesystem::create_directories(root / "out");
  const auto saved = (root / "out" / k_plugin).string();
  ASSERT_EQ(radicle_plugin_write_file(plugin, saved.c_str()), RADICLE_STATUS_OK);
  radicle_plugin_destroy(plugin);

  const char* paths[] = {saved.c_str()};
  radicle_env* env = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 1, &env), RADICLE_STATUS_OK) << radicle_last_error();
  const radicle_record* handle = nullptr;
  ASSERT_EQ(radicle_env_winner(env, rkey(gras_id), &handle), RADICLE_STATUS_OK);
  radicle_grass_info grass_info{};
  ASSERT_EQ(radicle_grass_get_info(env, handle, &grass_info), RADICLE_STATUS_OK);
  EXPECT_EQ(grass_info.density, 44);
  EXPECT_EQ(grass_info.max_slope, 90);
  ASSERT_EQ(radicle_env_winner(env, rkey(tree_id), &handle), RADICLE_STATUS_OK);
  radicle_tree_info tree_info{};
  ASSERT_EQ(radicle_tree_get_info(env, handle, &tree_info), RADICLE_STATUS_OK);
  EXPECT_EQ(tree_info.has_wind, 1);
  EXPECT_FLOAT_EQ(tree_info.wind[0], 1.0f);
  ASSERT_EQ(radicle_env_winner(env, rkey(flor_id), &handle), RADICLE_STATUS_OK);
  radicle_flora_info flora_info{};
  ASSERT_EQ(radicle_flora_get_info(env, handle, &flora_info), RADICLE_STATUS_OK);
  EXPECT_EQ(flora_info.harvest.has_seasons, 1);
  EXPECT_STREQ(flora_info.harvest.ingredient.plugin, k_plugin);
  EXPECT_EQ(flora_info.harvest.ingredient.local_id, k_misc);
  ASSERT_EQ(radicle_env_winner(env, rkey(ltex_id), &handle), RADICLE_STATUS_OK);
  const radicle_grass_links* links = nullptr;
  ASSERT_EQ(radicle_landscape_grasses_decode(env, handle, &links), RADICLE_STATUS_OK);
  EXPECT_EQ(links->grass_count, 1u);
  EXPECT_EQ(links->grasses[0].local_id, k_gras);
  radicle_landscape_grasses_free(links);
  ASSERT_EQ(radicle_env_winner(env, rkey(k_ltex), &handle), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_landscape_grasses_decode(env, handle, &links), RADICLE_STATUS_OK);
  EXPECT_EQ(links->grass_count, 1u);
  radicle_landscape_grasses_free(links);
  radicle_env_destroy(env);
}
