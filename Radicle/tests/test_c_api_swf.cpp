#include "abc_test_support.hpp"
#include "fo4/c_api.h"
#include "fo4/c_api_env.h"
#include "fo4/c_api_swf.h"
#include "swf_art_fixtures.hpp"
#include "swf_movie_builder.hpp"
#include "temp_data_dir.hpp"

#include <gtest/gtest.h>
#include <zlib.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace {

using swf_test::SwfBuilder;
using swf_test::wrap_body;

struct SwfDeleter {
  void operator()(radicle_swf* swf) const { radicle_swf_destroy(swf); }
};

using SwfHandle = std::unique_ptr<radicle_swf, SwfDeleter>;

SwfHandle open(const std::vector<std::byte>& bytes) {
  radicle_swf* raw = nullptr;
  EXPECT_EQ(radicle_swf_open_bytes(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size(), &raw),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  return SwfHandle(raw);
}

SwfHandle open_abc_movie() {
  const auto abc = fo4::test::build_abc();
  SwfBuilder file;
  file.rect(0, 11000, 0, 8000, 15);
  file.u16(0x0C00);
  file.u16(2);

  SwfBuilder attributes;
  attributes.u32(0x79u);
  file.tag(69, attributes.body, false);

  SwfBuilder label;
  label.text("start");
  file.tag(43, label.body, false);

  SwfBuilder symbols;
  symbols.u16(1);
  symbols.u16(7);
  symbols.text("Menu");
  file.tag(76, symbols.body, false);

  SwfBuilder exports;
  exports.u16(1);
  exports.u16(7);
  exports.text("Menu");
  file.tag(56, exports.body, false);

  SwfBuilder abc_tag;
  abc_tag.u32(0);
  abc_tag.text("frame 1");
  abc_tag.bytes(std::span<const std::byte>(reinterpret_cast<const std::byte*>(abc.data()), abc.size()));
  file.tag(82, abc_tag.body, false);

  SwfBuilder sprite;
  sprite.u16(7);
  sprite.u16(1);
  SwfBuilder nested;
  nested.tag(1);
  nested.tag(0);
  sprite.bytes(nested.body);
  file.tag(39, sprite.body, false);

  file.tag(1);
  file.tag(0);
  return open(wrap_body(file.body, 10, false, false));
}

std::string text_of(const uint8_t* bytes, size_t size) {
  return bytes == nullptr ? std::string() : std::string(reinterpret_cast<const char*>(bytes), size);
}

}  // namespace

