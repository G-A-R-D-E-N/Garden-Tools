#include "fo4/swf/swf.hpp"
#include "swf_movie_builder.hpp"

#include <gtest/gtest.h>
#include <zlib.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using fo4::swf::SwfFile;
using fo4::swf::SwfStatus;
using swf_test::SwfBuilder;
using swf_test::wrap_body;

std::vector<std::byte> movie_with(std::uint16_t code, std::span<const std::byte> payload) {
  SwfBuilder builder;
  builder.rect(0, 11000, 0, 8000, 15);
  builder.u16(0x0C00);
  builder.u16(1);
  builder.tag(code, payload, false);
  builder.tag(0);
  return wrap_body(builder.body, 10, false, false);
}

std::vector<std::byte> shape_tail(SwfBuilder& builder) {
  builder.u8(0);
  builder.u8(0);
  builder.u8(0);
  builder.push_bits(0, 6);
  builder.align();
  return builder.body;
}

std::vector<std::byte> shape_payload(std::uint16_t shape_id) {
  SwfBuilder builder;
  builder.u16(shape_id);
  builder.rect(0, 200, 0, 100, 9);
  return shape_tail(builder);
}

std::vector<std::byte> shape4_payload() {
  SwfBuilder builder;
  builder.u16(10);
  builder.rect(0, 200, 0, 100, 9);
  builder.rect(0, 190, 0, 90, 9);
  builder.u8(0x05);
  return shape_tail(builder);
}

std::vector<std::byte> zlib_bytes(std::span<const std::uint8_t> raw) {
  uLongf packed = compressBound(static_cast<uLong>(raw.size()));
  std::vector<std::byte> buffer(static_cast<std::size_t>(packed));
  EXPECT_EQ(compress2(reinterpret_cast<Bytef*>(buffer.data()), &packed,
                      reinterpret_cast<const Bytef*>(raw.data()), static_cast<uLong>(raw.size()),
                      Z_BEST_COMPRESSION),
            Z_OK);
  buffer.resize(static_cast<std::size_t>(packed));
  return buffer;
}

std::vector<std::byte> lossless_payload(std::uint16_t character_id, std::uint8_t format,
                                        std::uint16_t width, std::uint16_t height,
                                        std::optional<std::uint8_t> palette) {
  SwfBuilder builder;
  builder.u16(character_id);
  builder.u8(format);
  builder.u16(width);
  builder.u16(height);
  const auto pixels = static_cast<std::size_t>(format == 3 ? 1 : (format == 4 ? 2 : 4));
  const auto row = (static_cast<std::size_t>(width) * pixels + 3u) & ~std::size_t{3};
  std::vector<std::uint8_t> raw(row * height, 0x40);
  if (palette.has_value()) {
    builder.u8(*palette);
    raw.insert(raw.begin(), static_cast<std::size_t>(*palette + 1) * 3u, 0x11);
  }
  builder.bytes(zlib_bytes(raw));
  return builder.body;
}

std::vector<std::byte> font_payload() {
  SwfBuilder builder;
  builder.u16(11);
  builder.u16(6);
  builder.u16(8);
  builder.u16(10);
  builder.u16(12);
  builder.u32(0x00010203u);
  return builder.body;
}

std::vector<std::byte> font2_payload() {
  SwfBuilder builder;
  builder.u16(12);
  builder.u8(0x0Cu);
  builder.u8(1);
  builder.sized_text("Body");
  builder.u16(2);
  builder.u32(12);
  builder.u32(20);
  builder.u32(24);
  return builder.body;
}

std::vector<std::byte> font3_payload() {
  SwfBuilder builder;
  builder.u16(13);
  builder.u8(0x00u);
  builder.u8(1);
  builder.sized_text("Cn");
  builder.u16(3);
  builder.u16(6);
  builder.u16(8);
  builder.u16(10);
  builder.u16(12);
  return builder.body;
}

