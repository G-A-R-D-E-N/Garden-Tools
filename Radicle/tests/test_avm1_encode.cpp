#include <fo4/avm1/avm1.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <variant>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;
using fo4::avm1::Avm1Action;
using fo4::avm1::Avm1Branch;
using fo4::avm1::Avm1Stream;

Avm1Stream decode(const Bytes& input) {
  std::vector<std::byte> bytes;
  for (const std::uint8_t value : input) {
    bytes.push_back(static_cast<std::byte>(value));
  }
  return fo4::avm1::decode_actions(bytes);
}

Bytes encode(const Avm1Stream& stream) {
  Bytes out;
  for (const std::byte value : fo4::avm1::encode_actions(stream)) {
    out.push_back(std::to_integer<std::uint8_t>(value));
  }
  return out;
}

Avm1Action single(std::uint8_t code) {
  Avm1Action action;
  action.code = code;
  return action;
}

const Bytes kJumpOverPlayAndStop{0x99, 0x02, 0x00, 0x02, 0x00, 0x04, 0x06, 0x07, 0x00};

TEST(Avm1Encode, InsertedRecordBeforeAJumpTargetGrowsTheJumpOffset) {
  auto stream = decode(kJumpOverPlayAndStop);
  ASSERT_TRUE(stream.ok()) << stream.message;
  ASSERT_EQ(std::get<Avm1Branch>(stream.actions[0].payload).target, 7u);
  stream.actions.insert(stream.actions.begin() + 3, single(0x04));
  EXPECT_EQ(encode(stream), (Bytes{0x99, 0x02, 0x00, 0x03, 0x00, 0x04, 0x06, 0x04, 0x07, 0x00}));
}

TEST(Avm1Encode, DeletedRecordInsideAJumpSpanShrinksTheJumpOffset) {
  auto stream = decode(kJumpOverPlayAndStop);
  ASSERT_TRUE(stream.ok()) << stream.message;
  stream.actions.erase(stream.actions.begin() + 2);
  EXPECT_EQ(encode(stream), (Bytes{0x99, 0x02, 0x00, 0x01, 0x00, 0x04, 0x07, 0x00}));
}

TEST(Avm1Encode, DeletedJumpTargetRetargetsToTheNextSurvivingRecord) {
  auto stream = decode(kJumpOverPlayAndStop);
  ASSERT_TRUE(stream.ok()) << stream.message;
  stream.actions.erase(stream.actions.begin() + 2, stream.actions.begin() + 4);
  EXPECT_EQ(encode(stream), (Bytes{0x99, 0x02, 0x00, 0x01, 0x00, 0x04, 0x00}));
}

TEST(Avm1Encode, DeletedJumpTargetWithNoSurvivorRetargetsToTheStreamEnd) {
  const Bytes input{0x99, 0x02, 0x00, 0x02, 0x00, 0x06, 0x04, 0x00};
  auto stream = decode(input);
  ASSERT_TRUE(stream.ok()) << stream.message;
  stream.actions.erase(stream.actions.begin() + 1);
  stream.actions.erase(stream.actions.begin() + 2);
  EXPECT_EQ(encode(stream), (Bytes{0x99, 0x02, 0x00, 0x01, 0x00, 0x04}));
}

TEST(Avm1Encode, InsertedRecordInsideADefineFunctionBodyGrowsItsBodySize) {
  const Bytes input{0x9B, 0x05, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x04, 0x06, 0x00};
  auto stream = decode(input);
  ASSERT_TRUE(stream.ok()) << stream.message;
  ASSERT_EQ(stream.actions[0].body_records[0], 2u);
  stream.actions.insert(stream.actions.begin() + 3, single(0x07));
  stream.actions[0].body_records[0] = 3;
  EXPECT_EQ(encode(stream),
            (Bytes{0x9B, 0x05, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x04, 0x06, 0x07, 0x00}));
}

TEST(Avm1Encode, InsertedRecordInsideACatchBodyGrowsOnlyTheCatchSize) {
  const Bytes input{0x8F, 0x09, 0x00, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
                    0x65, 0x00, 0x04, 0x06, 0x00};
  auto stream = decode(input);
  ASSERT_TRUE(stream.ok()) << stream.message;
  ASSERT_EQ(stream.actions[0].body_records[0], 1u);
  ASSERT_EQ(stream.actions[0].body_records[1], 1u);
  stream.actions.insert(stream.actions.begin() + 3, single(0x07));
  stream.actions[0].body_records[1] = 2;
  EXPECT_EQ(encode(stream), (Bytes{0x8F, 0x09, 0x00, 0x01, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00,
                                   0x65, 0x00, 0x04, 0x06, 0x07, 0x00}));
}

TEST(Avm1Encode, UneditedStreamsReencodeToTheirInputBytes) {
  const Bytes function_stream{0x9B, 0x05, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x04, 0x06, 0x00};
  const Bytes try_stream{0x8F, 0x09, 0x00, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
                         0x65, 0x00, 0x04, 0x06, 0x00};
  for (const Bytes& input : {kJumpOverPlayAndStop, function_stream, try_stream}) {
    const auto stream = decode(input);
    ASSERT_TRUE(stream.ok()) << stream.message;
    EXPECT_EQ(encode(stream), input);
  }
}

}  // namespace
