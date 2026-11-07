#include "fo4/swf/swf.hpp"
#include "swf_movie_builder.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using fo4::swf::SwfCompression;
using fo4::swf::SwfFile;
using fo4::swf::SwfFormat;
using fo4::swf::SwfRect;
using fo4::swf::SwfStatus;
using swf_test::SwfBuilder;
using swf_test::wrap_body;

std::vector<std::byte> attribute_payload(std::uint32_t raw) {
  SwfBuilder builder;
  builder.u32(raw);
  return builder.body;
}

std::vector<std::byte> metadata_payload(std::string_view text) {
  SwfBuilder builder;
  builder.text(text);
  return builder.body;
}

std::vector<std::byte> symbol_class_payload() {
  SwfBuilder builder;
  builder.u16(2);
  builder.u16(1);
  builder.text("Frame");
  builder.u16(2);
  builder.text("Menu");
  return builder.body;
}

std::vector<std::byte> export_payload() {
  SwfBuilder builder;
  builder.u16(1);
  builder.u16(2);
  builder.text("Menu");
  return builder.body;
}

std::vector<std::byte> do_abc_payload(std::uint32_t flags, std::string_view name,
                                     std::span<const std::byte> abc) {
  SwfBuilder builder;
  builder.u32(flags);
  builder.text(name);
  builder.bytes(abc);
  return builder.body;
}

std::vector<std::byte> sprite_payload() {
  SwfBuilder builder;
  builder.u16(7);
  builder.u16(2);
  SwfBuilder nested;
  nested.u8(0x02);
  nested.u16(1);
  nested.u16(5);
  builder.tag(26, nested.body, false);
  builder.tag(1);
  builder.tag(0);
  return builder.body;
}

std::vector<std::byte> synthetic_body() {
  const std::vector<std::byte> abc{std::byte{0x10}, std::byte{0x00}, std::byte{0x2E},
                                   std::byte{0x00}, std::byte{0x00}};
  SwfBuilder builder;
  builder.rect(0, 11000, 0, 8000, 15);
  builder.u16(0x0C00);
  builder.u16(3);
  const auto attributes = attribute_payload(0x79u);
  builder.tag(69, attributes, false);
  SwfBuilder color;
  color.u8(0x11);
  color.u8(0x22);
  color.u8(0x33);
  builder.tag(9, color.body, false);
  const auto metadata = metadata_payload("<rdf:RDF/>");
  builder.tag(77, metadata, false);
  SwfBuilder limits;
  limits.u16(16);
  limits.u16(5);
  builder.tag(65, limits.body, false);
  const auto symbols = symbol_class_payload();
  builder.tag(76, symbols, false);
  const auto exports = export_payload();
  builder.tag(56, exports, false);
  builder.tag(72, abc, false);
  builder.tag(1);
  builder.tag(0);
  return builder.body;
}

std::vector<std::byte> synthetic_file() {
  const auto body = synthetic_body();
  return wrap_body(body, 10, false, false);
}

std::size_t do_abc_index(const SwfFile& file) {
  for (std::size_t index = 0; index < file.tags.size(); ++index) {
    if (file.tags[index].code == fo4::swf::tag::kDoAbc) {
      return index;
    }
  }
  return file.tags.size();
}

std::vector<std::byte> tag_count_movie(unsigned tags) {
  SwfBuilder builder;
  builder.rect(0, 200, 0, 100, 9);
  builder.u16(0x1800);
  builder.u16(1);
  for (unsigned index = 1; index < tags; ++index) {
    builder.tag(1);
  }
  builder.tag(0);
  return wrap_body(builder.body, 9, false, false);
}

}  // namespace

