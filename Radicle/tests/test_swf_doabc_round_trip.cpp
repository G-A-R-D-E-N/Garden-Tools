#include "fo4/abc/abc.hpp"
#include "fo4/swf/swf.hpp"
#include "abc_test_support.hpp"
#include "swf_movie_builder.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

using swf_test::SwfBuilder;
using swf_test::wrap_body;

namespace {

std::vector<std::byte> to_bytes(const std::vector<std::uint8_t>& values) {
  std::vector<std::byte> out;
  out.reserve(values.size());
  for (const std::uint8_t value : values) {
    out.push_back(static_cast<std::byte>(value));
  }
  return out;
}

std::vector<std::byte> movie_with_tag(std::uint16_t code, std::span<const std::byte> payload) {
  SwfBuilder builder;
  builder.rect(0, 11000, 0, 8000, 15);
  builder.u16(0x0C00);
  builder.u16(1);
  builder.tag(code, payload, false);
  builder.tag(0);
  return wrap_body(builder.body, 10, false, false);
}

std::size_t abc_tag_index(const fo4::swf::SwfFile& file) {
  for (std::size_t index = 0; index < file.tags.size(); ++index) {
    if (file.tags[index].abc.has_value()) {
      return index;
    }
  }
  return file.tags.size();
}

std::vector<std::byte> rewrite_abc_tag(fo4::swf::SwfFile& file, std::size_t index,
                                       const std::vector<std::uint8_t>& serialized) {
  const fo4::swf::SwfTag& tag = file.tags[index];
  const auto header_end = static_cast<std::ptrdiff_t>(tag.abc->payload_offset);
  std::vector<std::byte> payload(tag.payload.begin(), tag.payload.begin() + header_end);
  const auto abc_bytes = to_bytes(serialized);
  payload.insert(payload.end(), abc_bytes.begin(), abc_bytes.end());
  EXPECT_TRUE(fo4::swf::replace_tag_payload(file, index, payload));
  return fo4::swf::write_swf(file);
}

}  // namespace

TEST(SwfDoAbcRoundTrip, DoAbc2KeepsItsFlagsAndNameAndRewritesByteExact) {
  const auto abc = fo4::test::build_abc();
  SwfBuilder payload;
  payload.u32(3);
  payload.text("frame 1");
  payload.bytes(to_bytes(abc));
  const auto movie = movie_with_tag(82, payload.body);

  auto result = fo4::swf::read_swf(movie);
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const std::size_t index = abc_tag_index(result.file);
  ASSERT_LT(index, result.file.tags.size());
  ASSERT_EQ(result.file.tags[index].code, fo4::swf::tag::kDoAbc2);
  const auto& abc_info = *result.file.tags[index].abc;
  EXPECT_EQ(abc_info.flags, 3u);
  EXPECT_EQ(abc_info.name, "frame 1");
  EXPECT_EQ(abc_info.payload_offset, 12u);
  EXPECT_EQ(abc_info.payload_size, abc.size());

  fo4::abc::AbcFile parsed;
  ASSERT_EQ(fo4::abc::AbcFile::parse(abc, parsed), fo4::abc::AbcStatus::Ok);
  std::vector<std::uint8_t> serialized;
  ASSERT_EQ(parsed.serialize(serialized), fo4::abc::AbcStatus::Ok);
  EXPECT_EQ(serialized, abc);
  EXPECT_EQ(rewrite_abc_tag(result.file, index, serialized), movie);
}

TEST(SwfDoAbcRoundTrip, DoAbcCarriesRawAbcWithoutAFlagsOrNameHeader) {
  const auto abc = fo4::test::build_abc();
  const auto movie = movie_with_tag(72, to_bytes(abc));

  auto result = fo4::swf::read_swf(movie);
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const std::size_t index = abc_tag_index(result.file);
  ASSERT_LT(index, result.file.tags.size());
  ASSERT_EQ(result.file.tags[index].code, fo4::swf::tag::kDoAbc);
  const auto& abc_info = *result.file.tags[index].abc;
  EXPECT_EQ(abc_info.flags, 0u);
  EXPECT_TRUE(abc_info.name.empty());
  EXPECT_EQ(abc_info.payload_offset, 0u);
  EXPECT_EQ(abc_info.payload_size, abc.size());

  fo4::abc::AbcFile parsed;
  ASSERT_EQ(fo4::abc::AbcFile::parse(abc, parsed), fo4::abc::AbcStatus::Ok);
  std::vector<std::uint8_t> serialized;
  ASSERT_EQ(parsed.serialize(serialized), fo4::abc::AbcStatus::Ok);
  EXPECT_EQ(rewrite_abc_tag(result.file, index, serialized), movie);
}
