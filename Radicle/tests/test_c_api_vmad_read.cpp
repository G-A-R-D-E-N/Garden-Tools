#include "fo4/c_api.h"
#include "fo4/c_api_env.h"
#include "fo4/c_api_vmad.h"

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
    root_ = std::filesystem::temp_directory_path() / ("radicle_vmad_read_" + std::to_string(id));
    std::filesystem::create_directories(root_);
  }

  void TearDown() override {
    std::error_code ec;
    std::filesystem::remove_all(root_, ec);
  }

  EnvHandle open() {
    const auto data = make_plugin({{"REFR", 0x800, {{"VMAD", make_vmad()}}}, {"STAT", 0x806, {}}});
    const auto path = root_ / "Vmad.esp";
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
  ASSERT_EQ(radicle_vmad_script(scripts.get(), 0, &script), RADICLE_STATUS_OK);
  EXPECT_EQ(std::string(script.name), "ReadScript");
  EXPECT_EQ(script.property_count, 2u);
  EXPECT_EQ(script.unsupported, 0);

  radicle_vmad_property property{};
  ASSERT_EQ(radicle_vmad_property(scripts.get(), 0, 0, &property), RADICLE_STATUS_OK);
  EXPECT_EQ(std::string(property.name), "Label");
  EXPECT_EQ(property.value_kind, RADICLE_VMAD_TEXT);
  ASSERT_NE(property.text, nullptr);
  EXPECT_EQ(std::string(property.text), "Alpha");
  EXPECT_EQ(property.element_total, 0u);

  ASSERT_EQ(radicle_vmad_property(scripts.get(), 0, 1, &property), RADICLE_STATUS_OK);
  EXPECT_EQ(std::string(property.name), "Targets");
  EXPECT_EQ(property.element_kind, RADICLE_VMAD_OBJECT);
  EXPECT_EQ(property.declared_element_count, 2u);
  EXPECT_EQ(property.element_total, 2u);
  ASSERT_EQ(radicle_vmad_element_count(scripts.get(), 0, 1), 2u);

  radicle_vmad_element element{};
  ASSERT_EQ(radicle_vmad_element(scripts.get(), 0, 1, 0, &element), RADICLE_STATUS_OK);
  EXPECT_EQ(element.kind, RADICLE_VMAD_OBJECT);
  EXPECT_EQ(element.object_form_id, 0x805u);
  EXPECT_EQ(element.alias, -1);
  EXPECT_EQ(element.index_in_property, 0u);
  EXPECT_EQ(element.member_name, nullptr);

  ASSERT_EQ(radicle_vmad_element(scripts.get(), 0, 1, 1, &element), RADICLE_STATUS_OK);
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
  EXPECT_EQ(radicle_record_vmad_scripts(nullptr, winner(env.get(), 0x800), &raw), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_record_vmad_scripts(env.get(), nullptr, &raw), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_record_vmad_scripts(env.get(), winner(env.get(), 0x800), nullptr), RADICLE_STATUS_INVALID_ARGUMENT);

  ASSERT_EQ(radicle_record_vmad_scripts(env.get(), winner(env.get(), 0x800), &raw), RADICLE_STATUS_OK);
  const ScriptsHandle scripts(raw);
  radicle_vmad_script script{};
  EXPECT_EQ(radicle_vmad_script(scripts.get(), 1, &script), RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_vmad_property property{};
  EXPECT_EQ(radicle_vmad_property(scripts.get(), 0, 2, &property), RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_vmad_element element{};
  EXPECT_EQ(radicle_vmad_element(scripts.get(), 0, 1, 2, &element), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_vmad_element_count(scripts.get(), 0, 2), 0u);
  EXPECT_EQ(radicle_vmad_script_count(nullptr), 0u);
  radicle_vmad_scripts_destroy(nullptr);
}