TEST(SwfContainerTest, ReadsUncompressedFileAndDecodesCoreTags) {
  const auto bytes = synthetic_file();
  const auto result = fo4::swf::read_swf(bytes);

  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto& header = result.file.header;
  EXPECT_EQ(header.version, 10);
  EXPECT_EQ(header.format, SwfFormat::swf);
  EXPECT_EQ(header.compression, SwfCompression::none);
  EXPECT_EQ(header.file_length, bytes.size());
  EXPECT_EQ(header.frame_size, (SwfRect{0, 11000, 0, 8000, 15}));
  EXPECT_EQ(header.frame_rate, 0x0C00);
  EXPECT_DOUBLE_EQ(header.frame_rate_fps(), 12.0);
  EXPECT_EQ(header.frame_count, 3);

  ASSERT_EQ(result.file.tags.size(), 9u);
  EXPECT_EQ(result.file.tags[0].code, fo4::swf::tag::kFileAttributes);
  EXPECT_EQ(result.file.tags[0].offset, 15u);
  ASSERT_TRUE(result.file.tags[0].attributes.has_value());
  EXPECT_EQ(result.file.tags[0].attributes->raw, 0x79u);
  EXPECT_TRUE(result.file.tags[0].attributes->has_metadata);
  EXPECT_TRUE(result.file.tags[0].attributes->action_script3);
  EXPECT_TRUE(result.file.tags[0].attributes->use_network);
  EXPECT_TRUE(result.file.tags[0].attributes->use_gpu);
  EXPECT_TRUE(result.file.tags[0].attributes->use_direct_blit);

  ASSERT_TRUE(result.file.tags[1].background_color.has_value());
  EXPECT_EQ(*result.file.tags[1].background_color, 0x112233u);
  ASSERT_TRUE(result.file.tags[2].metadata.has_value());
  EXPECT_EQ(*result.file.tags[2].metadata, "<rdf:RDF/>");
  ASSERT_TRUE(result.file.tags[3].script_limits.has_value());
  EXPECT_EQ(result.file.tags[3].script_limits->max_recursion_depth, 16);
  EXPECT_EQ(result.file.tags[3].script_limits->script_timeout_seconds, 5);

  ASSERT_TRUE(result.file.tags[4].symbols.has_value());
  ASSERT_EQ(result.file.tags[4].symbols->size(), 2u);
  EXPECT_EQ((*result.file.tags[4].symbols)[0].tag_id, 1);
  EXPECT_EQ((*result.file.tags[4].symbols)[0].name, "Frame");
  EXPECT_EQ((*result.file.tags[4].symbols)[1].tag_id, 2);
  EXPECT_EQ((*result.file.tags[4].symbols)[1].name, "Menu");
  ASSERT_TRUE(result.file.tags[5].exports.has_value());
  ASSERT_EQ(result.file.tags[5].exports->size(), 1u);
  EXPECT_EQ((*result.file.tags[5].exports)[0].name, "Menu");

  ASSERT_TRUE(result.file.tags[6].abc.has_value());
  EXPECT_EQ(result.file.tags[6].abc->flags, 0u);
  EXPECT_TRUE(result.file.tags[6].abc->name.empty());
  EXPECT_EQ(result.file.tags[6].abc->payload_size, 5u);
  const auto abc = fo4::swf::abc_payload(result.file.tags[6]);
  ASSERT_EQ(abc.size(), 5u);
  EXPECT_EQ(std::to_integer<std::uint8_t>(abc[0]), 0x10);
  EXPECT_EQ(std::to_integer<std::uint8_t>(abc[2]), 0x2E);
  EXPECT_TRUE(fo4::swf::abc_payload(result.file.tags[0]).empty());
  EXPECT_EQ(result.file.tags[8].code, fo4::swf::tag::kEnd);
  EXPECT_EQ(result.file.tags[8].length, 0u);

  const auto blocks = fo4::swf::abc_blocks(result.file);
  ASSERT_EQ(blocks.size(), 1u);
  EXPECT_EQ(blocks[0].tag_index, 6u);
  EXPECT_EQ(blocks[0].payload.size(), 5u);

  const auto summary = fo4::swf::summarize(result.file);
  EXPECT_EQ(summary.version, 10);
  EXPECT_FALSE(summary.compressed);
  EXPECT_FALSE(summary.is_gfx);
  EXPECT_TRUE(summary.action_script3);
  EXPECT_TRUE(summary.has_metadata);
  EXPECT_TRUE(summary.use_network);
  EXPECT_EQ(summary.frame_count, 3);
  EXPECT_EQ(summary.tag_count, 9u);
  EXPECT_EQ(summary.symbol_classes.size(), 2u);
  EXPECT_EQ(summary.export_names, (std::vector<std::string>{"Menu"}));
  ASSERT_EQ(summary.abc_blocks.size(), 1u);
  EXPECT_EQ(summary.abc_blocks[0].size, 5u);
  EXPECT_EQ(fo4::swf::tag_code_name(fo4::swf::tag::kSymbolClass), "SymbolClass");
  EXPECT_EQ(fo4::swf::tag_code_name(9999), "Unknown");
  EXPECT_EQ(fo4::swf::status_name(SwfStatus::malformed_tag), "malformed_tag");
}

