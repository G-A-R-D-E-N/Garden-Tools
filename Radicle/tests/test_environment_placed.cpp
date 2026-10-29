#include "fo4/c_api_env.h"
#include "fo4/environment/placed.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Sub {
  std::string signature;
  std::vector<std::byte> data;
};

struct Rec {
  std::string signature;
  std::uint32_t form_id{0};
  std::vector<Sub> subrecords;
};

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

std::vector<std::byte> bytes(std::initializer_list<std::uint8_t> values) {
  std::vector<std::byte> out;
  for (const auto value : values) out.push_back(std::byte{value});
  return out;
}

std::vector<std::byte> zstring(std::string_view text) {
  std::vector<std::byte> out;
  for (const char ch : text) out.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  out.push_back(std::byte{0});
  return out;
}

std::vector<std::byte> concat(std::initializer_list<std::vector<std::byte>> parts) {
  std::vector<std::byte> out;
  for (const auto& part : parts) out.insert(out.end(), part.begin(), part.end());
  return out;
}

void wstring(fo4::test::ByteWriter& out, std::string_view text) {
  out.u16(static_cast<std::uint16_t>(text.size()));
  out.raw(text);
}

std::vector<std::byte> make_vmad(bool with_variable, std::uint16_t format = 2, std::uint16_t word = 0) {
  fo4::test::ByteWriter out;
  out.u16(6);
  out.u16(format);
  out.u16(1);
  wstring(out, "DoorScript");
  out.u8(0);
  out.u16(with_variable ? 2 : 5);
  wstring(out, "Target");
  out.u8(1);
  out.u8(1);
  if (format == 2) out.u16(word);
  if (format == 1) out.u32(0x00000805);
  out.u16(0xFFFF);
  if (format == 1) out.u16(word);
  if (format == 2) out.u32(0x00000805);
  if (with_variable) {
    wstring(out, "Anything");
    out.u8(6);
    out.u8(1);
    return out.bytes;
  }
  wstring(out, "Caf\xE9");
  out.u8(2);
  out.u8(1);
  wstring(out, "Caf\xC3\xA9");
  wstring(out, "Count");
  out.u8(3);
  out.u8(1);
  out.u32(7);
  wstring(out, "EmptyVariableArray");
  out.u8(16);
  out.u8(1);
  out.u32(0);
  wstring(out, "Values");
  out.u8(13);
  out.u8(1);
  out.u32(2);
  out.u32(10);
  out.u32(20);
  return out.bytes;
}

std::vector<std::byte> make_plugin(std::uint32_t tes4_flags, const std::vector<Rec>& records) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", tes4_flags, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  for (const auto& record : records) {
    const auto group = fo4::test::begin_group(out, record.signature, 0);
    const auto header = fo4::test::begin_record(out, record.signature, 0, record.form_id);
    for (const auto& sub : record.subrecords) fo4::test::write_subrecord(out, sub.signature, sub.data);
    fo4::test::end_record(out, header);
    fo4::test::end_group(out, group);
  }
  return out.bytes;
}