std::vector<std::byte> text_payload(std::uint16_t character_id, bool rgba) {
  SwfBuilder builder;
  builder.u16(character_id);
  builder.rect(0, 1000, 0, 500, 11);
  builder.push_bits(0, 1);
  builder.push_bits(0, 1);
  builder.push_bits(0, 5);
  builder.align();
  // Entry widths must leave a record off the byte boundary or the align regression is invisible
  builder.u8(6);
  builder.u8(7);
  builder.u8(0x8Eu);
  builder.u16(11);
  builder.u8(0x11);
  builder.u8(0x22);
  builder.u8(0x33);
  if (rgba) {
    builder.u8(0x44);
  }
  builder.u16(100);
  builder.u16(400);
  builder.u8(2);
  builder.push_bits(1, 6);
  builder.push_bits(20, 7);
  builder.push_bits(2, 6);
  builder.push_bits(30, 7);
  builder.align();
  builder.u8(0x88u);
  builder.u16(12);
  builder.u16(300);
  builder.u8(1);
  builder.push_bits(3, 6);
  builder.push_bits(40, 7);
  builder.align();
  builder.u8(0x00);
  return builder.body;
}

std::vector<std::byte> sound_payload() {
  SwfBuilder builder;
  builder.u16(30);
  builder.u8(0x2Fu);
  builder.u32(1024);
  builder.u32(0x0000FFF2u);
  return builder.body;
}

std::vector<std::byte> exporter_info_payload() {
  SwfBuilder builder;
  builder.u16(0x0401);
  builder.u32(0x00000003u);
  builder.u16(2);
  builder.sized_text("Menu");
  builder.sized_text("HUDMenu");
  builder.u16(2);
  builder.u32(0x10);
  builder.u32(0x20);
  return builder.body;
}

std::vector<std::byte> external_image_payload(std::uint32_t character_id, bool wide,
                                              std::string_view export_name,
                                              std::string_view file_name) {
  SwfBuilder builder;
  if (wide) {
    builder.u32(character_id);
  } else {
    builder.u16(character_id);
  }
  builder.u16(2);
  builder.u16(26);
  builder.u16(26);
  builder.sized_text(export_name);
  builder.sized_text(file_name);
  return builder.body;
}

std::vector<std::byte> font_texture_payload() {
  SwfBuilder builder;
  builder.u32(6);
  builder.u16(2);
  builder.sized_text("Font");
  builder.u16(512);
  builder.u16(512);
  builder.u8(1);
  builder.u16(20);
  builder.u16(1);
  for (int index = 0; index < 6; ++index) {
    builder.u32(0x3F800000u);
  }
  builder.u16(1);
  builder.u16(12);
  builder.u16(1);
  builder.u16(0);
  builder.u16(1);
  return builder.body;
}

std::vector<std::byte> background_color_payload(std::uint32_t alpha) {
  SwfBuilder builder;
  builder.u8(0x11);
  builder.u8(0x22);
  builder.u8(0x33);
  if (alpha != 0) {
    builder.u8(alpha);
  }
  return builder.body;
}

std::vector<std::byte> all_kinds_body() {
  SwfBuilder builder;
  builder.rect(0, 11000, 0, 8000, 15);
  builder.u16(0x0C00);
  builder.u16(1);
  const auto shape = shape_payload(7);
  builder.tag(2, shape, false);
  const auto shape2 = shape_payload(8);
  builder.tag(22, shape2, false);
  const auto shape3 = shape_payload(9);
  builder.tag(32, shape3, false);
  const auto shape4 = shape4_payload();
  builder.tag(83, shape4, false);
  const auto lossless = lossless_payload(3, 3, 16, 8, 255);
  builder.tag(20, lossless, false);
  const auto lossless2 = lossless_payload(4, 4, 32, 16, std::nullopt);
  builder.tag(36, lossless2, false);
  const auto font = font_payload();
  builder.tag(10, font, false);
  const auto font2 = font2_payload();
  builder.tag(48, font2, false);
  const auto font3 = font3_payload();
  builder.tag(75, font3, false);
  const auto text = text_payload(20, false);
  builder.tag(11, text, false);
  const auto text2 = text_payload(21, true);
  builder.tag(33, text2, false);
  const auto sound = sound_payload();
  builder.tag(14, sound, false);
  const auto background = background_color_payload(0x44);
  builder.tag(9, background, false);
  const auto exporter = exporter_info_payload();
  builder.tag(1000, exporter, false);
  const auto external = external_image_payload(4, false, "Z.png", "Z.png.dds");
  builder.tag(1001, external, false);
  const auto external4 = external_image_payload(0x00010001u, true, "Y.png", "Y.png.dds");
  builder.tag(1009, external4, false);
  const auto texture = font_texture_payload();
  builder.tag(1002, texture, false);
  builder.tag(1);
  builder.tag(0);
  return builder.body;
}

