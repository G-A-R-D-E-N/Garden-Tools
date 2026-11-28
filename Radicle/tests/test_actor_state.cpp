#include <fo4/actor_state/actor_state.hpp>
#include <fo4/error.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <random>
#include <span>
#include <stdexcept>
#include <vector>

namespace {

using namespace fo4::actor_state;

ActorStateRecord populated() {
  ActorStateRecord record;
  record.ref.form = {"Fallout4.esm", 0x00ABCDu};
  record.flags = kFlagEnabled | kFlagDead;
  record.cell = {"Fallout4.esm", 0x00012345u};
  record.transform = {{1.5f, -2.25f, 3.0f}, {0.0f, 90.0f, 0.5f}, 1.25f};
  record.inventory = {{{"Fallout4.esm", 0x0000000Fu}, 3}, {{"DLC01.esm", 0x00000010u}, -1}};
  record.actor_values = {{"Health", 25.0f}, {"Armor", -4.5f}};
  record.factions = {{{"Fallout4.esm", 0x0001C1F1u}, 2}};
  record.forced_package = {"Fallout4.esm", 0x0002B1E1u};
  record.combat_target = RefIdentity{RefKind::CreatedReference, {}, 77u, {"Fallout4.esm", 0x0000BEEFu},
                                     0x1122334455667788ull};
  record.bounty = 500;
  record.spawn_level = 42;
  return record;
}

void put_le32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
  for (std::size_t i = 0; i < 4; ++i) bytes[offset + i] = static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFu);
}

}  // namespace

TEST(ActorStateCodecTest, PopulatedRecordRoundTripsAndReencodesByteIdentically) {
  const ActorStateRecord record = populated();
  const std::vector<std::uint8_t> bytes = encode(record);
  const ActorStateRecord decoded = decode(bytes);
  EXPECT_EQ(decoded, record);
  EXPECT_EQ(encode(decoded), bytes);
}

TEST(ActorStateCodecTest, EmptyAndZeroRecordRoundTrips) {
  ActorStateRecord record;
  record.flags = 0;
  record.transform.scale = 0.0f;
  record.spawn_level = 0;
  const std::vector<std::uint8_t> bytes = encode(record);
  EXPECT_EQ(decode(bytes), record);
  EXPECT_EQ(encode(decode(bytes)), bytes);
}

TEST(ActorStateCodecTest, UnknownTrailingFieldsSurviveDecodeAndReencode) {
  std::vector<std::uint8_t> bytes = encode(populated());
  const std::vector<std::uint8_t> appended = {0xDE, 0xAD, 0xBE};
  bytes.insert(bytes.end(), appended.begin(), appended.end());
  put_le32(bytes, 8, static_cast<std::uint32_t>(bytes.size() - kHeaderBytes));

  const ActorStateRecord decoded = decode(bytes);
  EXPECT_EQ(decoded.unknown_tail, appended);
  EXPECT_EQ(encode(decoded), bytes);
}

TEST(ActorStateCodecTest, RefusesBadHeadersWithAParseError) {
  const std::vector<std::uint8_t> good = encode(populated());

  std::vector<std::uint8_t> bad_magic = good;
  bad_magic[0] = 'X';
  EXPECT_THROW(decode(bad_magic), fo4::parse_error);

  std::vector<std::uint8_t> bad_version = good;
  put_le32(bad_version, 4, kFormatVersion + 1);
  EXPECT_THROW(decode(bad_version), fo4::parse_error);

  std::vector<std::uint8_t> size_mismatch = good;
  put_le32(size_mismatch, 8, static_cast<std::uint32_t>(good.size()));
  EXPECT_THROW(decode(size_mismatch), fo4::parse_error);

  const std::vector<std::uint8_t> short_header(good.begin(), good.begin() + 7);
  EXPECT_THROW(decode(short_header), fo4::parse_error);
  EXPECT_THROW(decode(std::span<const std::uint8_t>()), fo4::parse_error);
}

TEST(ActorStateCodecTest, EveryTruncatedPayloadIsRefusedWithinItsBounds) {
  const std::vector<std::uint8_t> good = encode(populated());
  for (std::size_t length = kHeaderBytes; length < good.size(); ++length) {
    std::vector<std::uint8_t> cut(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(length));
    put_le32(cut, 8, static_cast<std::uint32_t>(length - kHeaderBytes));
    EXPECT_THROW(decode(cut), fo4::parse_error) << "payload cut at " << (length - kHeaderBytes);
  }
}

TEST(ActorStateCodecTest, CorruptedBytesNeverEscapeAsAnyOtherException) {
  const std::vector<std::uint8_t> good = encode(populated());
  std::mt19937 rng(1);
  std::uniform_int_distribution<std::size_t> pick_offset(0, good.size() - 1);
  std::uniform_int_distribution<int> pick_byte(0, 255);
  std::uniform_int_distribution<int> pick_count(1, 3);
  for (int iteration = 0; iteration < 2000; ++iteration) {
    std::vector<std::uint8_t> mutated = good;
    const int flips = pick_count(rng);
    for (int flip = 0; flip < flips; ++flip) {
      mutated[pick_offset(rng)] = static_cast<std::uint8_t>(pick_byte(rng));
    }
    try {
      const ActorStateRecord decoded = decode(mutated);
      EXPECT_NO_THROW(encode(decoded)) << "iteration " << iteration;
    } catch (const fo4::parse_error&) {
    }
  }
}