TEST(SwfContainerTest, FileAttributesUseTheSpecBitLayout) {
  const auto attributes = attribute_payload(0x08u);
  SwfBuilder builder;
  builder.rect(0, 10, 0, 10, 5);
  builder.u16(0x0C00);
  builder.u16(1);
  builder.tag(69, attributes, false);
  builder.tag(0);
  const auto bytes = wrap_body(builder.body, 10, false, false);

  const auto result = fo4::swf::read_swf(bytes);
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  ASSERT_TRUE(result.file.tags[0].attributes.has_value());
  EXPECT_TRUE(result.file.tags[0].attributes->action_script3);
  EXPECT_FALSE(result.file.tags[0].attributes->has_metadata);
  EXPECT_FALSE(result.file.tags[0].attributes->use_network);
  const auto summary = fo4::swf::summarize(result.file);
  EXPECT_TRUE(summary.action_script3);
  EXPECT_FALSE(summary.has_metadata);
}

TEST(SwfContainerTest, RoundTripsUncompressedCompressedAndGfxFilesByteIdentically) {
  const auto plain = synthetic_file();
  const auto plain_result = fo4::swf::read_swf(plain);
  ASSERT_TRUE(plain_result.ok());
  EXPECT_EQ(fo4::swf::write_swf(plain_result.file), plain);

  const auto body = synthetic_body();
  const auto compressed = wrap_body(body, 10, false, true);
  const auto compressed_result = fo4::swf::read_swf(compressed);
  ASSERT_TRUE(compressed_result.ok()) << fo4::swf::status_name(compressed_result.status);
  EXPECT_EQ(compressed_result.file.header.compression, SwfCompression::zlib);
  EXPECT_EQ(compressed_result.file.source_body, body);
  EXPECT_EQ(compressed_result.file.tags.size(), 9u);
  EXPECT_TRUE(fo4::swf::summarize(compressed_result.file).compressed);
  EXPECT_EQ(fo4::swf::write_swf(compressed_result.file), compressed);

  for (const bool compressed_variant : {false, true}) {
    const auto gfx = wrap_body(body, 4, true, compressed_variant);
    const auto gfx_result = fo4::swf::read_swf(gfx);
    ASSERT_TRUE(gfx_result.ok()) << fo4::swf::status_name(gfx_result.status);
    EXPECT_EQ(gfx_result.file.header.format, SwfFormat::gfx);
    EXPECT_TRUE(fo4::swf::summarize(gfx_result.file).is_gfx);
    EXPECT_EQ(fo4::swf::write_swf(gfx_result.file), gfx);
  }

  const auto as_uncompressed = fo4::swf::write_swf(compressed_result.file, SwfCompression::none);
  EXPECT_EQ(as_uncompressed, plain);
}