std::vector<std::byte> all_kinds_movie() {
  return wrap_body(all_kinds_body(), 10, false, false);
}

std::size_t tag_index(const SwfFile& file, std::uint16_t code) {
  for (std::size_t index = 0; index < file.tags.size(); ++index) {
    if (file.tags[index].code == code) {
      return index;
    }
  }
  return file.tags.size();
}

struct BrokenTag {
  std::uint16_t code{0};
  const char* name{nullptr};
  std::vector<std::byte> payload;
};

}  // namespace

TEST(SwfTagDecode, EveryNewTagPayloadDecodesInIsolation) {
  struct Case {
    std::uint16_t code;
    std::vector<std::byte> payload;
  };
  std::vector<Case> cases;
  cases.push_back({2, shape_payload(7)});
  cases.push_back({22, shape_payload(8)});
  cases.push_back({32, shape_payload(9)});
  cases.push_back({83, shape4_payload()});
  cases.push_back({20, lossless_payload(3, 3, 16, 8, 255)});
  cases.push_back({36, lossless_payload(4, 4, 32, 16, std::nullopt)});
  cases.push_back({10, font_payload()});
  cases.push_back({48, font2_payload()});
  cases.push_back({75, font3_payload()});
  cases.push_back({11, text_payload(20, false)});
  cases.push_back({33, text_payload(21, true)});
  cases.push_back({14, sound_payload()});
  cases.push_back({9, background_color_payload(0x44)});
  cases.push_back({1000, exporter_info_payload()});
  cases.push_back({1001, external_image_payload(4, false, "Z.png", "Z.png.dds")});
  cases.push_back({1009, external_image_payload(0x00010001u, true, "Y.png", "Y.png.dds")});
  cases.push_back({1002, font_texture_payload()});
  for (const Case& item : cases) {
    const auto bytes = movie_with(item.code, item.payload);
    const auto result = fo4::swf::read_swf(bytes);
    EXPECT_TRUE(result.ok()) << "tag " << item.code << " returned "
                             << fo4::swf::status_name(result.status);
  }
}

TEST(SwfTagDecode, DecodesShapeBoundsForEveryDefineShapeVariant) {
  const auto bytes = all_kinds_movie();
  const auto result = fo4::swf::read_swf(bytes);
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto& tags = result.file.tags;

  ASSERT_TRUE(tags[tag_index(result.file, 2)].shape.has_value());
  const auto& shape = *tags[tag_index(result.file, 2)].shape;
  EXPECT_EQ(shape.shape_id, 7);
  EXPECT_EQ(shape.bounds, (fo4::swf::SwfRect{0, 200, 0, 100, 9}));
  EXPECT_FALSE(shape.has_edge_bounds);

  ASSERT_TRUE(tags[tag_index(result.file, 22)].shape.has_value());
  EXPECT_EQ(tags[tag_index(result.file, 22)].shape->shape_id, 8);
  EXPECT_EQ(tags[tag_index(result.file, 22)].shape->bounds,
            (fo4::swf::SwfRect{0, 200, 0, 100, 9}));
  ASSERT_TRUE(tags[tag_index(result.file, 32)].shape.has_value());
  EXPECT_EQ(tags[tag_index(result.file, 32)].shape->shape_id, 9);

  ASSERT_TRUE(tags[tag_index(result.file, 83)].shape.has_value());
  const auto& shape4 = *tags[tag_index(result.file, 83)].shape;
  EXPECT_EQ(shape4.shape_id, 10);
  EXPECT_TRUE(shape4.has_edge_bounds);
  EXPECT_EQ(shape4.edge_bounds, (fo4::swf::SwfRect{0, 190, 0, 90, 9}));
  EXPECT_EQ(shape4.flags, 0x05);
}

