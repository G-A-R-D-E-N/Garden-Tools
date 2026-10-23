#include "fo4/environment/base_records.hpp"
#include "fo4/environment/environment.hpp"
#include "fo4/forms/weather.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/subrecord_authoring.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/preflight.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using Subs = std::vector<std::pair<std::string, Bytes>>;

class ClimateWeatherReal : public testing::TestWithParam<const char*> {};

bool is_sky(fo4::Signature signature) {
  return signature == fo4::Signature::from_string("CLMT") || signature == fo4::Signature::from_string("WTHR");
}

fo4::Signature sig(const char* text) { return fo4::Signature::from_string(text); }

std::filesystem::path official(const char* name, bool& skip) {
  const auto* dir = std::getenv("FO4_DATA_DIR");
  skip = dir == nullptr || !std::filesystem::exists(std::filesystem::path{dir} / name);
  return skip ? std::filesystem::path{} : std::filesystem::path{dir} / name;
}

Subs subs_of(const fo4::Plugin& plugin, const fo4::FormKey& key) {
  Subs out;
  if (const auto* record = fo4::find_record(plugin, key)) {
    for (const auto& sub : record->subrecords) { out.emplace_back(sub.signature.to_string(), sub.data); }
  }
  return out;
}

Subs without(Subs subs, const char* name) {
  subs.erase(std::remove_if(subs.begin(), subs.end(), [&](const auto& sub) { return sub.first == name; }), subs.end());
  return subs;
}

std::map<fo4::FormKey, Bytes> snapshot(const fo4::Plugin& plugin) {
  std::map<fo4::FormKey, Bytes> out;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    if (record.header.signature == fo4::Signature::from_string("TES4")) { return; }
    out[record.form_key] = fo4::test::record_original_bytes(plugin, record);
  });
  return out;
}

const std::map<std::string, fo4::FormKey>& base_targets(const std::filesystem::path& data) {
  static const auto targets = [&] {
    std::map<std::string, fo4::FormKey> out;
    const auto base = fo4::read_plugin_file(data / "Fallout4.esm");
    fo4::visit_records(base, [&](const fo4::Record& record) {
      const auto type = record.header.signature.to_string();
      if ((type == "WTHR" || type == "SNDR" || type == "STAT" || type == "IMGS") && !out.count(type)) { out[type] = record.form_key; }
    });
    return out;
  }();
  return targets;
}

}

TEST_P(ClimateWeatherReal, EveryOfficialRecordIsListedOrderedAndPreflightClean) {
  bool skip = false;
  const auto path = official(GetParam(), skip);
  if (skip) GTEST_SKIP() << "set FO4_DATA_DIR to installed Fallout 4 data";
  record_game_build(detect_game_build(path.parent_path()));
  const auto plugin = fo4::read_plugin_file(path);
  std::map<std::string, int> totals;
  std::vector<std::string> violators;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    if (!is_sky(record.header.signature)) { return; }
    ++totals[record.header.signature.to_string()];
    const auto rules = fo4::schema::authoring_subrecords(*fo4::schema::record_authoring_constraint(record.header.signature));
    for (const auto& sub : record.subrecords) {
      const auto listed = std::any_of(rules.begin(), rules.end(), [&](const auto& rule) { return rule.signature == sub.signature; });
      EXPECT_TRUE(listed) << record.form_key.to_string() << ' ' << sub.signature.to_string();
    }
    for (const auto& issue : fo4::schema::preflight_authoring_record(plugin, record.form_key)) {
      if (issue.severity == fo4::schema::PreflightSeverity::error) { violators.push_back(record.form_key.to_string() + ' ' + issue.path); }
    }
    for (const auto& issue : fo4::schema::preflight_record(record)) {
      EXPECT_NE(issue.severity, fo4::schema::PreflightSeverity::error) << record.form_key.to_string() << ' ' << issue.path << ' ' << issue.message;
    }
  });
  EXPECT_TRUE(violators.empty()) << violators.front();
  for (const auto& [type, count] : totals) { RecordProperty(type, count); }
  RecordProperty("order_violations", static_cast<int>(violators.size()));
}