TEST(SwfContainerTest, ReadsNestedSpriteTagStream) {
  const auto sprite = sprite_payload();
  SwfBuilder builder;
  builder.rect(0, 200, 0, 100, 9);
  builder.u16(0x1800);
  builder.u16(1);
  builder.tag(39, sprite, false);
  builder.tag(0);
  const auto bytes = wrap_body(builder.body, 9, false, false);

  const auto result = fo4::swf::read_swf(bytes);
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  ASSERT_EQ(result.file.tags.size(), 2u);
  const auto& sprite_tag = result.file.tags[0];
  ASSERT_TRUE(sprite_tag.sprite_id.has_value());
  EXPECT_EQ(*sprite_tag.sprite_id, 7);
  ASSERT_TRUE(sprite_tag.sprite_frame_count.has_value());
  EXPECT_EQ(*sprite_tag.sprite_frame_count, 2);
  ASSERT_EQ(sprite_tag.children.size(), 3u);
  EXPECT_EQ(sprite_tag.children[0].code, fo4::swf::tag::kPlaceObject2);
  EXPECT_EQ(sprite_tag.children[0].offset, 6u);
  EXPECT_EQ(sprite_tag.children[0].payload.size(), 5u);
  EXPECT_EQ(sprite_tag.children[1].code, fo4::swf::tag::kShowFrame);
  EXPECT_EQ(sprite_tag.children[2].code, fo4::swf::tag::kEnd);
  EXPECT_EQ(sprite_tag.payload, sprite);
  EXPECT_EQ(fo4::swf::summarize(result.file).tag_count, 5u);
  EXPECT_EQ(fo4::swf::write_swf(result.file), bytes);
}

TEST(SwfContainerTest, ReadsLongTagHeaderAndPreservesTheHeaderForm) {
  const auto long_metadata = metadata_payload(std::string(100, 'x'));
  SwfBuilder builder;
  builder.rect(0, 10, 0, 10, 5);
  builder.u16(0x0C00);
  builder.u16(1);
  builder.tag(77, long_metadata, false);
  builder.tag(1, std::span<const std::byte>{}, true);
  builder.tag(0);
  const auto bytes = wrap_body(builder.body, 10, false, false);

  const auto result = fo4::swf::read_swf(bytes);
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  ASSERT_EQ(result.file.tags.size(), 3u);
  EXPECT_TRUE(result.file.tags[0].long_header);
  EXPECT_EQ(result.file.tags[0].length, 101u);
  ASSERT_TRUE(result.file.tags[0].metadata.has_value());
  EXPECT_EQ(result.file.tags[0].metadata->size(), 100u);
  EXPECT_TRUE(result.file.tags[1].long_header);
  EXPECT_EQ(result.file.tags[1].length, 0u);
  EXPECT_FALSE(result.file.tags[2].long_header);
  EXPECT_EQ(fo4::swf::write_swf(result.file), bytes);
}

TEST(SwfContainerTest, ReportsTruncatedTagStreamAndKeepsEarlierTags) {
  const auto bytes = synthetic_file();
  const std::vector<std::byte> missing_end(bytes.begin(), bytes.end() - 1);
  const auto end_result = fo4::swf::read_swf(missing_end);
  EXPECT_EQ(end_result.status, SwfStatus::truncated_tag);
  EXPECT_EQ(end_result.file.tags.size(), 8u);
  EXPECT_EQ(end_result.file.trailing_bytes.size(), 1u);

  const std::vector<std::byte> missing_abc(bytes.begin(), bytes.end() - 6);
  const auto abc_result = fo4::swf::read_swf(missing_abc);
  EXPECT_EQ(abc_result.status, SwfStatus::truncated_tag);
  ASSERT_EQ(abc_result.file.tags.size(), 7u);
  const auto& last = abc_result.file.tags.back();
  EXPECT_EQ(last.code, fo4::swf::tag::kDoAbc);
  EXPECT_EQ(last.length, 5u);
  EXPECT_EQ(last.payload.size(), 3u);
  EXPECT_FALSE(last.abc.has_value());
  EXPECT_EQ(abc_result.file.tags[0].code, fo4::swf::tag::kFileAttributes);
}

