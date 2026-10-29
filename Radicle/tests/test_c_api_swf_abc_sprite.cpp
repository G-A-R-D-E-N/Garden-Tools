#include "abc_test_support.hpp"
#include "fo4/c_api.h"
#include "fo4/c_api_swf.h"
#include "fo4/swf/swf.hpp"
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

struct SwfDeleter {
  void operator()(radicle_swf* swf) const { radicle_swf_destroy(swf); }
};

using SwfHandle = std::unique_ptr<radicle_swf, SwfDeleter>;

SwfHandle open(const std::vector<std::byte>& bytes) {
  radicle_swf* raw = nullptr;
  EXPECT_EQ(radicle_swf_open_bytes(reinterpret_cast<const std::uint8_t*>(bytes.data()),
                                   bytes.size(), &raw),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  return SwfHandle(raw);
}

std::vector<std::byte> sprite_before_abc_movie(const std::vector<std::byte>& abc_payload) {
  SwfBuilder sprite;
  sprite.u16(7);
  sprite.u16(1);
  sprite.tag(fo4::swf::tag::kShowFrame);
  sprite.tag(fo4::swf::tag::kEnd);

  SwfBuilder body;
  body.rect(0, 200, 0, 100, 9);
  body.u16(24u * 256u);
  body.u16(1);
  body.tag(fo4::swf::tag::kDefineSprite, sprite.body, false);
  body.tag(fo4::swf::tag::kDoAbc2, abc_payload, true);
  body.tag(fo4::swf::tag::kEnd);
  return swf_test::wrap_body(body.body, 10, false, false);
}

}  // namespace

TEST(CApiSwfAbcSprite, ApplyTextKeepsTheDoAbc2PayloadWhenASpriteComesFirst) {
  const std::vector<std::uint8_t> abc = fo4::test::build_abc();
  SwfBuilder payload;
  payload.u32(0);
  payload.text("sprite");
  payload.bytes(std::span<const std::byte>(reinterpret_cast<const std::byte*>(abc.data()),
                                           abc.size()));

  SwfHandle swf = open(sprite_before_abc_movie(payload.body));
  ASSERT_NE(swf, nullptr);
  ASSERT_EQ(radicle_swf_abc_count(swf.get()), 1u);

  radicle_swf_abc_info info{};
  ASSERT_EQ(radicle_swf_abc_info_at(swf.get(), 0, &info), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_GT(info.tag_index, 1u);
  radicle_swf_tag found{};
  ASSERT_EQ(radicle_swf_tag_at(swf.get(), info.tag_index, &found), RADICLE_STATUS_OK);
  EXPECT_EQ(found.code, fo4::swf::tag::kDoAbc2);
  EXPECT_EQ(found.parent, RADICLE_SWF_NO_INDEX);

  std::uint8_t* rendered_raw = nullptr;
  std::size_t rendered_size = 0;
  ASSERT_EQ(radicle_swf_abc_text(swf.get(), 0, &rendered_raw, &rendered_size), RADICLE_STATUS_OK)
      << radicle_last_error();
  const std::string rendered(reinterpret_cast<const char*>(rendered_raw), rendered_size);
  radicle_free_bytes(rendered_raw);
  ASSERT_EQ(radicle_swf_abc_apply_text(swf.get(), 0,
                                       reinterpret_cast<const std::uint8_t*>(rendered.data()),
                                       rendered.size()),
            RADICLE_STATUS_OK)
      << radicle_last_error();

  std::uint8_t* payload_raw = nullptr;
  std::size_t payload_size = 0;
  ASSERT_EQ(radicle_swf_tag_payload(swf.get(), info.tag_index, &payload_raw, &payload_size),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_EQ(payload_size, payload.body.size());
  EXPECT_EQ(std::memcmp(payload_raw, payload.body.data(), payload.body.size()), 0);
  radicle_free_bytes(payload_raw);
}