// Regression: the flattened tree must keep the sprite parent, the frame label and the payload copy.
TEST(CApiSwfTest, WalksTheTagTreeAndReadsLabelsSymbolsAndExports) {
  const auto file = open_abc_movie();
  ASSERT_NE(file, nullptr);

  radicle_swf_info info{};
  ASSERT_EQ(radicle_swf_get_info(file.get(), &info), RADICLE_STATUS_OK);
  EXPECT_EQ(info.format, 0);
  EXPECT_EQ(info.compression, 0);
  EXPECT_EQ(info.version, 10);
  EXPECT_EQ(info.frame_count, 2);
  EXPECT_EQ(info.frame_rate, 12.0f);
  EXPECT_EQ(info.action_script3, 1);
  EXPECT_EQ(info.use_gpu, 1);
  EXPECT_EQ(info.use_direct_blit, 1);
  EXPECT_EQ(info.declared_length_matches, 1);
  EXPECT_EQ(info.tag_count, 10u);
  EXPECT_EQ(info.abc_count, 1u);
  EXPECT_EQ(info.symbol_count, 1u);
  EXPECT_EQ(info.export_count, 1u);
  EXPECT_EQ(info.frame_label_count, 1u);

  radicle_swf_tag tag{};
  ASSERT_EQ(radicle_swf_tag_at(file.get(), 0, &tag), RADICLE_STATUS_OK);
  EXPECT_EQ(tag.code, 69);
  EXPECT_EQ(tag.parent, RADICLE_SWF_NO_INDEX);
  EXPECT_EQ(tag.inside_sprite, 0);
  EXPECT_STREQ(tag.name, "FileAttributes");

  ASSERT_EQ(radicle_swf_tag_at(file.get(), 1, &tag), RADICLE_STATUS_OK);
  EXPECT_EQ(tag.code, 43);
  ASSERT_EQ(radicle_swf_tag_at(file.get(), 2, &tag), RADICLE_STATUS_OK);
  EXPECT_EQ(tag.code, 76);
  ASSERT_EQ(radicle_swf_tag_at(file.get(), 3, &tag), RADICLE_STATUS_OK);
  EXPECT_EQ(tag.code, 56);
  ASSERT_EQ(radicle_swf_tag_at(file.get(), 4, &tag), RADICLE_STATUS_OK);
  EXPECT_EQ(tag.code, 82);

  // A sprite child must sit after its sprite tag and must point its parent back at it.
  ASSERT_EQ(radicle_swf_tag_at(file.get(), 5, &tag), RADICLE_STATUS_OK);
  EXPECT_EQ(tag.code, 39);
  EXPECT_EQ(tag.sprite_id, 7u);
  const auto sprite_index = static_cast<size_t>(5);
  ASSERT_EQ(radicle_swf_tag_at(file.get(), 6, &tag), RADICLE_STATUS_OK);
  EXPECT_EQ(tag.code, 1);
  EXPECT_EQ(tag.parent, sprite_index);
  EXPECT_EQ(tag.inside_sprite, 1);
  EXPECT_EQ(tag.sprite_id, 7u);
  ASSERT_EQ(radicle_swf_tag_at(file.get(), 7, &tag), RADICLE_STATUS_OK);
  EXPECT_EQ(tag.code, 0);
  EXPECT_EQ(tag.parent, sprite_index);
  ASSERT_EQ(radicle_swf_tag_at(file.get(), 8, &tag), RADICLE_STATUS_OK);
  EXPECT_EQ(tag.code, 1);
  EXPECT_EQ(tag.parent, RADICLE_SWF_NO_INDEX);
  ASSERT_EQ(radicle_swf_tag_at(file.get(), 9, &tag), RADICLE_STATUS_OK);
  EXPECT_EQ(tag.code, 0);

  EXPECT_EQ(radicle_swf_tag_at(file.get(), 10, &tag), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_swf_tag_at(nullptr, 0, &tag), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_swf_tag_count(nullptr), 0u);

  uint8_t* payload = nullptr;
  size_t payload_size = 0;
  ASSERT_EQ(radicle_swf_tag_payload(file.get(), 0, &payload, &payload_size), RADICLE_STATUS_OK);
  ASSERT_NE(payload, nullptr);
  EXPECT_EQ(payload_size, 4u);
  radicle_free_bytes(payload);
  EXPECT_EQ(radicle_swf_tag_payload(file.get(), 0, nullptr, &payload_size), RADICLE_STATUS_INVALID_ARGUMENT);

  radicle_swf_symbol symbol{};
  ASSERT_EQ(radicle_swf_symbol_at(file.get(), 0, &symbol), RADICLE_STATUS_OK);
  EXPECT_EQ(symbol.tag_id, 7u);
  EXPECT_STREQ(symbol.name, "Menu");
  ASSERT_EQ(radicle_swf_export_at(file.get(), 0, &symbol), RADICLE_STATUS_OK);
  EXPECT_STREQ(symbol.name, "Menu");
  EXPECT_EQ(radicle_swf_symbol_at(file.get(), 1, &symbol), RADICLE_STATUS_INVALID_ARGUMENT);

  radicle_swf_frame_label label{};
  ASSERT_EQ(radicle_swf_frame_label_at(file.get(), 0, &label), RADICLE_STATUS_OK);
  EXPECT_STREQ(label.name, "start");
  EXPECT_EQ(label.tag_index, 1u);
  EXPECT_EQ(label.inside_sprite, 0);
  EXPECT_EQ(radicle_swf_frame_label_count(nullptr), 0u);
}

