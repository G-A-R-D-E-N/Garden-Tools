#include "fo4/c_api_env.h"
#include "fo4/plugin/reader.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr std::uint32_t k_flst = 0x01000810;
constexpr std::uint32_t k_refr = 0x01000820;
constexpr std::uint32_t k_expl = 0x01000830;
constexpr std::uint32_t k_race = 0x01000840;

std::vector<std::byte> words(std::initializer_list<std::uint32_t> values) {
  fo4::test::ByteWriter out;
  for (const auto value : values) out.u32(value);
  return out.bytes;
}

std::vector<std::byte> zstr(std::string_view value) {
  std::vector<std::byte> out;
  for (const char ch : value) out.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  out.push_back(std::byte{0});
  return out;
}

void add_record(fo4::test::ByteWriter& out,
                std::string_view signature,
                std::uint32_t form_id,
                const std::vector<std::pair<std::string, std::vector<std::byte>>>& subs) {
  const auto group = fo4::test::begin_group(out, signature, 0);
  const auto record = fo4::test::begin_record(out, signature, 0, form_id);
  for (const auto& [name, data] : subs) fo4::test::write_subrecord(out, name, data);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
}

std::vector<std::byte> make_plugin() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(5);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view("Master.esm\0", 11));
  fo4::test::write_subrecord(out, "DATA", words({0, 0}));
  fo4::test::end_record(out, tes4);

  add_record(out, "FLST", k_flst, {{"EDID", zstr("List")}, {"LNAM", words({0x801})}, {"LNAM", words({0x802})}});
  add_record(out, "REFR", k_refr, {{"XHLT", words({50})}});
  std::vector<std::byte> data(84, std::byte{0});
  data[0] = std::byte{0x01};
  data[1] = std::byte{0x08};
  add_record(out, "EXPL", k_expl, {{"DATA", data}});
  add_record(out, "RACE", k_race,
             {{"RPRM", words({0x801})}, {"NAME", zstr("Hand")}, {"NAME", zstr("Foot")}, {"HNAM", zstr("hair.dds")}});
  return out.bytes;
}

class CApiFields : public ::testing::Test {
protected:
  void SetUp() override {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    root_ = std::filesystem::temp_directory_path() / ("radicle_c_fields_" + std::to_string(id));
    std::filesystem::create_directories(root_);
    path_ = root_ / "Fields.esp";
    const auto bytes = make_plugin();
    std::ofstream stream(path_, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    stream.close();
    ASSERT_EQ(radicle_plugin_read_file(path_.string().c_str(), &plugin_), RADICLE_STATUS_OK) << radicle_last_error();
  }

  void TearDown() override {
    radicle_plugin_destroy(plugin_);
    std::error_code error;
    std::filesystem::remove_all(root_, error);
  }

  static radicle_form_key key(std::uint32_t form_id) { return {"Fields.esp", form_id & 0xFFFFFF}; }

  fo4::Plugin saved() {
    const auto out = root_ / "Saved.esp";
    EXPECT_EQ(radicle_plugin_write_file(plugin_, out.string().c_str()), RADICLE_STATUS_OK) << radicle_last_error();
    return fo4::read_plugin_file(out);
  }

  static std::vector<std::vector<std::byte>> subs(const fo4::Plugin& plugin, std::uint32_t form_id, std::string_view sig) {
    std::vector<std::vector<std::byte>> out;
    const auto* record = fo4::find_record(plugin, fo4::FormKey{plugin.key, fo4::LocalFormId{form_id & 0xFFFFFF}});
    if (record == nullptr) return out;
    for (const auto& sub : record->subrecords) {
      if (sub.signature.to_string() == sig) out.push_back(sub.data);
    }
    return out;
  }

  std::filesystem::path root_;
  std::filesystem::path path_;
  radicle_plugin* plugin_{nullptr};
};

}

