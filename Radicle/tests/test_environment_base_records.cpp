#include "fo4/c_api_env.h"
#include "fo4/environment/base_records.hpp"
#include "fo4/environment/placed.hpp"
#include "fo4/forms/placed_reference.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
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
  std::uint16_t form_version{131};
  std::vector<Sub> subrecords;
  std::uint32_t label{0};
  std::int32_t group_type{0};
  std::vector<Node> children;
};

Node rec(std::string signature, std::uint32_t form_id, std::vector<Sub> subrecords = {}) {
  Node node;
  node.signature = std::move(signature);
  node.form_id = form_id;
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

std::vector<std::byte> words(std::initializer_list<std::uint32_t> values) {
  fo4::test::ByteWriter out;
  for (const auto value : values) out.u32(value);
  return out.bytes;
}

std::vector<std::byte> floats(std::initializer_list<float> values) {
  fo4::test::ByteWriter out;
  for (const auto value : values) out.f32(value);
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
  const auto header = fo4::test::begin_record(out, node.signature, 0, node.form_id, node.form_version);
  for (const auto& sub : node.subrecords) fo4::test::write_subrecord(out, sub.signature, sub.data);
  fo4::test::end_record(out, header);
}

std::vector<std::byte> make_plugin(std::uint32_t flags, const std::vector<std::string>& masters,
                                   const std::vector<Node>& groups) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", flags, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  for (const auto& master : masters) {
    std::vector<std::byte> name;
    for (const char ch : master) name.push_back(std::byte{static_cast<std::uint8_t>(ch)});
    name.push_back(std::byte{0});
    fo4::test::write_subrecord(out, "MAST", name);
    fo4::test::write_subrecord(out, "DATA", words({0, 0}));
  }
  fo4::test::end_record(out, tes4);
  for (const auto& group : groups) write_node(out, group);
  return out.bytes;
}


std::vector<std::byte> text(std::string_view value) {
  std::vector<std::byte> out;
  for (const char ch : value) out.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  out.push_back(std::byte{0});
  return out;
}

std::vector<std::byte> light_data(std::size_t size) {
  fo4::test::ByteWriter out;
  out.i32(-1);
  out.u32(512);
  out.u32(0x00804020);
  out.u32(0x00000400 | 0x00000008);
  for (float v : {1.5f, 90.0f, 10.0f, 0.2f, 0.3f, 0.4f, 1.0f, 2.0f, 3.0f, 4.0f}) out.f32(v);
  out.u32(25);
  out.f32(0.5f);
  out.bytes.resize(size);
  return out.bytes;
}

std::vector<std::byte> spline_data(std::size_t size) {
  fo4::test::ByteWriter out;
  out.f32(2.0f);
  out.u16(6);
  out.u16(1);
  for (float v : {0.25f, 0.5f, 0.75f, 0.1f, 0.2f}) out.f32(v);
  out.bytes.resize(size);
  return out.bytes;
}

std::vector<std::byte> lgtm_data() {
  fo4::test::ByteWriter out;
  out.u32(0x00030201);
  out.u32(0x00060504);
  out.u32(0x00090807);
  for (float v : {100.0f, 5000.0f}) out.f32(v);
  out.i32(10);
  out.i32(20);
  for (float v : {1.0f, 9000.0f, 2.0f}) out.f32(v);
  for (int i = 0; i < 32; ++i) out.u8(0);
  out.u32(0x000C0B0A);
  for (float v : {0.9f, 100.0f, 200.0f}) out.f32(v);
  out.u32(0xFFFFFFFF);
  for (float v : {300.0f, 400.0f}) out.f32(v);
  out.u32(0x00332211);
  out.u32(0x00664455);
  for (float v : {2.0f, 3.0f, 4.0f, 5.0f, 6.0f}) out.f32(v);
  for (float v : {500.0f, 600.0f}) out.f32(v);
  return out.bytes;
}

