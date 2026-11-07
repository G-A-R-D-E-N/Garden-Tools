#include "fo4/c_api_vegetation.h"
#include "fo4/environment/vegetation.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <tuple>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using Sub = std::pair<std::string, Bytes>;
struct Rec { std::string signature; std::uint32_t id; std::vector<Sub> subs; };
struct Case { std::string path; std::uint32_t id; std::string value; std::function<void(const fo4::env::Environment&, const fo4::env::RecordRef&)> check; };

constexpr std::uint32_t k_gras = 0x800, k_tree = 0x801, k_flor = 0x802;
constexpr std::uint8_t k_sentinel = 0xA5;

Bytes grass_data() {
  fo4::test::ByteWriter b;
  b.u8(0xFE); b.u8(4); b.u8(90); b.u8(k_sentinel); b.u16(500); b.u8(k_sentinel); b.u8(k_sentinel); b.u32(3);
  b.f32(32); b.f32(0.2f); b.f32(0.5f); b.f32(10); b.u8(0x81);
  for (int i = 0; i < 7; ++i) b.u8(k_sentinel);
  return b.bytes;
}

Bytes tree_cnam() {
  fo4::test::ByteWriter b;
  for (int i = 1; i <= 12; ++i) b.f32(static_cast<float>(i));
  return b.bytes;
}

Bytes sized(std::size_t size, std::uint8_t fill = 1) { return Bytes(size, std::byte{fill}); }
Bytes seasons() { return {std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}}; }

std::vector<Rec> defaults() {
  const Sub unknown{"XUNK", sized(9, k_sentinel)};
  return {{"GRAS", k_gras, {{"EDID", {std::byte{'g'}, std::byte{0}}}, {"DATA", grass_data()}, unknown}},
          {"TREE", k_tree, {{"CNAM", tree_cnam()}, {"PFPC", seasons()}, unknown}},
          {"FLOR", k_flor, {{"FNAM", {std::byte{0x34}, std::byte{0x80}}}, {"PFPC", seasons()}, unknown}}};
}

Rec& rec(std::vector<Rec>& records, std::uint32_t id) {
  return *std::find_if(records.begin(), records.end(), [&](const Rec& r) { return r.id == id; });
}

Sub& sub(std::vector<Rec>& records, std::uint32_t id, const std::string& tag) {
  auto& subs = rec(records, id).subs;
  return *std::find_if(subs.begin(), subs.end(), [&](const Sub& s) { return s.first == tag; });
}

void drop(std::vector<Rec>& records, std::uint32_t id, const std::string& tag) {
  auto& subs = rec(records, id).subs;
  subs.erase(std::find_if(subs.begin(), subs.end(), [&](const Sub& s) { return s.first == tag; }));
}

void duplicate(std::vector<Rec>& records, std::uint32_t id, const std::string& tag) {
  rec(records, id).subs.push_back(sub(records, id, tag));
}

class VegetationEdit : public testing::Test {
protected:
  void SetUp() override {
    root = std::filesystem::temp_directory_path() /
           ("radicle_vegetation_edit_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    path = root / "Vegetation.esm";
  }
  void TearDown() override { std::error_code error; std::filesystem::remove_all(root, error); }

  void write(const std::vector<Rec>& records) {
    fo4::test::ByteWriter out;
    const auto h = fo4::test::begin_record(out, "TES4", 1, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1); hedr.u32(static_cast<std::uint32_t>(records.size())); hedr.u32(0x900);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, h);
    for (const auto& record : records) {
      const auto group = fo4::test::begin_group(out, record.signature, 0);
      const auto rec = fo4::test::begin_record(out, record.signature, 0, record.id, 131);
      for (const auto& [tag, bytes] : record.subs) fo4::test::write_subrecord(out, tag, bytes);
      fo4::test::end_record(out, rec); fo4::test::end_group(out, group);
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(out.bytes.data()), static_cast<std::streamsize>(out.bytes.size()));
  }

  fo4::env::Environment load() { const std::filesystem::path paths[] = {path}; return fo4::env::Environment::load(paths); }
  static fo4::FormKey key(std::uint32_t id) { return {fo4::PluginKey::from_filename("Vegetation.esm"), fo4::LocalFormId{id}}; }
  static const fo4::env::RecordRef& record(const fo4::env::Environment& env, std::uint32_t id) { return *env.winner(key(id)); }