TEST(SwfTagDecode, DecodesLosslessBitmapHeaderAndPaletteSize) {
  const auto result = fo4::swf::read_swf(all_kinds_movie());
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);

  const auto& palettized = result.file.tags[tag_index(result.file, 20)];
  ASSERT_TRUE(palettized.bitmap.has_value());
  EXPECT_EQ(palettized.bitmap->character_id, 3);
  EXPECT_EQ(palettized.bitmap->format, 3);
  EXPECT_EQ(palettized.bitmap->width, 16);
  EXPECT_EQ(palettized.bitmap->height, 8);
  ASSERT_TRUE(palettized.bitmap->color_table_size.has_value());
  EXPECT_EQ(*palettized.bitmap->color_table_size, 255);

  const auto& direct = result.file.tags[tag_index(result.file, 36)];
  ASSERT_TRUE(direct.bitmap.has_value());
  EXPECT_EQ(direct.bitmap->character_id, 4);
  EXPECT_EQ(direct.bitmap->format, 4);
  EXPECT_EQ(direct.bitmap->width, 32);
  EXPECT_EQ(direct.bitmap->height, 16);
  EXPECT_FALSE(direct.bitmap->color_table_size.has_value());
}

TEST(SwfTagDecode, DecodesFontGlyphCountsForEveryDefineFontVariant) {
  const auto result = fo4::swf::read_swf(all_kinds_movie());
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);

  const auto& font = result.file.tags[tag_index(result.file, 10)];
  ASSERT_TRUE(font.font.has_value());
  EXPECT_EQ(font.font->font_id, 11);
  EXPECT_EQ(font.font->glyph_count, 3u);
  EXPECT_TRUE(font.font->name.empty());

  const auto& font2 = result.file.tags[tag_index(result.file, 48)];
  ASSERT_TRUE(font2.font.has_value());
  EXPECT_EQ(font2.font->font_id, 12);
  EXPECT_EQ(font2.font->glyph_count, 2u);
  EXPECT_EQ(font2.font->name, "Body");
  EXPECT_EQ(font2.font->language, 1);
  EXPECT_TRUE(font2.font->wide_offsets);
  EXPECT_TRUE(font2.font->wide_codes);

  const auto& font3 = result.file.tags[tag_index(result.file, 75)];
  ASSERT_TRUE(font3.font.has_value());
  EXPECT_EQ(font3.font->font_id, 13);
  EXPECT_EQ(font3.font->glyph_count, 3u);
  EXPECT_EQ(font3.font->name, "Cn");
  EXPECT_FALSE(font3.font->wide_offsets);
  EXPECT_TRUE(font3.font->wide_codes);
}

TEST(SwfTagDecode, CountsTextRecordsAndGlyphEntries) {
  const auto result = fo4::swf::read_swf(all_kinds_movie());
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);

  const auto& text = result.file.tags[tag_index(result.file, 11)];
  ASSERT_TRUE(text.text.has_value());
  EXPECT_EQ(text.text->character_id, 20);
  EXPECT_EQ(text.text->bounds, (fo4::swf::SwfRect{0, 1000, 0, 500, 11}));
  EXPECT_EQ(text.text->record_count, 2u);
  EXPECT_EQ(text.text->glyph_count, 3u);

  const auto& text2 = result.file.tags[tag_index(result.file, 33)];
  ASSERT_TRUE(text2.text.has_value());
  EXPECT_EQ(text2.text->character_id, 21);
  EXPECT_EQ(text2.text->record_count, 2u);
  EXPECT_EQ(text2.text->glyph_count, 3u);
}

