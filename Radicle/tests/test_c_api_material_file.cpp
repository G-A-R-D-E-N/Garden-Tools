#include "fo4/c_api.h"
#include "fo4/c_api_material_file.h"
#include "material_file_builder.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

const uint8_t* as_u8(const std::vector<std::byte>& bytes) {
  return reinterpret_cast<const uint8_t*>(bytes.data());
}

std::string take_text(uint8_t* bytes, size_t size) {
  std::string text;
  if (bytes && size != 0) {
    text.assign(reinterpret_cast<const char*>(bytes), size);
  }
  radicle_free_bytes(bytes);
  return text;
}

std::vector<std::byte> take_written(const radicle_material_file* file) {
  uint8_t* bytes = nullptr;
  size_t size = 0;
  EXPECT_EQ(radicle_material_file_write(file, &bytes, &size), RADICLE_STATUS_OK) << radicle_last_error();
  std::vector<std::byte> out(size);
  if (bytes && size != 0) {
    std::memcpy(out.data(), bytes, size);
  }
  radicle_free_bytes(bytes);
  return out;
}

radicle_material_file* open_material(const std::vector<std::byte>& bytes) {
  radicle_material_file* file = nullptr;
  EXPECT_EQ(radicle_material_file_read(as_u8(bytes), bytes.size(), &file), RADICLE_STATUS_OK)
      << radicle_last_error();
  return file;
}

radicle_status set_text(radicle_material_file* file, const char* name, const std::string& text) {
  return radicle_material_file_set_string(file, name, reinterpret_cast<const uint8_t*>(text.data()), text.size());
}

}  // namespace

TEST(CApiMaterialFile, ReadsFormatVersionAndFieldTable) {
  const auto bytes = fo4::test::material_bgem({.version = 2});
  radicle_material_file* file = open_material(bytes);
  ASSERT_NE(file, nullptr);

  radicle_material_format format = RADICLE_MATERIAL_FORMAT_BGSM;
  ASSERT_EQ(radicle_material_file_format(file, &format), RADICLE_STATUS_OK);
  EXPECT_EQ(format, RADICLE_MATERIAL_FORMAT_BGEM);

  uint32_t version = 0;
  ASSERT_EQ(radicle_material_file_version(file, &version), RADICLE_STATUS_OK);
  EXPECT_EQ(version, 2u);

  size_t count = 0;
  ASSERT_EQ(radicle_material_file_field_count(file, &count), RADICLE_STATUS_OK);
  EXPECT_GT(count, 0u);

  uint8_t* name = nullptr;
  size_t name_size = 0;
  ASSERT_EQ(radicle_material_file_field_name(file, 0, &name, &name_size), RADICLE_STATUS_OK);
  EXPECT_EQ(take_text(name, name_size), "Signature");

  radicle_material_field_type type = RADICLE_MATERIAL_FIELD_STRING;
  ASSERT_EQ(radicle_material_file_field_type(file, 0, &type), RADICLE_STATUS_OK);
  EXPECT_EQ(type, RADICLE_MATERIAL_FIELD_U32);

  radicle_material_file_destroy(file);
}

TEST(CApiMaterialFile, GetsSetsAndWritesTypedFields) {
  const auto bytes = fo4::test::material_bgsm({.version = 20, .emit = true});
  radicle_material_file* file = open_material(bytes);
  ASSERT_NE(file, nullptr);

  uint8_t emit = 0;
  ASSERT_EQ(radicle_material_file_get_bool(file, "EmitEnabled", &emit), RADICLE_STATUS_OK);
  EXPECT_EQ(emit, 1);

  radicle_material_color3 color{};
  ASSERT_EQ(radicle_material_file_get_color3(file, "EmittanceColor", &color), RADICLE_STATUS_OK);
  EXPECT_FLOAT_EQ(color.r, 0.25F);
  EXPECT_FLOAT_EQ(color.g, 0.5F);
  EXPECT_FLOAT_EQ(color.b, 0.75F);

  float smoothness = 0.0F;
  ASSERT_EQ(radicle_material_file_get_f32(file, "smoothness", &smoothness), RADICLE_STATUS_OK);
  EXPECT_FLOAT_EQ(smoothness, 0.5F);

  uint32_t tile_flags = 0;
  ASSERT_EQ(radicle_material_file_get_u32(file, "TileFlags", &tile_flags), RADICLE_STATUS_OK);
  EXPECT_EQ(tile_flags, 0x80000003u);

  uint8_t alpha_ref = 0;
  ASSERT_EQ(radicle_material_file_get_u8(file, "AlphaTestRef", &alpha_ref), RADICLE_STATUS_OK);
  EXPECT_EQ(alpha_ref, 128);

  uint8_t* text = nullptr;
  size_t text_size = 0;
  ASSERT_EQ(radicle_material_file_get_string(file, "NormalTexture", &text, &text_size), RADICLE_STATUS_OK);
  EXPECT_EQ(take_text(text, text_size), "NoNul");

  ASSERT_EQ(radicle_material_file_get_string(file, "RootMaterialPath", &text, &text_size), RADICLE_STATUS_OK);
  EXPECT_EQ(text, nullptr);
  EXPECT_EQ(text_size, 0u);

  ASSERT_EQ(radicle_material_file_set_f32(file, "Smoothness", 0.25F), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_material_file_set_bool(file, "EmitEnabled", 0), RADICLE_STATUS_OK);
  const radicle_material_color3 next{0.1F, 0.2F, 0.3F};
  ASSERT_EQ(radicle_material_file_set_color3(file, "EmittanceColor", &next), RADICLE_STATUS_OK);
  ASSERT_EQ(set_text(file, "DiffuseTexture", "Textures/new.dds"), RADICLE_STATUS_OK);

  const auto written = take_written(file);
  radicle_material_file_destroy(file);

  radicle_material_file* reread = open_material(written);
  ASSERT_NE(reread, nullptr);
  ASSERT_EQ(radicle_material_file_get_bool(reread, "EmitEnabled", &emit), RADICLE_STATUS_OK);
  EXPECT_EQ(emit, 0);
  EXPECT_EQ(radicle_material_file_get_color3(reread, "EmittanceColor", &color), RADICLE_STATUS_NOT_FOUND);
  ASSERT_EQ(radicle_material_file_get_f32(reread, "Smoothness", &smoothness), RADICLE_STATUS_OK);
  EXPECT_FLOAT_EQ(smoothness, 0.25F);
  ASSERT_EQ(radicle_material_file_get_string(reread, "DiffuseTexture", &text, &text_size), RADICLE_STATUS_OK);
  EXPECT_EQ(take_text(text, text_size), "Textures/new.dds");
  radicle_material_file_destroy(reread);
}