  static std::vector<Case> cases() {
    std::vector<Case> out;
    const auto grass = [&](const char* name, const char* value, std::function<void(const fo4::env::GrassData&)> check) {
      out.push_back({std::string("GRAS\\") + name, k_gras, value, [check](const auto& env, const auto& ref) {
        const auto data = fo4::env::decode_grass(env, ref);
        ASSERT_TRUE(data);
        check(*data);
      }});
    };
    grass("Density", "17", [](const auto& g) { EXPECT_EQ(g.density, 17); });
    grass("MinSlope", "18", [](const auto& g) { EXPECT_EQ(g.min_slope, 18); });
    grass("MaxSlope", "19", [](const auto& g) { EXPECT_EQ(g.max_slope, 19); });
    grass("UnitsFromWater", "65535", [](const auto& g) { EXPECT_EQ(g.units_from_water, 65535); });
    grass("UnitsFromWaterType", "4294967295", [](const auto& g) { EXPECT_EQ(g.water_type, 4294967295u); });
    grass("PositionRange", "-1e10", [](const auto& g) { EXPECT_FLOAT_EQ(g.position_range, -1e10f); });
    grass("HeightRange", "3.25", [](const auto& g) { EXPECT_FLOAT_EQ(g.height_range, 3.25f); });
    grass("ColorRange", "0.75", [](const auto& g) { EXPECT_FLOAT_EQ(g.color_range, 0.75f); });
    grass("WavePeriod", "99.5", [](const auto& g) { EXPECT_FLOAT_EQ(g.wave_period, 99.5f); });
    grass("Flags", "255", [](const auto& g) { EXPECT_EQ(g.flags, 255); });
    const char* wind[] = {"TrunkFlexibility", "BranchFlexibility", "TrunkAmplitude", "FrontAmplitude", "BackAmplitude", "SideAmplitude",
                          "FrontFrequency", "BackFrequency", "SideFrequency", "LeafFlexibility", "LeafAmplitude", "LeafFrequency"};
    for (std::size_t i = 0; i < 12; ++i) {
      out.push_back({std::string("TREE\\") + wind[i], k_tree, std::to_string(100 + i), [i](const auto& env, const auto& ref) {
        const auto data = fo4::env::decode_tree(env, ref);
        ASSERT_TRUE(data.wind);
        for (std::size_t j = 0; j < 12; ++j) { EXPECT_FLOAT_EQ((*data.wind)[j], j == i ? static_cast<float>(100 + i) : static_cast<float>(j + 1)); }
      }});
    }
    const char* season[] = {"Spring", "Summer", "Fall", "Winter"};
    for (std::size_t i = 0; i < 4; ++i) {
      const auto check = [i](const std::array<std::uint8_t, 4>& values) {
        for (std::size_t j = 0; j < 4; ++j) { EXPECT_EQ(values[j], j == i ? 200 + i : j + 1); }
      };
      out.push_back({std::string("TREE\\") + season[i], k_tree, std::to_string(200 + i), [check](const auto& env, const auto& ref) {
        const auto data = fo4::env::decode_tree(env, ref);
        ASSERT_TRUE(data.harvest.seasons);
        check(*data.harvest.seasons);
      }});
      out.push_back({std::string("FLOR\\") + season[i], k_flor, std::to_string(200 + i), [check](const auto& env, const auto& ref) {
        const auto data = fo4::env::decode_flora(env, ref);
        ASSERT_TRUE(data.harvest.seasons);
        check(*data.harvest.seasons);
      }});
    }
    out.push_back({"FLOR\\Flags", k_flor, "65535", [](const auto& env, const auto& ref) {
      EXPECT_EQ(fo4::env::decode_flora(env, ref).flags, 0xFFFF);
    }});
    return out;
  }

  std::filesystem::path root, path;
};

const fo4::Record& stored(const fo4::Plugin& plugin, std::uint32_t id) {
  const auto* found = fo4::find_record(plugin, {fo4::PluginKey::from_filename("Vegetation.esm"), fo4::LocalFormId{id}});
  EXPECT_NE(found, nullptr);
  return *found;
}