TEST_P(ClimateWeatherReal, EveryScalarAndStringPathEditsFirstAndLastSlotsWithoutTouchingOtherBytes) {
  bool skip = false;
  const auto path = official(GetParam(), skip);
  if (skip) GTEST_SKIP() << "set FO4_DATA_DIR to installed Fallout 4 data";
  record_game_build(detect_game_build(path.parent_path()));
  const auto full = fo4::read_plugin_file(path);
  auto sample = fo4::make_empty_plugin(full.key.filename());
  sample.header = full.header;
  sample.tes4 = full.tes4;
  sample.original_data = full.original_data;
  std::map<std::string, fo4::Group> groups;
  fo4::visit_records(full, [&](const fo4::Record& record) {
    if (!is_sky(record.header.signature)) { return; }
    const auto type = record.header.signature.to_string();
    auto& group = groups[type];
    group.header.label = {std::byte{static_cast<unsigned char>(type[0])}, std::byte{static_cast<unsigned char>(type[1])},
                          std::byte{static_cast<unsigned char>(type[2])}, std::byte{static_cast<unsigned char>(type[3])}};
    group.children.emplace_back(record);
  });
  for (auto& [type, group] : groups) { sample.groups.push_back(std::move(group)); }
  sample = fo4::read_plugin(fo4::write_plugin(sample), path);
  fo4::assign_record_form_keys(sample);
  const auto original = sample;
  std::map<fo4::FormKey, std::vector<Bytes>> expected;
  std::map<std::string, int> edits;
  fo4::visit_records(original, [&](const fo4::Record& record) {
    if (!is_sky(record.header.signature)) { return; }
    auto& want = expected[record.form_key];
    for (const auto& sub : record.subrecords) { want.push_back(sub.data); }
    for (const auto& field : fo4::schema::scalar_fields(record.header.signature)) {
      SCOPED_TRACE(record.form_key.to_string() + ' ' + std::string(field.path));
      const auto slots = fo4::schema::resolve_field_slots(record, fo4::schema::scalar_field_shape(field.scalar), field.scalar.address);
      std::set<std::size_t> picks;
      if (!slots.slots.empty()) { picks = {0, slots.slots.size() - 1}; }
      for (const auto index : picks) {
        const auto& slot = slots.slots[index];
        if (!slot.valid || slot.misfit) {
          EXPECT_EQ(fo4::set_record_scalar(sample, record.form_key, field, index, "1"), fo4::ScalarMutationStatus::subrecord_missing);
          continue;
        }
        if (field.scalar.storage == fo4::schema::ScalarStorage::zstring) {
          ASSERT_EQ(fo4::set_record_string(sample, record.form_key, field, index, "radicle\\edit.dds"), fo4::ScalarMutationStatus::updated);
          const std::string_view stored{"radicle\\edit.dds"};
          Bytes payload(reinterpret_cast<const std::byte*>(stored.data()), reinterpret_cast<const std::byte*>(stored.data()) + stored.size());
          payload.push_back(std::byte{0});
          want[slot.subrecord] = payload;
        } else {
          const auto existing = fo4::schema::scalar_slot_bytes(record.subrecords[slot.subrecord].data, slot, field.scalar);
          const std::string value = fo4::schema::encode_scalar("1", field.scalar) == Bytes(existing.begin(), existing.end()) ? "2" : "1";
          ASSERT_EQ(fo4::set_record_scalar(sample, record.form_key, field, index, value), fo4::ScalarMutationStatus::updated);
          const auto bytes = fo4::schema::encode_scalar(value, field.scalar);
          std::copy(bytes.begin(), bytes.end(), want[slot.subrecord].begin() + static_cast<std::ptrdiff_t>(slot.offset));
        }
        ++edits[std::string(field.path)];
      }
    }
  });
  const auto reread = fo4::read_plugin(fo4::write_plugin(sample), path);
  if (expected.empty()) { EXPECT_EQ(fo4::write_plugin(sample), fo4::write_plugin(original)); }
  int checked = 0;
  for (const auto& [key, want] : expected) {
    SCOPED_TRACE(key.to_string());
    const auto* after = fo4::find_record(reread, key);
    const auto* before = fo4::find_record(original, key);
    ASSERT_NE(after, nullptr);
    ASSERT_EQ(after->subrecords.size(), want.size());
    for (std::size_t i = 0; i < want.size(); ++i) {
      EXPECT_EQ(after->subrecords[i].signature, before->subrecords[i].signature);
      EXPECT_EQ(after->subrecords[i].data, want[i]) << before->subrecords[i].signature.to_string() << ' ' << i;
    }
    if (after->header.signature == sig("WTHR")) {
      EXPECT_NO_THROW(static_cast<void>(fo4::forms::decode_weather(reread, key)));
    }
    ++checked;
  }
  int total = 0;
  for (const auto& [name, count] : edits) { total += count; }
  RecordProperty("records", checked);
  RecordProperty("paths", static_cast<int>(edits.size()));
  RecordProperty("edits", total);
}