std::vector<std::byte> weather_colors(std::size_t groups, std::size_t times) {
  std::vector<std::byte> out;
  for (std::size_t g = 0; g < groups; ++g) {
    for (std::size_t t = 0; t < times; ++t) {
      for (const auto value : {g, t, g + t, std::size_t{0}}) out.push_back(std::byte{static_cast<std::uint8_t>(value)});
    }
  }
  return out;
}

std::vector<std::byte> placement(float base) {
  fo4::test::ByteWriter out;
  for (int i = 0; i < 6; ++i) out.f32(base + static_cast<float>(i));
  out.f32(1.5f);
  return out.bytes;
}

std::vector<std::byte> concat(std::vector<std::byte> a, const std::vector<std::byte>& b) {
  a.insert(a.end(), b.begin(), b.end());
  return a;
}

Node versioned(Node node, std::uint16_t version) {
  node.form_version = version;
  return node;
}

std::vector<std::byte> make_base() {
  return make_plugin(0x1, {},
                     {top("LIGH", {rec("LIGH", 0x800, {{"DATA", light_data(64)}, {"FNAM", floats({0.75f})},
                                                        {"NAM0", text("Gobo.dds")}, {"LNAM", words({0x850})}}),
                                   rec("LIGH", 0x801, {{"DATA", light_data(48)}}),
                                   rec("LIGH", 0x802, {{"DATA", light_data(40)}}),
                                   rec("LIGH", 0x803, {{"DATA", light_data(50)}}),
                                   rec("LIGH", 0x804, {{"DATA", light_data(68)}})}),
                      top("BNDS", {rec("BNDS", 0x810, {{"DNAM", spline_data(28)}, {"TNAM", words({0x851})}}),
                                   rec("BNDS", 0x811, {{"DNAM", spline_data(24)}}),
                                   rec("BNDS", 0x812, {{"DNAM", spline_data(10)}})}),
                      top("LAYR", {rec("LAYR", 0x820), rec("LAYR", 0x821, {{"PNAM", words({0x820})}})}),
                      top("LGTM", {rec("LGTM", 0x830, {{"DATA", lgtm_data()}, {"WGDR", words({0x852})}})}),
                      top("CLMT", {rec("CLMT", 0x840, {{"WLST", words({0x841, 60, 0, 0x842, 40, 0x853})},
                                                        {"FNAM", text("Sun.dds")},
                                                        {"GNAM", text("Glare.dds")}}),
                                   rec("CLMT", 0x843, {{"WLST", words({0x841, 60})}})}),
                      top("WTHR", {rec("WTHR", 0x841, {{"NAM0", weather_colors(19, 8)},
                                                        {"FNAM", floats({100, 9000, 50, 4000, 1, 1, 1, 1, 0, 10000,
                                                                         0, 10000, 1, 1, 0, 0, 0, 0})}}),
                                   versioned(rec("WTHR", 0x842, {{"NAM0", weather_colors(17, 4)},
                                                                 {"FNAM", floats({1, 2, 3, 4, 5, 6, 7, 8})}}),
                                             100),
                                   rec("WTHR", 0x844, {{"NAM0", weather_colors(8, 8)}})}),
                      top("SCOL", {rec("SCOL", 0x860, {{"EDID", text("Pile")},
                                                        {"ONAM", words({0x870})},
                                                        {"DATA", concat(placement(0), placement(10))},
                                                        {"ONAM", words({0})},
                                                        {"ONAM", words({0x871})},
                                                        {"DATA", placement(20)}}),
                                   rec("SCOL", 0x861, {{"DATA", placement(0)}})}),
                      top("ADDN", {rec("ADDN", 0x880, {{"DATA", words({283})},
                                                       {"LNAM", words({0x800})},
                                                       {"MODL", text("Lights\\FireAddOn.nif")},
                                                       {"DNAM", words({0x0002011B})}}),
                                   rec("ADDN", 0x881, {{"DATA", words({282})},
                                                       {"MODL", text("Lights\\Klaxon.nif")},
                                                       {"DNAM", words({0x00030000})}}),
                                   rec("ADDN", 0x882, {{"DATA", words({281})},
                                                       {"LNAM", words({0x801})},
                                                       {"DNAM", words({0x00040001})}}),
                                   rec("ADDN", 0x883, {{"DATA", words({280})},
                                                       {"LNAM", words({0x800})},
                                                       {"MODL", text("Lights\\LunchBox.nif")}}),
                                   rec("ADDN", 0x884, {{"DNAM", words({1, 2})}}),
                                   rec("ADDN", 0x885, {{"DATA", {std::byte{1}, std::byte{2}}}})})});
}

