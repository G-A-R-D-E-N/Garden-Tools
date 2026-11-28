#include "fo4/c_api_authoring.h"
#include "fo4/c_api_placed_create.h"
#include "fo4/forms/placed_reference.hpp"
#include "fo4/plugin/reader.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;

constexpr float kPosition[3] = {1.0f, 2.0f, 3.0f};
constexpr float kRotation[3] = {0.0f, 0.0f, 0.5f};

Bytes written(radicle_plugin* handle) {
  std::uint8_t* bytes = nullptr;
  std::size_t size = 0;
  EXPECT_EQ(radicle_plugin_write_bytes(handle, &bytes, &size), RADICLE_STATUS_OK);
  Bytes copy(reinterpret_cast<std::byte*>(bytes), reinterpret_cast<std::byte*>(bytes) + size);
  radicle_free_bytes(bytes);
  return copy;
}

fo4::Plugin snapshot(radicle_plugin* handle) {
  return fo4::read_plugin(written(handle), "Placed.esp");
}

Bytes placed_fixture() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Fallout4.esm\0", 13});
  fo4::test::ByteWriter master_data;
  master_data.u32(0);
  master_data.u32(0);
  fo4::test::write_subrecord(out, "DATA", master_data.bytes);
  fo4::test::end_record(out, tes4);
  const auto cell_group = fo4::test::begin_group(out, "CELL", 0);
  const auto cell = fo4::test::begin_record(out, "CELL", 0, 0x01000800u);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Garden\0", 7});
  fo4::test::write_subrecord(out, "DATA", Bytes{std::byte{0x01}});
  fo4::test::end_record(out, cell);
  fo4::test::end_group(out, cell_group);
  return out.bytes;
}

struct Plugin {
  radicle_plugin* handle{nullptr};
  std::filesystem::path root;

  Plugin() {
    root = std::filesystem::temp_directory_path() /
           ("radicle_c_placed_" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    const auto path = root / "Placed.esp";
    const auto body = placed_fixture();
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(body.data()), static_cast<std::streamsize>(body.size()));
    stream.close();
    EXPECT_EQ(radicle_plugin_read_file(path.string().c_str(), &handle), RADICLE_STATUS_OK)
        << radicle_last_error();
  }
  ~Plugin() {
    radicle_plugin_destroy(handle);
    std::error_code error;
    std::filesystem::remove_all(root, error);
  }
  Plugin(const Plugin&) = delete;
  Plugin& operator=(const Plugin&) = delete;
};

}  // namespace

TEST(CApiPlacedCreate, CreatesAnActorInItsCellAndTakesAPatrolGroup) {
  Plugin plugin;
  const radicle_form_key cell{"Placed.esp", 0x800};
  const radicle_form_key base{"Fallout4.esm", 0xA00};

  radicle_form_key actor{};
  ASSERT_EQ(radicle_plugin_create_placed_actor(plugin.handle, cell, base, kPosition, kRotation, 1, 0,
                                               &actor),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_NE(actor.plugin, nullptr);
  EXPECT_STREQ(actor.plugin, "Placed.esp");

  const auto created = snapshot(plugin.handle);
  const auto views = fo4::forms::placed_references_for_cell(
      created, fo4::FormKey{fo4::PluginKey::from_filename("Placed.esp"), fo4::LocalFormId{0x800u}});
  ASSERT_EQ(views.size(), 1u);
  EXPECT_EQ(views[0].kind, fo4::forms::PlacedReferenceKind::npc);
  ASSERT_TRUE(views[0].base.has_value());
  EXPECT_EQ(*views[0].base, (fo4::FormKey{fo4::PluginKey::from_filename("Fallout4.esm"), fo4::LocalFormId{0xA00u}}));
  EXPECT_FLOAT_EQ(views[0].transform.x, 1.0f);
  EXPECT_FLOAT_EQ(views[0].transform.z, 3.0f);
  EXPECT_FLOAT_EQ(views[0].transform.rz, 0.5f);

  EXPECT_EQ(radicle_plugin_subrecord_group_insert(plugin.handle, actor, "ACHR\\Patrol", 0),
            RADICLE_STATUS_OK);
  std::vector<std::string> signatures;
  const auto current = snapshot(plugin.handle);
  for (const auto* record : fo4::collect_records(current)) {
    if (record->form_key.local().value() != actor.local_id) continue;
    for (const auto& subrecord : record->subrecords) signatures.push_back(subrecord.signature.to_string());
  }
  EXPECT_NE(std::find(signatures.begin(), signatures.end(), std::string("XPRD")), signatures.end());
  EXPECT_EQ(radicle_plugin_subrecord_group_insert(plugin.handle, actor, "ACHR\\Patrol", 2),
            RADICLE_STATUS_NOT_FOUND);
}

TEST(CApiPlacedCreate, MapsRefusalsToStatusesWithoutAddingAnActor) {
  Plugin plugin;
  const radicle_form_key cell{"Placed.esp", 0x800};
  const radicle_form_key base{"Fallout4.esm", 0xA00};
  const auto before = written(plugin.handle);
  radicle_form_key actor{};

  EXPECT_EQ(radicle_plugin_create_placed_actor(plugin.handle, {"Placed.esp", 0x8FF}, base, kPosition,
                                               kRotation, 1, 0, &actor),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_create_placed_actor(plugin.handle, cell, {"Other.esp", 0xA00}, kPosition,
                                               kRotation, 1, 0, &actor),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_create_placed_actor(nullptr, cell, base, kPosition, kRotation, 1, 0, &actor),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_create_placed_actor(plugin.handle, cell, base, nullptr, kRotation, 1, 0,
                                               &actor),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_create_placed_actor(plugin.handle, cell, base, kPosition, kRotation, 1, 0,
                                               nullptr),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(actor.plugin, nullptr);
  EXPECT_EQ(written(plugin.handle), before);
}

TEST(CApiPlacedCreate, RefusesNonFiniteTransformsWithoutAddingAnActor) {
  Plugin plugin;
  const radicle_form_key cell{"Placed.esp", 0x800};
  const radicle_form_key base{"Fallout4.esm", 0xA00};
  const auto before = written(plugin.handle);
  radicle_form_key actor{};
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float inf = std::numeric_limits<float>::infinity();
  const float nan_position[3] = {1.0f, nan, 3.0f};
  const float inf_rotation[3] = {inf, 0.0f, 0.5f};
  const float negative_inf_rotation[3] = {0.0f, 0.0f, -inf};

  EXPECT_EQ(radicle_plugin_create_placed_actor(plugin.handle, cell, base, nan_position, kRotation, 1,
                                               0, &actor),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_create_placed_actor(plugin.handle, cell, base, kPosition, inf_rotation, 1,
                                               0, &actor),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_create_placed_actor(plugin.handle, cell, base, kPosition,
                                               negative_inf_rotation, 1, 0, &actor),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(actor.plugin, nullptr);
  EXPECT_EQ(written(plugin.handle), before);
}