TEST(SwfContainerTest, RejectsBadSignaturesAndTruncatedHeaders) {
  const auto empty = fo4::swf::read_swf(std::span<const std::byte>{});
  EXPECT_EQ(empty.status, SwfStatus::empty_input);

  const auto short_header = fo4::swf::read_swf(
      std::vector<std::byte>{std::byte{'F'}, std::byte{'W'}, std::byte{'S'}, std::byte{10}});
  EXPECT_EQ(short_header.status, SwfStatus::truncated_header);

  auto bad = synthetic_file();
  bad[0] = std::byte{'X'};
  const auto bad_signature = fo4::swf::read_swf(bad);
  EXPECT_EQ(bad_signature.status, SwfStatus::bad_signature);
  EXPECT_EQ(bad_signature.file.tags.size(), 0u);

  const std::vector<std::byte> tiny_body{std::byte{0x00}, std::byte{0x00}, std::byte{0x00}};
  const auto tiny = wrap_body(tiny_body, 10, false, false);
  const auto tiny_result = fo4::swf::read_swf(tiny);
  EXPECT_EQ(tiny_result.status, SwfStatus::truncated_header);
  EXPECT_EQ(tiny_result.file.header.version, 10);
}

TEST(SwfContainerTest, ReportsZwsWithoutAValidStreamAsDecompressFailure) {
  SwfBuilder builder;
  builder.u8(13);
  builder.u32(0x1000);
  for (int index = 0; index < 16; ++index) {
    builder.u8(0x5A);
  }
  std::vector<std::byte> bytes;
  bytes.push_back(std::byte{'Z'});
  bytes.push_back(std::byte{'W'});
  bytes.push_back(std::byte{'S'});
  bytes.insert(bytes.end(), builder.body.begin(), builder.body.end());

  const auto result = fo4::swf::read_swf(bytes);
  EXPECT_EQ(result.status, SwfStatus::decompress_failed);
  EXPECT_EQ(result.file.header.compression, SwfCompression::lzma);
  EXPECT_EQ(result.file.header.version, 13);
  EXPECT_EQ(result.file.header.file_length, 0x1000u);
  EXPECT_EQ(result.file.tags.size(), 0u);
}

TEST(SwfContainerTest, ReportsDecompressFailure) {
  SwfBuilder builder;
  builder.u8(10);
  builder.u32(0x200);
  for (int index = 0; index < 12; ++index) {
    builder.u8(0x7F);
  }
  std::vector<std::byte> bytes;
  bytes.push_back(std::byte{'C'});
  bytes.push_back(std::byte{'W'});
  bytes.push_back(std::byte{'S'});
  bytes.insert(bytes.end(), builder.body.begin(), builder.body.end());

  const auto result = fo4::swf::read_swf(bytes);
  EXPECT_EQ(result.status, SwfStatus::decompress_failed);
  EXPECT_FALSE(result.message.empty());
  EXPECT_EQ(result.file.tags.size(), 0u);
}

TEST(SwfContainerTest, ReportsMalformedTagPayloadAndKeepsTheRawBytes) {
  const std::vector<std::byte> nameless{std::byte{0x01}, std::byte{0x00}, std::byte{0x00},
                                        std::byte{0x00}};
  SwfBuilder builder;
  builder.rect(0, 10, 0, 10, 5);
  builder.u16(0x0C00);
  builder.u16(1);
  builder.tag(82, nameless, false);
  SwfBuilder broken_symbols;
  broken_symbols.u16(2);
  broken_symbols.u16(1);
  broken_symbols.text("Frame");
  builder.tag(76, broken_symbols.body, false);
  builder.tag(1);
  builder.tag(0);
  const auto bytes = wrap_body(builder.body, 10, false, false);

  const auto result = fo4::swf::read_swf(bytes);
  EXPECT_EQ(result.status, SwfStatus::malformed_tag);
  ASSERT_EQ(result.file.tags.size(), 4u);
  EXPECT_EQ(result.file.tags[0].payload, nameless);
  EXPECT_FALSE(result.file.tags[0].abc.has_value());
  EXPECT_FALSE(result.file.tags[1].symbols.has_value());
  EXPECT_EQ(result.file.tags[2].code, fo4::swf::tag::kShowFrame);
  EXPECT_EQ(result.file.tags[3].code, fo4::swf::tag::kEnd);
  EXPECT_EQ(fo4::swf::write_swf(result.file), bytes);
}