class BaseRecordsTest : public ::testing::Test {
protected:
  void SetUp() override {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    root_ = std::filesystem::temp_directory_path() / ("radicle_base_records_" + std::to_string(id));
    std::filesystem::create_directories(root_);
    path_ = root_ / "Base.esm";
    const auto data = make_base();
    std::ofstream stream(path_, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
  }

  void TearDown() override {
    std::error_code ec;
    std::filesystem::remove_all(root_, ec);
  }

  std::filesystem::path root_;
  std::filesystem::path path_;
};

fo4::FormKey key(std::uint32_t local) {
  return fo4::FormKey{fo4::PluginKey::from_filename("Base.esm"), fo4::LocalFormId{local}};
}

}  // namespace

TEST_F(BaseRecordsTest, DecodesLightsSplinesAndLayers) {
  const std::vector<std::filesystem::path> paths{path_};
  const auto env = fo4::env::Environment::load(paths);
  const auto light = fo4::env::decode_light(env, *env.winner(key(0x800)));
  ASSERT_TRUE(light.data.has_value());
  EXPECT_EQ(light.data->radius, 512u);
  EXPECT_EQ(light.data->color.red, 0x20);
  EXPECT_EQ(light.data->color.blue, 0x80);
  EXPECT_EQ(light.data->flags, 0x408u);
  EXPECT_EQ(light.data->fov, 90.0f);
  EXPECT_EQ(light.data->weight, std::optional<float>{0.5f});
  EXPECT_EQ(light.fade, std::optional<float>{0.75f});
  EXPECT_EQ(light.gobo, std::optional<std::string_view>{"Gobo.dds"});
  EXPECT_EQ(light.lens, key(0x850));
  const auto short_light = fo4::env::decode_light(env, *env.winner(key(0x801)));
  EXPECT_EQ(short_light.data->scalar, 2.0f);
  EXPECT_FALSE(short_light.data->exponent.has_value());
  EXPECT_THROW(fo4::env::decode_light(env, *env.winner(key(0x802))), fo4::env::WorldDecodeError);
  EXPECT_THROW(fo4::env::decode_light(env, *env.winner(key(0x803))), fo4::env::WorldDecodeError);
  EXPECT_THROW(fo4::env::decode_light(env, *env.winner(key(0x804))), fo4::env::WorldDecodeError);
  EXPECT_THROW(fo4::env::decode_light(env, *env.winner(key(0x810))), fo4::env::WorldDecodeError);

  const auto spline = fo4::env::decode_bendable_spline(env, *env.winner(key(0x810)));
  EXPECT_EQ(spline.default_slices, std::optional<std::uint16_t>{6});
  EXPECT_EQ(spline.tiles_relative_to_length, std::optional<bool>{true});
  EXPECT_EQ(spline.default_color, (std::optional<std::array<float, 3>>{{0.25f, 0.5f, 0.75f}}));
  EXPECT_EQ(spline.wind_flexibility, std::optional<float>{0.2f});
  EXPECT_EQ(spline.texture, key(0x851));
  EXPECT_FALSE(fo4::env::decode_bendable_spline(env, *env.winner(key(0x811))).wind_flexibility.has_value());
  EXPECT_THROW(fo4::env::decode_bendable_spline(env, *env.winner(key(0x812))), fo4::env::WorldDecodeError);

  EXPECT_FALSE(fo4::env::decode_layer_parent(env, *env.winner(key(0x820))).has_value());
  EXPECT_EQ(fo4::env::decode_layer_parent(env, *env.winner(key(0x821))), key(0x820));
}