TEST_F(VegetationEdit, EveryPathChangesOnlyItsFieldBytes) {
  const auto all = cases();
  ASSERT_EQ(all.size(), 10u + 12u + 8u + 1u);
  for (const auto& c : all) {
    SCOPED_TRACE(c.path);
    write(defaults());
    auto plugin = fo4::read_plugin_file(path);
    const auto before_bytes = fo4::write_plugin(plugin);
    const auto before = stored(plugin, c.id);
    const auto field = fo4::schema::scalar_field_descriptor(c.path);
    ASSERT_TRUE(field);
    const auto width = fo4::schema::scalar_storage_size(field->scalar.storage);
    ASSERT_EQ(fo4::set_record_scalar(plugin, key(c.id), *field, c.value), fo4::ScalarMutationStatus::updated);
    const auto& after = stored(plugin, c.id);
    ASSERT_EQ(after.subrecords.size(), before.subrecords.size());
    for (std::size_t i = 0; i < before.subrecords.size(); ++i) {
      ASSERT_EQ(after.subrecords[i].signature, before.subrecords[i].signature);
      ASSERT_EQ(after.subrecords[i].data.size(), before.subrecords[i].data.size());
      for (std::size_t b = 0; b < before.subrecords[i].data.size(); ++b) {
        const bool inside = before.subrecords[i].signature == field->scalar.subrecord &&
                            b >= field->scalar.offset && b < field->scalar.offset + width;
        if (!inside) { EXPECT_EQ(after.subrecords[i].data[b], before.subrecords[i].data[b]) << "byte " << b; }
      }
    }
    fo4::write_plugin_file(plugin, path);
    const auto after_bytes = fo4::write_plugin(fo4::read_plugin_file(path));
    ASSERT_EQ(after_bytes.size(), before_bytes.size());
    std::size_t changed = 0;
    for (std::size_t i = 0; i < before_bytes.size(); ++i) { changed += before_bytes[i] != after_bytes[i]; }
    EXPECT_GE(changed, 1u);
    EXPECT_LE(changed, width);
    const auto env = load();
    c.check(env, record(env, c.id));
  }
}

TEST_F(VegetationEdit, UnchangedValueLeavesTheWholeFileIdentical) {
  for (const auto& c : cases()) {
    SCOPED_TRACE(c.path);
    write(defaults());
    auto plugin = fo4::read_plugin_file(path);
    const auto before = fo4::write_plugin(plugin);
    const auto field = fo4::schema::scalar_field_descriptor(c.path);
    ASSERT_TRUE(field);
    const auto& data = std::find_if(stored(plugin, c.id).subrecords.begin(), stored(plugin, c.id).subrecords.end(),
        [&](const fo4::Subrecord& s) { return s.signature == field->scalar.subrecord; })->data;
    const auto current = fo4::schema::decode_scalar(fo4::schema::scalar_field_bytes(data, field->scalar), field->scalar);
    EXPECT_EQ(fo4::set_record_scalar(plugin, key(c.id), *field, current), fo4::ScalarMutationStatus::unchanged);
    EXPECT_EQ(fo4::write_plugin(plugin), before);
  }
}

TEST_F(VegetationEdit, ShortGrassDataExposesOnlyItsPrefix) {
  for (const std::size_t size : {0u, 3u, 4u, 5u, 12u, 28u, 29u}) {
    SCOPED_TRACE(size);
    auto records = defaults();
    sub(records, k_gras, "DATA").second.resize(size);
    write(records);
    auto plugin = fo4::read_plugin_file(path);
    for (const auto& field : fo4::schema::scalar_fields(fo4::Signature::from_string("GRAS"))) {
      const auto before = fo4::write_plugin(plugin);
      if (field.scalar.offset + fo4::schema::scalar_storage_size(field.scalar.storage) > size) {
        EXPECT_EQ(fo4::set_record_scalar(plugin, key(k_gras), field, "1"), fo4::ScalarMutationStatus::subrecord_missing) << field.path;
        EXPECT_EQ(fo4::write_plugin(plugin), before);
      }
    }
  }
}

