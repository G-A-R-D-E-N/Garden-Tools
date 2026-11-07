#include "fo4/c_api_env.h"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

std::vector<std::byte> base_plugin() {
  fo4::test::ByteWriter out;
  out.bytes = fo4::test::make_tes4_plugin(0, 1.0f, {});
  const auto group = fo4::test::begin_group(out, "WEAP", 0);
  const auto record = fo4::test::begin_record(out, "WEAP", 0, 0x00000800);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

std::vector<std::byte> patch_plugin() {
  fo4::test::ByteWriter out;
  out.bytes = fo4::test::make_tes4_plugin(0, 1.0f, {"Base.esm"});
  const auto group = fo4::test::begin_group(out, "MISC", 0);
  const auto record = fo4::test::begin_record(out, "MISC", 0, 0x01000801);
  fo4::test::ByteWriter link;
  link.u32(0x00000800);
  fo4::test::write_subrecord(out, "NAME", link.bytes);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

std::string write_file(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
  std::ofstream(path, std::ios::binary)
      .write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  const auto text = path.u8string();
  return {text.begin(), text.end()};
}

}  // namespace

TEST(CApiReferencedBy, ReportsInboundAndOutboundLinksWithPluginIndex) {
  const auto root = std::filesystem::temp_directory_path() /
                    ("radicle_c_referenced_by_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  const auto base = write_file(root / "Base.esm", base_plugin());
  const auto patch = write_file(root / "Patch.esp", patch_plugin());
  const char* paths[] = {base.c_str(), patch.c_str()};
  radicle_env* env = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 2, &env), RADICLE_STATUS_OK) << radicle_last_error();

  radicle_reference_hit* hits = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_env_referenced_by(env, radicle_form_key{"Base.esm", 0x800}, &hits, &count), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_EQ(count, 1u);
  EXPECT_STREQ(hits[0].record.plugin, "Patch.esp");
  EXPECT_EQ(hits[0].record.local_id, 0x801u);
  EXPECT_EQ(hits[0].plugin_index, 1u);
  EXPECT_STREQ(hits[0].subrecord, "NAME");
  EXPECT_EQ(hits[0].offset, 0u);
  radicle_free_reference_hits(hits);

  ASSERT_EQ(radicle_env_references_from(env, radicle_form_key{"Patch.esp", 0x801}, &hits, &count), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_EQ(count, 1u);
  EXPECT_STREQ(hits[0].record.plugin, "Base.esm");
  EXPECT_EQ(hits[0].record.local_id, 0x800u);
  EXPECT_EQ(hits[0].plugin_index, 1u);
  EXPECT_STREQ(hits[0].subrecord, "NAME");
  radicle_free_reference_hits(hits);

  ASSERT_EQ(radicle_env_referenced_by(env, radicle_form_key{"Base.esm", 0x999}, &hits, &count), RADICLE_STATUS_OK);
  EXPECT_EQ(hits, nullptr);
  EXPECT_EQ(count, 0u);
  ASSERT_EQ(radicle_env_references_from(env, radicle_form_key{"Base.esm", 0x999}, &hits, &count), RADICLE_STATUS_OK);
  EXPECT_EQ(hits, nullptr);
  EXPECT_EQ(count, 0u);

  const radicle_form_key weapon{"Base.esm", 0x800};
  EXPECT_EQ(radicle_env_referenced_by(nullptr, weapon, &hits, &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_env_referenced_by(env, weapon, nullptr, &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_env_references_from(nullptr, weapon, &hits, &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_env_references_from(env, weapon, &hits, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_env_referenced_by(env, radicle_form_key{nullptr, 0x800}, &hits, &count),
            RADICLE_STATUS_INVALID_ARGUMENT);

  radicle_env_destroy(env);
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
}