// Regression: ADDN decode must keep a missing LNAM or MODL absent instead of inventing a key or an empty path.
TEST_F(BaseRecordsTest, DecodesAddOnNodesAndDistinguishesMissingLinksFromMissingModels) {
  const std::vector<std::filesystem::path> paths{path_};
  const auto env = fo4::env::Environment::load(paths);

  const auto full = fo4::env::decode_add_on_node(env, *env.winner(key(0x880)));
  EXPECT_EQ(full.node_index, std::optional<std::uint32_t>{283});
  EXPECT_EQ(full.light, key(0x800));
  EXPECT_EQ(full.model, std::optional<std::string_view>{"Lights\\FireAddOn.nif"});
  EXPECT_EQ(full.master_particle_cap, std::optional<std::uint16_t>{283});
  EXPECT_EQ(full.flags, std::optional<std::uint16_t>{2});

  const auto without_light = fo4::env::decode_add_on_node(env, *env.winner(key(0x881)));
  EXPECT_FALSE(without_light.light.has_value());
  EXPECT_EQ(without_light.model, std::optional<std::string_view>{"Lights\\Klaxon.nif"});

  const auto without_model = fo4::env::decode_add_on_node(env, *env.winner(key(0x882)));
  EXPECT_EQ(without_model.light, key(0x801));
  EXPECT_FALSE(without_model.model.has_value());

  const auto without_dnam = fo4::env::decode_add_on_node(env, *env.winner(key(0x883)));
  EXPECT_FALSE(without_dnam.master_particle_cap.has_value());
  EXPECT_FALSE(without_dnam.flags.has_value());

  EXPECT_THROW(fo4::env::decode_add_on_node(env, *env.winner(key(0x884))), fo4::env::WorldDecodeError);
  EXPECT_THROW(fo4::env::decode_add_on_node(env, *env.winner(key(0x885))), fo4::env::WorldDecodeError);
}

TEST_F(BaseRecordsTest, DecodesSkyRecords) {
  const std::vector<std::filesystem::path> paths{path_};
  const auto env = fo4::env::Environment::load(paths);
  const auto lgtm = fo4::env::decode_lighting_template(env, *env.winner(key(0x830)));
  ASSERT_TRUE(lgtm.lighting.has_value());
  EXPECT_EQ(lgtm.lighting->ambient.green, 2);
  EXPECT_EQ(lgtm.lighting->fog_far, 5000.0f);
  EXPECT_EQ(lgtm.lighting->fog_far_color->red, 0x0A);
  EXPECT_EQ(lgtm.lighting->light_fade_end, std::optional<float>{200.0f});
  EXPECT_FALSE(lgtm.lighting->inherits.has_value());
  EXPECT_EQ(lgtm.lighting->near_height_mid, std::optional<float>{300.0f});
  EXPECT_EQ(lgtm.lighting->near_height_range, std::optional<float>{400.0f});
  ASSERT_TRUE(lgtm.lighting->fog_color_high_far.has_value());
  EXPECT_EQ(lgtm.lighting->fog_color_high_far->blue, 0x66);
  EXPECT_EQ(lgtm.lighting->high_density_scale, std::optional<float>{2.0f});
  EXPECT_EQ(lgtm.lighting->fog_high_far_scale, std::optional<float>{6.0f});
  EXPECT_EQ(lgtm.lighting->far_height_range, std::optional<float>{600.0f});
  EXPECT_EQ(lgtm.god_rays, key(0x852));

  const auto climate = fo4::env::decode_climate(env, *env.winner(key(0x840)));
  ASSERT_EQ(climate.weathers.size(), 2u);
  EXPECT_EQ(climate.weathers[0].weather, key(0x841));
  EXPECT_EQ(climate.weathers[0].chance, 60);
  EXPECT_TRUE(climate.weathers[0].global.owner().empty());
  EXPECT_EQ(climate.weathers[1].global, key(0x853));
  EXPECT_EQ(climate.sun_texture, std::optional<std::string_view>{"Sun.dds"});
  EXPECT_THROW(fo4::env::decode_climate(env, *env.winner(key(0x843))), fo4::env::WorldDecodeError);

  using fo4::env::WeatherColorGroup;
  const auto weather = fo4::env::decode_weather(env, *env.winner(key(0x841)));
  ASSERT_TRUE(weather.colors.has_value());
  EXPECT_EQ(weather.colors->times, 8u);
  EXPECT_EQ(weather.colors->groups, 19u);
  const auto horizon = weather.colors->color(WeatherColorGroup::horizon, fo4::env::k_weather_day);
  ASSERT_TRUE(horizon.has_value());
  EXPECT_EQ(horizon->red, 8);
  EXPECT_EQ(horizon->green, 1);
  EXPECT_EQ(weather.colors->color(WeatherColorGroup::fog_far, 1)->red, 12);
  EXPECT_FALSE(weather.colors->color(WeatherColorGroup::sky_upper, 8).has_value());
  ASSERT_EQ(weather.fog_distances.size(), 18u);
  EXPECT_EQ(weather.fog_distances[1], 9000.0f);

  const auto old = fo4::env::decode_weather(env, *env.winner(key(0x842)));
  EXPECT_EQ(old.colors->times, 4u);
  EXPECT_EQ(old.colors->groups, 17u);
  EXPECT_FALSE(old.colors->color(WeatherColorGroup::fog_near_high, 0).has_value());
  EXPECT_EQ(old.colors->color(WeatherColorGroup::sky_lower, 3)->blue, 10);
  EXPECT_THROW(fo4::env::decode_weather(env, *env.winner(key(0x844))), fo4::env::WorldDecodeError);
}

