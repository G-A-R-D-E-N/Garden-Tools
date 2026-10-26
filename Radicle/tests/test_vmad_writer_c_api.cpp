#include "fo4/c_api_vmad.h"
#include "vmad_writer_fixture.hpp"

#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>

namespace {

std::filesystem::path temp_root() {
  const auto root = std::filesystem::temp_directory_path() /
    ("radicle_vmad_capi_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  return root;
}

struct CApiVmadFile {
  std::filesystem::path root{temp_root()};
  std::filesystem::path input{root / "Vmad.esp"};
  radicle_plugin* plugin{nullptr};
  explicit CApiVmadFile(const std::vector<std::byte>& bytes) {
    std::ofstream stream(input, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    stream.close();
  }
  CApiVmadFile(const CApiVmadFile&) = delete;
  CApiVmadFile& operator=(const CApiVmadFile&) = delete;
  ~CApiVmadFile() {
    radicle_plugin_destroy(plugin);
    std::error_code error;
    std::filesystem::remove_all(root, error);
  }
  bool open() { return radicle_plugin_read_file(input.string().c_str(), &plugin) == RADICLE_STATUS_OK; }
  std::filesystem::path out(const char* name) const { return root / name; }
};

const fo4::Subrecord* vmad_of(const fo4::Record& record) {
  for (const auto& sub : record.subrecords) {
    if (sub.signature.to_string() == "VMAD") return &sub;
  }
  return nullptr;
}

std::vector<std::byte> flagged_scene_vmad() {
  namespace fixture = fo4::test::vmad;
  fo4::test::ByteWriter out;
  out.u16(6); out.u16(2); out.u16(1);
  fixture::name(out, "Script"); out.u8(3); out.u16(0);
  out.u8(3);
  out.u8(0);
  fixture::name(out, "PF_Frag"); out.u8(0); out.u16(0);
  out.u16(0);
  return out.bytes;
}

std::vector<std::byte> indexed_perk_vmad() {
  namespace fixture = fo4::test::vmad;
  fo4::test::ByteWriter out;
  out.u16(6); out.u16(2); out.u16(1);
  fixture::name(out, "Script"); out.u8(3); out.u16(0);
  out.u8(3);
  fixture::name(out, "PF_Frag"); out.u8(0); out.u16(0);
  out.u16(0);
  return out.bytes;
}

}  // namespace

TEST(CApiVmadWriter, TypedSettersReparseAndRejectInvalidBoundaryValues) {
  namespace fixture = fo4::test::vmad;
  const auto root = std::filesystem::temp_directory_path() /
    ("radicle_vmad_writer_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  struct Cleanup {
    std::filesystem::path path;
    radicle_plugin* plugin{nullptr};
    ~Cleanup() { radicle_plugin_destroy(plugin); std::error_code error; std::filesystem::remove_all(path, error); }
  } cleanup{root, nullptr};
  const auto input = root / "Vmad.esp";
  const auto bytes = fixture::plugin_bytes();
  std::ofstream stream(input, std::ios::binary);
  stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  stream.close();
  ASSERT_EQ(radicle_plugin_read_file(input.string().c_str(), &cleanup.plugin), RADICLE_STATUS_OK);
  const radicle_form_key key{"Vmad.esp", 0x800};
  auto* plugin = cleanup.plugin;
  EXPECT_EQ(radicle_plugin_set_vmad_int(nullptr, key, 0, 4, 0, 1), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_vmad_int(plugin, key, 99, 4, 0, 1), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_vmad_int(plugin, key, 0, 2, 0, 1), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_vmad_bool(plugin, key, 0, 8, 0, 2), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_vmad_float(plugin, key, 0, 6, 0, std::numeric_limits<float>::quiet_NaN()), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_vmad_string(plugin, key, 0, 2, 0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_vmad_string(plugin, key, 0, 2, 0, "\xFF"), RADICLE_STATUS_INVALID_ARGUMENT);
  const auto unchanged = root / "Unchanged.esp";
  ASSERT_EQ(radicle_plugin_write_file(plugin, unchanged.string().c_str()), RADICLE_STATUS_OK);
  EXPECT_EQ(fo4::test::original_bytes(fo4::read_plugin_file(unchanged)), bytes);
  EXPECT_EQ(radicle_plugin_set_vmad_object(plugin, key, 0, 0, 0, {nullptr, 0}, -1), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_vmad_string(plugin, key, 0, 2, 0, "Caf\xC3\xA9"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_vmad_string(plugin, key, 0, 3, 0, "Caf\xC3\xA9"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_vmad_int(plugin, key, 0, 4, 0, -42), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_vmad_float(plugin, key, 0, 6, 0, 7.5f), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_vmad_bool(plugin, key, 0, 9, 1, 0), RADICLE_STATUS_OK);
  const auto output = root / "Saved.esp";
  ASSERT_EQ(radicle_plugin_write_file(plugin, output.string().c_str()), RADICLE_STATUS_OK);
  auto reparsed = fo4::read_plugin_file(output);
  const auto* record = fo4::find_record(reparsed, fixture::key(0x800, "Saved.esp"));
  ASSERT_NE(record, nullptr);
  const auto properties = fo4::read_vmad_scripts(record->subrecords[0].data).scripts[0].properties;
  EXPECT_EQ(properties[0].object->value(), 0u); EXPECT_EQ(properties[0].alias, -1);
  EXPECT_EQ(properties[2].text, "Caf\xC3\xA9"); EXPECT_EQ(properties[4].integer, -42);
  EXPECT_EQ(properties[6].real, 7.5f);
  EXPECT_EQ(record->subrecords[0].data[properties[9].value_offset + 5], std::byte{0});
  const auto& data = record->subrecords[0].data;
  const std::vector<std::byte> cafe{std::byte{0x43}, std::byte{0x61}, std::byte{0x66}, std::byte{0xC3}, std::byte{0xA9}};
  const auto text_at = properties[2].value_offset;
  EXPECT_EQ(data[text_at], std::byte{5}); EXPECT_EQ(data[text_at + 1], std::byte{0});
  EXPECT_EQ(std::vector<std::byte>(data.begin() + text_at + 2, data.begin() + text_at + 7), cafe);
  const auto element_at = properties[3].value_offset + 4;
  EXPECT_EQ(data[element_at], std::byte{5}); EXPECT_EQ(data[element_at + 1], std::byte{0});
  EXPECT_EQ(std::vector<std::byte>(data.begin() + element_at + 2, data.begin() + element_at + 7), cafe);
}

TEST(CApiVmadWriter, RefusesInvalidUtf8StringValueAndLeavesRecordUnchanged) {
  namespace fixture = fo4::test::vmad;
  const auto root = std::filesystem::temp_directory_path() /
    ("radicle_vmad_writer_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  struct Cleanup {
    std::filesystem::path path;
    radicle_plugin* plugin{nullptr};
    ~Cleanup() { radicle_plugin_destroy(plugin); std::error_code error; std::filesystem::remove_all(path, error); }
  } cleanup{root, nullptr};
  const auto input = root / "Vmad.esp";
  const auto bytes = fixture::plugin_bytes();
  std::ofstream stream(input, std::ios::binary);
  stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  stream.close();
  ASSERT_EQ(radicle_plugin_read_file(input.string().c_str(), &cleanup.plugin), RADICLE_STATUS_OK);
  const radicle_form_key key{"Vmad.esp", 0x800};
  auto* plugin = cleanup.plugin;
  EXPECT_EQ(radicle_plugin_set_vmad_string(plugin, key, 0, 2, 0, "Caf\xE9"), RADICLE_STATUS_INVALID_ARGUMENT);
  const auto output = root / "Refused.esp";
  ASSERT_EQ(radicle_plugin_write_file(plugin, output.string().c_str()), RADICLE_STATUS_OK);
  EXPECT_EQ(fo4::test::original_bytes(fo4::read_plugin_file(output)), bytes);
}

TEST(CApiVmadWriter, StructureExportsAddScriptsAndEveryPropertyType) {
  namespace fixture = fo4::test::vmad;
  CApiVmadFile file(fixture::plugin_bytes_without_vmad("MISC"));
  ASSERT_TRUE(file.open());
  const radicle_form_key key{"Vmad.esp", 0x800};
  auto* plugin = file.plugin;
  EXPECT_EQ(radicle_plugin_add_vmad(nullptr, key), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_add_vmad(plugin, radicle_form_key{nullptr, 0}), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_add_vmad(plugin, key), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_add_vmad(plugin, key), RADICLE_STATUS_INVALID_ARGUMENT);
  size_t index = 99;
  EXPECT_EQ(radicle_plugin_add_vmad_script(plugin, key, 0, "PF_Test", 1, &index), RADICLE_STATUS_OK);
  EXPECT_EQ(index, 0u);
  index = 99;
  EXPECT_EQ(radicle_plugin_add_vmad_script(plugin, key, 1, "pf_test", 1, &index), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(index, 0u);
  EXPECT_EQ(radicle_plugin_add_vmad_script(plugin, key, 9, "Other", 0, &index), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_add_vmad_script(plugin, key, 1, nullptr, 0, &index), RADICLE_STATUS_INVALID_ARGUMENT);
  const std::uint8_t types[] = {1, 2, 3, 4, 5, 7, 11, 12, 13, 14, 15, 17};
  std::size_t at = 0;
  for (const auto type : types) {
    size_t added = 99;
    EXPECT_EQ(radicle_plugin_add_vmad_property(plugin, key, 0, at, "Property", type, 0, &added),
              RADICLE_STATUS_OK) << static_cast<int>(type);
    EXPECT_EQ(added, at);
    ++at;
  }
  EXPECT_EQ(radicle_plugin_set_vmad_object(plugin, key, 0, 0, 0, {nullptr, 0}, -1), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_vmad_string(plugin, key, 0, 1, 0, "Cafe"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_vmad_int(plugin, key, 0, 2, 0, -7), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_vmad_float(plugin, key, 0, 3, 0, 1.5f), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_vmad_bool(plugin, key, 0, 4, 0, 1), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_vmad_int(plugin, key, 0, 8, 0, 7), RADICLE_STATUS_OK);
  size_t refused = 99;
  EXPECT_EQ(radicle_plugin_add_vmad_property(plugin, key, 0, at, "Bad", 16, 0, &refused),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(refused, 0u);
  EXPECT_EQ(radicle_plugin_add_vmad_property(plugin, key, 0, at, "Bad", 0, 0, &refused),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_add_vmad_property(plugin, key, 7, 0, "Bad", 3, 0, &refused),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_add_vmad_property(plugin, key, 0, 0, nullptr, 3, 0, &refused),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_add_vmad_fragment(plugin, key, 0, "PF_Test", "F"), RADICLE_STATUS_INVALID_ARGUMENT);
  ASSERT_EQ(radicle_plugin_write_file(plugin, file.out("Saved.esp").string().c_str()), RADICLE_STATUS_OK);
  auto reparsed = fo4::read_plugin_file(file.out("Saved.esp"));
  const auto* record = fo4::find_record(reparsed, fixture::key(0x800, "Saved.esp"));
  ASSERT_NE(record, nullptr);
  const auto* vmad = vmad_of(*record);
  ASSERT_NE(vmad, nullptr);
  const auto decoded = fo4::read_vmad_scripts(vmad->data);
  ASSERT_EQ(decoded.scripts.size(), 1u);
  EXPECT_EQ(decoded.scripts[0].name, "PF_Test");
  EXPECT_EQ(decoded.scripts[0].flags, 1);
  ASSERT_EQ(decoded.scripts[0].properties.size(), 12u);
  EXPECT_EQ(decoded.scripts[0].properties[0].object->value(), 0u);
  EXPECT_EQ(decoded.scripts[0].properties[0].alias, -1);
  EXPECT_EQ(decoded.scripts[0].properties[1].text, "Cafe");
  EXPECT_EQ(decoded.scripts[0].properties[2].integer, -7);
  EXPECT_EQ(decoded.scripts[0].properties[3].real, 1.5f);
  EXPECT_EQ(decoded.scripts[0].properties[4].boolean, true);
  EXPECT_EQ(decoded.scripts[0].properties[8].element_count, 1u);
  ASSERT_TRUE(decoded.scripts[0].properties[8].integer_elements.has_value());
  EXPECT_EQ(decoded.scripts[0].properties[8].integer_elements->at(0), 7);
  EXPECT_EQ(decoded.scripts[0].properties[5].struct_elements->size(), 1u);
  EXPECT_EQ(decoded.scripts[0].properties[11].element_count, 1u);
  EXPECT_EQ(radicle_plugin_remove_vmad_property(plugin, key, 0, 8), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_remove_vmad_property(plugin, key, 0, 10), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_remove_vmad_property(plugin, key, 0, 10), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_remove_vmad_script(plugin, key, 0), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_remove_vmad_script(plugin, key, 0), RADICLE_STATUS_INVALID_ARGUMENT);
  ASSERT_EQ(radicle_plugin_write_file(plugin, file.out("Empty.esp").string().c_str()), RADICLE_STATUS_OK);
  auto emptied = fo4::read_plugin_file(file.out("Empty.esp"));
  const auto* emptied_record = fo4::find_record(emptied, fixture::key(0x800, "Empty.esp"));
  ASSERT_NE(emptied_record, nullptr);
  const auto* emptied_vmad = vmad_of(*emptied_record);
  ASSERT_NE(emptied_vmad, nullptr);
  EXPECT_EQ(fo4::read_vmad_scripts(emptied_vmad->data).scripts.size(), 0u);
}

TEST(CApiVmadWriter, StructureRefusalsLeaveTheFileByteIdentical) {
  namespace fixture = fo4::test::vmad;
  const auto bytes = fixture::plugin_bytes_without_vmad("SCEN");
  CApiVmadFile file(bytes);
  ASSERT_TRUE(file.open());
  const radicle_form_key key{"Vmad.esp", 0x800};
  auto* plugin = file.plugin;
  size_t out = 99;
  EXPECT_EQ(radicle_plugin_add_vmad_script(plugin, key, 0, "PF_Test", 0, &out), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_add_vmad_property(plugin, key, 0, 0, "P", 3, 0, &out), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_remove_vmad_script(plugin, key, 0), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_remove_vmad_property(plugin, key, 0, 0), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_add_vmad_fragment(plugin, key, 0, "PF_Test", "F"), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_remove_vmad_fragment(plugin, key, 0), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_add_vmad_script(plugin, key, 0, "", 0, &out), RADICLE_STATUS_INVALID_ARGUMENT);
  ASSERT_EQ(radicle_plugin_write_file(plugin, file.out("Refused.esp").string().c_str()), RADICLE_STATUS_OK);
  EXPECT_EQ(fo4::test::original_bytes(fo4::read_plugin_file(file.out("Refused.esp"))), bytes);
}

TEST(CApiVmadWriter, GenericFragmentsAddAndRemoveOnEveryFragmentRecordShape) {
  namespace fixture = fo4::test::vmad;
  const radicle_form_key key{"Vmad.esp", 0x800};
  {
    CApiVmadFile file(fixture::plugin_bytes(flagged_scene_vmad(), "SCEN"));
    ASSERT_TRUE(file.open());
    auto* plugin = file.plugin;
    EXPECT_EQ(radicle_plugin_add_vmad_fragment(plugin, key, 0, "PF_Frag", "Fragment_0"), RADICLE_STATUS_OK);
    EXPECT_EQ(radicle_plugin_add_vmad_fragment(plugin, key, 0, "PF_Frag", "Fragment_0"),
              RADICLE_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(radicle_plugin_add_vmad_fragment(plugin, key, 4, "PF_Frag", "Fragment_4"),
              RADICLE_STATUS_INVALID_ARGUMENT);
    ASSERT_EQ(radicle_plugin_write_file(plugin, file.out("Scene.esp").string().c_str()), RADICLE_STATUS_OK);
    auto reparsed = fo4::read_plugin_file(file.out("Scene.esp"));
    const auto* record = fo4::find_record(reparsed, fixture::key(0x800, "Scene.esp"));
    ASSERT_NE(record, nullptr);
    const auto* vmad = vmad_of(*record);
    ASSERT_NE(vmad, nullptr);
    ASSERT_TRUE(fo4::vmad_coverage(vmad->data, fo4::sig_scen).complete());
    EXPECT_EQ(fo4::read_vmad_scripts(vmad->data).scripts.size(), 1u);
    EXPECT_EQ(radicle_plugin_remove_vmad_fragment(plugin, key, 0), RADICLE_STATUS_OK);
    EXPECT_EQ(radicle_plugin_remove_vmad_fragment(plugin, key, 0), RADICLE_STATUS_NOT_FOUND);
  }
  {
    CApiVmadFile file(fixture::plugin_bytes(indexed_perk_vmad(), "PERK"));
    ASSERT_TRUE(file.open());
    auto* plugin = file.plugin;
    EXPECT_EQ(radicle_plugin_add_vmad_fragment(plugin, key, 5, "PF_Frag", "Fragment_5"), RADICLE_STATUS_OK);
    EXPECT_EQ(radicle_plugin_add_vmad_fragment(plugin, key, 5, "PF_Frag", "Fragment_5"),
              RADICLE_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(radicle_plugin_remove_vmad_fragment(plugin, key, 5), RADICLE_STATUS_OK);
    EXPECT_EQ(radicle_plugin_remove_vmad_fragment(plugin, key, 5), RADICLE_STATUS_NOT_FOUND);
  }
  {
    CApiVmadFile file(fixture::plugin_bytes());
    ASSERT_TRUE(file.open());
    EXPECT_EQ(radicle_plugin_add_vmad_fragment(file.plugin, key, 0, "PF_Frag", "Fragment_0"),
              RADICLE_STATUS_INVALID_ARGUMENT);
  }
}