TEST_P(ClimateWeatherReal, LinksAndOptionalSubrecordsEditOfficialRecordsAndKeepEveryOtherRecordByteIdentical) {
  bool skip = false;
  const auto path = official(GetParam(), skip);
  if (skip) GTEST_SKIP() << "set FO4_DATA_DIR to installed Fallout 4 data";
  record_game_build(detect_game_build(path.parent_path()));
  const auto& pick = base_targets(path.parent_path());
  ASSERT_EQ(pick.size(), 4u);
  auto plugin = fo4::read_plugin_file(path);
  fo4::assign_record_form_keys(plugin);
  const auto original = snapshot(plugin);
  const auto field = [](const char* name) { return *fo4::schema::formlink_field_descriptor(name); };
  std::vector<std::pair<std::string, fo4::FormKey>> targets;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    if (is_sky(record.header.signature)) { targets.emplace_back(record.header.signature.to_string(), record.form_key); }
  });
  const auto untouched_bytes = targets.empty() ? fo4::write_plugin(plugin) : Bytes{};
  const auto changed = [](fo4::FormLinkFieldMutationStatus status) {
    return status == fo4::FormLinkFieldMutationStatus::updated || status == fo4::FormLinkFieldMutationStatus::unchanged;
  };
  int edits = 0, added = 0;
  for (const auto& [type, record] : targets) {
    SCOPED_TRACE(record.to_string());
    const auto start = subs_of(plugin, record);
    const auto has = [&](const char* name) { return std::any_of(start.begin(), start.end(), [&](const auto& sub) { return sub.first == name; }); };
    if (type == "CLMT") {
      EXPECT_TRUE(changed(fo4::set_record_formlink_field(plugin, record, field("CLMT\\Weather"), 0, pick.at("WTHR"))));
      EXPECT_TRUE(changed(fo4::set_record_formlink_field(plugin, record, field("CLMT\\WeatherGlobal"), 0, fo4::FormKey{})));
      edits += 2;
      if (!has("FNAM")) {
        ASSERT_EQ(fo4::add_record_subrecord(plugin, record, sig("FNAM")), fo4::SubrecordAddStatus::added);
        ASSERT_EQ(fo4::set_record_string(plugin, record, "CLMT\\SunTexture", 0, "sky\\sun.dds"), fo4::ScalarMutationStatus::updated);
        ++added;
      }
      EXPECT_EQ(fo4::remove_record_subrecord(plugin, record, sig("TNAM"), 0), fo4::SubrecordRemoveStatus::below_minimum);
      EXPECT_EQ(fo4::add_record_subrecord(plugin, record, sig("MODL")), fo4::SubrecordAddStatus::not_authorable);
      continue;
    }
    const auto statics = *fo4::record_field_count(plugin, record, "WTHR\\SkyStatic");
    if (statics > 0) {
      EXPECT_TRUE(changed(fo4::set_record_formlink_field(plugin, record, field("WTHR\\SkyStatic"), statics - 1, pick.at("STAT"))));
      ++edits;
    }
    const auto spaces = *fo4::record_field_count(plugin, record, "WTHR\\ImageSpace");
    ASSERT_TRUE(spaces == 4u || spaces == 8u);
    EXPECT_TRUE(changed(fo4::set_record_formlink_field(plugin, record, field("WTHR\\ImageSpace"), spaces - 1, pick.at("IMGS"))));
    EXPECT_EQ(fo4::set_record_formlink_field(plugin, record, field("WTHR\\ImageSpace"), spaces, pick.at("IMGS")),
              fo4::FormLinkFieldMutationStatus::subrecord_missing);
    EXPECT_TRUE(changed(fo4::set_record_formlink_field(plugin, record, field("WTHR\\Precipitation"), 0, fo4::FormKey{})));
    edits += 2;
    const auto sounds = *fo4::record_field_count(plugin, record, "WTHR\\Sound");
    const auto before_add = subs_of(plugin, record);
    ASSERT_EQ(fo4::add_record_subrecord(plugin, record, sig("TNAM")), fo4::SubrecordAddStatus::added);
    ASSERT_EQ(fo4::remove_record_subrecord(plugin, record, sig("TNAM"), statics), fo4::SubrecordRemoveStatus::removed);
    EXPECT_EQ(subs_of(plugin, record), before_add);
    ASSERT_EQ(fo4::add_record_subrecord(plugin, record, sig("SNAM")), fo4::SubrecordAddStatus::added);
    ASSERT_EQ(fo4::set_record_formlink_field(plugin, record, field("WTHR\\Sound"), sounds, pick.at("SNDR")), fo4::FormLinkFieldMutationStatus::updated);
    ASSERT_EQ(fo4::set_record_scalar(plugin, record, *fo4::schema::scalar_field_descriptor("WTHR\\SoundType"), sounds, "1"),
              fo4::ScalarMutationStatus::updated);
    ++added;
    EXPECT_EQ(fo4::remove_record_subrecord(plugin, record, sig("DALC"), 0), fo4::SubrecordRemoveStatus::not_authorable);
    EXPECT_EQ(fo4::remove_record_subrecord(plugin, record, sig("IMSP"), 0), fo4::SubrecordRemoveStatus::below_minimum);
  }

  const auto bytes = fo4::write_plugin(plugin);
  if (targets.empty()) { EXPECT_EQ(bytes, untouched_bytes); }
  auto reread = fo4::read_plugin(bytes, path);
  fo4::assign_record_form_keys(reread);
  const auto after = snapshot(reread);
  std::set<fo4::FormKey> touched;
  for (const auto& [type, record] : targets) { touched.insert(record); }
  for (const auto& [record, data] : original) {
    if (touched.count(record)) { continue; }
    ASSERT_TRUE(after.count(record)) << record.to_string();
    EXPECT_EQ(after.at(record), data) << record.to_string();
  }
  const std::filesystem::path paths[] = {path};
  const auto env = fo4::env::Environment::load(paths);
  for (const auto& [type, record] : targets) {
    SCOPED_TRACE(record.to_string());
    EXPECT_EQ(subs_of(reread, record), subs_of(plugin, record));
    for (const auto& issue : fo4::schema::preflight_authoring_record(reread, record)) {
      EXPECT_NE(issue.severity, fo4::schema::PreflightSeverity::error) << issue.path;
    }
    const auto* stored = fo4::find_record(reread, record);
    if (type == "CLMT") {
      const auto climate = fo4::env::decode_climate(env, fo4::env::RecordRef{0, stored});
      ASSERT_FALSE(climate.weathers.empty());
      EXPECT_EQ(climate.weathers[0].weather, pick.at("WTHR"));
      EXPECT_TRUE(climate.weathers[0].global.owner().empty());
      EXPECT_TRUE(climate.timing.has_value());
    } else {
      const auto weather = fo4::forms::decode_weather(reread, record);
      ASSERT_FALSE(weather.snam.empty());
      EXPECT_EQ(weather.snam.back().sound, pick.at("SNDR"));
      EXPECT_EQ(weather.snam.back().type, 1u);
      EXPECT_EQ(weather.imsp[weather.imsp_count - 1u], pick.at("IMGS"));
      EXPECT_EQ(weather.mnam, std::nullopt);
    }
  }
  RecordProperty("link_edits", edits);
  RecordProperty("subrecords_added", added);
  RecordProperty("records_edited", static_cast<int>(targets.size()));
}

