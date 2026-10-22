#include "fo4/c_api.h"
#include "fo4/c_api_env.h"
#include "fo4/c_api_materials.h"
#include "fo4/c_api_sound.h"
#include "fo4/c_api_vegetation.h"
#include "plugin_bytes.hpp"
#include "quest_plugin_fixture.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::filesystem::path testdata_path(const char* name) {
  return std::filesystem::path(FO4_TESTDATA_DIR) / name;
}

std::filesystem::path temp_root() {
  const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() / ("radicle_c_api_" + std::to_string(id));
}

struct Cleanup {
  std::filesystem::path root;
  ~Cleanup() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
};

void write_bytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  ASSERT_TRUE(stream.good());
  stream.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  ASSERT_TRUE(stream.good());
}

std::vector<std::byte> make_remap_fixture() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);

  fo4::test::ByteWriter data;
  data.u32(0);
  data.u32(0);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Fallout4.esm\0", 13});
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Drop.esm\0", 9});
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Keep.esp\0", 9});
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, "KYWD", 0);
  const auto local = fo4::test::begin_record(out, "KYWD", 0, 0x03000800);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Local\0", 6});
  fo4::test::end_record(out, local);
  const auto inherited = fo4::test::begin_record(out, "KYWD", 0, 0x0200009A);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"KeepRecord\0", 11});
  fo4::test::end_record(out, inherited);
  fo4::test::end_group(out, group);
  return out.bytes;
}

std::set<std::string> collect_form_keys(radicle_plugin* plugin) {
  char** values = nullptr;
  size_t count = 0;
  EXPECT_EQ(radicle_plugin_collect_form_keys(plugin, &values, &count), RADICLE_STATUS_OK)
      << radicle_last_error();

  std::set<std::string> out;
  for (size_t i = 0; i < count; ++i) {
    if (values[i] != nullptr) out.emplace(values[i]);
  }
  radicle_free_string_array(values, count);
  return out;
}

std::set<std::string> collect_form_links_text(radicle_plugin* plugin) {
  char** values = nullptr;
  size_t count = 0;
  EXPECT_EQ(radicle_plugin_collect_form_links(plugin, &values, &count), RADICLE_STATUS_OK)
      << radicle_last_error();

  std::set<std::string> out;
  for (size_t i = 0; i < count; ++i) {
    if (values[i] != nullptr) out.emplace(values[i]);
  }
  radicle_free_string_array(values, count);
  return out;
}

std::vector<std::byte> make_placed_plugin() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, "REFR", 0);
  const auto refr = fo4::test::begin_record(out, "REFR", 0, 0x00000800);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"DoorRef\0", 8});
  const auto vmad = fo4::test::vmad_scripts();
  fo4::test::write_subrecord(out, "VMAD", vmad);
  fo4::test::ByteWriter base;
  base.u32(0x00000900);
  fo4::test::write_subrecord(out, "NAME", base.bytes);
  fo4::test::end_record(out, refr);
  fo4::test::end_group(out, group);
  return out.bytes;
}

std::vector<std::byte> make_dial_plugin() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, "DIAL", 0);
  const auto dial = fo4::test::begin_record(out, "DIAL", 0, 0x00000800);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Topic\0", 6});
  fo4::test::end_record(out, dial);
  fo4::test::end_group(out, group);
  return out.bytes;
}

int sentinel_marker = 0;

template <class T>
T* sentinel_ptr() {
  return reinterpret_cast<T*>(&sentinel_marker);
}

template <class T>
T sentinel_value() {
  T value{};
  std::memset(&value, 0xA5, sizeof(T));
  return value;
}

template <class T>
void expect_sentinel_value(const T& value) {
  const auto expected = sentinel_value<T>();
  EXPECT_EQ(std::memcmp(&value, &expected, sizeof(T)), 0);
}

}  // namespace