TEST(SwfTagDecode, DecodesSoundHeaderAndSampleCount) {
  const auto result = fo4::swf::read_swf(all_kinds_movie());
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);

  const auto& sound = result.file.tags[tag_index(result.file, 14)];
  ASSERT_TRUE(sound.sound.has_value());
  EXPECT_EQ(sound.sound->sound_id, 30);
  EXPECT_EQ(sound.sound->format, 2);
  EXPECT_EQ(sound.sound->rate, 3);
  EXPECT_TRUE(sound.sound->sample_16bit);
  EXPECT_TRUE(sound.sound->stereo);
  EXPECT_EQ(sound.sound->sample_count, 1024u);
  EXPECT_EQ(sound.sound->data_size, 4u);
}

TEST(SwfTagDecode, DecodesScaleformExporterImageAndFontTextureTags) {
  const auto result = fo4::swf::read_swf(all_kinds_movie());
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);

  const auto& exporter = result.file.tags[tag_index(result.file, 1000)];
  ASSERT_TRUE(exporter.exporter_info.has_value());
  EXPECT_EQ(exporter.exporter_info->version, 0x0401);
  EXPECT_EQ(exporter.exporter_info->flags, 3u);
  EXPECT_EQ(exporter.exporter_info->bitmap_format, 2);
  EXPECT_EQ(exporter.exporter_info->prefix, "Menu");
  EXPECT_EQ(exporter.exporter_info->swf_name, "HUDMenu");
  EXPECT_EQ(exporter.exporter_info->code_offset_count, 2u);

  const auto& external = result.file.tags[tag_index(result.file, 1001)];
  ASSERT_TRUE(external.external_image.has_value());
  EXPECT_EQ(external.external_image->character_id, 4u);
  EXPECT_EQ(external.external_image->bitmap_format, 2);
  EXPECT_EQ(external.external_image->target_width, 26);
  EXPECT_EQ(external.external_image->target_height, 26);
  EXPECT_EQ(external.external_image->export_name, "Z.png");
  EXPECT_EQ(external.external_image->file_name, "Z.png.dds");

  const auto& external4 = result.file.tags[tag_index(result.file, 1009)];
  ASSERT_TRUE(external4.external_image.has_value());
  EXPECT_EQ(external4.external_image->character_id, 0x00010001u);
  EXPECT_EQ(external4.external_image->file_name, "Y.png.dds");

  const auto& texture = result.file.tags[tag_index(result.file, 1002)];
  ASSERT_TRUE(texture.font_texture.has_value());
  EXPECT_EQ(texture.font_texture->texture_id, 6u);
  EXPECT_EQ(texture.font_texture->texture_format, 2);
  EXPECT_EQ(texture.font_texture->file_name, "Font");
  EXPECT_EQ(texture.font_texture->texture_width, 512);
  EXPECT_EQ(texture.font_texture->texture_height, 512);
  EXPECT_EQ(texture.font_texture->pad_pixels, 1);
  EXPECT_EQ(texture.font_texture->nominal_glyph_size, 20);
  EXPECT_EQ(texture.font_texture->texture_glyph_count, 1u);
  EXPECT_EQ(texture.font_texture->font_count, 1u);
}

TEST(SwfTagDecode, ExporterInfoVersionGateSkipsFlagsBelow0x10A) {
  SwfBuilder below;
  below.u16(0x0109);
  below.u16(2);
  below.sized_text("Menu");
  below.sized_text("HUDMenu");
  const auto below_result = fo4::swf::read_swf(movie_with(1000, below.body));
  ASSERT_TRUE(below_result.ok()) << fo4::swf::status_name(below_result.status);
  const auto& below_info = below_result.file.tags[0].exporter_info;
  ASSERT_TRUE(below_info.has_value());
  EXPECT_EQ(below_info->version, 0x0109);
  EXPECT_EQ(below_info->flags, 0u);
  EXPECT_EQ(below_info->bitmap_format, 2);
  EXPECT_EQ(below_info->prefix, "Menu");
  EXPECT_EQ(below_info->swf_name, "HUDMenu");
  EXPECT_EQ(below_info->code_offset_count, 0u);

  SwfBuilder at;
  at.u16(0x010A);
  at.u32(0x00000003u);
  at.u16(2);
  at.sized_text("Menu");
  at.sized_text("HUDMenu");
  const auto at_result = fo4::swf::read_swf(movie_with(1000, at.body));
  ASSERT_TRUE(at_result.ok()) << fo4::swf::status_name(at_result.status);
  const auto& at_info = at_result.file.tags[0].exporter_info;
  ASSERT_TRUE(at_info.has_value());
  EXPECT_EQ(at_info->version, 0x010A);
  EXPECT_EQ(at_info->flags, 3u);
  EXPECT_EQ(at_info->bitmap_format, 2);
  EXPECT_EQ(at_info->prefix, "Menu");
  EXPECT_EQ(at_info->swf_name, "HUDMenu");
  EXPECT_EQ(at_info->code_offset_count, 0u);
}