TEST(SwfContainerTest, ReportsDeclaredLengthMismatchWithoutFailingTheRead) {
  const auto body = synthetic_body();
  auto longer = wrap_body(body, 10, false, false);
  const auto declared = static_cast<std::uint32_t>(longer.size() + 8);
  longer[4] = static_cast<std::byte>(declared & 0xFFu);
  longer[5] = static_cast<std::byte>((declared >> 8) & 0xFFu);
  longer[6] = static_cast<std::byte>((declared >> 16) & 0xFFu);
  longer[7] = static_cast<std::byte>((declared >> 24) & 0xFFu);

  const auto long_result = fo4::swf::read_swf(longer);
  EXPECT_TRUE(long_result.ok());
  EXPECT_FALSE(long_result.declared_length_matches);
  EXPECT_EQ(long_result.file.tags.size(), 9u);

  const auto shorter_declared = static_cast<std::uint32_t>(longer.size() - 2);
  longer[4] = static_cast<std::byte>(shorter_declared & 0xFFu);
  longer[5] = static_cast<std::byte>((shorter_declared >> 8) & 0xFFu);
  longer[6] = static_cast<std::byte>((shorter_declared >> 16) & 0xFFu);
  longer[7] = static_cast<std::byte>((shorter_declared >> 24) & 0xFFu);

  const auto short_result = fo4::swf::read_swf(longer);
  EXPECT_TRUE(short_result.ok());
  EXPECT_FALSE(short_result.declared_length_matches);
  EXPECT_EQ(short_result.file.tags.size(), 8u);
  EXPECT_EQ(short_result.file.trailing_bytes.size(), 2u);
}

TEST(SwfContainerTest, ReplacesTagPayloadByIndex) {
  auto result = fo4::swf::read_swf(synthetic_file());
  ASSERT_TRUE(result.ok());
  const auto index = do_abc_index(result.file);
  ASSERT_LT(index, result.file.tags.size());

  const std::vector<std::byte> abc{std::byte{0xAA}, std::byte{0xBB}};
  EXPECT_TRUE(fo4::swf::replace_tag_payload(result.file, index, abc));
  EXPECT_EQ(result.file.tags[index].length, abc.size());
  ASSERT_TRUE(result.file.tags[index].abc.has_value());
  EXPECT_EQ(result.file.tags[index].abc->flags, 0u);
  EXPECT_TRUE(result.file.tags[index].abc->name.empty());
  const auto payload_bytes = fo4::swf::abc_payload(result.file.tags[index]);
  EXPECT_EQ(std::vector<std::byte>(payload_bytes.begin(), payload_bytes.end()), abc);

  const auto written = fo4::swf::write_swf(result.file);
  auto reread = fo4::swf::read_swf(written);
  ASSERT_TRUE(reread.ok()) << fo4::swf::status_name(reread.status);
  ASSERT_EQ(reread.file.tags.size(), 9u);
  ASSERT_TRUE(reread.file.tags[index].abc.has_value());
  EXPECT_TRUE(reread.file.tags[index].abc->name.empty());
  const auto reread_bytes = fo4::swf::abc_payload(reread.file.tags[index]);
  EXPECT_EQ(std::vector<std::byte>(reread_bytes.begin(), reread_bytes.end()), abc);
  EXPECT_EQ(reread.file.tags[1].payload, result.file.tags[1].payload);
  EXPECT_EQ(reread.file.header.frame_size, result.file.header.frame_size);

  const auto before = fo4::swf::write_swf(reread.file);
  EXPECT_FALSE(fo4::swf::replace_tag_payload(reread.file, 99, abc));
  EXPECT_EQ(fo4::swf::write_swf(reread.file), before);

  EXPECT_TRUE(fo4::swf::replace_tag_payload(reread.file, 7, std::span<const std::byte>{}));
  EXPECT_EQ(reread.file.tags[7].payload.size(), 0u);
  EXPECT_EQ(reread.file.tags[7].length, 0u);
  const auto after = fo4::swf::read_swf(fo4::swf::write_swf(reread.file));
  EXPECT_TRUE(after.ok());
  EXPECT_EQ(after.file.tags[7].code, fo4::swf::tag::kShowFrame);
  EXPECT_EQ(after.file.tags[7].payload.size(), 0u);
}

