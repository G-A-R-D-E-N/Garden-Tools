#include "abc_test_support.hpp"
#include "fo4/c_api.h"
#include "fo4/c_api_swf.h"
#include "swf_movie_builder.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
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