TEST(SwfTagDecode, DefineFontTenBoundAcceptsTheMinimumTail) {
  SwfBuilder minimum;
  minimum.u16(11);
  minimum.u16(2);
  minimum.u16(0);
  const auto accepted = fo4::swf::read_swf(movie_with(10, minimum.body));
  ASSERT_TRUE(accepted.ok()) << fo4::swf::status_name(accepted.status);
  ASSERT_TRUE(accepted.file.tags[0].font.has_value());
  EXPECT_EQ(accepted.file.tags[0].font->font_id, 11);
  EXPECT_EQ(accepted.file.tags[0].font->glyph_count, 1u);

  SwfBuilder short_tail;
  short_tail.u16(11);
  short_tail.u16(2);
  short_tail.u8(0);
  const auto rejected = fo4::swf::read_swf(movie_with(10, short_tail.body));
  EXPECT_EQ(rejected.status, SwfStatus::malformed_tag);
  EXPECT_FALSE(rejected.file.tags[0].font.has_value());
}

TEST(SwfTagDecode, BackgroundColorKeepsTheFourthAlphaByte) {
  const auto four_byte = movie_with(9, background_color_payload(0x44));
  const auto four_result = fo4::swf::read_swf(four_byte);
  ASSERT_TRUE(four_result.ok()) << fo4::swf::status_name(four_result.status);
  ASSERT_TRUE(four_result.file.tags[0].background_color.has_value());
  EXPECT_EQ(*four_result.file.tags[0].background_color, 0x44112233u);
  EXPECT_EQ(fo4::swf::write_swf(four_result.file), four_byte);

  const auto three_byte = movie_with(9, background_color_payload(0));
  const auto three_result = fo4::swf::read_swf(three_byte);
  ASSERT_TRUE(three_result.ok()) << fo4::swf::status_name(three_result.status);
  ASSERT_TRUE(three_result.file.tags[0].background_color.has_value());
  EXPECT_EQ(*three_result.file.tags[0].background_color, 0x112233u);
}

TEST(SwfTagDecode, SummarizesAndRoundTripsAMovieWithEveryNewTagKind) {
  const auto bytes = all_kinds_movie();
  const auto result = fo4::swf::read_swf(bytes);
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);

  const auto summary = fo4::swf::summarize(result.file);
  EXPECT_EQ(summary.tag_count, 19u);
  EXPECT_EQ(summary.shape_tags, 4u);
  EXPECT_EQ(summary.bitmap_tags, 2u);
  EXPECT_EQ(summary.font_tags, 3u);
  EXPECT_EQ(summary.text_tags, 2u);
  EXPECT_EQ(summary.sound_tags, 1u);
  EXPECT_EQ(summary.exporter_info_tags, 1u);
  EXPECT_EQ(summary.external_image_tags, 2u);
  EXPECT_EQ(summary.font_texture_tags, 1u);

  EXPECT_EQ(fo4::swf::tag_code_name(1009), "DefineExternalImage");
  EXPECT_EQ(fo4::swf::write_swf(result.file), bytes);
}