// Regression: a truncated body and an LZMA movie must be refused, and outputs must be cleared first.
TEST(CApiSwfTest, RefusesTruncatedLzmaAndBadArgumentsWithoutLeavingOutputsDirty) {
  SwfBuilder builder;
  builder.rect(0, 100, 0, 100, 9);
  builder.u16(0x0C00);
  builder.u16(1);
  builder.tag(1);
  builder.tag(0);
  const auto movie = wrap_body(builder.body, 9, false, true);

  const auto file = open(movie);
  ASSERT_NE(file, nullptr);
  radicle_swf_info info{};
  ASSERT_EQ(radicle_swf_get_info(file.get(), &info), RADICLE_STATUS_OK);
  EXPECT_EQ(info.compression, 1);
  EXPECT_EQ(info.tag_count, 2u);

  const std::vector<std::byte> truncated(movie.begin(), movie.end() - 4);
  radicle_swf* raw = nullptr;
  EXPECT_EQ(radicle_swf_open_bytes(reinterpret_cast<const std::uint8_t*>(truncated.data()), truncated.size(), &raw),
            RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(raw, nullptr);

  std::vector<uint8_t> lzma{'Z', 'W', 'S', 13, 0, 0, 0, 0, 1, 2, 3};
  EXPECT_EQ(radicle_swf_open_bytes(lzma.data(), lzma.size(), &raw), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_NE(std::string(radicle_last_error()).find("LZMA"), std::string::npos);

  EXPECT_EQ(radicle_swf_open_bytes(nullptr, 0, &raw), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_swf_open_bytes(lzma.data(), lzma.size(), nullptr), RADICLE_STATUS_INVALID_ARGUMENT);

  radicle_swf_info cleared{};
  cleared.version = 9;
  EXPECT_EQ(radicle_swf_get_info(nullptr, &cleared), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(cleared.version, 0);

  radicle_swf_tag tag{};
  tag.code = 42;
  EXPECT_EQ(radicle_swf_tag_at(file.get(), 99, &tag), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(tag.code, 0);

  radicle_swf_call call{};
  call.offset = 42;
  EXPECT_EQ(radicle_swf_abc_call_at(file.get(), 0, RADICLE_SWF_OWNER_CLASS, 0, 0, 0, &call),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(call.offset, 0u);
}

// Regression: the ABC view must name its methods, calls and listings, not just count them.
TEST(CApiSwfTest, SummarizesAbcAndDisassemblesAMethodBody) {
  const auto file = open_abc_movie();
  ASSERT_NE(file, nullptr);

  radicle_swf_abc_info abc{};
  ASSERT_EQ(radicle_swf_abc_info_at(file.get(), 0, &abc), RADICLE_STATUS_OK);
  EXPECT_STREQ(abc.name, "frame 1");
  EXPECT_EQ(abc.parsed, 1);
  EXPECT_EQ(abc.tag_index, 4u);
  EXPECT_GT(abc.payload_size, 0u);

  EXPECT_EQ(radicle_swf_abc_class_count(file.get(), 0), 1u);
  radicle_swf_class klass{};
  ASSERT_EQ(radicle_swf_abc_class_at(file.get(), 0, 0, &klass), RADICLE_STATUS_OK);
  EXPECT_STREQ(klass.name, "Test");
  EXPECT_STREQ(klass.super_name, "Object");
  EXPECT_EQ(klass.interface_count, 1u);
  EXPECT_EQ(klass.method_count, 2u);

  const char* interface_name = nullptr;
  ASSERT_EQ(radicle_swf_abc_class_interface(file.get(), 0, 0, 0, &interface_name), RADICLE_STATUS_OK);
  EXPECT_STREQ(interface_name, "click");
  EXPECT_EQ(radicle_swf_abc_class_interface(file.get(), 0, 0, 1, &interface_name), RADICLE_STATUS_INVALID_ARGUMENT);

  radicle_swf_method method{};
  ASSERT_EQ(radicle_swf_abc_class_method(file.get(), 0, 0, 0, &method), RADICLE_STATUS_OK);
  EXPECT_STREQ(method.name, "<init>");
  EXPECT_STREQ(method.return_type, "void");
  EXPECT_EQ(method.instruction_count, 4u);
  EXPECT_EQ(method.call_count, 1u);
  EXPECT_EQ(method.multiname_count, 2u);

  const char* multiname = nullptr;
  ASSERT_EQ(radicle_swf_abc_multiname_at(file.get(), 0, RADICLE_SWF_OWNER_CLASS, 0, 0, 0, &multiname),
            RADICLE_STATUS_OK);
  EXPECT_STREQ(multiname, "flash.external::ExternalInterface");

  radicle_swf_call call{};
  ASSERT_EQ(radicle_swf_abc_call_at(file.get(), 0, RADICLE_SWF_OWNER_CLASS, 0, 0, 0, &call), RADICLE_STATUS_OK);
  EXPECT_STREQ(call.opcode, "callpropvoid");
  EXPECT_STREQ(call.target, "flash.external::call");
  EXPECT_EQ(call.argument_count, 1u);
  EXPECT_EQ(call.is_external_interface, 1);
  EXPECT_EQ(call.is_event_registration, 0);

  ASSERT_EQ(radicle_swf_abc_call_at(file.get(), 0, RADICLE_SWF_OWNER_CLASS, 0, 1, 0, &call), RADICLE_STATUS_OK);
  EXPECT_STREQ(call.target, "addEventListener");
  EXPECT_EQ(call.is_event_registration, 1);
  EXPECT_STREQ(call.event_name, "click");
  EXPECT_STREQ(call.handler, "handler");
  EXPECT_EQ(radicle_swf_abc_call_at(file.get(), 0, RADICLE_SWF_OWNER_CLASS, 0, 0, 4, &call),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_swf_abc_call_at(file.get(), 0, RADICLE_SWF_OWNER_CLASS, 0, 9, 0, &call), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_swf_abc_call_at(file.get(), 0, 7, 0, 0, 0, &call), RADICLE_STATUS_NOT_FOUND);

  EXPECT_EQ(radicle_swf_abc_script_count(file.get(), 0), 0u);
  ASSERT_EQ(radicle_swf_abc_function_count(file.get(), 0), 1u);
  radicle_swf_method free_function{};
  ASSERT_EQ(radicle_swf_abc_function_at(file.get(), 0, 0, &free_function), RADICLE_STATUS_OK);
  EXPECT_STREQ(free_function.name, "helper");
  EXPECT_EQ(free_function.instruction_count, 1u);
  EXPECT_EQ(free_function.call_count, 0u);
  EXPECT_EQ(radicle_swf_abc_script_at(file.get(), 0, 0, &free_function), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_swf_abc_call_at(file.get(), 0, RADICLE_SWF_OWNER_FUNCTION, 0, 0, 0, &call),
            RADICLE_STATUS_INVALID_ARGUMENT);

  // The method pool must be the view the disassembler decodes.
  ASSERT_EQ(radicle_swf_abc_method_count(file.get(), 0), 3u);
  radicle_swf_method_pool pool{};
  ASSERT_EQ(radicle_swf_abc_method_at(file.get(), 0, 0, &pool), RADICLE_STATUS_OK);
  // The pool must report the stored name ("Test"); only the summary derives "<init>".
  EXPECT_STREQ(pool.name, "Test");
  EXPECT_EQ(pool.has_body, 1);
  EXPECT_GT(pool.code_size, 0u);
  EXPECT_GE(pool.max_stack, 1u);

  uint8_t* listing = nullptr;
  size_t listing_size = 0;
  ASSERT_EQ(radicle_swf_abc_method_disassemble(file.get(), 0, 0, &listing, &listing_size), RADICLE_STATUS_OK)
      << radicle_last_error();
  const auto text = text_of(listing, listing_size);
  radicle_free_bytes(listing);
  EXPECT_NE(text.find("callpropvoid"), std::string::npos) << text;
  EXPECT_NE(text.find("flash.external::call"), std::string::npos) << text;

  EXPECT_EQ(radicle_swf_abc_method_at(file.get(), 0, 3, &pool), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_swf_abc_method_disassemble(file.get(), 0, 9, &listing, &listing_size),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(listing, nullptr);
  EXPECT_EQ(listing_size, 0u);
}

TEST(CApiSwfTest, DecodesDoAbcBlocksByTheirTagLayoutAndRefusesAnUnterminatedName) {
  const auto abc = fo4::test::build_abc();
  const auto abc_span = std::span<const std::byte>(reinterpret_cast<const std::byte*>(abc.data()), abc.size());
  const auto movie_with = [](std::uint16_t code, const SwfBuilder& payload) {
    SwfBuilder builder;
    builder.rect(0, 11000, 0, 8000, 15);
    builder.u16(0x0C00);
    builder.u16(1);
    builder.tag(code, payload.body, false);
    builder.tag(0);
    return wrap_body(builder.body, 10, false, false);
  };

  SwfBuilder abc_tag;
  abc_tag.u32(1);
  abc_tag.text("frame 1");
  abc_tag.bytes(abc_span);
  const auto file = open(movie_with(82, abc_tag));
  ASSERT_NE(file, nullptr);

  radicle_swf_abc_info info{};
  ASSERT_EQ(radicle_swf_abc_info_at(file.get(), 0, &info), RADICLE_STATUS_OK);
  EXPECT_EQ(info.parsed, 1);
  EXPECT_STREQ(info.name, "frame 1");
  EXPECT_EQ(info.flags, 1u);
  EXPECT_EQ(info.payload_size, abc.size());
  EXPECT_EQ(radicle_swf_abc_class_count(file.get(), 0), 1u);

  uint8_t* listing = nullptr;
  size_t listing_size = 0;
  ASSERT_EQ(radicle_swf_abc_method_disassemble(file.get(), 0, 0, &listing, &listing_size), RADICLE_STATUS_OK)
      << radicle_last_error();
  const auto text = text_of(listing, listing_size);
  radicle_free_bytes(listing);
  EXPECT_NE(text.find("callpropvoid"), std::string::npos) << text;

  SwfBuilder raw_tag;
  raw_tag.bytes(abc_span);
  const auto raw = open(movie_with(72, raw_tag));
  ASSERT_NE(raw, nullptr);
  ASSERT_EQ(radicle_swf_abc_info_at(raw.get(), 0, &info), RADICLE_STATUS_OK);
  EXPECT_EQ(info.parsed, 1);
  EXPECT_STREQ(info.name, "");
  EXPECT_EQ(info.flags, 0u);
  EXPECT_EQ(info.tag_index, 0u);
  EXPECT_EQ(info.payload_size, abc.size());
  EXPECT_EQ(radicle_swf_abc_class_count(raw.get(), 0), 1u);

  uint8_t* rendered_raw = nullptr;
  size_t rendered_size = 0;
  ASSERT_EQ(radicle_swf_abc_text(raw.get(), 0, &rendered_raw, &rendered_size), RADICLE_STATUS_OK)
      << radicle_last_error();
  const auto rendered = text_of(rendered_raw, rendered_size);
  radicle_free_bytes(rendered_raw);
  ASSERT_EQ(radicle_swf_abc_apply_text(raw.get(), 0, reinterpret_cast<const uint8_t*>(rendered.data()),
                                       rendered.size()), RADICLE_STATUS_OK)
      << radicle_last_error();
  uint8_t* payload = nullptr;
  size_t payload_size = 0;
  ASSERT_EQ(radicle_swf_tag_payload(raw.get(), info.tag_index, &payload, &payload_size), RADICLE_STATUS_OK);
  ASSERT_EQ(payload_size, abc.size());
  EXPECT_EQ(std::memcmp(payload, abc.data(), abc.size()), 0);
  radicle_free_bytes(payload);

  SwfBuilder unterminated;
  unterminated.u32(0);
  unterminated.u8('x');
  const auto broken = movie_with(82, unterminated);
  radicle_swf* refused = nullptr;
  EXPECT_EQ(radicle_swf_open_bytes(reinterpret_cast<const std::uint8_t*>(broken.data()), broken.size(), &refused),
            RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(refused, nullptr);
}

// Regression: a non-ASCII loose path must reach the loader as UTF-8 on Windows, and a missing or null path must be refused.
TEST(CApiSwfTest, OpensALooseMovieByUtf8PathAndRefusesMissingAndNullPaths) {
  SwfBuilder builder;
  builder.rect(0, 100, 0, 100, 9);
  builder.u16(0x0C00);
  builder.u16(1);
  builder.tag(1);
  builder.tag(0);
  const auto movie = wrap_body(builder.body, 9, false, false);

  fo4::test::TempDataDir dir;
  const auto loose = dir.path() / std::filesystem::path(u8"movie_\u00e9.swf");
  {
    std::ofstream out(loose, std::ios::binary);
    out.write(reinterpret_cast<const char*>(movie.data()), static_cast<std::streamsize>(movie.size()));
  }
  const auto loose_utf8 = loose.u8string();
  radicle_swf* raw = nullptr;
  ASSERT_EQ(radicle_swf_open_path(reinterpret_cast<const char*>(loose_utf8.c_str()), &raw), RADICLE_STATUS_OK)
      << radicle_last_error();
  const SwfHandle from_path(raw);
  const auto from_bytes = open(movie);
  radicle_swf_info path_info{};
  radicle_swf_info bytes_info{};
  ASSERT_EQ(radicle_swf_get_info(from_path.get(), &path_info), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_swf_get_info(from_bytes.get(), &bytes_info), RADICLE_STATUS_OK);
  EXPECT_EQ(path_info.tag_count, bytes_info.tag_count);
  EXPECT_EQ(path_info.frame_count, bytes_info.frame_count);

  const auto missing_utf8 = (dir.path() / "missing.swf").u8string();
  EXPECT_EQ(radicle_swf_open_path(reinterpret_cast<const char*>(missing_utf8.c_str()), &raw),
            RADICLE_STATUS_IO_ERROR);
  EXPECT_EQ(raw, nullptr);
  EXPECT_EQ(radicle_swf_open_path(nullptr, &raw), RADICLE_STATUS_INVALID_ARGUMENT);
}

// Regression: an LZMA refusal reached through an archive must keep its message after the extracted buffer is released.
TEST(CApiSwfTest, KeepsTheLzmaMessageWhenAnArchiveMovieIsRefused) {
  const std::vector<uint8_t> lzma{'Z', 'W', 'S', 13, 0, 0, 0, 0, 1, 2, 3};
  const radicle_ba2_file files[] = {{"movie.swf", lzma.data(), lzma.size()}};
  uint8_t* archive = nullptr;
  size_t archive_size = 0;
  ASSERT_EQ(radicle_ba2_write_general(files, 1, 1, 0, &archive, &archive_size), RADICLE_STATUS_OK);
  fo4::test::TempDataDir dir;
  const auto archive_path = dir.path() / "movies.ba2";
  {
    std::ofstream out(archive_path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(archive), static_cast<std::streamsize>(archive_size));
  }
  radicle_free_bytes(archive);

  radicle_ba2* raw_archive = nullptr;
  ASSERT_EQ(radicle_ba2_open(archive_path.string().c_str(), &raw_archive), RADICLE_STATUS_OK)
      << radicle_last_error();
  const std::unique_ptr<radicle_ba2, decltype(&radicle_ba2_close)> ba2(raw_archive, &radicle_ba2_close);

  radicle_swf* raw = nullptr;
  EXPECT_EQ(radicle_swf_open_ba2(ba2.get(), "movie.swf", &raw), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(raw, nullptr);
  EXPECT_NE(std::string(radicle_last_error()).find("LZMA"), std::string::npos);
}

// Regression: an unparsed DoABC block must report parsed=0 and must not answer a method pool.
TEST(CApiSwfTest, ReportsAnAbcBlockThatDoesNotParse) {
  SwfBuilder builder;
  builder.rect(0, 100, 0, 100, 9);
  builder.u16(0x0C00);
  builder.u16(1);
  SwfBuilder abc_tag;
  abc_tag.u32(0);
  abc_tag.text("broken");
  abc_tag.u8(0x10);
  builder.tag(82, abc_tag.body, false);
  builder.tag(0);
  const auto file = open(wrap_body(builder.body, 9, false, false));
  ASSERT_NE(file, nullptr);

  radicle_swf_abc_info abc{};
  ASSERT_EQ(radicle_swf_abc_info_at(file.get(), 0, &abc), RADICLE_STATUS_OK);
  EXPECT_EQ(abc.parsed, 0);
  EXPECT_STREQ(abc.name, "broken");
  EXPECT_EQ(radicle_swf_abc_class_count(file.get(), 0), 0u);
  EXPECT_EQ(radicle_swf_abc_method_count(file.get(), 0), 0u);

  radicle_swf_method_pool pool{};
  EXPECT_EQ(radicle_swf_abc_method_at(file.get(), 0, 0, &pool), RADICLE_STATUS_PARSE_ERROR);
  uint8_t* listing = nullptr;
  size_t listing_size = 0;
  EXPECT_EQ(radicle_swf_abc_method_disassemble(file.get(), 0, 0, &listing, &listing_size),
            RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(listing, nullptr);
  EXPECT_EQ(radicle_swf_abc_info_at(file.get(), 1, &abc), RADICLE_STATUS_INVALID_ARGUMENT);
}

namespace {

void art_be16(SwfBuilder& builder, std::uint32_t value) {
  builder.u8((value >> 8) & 0xFFu);
  builder.u8(value & 0xFFu);
}

std::vector<std::byte> art_shape_payload() {
  SwfBuilder body;
  body.u16(44);
  body.rect(0, 400, 0, 300, 10);
  body.u8(1);
  body.u8(0x00);
  body.u8(0x12);
  body.u8(0x34);
  body.u8(0x56);
  body.u8(0x78);
  body.u8(0);
  body.u8(0x10);
  body.push_bits(0, 1);
  body.push_bits(0x02, 5);
  body.push_bits(0, 1);
  body.push_bits(1, 1);
  body.push_bits(1, 1);
  body.push_bits(5, 4);
  body.push_bits(1, 1);
  body.push_bits(10, 7);
  body.push_bits(20, 7);
  body.push_bits(0, 6);
  body.align();
  return body.body;
}

std::vector<std::byte> art_morph_payload() {
  SwfBuilder middle;
  middle.u8(0);
  middle.u8(0);
  middle.u8(0x00);
  middle.push_bits(0, 1);
  middle.push_bits(0x01, 5);
  middle.push_bits(4, 5);
  middle.push_bits(3, 4);
  middle.push_bits(0x0E, 4);
  middle.push_bits(1, 1);
  middle.push_bits(1, 1);
  middle.push_bits(2, 4);
  middle.push_bits(1, 1);
  middle.push_bits(5, 4);
  middle.push_bits(6, 4);
  middle.push_bits(0, 6);
  middle.align();
  SwfBuilder body;
  body.u16(45);
  body.rect(0, 400, 0, 300, 12);
  body.rect(0, 500, 0, 350, 12);
  body.rect(0, 380, 0, 280, 12);
  body.rect(0, 480, 0, 330, 12);
  body.u8(0);
  body.u32(static_cast<std::uint32_t>(middle.body.size()));
  body.bytes(middle.body);
  body.u8(0);
  body.push_bits(0, 6);
  body.align();
  return body.body;
}

std::vector<std::byte> art_lossless_payload() {
  std::vector<std::uint8_t> raw(16, 0x21);
  uLongf packed = compressBound(static_cast<uLong>(raw.size()));
  std::vector<std::byte> buffer(static_cast<std::size_t>(packed));
  EXPECT_EQ(compress2(reinterpret_cast<Bytef*>(buffer.data()), &packed,
                      reinterpret_cast<const Bytef*>(raw.data()), static_cast<uLong>(raw.size()),
                      Z_BEST_COMPRESSION),
            Z_OK);
  buffer.resize(static_cast<std::size_t>(packed));
  SwfBuilder body;
  body.u16(46);
  body.u8(5);
  body.u16(2);
  body.u16(2);
  body.bytes(buffer);
  return body.body;
}

std::vector<std::byte> art_jpeg_payload() {
  SwfBuilder image;
  image.u8(0xFF);
  image.u8(0xD8);
  image.u8(0xFF);
  image.u8(0xC0);
  art_be16(image, 11);
  image.u8(8);
  art_be16(image, 3);
  art_be16(image, 2);
  image.u8(1);
  image.u8(1);
  image.u8(0x11);
  image.u8(0);
  image.u8(0xFF);
  image.u8(0xD9);
  std::vector<std::uint8_t> plane(6, 0x33);
  uLongf packed = compressBound(static_cast<uLong>(plane.size()));
  std::vector<std::byte> alpha(static_cast<std::size_t>(packed));
  EXPECT_EQ(compress2(reinterpret_cast<Bytef*>(alpha.data()), &packed,
                      reinterpret_cast<const Bytef*>(plane.data()), static_cast<uLong>(plane.size()),
                      Z_BEST_COMPRESSION),
            Z_OK);
  alpha.resize(static_cast<std::size_t>(packed));
  SwfBuilder body;
  body.u16(47);
  body.u32(static_cast<std::uint32_t>(image.body.size()));
  body.u16(0x0100);
  body.bytes(image.body);
  body.bytes(alpha);
  return body.body;
}

SwfHandle open_art_movie() {
  SwfBuilder file;
  file.rect(0, 11000, 0, 8000, 15);
  file.u16(0x0C00);
  file.u16(1);
  file.tag(32, art_shape_payload(), false);
  file.tag(84, art_morph_payload(), false);
  file.tag(36, art_lossless_payload(), false);
  file.tag(90, art_jpeg_payload(), false);
  file.tag(0);
  return open(wrap_body(file.body, 10, false, false));
}

}  // namespace

// Regression: an art getter must not report another kind of art for the tag it is given.
TEST(CApiSwfTest, ReadsDecodedShapeMorphBitmapAndJpegArt) {
  const auto file = open_art_movie();
  ASSERT_NE(file, nullptr);

  radicle_swf_shape shape{};
  ASSERT_EQ(radicle_swf_shape_at(file.get(), 0, &shape), RADICLE_STATUS_OK);
  EXPECT_EQ(shape.character_id, 44);
  EXPECT_EQ(shape.version, 3);
  EXPECT_EQ(shape.has_edge_bounds, 0);
  EXPECT_EQ(shape.bounds.x_max, 400);
  EXPECT_EQ(shape.edge_bounds.x_max, 400);
  EXPECT_EQ(shape.fill_style_count, 1u);
  EXPECT_EQ(shape.line_style_count, 0u);
  EXPECT_EQ(shape.edge_count, 1u);
  EXPECT_EQ(shape.curved_edge_count, 0u);
  EXPECT_EQ(shape.style_change_count, 1u);

  EXPECT_EQ(shape.record_count, 2u);
  radicle_swf_shape_record record{};
  ASSERT_EQ(radicle_swf_shape_record_at(file.get(), 0, 0, &record), RADICLE_STATUS_OK);
  EXPECT_EQ(record.kind, RADICLE_SWF_RECORD_STYLE_CHANGE);
  EXPECT_EQ(record.fill0, 0);
  EXPECT_EQ(record.fill1, 0);
  EXPECT_EQ(record.line, 0);
  EXPECT_EQ(record.x, 0);
  EXPECT_EQ(record.y, 0);
  ASSERT_EQ(radicle_swf_shape_record_at(file.get(), 0, 1, &record), RADICLE_STATUS_OK);
  EXPECT_EQ(record.kind, RADICLE_SWF_RECORD_STRAIGHT_EDGE);
  EXPECT_EQ(record.x, 10);
  EXPECT_EQ(record.y, 20);
  EXPECT_EQ(radicle_swf_shape_record_at(file.get(), 0, 2, &record), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_swf_shape_record_at(file.get(), 1, 0, &record), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_swf_shape_record_at(nullptr, 0, 0, &record), RADICLE_STATUS_INVALID_ARGUMENT);

  radicle_swf_fill_style fill{};
  ASSERT_EQ(radicle_swf_shape_fill_at(file.get(), 0, 0, &fill), RADICLE_STATUS_OK);
  EXPECT_EQ(fill.kind, 0x00);
  EXPECT_EQ(fill.color, 0x78123456u);
  EXPECT_EQ(fill.has_matrix, 0);
  EXPECT_EQ(fill.stop_count, 0u);

  radicle_swf_morph_shape morph{};
  ASSERT_EQ(radicle_swf_morph_at(file.get(), 1, &morph), RADICLE_STATUS_OK);
  EXPECT_EQ(morph.character_id, 45);
  EXPECT_EQ(morph.version, 2);
  EXPECT_EQ(morph.has_edge_bounds, 1);
  EXPECT_EQ(morph.start_bounds.x_max, 400);
  EXPECT_EQ(morph.end_bounds.x_max, 500);
  EXPECT_EQ(morph.start_edge_bounds.x_max, 380);
  EXPECT_EQ(morph.end_edge_bounds.x_max, 480);
  EXPECT_EQ(morph.fill_style_count, 0u);
  EXPECT_EQ(morph.start_edge_count, 1u);
  EXPECT_EQ(morph.end_edge_count, 0u);
  EXPECT_EQ(morph.start_record_count, 2u);
  EXPECT_EQ(morph.end_record_count, 0u);
  ASSERT_EQ(radicle_swf_morph_start_record_at(file.get(), 1, 0, &record), RADICLE_STATUS_OK);
  EXPECT_EQ(record.kind, RADICLE_SWF_RECORD_MOVE);
  EXPECT_EQ(record.x, 3);
  EXPECT_EQ(record.y, -2);
  ASSERT_EQ(radicle_swf_morph_start_record_at(file.get(), 1, 1, &record), RADICLE_STATUS_OK);
  EXPECT_EQ(record.kind, RADICLE_SWF_RECORD_STRAIGHT_EDGE);
  EXPECT_EQ(record.x, 5);
  EXPECT_EQ(record.y, 6);
  EXPECT_EQ(radicle_swf_morph_start_record_at(file.get(), 1, 2, &record), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_swf_morph_end_record_at(file.get(), 1, 0, &record), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_swf_morph_start_record_at(file.get(), 0, 0, &record), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_swf_morph_end_record_at(nullptr, 1, 0, &record), RADICLE_STATUS_INVALID_ARGUMENT);

  radicle_swf_bitmap_info bitmap{};
  ASSERT_EQ(radicle_swf_bitmap_at(file.get(), 2, &bitmap), RADICLE_STATUS_OK);
  EXPECT_EQ(bitmap.character_id, 46);
  EXPECT_EQ(bitmap.format, 5);
  EXPECT_EQ(bitmap.width, 2);
  EXPECT_EQ(bitmap.height, 2);
  EXPECT_EQ(bitmap.has_color_table, 0);
  EXPECT_GT(bitmap.data_size, 0u);

  radicle_swf_jpeg_info jpeg{};
  ASSERT_EQ(radicle_swf_jpeg_at(file.get(), 3, &jpeg), RADICLE_STATUS_OK);
  EXPECT_EQ(jpeg.character_id, 47);
  EXPECT_EQ(jpeg.deblock, 0x0100u);
  EXPECT_EQ(jpeg.image_size, 17u);
  EXPECT_EQ(jpeg.has_alpha, 1);
  EXPECT_GT(jpeg.alpha_size, 0u);

  radicle_swf_fill_style wrong{};
  EXPECT_EQ(radicle_swf_shape_fill_at(file.get(), 1, 0, &wrong), RADICLE_STATUS_NOT_FOUND);
  radicle_swf_jpeg_tables_info tables{};
  EXPECT_EQ(radicle_swf_jpeg_tables_at(file.get(), 0, &tables), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_swf_shape_at(file.get(), 99, &shape), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_swf_shape_fill_at(file.get(), 0, 1, &fill), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_swf_shape_at(nullptr, 0, &shape), RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST(CApiSwfTest, ReadsAShapeLineFillThroughTheGetters) {
  const auto file = open(swf_art_test::movie_with(83, swf_art_test::define_shape_4_payload()));
  ASSERT_NE(file, nullptr);

  radicle_swf_shape shape{};
  ASSERT_EQ(radicle_swf_shape_at(file.get(), 0, &shape), RADICLE_STATUS_OK);
  EXPECT_EQ(shape.line_style_count, 2u);

  radicle_swf_line_style line{};
  radicle_swf_fill_style fill{};
  radicle_swf_gradient_stop stop{};
  ASSERT_EQ(radicle_swf_shape_line_at(file.get(), 0, 0, &line), RADICLE_STATUS_OK);
  EXPECT_EQ(line.has_fill, 0);
  EXPECT_EQ(line.has_miter_limit, 1);
  EXPECT_EQ(line.miter_limit, 0x0300);
  EXPECT_EQ(radicle_swf_shape_line_fill_at(file.get(), 0, 0, &fill), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_swf_shape_line_fill_stop_at(file.get(), 0, 0, 0, &stop),
            RADICLE_STATUS_NOT_FOUND);

  ASSERT_EQ(radicle_swf_shape_line_at(file.get(), 0, 1, &line), RADICLE_STATUS_OK);
  EXPECT_EQ(line.has_fill, 1);
  EXPECT_EQ(line.has_miter_limit, 0);
  EXPECT_EQ(line.color, 0u);

  ASSERT_EQ(radicle_swf_shape_line_fill_at(file.get(), 0, 1, &fill), RADICLE_STATUS_OK);
  EXPECT_EQ(fill.kind, 0x00);
  EXPECT_EQ(fill.color, 0xFF778899u);
  EXPECT_EQ(fill.has_matrix, 0);
  EXPECT_EQ(fill.stop_count, 0u);

  EXPECT_EQ(radicle_swf_shape_line_fill_stop_at(file.get(), 0, 1, 0, &stop),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_swf_shape_line_fill_at(file.get(), 0, 2, &fill), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_swf_shape_line_fill_stop_at(file.get(), 0, 2, 0, &stop),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_swf_shape_line_fill_at(nullptr, 0, 1, &fill), RADICLE_STATUS_INVALID_ARGUMENT);
}