TEST_F(VegetationEdit, RefusesUnknownLayoutsAndBadValuesBeforeAnyByteChanges) {
  const auto refuse = [&](const std::vector<Rec>& records, std::uint32_t id, const char* path_name, const char* value) {
    write(records);
    auto plugin = fo4::read_plugin_file(path);
    const auto before = fo4::write_plugin(plugin);
    const auto field = fo4::schema::scalar_field_descriptor(path_name);
    ASSERT_TRUE(field);
    EXPECT_THROW(fo4::set_record_scalar(plugin, key(id), *field, value), fo4::schema::ScalarError) << path_name << '=' << value;
    EXPECT_EQ(fo4::write_plugin(plugin), before);
  };
  const auto absent = [&](const std::vector<Rec>& records, std::uint32_t id, const char* path_name) {
    write(records);
    auto plugin = fo4::read_plugin_file(path);
    const auto before = fo4::write_plugin(plugin);
    const auto field = fo4::schema::scalar_field_descriptor(path_name);
    ASSERT_TRUE(field);
    EXPECT_EQ(fo4::set_record_scalar(plugin, key(id), *field, "9"), fo4::ScalarMutationStatus::subrecord_missing) << path_name;
    EXPECT_EQ(fo4::write_plugin(plugin), before);
  };
  for (const std::size_t size : {44u, 52u}) {
    auto records = defaults();
    sub(records, k_tree, "CNAM").second = sized(size, 0);
    absent(records, k_tree, "TREE\\TrunkFlexibility");
    absent(records, k_tree, "TREE\\LeafFrequency");
  }
  for (const std::size_t size : {3u, 5u}) {
    auto records = defaults();
    sub(records, k_tree, "PFPC").second = sized(size);
    sub(records, k_flor, "PFPC").second = sized(size);
    absent(records, k_tree, "TREE\\Spring");
    absent(records, k_flor, "FLOR\\Winter");
  }
  for (const std::size_t size : {1u, 3u}) {
    auto records = defaults();
    sub(records, k_flor, "FNAM").second = sized(size);
    absent(records, k_flor, "FLOR\\Flags");
  }
  for (const auto& [id, tag, path_name] : {std::tuple{k_gras, "DATA", "GRAS\\Density"}, std::tuple{k_tree, "CNAM", "TREE\\TrunkAmplitude"},
                                           std::tuple{k_tree, "PFPC", "TREE\\Fall"}, std::tuple{k_flor, "FNAM", "FLOR\\Flags"},
                                           std::tuple{k_flor, "PFPC", "FLOR\\Fall"}}) {
    auto records = defaults();
    duplicate(records, id, tag);
    refuse(records, id, path_name, "1");
  }
  const auto records = defaults();
  refuse(records, k_tree, "GRAS\\Density", "1");
  refuse(records, k_gras, "TREE\\Spring", "1");
  refuse(records, k_flor, "TREE\\Spring", "1");
  for (const char* value : {"256", "-1", "abc", "", "1.5", "1e2"}) { refuse(records, k_gras, "GRAS\\Density", value); }
  refuse(records, k_gras, "GRAS\\UnitsFromWater", "65536");
  refuse(records, k_gras, "GRAS\\UnitsFromWaterType", "4294967296");
  refuse(records, k_gras, "GRAS\\Flags", "256");
  refuse(records, k_tree, "TREE\\Winter", "256");
  refuse(records, k_flor, "FLOR\\Flags", "65536");
  for (const char* value : {"nan", "inf", "-inf", "abc", "", "1e39", "-1e39"}) {
    refuse(records, k_gras, "GRAS\\PositionRange", value);
    refuse(records, k_tree, "TREE\\SideFrequency", value);
  }
}

TEST(VegetationEditSchema, PayloadSizeMismatchReadsAsAbsentWithoutThrowing) {
  const auto field = fo4::schema::scalar_field_descriptor("TREE\\Spring");
  ASSERT_TRUE(field);
  for (const std::size_t size : {0u, 3u, 5u}) {
    const Bytes payload(size);
    EXPECT_TRUE(fo4::schema::scalar_field_bytes(payload, field->scalar).empty()) << size;
  }
  const Bytes exact(4);
  EXPECT_EQ(fo4::schema::scalar_field_bytes(exact, field->scalar).size(), 1u);
}