TEST(SwfTagDecode, TruncatedPayloadsKeepRawBytesAndParsingContinues) {
  const std::vector<BrokenTag> broken{
      {2, "DefineShape id without bounds", {std::byte{0x07}, std::byte{0x00}}},
      {22, "DefineShape2 bounds cut mid-rectangle",
       {std::byte{0x08}, std::byte{0x00}, std::byte{0x78}}},
      {32, "DefineShape3 rectangle without values",
       {std::byte{0x09}, std::byte{0x00}, std::byte{0x7B}, std::byte{0xFF}}},
      {83, "DefineShape4 edge bounds cut",
       {std::byte{0x0A}, std::byte{0x00}, std::byte{0x78}, std::byte{0x00}, std::byte{0x04},
        std::byte{0x4C}, std::byte{0x20}, std::byte{0x00}, std::byte{0x01}, std::byte{0x8F},
        std::byte{0x00}, std::byte{0x7B}}},
      {20, "DefineBitsLossless without height",
       {std::byte{0x03}, std::byte{0x00}, std::byte{0x03}, std::byte{0x10}, std::byte{0x00}}},
      {36, "DefineBitsLossless2 palette byte missing",
       {std::byte{0x04}, std::byte{0x00}, std::byte{0x03}, std::byte{0x20}, std::byte{0x00},
        std::byte{0x10}, std::byte{0x00}}},
      {10, "DefineFont offset table overruns",
       {std::byte{0x0B}, std::byte{0x00}, std::byte{0x04}, std::byte{0x00}, std::byte{0x06},
        std::byte{0x00}}},
      {48, "DefineFont2 glyph count overruns",
       {std::byte{0x0C}, std::byte{0x00}, std::byte{0x8C}, std::byte{0x01}, std::byte{0x04},
        std::byte{'B'}, std::byte{'o'}, std::byte{'d'}, std::byte{'y'}, std::byte{0x00},
        std::byte{0x40}}},
      {75, "DefineFont3 offset table overruns",
       {std::byte{0x0D}, std::byte{0x00}, std::byte{0x84}, std::byte{0x01}, std::byte{0x02},
        std::byte{'C'}, std::byte{'n'}, std::byte{0x05}, std::byte{0x00}}},
      {11, "DefineText glyph entries cut",
       {std::byte{0x14}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x08},
        std::byte{0x08}, std::byte{0x88}, std::byte{0x0B}, std::byte{0x00}, std::byte{0x90},
        std::byte{0x01}, std::byte{0x04}, std::byte{0x01}, std::byte{0x14}}},
      {33, "DefineText2 record colour cut",
       {std::byte{0x15}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x08},
        std::byte{0x08}, std::byte{0x84}, std::byte{0x11}, std::byte{0x22}}},
      {14, "DefineSound sample count cut",
       {std::byte{0x1E}, std::byte{0x00}, std::byte{0x2F}, std::byte{0x00}, std::byte{0x04}}},
      {1000, "ExporterInfo prefix cut",
       {std::byte{0x01}, std::byte{0x04}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x02}, std::byte{0x00}, std::byte{0x08}, std::byte{'M'},
        std::byte{'e'}}},
      {1001, "DefineExternalImage file name cut",
       {std::byte{0x04}, std::byte{0x00}, std::byte{0x02}, std::byte{0x00}, std::byte{0x1A},
        std::byte{0x00}, std::byte{0x1A}, std::byte{0x00}, std::byte{0x05}, std::byte{'Z'},
        std::byte{'.'}, std::byte{'p'}}},
      {1009, "DefineExternalImage V4 character id cut",
       {std::byte{0x01}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x02},
        std::byte{0x00}}},
      {1002, "FontTextureInfo glyph array overruns",
       {std::byte{0x06}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x02},
        std::byte{0x00}, std::byte{0x04}, std::byte{'F'}, std::byte{'o'}, std::byte{'n'},
        std::byte{'t'}, std::byte{0x00}, std::byte{0x02}, std::byte{0x00}, std::byte{0x02},
        std::byte{0x01}, std::byte{0x14}, std::byte{0x00}, std::byte{0x02}, std::byte{0x00}}},
  };

  for (const auto& entry : broken) {
    SCOPED_TRACE(entry.name);
    SwfBuilder builder;
    builder.rect(0, 100, 0, 100, 9);
    builder.u16(0x0C00);
    builder.u16(1);
    builder.tag(entry.code, entry.payload, false);
    builder.tag(1);
    builder.tag(0);
    const auto bytes = wrap_body(builder.body, 10, false, false);

    const auto result = fo4::swf::read_swf(bytes);
    EXPECT_EQ(result.status, SwfStatus::malformed_tag);
    ASSERT_EQ(result.file.tags.size(), 3u);
    EXPECT_EQ(result.file.tags[0].code, entry.code);
    EXPECT_EQ(result.file.tags[0].payload, entry.payload);
    EXPECT_FALSE(result.file.tags[0].shape.has_value());
    EXPECT_FALSE(result.file.tags[0].bitmap.has_value());
    EXPECT_FALSE(result.file.tags[0].font.has_value());
    EXPECT_FALSE(result.file.tags[0].text.has_value());
    EXPECT_FALSE(result.file.tags[0].sound.has_value());
    EXPECT_FALSE(result.file.tags[0].exporter_info.has_value());
    EXPECT_FALSE(result.file.tags[0].external_image.has_value());
    EXPECT_FALSE(result.file.tags[0].font_texture.has_value());
    EXPECT_EQ(result.file.tags[1].code, fo4::swf::tag::kShowFrame);
    EXPECT_EQ(result.file.tags[2].code, fo4::swf::tag::kEnd);
    EXPECT_EQ(fo4::swf::write_swf(result.file), bytes);
  }
}