std::vector<Rec> fixture_records() {
  return {
      {"REFR", 0x800,
       {{"EDID", zstring("DoorRef")},
        {"VMAD", make_vmad(false)},
        {"NAME", words({0x900})},
        {"XPRM", concat({floats({1.0f, 2.0f, 3.0f, 0.5f, 0.25f, 0.125f, 9.0f}), words({1})})},
        {"XPOD", words({0x901, 0x902})},
        {"XPTL", floats({10.0f, 20.0f, 1.0f, 2.0f, 3.0f, 0.0f, 0.0f, 0.0f, 1.0f})},
        {"XRMR", bytes({2, 0xC0, 0, 0})},
        {"LNAM", words({0x903})},
        {"INAM", words({0x904})},
        {"XLRM", words({0x905})},
        {"XLRM", words({0x906})},
        {"XTEL", concat({words({0x907}), floats({1, 2, 3, 0.1f, 0.2f, 0.3f}), words({2, 0x908})})},
        {"XLYR", words({0x909})},
        {"XBSD", concat({floats({0.5f, 2.0f, 4.0f, 5.0f, 6.0f}), bytes({1})})},
        {"XAPD", bytes({1})},
        {"XAPR", concat({words({0x90A}), floats({1.5f})})},
        {"XEZN", words({0x90B})},
        {"XEMI", words({0x917})},
        {"XLRL", words({0x90C})},
        {"XLRT", words({0x90D, 0x90E})},
        {"XIS2", {}},
        {"XOWN", concat({words({0x90F, 0}), bytes({1, 0, 0, 0})})},
        {"XRNK", words({3})},
        {"XCNT", words({5})},
        {"XESP", concat({words({0x910}), bytes({1, 0, 0, 0})})},
        {"XLKR", words({0x911, 0x912})},
        {"XLKR", words({0x913})},
        {"XPRD", floats({12.0f})},
        {"XPPA", {}},
        {"INAM", words({0x914})},
        {"PDTO", words({0, 0x915})},
        {"PDTO", concat({words({1}), bytes({'H', 'E', 'L', 'O'})})},
        {"ONAM", {}},
        {"XMRK", {}},
        {"FNAM", bytes({3})},
        {"FULL", zstring("Sanctuary")},
        {"TNAM", bytes({12, 0})},
        {"MNAM", zstring("placed by test")},
        {"XATR", words({0x916})},
        {"DATA", floats({100.0f, 200.0f, 300.0f, 0.0f, 0.0f, 1.5f})},
        {"XSCL", floats({2.0f})}}},
      {"ACHR", 0x801,
       {{"NAME", words({0x920})},
        {"XPRD", floats({3.0f})},
        {"XPPA", {}},
        {"INAM", words({0x921})},
        {"TNAM", words({0x922})},
        {"XLCM", words({2})},
        {"DATA", floats({1, 2, 3, 0, 0, 0})}}},
      {"PHZD", 0x802, {{"NAME", words({0x930})}, {"DATA", floats({0, 0, 0, 0, 0, 0})}}},
      {"REFR", 0x803, {{"NAME", words({0x900})}, {"TNAM", bytes({1, 0})}}},
      {"REFR", 0x804, {{"NAME", words({0x900})}, {"VMAD", make_vmad(true)}}},
      {"ARMO", 0x805, {{"MODL", words({0x940})}, {"MOD4", zstring("Armor\\Female.nif")}}},
      {"SCOL", 0x806, {{"MODL", zstring("Scol\\Combined.nif")}}},
      {"STAT", 0x807, {{"MODL", zstring(std::string_view{"Rock.nif\0junk", 13})}}},
      {"REFR", 0x808, {{"NAME", words({0x900})}, {"XTEL", words({1, 2})}}},
      {"REFR", 0x809, {{"NAME", words({0x900})}, {"DATA", floats({0, 0, 0, 0, 0, 0})}}},
  };
}

class PlacedEnvironmentTest : public ::testing::Test {
protected:
  void SetUp() override {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    root_ = std::filesystem::temp_directory_path() / ("radicle_placed_" + std::to_string(id));
    std::filesystem::create_directories(root_);
    path_ = root_ / "Placed.esp";
    const auto data = make_plugin(0, fixture_records());
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
  return fo4::FormKey{fo4::PluginKey::from_filename("Placed.esp"), fo4::LocalFormId{local}};
}

}  // namespace

