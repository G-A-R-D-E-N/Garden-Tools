#include "fo4/c_api.h"
#include "fo4/c_api_env.h"
#include "fo4/c_api_vmad.h"

#include "plugin_bytes.hpp"
#include "temp_data_dir.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
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

void wstring(fo4::test::ByteWriter& out, std::string_view text) {
  out.u16(static_cast<std::uint16_t>(text.size()));
  out.raw(text);
}

std::vector<std::byte> make_vmad() {
  fo4::test::ByteWriter out;
  out.u16(6);
  out.u16(2);
  out.u16(1);
  wstring(out, "ReadScript");
  out.u8(0);
  out.u16(2);
  wstring(out, "Label");
  out.u8(2);
  out.u8(1);
  wstring(out, "Alpha");
  wstring(out, "Targets");
  out.u8(11);
  out.u8(1);
  out.u32(2);
  out.u16(0);
  out.u16(0xFFFF);
  out.u32(0x805);
  out.u16(0);
  out.u16(3);
  out.u32(0x806);
  return out.bytes;
}

std::vector<std::byte> make_struct_with_array_member_vmad() {
  fo4::test::ByteWriter out;
  out.u16(6);
  out.u16(2);
  out.u16(1);
  wstring(out, "StructScript");
  out.u8(0);
  out.u16(1);
  wstring(out, "Holder");
  out.u8(7);
  out.u8(1);
  out.u32(2);
  wstring(out, "Count");
  out.u8(3);
  out.u8(0);
  out.u32(7);
  wstring(out, "Targets");
  out.u8(11);
  out.u8(1);
  out.u32(1);
  out.u16(0);
  out.u16(0xFFFF);
  out.u32(0x805);
  return out.bytes;
}

std::vector<std::byte> make_scalar_array_vmad() {
  fo4::test::ByteWriter out;
  out.u16(6);
  out.u16(2);
  out.u16(1);
  wstring(out, "KindsScript");
  out.u8(0);
  out.u16(10);
  wstring(out, "Count");
  out.u8(3);
  out.u8(0);
  out.i32(-5);
  wstring(out, "Ratio");
  out.u8(4);
  out.u8(0);
  out.f32(2.5f);
  wstring(out, "Flag");
  out.u8(5);
  out.u8(0);
  out.u8(1);
  wstring(out, "Owner");
  out.u8(1);
  out.u8(0);
  out.u16(0);
  out.u16(0xFFFF);
  out.u32(0x805);
  wstring(out, "Label");
  out.u8(2);
  out.u8(0);
  wstring(out, "Alpha");
  wstring(out, "Counts");
  out.u8(13);
  out.u8(0);
  out.u32(2);
  out.i32(1);
  out.i32(2);
  wstring(out, "Ratios");
  out.u8(14);
  out.u8(0);
  out.u32(2);
  out.f32(0.5f);
  out.f32(-1.5f);
  wstring(out, "Flags");
  out.u8(15);
  out.u8(0);
  out.u32(2);
  out.u8(0);
  out.u8(1);
  wstring(out, "Owners");
  out.u8(11);
  out.u8(0);
  out.u32(1);
  out.u16(0);
  out.u16(3);
  out.u32(0x806);
  wstring(out, "Labels");
  out.u8(12);
  out.u8(0);
  out.u32(1);
  wstring(out, "Beta");
  return out.bytes;
}

std::vector<std::byte> make_struct_values_vmad() {
  fo4::test::ByteWriter out;
  out.u16(6);
  out.u16(2);
  out.u16(1);
  wstring(out, "StructValues");
  out.u8(0);
  out.u16(2);
  wstring(out, "Point");
  out.u8(7);
  out.u8(0);
  out.u32(2);
  wstring(out, "X");
  out.u8(3);
  out.u8(0);
  out.i32(7);
  wstring(out, "Name");
  out.u8(2);
  out.u8(0);
  wstring(out, "Origin");
  wstring(out, "Pairs");
  out.u8(17);
  out.u8(0);
  out.u32(2);
  out.u32(2);
  wstring(out, "Id");
  out.u8(3);
  out.u8(0);
  out.i32(9);
  wstring(out, "Owner");
  out.u8(1);
  out.u8(0);
  out.u16(0);
  out.u16(4);
  out.u32(0x807);
  out.u32(0);
  return out.bytes;
}