TEST_F(CApiFields, ScalarExportsMapStatusesAndSurviveSaveReload) {
  EXPECT_EQ(radicle_plugin_set_record_scalar(plugin_, key(k_refr), "REFR\\HealthPercent", "75"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_record_scalar_at(plugin_, key(k_refr), "REFR\\HealthPercent", 0, "80"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_record_scalar_at(plugin_, key(k_refr), "REFR\\HealthPercent", 0, "80"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_record_scalar_at(plugin_, key(k_expl), "EXPL\\Damage", 0, "40"), RADICLE_STATUS_OK);

  EXPECT_EQ(radicle_plugin_set_record_scalar_at(plugin_, key(k_refr), "REFR\\HealthPercent", 1, "80"), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_record_scalar_at(plugin_, key(k_refr), "REFR\\Scale", 0, "1.5"), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_record_scalar_at(plugin_, key(0x999), "REFR\\Scale", 0, "1.5"), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_record_scalar(plugin_, key(0x999), "REFR\\Scale", "1.5"), RADICLE_STATUS_NOT_FOUND);

  EXPECT_EQ(radicle_plugin_set_record_scalar_at(plugin_, key(k_refr), "REFR\\Nope", 0, "1"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_scalar_at(plugin_, key(k_expl), "REFR\\HealthPercent", 0, "1"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_scalar_at(plugin_, key(k_refr), "REFR\\HealthPercent", 0, "abc"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_scalar_at(plugin_, key(k_refr), "REFR\\HealthPercent", 0, "150"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_scalar_at(plugin_, key(k_race), "RACE\\BipedObjectNames", 0, "x"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_scalar_at(nullptr, key(k_refr), "REFR\\HealthPercent", 0, "1"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_scalar_at(plugin_, {nullptr, 1}, "REFR\\HealthPercent", 0, "1"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_scalar_at(plugin_, key(k_refr), nullptr, 0, "1"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_scalar_at(plugin_, key(k_refr), "REFR\\HealthPercent", 0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_scalar(plugin_, key(k_refr), "REFR\\Nope", "1"), RADICLE_STATUS_INVALID_ARGUMENT);

  const auto reloaded = saved();
  EXPECT_EQ(subs(reloaded, k_refr, "XHLT").at(0), words({80}));
  const auto damage = subs(reloaded, k_expl, "DATA").at(0);
  ASSERT_EQ(damage.size(), 84u);
  EXPECT_EQ(damage[28], std::byte{0x00});
  EXPECT_EQ(damage[29], std::byte{0x00});
  EXPECT_EQ(damage[30], std::byte{0x20});
  EXPECT_EQ(damage[31], std::byte{0x42});
}

TEST_F(CApiFields, FormLinkExportMapsStatusesAndSurvivesSaveReload) {
  const radicle_form_key target{"Master.esm", 0x900};
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin_, key(k_expl), "EXPL\\Light", 0, &target), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin_, key(k_expl), "EXPL\\Light", 0, &target), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin_, key(k_expl), "EXPL\\Sound1", 0, nullptr), RADICLE_STATUS_OK);

  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin_, key(k_expl), "EXPL\\Light", 1, &target), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin_, key(0x999), "EXPL\\Light", 0, &target), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin_, key(k_expl), "EXPL\\Nope", 0, &target), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin_, key(k_refr), "EXPL\\Light", 0, &target), RADICLE_STATUS_INVALID_ARGUMENT);

  const radicle_form_key unknown_master{"Other.esm", 0x900};
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin_, key(k_expl), "EXPL\\Light", 0, &unknown_master), RADICLE_STATUS_INVALID_ARGUMENT);
  const radicle_form_key bad_key{nullptr, 0x900};
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin_, key(k_expl), "EXPL\\Light", 0, &bad_key), RADICLE_STATUS_INVALID_ARGUMENT);
  const auto local_wrong_type = key(k_flst);
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin_, key(k_expl), "EXPL\\Light", 0, &local_wrong_type), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_formlink(nullptr, key(k_expl), "EXPL\\Light", 0, &target), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin_, key(k_expl), nullptr, 0, &target), RADICLE_STATUS_INVALID_ARGUMENT);

  const auto reloaded = saved();
  const auto data = subs(reloaded, k_expl, "DATA").at(0);
  EXPECT_EQ(data[0], std::byte{0x00});
  EXPECT_EQ(data[1], std::byte{0x09});
  EXPECT_EQ(data[4], std::byte{0});
  EXPECT_EQ(data[5], std::byte{0});
}