TEST_F(PlacedEnvironmentTest, DecodesObjectReferenceFields) {
  const std::vector<std::filesystem::path> paths{path_};
  const auto env = fo4::env::Environment::load(paths);
  const auto d = fo4::env::decode_placed_reference(env, *env.winner(key(0x800)));

  EXPECT_EQ(d.base, key(0x900));
  ASSERT_TRUE(d.position.has_value());
  EXPECT_EQ((*d.position)[2], 300.0f);
  EXPECT_EQ((*d.rotation)[2], 1.5f);
  EXPECT_EQ(d.scale, 2.0f);
  EXPECT_EQ(d.layer, key(0x909));
  EXPECT_EQ(d.encounter_zone, key(0x90B));
  EXPECT_EQ(d.emittance, key(0x917));
  EXPECT_EQ(d.location_reference, key(0x90C));
  ASSERT_EQ(d.location_ref_types.size(), 2u);
  EXPECT_EQ(d.location_ref_types[1], key(0x90E));
  ASSERT_TRUE(d.enable_parent.has_value());
  EXPECT_EQ(d.enable_parent->reference, key(0x910));
  EXPECT_EQ(d.enable_parent->flags, std::optional<std::uint8_t>{1});
  ASSERT_TRUE(d.ownership.has_value());
  EXPECT_EQ(d.ownership->owner, key(0x90F));
  EXPECT_EQ(d.ownership->no_crime, std::optional<bool>{true});
  EXPECT_EQ(d.faction_rank, 3);
  ASSERT_EQ(d.linked_references.size(), 2u);
  EXPECT_EQ(d.linked_references[0].keyword, key(0x911));
  EXPECT_EQ(d.linked_references[0].reference, key(0x912));
  EXPECT_EQ(d.linked_references[1].keyword, key(0x913));
  EXPECT_TRUE(d.linked_references[1].reference.owner().empty());
  EXPECT_EQ(d.activate_parent_flags, std::optional<std::uint8_t>{1});
  ASSERT_EQ(d.activate_parents.size(), 1u);
  EXPECT_EQ(d.activate_parents[0].delay, 1.5f);
  EXPECT_EQ(d.comments, "placed by test");
  EXPECT_TRUE(d.ignored_by_sandbox);
  EXPECT_TRUE(d.open_by_default);
  EXPECT_EQ(d.attach_ref, key(0x916));
  EXPECT_EQ(d.count, 5);

  ASSERT_TRUE(d.map_marker.has_value());
  EXPECT_EQ(d.map_marker->flags, std::optional<std::uint8_t>{3});
  EXPECT_EQ(d.map_marker->name.text, "Sanctuary");
  EXPECT_EQ(d.map_marker->type, std::optional<std::uint8_t>{12});

  ASSERT_TRUE(d.teleport.has_value());
  EXPECT_EQ(d.teleport->door, key(0x907));
  EXPECT_EQ(d.teleport->rotation[1], 0.2f);
  EXPECT_EQ(d.teleport->flags, 2u);
  EXPECT_EQ(d.teleport->transition_interior, key(0x908));

  ASSERT_TRUE(d.primitive.has_value());
  EXPECT_EQ(d.primitive->bounds[1], 2.0f);
  EXPECT_EQ(d.primitive->color[3], 9.0f);
  EXPECT_EQ(d.primitive->type, 1u);
  ASSERT_TRUE(d.spline.has_value());
  EXPECT_EQ(d.spline->half_extents[2], 6.0f);
  EXPECT_EQ(d.spline->wind_detached_end, std::optional<bool>{true});

  ASSERT_EQ(d.portals.size(), 1u);
  EXPECT_EQ(d.portals[0].destination, key(0x902));
  ASSERT_TRUE(d.room_portal.has_value());
  EXPECT_EQ(d.room_portal->height, 20.0f);
  EXPECT_EQ(d.room_portal->rotation[3], 1.0f);
  ASSERT_TRUE(d.room.has_value());
  EXPECT_EQ(d.room->linked_room_count, 2);
  EXPECT_EQ(d.room->flags, 0xC0);
  EXPECT_EQ(d.room->lighting_template, key(0x903));
  EXPECT_EQ(d.room->image_space, key(0x904));
  ASSERT_EQ(d.room->linked_rooms.size(), 2u);

  ASSERT_EQ(d.patrols.size(), 1u);
  EXPECT_EQ(d.patrols[0].idle_time, 12.0f);
  EXPECT_EQ(d.patrols[0].idle, key(0x914));
  ASSERT_EQ(d.patrols[0].topics.size(), 2u);
  EXPECT_EQ(d.patrols[0].topics[0].topic, key(0x915));
  EXPECT_EQ(d.patrols[0].topics[1].type, 1u);

  ASSERT_TRUE(d.scripts.has_value());
  EXPECT_FALSE(d.scripts->raw.unsupported);
  ASSERT_EQ(d.scripts->scripts.size(), 1u);
  const auto& script = d.scripts->scripts[0];
  EXPECT_EQ(script.name, "DoorScript");
  ASSERT_EQ(script.properties.size(), 5u);
  EXPECT_EQ(script.properties[0].object, key(0x805));
  EXPECT_EQ(script.properties[0].alias, std::optional<std::int16_t>{-1});
  EXPECT_EQ(script.properties[1].name, "Caf\xE9");
  EXPECT_EQ(script.properties[1].text, std::optional<std::string>{"Caf\xC3\xA9"});
  EXPECT_EQ(script.properties[2].integer, 7);
  EXPECT_EQ(script.properties[3].type, 16);
  EXPECT_EQ(script.properties[3].element_count, std::optional<std::uint32_t>{0});
  EXPECT_EQ(script.properties[4].element_count, std::optional<std::uint32_t>{2});

  std::optional<fo4::env::PlacedReferenceData> original{d};
  const auto copy = *original;
  original.reset();
  EXPECT_EQ(copy.scripts->scripts[0].name, "DoorScript");
  EXPECT_EQ(copy.scripts->scripts[0].properties[0].name, d.scripts->scripts[0].properties[0].name);
}