TEST(ActorStateCodecTest, CreatedRefsWithDifferentSeedsKeepDistinctIdentities) {
  const FormKey base{"Fallout4.esm", 0x0000BEEFu};
  ActorStateRecord first;
  first.ref = RefIdentity{RefKind::CreatedReference, {}, 1001u, base, 7u};
  ActorStateRecord second;
  second.ref = RefIdentity{RefKind::CreatedReference, {}, 1002u, base, 8u};

  const ActorStateRecord first_back = decode(encode(first));
  const ActorStateRecord second_back = decode(encode(second));
  EXPECT_EQ(first_back.ref, first.ref);
  EXPECT_EQ(second_back.ref, second.ref);
  EXPECT_NE(first_back.ref, second_back.ref);
  EXPECT_EQ(first_back.ref.base, second_back.ref.base);
}

TEST(ActorStateCodecTest, EncodeRefusesValuesDecodeWouldNotAccept) {
  ActorStateRecord long_name;
  long_name.cell.plugin = std::string(kMaxStringBytes + 1, 'p');
  EXPECT_THROW(encode(long_name), std::invalid_argument);

  ActorStateRecord too_many;
  too_many.inventory.resize(kMaxListEntries + 1);
  EXPECT_THROW(encode(too_many), std::invalid_argument);

  ActorStateRecord nameless_with_id;
  nameless_with_id.forced_package.local_id = 5;
  EXPECT_THROW(encode(nameless_with_id), std::invalid_argument);
}

namespace {

ActorStateRecord golden_record() {
  ActorStateRecord record;
  record.ref.form = {"ab", 0x11u};
  record.flags = 5;
  record.transform.position = {1.0f, 0.0f, 0.0f};
  record.bounty = -2;
  record.spawn_level = 3;
  return record;
}

std::vector<std::uint8_t> golden_bytes() {
  return {
      0x52, 0x41, 0x53, 0x56, 0x01, 0x00, 0x00, 0x00, 0x6A, 0x00, 0x00, 0x00,
      0x00, 0x02, 0x00, 0x00, 0x00, 0x61, 0x62, 0x11, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3F, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3F, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0xFE, 0xFF, 0xFF, 0xFF, 0x03, 0x00,
  };
}

constexpr std::size_t kGoldenRefKindOffset = 12;
constexpr std::size_t kGoldenFirstStringLengthOffset = 13;
constexpr std::size_t kGoldenInventoryCountOffset = 83;
constexpr std::size_t kGoldenCombatPresenceOffset = 111;

}  // namespace

TEST(ActorStateCodecTest, VersionOneLayoutIsPinnedByteForByte) {
  EXPECT_EQ(encode(golden_record()), golden_bytes());
  EXPECT_EQ(decode(golden_bytes()), golden_record());
}

TEST(ActorStateCodecTest, OversizedPrefixesAndUnknownEnumValuesAreEachRefused) {
  const auto refused_with = [](std::size_t offset, std::uint32_t value, bool whole_word) {
    std::vector<std::uint8_t> bytes = golden_bytes();
    if (whole_word) {
      put_le32(bytes, offset, value);
    } else {
      bytes[offset] = static_cast<std::uint8_t>(value);
    }
    EXPECT_THROW(decode(bytes), fo4::parse_error) << "offset " << offset << " value " << value;
  };
  refused_with(kGoldenFirstStringLengthOffset, static_cast<std::uint32_t>(kMaxStringBytes + 1), true);
  refused_with(kGoldenFirstStringLengthOffset, 0xFFFFFFFFu, true);
  refused_with(kGoldenInventoryCountOffset, static_cast<std::uint32_t>(kMaxListEntries + 1), true);
  refused_with(kGoldenInventoryCountOffset, 0xFFFFFFFFu, true);
  refused_with(kGoldenCombatPresenceOffset, 2, false);
  refused_with(kGoldenRefKindOffset, 2, false);
}

TEST(ActorStateCodecTest, UnknownFlagBitsAreKeptAsWritten) {
  ActorStateRecord record = golden_record();
  record.flags = 0xFFFFFFF8u | kFlagDead;
  const std::vector<std::uint8_t> bytes = encode(record);
  EXPECT_EQ(decode(bytes).flags, record.flags);
  EXPECT_EQ(encode(decode(bytes)), bytes);
}

TEST(ActorStateCodecTest, EncodeRefusesAReferenceKindDecodeWouldNotAccept) {
  ActorStateRecord record = golden_record();
  record.ref.kind = static_cast<RefKind>(2);
  EXPECT_THROW(encode(record), std::invalid_argument);

  ActorStateRecord target = golden_record();
  target.combat_target = RefIdentity{};
  target.combat_target->kind = static_cast<RefKind>(7);
  EXPECT_THROW(encode(target), std::invalid_argument);
}