TEST_F(CApiFields, StringExportMapsStatusesAndSurvivesSaveReload) {
  EXPECT_EQ(radicle_plugin_set_record_string(plugin_, key(k_race), "RACE\\BipedObjectNames", 1, "Torso"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_record_string(plugin_, key(k_race), "RACE\\BipedObjectNames", 1, "Torso"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_record_string(plugin_, key(k_race), "RACE\\HairColorLookupTexture", 0, "new.dds"), RADICLE_STATUS_OK);

  EXPECT_EQ(radicle_plugin_set_record_string(plugin_, key(k_race), "RACE\\BipedObjectNames", 2, "x"), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_record_string(plugin_, key(k_race), "RACE\\HairColorExtendedLookupTexture", 0, "x"), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_record_string(plugin_, key(0x999), "RACE\\BipedObjectNames", 0, "x"), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_record_string(plugin_, key(k_race), "RACE\\Nope", 0, "x"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_string(plugin_, key(k_refr), "RACE\\BipedObjectNames", 0, "x"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_string(plugin_, key(k_refr), "REFR\\HealthPercent", 0, "x"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_string(plugin_, key(k_race), "RACE\\BipedObjectNames", 0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_string(nullptr, key(k_race), "RACE\\BipedObjectNames", 0, "x"), RADICLE_STATUS_INVALID_ARGUMENT);

  const auto reloaded = saved();
  const auto names = subs(reloaded, k_race, "NAME");
  ASSERT_EQ(names.size(), 2u);
  EXPECT_EQ(names[0], zstr("Hand"));
  EXPECT_EQ(names[1], zstr("Torso"));
  EXPECT_EQ(subs(reloaded, k_race, "HNAM").at(0), zstr("new.dds"));
}

TEST_F(CApiFields, StringExportConvertsUtf8ToWindows1252LikeEditorIds) {
  EXPECT_EQ(radicle_plugin_set_record_string(plugin_, key(k_race), "RACE\\HairColorLookupTexture", 0, "caf\xC3\xA9\xE2\x82\xAC"),
            RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_record_string(plugin_, key(k_race), "RACE\\HairColorLookupTexture", 0, "\xC4\x80"),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_string(plugin_, key(k_race), "RACE\\HairColorLookupTexture", 0, "\xFF"),
            RADICLE_STATUS_INVALID_ARGUMENT);
  const auto reloaded = saved();
  std::vector<std::byte> expected;
  for (const int ch : std::initializer_list<int>{'c', 'a', 'f', 0xE9, 0x80, 0}) expected.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  EXPECT_EQ(subs(reloaded, k_race, "HNAM").at(0), expected);
}

TEST_F(CApiFields, FieldCountReportsOccurrencesAndMapsStatuses) {
  size_t count = 99;
  EXPECT_EQ(radicle_plugin_record_field_count(plugin_, key(k_race), "RACE\\BipedObjectNames", &count), RADICLE_STATUS_OK);
  EXPECT_EQ(count, 2u);
  EXPECT_EQ(radicle_plugin_record_field_count(plugin_, key(k_expl), "EXPL\\Light", &count), RADICLE_STATUS_OK);
  EXPECT_EQ(count, 1u);
  EXPECT_EQ(radicle_plugin_record_field_count(plugin_, key(k_refr), "REFR\\Scale", &count), RADICLE_STATUS_OK);
  EXPECT_EQ(count, 0u);
  EXPECT_EQ(radicle_plugin_record_field_count(plugin_, key(k_race), "RACE\\MalePresets", &count), RADICLE_STATUS_OK);
  EXPECT_EQ(count, 1u);

  EXPECT_EQ(radicle_plugin_record_field_count(plugin_, key(0x999), "REFR\\Scale", &count), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(count, 0u);
  EXPECT_EQ(radicle_plugin_record_field_count(plugin_, key(k_refr), "REFR\\Nope", &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_record_field_count(plugin_, key(k_refr), "EXPL\\Force", &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_record_field_count(plugin_, key(k_refr), "REFR\\Scale", nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_record_field_count(nullptr, key(k_refr), "REFR\\Scale", &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_record_field_count(plugin_, key(k_refr), nullptr, &count), RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST_F(CApiFields, FormLinkListExportsEditByPathAndSurviveSaveReload) {
  size_t count = 0;
  EXPECT_EQ(radicle_plugin_formlink_list_size(plugin_, key(k_flst), "FLST\\Items", &count), RADICLE_STATUS_OK);
  EXPECT_EQ(count, 2u);

  const radicle_form_key added{"Master.esm", 0x900};
  const radicle_form_key replacement{"Master.esm", 0x901};
  EXPECT_EQ(radicle_plugin_formlink_list_insert(plugin_, key(k_flst), "FLST\\Items", 1, &added), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_formlink_list_set(plugin_, key(k_flst), "FLST\\Items", 2, &replacement), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_formlink_list_set(plugin_, key(k_flst), "FLST\\Items", 2, &replacement), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_formlink_list_size(plugin_, key(k_flst), "FLST\\Items", &count), RADICLE_STATUS_OK);
  EXPECT_EQ(count, 3u);

  EXPECT_EQ(radicle_plugin_formlink_list_insert(plugin_, key(k_flst), "FLST\\Items", 9, &added), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_formlink_list_set(plugin_, key(k_flst), "FLST\\Items", 9, &added), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_formlink_list_remove(plugin_, key(k_flst), "FLST\\Items", 9), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_formlink_list_insert(plugin_, key(k_flst), "FLST\\Items", 0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_formlink_list_set(plugin_, key(k_flst), "FLST\\Items", 0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);

  const radicle_form_key unknown_master{"Other.esm", 0x900};
  EXPECT_EQ(radicle_plugin_formlink_list_insert(plugin_, key(k_flst), "FLST\\Items", 0, &unknown_master), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_formlink_list_set(plugin_, key(k_flst), "FLST\\Items", 0, &unknown_master), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_formlink_list_size(plugin_, key(k_refr), "FLST\\Items", &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_formlink_list_size(plugin_, key(0x999), "FLST\\Items", &count), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_formlink_list_size(plugin_, key(k_flst), "FLST\\Nope", &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_formlink_list_size(plugin_, key(k_flst), "nonsense", &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_formlink_list_size(plugin_, key(k_flst), "FLST\\Items", nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_formlink_list_remove(plugin_, key(k_refr), "FLST\\Items", 0), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_formlink_list_clear(plugin_, key(k_refr), "FLST\\Items"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_formlink_list_clear(plugin_, key(0x999), "FLST\\Items"), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_formlink_list_clear(nullptr, key(k_flst), "FLST\\Items"), RADICLE_STATUS_INVALID_ARGUMENT);

  EXPECT_EQ(radicle_plugin_formlink_list_remove(plugin_, key(k_flst), "FLST\\Items", 0), RADICLE_STATUS_OK);

  const auto after_edit = saved();
  const auto items = subs(after_edit, k_flst, "LNAM");
  ASSERT_EQ(items.size(), 2u);
  EXPECT_EQ(items[0], words({0x900}));
  EXPECT_EQ(items[1], words({0x901}));

  EXPECT_EQ(radicle_plugin_formlink_list_clear(plugin_, key(k_flst), "FLST\\Items"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_formlink_list_clear(plugin_, key(k_flst), "FLST\\Items"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_formlink_list_size(plugin_, key(k_flst), "FLST\\Items", &count), RADICLE_STATUS_OK);
  EXPECT_EQ(count, 0u);
  EXPECT_TRUE(subs(saved(), k_flst, "LNAM").empty());
}

TEST_F(CApiFields, NullableListsAcceptNullElements) {
  EXPECT_EQ(radicle_plugin_formlink_list_insert(plugin_, key(k_race), "RACE\\MalePresets", 1, nullptr), RADICLE_STATUS_OK);
  size_t count = 0;
  EXPECT_EQ(radicle_plugin_formlink_list_size(plugin_, key(k_race), "RACE\\MalePresets", &count), RADICLE_STATUS_OK);
  EXPECT_EQ(count, 2u);
  EXPECT_EQ(radicle_plugin_formlink_list_set(plugin_, key(k_race), "RACE\\MalePresets", 0, nullptr), RADICLE_STATUS_OK);

  const auto reloaded = saved();
  const auto presets = subs(reloaded, k_race, "RPRM");
  ASSERT_EQ(presets.size(), 2u);
  EXPECT_EQ(presets[0], words({0}));
  EXPECT_EQ(presets[1], words({0}));
}