TEST(SwfTags, ReadsImportAssetsAndImportAssets2WithTheSharedMovie) {
  SwfBuilder first;
  first.text("Shared.swf");
  first.u16(2);
  first.u16(10);
  first.text("FontA");
  first.u16(11);
  first.text("IconB");
  const auto imported =
      fo4::swf::read_swf(movie_with(fo4::swf::tag::kImportAssets, first.body));
  ASSERT_EQ(imported.status, SwfStatus::ok);
  ASSERT_TRUE(imported.file.tags[0].imports.has_value());
  const auto& one = *imported.file.tags[0].imports;
  EXPECT_EQ(one.version, 1);
  EXPECT_EQ(one.source_movie, "Shared.swf");
  ASSERT_EQ(one.symbols.size(), 2u);
  EXPECT_EQ(one.symbols[0].tag_id, 10);
  EXPECT_EQ(one.symbols[0].name, "FontA");
  EXPECT_EQ(one.symbols[1].tag_id, 11);
  EXPECT_EQ(one.symbols[1].name, "IconB");

  SwfBuilder second;
  second.text("Shared2.swf");
  second.u8(0xAB);
  second.u8(0xCD);
  second.u16(1);
  second.u16(12);
  second.text("Thing");
  const auto imported2 =
      fo4::swf::read_swf(movie_with(fo4::swf::tag::kImportAssets2, second.body));
  ASSERT_EQ(imported2.status, SwfStatus::ok);
  ASSERT_TRUE(imported2.file.tags[0].imports.has_value());
  const auto& two = *imported2.file.tags[0].imports;
  EXPECT_EQ(two.version, 2);
  EXPECT_EQ(two.source_movie, "Shared2.swf");
  ASSERT_EQ(two.symbols.size(), 1u);
  EXPECT_EQ(two.symbols[0].name, "Thing");
}

TEST(SwfTags, RefusesAnImportAssetListThatRunsOutOfBytes) {
  SwfBuilder truncated;
  truncated.text("Shared.swf");
  truncated.u16(2);
  truncated.u16(10);
  truncated.text("FontA");
  const auto result =
      fo4::swf::read_swf(movie_with(fo4::swf::tag::kImportAssets, truncated.body));
  EXPECT_EQ(result.status, SwfStatus::malformed_tag);
  ASSERT_EQ(result.file.tags.size(), 2u);
  EXPECT_FALSE(result.file.tags[0].imports.has_value());
  EXPECT_EQ(result.file.tags[0].payload, truncated.body);

  SwfBuilder unterminated;
  unterminated.text("Shared.swf");
  unterminated.u16(1);
  unterminated.u16(10);
  const auto no_name =
      fo4::swf::read_swf(movie_with(fo4::swf::tag::kImportAssets2, unterminated.body));
  EXPECT_EQ(no_name.status, SwfStatus::malformed_tag);
}