TEST(CApiMaterialFile, ReportsStatusesForBadInput) {
  const std::vector<std::byte> bad_magic(4, std::byte{0});
  radicle_material_file* file = nullptr;
  EXPECT_EQ(radicle_material_file_read(as_u8(bad_magic), bad_magic.size(), &file), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(file, nullptr);
  EXPECT_NE(std::strlen(radicle_last_error()), 0u);

  const auto bytes = fo4::test::material_bgem({.version = 2});
  const std::vector<std::byte> truncated(bytes.begin(), bytes.begin() + 10);
  EXPECT_EQ(radicle_material_file_read(as_u8(truncated), truncated.size(), &file), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(file, nullptr);

  file = open_material(bytes);
  ASSERT_NE(file, nullptr);

  uint32_t value = 0;
  EXPECT_EQ(radicle_material_file_get_u32(file, "NoSuchField", &value), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_material_file_get_u32(file, "BaseTexture", &value), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_material_file_set_u32(file, "Version", 3), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_material_file_set_u32(file, "Signature", 0), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_material_file_set_u32(file, "NoSuchField", 1), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_material_file_set_f32(file, "BaseTexture", 1.0F), RADICLE_STATUS_INVALID_ARGUMENT);

  radicle_material_field_type type = RADICLE_MATERIAL_FIELD_BOOL;
  EXPECT_EQ(radicle_material_file_field_type(file, 100000, &type), RADICLE_STATUS_INVALID_ARGUMENT);
  uint8_t* name = nullptr;
  size_t name_size = 0;
  EXPECT_EQ(radicle_material_file_field_name(file, 100000, &name, &name_size), RADICLE_STATUS_INVALID_ARGUMENT);

  radicle_material_file_destroy(file);
}

TEST(CApiMaterialFile, NullArgumentsReturnInvalidArgument) {
  const auto bytes = fo4::test::material_bgem({.version = 2});
  radicle_material_file* file = nullptr;
  radicle_material_format format{};
  uint32_t version = 0;
  size_t count = 0;
  uint8_t* out = nullptr;
  size_t size = 0;
  uint8_t flag = 0;
  float real = 0.0F;
  radicle_material_color3 color{};
  radicle_material_field_type type{};

  EXPECT_EQ(radicle_material_file_read(nullptr, 4, &file), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_material_file_read(as_u8(bytes), bytes.size(), nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_material_file_format(nullptr, &format), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_material_file_version(nullptr, &version), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_material_file_field_count(nullptr, &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_material_file_field_name(nullptr, 0, &out, &size), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_material_file_field_type(nullptr, 0, &type), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_material_file_get_bool(nullptr, "EmitEnabled", &flag), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_material_file_get_f32(nullptr, "Smoothness", &real), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_material_file_get_color3(nullptr, "EmittanceColor", &color), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_material_file_get_string(nullptr, "BaseTexture", &out, &size), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_material_file_write(nullptr, &out, &size), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_material_file_set_bool(nullptr, "EmitEnabled", 1), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_material_file_set_color3(nullptr, "EmittanceColor", &color), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_material_file_set_string(nullptr, "BaseTexture", nullptr, 0), RADICLE_STATUS_INVALID_ARGUMENT);

  radicle_material_file_destroy(nullptr);

  file = open_material(bytes);
  ASSERT_NE(file, nullptr);
  EXPECT_EQ(radicle_material_file_get_bool(file, nullptr, &flag), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_material_file_get_color3(file, "EmittanceColor", nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_material_file_write(file, nullptr, &size), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_material_file_set_color3(file, "EmittanceColor", nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_material_file_set_string(file, "BaseTexture", nullptr, 3), RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_material_file_destroy(file);
}
