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
using fo4::avm1::Avm1Status;
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

Bytes encode_checked(const Avm1Stream& stream, Avm1Status& status) {
  Bytes out;
  for (const std::byte value : fo4::avm1::encode_actions_checked(stream, status)) {
    out.push_back(std::to_integer<std::uint8_t>(value));
  }
  return out;
}

Avm1Action single(std::uint8_t code) {
  Avm1Action action;
  action.code = code;
  return action;
}

Avm1Action raw_record(std::size_t size) {
  Avm1Action action;
  action.code = 0x83;
  action.payload = fo4::avm1::Avm1Raw{std::vector<std::byte>(size)};
  return action;
}

const Bytes kJumpOverPlayAndStop{0x99, 0x02, 0x00, 0x02, 0x00, 0x04, 0x06, 0x07, 0x00};

const Bytes kFunctionOfTry{0x9B, 0x05, 0x00, 0x00, 0x00, 0x00, 0x0E, 0x00, 0x8F, 0x09, 0x00, 0x01,
                           0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x65, 0x00, 0x04, 0x06, 0x00};

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
  EXPECT_EQ(fo4::avm1::validate_actions(stream), Avm1Status::BranchUnresolved);
}

TEST(Avm1Encode, DeletedJumpTargetWithNoSurvivorRetargetsToTheStreamEnd) {
  const Bytes input{0x99, 0x02, 0x00, 0x02, 0x00, 0x06, 0x04, 0x00};
  auto stream = decode(input);
  ASSERT_TRUE(stream.ok()) << stream.message;
  stream.actions.erase(stream.actions.begin() + 1);
  stream.actions.erase(stream.actions.begin() + 2);
  EXPECT_EQ(encode(stream), (Bytes{0x99, 0x02, 0x00, 0x01, 0x00, 0x04}));
  EXPECT_EQ(fo4::avm1::validate_actions(stream), Avm1Status::BranchUnresolved);
}

TEST(Avm1Encode, BranchToAnErasedEndRecordStillTargetsTheStreamEnd) {
  const Bytes input{0x99, 0x02, 0x00, 0x02, 0x00, 0x04, 0x00};
  auto stream = decode(input);
  ASSERT_TRUE(stream.ok()) << stream.message;
  ASSERT_EQ(stream.original_end, 7u);
  ASSERT_TRUE(fo4::avm1::erase_action(stream.actions, 2));
  EXPECT_EQ(fo4::avm1::validate_actions(stream), Avm1Status::Ok);
  EXPECT_EQ(encode(stream), (Bytes{0x99, 0x02, 0x00, 0x01, 0x00, 0x04}));
}

TEST(Avm1Encode, BranchOffsetFieldIsIgnoredAndTheTargetDecidesTheDelta) {
  auto stream = decode(kJumpOverPlayAndStop);
  ASSERT_TRUE(stream.ok()) << stream.message;
  std::get<Avm1Branch>(stream.actions[0].payload).offset = 100;
  EXPECT_EQ(encode(stream), kJumpOverPlayAndStop);
  EXPECT_EQ(fo4::avm1::validate_actions(stream), Avm1Status::Ok);
}

TEST(Avm1Encode, EncodeCheckedReturnsTheBytesOfAValidStream) {
  const auto stream = decode(kJumpOverPlayAndStop);
  ASSERT_TRUE(stream.ok()) << stream.message;
  Avm1Status status = Avm1Status::TooDeep;
  EXPECT_EQ(encode_checked(stream, status), kJumpOverPlayAndStop);
  EXPECT_EQ(status, Avm1Status::Ok);
}

TEST(Avm1Encode, EncodeCheckedReturnsNoBytesForABranchDeltaOutOfRange) {
  const Bytes input{0x99, 0x02, 0x00, 0x02, 0x00, 0x04, 0x00};
  auto stream = decode(input);
  ASSERT_TRUE(stream.ok()) << stream.message;
  ASSERT_TRUE(fo4::avm1::insert_action(stream.actions, 1, raw_record(40000)));
  Avm1Status status = Avm1Status::Ok;
  EXPECT_TRUE(encode_checked(stream, status).empty());
  EXPECT_EQ(status, Avm1Status::BranchDeltaOutOfRange);
  EXPECT_EQ(encode(stream).size(), 40010u);
}

TEST(Avm1Encode, EncodeCheckedReturnsNoBytesForARecordOrBodyOver65535) {
  Avm1Status status = Avm1Status::Ok;
  Avm1Stream record_stream;
  record_stream.actions = {raw_record(70000)};
  EXPECT_TRUE(encode_checked(record_stream, status).empty());
  EXPECT_EQ(status, Avm1Status::SizeTooLarge);

  Avm1Action function;
  function.code = 0x9B;
  function.payload = fo4::avm1::Avm1Function{};
  function.body_records[0] = 2;
  Avm1Stream body_stream;
  body_stream.actions = {function, raw_record(40000), raw_record(40000)};
  status = Avm1Status::Ok;
  EXPECT_TRUE(encode_checked(body_stream, status).empty());
  EXPECT_EQ(status, Avm1Status::SizeTooLarge);
}