TEST_F(PlacedEnvironmentTest, EmittanceStaysEmptyForAReferenceWithoutXemi) {
  const std::vector<std::filesystem::path> paths{path_};
  const auto env = fo4::env::Environment::load(paths);
  EXPECT_FALSE(fo4::env::decode_placed_reference(env, *env.winner(key(0x809))).emittance.has_value());
}

TEST_F(PlacedEnvironmentTest, DecodesActorAndTrapReferences) {
  const std::vector<std::filesystem::path> paths{path_};
  const auto env = fo4::env::Environment::load(paths);
  const auto actor = fo4::env::decode_placed_reference(env, *env.winner(key(0x801)));
  EXPECT_EQ(actor.signature, fo4::Signature::from_string("ACHR"));
  EXPECT_EQ(actor.level_modifier, 2);
  ASSERT_EQ(actor.patrols.size(), 1u);
  EXPECT_EQ(actor.patrols[0].idle, key(0x921));
  EXPECT_EQ(actor.patrols[0].topic, key(0x922));
  EXPECT_FALSE(actor.map_marker.has_value());

  const auto hazard = fo4::env::decode_placed_reference(env, *env.winner(key(0x802)));
  EXPECT_EQ(hazard.base, key(0x930));
  EXPECT_TRUE(hazard.position.has_value());
}

TEST_F(PlacedEnvironmentTest, LinkPastTheMasterTableResolvesToTheOwningPlugin) {
  const auto path = root_ / "Dangling.esp";
  const auto data = make_plugin(0, {{"REFR", 0x810,
                                     {{"NAME", words({0x900})},
                                      {"XLYR", words({0x02006FCB})},
                                      {"DATA", floats({0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f})}}}});
  std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(data.data()),
                                               static_cast<std::streamsize>(data.size()));
  const std::vector<std::filesystem::path> paths{path};
  const auto env = fo4::env::Environment::load(paths);
  const fo4::FormKey ref{fo4::PluginKey::from_filename("Dangling.esp"), fo4::LocalFormId{0x810}};
  const auto d = fo4::env::decode_placed_reference(env, *env.winner(ref));
  EXPECT_EQ(d.layer, (fo4::FormKey{fo4::PluginKey::from_filename("Dangling.esp"), fo4::LocalFormId{0x006FCB}}));
}

TEST_F(PlacedEnvironmentTest, FailsClosedOnSectionAndSizeErrors) {
  const std::vector<std::filesystem::path> paths{path_};
  const auto env = fo4::env::Environment::load(paths);
  EXPECT_THROW(fo4::env::decode_placed_reference(env, *env.winner(key(0x803))),
               fo4::env::WorldDecodeError);
  EXPECT_THROW(fo4::env::decode_placed_reference(env, *env.winner(key(0x808))),
               fo4::env::WorldDecodeError);
  EXPECT_THROW(fo4::env::decode_placed_reference(env, *env.winner(key(0x805))),
               fo4::env::WorldDecodeError);

  const auto variable = fo4::env::decode_placed_reference(env, *env.winner(key(0x804)));
  ASSERT_TRUE(variable.scripts.has_value());
  EXPECT_TRUE(variable.scripts->raw.unsupported);
  ASSERT_EQ(variable.scripts->scripts.size(), 1u);
  EXPECT_EQ(variable.scripts->scripts[0].properties.size(), 1u);
}

