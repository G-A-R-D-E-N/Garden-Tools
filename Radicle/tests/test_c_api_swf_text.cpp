#include "fo4/avm1/avm1.hpp"
#include "fo4/c_api.h"
#include "fo4/c_api_swf.h"
#include "fo4/swf/swf.hpp"
#include "fo4/swf/swf_timeline.hpp"
#include "swf_movie_builder.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
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

std::string text_of(const uint8_t* bytes, size_t size) {
  return bytes == nullptr ? std::string() : std::string(reinterpret_cast<const char*>(bytes), size);
}

std::vector<std::byte> action_movie() {
  SwfBuilder actions;
  actions.u8(0x06);
  actions.u8(0x07);
  actions.u8(0x26);
  actions.u8(0x00);

  SwfBuilder init;
  init.u16(7);
  init.u8(0x04);
  init.u8(0x00);

  SwfBuilder child_actions;
  child_actions.u8(0x05);
  child_actions.u8(0x00);
  SwfBuilder nested;
  nested.tag(12, child_actions.body, false);
  nested.tag(1);
  nested.tag(0);
  SwfBuilder sprite;
  sprite.u16(7);
  sprite.u16(1);
  sprite.bytes(nested.body);

  SwfBuilder failing;
  failing.u8(0x83);
  failing.u8(0x10);
  failing.u8(0x00);
  failing.u8(0x61);

  SwfBuilder file;
  file.rect(0, 11000, 0, 8000, 15);
  file.u16(0x0C00);
  file.u16(1);
  file.tag(12, actions.body, false);
  file.tag(59, init.body, false);
  file.tag(1);
  file.tag(39, sprite.body, false);
  file.tag(12, failing.body, false);
  file.tag(0);
  return wrap_body(file.body, 10, false, false);
}

}  // namespace

TEST(CApiSwfTextTest, DisassemblesDoActionAndDoInitActionByFlatIndex) {
  const auto file = open(action_movie());
  ASSERT_NE(file, nullptr);

  radicle_swf_tag tag{};
  ASSERT_EQ(radicle_swf_tag_at(file.get(), 4, &tag), RADICLE_STATUS_OK);
  EXPECT_EQ(tag.code, 12);
  EXPECT_EQ(tag.parent, size_t{3});

  const auto listing = [&](size_t index) {
    uint8_t* text = nullptr;
    size_t size = 0;
    EXPECT_EQ(radicle_swf_avm1_disassemble(file.get(), index, &text, &size), RADICLE_STATUS_OK)
        << radicle_last_error();
    const auto out = text_of(text, size);
    radicle_free_bytes(text);
    return out;
  };
  EXPECT_EQ(listing(0), "0000  Play\n0001  Stop\n0002  Trace\n0003  End\n");
  EXPECT_EQ(listing(1), "0000  NextFrame\n0001  End\n");
  EXPECT_EQ(listing(4), "0000  PreviousFrame\n0001  End\n");
}

TEST(CApiSwfTextTest, RefusesNonActionTagsAndUndecodableStreamsWithOutputsCleared) {
  const auto file = open(action_movie());
  ASSERT_NE(file, nullptr);

  uint8_t sentinel = 0;
  uint8_t* text = &sentinel;
  size_t size = 77;
  EXPECT_EQ(radicle_swf_avm1_disassemble(file.get(), 2, &text, &size), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(text, nullptr);
  EXPECT_EQ(size, 0u);

  text = &sentinel;
  size = 77;
  EXPECT_EQ(radicle_swf_avm1_disassemble(file.get(), 99, &text, &size), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(text, nullptr);
  EXPECT_EQ(size, 0u);

  text = &sentinel;
  size = 77;
  EXPECT_EQ(radicle_swf_avm1_disassemble(nullptr, 0, &text, &size), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(text, nullptr);
  EXPECT_EQ(size, 0u);

  text = &sentinel;
  EXPECT_EQ(radicle_swf_avm1_disassemble(file.get(), 0, &text, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(text, nullptr);

  const std::vector<std::byte> failing{std::byte{0x83}, std::byte{0x10}, std::byte{0x00}, std::byte{0x61}};
  const auto stream = fo4::avm1::decode_actions(failing);
  ASSERT_FALSE(stream.ok());
  text = &sentinel;
  size = 77;
  EXPECT_EQ(radicle_swf_avm1_disassemble(file.get(), 7, &text, &size), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(text, nullptr);
  EXPECT_EQ(size, 0u);
  EXPECT_EQ(std::string(radicle_last_error()), stream.message);
}

TEST(CApiSwfTextTest, InspectTextMatchesTheCppInspectionAndRefusesANullName) {
  const auto movie = action_movie();
  const auto file = open(movie);
  ASSERT_NE(file, nullptr);

  const auto read = fo4::swf::read_swf(movie);
  ASSERT_TRUE(read.ok());
  const std::string expected = fo4::swf::inspect_swf(read.file, "movie.swf");

  uint8_t* text = nullptr;
  size_t size = 0;
  ASSERT_EQ(radicle_swf_inspect_text(file.get(), "movie.swf", &text, &size), RADICLE_STATUS_OK)
      << radicle_last_error();
  const auto actual = text_of(text, size);
  radicle_free_bytes(text);
  EXPECT_EQ(actual, expected);
  EXPECT_EQ(actual.rfind("## movie.swf\nStage ", 0), 0u) << actual;

  uint8_t sentinel = 0;
  text = &sentinel;
  size = 77;
  EXPECT_EQ(radicle_swf_inspect_text(file.get(), nullptr, &text, &size), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(text, nullptr);
  EXPECT_EQ(size, 0u);
}