TEST_F(VegetationEdit, MissingRecordAndSubrecordAreReportedWithoutInsertion) {
  auto records = defaults();
  drop(records, k_gras, "DATA");
  drop(records, k_tree, "CNAM");
  drop(records, k_tree, "PFPC");
  drop(records, k_flor, "FNAM");
  drop(records, k_flor, "PFPC");
  write(records);
  auto plugin = fo4::read_plugin_file(path);
  const auto before = fo4::write_plugin(plugin);
  const std::pair<std::uint32_t, const char*> targets[] = {{k_gras, "GRAS\\Density"}, {k_tree, "TREE\\TrunkFlexibility"}, {k_tree, "TREE\\Spring"},
                                                           {k_flor, "FLOR\\Flags"}, {k_flor, "FLOR\\Spring"}};
  for (const auto& [id, path_name] : targets) {
    EXPECT_EQ(fo4::set_record_scalar(plugin, key(id), *fo4::schema::scalar_field_descriptor(path_name), "1"),
              fo4::ScalarMutationStatus::subrecord_missing) << path_name;
  }
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(0x999), *fo4::schema::scalar_field_descriptor("GRAS\\Density"), "1"),
            fo4::ScalarMutationStatus::record_missing);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST_F(VegetationEdit, CompressedRecordEditRoundTrips) {
  write(defaults());
  auto seed = fo4::read_plugin_file(path);
  for (auto& group : seed.groups) {
    auto& target = std::get<fo4::Record>(group.children.front());
    target.compressed = true;
    target.header.flags = fo4::RecordFlags{target.header.flags.value() | fo4::RecordFlags::compressed};
    target.original_range.clear();
  }
  seed.original_range.clear();
  fo4::write_plugin_file(seed, path);
  auto plugin = fo4::read_plugin_file(path);
  const auto before = fo4::write_plugin(plugin);
  ASSERT_EQ(fo4::set_record_scalar(plugin, key(k_tree), *fo4::schema::scalar_field_descriptor("TREE\\Summer"), "2"), fo4::ScalarMutationStatus::unchanged);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
  ASSERT_EQ(fo4::set_record_scalar(plugin, key(k_tree), *fo4::schema::scalar_field_descriptor("TREE\\Summer"), "77"), fo4::ScalarMutationStatus::updated);
  ASSERT_EQ(fo4::set_record_scalar(plugin, key(k_gras), *fo4::schema::scalar_field_descriptor("GRAS\\WavePeriod"), "12.5"), fo4::ScalarMutationStatus::updated);
  fo4::write_plugin_file(plugin, path);
  const auto env = load();
  EXPECT_TRUE(record(env, k_tree).record->compressed);
  EXPECT_TRUE(record(env, k_gras).record->compressed);
  const auto tree = fo4::env::decode_tree(env, record(env, k_tree));
  ASSERT_TRUE(tree.harvest.seasons);
  EXPECT_EQ(*tree.harvest.seasons, (std::array<std::uint8_t, 4>{1, 77, 3, 4}));
  ASSERT_TRUE(tree.wind);
  EXPECT_FLOAT_EQ((*tree.wind)[11], 12.f);
  const auto grass = fo4::env::decode_grass(env, record(env, k_gras));
  ASSERT_TRUE(grass);
  EXPECT_FLOAT_EQ(grass->wave_period, 12.5f);
  const auto& subs = record(env, k_gras).record->subrecords;
  const auto data = std::find_if(subs.begin(), subs.end(), [](const fo4::Subrecord& s) { return s.signature == fo4::Signature::from_string("DATA"); });
  ASSERT_NE(data, subs.end());
  auto expected = grass_data();
  const float wave = 12.5f;
  std::memcpy(expected.data() + 24, &wave, 4);
  EXPECT_EQ(data->data, expected);
}