std::vector<std::byte> make_latin1_vmad() {
  fo4::test::ByteWriter out;
  out.u16(6);
  out.u16(2);
  out.u16(2);
  wstring(out, "Caf\xE9");
  out.u8(0);
  out.u16(3);
  wstring(out, "Caf\xE9");
  out.u8(2);
  out.u8(0);
  wstring(out, "Caf\xC3\xA9");
  wstring(out, "Labels");
  out.u8(12);
  out.u8(0);
  out.u32(1);
  wstring(out, "Caf\xC3\xA9");
  wstring(out, "Holder");
  out.u8(7);
  out.u8(0);
  out.u32(1);
  wstring(out, "Caf\xE9");
  out.u8(2);
  out.u8(0);
  wstring(out, "Caf\xC3\xA9");
  wstring(out, "Quo\x93te\x9F");
  out.u8(0);
  out.u16(1);
  wstring(out, "");
  out.u8(2);
  out.u8(0);
  wstring(out, "\x80");
  return out.bytes;
}

std::vector<std::byte> make_unsupported_vmad() {
  fo4::test::ByteWriter out;
  out.u16(6);
  out.u16(2);
  out.u16(1);
  wstring(out, "VariableScript");
  out.u8(0);
  out.u16(1);
  wstring(out, "Unsure");
  out.u8(6);
  out.u8(0);
  return out.bytes;
}

std::vector<std::byte> with_trailing(std::vector<std::byte> bytes, std::size_t extra) {
  for (std::size_t i = 0; i < extra; ++i) bytes.push_back(std::byte{0xAB});
  return bytes;
}

// A pointer that a rejected call must overwrite; it is never dereferenced.
template <typename T>
T* sentinel() {
  return reinterpret_cast<T*>(static_cast<std::uintptr_t>(1));
}

std::vector<std::byte> make_undecodable_plugin() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  const auto group = fo4::test::begin_group(out, "REFR", 0);
  const auto header = fo4::test::begin_record(out, "REFR", fo4::RecordFlags::compressed, 0x800);
  out.u32(16);
  out.raw("not a zlib stream");
  fo4::test::end_record(out, header);
  fo4::test::end_group(out, group);
  return out.bytes;
}

std::vector<std::byte> make_plugin(const std::vector<Rec>& records) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
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

struct EnvDeleter {
  void operator()(radicle_env* env) const { radicle_env_destroy(env); }
};

struct ScriptsDeleter {
  void operator()(radicle_vmad_scripts* scripts) const { radicle_vmad_scripts_destroy(scripts); }
};

using EnvHandle = std::unique_ptr<radicle_env, EnvDeleter>;
using ScriptsHandle = std::unique_ptr<radicle_vmad_scripts, ScriptsDeleter>;

class CApiVmadReadTest : public ::testing::Test {
protected:
  void SetUp() override {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    root_ = std::filesystem::temp_directory_path() /
            ("radicle_vmad_read_" + std::to_string(fo4::test::process_id()) + "_" + std::to_string(id));
    std::filesystem::create_directories(root_);
  }

  void TearDown() override {
    std::error_code ec;
    std::filesystem::remove_all(root_, ec);
  }

  EnvHandle open(std::vector<std::byte> vmad = make_vmad()) {
    return open_plugin(make_plugin({{"REFR", 0x800, {{"VMAD", std::move(vmad)}}}, {"STAT", 0x806, {}}}));
  }