TEST_F(BaseRecordsTest, RejectsFogDistanceSizesOutsideTheFormSet) {
  const auto data = make_plugin(0x1, {}, {top("WTHR", {rec("WTHR", 0x850, {{"FNAM", floats({1, 2, 3, 4, 5})}})})});
  {
    std::ofstream stream(path_, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
  }
  const std::vector<std::filesystem::path> paths{path_};
  const auto env = fo4::env::Environment::load(paths);
  EXPECT_THROW(fo4::env::decode_weather(env, *env.winner(key(0x850))), fo4::env::WorldDecodeError);
}

TEST_F(BaseRecordsTest, RejectsFogDistanceSizeThatDisagreesWithFormVersion) {
  const auto data = make_plugin(0x1, {}, {top("WTHR", {versioned(rec("WTHR", 0x851, {{"FNAM", floats({1, 2, 3, 4, 5, 6, 7, 8})}}), 120)})});
  {
    std::ofstream stream(path_, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
  }
  const std::vector<std::filesystem::path> paths{path_};
  const auto env = fo4::env::Environment::load(paths);
  EXPECT_THROW(fo4::env::decode_weather(env, *env.winner(key(0x851))), fo4::env::WorldDecodeError);
}

TEST_F(BaseRecordsTest, DecodesStaticCollectionsAndListsPluginRecords) {
  const std::vector<std::filesystem::path> paths{path_};
  const auto env = fo4::env::Environment::load(paths);
  const auto parts = fo4::env::decode_static_collection(env, *env.winner(key(0x860)));
  ASSERT_EQ(parts.size(), 3u);
  EXPECT_EQ(parts[0].static_record, key(0x870));
  ASSERT_EQ(parts[0].placements.size(), 2u);
  EXPECT_EQ(parts[0].placements[1].position, (std::array<float, 3>{10, 11, 12}));
  EXPECT_EQ(parts[0].placements[1].rotation, (std::array<float, 3>{13, 14, 15}));
  EXPECT_EQ(parts[0].placements[1].scale, 1.5f);
  EXPECT_TRUE(parts[1].static_record.owner().empty());
  EXPECT_TRUE(parts[1].placements.empty());
  EXPECT_EQ(parts[2].placements.size(), 1u);
  EXPECT_THROW(fo4::env::decode_static_collection(env, *env.winner(key(0x861))), fo4::env::WorldDecodeError);

  const auto records = env.plugin_records(0);
  ASSERT_EQ(records.size(), 24u);
  EXPECT_EQ(records.front()->record->form_key, key(0x800));
  EXPECT_EQ(records.back()->record->form_key, key(0x885));
  EXPECT_TRUE(env.plugin_records(1).empty());
}

TEST_F(BaseRecordsTest, CApiDecodesBaseRecords) {
  const std::string path = path_.string();
  const char* raw[] = {path.c_str()};
  radicle_env* env = nullptr;
  ASSERT_EQ(radicle_env_load(raw, 1, &env), RADICLE_STATUS_OK);
  const auto record = [&](std::uint32_t local) {
    const radicle_record* out = nullptr;
    EXPECT_EQ(radicle_env_winner(env, {"Base.esm", local}, &out), RADICLE_STATUS_OK);
    return out;
  };

  const radicle_record** records = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_env_plugin_records(env, 0, &records, &count), RADICLE_STATUS_OK);
  EXPECT_EQ(count, 24u);
  radicle_free_record_array(records);
  EXPECT_EQ(radicle_env_plugin_records(env, 1, &records, &count), RADICLE_STATUS_INVALID_ARGUMENT);

  radicle_light_info light{};
  ASSERT_EQ(radicle_light_get_info(env, record(0x800), &light), RADICLE_STATUS_OK);
  EXPECT_EQ(light.radius, 512u);
  EXPECT_EQ(light.has_weight, 1);
  EXPECT_STREQ(light.gobo, "Gobo.dds");
  EXPECT_EQ(radicle_light_get_info(env, record(0x802), &light), RADICLE_STATUS_PARSE_ERROR);

  radicle_add_on_node_info addon{};
  ASSERT_EQ(radicle_add_on_node_get_info(env, record(0x880), &addon), RADICLE_STATUS_OK);
  EXPECT_EQ(addon.has_node_index, 1);
  EXPECT_EQ(addon.node_index, 283u);
  EXPECT_STREQ(addon.light.plugin, "Base.esm");
  EXPECT_EQ(addon.light.local_id, 0x800u);
  EXPECT_STREQ(addon.model, "Lights\\FireAddOn.nif");
  EXPECT_EQ(addon.has_dnam, 1);
  EXPECT_EQ(addon.master_particle_cap, 283);
  EXPECT_EQ(addon.flags, 2);
  ASSERT_EQ(radicle_add_on_node_get_info(env, record(0x881), &addon), RADICLE_STATUS_OK);
  EXPECT_EQ(addon.light.plugin, nullptr);
  EXPECT_STREQ(addon.model, "Lights\\Klaxon.nif");
  EXPECT_EQ(addon.flags, 3);
  ASSERT_EQ(radicle_add_on_node_get_info(env, record(0x882), &addon), RADICLE_STATUS_OK);
  EXPECT_EQ(addon.light.local_id, 0x801u);
  EXPECT_EQ(addon.model, nullptr);
  ASSERT_EQ(radicle_add_on_node_get_info(env, record(0x883), &addon), RADICLE_STATUS_OK);
  EXPECT_EQ(addon.has_dnam, 0);
  EXPECT_EQ(radicle_add_on_node_get_info(env, record(0x884), &addon), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(radicle_add_on_node_get_info(env, record(0x885), &addon), RADICLE_STATUS_PARSE_ERROR);

  radicle_bendable_spline_info spline{};
  ASSERT_EQ(radicle_bendable_spline_get_info(env, record(0x810), &spline), RADICLE_STATUS_OK);
  EXPECT_EQ(spline.default_slices, 6);
  EXPECT_EQ(spline.default_color[2], 0.75f);

  radicle_form_key parent{};
  ASSERT_EQ(radicle_layer_get_parent(env, record(0x821), &parent), RADICLE_STATUS_OK);
  EXPECT_EQ(parent.local_id, 0x820u);

  radicle_lighting_template_info lgtm{};
  ASSERT_EQ(radicle_lighting_template_get_info(env, record(0x830), &lgtm), RADICLE_STATUS_OK);
  EXPECT_EQ(lgtm.has_lighting, 1);
  EXPECT_EQ(lgtm.lighting.fog_near, 100.0f);
  EXPECT_EQ(lgtm.lighting.has_inherits, 0);

  radicle_climate* climate = nullptr;
  ASSERT_EQ(radicle_climate_decode(env, record(0x840), &climate), RADICLE_STATUS_OK);
  ASSERT_EQ(climate->weather_count, 2u);
  EXPECT_EQ(climate->weathers[1].chance, 40);
  EXPECT_STREQ(climate->sun_glare_texture, "Glare.dds");
  radicle_climate_free(climate);

  radicle_weather_info* weather = nullptr;
  ASSERT_EQ(radicle_weather_decode(env, record(0x841), &weather), RADICLE_STATUS_OK);
  EXPECT_EQ(weather->color_groups, 19u);
  EXPECT_EQ(weather->colors[(8 * weather->color_times + 1) * 4], 8);
  EXPECT_EQ(weather->fog_distance_count, 18u);
  radicle_weather_free(weather);

  radicle_static_collection* collection = nullptr;
  ASSERT_EQ(radicle_static_collection_decode(env, record(0x860), &collection), RADICLE_STATUS_OK);
  ASSERT_EQ(collection->part_count, 3u);
  EXPECT_EQ(collection->parts[0].placement_count, 2u);
  EXPECT_EQ(collection->parts[0].placements[1].scale, 1.5f);
  EXPECT_EQ(collection->parts[1].static_record.plugin, nullptr);
  radicle_static_collection_free(collection);
  radicle_env_destroy(env);
}

TEST(BaseRecordsRealData, DecodesEveryShippedBaseRecord) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder";
  std::vector<std::filesystem::path> order;
  for (const char* name : {"Fallout4.esm", "DLCRobot.esm", "DLCworkshop01.esm", "DLCCoast.esm", "DLCworkshop02.esm",
                           "DLCworkshop03.esm", "DLCNukaWorld.esm"}) {
    const auto path = std::filesystem::path(dir) / name;
    if (std::filesystem::exists(path)) order.push_back(path);
  }
  if (order.empty()) GTEST_SKIP() << "no official masters in FO4_DATA_DIR";
  record_game_build(detect_game_build(dir));
  const auto env = fo4::env::Environment::load(order);
  std::map<std::string, std::size_t> decoded;
  for (std::size_t plugin = 0; plugin < env.plugins().size(); ++plugin) {
    for (const auto* ref : env.plugin_records(plugin)) {
      const auto signature = ref->record->header.signature.to_string();
      SCOPED_TRACE(signature + " " + ref->record->form_key.to_string());
      if (signature == "LIGH") {
        EXPECT_NO_THROW(fo4::env::decode_light(env, *ref));
      } else if (signature == "BNDS") {
        EXPECT_NO_THROW(fo4::env::decode_bendable_spline(env, *ref));
      } else if (signature == "LAYR") {
        EXPECT_NO_THROW(fo4::env::decode_layer_parent(env, *ref));
      } else if (signature == "LGTM") {
        EXPECT_TRUE(fo4::env::decode_lighting_template(env, *ref).lighting.has_value());
      } else if (signature == "CLMT") {
        EXPECT_NO_THROW(fo4::env::decode_climate(env, *ref));
      } else if (signature == "WTHR") {
        const auto weather = fo4::env::decode_weather(env, *ref);
        ASSERT_TRUE(weather.colors.has_value());
        EXPECT_GE(weather.colors->groups, 17u);
        EXPECT_GE(weather.fog_distances.size(), 8u);
      } else if (signature == "SCOL") {
        EXPECT_NO_THROW(fo4::env::decode_static_collection(env, *ref));
      } else {
        continue;
      }
      ++decoded[signature];
    }
  }
  for (const char* signature : {"LIGH", "BNDS", "LAYR", "LGTM", "CLMT", "WTHR", "SCOL"}) {
    EXPECT_GT(decoded[signature], 0u) << signature;
    RecordProperty(signature, static_cast<int>(decoded[signature]));
  }
}