TEST_F(VegetationEdit, CAbiMapsStatusesAndSavesRequestedValues) {
  write(defaults());
  const auto name = path.string();
  radicle_plugin* plugin = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(name.c_str(), &plugin), RADICLE_STATUS_OK);
  const radicle_form_key gras{"Vegetation.esm", k_gras}, tree{"Vegetation.esm", k_tree}, flor{"Vegetation.esm", k_flor};
  EXPECT_EQ(radicle_plugin_set_record_scalar(nullptr, gras, "GRAS\\Density", "1"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_scalar(plugin, {nullptr, k_gras}, "GRAS\\Density", "1"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_scalar(plugin, gras, nullptr, "1"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_scalar(plugin, gras, "GRAS\\Density", nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_scalar(plugin, gras, "GRAS\\Nope", "1"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_scalar(plugin, tree, "GRAS\\Density", "1"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_scalar(plugin, gras, "GRAS\\Density", "256"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_scalar(plugin, gras, "GRAS\\PositionRange", "nan"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_scalar(plugin, gras, "GRAS\\Density", "abc"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_scalar(plugin, {"Vegetation.esm", 0x999}, "GRAS\\Density", "1"), RADICLE_STATUS_NOT_FOUND);
  EXPECT_STREQ(radicle_last_error(), "plugin has no such record");
  EXPECT_EQ(radicle_plugin_set_record_scalar(plugin, gras, "GRAS\\Density", "254"), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_plugin_set_record_scalar(plugin, gras, "GRAS\\Density", "21"), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_plugin_set_record_scalar(plugin, gras, "GRAS\\UnitsFromWaterType", "6"), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_plugin_set_record_scalar(plugin, tree, "TREE\\LeafFrequency", "-3.5"), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_plugin_set_record_scalar(plugin, tree, "TREE\\Winter", "250"), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_plugin_set_record_scalar(plugin, flor, "FLOR\\Flags", "513"), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_plugin_set_record_scalar(plugin, flor, "FLOR\\Spring", "9"), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_plugin_write_file(plugin, name.c_str()), RADICLE_STATUS_OK);
  radicle_plugin_destroy(plugin);

  const char* paths[] = {name.c_str()};
  radicle_env* env = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 1, &env), RADICLE_STATUS_OK);
  const radicle_record* handle = nullptr;
  ASSERT_EQ(radicle_env_winner(env, gras, &handle), RADICLE_STATUS_OK);
  radicle_grass_info grass{};
  ASSERT_EQ(radicle_grass_get_info(env, handle, &grass), RADICLE_STATUS_OK);
  EXPECT_EQ(grass.density, 21);
  EXPECT_EQ(grass.water_type, 6u);
  EXPECT_EQ(grass.units_from_water, 500);
  ASSERT_EQ(radicle_env_winner(env, tree, &handle), RADICLE_STATUS_OK);
  radicle_tree_info tree_info{};
  ASSERT_EQ(radicle_tree_get_info(env, handle, &tree_info), RADICLE_STATUS_OK);
  EXPECT_FLOAT_EQ(tree_info.wind[11], -3.5f);
  EXPECT_EQ(tree_info.harvest.seasons[3], 250);
  EXPECT_EQ(tree_info.harvest.seasons[0], 1);
  ASSERT_EQ(radicle_env_winner(env, flor, &handle), RADICLE_STATUS_OK);
  radicle_flora_info flora{};
  ASSERT_EQ(radicle_flora_get_info(env, handle, &flora), RADICLE_STATUS_OK);
  EXPECT_EQ(flora.flags, 513);
  EXPECT_EQ(flora.harvest.seasons[0], 9);
  radicle_env_destroy(env);

  auto missing = defaults();
  drop(missing, k_gras, "DATA");
  sub(missing, k_tree, "CNAM").second = sized(44);
  duplicate(missing, k_flor, "FNAM");
  write(missing);
  ASSERT_EQ(radicle_plugin_read_file(name.c_str(), &plugin), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_record_scalar(plugin, gras, "GRAS\\Density", "1"), RADICLE_STATUS_NOT_FOUND);
  EXPECT_STREQ(radicle_last_error(), "record has no such field occurrence, or the field is outside its supported payload layout");
  EXPECT_EQ(radicle_plugin_set_record_scalar(plugin, tree, "TREE\\TrunkFlexibility", "1"), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_record_scalar(plugin, flor, "FLOR\\Flags", "1"), RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_plugin_destroy(plugin);
}

}