TEST_F(PlacedEnvironmentTest, PrimaryModelPathMatchesModelPathResolver) {
  const std::vector<std::filesystem::path> paths{path_};
  const auto env = fo4::env::Environment::load(paths);
  EXPECT_EQ(fo4::env::primary_model_path(*env.winner(key(0x805))), "Armor\\Female.nif");
  EXPECT_FALSE(fo4::env::primary_model_path(*env.winner(key(0x806))).has_value());
  EXPECT_EQ(fo4::env::primary_model_path(*env.winner(key(0x807))), "Rock.nif");
  EXPECT_FALSE(fo4::env::primary_model_path(*env.winner(key(0x800))).has_value());
  EXPECT_EQ(env.metadata(*env.winner(key(0x805))).model, "Armor\\Female.nif");
}

TEST_F(PlacedEnvironmentTest, PrimaryModelPathIgnoresModlOnRecordsWithoutAModel) {
  const auto path = root_ / "Unmodeled.esp";
  const auto data = make_plugin(0, {{"RACE", 0x810, {{"MODL", zstring("Actors\\Character\\Body.egt")}}},
                                    {"ARMA", 0x811, {{"MODL", words({0x900})}}},
                                    {"WTHR", 0x812, {{"MODL", zstring("Sky\\Storm.nif")}}},
                                    {"LIGH", 0x813, {{"MODL", zstring("Lights\\Bulb.nif")}}}});
  std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(data.data()),
                                               static_cast<std::streamsize>(data.size()));
  const std::vector<std::filesystem::path> paths{path};
  const auto env = fo4::env::Environment::load(paths);
  const auto model = [&](std::uint32_t id) {
    return fo4::env::primary_model_path(
        *env.winner(fo4::FormKey{fo4::PluginKey::from_filename("Unmodeled.esp"), fo4::LocalFormId{id}}));
  };
  EXPECT_FALSE(model(0x810).has_value());
  EXPECT_FALSE(model(0x811).has_value());
  EXPECT_FALSE(model(0x812).has_value());
  EXPECT_EQ(model(0x813), "Lights\\Bulb.nif");
}