// Invariant: a stream whose decode failed must report that status, not an empty Ok encode.
TEST(Avm1Encode, EncodeCheckedRefusesAStreamWhoseDecodeFailed) {
  const Bytes input{0x04, 0x07};
  auto stream = decode(input);
  ASSERT_EQ(stream.status, Avm1Status::MissingEnd);
  Avm1Status status = Avm1Status::Ok;
  EXPECT_TRUE(encode_checked(stream, status).empty());
  EXPECT_EQ(status, Avm1Status::MissingEnd);
}

TEST(Avm1Encode, InsertedRecordIntoAnEmptyFunctionBodyGrowsItsBodySize) {
  const Bytes input{0x9B, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  auto stream = decode(input);
  ASSERT_TRUE(stream.ok()) << stream.message;
  ASSERT_EQ(stream.actions[0].body_records[0], 0u);
  ASSERT_TRUE(fo4::avm1::insert_action_into_body(stream.actions, 0, 0, 1, single(0x07)));
  EXPECT_EQ(encode(stream), (Bytes{0x9B, 0x05, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x07, 0x00}));
}

TEST(Avm1Encode, InsertedRecordIntoAFinallyOnlyTryFillsTheFinallyBody) {
  const Bytes input{0x8F, 0x08, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  auto stream = decode(input);
  ASSERT_TRUE(stream.ok()) << stream.message;
  ASSERT_EQ(stream.actions[0].body_records[1], 0u);
  ASSERT_TRUE(fo4::avm1::insert_action_into_body(stream.actions, 0, 1, 1, single(0x07)));
  EXPECT_EQ(encode(stream), (Bytes{0x8F, 0x08, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
                                   0x00, 0x07, 0x00}));
}

// Invariant: insert_action_into_body must accept the index one past a non-empty body's last record.
TEST(Avm1Encode, InsertedRecordAppendsToTheEndOfANonEmptyTryBody) {
  auto stream = decode(kFunctionOfTry);
  ASSERT_TRUE(stream.ok()) << stream.message;
  ASSERT_EQ(stream.actions[1].body_records[0], 1u);
  ASSERT_TRUE(fo4::avm1::insert_action_into_body(stream.actions, 1, 0, 3, single(0x07)));
  EXPECT_EQ(encode(stream), (Bytes{0x9B, 0x05, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x8F, 0x09,
                                   0x00, 0x01, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x65, 0x00,
                                   0x04, 0x07, 0x06, 0x00}));
}

// Invariant: insert_action_into_body must refuse an index outside the chosen slot's body.
TEST(Avm1Encode, InsertIntoABodyRefusesAnIndexOutsideItsSlot) {
  auto stream = decode(kFunctionOfTry);
  ASSERT_TRUE(stream.ok()) << stream.message;
  EXPECT_FALSE(fo4::avm1::insert_action_into_body(stream.actions, 1, 1, 2, single(0x07)));
  EXPECT_EQ(stream.actions.size(), 5u);
  EXPECT_EQ(encode(stream), kFunctionOfTry);
}

// Invariant: insert_action_into_body must refuse a slot at or past the container's body_count.
TEST(Avm1Encode, InsertIntoABodyRefusesASlotPastTheBodyCount) {
  auto stream = decode(kFunctionOfTry);
  ASSERT_TRUE(stream.ok()) << stream.message;
  EXPECT_FALSE(fo4::avm1::insert_action_into_body(stream.actions, 1, 2, 4, single(0x07)));
  EXPECT_EQ(stream.actions.size(), 5u);
  EXPECT_EQ(encode(stream), kFunctionOfTry);
}

// Invariant: insert_action_into_body must refuse a container_index whose record has no body.
TEST(Avm1Encode, InsertIntoABodyRefusesARecordThatIsNotAContainer) {
  auto stream = decode(kFunctionOfTry);
  ASSERT_TRUE(stream.ok()) << stream.message;
  EXPECT_FALSE(fo4::avm1::insert_action_into_body(stream.actions, 2, 0, 3, single(0x07)));
  EXPECT_EQ(stream.actions.size(), 5u);
  EXPECT_EQ(encode(stream), kFunctionOfTry);
}

TEST(Avm1Encode, AppendAtTheListEndStaysOutsideABodyThatEndsThere) {
  Avm1Action function;
  function.code = 0x9B;
  function.payload = fo4::avm1::Avm1Function{};
  function.body_records[0] = 2;
  Avm1Stream stream;
  stream.actions = {function, single(0x04), single(0x06)};
  ASSERT_TRUE(fo4::avm1::insert_action(stream.actions, 3, single(0x07)));
  EXPECT_EQ(stream.actions[0].body_records[0], 2u);
  EXPECT_EQ(encode(stream), (Bytes{0x9B, 0x05, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x04, 0x06,
                                   0x07}));
}

TEST(Avm1Encode, ErasedRecordInsideNestedBodiesShrinksEveryEnclosingBody) {
  auto stream = decode(kFunctionOfTry);
  ASSERT_TRUE(stream.ok()) << stream.message;
  ASSERT_TRUE(fo4::avm1::erase_action(stream.actions, 2));
  EXPECT_EQ(encode(stream), (Bytes{0x9B, 0x05, 0x00, 0x00, 0x00, 0x00, 0x0D, 0x00, 0x8F, 0x09,
                                   0x00, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x65, 0x00,
                                   0x06, 0x00}));
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
    EXPECT_EQ(fo4::avm1::validate_actions(stream), Avm1Status::Ok);
  }
}

TEST(Avm1Encode, InsertedRecordInsideANestedTryGrowsEveryEnclosingBody) {
  auto stream = decode(kFunctionOfTry);
  ASSERT_TRUE(stream.ok()) << stream.message;
  ASSERT_TRUE(fo4::avm1::insert_action(stream.actions, 2, single(0x07)));
  EXPECT_EQ(encode(stream),
            (Bytes{0x9B, 0x05, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x8F, 0x09, 0x00, 0x01,
                   0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x65, 0x00, 0x07, 0x04, 0x06, 0x00}));
}

TEST(Avm1Encode, ErasedContainerRecordTakesItsBodyRecordsWithIt) {
  auto stream = decode(kFunctionOfTry);
  ASSERT_TRUE(stream.ok()) << stream.message;
  ASSERT_TRUE(fo4::avm1::erase_action(stream.actions, 1));
  EXPECT_EQ(encode(stream), (Bytes{0x9B, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}));
}

TEST(Avm1Encode, InsertedRecordInsideACatchByRegisterTryGrowsOnlyTheCatchBody) {
  const Bytes input{0x8F, 0x08, 0x00, 0x05, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02,
                    0x04, 0x06, 0x00};
  auto stream = decode(input);
  ASSERT_TRUE(stream.ok()) << stream.message;
  ASSERT_TRUE(fo4::avm1::insert_action(stream.actions, 2, single(0x07)));
  EXPECT_EQ(encode(stream),
            (Bytes{0x8F, 0x08, 0x00, 0x05, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x04,
                   0x07, 0x06, 0x00}));
}

TEST(Avm1Encode, InsertedRecordInsideAWithBodyGrowsTheWithBodySize) {
  const Bytes input{0x94, 0x02, 0x00, 0x01, 0x00, 0x04, 0x00};
  auto stream = decode(input);
  ASSERT_TRUE(stream.ok()) << stream.message;
  ASSERT_TRUE(fo4::avm1::insert_action(stream.actions, 1, single(0x07)));
  EXPECT_EQ(encode(stream), (Bytes{0x94, 0x02, 0x00, 0x02, 0x00, 0x07, 0x04, 0x00}));
}

TEST(Avm1Encode, JumpIntoAFunctionBodyFollowsTheBodyAfterAnInsert) {
  const Bytes input{0x99, 0x02, 0x00, 0x09, 0x00, 0x9B, 0x05, 0x00, 0x00, 0x00,
                    0x00, 0x02, 0x00, 0x04, 0x06, 0x00};
  auto stream = decode(input);
  ASSERT_TRUE(stream.ok()) << stream.message;
  ASSERT_EQ(std::get<Avm1Branch>(stream.actions[0].payload).target, 14u);
  ASSERT_TRUE(fo4::avm1::insert_action(stream.actions, 3, single(0x07)));
  EXPECT_EQ(encode(stream), (Bytes{0x99, 0x02, 0x00, 0x0A, 0x00, 0x9B, 0x05, 0x00, 0x00, 0x00,
                                   0x00, 0x03, 0x00, 0x04, 0x07, 0x06, 0x00}));
}

TEST(Avm1Encode, BranchToTheOriginalStreamEndFollowsTheEndAfterAnInsert) {
  const Bytes input{0x99, 0x02, 0x00, 0x02, 0x00, 0x04, 0x00};
  auto stream = decode(input);
  ASSERT_TRUE(stream.ok()) << stream.message;
  ASSERT_EQ(std::get<Avm1Branch>(stream.actions[0].payload).target, 7u);
  ASSERT_TRUE(fo4::avm1::insert_action(stream.actions, 1, single(0x07)));
  EXPECT_EQ(encode(stream), (Bytes{0x99, 0x02, 0x00, 0x03, 0x00, 0x07, 0x04, 0x00}));
}

TEST(Avm1Encode, NewRecordBeforeABranchToOffsetZeroLeavesTheBranchOnTheOffsetZeroRecord) {
  const Bytes input{0x99, 0x02, 0x00, 0xFB, 0xFF, 0x04, 0x00};
  auto stream = decode(input);
  ASSERT_TRUE(stream.ok()) << stream.message;
  ASSERT_EQ(std::get<Avm1Branch>(stream.actions[0].payload).target, 0u);
  ASSERT_TRUE(fo4::avm1::insert_action(stream.actions, 0, single(0x07)));
  EXPECT_EQ(encode(stream), (Bytes{0x07, 0x99, 0x02, 0x00, 0xFB, 0xFF, 0x04, 0x00}));
}

TEST(Avm1Encode, EraseAndInsertRefuseAnIndexPastTheList) {
  auto stream = decode(kJumpOverPlayAndStop);
  ASSERT_TRUE(stream.ok()) << stream.message;
  const std::size_t size = stream.actions.size();
  EXPECT_FALSE(fo4::avm1::insert_action(stream.actions, size + 1, single(0x07)));
  EXPECT_FALSE(fo4::avm1::erase_action(stream.actions, size));
  EXPECT_EQ(stream.actions.size(), size);
}

TEST(Avm1Encode, ValidateRejectsABodyLargerThan65535Bytes) {
  Avm1Action function;
  function.code = 0x9B;
  function.payload = fo4::avm1::Avm1Function{};
  function.body_records[0] = 2;
  Avm1Stream stream;
  stream.actions = {function, raw_record(40000), raw_record(40000)};
  EXPECT_EQ(fo4::avm1::validate_actions(stream), Avm1Status::SizeTooLarge);
}

TEST(Avm1Encode, ValidateRejectsABranchDeltaThatDoesNotFitInt16) {
  const Bytes input{0x99, 0x02, 0x00, 0x02, 0x00, 0x04, 0x00};
  auto stream = decode(input);
  ASSERT_TRUE(stream.ok()) << stream.message;
  ASSERT_TRUE(fo4::avm1::insert_action(stream.actions, 1, raw_record(40000)));
  EXPECT_EQ(fo4::avm1::validate_actions(stream), Avm1Status::BranchDeltaOutOfRange);
}

TEST(Avm1Encode, ValidateRejectsABodyRecordCountPastTheList) {
  const Bytes input{0x9B, 0x05, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x04, 0x06, 0x00};
  auto stream = decode(input);
  ASSERT_TRUE(stream.ok()) << stream.message;
  stream.actions[0].body_records[0] = 4;
  EXPECT_EQ(fo4::avm1::validate_actions(stream), Avm1Status::BodyRecordsPastEnd);
}

TEST(Avm1Encode, ValidateRejectsANestedBodyThatRunsPastItsParentRange) {
  auto stream = decode(kFunctionOfTry);
  ASSERT_TRUE(stream.ok()) << stream.message;
  stream.actions[1].body_records[0] = 2;
  EXPECT_EQ(fo4::avm1::validate_actions(stream), Avm1Status::BodyRecordsPastEnd);
}

// Invariant: validate_actions must return the decode status of a failed decode, not Ok.
TEST(Avm1Encode, ValidateReturnsTheDecodeStatusForAFailedDecode) {
  const Bytes input{0x81, 0x01, 0x00, 0x00};
  const auto stream = decode(input);
  ASSERT_EQ(stream.status, Avm1Status::Truncated);
  EXPECT_EQ(fo4::avm1::validate_actions(stream), Avm1Status::Truncated);
}

// Invariant: validate_actions must keep returning Ok for a valid decoded stream.
TEST(Avm1Encode, ValidateReturnsOkForAValidDecodedStream) {
  const auto stream = decode(kJumpOverPlayAndStop);
  ASSERT_TRUE(stream.ok()) << stream.message;
  EXPECT_EQ(fo4::avm1::validate_actions(stream), Avm1Status::Ok);
}

// Invariant: validate_actions must report BranchUnresolved after an erased branch target.
TEST(Avm1Encode, ValidateReportsBranchUnresolvedAfterTheTargetIsErased) {
  auto stream = decode(kJumpOverPlayAndStop);
  ASSERT_TRUE(stream.ok()) << stream.message;
  ASSERT_EQ(std::get<Avm1Branch>(stream.actions[0].payload).target, 7u);
  ASSERT_TRUE(fo4::avm1::erase_action(stream.actions, 3));
  EXPECT_EQ(fo4::avm1::validate_actions(stream), Avm1Status::BranchUnresolved);
}

}  // namespace