TEST(CApi, NegativeSurfaceRejectsNullAndInvalidInput) {
  radicle_plugin* plugin = nullptr;
  EXPECT_EQ(radicle_plugin_read_file(nullptr, &plugin), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_NE(std::string(radicle_last_error()).find("required"), std::string::npos);

  // Null path string.
  EXPECT_EQ(radicle_plugin_read_file("/definitely/not/a/plugin.esp", nullptr),
            RADICLE_STATUS_INVALID_ARGUMENT);

  // Null out pointers on every entry point that produces output.
  uint8_t* bytes = reinterpret_cast<uint8_t*>(0x1);
  size_t byte_count = 1;
  EXPECT_EQ(radicle_plugin_write_bytes(nullptr, &bytes, &byte_count),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(bytes, reinterpret_cast<uint8_t*>(0x1));
  EXPECT_EQ(radicle_plugin_write_bytes(plugin, &bytes, &byte_count),
            RADICLE_STATUS_INVALID_ARGUMENT);

  char** strings = nullptr;
  size_t string_count = 0;
  EXPECT_EQ(radicle_plugin_collect_form_keys(nullptr, &strings, &string_count),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(strings, nullptr);
  EXPECT_EQ(string_count, 0u);
  EXPECT_EQ(radicle_plugin_collect_form_links(nullptr, &strings, &string_count),
            RADICLE_STATUS_INVALID_ARGUMENT);

  // Null master arrays / descriptors.
  EXPECT_EQ(radicle_plugin_stamp_master_kinds(nullptr, nullptr, 0),
            RADICLE_STATUS_INVALID_ARGUMENT);
  const char* const master_paths[] = {nullptr};
  EXPECT_EQ(radicle_plugin_stamp_master_kinds(plugin, master_paths, 1),
            RADICLE_STATUS_INVALID_ARGUMENT);
  const radicle_master_descriptor bad_descriptor[] = {{nullptr, RADICLE_PLUGIN_FULL}};
  EXPECT_EQ(radicle_plugin_remap_masters(plugin, bad_descriptor, 1),
            RADICLE_STATUS_INVALID_ARGUMENT);
  // Out-of-range enum value must not crash; it is rejected as invalid.
  const radicle_master_descriptor bad_kind[] = {
      {"Fallout4.esm", static_cast<radicle_plugin_kind>(99)}};
  EXPECT_EQ(radicle_plugin_remap_masters(plugin, bad_kind, 1),
            RADICLE_STATUS_INVALID_ARGUMENT);

  // Null write target, null-destroy, and freeing null buffers are all safe.
  EXPECT_EQ(radicle_plugin_write_file(plugin, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_plugin_destroy(nullptr);
  radicle_free_bytes(nullptr);
  radicle_free_string_array(nullptr, 3u);
}

#if !defined(_WIN32)
TEST(CApi, FailedReplacementLeavesExistingFileIntact) {
  namespace fs = std::filesystem;
  const auto root = temp_root();
  Cleanup cleanup{root};

  radicle_plugin* plugin = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(
                testdata_path("FW_HeatherSurvivalPatch.esp").string().c_str(), &plugin),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_NE(plugin, nullptr);

  // An existing destination plus a read-only parent directory forces the
  // replacement primitive itself to fail: the staged rename cannot land. The
  // original bytes must survive untouched — this is the regression guard for
  // any future fallback that deletes the destination before renaming.
  const auto dir = root / "out";
  std::filesystem::create_directories(dir);
  const auto target = dir / "Patch.esp";
  const std::vector<std::byte> sentinel{{std::byte{0xDE}, std::byte{0xAD},
                                         std::byte{0xBE}, std::byte{0xEF}}};
  ASSERT_NO_FATAL_FAILURE(write_bytes(target, sentinel));

  {
    std::error_code ec;
    std::filesystem::permissions(dir, std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec,
                                 ec);
    ASSERT_FALSE(ec) << ec.message();
  }

  EXPECT_NE(radicle_plugin_write_file(plugin, target.string().c_str()),
            RADICLE_STATUS_OK);

  std::vector<std::byte> after(sentinel.size());
  {
    std::ifstream in(target, std::ios::binary);
    ASSERT_TRUE(in.good());
    in.read(reinterpret_cast<char*>(after.data()),
            static_cast<std::streamsize>(after.size()));
    EXPECT_TRUE(in.good());
  }
  EXPECT_EQ(after, sentinel);

  // Restore permissions so Cleanup can remove the tree.
  std::error_code restore_ec;
  std::filesystem::permissions(dir, std::filesystem::perms::owner_all, restore_ec);
  EXPECT_FALSE(restore_ec);

  radicle_plugin_destroy(plugin);
}
#endif

TEST(CApi, FailedWriteCleansUpTempFiles) {
  namespace fs = std::filesystem;
  const auto root = temp_root();
  Cleanup cleanup{root};

  radicle_plugin* plugin = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(
                testdata_path("FW_HeatherSurvivalPatch.esp").string().c_str(), &plugin),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_NE(plugin, nullptr);

  // A regular file sitting where a parent directory must be created forces
  // failure long before any destination byte is touched.
  const auto blocker = root / "blocker";
  write_bytes(blocker, {std::byte{0x00}});
  const auto impossible = blocker / "Nested" / "Patch.esp";
  EXPECT_NE(radicle_plugin_write_file(plugin, impossible.string().c_str()),
            RADICLE_STATUS_OK);
  EXPECT_TRUE(std::filesystem::is_regular_file(blocker));

  // A sibling temporary file from a failed staged write must not linger next
  // to a real output file after a successful save.
  const auto target = root / "out" / "Patch.esp";
  ASSERT_EQ(radicle_plugin_write_file(plugin, target.string().c_str()), RADICLE_STATUS_OK)
      << radicle_last_error();

  bool stray_temp_files = false;
  for (const auto& entry : std::filesystem::directory_iterator(target.parent_path())) {
    if (entry.path().filename().string().find(".radicle-tmp") != std::string::npos) {
      stray_temp_files = true;
    }
  }
  EXPECT_FALSE(stray_temp_files);

  radicle_plugin_destroy(plugin);
}

TEST(CApi, ReadsSerializesAndCollectsFormKeys) {
  const auto path = testdata_path("FW_HeatherSurvivalPatch.esp").string();

  radicle_plugin* plugin = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(path.c_str(), &plugin), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_NE(plugin, nullptr);

  uint8_t* bytes = nullptr;
  size_t byte_count = 0;
  ASSERT_EQ(radicle_plugin_write_bytes(plugin, &bytes, &byte_count), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_NE(bytes, nullptr);
  EXPECT_GT(byte_count, 0u);
  radicle_free_bytes(bytes);

  char** form_keys = nullptr;
  size_t form_key_count = 0;
  ASSERT_EQ(radicle_plugin_collect_form_keys(plugin, &form_keys, &form_key_count),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_GT(form_key_count, 0u);
  radicle_free_string_array(form_keys, form_key_count);

  char** form_links = nullptr;
  size_t form_link_count = 0;
  ASSERT_EQ(radicle_plugin_collect_form_links(plugin, &form_links, &form_link_count),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  radicle_free_string_array(form_links, form_link_count);

  radicle_plugin_destroy(plugin);
}

TEST(CApi, RemapsMastersWritesAndReloadsStableFormKeys) {
  const auto root = temp_root();
  Cleanup cleanup{root};
  const auto input = root / "input" / "Patch.esp";
  const auto output = root / "output" / "Patch.esp";
  write_bytes(input, make_remap_fixture());
  std::filesystem::create_directories(output.parent_path());

  radicle_plugin* plugin = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(input.string().c_str(), &plugin), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_NE(plugin, nullptr);

  const auto before = collect_form_keys(plugin);
  EXPECT_TRUE(before.contains("000800:Patch.esp"));
  EXPECT_TRUE(before.contains("00009A:Keep.esp"));

  const radicle_master_descriptor masters[] = {
      {"Fallout4.esm", RADICLE_PLUGIN_FULL},
      {"Keep.esp", RADICLE_PLUGIN_FULL},
  };
  ASSERT_EQ(radicle_plugin_remap_masters(plugin, masters, 2), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_EQ(radicle_plugin_write_file(plugin, output.string().c_str()), RADICLE_STATUS_OK)
      << radicle_last_error();
  radicle_plugin_destroy(plugin);
  plugin = nullptr;

  ASSERT_EQ(radicle_plugin_read_file(output.string().c_str(), &plugin), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_NE(plugin, nullptr);
  const auto after = collect_form_keys(plugin);
  EXPECT_TRUE(after.contains("000800:Patch.esp"));
  EXPECT_TRUE(after.contains("00009A:Keep.esp"));
  radicle_plugin_destroy(plugin);
}

TEST(CApi, FailedRemapMastersLeavesThePluginUnchanged) {
  const auto root = temp_root();
  Cleanup cleanup{root};
  const auto input = root / "input" / "Patch.esp";
  const auto output = root / "output" / "Patch.esp";
  write_bytes(input, make_remap_fixture());
  std::filesystem::create_directories(output.parent_path());

  radicle_plugin* plugin = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(input.string().c_str(), &plugin), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_NE(plugin, nullptr);

  const auto before = collect_form_keys(plugin);
  EXPECT_TRUE(before.contains("00009A:Keep.esp"));

  const radicle_master_descriptor masters[] = {
      {"Fallout4.esm", RADICLE_PLUGIN_FULL},
  };
  EXPECT_NE(radicle_plugin_remap_masters(plugin, masters, 1), RADICLE_STATUS_OK);
  EXPECT_NE(std::string(radicle_last_error()).find("destination master table"),
            std::string::npos);
  EXPECT_EQ(collect_form_keys(plugin), before);

  ASSERT_EQ(radicle_plugin_write_file(plugin, output.string().c_str()), RADICLE_STATUS_OK)
      << radicle_last_error();
  radicle_plugin_destroy(plugin);
  plugin = nullptr;

  ASSERT_EQ(radicle_plugin_read_file(output.string().c_str(), &plugin), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_NE(plugin, nullptr);
  EXPECT_EQ(collect_form_keys(plugin), before);
  radicle_plugin_destroy(plugin);
}

TEST(CApi, HeatherRealFixtureDropsInsertedMasterWritesAndReloadsStableIdentity) {
  const auto root = temp_root();
  Cleanup cleanup{root};
  const auto input = testdata_path("FW_HeatherSurvivalPatch.esp");
  const auto output = root / "out" / "FW_HeatherSurvivalPatch.esp";

  radicle_plugin* plugin = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(input.string().c_str(), &plugin), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_NE(plugin, nullptr);

  const auto before_keys = collect_form_keys(plugin);
  const auto before_links = collect_form_links_text(plugin);
  ASSERT_FALSE(before_keys.empty());
  ASSERT_FALSE(before_links.empty());

  // Insert an unused master between Fallout4 and Heather. This forces every
  // Heather/Survival reference and every local record's current-plugin ordinal
  // to move, then the second remap drops that master again. The test therefore
  // exercises a real master-drop on the real Heather fixture without pretending
  // one of Heather's three genuinely referenced shipped masters is removable.
  const radicle_master_descriptor expanded[] = {
      {"Fallout4.esm", RADICLE_PLUGIN_FULL},
      {"RadicleUnusedProbe.esp", RADICLE_PLUGIN_FULL},
      {"llamaCompanionHeatherv2.esp", RADICLE_PLUGIN_FULL},
      {"Survival Mode Overhaul.esp", RADICLE_PLUGIN_FULL},
  };
  ASSERT_EQ(radicle_plugin_remap_masters(plugin, expanded, 4), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(collect_form_keys(plugin), before_keys);
  EXPECT_EQ(collect_form_links_text(plugin), before_links);

  const radicle_master_descriptor original[] = {
      {"Fallout4.esm", RADICLE_PLUGIN_FULL},
      {"llamaCompanionHeatherv2.esp", RADICLE_PLUGIN_FULL},
      {"Survival Mode Overhaul.esp", RADICLE_PLUGIN_FULL},
  };
  ASSERT_EQ(radicle_plugin_remap_masters(plugin, original, 3), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_EQ(radicle_plugin_write_file(plugin, output.string().c_str()), RADICLE_STATUS_OK)
      << radicle_last_error();
  radicle_plugin_destroy(plugin);
  plugin = nullptr;

  ASSERT_EQ(radicle_plugin_read_file(output.string().c_str(), &plugin), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_NE(plugin, nullptr);
  EXPECT_EQ(collect_form_keys(plugin), before_keys);
  EXPECT_EQ(collect_form_links_text(plugin), before_links);
  radicle_plugin_destroy(plugin);
}

TEST(CApi, PlacedScriptTextKeepsEmbeddedNulBytesWithItsLength) {
  const auto root = temp_root();
  Cleanup cleanup{root};
  const auto file = root / "Placed.esp";
  write_bytes(file, make_placed_plugin());
  const auto path = file.string();
  const char* paths[] = {path.c_str()};

  radicle_env* env = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 1, &env), RADICLE_STATUS_OK) << radicle_last_error();
  const radicle_record* record = nullptr;
  ASSERT_EQ(radicle_env_winner(env, radicle_form_key{"Placed.esp", 0x800}, &record),
            RADICLE_STATUS_OK)
      << radicle_last_error();

  radicle_placed_ref* ref = nullptr;
  ASSERT_EQ(radicle_placed_ref_decode(env, record, &ref), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_NE(ref, nullptr);
  ASSERT_EQ(ref->script_count, 1u);
  const auto& script = ref->scripts[0];
  const radicle_script_property* embedded = nullptr;
  for (size_t index = 0; index < script.property_count; ++index) {
    if (std::string(script.properties[index].name) == "Embedded") embedded = &script.properties[index];
  }
  ASSERT_NE(embedded, nullptr);
  ASSERT_NE(embedded->text, nullptr);
  const std::string expected("Garden\0Patch", 12);
  EXPECT_EQ(embedded->text_size, expected.size());
  EXPECT_EQ(std::string(embedded->text, embedded->text_size), expected);

  radicle_placed_ref_free(ref);
  radicle_env_destroy(env);
}

TEST(CApi, RecordFromAnotherEnvironmentIsRejected) {
  const auto root = temp_root();
  Cleanup cleanup{root};
  const auto file_a = root / "a" / "Placed.esp";
  const auto file_b = root / "b" / "Placed.esp";
  const auto topic_a = root / "c" / "Topic.esp";
  const auto topic_b = root / "d" / "Topic.esp";
  write_bytes(file_a, make_placed_plugin());
  write_bytes(file_b, make_placed_plugin());
  write_bytes(topic_a, make_dial_plugin());
  write_bytes(topic_b, make_dial_plugin());
  const auto path_a = file_a.string();
  const auto path_b = file_b.string();
  const auto topic_path_a = topic_a.string();
  const auto topic_path_b = topic_b.string();
  const char* paths_a[] = {path_a.c_str()};
  const char* paths_b[] = {path_b.c_str()};
  const char* topic_paths_a[] = {topic_path_a.c_str()};
  const char* topic_paths_b[] = {topic_path_b.c_str()};

  radicle_env* env_a = nullptr;
  radicle_env* env_b = nullptr;
  radicle_env* topic_env_a = nullptr;
  radicle_env* topic_env_b = nullptr;
  ASSERT_EQ(radicle_env_load(paths_a, 1, &env_a), RADICLE_STATUS_OK) << radicle_last_error();
  ASSERT_EQ(radicle_env_load(paths_b, 1, &env_b), RADICLE_STATUS_OK) << radicle_last_error();
  ASSERT_EQ(radicle_env_load(topic_paths_a, 1, &topic_env_a), RADICLE_STATUS_OK) << radicle_last_error();
  ASSERT_EQ(radicle_env_load(topic_paths_b, 1, &topic_env_b), RADICLE_STATUS_OK) << radicle_last_error();

  const radicle_form_key key{"Placed.esp", 0x800};
  const radicle_record* record = nullptr;
  ASSERT_EQ(radicle_env_winner(env_a, key, &record), RADICLE_STATUS_OK) << radicle_last_error();
  const radicle_record* own = nullptr;
  ASSERT_EQ(radicle_env_winner(env_b, key, &own), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_NE(own, record);

  const radicle_form_key topic_key{"Topic.esp", 0x800};
  const radicle_record* topic = nullptr;
  ASSERT_EQ(radicle_env_winner(topic_env_a, topic_key, &topic), RADICLE_STATUS_OK) << radicle_last_error();

  radicle_record_info info{};
  EXPECT_EQ(radicle_record_get_info(env_a, record, &info), RADICLE_STATUS_OK);
  radicle_record_info rejected_info = sentinel_value<radicle_record_info>();
  EXPECT_EQ(radicle_record_get_info(env_b, record, &rejected_info), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_NE(std::string(radicle_last_error()).find("does not belong"), std::string::npos);
  expect_sentinel_value(rejected_info);

  radicle_placed_ref* ref = sentinel_ptr<radicle_placed_ref>();
  EXPECT_EQ(radicle_placed_ref_decode(env_b, record, &ref), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(ref, sentinel_ptr<radicle_placed_ref>());

  const char* model = sentinel_ptr<const char>();
  EXPECT_EQ(radicle_record_primary_model_path(env_b, record, &model), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(model, sentinel_ptr<const char>());

  radicle_light_info light = sentinel_value<radicle_light_info>();
  EXPECT_EQ(radicle_light_get_info(env_b, record, &light), RADICLE_STATUS_INVALID_ARGUMENT);
  expect_sentinel_value(light);

  radicle_worldspace_info worldspace = sentinel_value<radicle_worldspace_info>();
  EXPECT_EQ(radicle_worldspace_get_info(env_b, record, &worldspace), RADICLE_STATUS_INVALID_ARGUMENT);
  expect_sentinel_value(worldspace);

  radicle_landscape* landscape = sentinel_ptr<radicle_landscape>();
  EXPECT_EQ(radicle_landscape_decode(env_b, record, &landscape), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(landscape, sentinel_ptr<radicle_landscape>());

  radicle_material_swap* swap = sentinel_ptr<radicle_material_swap>();
  EXPECT_EQ(radicle_material_swap_decode(env_b, record, &swap), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(swap, sentinel_ptr<radicle_material_swap>());

  radicle_grass_info grass = sentinel_value<radicle_grass_info>();
  EXPECT_EQ(radicle_grass_get_info(env_b, record, &grass), RADICLE_STATUS_INVALID_ARGUMENT);
  expect_sentinel_value(grass);
  radicle_tree_info tree = sentinel_value<radicle_tree_info>();
  EXPECT_EQ(radicle_tree_get_info(env_b, record, &tree), RADICLE_STATUS_INVALID_ARGUMENT);
  expect_sentinel_value(tree);
  radicle_flora_info flora = sentinel_value<radicle_flora_info>();
  EXPECT_EQ(radicle_flora_get_info(env_b, record, &flora), RADICLE_STATUS_INVALID_ARGUMENT);
  expect_sentinel_value(flora);
  const radicle_grass_links* grasses = sentinel_ptr<const radicle_grass_links>();
  EXPECT_EQ(radicle_landscape_grasses_decode(env_b, record, &grasses), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_NE(std::string(radicle_last_error()).find("does not belong"), std::string::npos);
  EXPECT_EQ(grasses, nullptr);

  const radicle_sound_descriptor* descriptor = sentinel_ptr<const radicle_sound_descriptor>();
  EXPECT_EQ(radicle_sound_descriptor_decode(env_b, record, &descriptor), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_NE(std::string(radicle_last_error()).find("does not belong"), std::string::npos);
  EXPECT_EQ(descriptor, nullptr);
  radicle_sound_marker_info marker = sentinel_value<radicle_sound_marker_info>();
  EXPECT_EQ(radicle_sound_marker_get_info(env_b, record, &marker), RADICLE_STATUS_INVALID_ARGUMENT);
  expect_sentinel_value(marker);
  const radicle_music_type* music = sentinel_ptr<const radicle_music_type>();
  EXPECT_EQ(radicle_music_type_decode(env_b, record, &music), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(music, nullptr);
  radicle_footstep_info footstep = sentinel_value<radicle_footstep_info>();
  EXPECT_EQ(radicle_footstep_get_info(env_b, record, &footstep), RADICLE_STATUS_INVALID_ARGUMENT);
  expect_sentinel_value(footstep);
  const radicle_footstep_set* footsteps = sentinel_ptr<const radicle_footstep_set>();
  EXPECT_EQ(radicle_footstep_set_decode(env_b, record, &footsteps), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(footsteps, nullptr);

  radicle_plugin* target = nullptr;
  ASSERT_EQ(radicle_plugin_create("Target.esp", &target), RADICLE_STATUS_OK) << radicle_last_error();
  radicle_form_key new_key = sentinel_value<radicle_form_key>();
  EXPECT_EQ(radicle_plugin_copy_placed_ref_new(target, env_b, record, &new_key),
            RADICLE_STATUS_INVALID_ARGUMENT);
  expect_sentinel_value(new_key);
  EXPECT_TRUE(collect_form_keys(target).empty());

  radicle_placed_copy_status override_status = sentinel_value<radicle_placed_copy_status>();
  EXPECT_EQ(radicle_plugin_copy_placed_ref_override(target, env_b, record, 0, &override_status),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_NE(std::string(radicle_last_error()).find("does not belong"), std::string::npos);
  expect_sentinel_value(override_status);
  EXPECT_TRUE(collect_form_keys(target).empty());

  radicle_strings* strings = nullptr;
  ASSERT_EQ(radicle_strings_load(env_b, file_b.parent_path().string().c_str(), "en", &strings),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  const char* name_text = sentinel_ptr<const char>();
  size_t name_size = 0xDEADBEEF;
  EXPECT_EQ(radicle_strings_resolve_name(strings, record, &name_text, &name_size),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(name_text, sentinel_ptr<const char>());
  EXPECT_EQ(name_size, 0xDEADBEEFu);
  radicle_strings_destroy(strings);

  radicle_texture_set_info texture_set = sentinel_value<radicle_texture_set_info>();
  EXPECT_EQ(radicle_texture_set_get_info(env_b, record, &texture_set),
            RADICLE_STATUS_INVALID_ARGUMENT);
  expect_sentinel_value(texture_set);

  radicle_form_key landscape_texture = sentinel_value<radicle_form_key>();
  EXPECT_EQ(radicle_landscape_texture_get_set(env_b, record, &landscape_texture),
            RADICLE_STATUS_INVALID_ARGUMENT);
  expect_sentinel_value(landscape_texture);

  radicle_cell_info cell = sentinel_value<radicle_cell_info>();
  EXPECT_EQ(radicle_cell_get_info(env_b, record, &cell), RADICLE_STATUS_INVALID_ARGUMENT);
  expect_sentinel_value(cell);

  int32_t container_group = 0x5EED;
  radicle_form_key container_world = sentinel_value<radicle_form_key>();
  radicle_form_key container_cell = sentinel_value<radicle_form_key>();
  EXPECT_EQ(radicle_record_get_container(env_b, record, &container_group, &container_world,
                                         &container_cell),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(container_group, 0x5EED);
  expect_sentinel_value(container_world);
  expect_sentinel_value(container_cell);

  radicle_bendable_spline_info bendable = sentinel_value<radicle_bendable_spline_info>();
  EXPECT_EQ(radicle_bendable_spline_get_info(env_b, record, &bendable), RADICLE_STATUS_INVALID_ARGUMENT);
  expect_sentinel_value(bendable);

  radicle_climate* climate = sentinel_ptr<radicle_climate>();
  EXPECT_EQ(radicle_climate_decode(env_b, record, &climate), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(climate, sentinel_ptr<radicle_climate>());

  radicle_form_key layer_parent = sentinel_value<radicle_form_key>();
  EXPECT_EQ(radicle_layer_get_parent(env_b, record, &layer_parent), RADICLE_STATUS_INVALID_ARGUMENT);
  expect_sentinel_value(layer_parent);

  radicle_lighting_template_info lighting = sentinel_value<radicle_lighting_template_info>();
  EXPECT_EQ(radicle_lighting_template_get_info(env_b, record, &lighting), RADICLE_STATUS_INVALID_ARGUMENT);
  expect_sentinel_value(lighting);

  radicle_static_collection* collection = sentinel_ptr<radicle_static_collection>();
  EXPECT_EQ(radicle_static_collection_decode(env_b, record, &collection), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(collection, sentinel_ptr<radicle_static_collection>());

  radicle_static_distant_lod distant_lod = sentinel_value<radicle_static_distant_lod>();
  EXPECT_EQ(radicle_static_get_distant_lod(env_b, record, &distant_lod), RADICLE_STATUS_INVALID_ARGUMENT);
  expect_sentinel_value(distant_lod);

  radicle_form_key water_type = sentinel_value<radicle_form_key>();
  EXPECT_EQ(radicle_activator_get_water_type(env_b, record, &water_type), RADICLE_STATUS_INVALID_ARGUMENT);
  expect_sentinel_value(water_type);

  radicle_water_info water = sentinel_value<radicle_water_info>();
  EXPECT_EQ(radicle_water_get_info(env_b, record, &water), RADICLE_STATUS_INVALID_ARGUMENT);
  expect_sentinel_value(water);

  radicle_weather_info* weather = sentinel_ptr<radicle_weather_info>();
  EXPECT_EQ(radicle_weather_decode(env_b, record, &weather), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(weather, sentinel_ptr<radicle_weather_info>());

  radicle_form_key placed_swap = sentinel_value<radicle_form_key>();
  EXPECT_EQ(radicle_placed_reference_material_swap(env_b, record, &placed_swap), RADICLE_STATUS_INVALID_ARGUMENT);
  expect_sentinel_value(placed_swap);

  radicle_form_key model_swap = sentinel_value<radicle_form_key>();
  EXPECT_EQ(radicle_record_primary_model_material_swap(env_b, record, &model_swap), RADICLE_STATUS_INVALID_ARGUMENT);
  expect_sentinel_value(model_swap);

  float color_index = 1234.5f;
  EXPECT_EQ(radicle_record_primary_model_color_remapping_index(env_b, record, &color_index),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_FLOAT_EQ(color_index, 1234.5f);

  uint16_t form_version = 0xBEEF;
  size_t subrecord_count = 0xDEADBEEF;
  EXPECT_EQ(radicle_record_payload_info(env_b, record, &form_version, &subrecord_count),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(form_version, 0);
  EXPECT_EQ(subrecord_count, 0u);

  uint8_t signature[4] = {0xA5, 0xA5, 0xA5, 0xA5};
  uint8_t* subrecord_bytes = sentinel_ptr<uint8_t>();
  size_t subrecord_size = 0xDEADBEEF;
  int32_t subrecord_extended = 0x5EED;
  EXPECT_EQ(radicle_record_copy_subrecord(env_b, record, 0, signature, &subrecord_bytes,
                                          &subrecord_size, &subrecord_extended),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(std::memcmp(signature, "\0\0\0\0", 4), 0);
  EXPECT_EQ(subrecord_bytes, nullptr);
  EXPECT_EQ(subrecord_size, 0u);
  EXPECT_EQ(subrecord_extended, 0);

  radicle_form_key resolved = sentinel_value<radicle_form_key>();
  EXPECT_EQ(radicle_record_resolve_form_id(env_b, record, 0x00000800, &resolved),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(resolved.plugin, nullptr);
  EXPECT_EQ(resolved.local_id, 0u);

  const radicle_record** topics = sentinel_ptr<const radicle_record*>();
  size_t topic_count = 0xDEADBEEF;
  EXPECT_EQ(radicle_record_topic_children(topic_env_b, topic, &topics, &topic_count),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_NE(std::string(radicle_last_error()).find("does not belong"), std::string::npos);
  EXPECT_EQ(topics, nullptr);
  EXPECT_EQ(topic_count, 0u);

  radicle_plugin_destroy(target);
  radicle_env_destroy(env_a);
  radicle_env_destroy(env_b);
  radicle_env_destroy(topic_env_a);
  radicle_env_destroy(topic_env_b);
}