TEST(SwfContainerTest, ReadsAbc2InitActionBinaryDataAndScaleformTags) {
  const std::vector<std::byte> abc{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};
  SwfBuilder builder;
  builder.rect(0, 10, 0, 10, 5);
  builder.u16(0x0C00);
  builder.u16(1);
  const auto abc2 = do_abc_payload(1, "Menu", abc);
  builder.tag(82, abc2, false);
  SwfBuilder init_action;
  init_action.u16(5);
  init_action.u8(0x81);
  init_action.u8(0x00);
  init_action.u8(0x00);
  builder.tag(59, init_action.body, false);
  SwfBuilder action;
  action.u8(0x07);
  action.u8(0x00);
  builder.tag(12, action.body, false);
  SwfBuilder binary;
  binary.u16(9);
  binary.u32(0);
  binary.u8(0xDE);
  binary.u8(0xAD);
  builder.tag(87, binary.body, false);
  SwfBuilder exporter;
  exporter.u16(0x0401);
  exporter.u32(2);
  exporter.u16(2);
  exporter.sized_text("Menu");
  exporter.sized_text("HUDMenu");
  exporter.u16(0);
  builder.tag(1000, exporter.body, false);
  SwfBuilder external;
  external.u16(4);
  external.u16(2);
  external.u16(26);
  external.u16(26);
  external.sized_text("Z.png");
  external.sized_text("Z.png.dds");
  builder.tag(1001, external.body, false);
  SwfBuilder font;
  font.u32(6);
  font.u16(2);
  font.sized_text("Font");
  font.u16(512);
  font.u16(512);
  font.u8(1);
  font.u16(20);
  font.u16(0);
  font.u16(0);
  builder.tag(1002, font.body, false);
  builder.tag(0);
  const auto bytes = wrap_body(builder.body, 10, false, false);

  const auto result = fo4::swf::read_swf(bytes);
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  ASSERT_EQ(result.file.tags.size(), 8u);
  ASSERT_TRUE(result.file.tags[0].abc.has_value());
  EXPECT_EQ(result.file.tags[0].abc->flags, 1u);
  EXPECT_EQ(result.file.tags[0].abc->name, "Menu");
  const auto abc2_payload = fo4::swf::abc_payload(result.file.tags[0]);
  EXPECT_EQ(std::vector<std::byte>(abc2_payload.begin(), abc2_payload.end()), abc);
  ASSERT_TRUE(result.file.tags[1].action.has_value());
  EXPECT_EQ(result.file.tags[1].action->sprite_id, 5);
  EXPECT_EQ(result.file.tags[1].action->offset, 2u);
  EXPECT_EQ(result.file.tags[1].action->size, 3u);
  ASSERT_TRUE(result.file.tags[2].action.has_value());
  EXPECT_EQ(result.file.tags[2].action->sprite_id, 0);
  EXPECT_EQ(result.file.tags[2].action->size, 2u);
  ASSERT_TRUE(result.file.tags[3].binary_data.has_value());
  EXPECT_EQ(result.file.tags[3].binary_data->tag_id, 9);
  EXPECT_EQ(result.file.tags[3].binary_data->reserved, 0u);
  EXPECT_EQ(result.file.tags[3].binary_data->offset, 6u);
  EXPECT_EQ(result.file.tags[3].binary_data->size, 2u);
  EXPECT_EQ(result.file.tags[3].payload[6], std::byte{0xDE});
  EXPECT_FALSE(result.file.tags[4].attributes.has_value());
  EXPECT_EQ(fo4::swf::tag_code_name(result.file.tags[4].code), "ExporterInfo");
  EXPECT_EQ(fo4::swf::tag_code_name(result.file.tags[5].code), "DefineExternalImage");
  EXPECT_EQ(fo4::swf::tag_code_name(result.file.tags[6].code), "FontTextureInfo");
  ASSERT_TRUE(result.file.tags[4].exporter_info.has_value());
  EXPECT_EQ(result.file.tags[4].exporter_info->swf_name, "HUDMenu");
  ASSERT_TRUE(result.file.tags[5].external_image.has_value());
  EXPECT_EQ(result.file.tags[5].external_image->file_name, "Z.png.dds");
  ASSERT_TRUE(result.file.tags[6].font_texture.has_value());
  EXPECT_EQ(result.file.tags[6].font_texture->texture_width, 512);
  EXPECT_EQ(fo4::swf::write_swf(result.file), bytes);
}