  EnvHandle open_plugin(std::vector<std::byte> data, const std::string& filename = "Vmad.esp") {
    const auto path = root_ / filename;
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(data.data()),
                                                 static_cast<std::streamsize>(data.size()));
    const auto utf8 = path.u8string();
    const std::string text(utf8.begin(), utf8.end());
    const char* raw[] = {text.c_str()};
    radicle_env* pointer = nullptr;
    EXPECT_EQ(radicle_env_load(raw, 1, &pointer), RADICLE_STATUS_OK) << radicle_last_error();
    return EnvHandle(pointer);
  }

  const radicle_record* winner(const radicle_env* env, std::uint32_t local_id) {
    const radicle_record* record = nullptr;
    const char* plugin = "Vmad.esp";
    EXPECT_EQ(radicle_env_winner(env, {plugin, local_id}, &record), RADICLE_STATUS_OK) << radicle_last_error();
    return record;
  }

  std::filesystem::path root_;
};

}  // namespace

TEST_F(CApiVmadReadTest, ReadsScriptsPropertiesAndObjectElements) {
  const auto env = open();
  ASSERT_NE(env, nullptr);
  radicle_vmad_scripts* raw = nullptr;
  ASSERT_EQ(radicle_record_vmad_scripts(env.get(), winner(env.get(), 0x800), &raw), RADICLE_STATUS_OK)
      << radicle_last_error();
  const ScriptsHandle scripts(raw);
  ASSERT_EQ(radicle_vmad_script_count(scripts.get()), 1u);

  radicle_vmad_script script{};
  ASSERT_EQ(radicle_vmad_scripts_script(scripts.get(), 0, &script), RADICLE_STATUS_OK);
  EXPECT_EQ(std::string(script.name), "ReadScript");
  EXPECT_EQ(script.property_count, 2u);
  EXPECT_EQ(script.unsupported, 0);

  radicle_vmad_property property{};
  ASSERT_EQ(radicle_vmad_scripts_property(scripts.get(), 0, 0, &property), RADICLE_STATUS_OK);
  EXPECT_EQ(std::string(property.name), "Label");
  EXPECT_EQ(property.value_kind, RADICLE_VMAD_TEXT);
  ASSERT_NE(property.text, nullptr);
  EXPECT_EQ(std::string(property.text), "Alpha");
  EXPECT_EQ(property.element_total, 0u);

  ASSERT_EQ(radicle_vmad_scripts_property(scripts.get(), 0, 1, &property), RADICLE_STATUS_OK);
  EXPECT_EQ(std::string(property.name), "Targets");
  EXPECT_EQ(property.element_kind, RADICLE_VMAD_OBJECT);
  EXPECT_EQ(property.declared_element_count, 2u);
  EXPECT_EQ(property.element_total, 2u);
  ASSERT_EQ(radicle_vmad_element_count(scripts.get(), 0, 1), 2u);

  radicle_vmad_element element{};
  ASSERT_EQ(radicle_vmad_scripts_element(scripts.get(), 0, 1, 0, &element), RADICLE_STATUS_OK);
  EXPECT_EQ(element.kind, RADICLE_VMAD_OBJECT);
  EXPECT_EQ(element.object_form_id, 0x805u);
  EXPECT_EQ(element.alias, -1);
  EXPECT_EQ(element.index_in_property, 0u);
  EXPECT_EQ(element.member_name, nullptr);

  ASSERT_EQ(radicle_vmad_scripts_element(scripts.get(), 0, 1, 1, &element), RADICLE_STATUS_OK);
  EXPECT_EQ(element.object_form_id, 0x806u);
  EXPECT_EQ(element.alias, 3);
  EXPECT_EQ(element.index_in_property, 1u);
}