TEST_F(PlacedEnvironmentTest, CApiReportsUnexposedScalarObjectWords) {
  for (const auto format : {std::uint16_t{1}, std::uint16_t{2}}) {
    for (const auto word : {std::uint16_t{0}, std::uint16_t{1}, std::uint16_t{0xFFFF}}) {
      SCOPED_TRACE("format=" + std::to_string(format) + " word=" + std::to_string(word));
      const auto bytes = make_plugin(0, {{"REFR", 0x800, {{"VMAD", make_vmad(false, format, word)}}}});
      {
        std::ofstream file(path_, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
      }
      const auto utf8 = path_.u8string();
      const std::string path(utf8.begin(), utf8.end());
      const char* raw[] = {path.c_str()};
      radicle_env* pointer = nullptr;
      ASSERT_EQ(radicle_env_load(raw, 1, &pointer), RADICLE_STATUS_OK) << radicle_last_error();
      const std::unique_ptr<radicle_env, decltype(&radicle_env_destroy)> env(pointer, radicle_env_destroy);
      const radicle_record* record = nullptr;
      ASSERT_EQ(radicle_env_winner(env.get(), {"Placed.esp", 0x800}, &record), RADICLE_STATUS_OK);
      radicle_placed_ref* decoded = nullptr;
      ASSERT_EQ(radicle_placed_ref_decode(env.get(), record, &decoded), RADICLE_STATUS_OK) << radicle_last_error();
      const std::unique_ptr<radicle_placed_ref, decltype(&radicle_placed_ref_free)> ref(decoded, radicle_placed_ref_free);
      EXPECT_EQ(ref->script_object_format, format);
      EXPECT_EQ(ref->scripts_unsupported, word == 0 ? 0 : 1);
      ASSERT_EQ(ref->script_count, 1u);
      EXPECT_EQ(ref->scripts[0].property_count, word == 0 ? 5u : 0u);
      if (word == 0) {
        ASSERT_EQ(ref->scripts[0].property_count, 5u);
        EXPECT_EQ(ref->scripts[0].properties[0].object.local_id, 0x805u);
        EXPECT_EQ(ref->scripts[0].properties[0].alias, -1);
      }
    }
  }
}

TEST_F(PlacedEnvironmentTest, CApiDecodesPlacedReferences) {
  const auto utf8 = path_.u8string();
  const std::string path(utf8.begin(), utf8.end());
  const char* raw[] = {path.c_str()};
  radicle_env* env = nullptr;
  ASSERT_EQ(radicle_env_load(raw, 1, &env), RADICLE_STATUS_OK) << radicle_last_error();

  const radicle_record* record = nullptr;
  ASSERT_EQ(radicle_env_winner(env, {"Placed.esp", 0x800}, &record), RADICLE_STATUS_OK);
  radicle_placed_ref* ref = nullptr;
  ASSERT_EQ(radicle_placed_ref_decode(env, record, &ref), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_STREQ(ref->signature, "REFR");
  EXPECT_STREQ(ref->base.plugin, "Placed.esp");
  EXPECT_EQ(ref->base.local_id, 0x900u);
  EXPECT_EQ(ref->has_placement, 1);
  EXPECT_EQ(ref->position[0], 100.0f);
  EXPECT_EQ(ref->has_scale, 1);
  EXPECT_EQ(ref->location_ref_type_count, 2u);
  EXPECT_EQ(ref->has_emittance, 1);
  EXPECT_STREQ(ref->emittance.plugin, "Placed.esp");
  EXPECT_EQ(ref->emittance.local_id, 0x917u);
  EXPECT_EQ(ref->linked_ref_count, 2u);
  EXPECT_EQ(ref->linked_refs[1].keyword.local_id, 0x913u);
  EXPECT_EQ(ref->linked_refs[1].reference.plugin, nullptr);
  EXPECT_EQ(ref->has_no_crime, 1);
  EXPECT_EQ(ref->no_crime, 1);
  EXPECT_STREQ(ref->comments, "placed by test");
  EXPECT_EQ(ref->map_marker_name_kind, RADICLE_NAME_INLINE);
  EXPECT_STREQ(ref->map_marker_name_text, "Sanctuary");
  EXPECT_EQ(ref->teleport_transition_interior.local_id, 0x908u);
  EXPECT_EQ(ref->linked_room_count, 2u);
  EXPECT_EQ(ref->room_image_space.local_id, 0x904u);
  ASSERT_EQ(ref->patrol_count, 1u);
  ASSERT_EQ(ref->patrols[0].topic_count, 2u);
  EXPECT_EQ(ref->patrols[0].topics[0].topic.local_id, 0x915u);
  ASSERT_EQ(ref->script_count, 1u);
  EXPECT_STREQ(ref->scripts[0].name, "DoorScript");
  EXPECT_EQ(ref->scripts_unsupported, 0);
  ASSERT_EQ(ref->scripts[0].property_count, 5u);
  EXPECT_EQ(ref->scripts[0].properties[0].object.local_id, 0x805u);
  EXPECT_STREQ(ref->scripts[0].properties[1].name, "Caf\xC3\xA9");
  EXPECT_STREQ(ref->scripts[0].properties[1].text, "Caf\xC3\xA9");
  EXPECT_EQ(ref->scripts[0].properties[2].has_integer, 1);
  EXPECT_EQ(ref->scripts[0].properties[3].type, 16);
  EXPECT_EQ(ref->scripts[0].properties[3].has_element_count, 1);
  EXPECT_EQ(ref->scripts[0].properties[3].element_count, 0u);
  EXPECT_EQ(ref->scripts[0].properties[4].element_count, 2u);
  radicle_placed_ref_free(ref);

  ASSERT_EQ(radicle_env_winner(env, {"Placed.esp", 0x803}, &record), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_placed_ref_decode(env, record, &ref), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(ref, nullptr);

  const char* model = nullptr;
  ASSERT_EQ(radicle_env_winner(env, {"Placed.esp", 0x805}, &record), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_record_primary_model_path(env, record, &model), RADICLE_STATUS_OK);
  EXPECT_STREQ(model, "Armor\\Female.nif");
  ASSERT_EQ(radicle_env_winner(env, {"Placed.esp", 0x806}, &record), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_record_primary_model_path(env, record, &model), RADICLE_STATUS_NOT_FOUND);
  radicle_placed_ref_free(nullptr);
  radicle_env_destroy(env);
}