TEST(SwfContainerTest, RoundTripsBytesAfterTheDeclaredLengthByteExactly) {
  const auto body = synthetic_body();
  auto bytes = wrap_body(body, 10, false, false);
  const std::vector<std::byte> tail{std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE}};
  bytes.insert(bytes.end(), tail.begin(), tail.end());

  const auto result = fo4::swf::read_swf(bytes);
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  EXPECT_FALSE(result.declared_length_matches);
  EXPECT_EQ(result.file.trailing_bytes, tail);
  EXPECT_EQ(fo4::swf::write_swf(result.file), bytes);
}

TEST(SwfContainerTest, RoundTripsAZeroBitFrameRectByteExactly) {
  SwfBuilder builder;
  builder.rect(0, 0, 0, 0, 0);
  builder.u16(0x0C00);
  builder.u16(1);
  builder.tag(0);
  const auto bytes = wrap_body(builder.body, 10, false, false);

  const auto result = fo4::swf::read_swf(bytes);
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  EXPECT_EQ(result.file.header.frame_size.nbits, 0);
  EXPECT_EQ(result.file.header.frame_rate, 0x0C00);
  EXPECT_EQ(fo4::swf::write_swf(result.file), bytes);
}

TEST(SwfContainerTest, RoundTripsNonZeroRectPaddingBitsByteExactly) {
  SwfBuilder builder;
  builder.rect(0, 10, 0, 10, 5, 0x7Fu);
  builder.u16(0x0C00);
  builder.u16(1);
  builder.tag(0);
  const auto bytes = wrap_body(builder.body, 10, false, false);

  const auto result = fo4::swf::read_swf(bytes);
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  EXPECT_EQ(fo4::swf::write_swf(result.file), bytes);
}

TEST(SwfContainerTest, AcceptsTagCountAtTheLimit) {
  const auto bytes = tag_count_movie(4);
  const auto result = fo4::swf::read_swf(bytes, 4);
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  EXPECT_EQ(result.file.tags.size(), 4u);
}

TEST(SwfContainerTest, RejectsOneTagOverTheLimitWithoutPushingIt) {
  const auto bytes = tag_count_movie(5);
  const auto result = fo4::swf::read_swf(bytes, 4);
  EXPECT_EQ(result.status, SwfStatus::too_many_tags);
  EXPECT_FALSE(result.message.empty());
  EXPECT_EQ(result.file.tags.size(), 4u);
}

TEST(SwfContainerTest, CountsSpriteChildrenAgainstTheWholeFileBudget) {
  SwfBuilder sprite;
  sprite.u16(7);
  sprite.u16(1);
  sprite.tag(1);
  sprite.tag(1);
  sprite.tag(1);
  sprite.tag(0);
  SwfBuilder builder;
  builder.rect(0, 200, 0, 100, 9);
  builder.u16(0x1800);
  builder.u16(1);
  builder.tag(39, sprite.body, false);
  builder.tag(1);
  builder.tag(0);
  const auto bytes = wrap_body(builder.body, 9, false, false);

  const auto result = fo4::swf::read_swf(bytes, 7);
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  EXPECT_EQ(fo4::swf::summarize(result.file).tag_count, 7u);

  const auto over = fo4::swf::read_swf(bytes, 6);
  EXPECT_EQ(over.status, SwfStatus::too_many_tags);
  EXPECT_FALSE(over.message.empty());
}