TEST(ClimateWeatherEditReal, Fallout4ClimateAndWeatherEditsReDecodeAfterWriteAndReload) {
  bool skip = false;
  const auto path = official("Fallout4.esm", skip);
  if (skip) GTEST_SKIP() << "set FO4_DATA_DIR to installed Fallout 4 data";
  record_game_build(detect_game_build(path.parent_path()));
  auto plugin = fo4::read_plugin_file(path);
  std::optional<fo4::FormKey> climate;
  std::optional<fo4::FormKey> weather;
  std::size_t fnam_count = 0;
  std::size_t time_slots = 0;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    const auto type = record.header.signature;
    if (!climate && type == sig("CLMT")) { climate = record.form_key; }
    if (!weather && type == sig("WTHR")) {
      weather = record.form_key;
      fnam_count = fo4::forms::weather_fnam_size(record.header.form_version) / 4;
      time_slots = fo4::forms::weather_time_slots(record.header.form_version);
    }
  });
  ASSERT_TRUE(climate && weather);

  const auto climate_before = subs_of(plugin, *climate);
  const auto weather_before = subs_of(plugin, *weather);
  const std::vector<fo4::env::ClimateWeather> weathers{{*weather, 42, {}}};
  const std::vector<fo4::env::ByteColor> colours(time_slots, fo4::env::ByteColor{9, 8, 7});
  const std::vector<float> fog(fnam_count, 3.5f);
  ASSERT_TRUE(fo4::env::set_climate_weathers(plugin, *climate, weathers));
  ASSERT_TRUE(fo4::env::set_weather_colors(plugin, *weather, fo4::env::WeatherColorGroup::sun_glare, colours));
  ASSERT_TRUE(fo4::env::set_weather_fog(plugin, *weather, fog));
  ASSERT_TRUE(fo4::env::set_weather_transition(plugin, *weather, 77));

  auto reread = fo4::read_plugin(fo4::write_plugin(plugin), path);
  fo4::assign_record_form_keys(reread);
  EXPECT_EQ(without(subs_of(reread, *climate), "WLST"), without(climate_before, "WLST"));
  EXPECT_EQ(without(without(without(subs_of(reread, *weather), "NAM0"), "FNAM"), "DATA"),
            without(without(without(weather_before, "NAM0"), "FNAM"), "DATA"));

  const auto env = fo4::env::Environment::from_plugins(std::vector<fo4::Plugin>{std::move(reread)});
  const auto* climate_ref = env.winner(*climate);
  const auto* weather_ref = env.winner(*weather);
  ASSERT_NE(climate_ref, nullptr);
  ASSERT_NE(weather_ref, nullptr);
  const auto decoded_climate = fo4::env::decode_climate(env, *climate_ref);
  ASSERT_EQ(decoded_climate.weathers.size(), 1u);
  EXPECT_EQ(decoded_climate.weathers[0].weather, *weather);
  EXPECT_EQ(decoded_climate.weathers[0].chance, 42);
  const auto decoded = fo4::env::decode_weather(env, *weather_ref);
  ASSERT_TRUE(decoded.colors.has_value());
  const auto color = decoded.colors->color(fo4::env::WeatherColorGroup::sun_glare, fo4::env::k_weather_day);
  ASSERT_TRUE(color.has_value());
  EXPECT_EQ(color->red, 9);
  EXPECT_EQ(decoded.fog_distances, fog);
  ASSERT_TRUE(decoded.parameters.has_value());
  EXPECT_EQ(decoded.parameters->transition_delta, 77);
}

INSTANTIATE_TEST_SUITE_P(OfficialMasters, ClimateWeatherReal,
    testing::Values("Fallout4.esm", "DLCRobot.esm", "DLCworkshop01.esm", "DLCCoast.esm", "DLCworkshop02.esm", "DLCworkshop03.esm", "DLCNukaWorld.esm"));