TEST_F(CApiVmadReadTest, RefusesRecordsWithoutVmadAndBadIndexes) {
  const auto env = open();
  ASSERT_NE(env, nullptr);
  radicle_vmad_scripts* raw = nullptr;
  EXPECT_EQ(radicle_record_vmad_scripts(env.get(), winner(env.get(), 0x806), &raw), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(raw, nullptr);
  // Invariant: a rejected call clears the caller's handle before the argument and environment checks.
  radicle_vmad_scripts* stale = sentinel<radicle_vmad_scripts>();
  EXPECT_EQ(radicle_record_vmad_scripts(nullptr, winner(env.get(), 0x800), &stale), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(stale, nullptr);
  stale = sentinel<radicle_vmad_scripts>();
  EXPECT_EQ(radicle_record_vmad_scripts(env.get(), nullptr, &stale), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(stale, nullptr);
  EXPECT_EQ(radicle_record_vmad_scripts(env.get(), winner(env.get(), 0x800), nullptr), RADICLE_STATUS_INVALID_ARGUMENT);

  ASSERT_EQ(radicle_record_vmad_scripts(env.get(), winner(env.get(), 0x800), &raw), RADICLE_STATUS_OK);
  const ScriptsHandle scripts(raw);
  radicle_vmad_script script{};
  EXPECT_EQ(radicle_vmad_scripts_script(scripts.get(), 1, &script), RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_vmad_property property{};
  EXPECT_EQ(radicle_vmad_scripts_property(scripts.get(), 0, 2, &property), RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_vmad_element element{};
  EXPECT_EQ(radicle_vmad_scripts_element(scripts.get(), 0, 1, 2, &element), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_vmad_element_count(scripts.get(), 0, 2), 0u);
  EXPECT_EQ(radicle_vmad_script_count(nullptr), 0u);
  radicle_vmad_scripts_destroy(nullptr);
}

TEST_F(CApiVmadReadTest, ReportsAStructMemberItCannotDecodeAsUnsupported) {
  const auto env = open(make_struct_with_array_member_vmad());
  ASSERT_NE(env, nullptr);
  radicle_vmad_scripts* raw = nullptr;
  ASSERT_EQ(radicle_record_vmad_scripts(env.get(), winner(env.get(), 0x800), &raw), RADICLE_STATUS_OK)
      << radicle_last_error();
  const ScriptsHandle scripts(raw);
  ASSERT_EQ(radicle_vmad_script_count(scripts.get()), 1u);

  // Invariant: a struct holding an array member must report unsupported, because its scalar members are dropped too.
  radicle_vmad_script script{};
  ASSERT_EQ(radicle_vmad_scripts_script(scripts.get(), 0, &script), RADICLE_STATUS_OK);
  EXPECT_EQ(std::string(script.name), "StructScript");
  EXPECT_EQ(script.unsupported, 1);
  EXPECT_EQ(script.property_count, 1u);

  radicle_vmad_property property{};
  ASSERT_EQ(radicle_vmad_scripts_property(scripts.get(), 0, 0, &property), RADICLE_STATUS_OK);
  EXPECT_EQ(std::string(property.name), "Holder");
  EXPECT_EQ(property.value_kind, RADICLE_VMAD_NONE);
  EXPECT_EQ(property.declared_element_count, 2u);
  EXPECT_EQ(property.element_total, 0u);
  EXPECT_EQ(radicle_vmad_element_count(scripts.get(), 0, 0), 0u);

  // Invariant: a rejected accessor must clear its out-parameter before the range check.
  radicle_vmad_property stale_property{};
  stale_property.name = "stale";
  stale_property.declared_element_count = 9u;
  EXPECT_EQ(radicle_vmad_scripts_property(scripts.get(), 0, 1, &stale_property), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(stale_property.name, static_cast<const char*>(nullptr));
  EXPECT_EQ(stale_property.declared_element_count, 0u);
  radicle_vmad_element stale_element{};
  stale_element.kind = RADICLE_VMAD_INT;
  stale_element.struct_index = 5u;
  EXPECT_EQ(radicle_vmad_scripts_element(scripts.get(), 0, 0, 0, &stale_element), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(stale_element.kind, RADICLE_VMAD_NONE);
  EXPECT_EQ(stale_element.struct_index, 0u);
}

TEST_F(CApiVmadReadTest, ReadsEveryScalarKindAndItsArray) {
  const auto env = open(make_scalar_array_vmad());
  ASSERT_NE(env, nullptr);
  radicle_vmad_scripts* raw = nullptr;
  ASSERT_EQ(radicle_record_vmad_scripts(env.get(), winner(env.get(), 0x800), &raw), RADICLE_STATUS_OK)
      << radicle_last_error();
  const ScriptsHandle scripts(raw);
  radicle_vmad_script script{};
  ASSERT_EQ(radicle_vmad_scripts_script(scripts.get(), 0, &script), RADICLE_STATUS_OK);
  EXPECT_EQ(std::string(script.name), "KindsScript");
  EXPECT_EQ(script.property_count, 10u);
  EXPECT_EQ(script.unsupported, 0);

  // Invariant: an array property reports one row per element; a scalar reports its kind and no rows.
  radicle_vmad_property property{};
  ASSERT_EQ(radicle_vmad_scripts_property(scripts.get(), 0, 0, &property), RADICLE_STATUS_OK);
  EXPECT_EQ(std::string(property.name), "Count");
  EXPECT_EQ(property.value_kind, RADICLE_VMAD_INT);
  EXPECT_EQ(property.integer, -5);
  EXPECT_EQ(property.element_kind, RADICLE_VMAD_NONE);
  EXPECT_EQ(property.element_total, 0u);

  ASSERT_EQ(radicle_vmad_scripts_property(scripts.get(), 0, 1, &property), RADICLE_STATUS_OK);
  EXPECT_EQ(property.value_kind, RADICLE_VMAD_REAL);
  EXPECT_FLOAT_EQ(property.real, 2.5f);

  ASSERT_EQ(radicle_vmad_scripts_property(scripts.get(), 0, 2, &property), RADICLE_STATUS_OK);
  EXPECT_EQ(property.value_kind, RADICLE_VMAD_BOOL);
  EXPECT_EQ(property.boolean, 1);

  ASSERT_EQ(radicle_vmad_scripts_property(scripts.get(), 0, 3, &property), RADICLE_STATUS_OK);
  EXPECT_EQ(property.value_kind, RADICLE_VMAD_OBJECT);
  EXPECT_EQ(property.object_form_id, 0x805u);
  EXPECT_EQ(property.alias, -1);

  ASSERT_EQ(radicle_vmad_scripts_property(scripts.get(), 0, 4, &property), RADICLE_STATUS_OK);
  EXPECT_EQ(property.value_kind, RADICLE_VMAD_TEXT);
  ASSERT_NE(property.text, nullptr);
  EXPECT_EQ(std::string(property.text), "Alpha");

  radicle_vmad_element element{};
  ASSERT_EQ(radicle_vmad_scripts_property(scripts.get(), 0, 5, &property), RADICLE_STATUS_OK);
  EXPECT_EQ(std::string(property.name), "Counts");
  EXPECT_EQ(property.value_kind, RADICLE_VMAD_NONE);
  EXPECT_EQ(property.element_kind, RADICLE_VMAD_INT);
  EXPECT_EQ(property.declared_element_count, 2u);
  EXPECT_EQ(property.element_total, 2u);
  ASSERT_EQ(radicle_vmad_scripts_element(scripts.get(), 0, 5, 1, &element), RADICLE_STATUS_OK);
  EXPECT_EQ(element.kind, RADICLE_VMAD_INT);
  EXPECT_EQ(element.integer, 2);
  EXPECT_EQ(element.member_name, nullptr);

  ASSERT_EQ(radicle_vmad_scripts_property(scripts.get(), 0, 6, &property), RADICLE_STATUS_OK);
  EXPECT_EQ(property.element_kind, RADICLE_VMAD_REAL);
  ASSERT_EQ(radicle_vmad_scripts_element(scripts.get(), 0, 6, 0, &element), RADICLE_STATUS_OK);
  EXPECT_FLOAT_EQ(element.real, 0.5f);
  ASSERT_EQ(radicle_vmad_scripts_element(scripts.get(), 0, 6, 1, &element), RADICLE_STATUS_OK);
  EXPECT_FLOAT_EQ(element.real, -1.5f);

  ASSERT_EQ(radicle_vmad_scripts_property(scripts.get(), 0, 7, &property), RADICLE_STATUS_OK);
  EXPECT_EQ(property.element_kind, RADICLE_VMAD_BOOL);
  ASSERT_EQ(radicle_vmad_scripts_element(scripts.get(), 0, 7, 1, &element), RADICLE_STATUS_OK);
  EXPECT_EQ(element.boolean, 1);

  ASSERT_EQ(radicle_vmad_scripts_property(scripts.get(), 0, 8, &property), RADICLE_STATUS_OK);
  EXPECT_EQ(property.element_kind, RADICLE_VMAD_OBJECT);
  ASSERT_EQ(radicle_vmad_scripts_element(scripts.get(), 0, 8, 0, &element), RADICLE_STATUS_OK);
  EXPECT_EQ(element.object_form_id, 0x806u);
  EXPECT_EQ(element.alias, 3);

  ASSERT_EQ(radicle_vmad_scripts_property(scripts.get(), 0, 9, &property), RADICLE_STATUS_OK);
  EXPECT_EQ(property.element_kind, RADICLE_VMAD_TEXT);
  ASSERT_EQ(radicle_vmad_scripts_element(scripts.get(), 0, 9, 0, &element), RADICLE_STATUS_OK);
  ASSERT_NE(element.text, nullptr);
  EXPECT_EQ(std::string(element.text), "Beta");
}

TEST_F(CApiVmadReadTest, ReadsPlainStructsAndStructArrays) {
  const auto env = open(make_struct_values_vmad());
  ASSERT_NE(env, nullptr);
  radicle_vmad_scripts* raw = nullptr;
  ASSERT_EQ(radicle_record_vmad_scripts(env.get(), winner(env.get(), 0x800), &raw), RADICLE_STATUS_OK)
      << radicle_last_error();
  const ScriptsHandle scripts(raw);
  radicle_vmad_script script{};
  ASSERT_EQ(radicle_vmad_scripts_script(scripts.get(), 0, &script), RADICLE_STATUS_OK);
  EXPECT_EQ(script.property_count, 2u);
  EXPECT_EQ(script.unsupported, 0);

  // Invariant: a plain struct and a struct array both list member rows, and the array also reports its struct count.
  radicle_vmad_property property{};
  ASSERT_EQ(radicle_vmad_scripts_property(scripts.get(), 0, 0, &property), RADICLE_STATUS_OK);
  EXPECT_EQ(std::string(property.name), "Point");
  EXPECT_EQ(property.type, 7);
  EXPECT_EQ(property.value_kind, RADICLE_VMAD_NONE);
  EXPECT_EQ(property.element_kind, RADICLE_VMAD_STRUCT_MEMBER);
  EXPECT_EQ(property.declared_element_count, 2u);
  EXPECT_EQ(property.element_total, 2u);

  radicle_vmad_element element{};
  ASSERT_EQ(radicle_vmad_scripts_element(scripts.get(), 0, 0, 0, &element), RADICLE_STATUS_OK);
  EXPECT_EQ(element.kind, RADICLE_VMAD_INT);
  EXPECT_EQ(std::string(element.member_name), "X");
  EXPECT_EQ(element.struct_index, 0u);
  EXPECT_EQ(element.integer, 7);
  ASSERT_EQ(radicle_vmad_scripts_element(scripts.get(), 0, 0, 1, &element), RADICLE_STATUS_OK);
  EXPECT_EQ(element.kind, RADICLE_VMAD_TEXT);
  EXPECT_EQ(std::string(element.member_name), "Name");
  ASSERT_NE(element.text, nullptr);
  EXPECT_EQ(std::string(element.text), "Origin");

  // Invariant: a struct array counts structs in declared_element_count and rows in element_total.
  ASSERT_EQ(radicle_vmad_scripts_property(scripts.get(), 0, 1, &property), RADICLE_STATUS_OK);
  EXPECT_EQ(std::string(property.name), "Pairs");
  EXPECT_EQ(property.type, 17);
  EXPECT_EQ(property.declared_element_count, 2u);
  EXPECT_EQ(property.element_total, 2u);
  ASSERT_EQ(radicle_vmad_scripts_element(scripts.get(), 0, 1, 0, &element), RADICLE_STATUS_OK);
  EXPECT_EQ(std::string(element.member_name), "Id");
  EXPECT_EQ(element.struct_index, 0u);
  EXPECT_EQ(element.integer, 9);
  ASSERT_EQ(radicle_vmad_scripts_element(scripts.get(), 0, 1, 1, &element), RADICLE_STATUS_OK);
  EXPECT_EQ(std::string(element.member_name), "Owner");
  EXPECT_EQ(element.struct_index, 0u);
  EXPECT_EQ(element.kind, RADICLE_VMAD_OBJECT);
  EXPECT_EQ(element.object_form_id, 0x807u);
  EXPECT_EQ(element.alias, 4);
  // Invariant: the second struct has no members, so it contributes no rows.
  EXPECT_EQ(radicle_vmad_scripts_element(scripts.get(), 0, 1, 2, &element), RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST_F(CApiVmadReadTest, ConvertsNamesFromWindows1252AndKeepsUtf8Values) {
  const auto env = open(make_latin1_vmad());
  ASSERT_NE(env, nullptr);
  radicle_vmad_scripts* raw = nullptr;
  ASSERT_EQ(radicle_record_vmad_scripts(env.get(), winner(env.get(), 0x800), &raw), RADICLE_STATUS_OK)
      << radicle_last_error();
  const ScriptsHandle scripts(raw);
  radicle_vmad_script script{};
  ASSERT_EQ(radicle_vmad_scripts_script(scripts.get(), 0, &script), RADICLE_STATUS_OK);
  // Invariant: names reach the caller as UTF-8, converted from the Windows-1252 bytes the file stores,
  // while string values are stored as UTF-8 and must not be converted.
  EXPECT_EQ(std::string(script.name), "Caf\xC3\xA9");
  radicle_vmad_property property{};
  ASSERT_EQ(radicle_vmad_scripts_property(scripts.get(), 0, 0, &property), RADICLE_STATUS_OK);
  EXPECT_EQ(std::string(property.name), "Caf\xC3\xA9");
  ASSERT_NE(property.text, nullptr);
  // Invariant: string values are UTF-8 and returned unchanged, so converting them again would double-encode them.
  EXPECT_EQ(std::string(property.text), "Caf\xC3\xA9");
  radicle_vmad_element element{};
  ASSERT_EQ(radicle_vmad_scripts_element(scripts.get(), 0, 1, 0, &element), RADICLE_STATUS_OK);
  ASSERT_NE(element.text, nullptr);
  EXPECT_EQ(std::string(element.text), "Caf\xC3\xA9");
  ASSERT_EQ(radicle_vmad_scripts_property(scripts.get(), 0, 2, &property), RADICLE_STATUS_OK);
  EXPECT_EQ(property.element_kind, RADICLE_VMAD_STRUCT_MEMBER);
  ASSERT_EQ(radicle_vmad_scripts_element(scripts.get(), 0, 2, 0, &element), RADICLE_STATUS_OK);
  EXPECT_EQ(std::string(element.member_name), "Caf\xC3\xA9");
  ASSERT_NE(element.text, nullptr);  EXPECT_EQ(std::string(element.text), "Caf\xC3\xA9");

  radicle_vmad_script second{};
  ASSERT_EQ(radicle_vmad_scripts_script(scripts.get(), 1, &second), RADICLE_STATUS_OK);
  // Invariant: bytes 0x80, 0x93 and 0x9F must decode as Windows-1252 (euro, left double quote, Y diaeresis) and not as Latin-1 U+0080, U+0093 and U+009F.
  EXPECT_EQ(std::string(second.name), "Quo\xE2\x80\x9Cte\xC5\xB8");
  ASSERT_EQ(radicle_vmad_scripts_property(scripts.get(), 1, 0, &property), RADICLE_STATUS_OK);
  // Invariant: an empty name is a valid UTF-8 string, not a missing one, so the pointer must not be NULL.
  ASSERT_NE(property.name, nullptr);
  EXPECT_EQ(std::string(property.name), "");
  ASSERT_NE(property.text, nullptr);
  EXPECT_EQ(std::string(property.text), "\x80");
}

TEST_F(CApiVmadReadTest, ReportsTruncatedVmadAsParseError) {
  auto truncated = make_vmad();
  truncated.resize(truncated.size() - 4);
  const auto env = open(std::move(truncated));
  ASSERT_NE(env, nullptr);
  radicle_vmad_scripts* raw = nullptr;
  // Invariant: a truncated payload is a parse error, not an empty script list.
  EXPECT_EQ(radicle_record_vmad_scripts(env.get(), winner(env.get(), 0x800), &raw), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(raw, nullptr);
}

TEST_F(CApiVmadReadTest, ReportsUnsupportedPropertiesAndTrailingBytes) {
  const auto unsupported_env = open(make_unsupported_vmad());
  ASSERT_NE(unsupported_env, nullptr);
  radicle_vmad_scripts* raw = nullptr;
  ASSERT_EQ(radicle_record_vmad_scripts(unsupported_env.get(), winner(unsupported_env.get(), 0x800), &raw),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  const ScriptsHandle unsupported(raw);
  ASSERT_EQ(radicle_vmad_script_count(unsupported.get()), 1u);
  radicle_vmad_script script{};
  ASSERT_EQ(radicle_vmad_scripts_script(unsupported.get(), 0, &script), RADICLE_STATUS_OK);
  // Invariant: an undecodable property sets unsupported, so the script reads as incomplete.
  EXPECT_EQ(std::string(script.name), "VariableScript");
  EXPECT_EQ(script.unsupported, 1);
  EXPECT_EQ(script.property_count, 0u);

  const auto trailing_env = open(with_trailing(make_vmad(), 3));
  ASSERT_NE(trailing_env, nullptr);
  ASSERT_EQ(radicle_record_vmad_scripts(trailing_env.get(), winner(trailing_env.get(), 0x800), &raw),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  const ScriptsHandle trailing(raw);
  ASSERT_EQ(radicle_vmad_scripts_script(trailing.get(), 0, &script), RADICLE_STATUS_OK);
  // Invariant: trailing_bytes reports the bytes after the last property.
  EXPECT_EQ(script.trailing_bytes, 3u);
  EXPECT_EQ(script.unsupported, 0);
}

TEST_F(CApiVmadReadTest, RejectsRecordsFromAnotherEnvironment) {
  const auto first = open();
  ASSERT_NE(first, nullptr);
  const auto second = open_plugin(make_plugin({{"REFR", 0x800, {{"VMAD", make_vmad()}}}}), "Vmad2.esp");
  ASSERT_NE(second, nullptr);
  radicle_vmad_scripts* raw = nullptr;
  // Invariant: a record handle from another environment is refused with INVALID_ARGUMENT.
  EXPECT_EQ(radicle_record_vmad_scripts(second.get(), winner(first.get(), 0x800), &raw),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(raw, nullptr);
}

TEST_F(CApiVmadReadTest, ReportsUndecodableRecordsAsParseErrors) {
  const auto env = open_plugin(make_undecodable_plugin());
  ASSERT_NE(env, nullptr);
  radicle_vmad_scripts* raw = nullptr;
  // Invariant: an undecodable record is a parse error, never a missing VMAD.
  EXPECT_EQ(radicle_record_vmad_scripts(env.get(), winner(env.get(), 0x800), &raw), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(raw, nullptr);
}
