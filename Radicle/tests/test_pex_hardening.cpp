#include "fo4/pex/pex.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using fo4::pex::PexFile;

namespace {

class Bytes {
public:
  void u8(std::uint8_t value) { data_.push_back(value); }

  void u16(std::uint16_t value) {
    u8(static_cast<std::uint8_t>(value & 0xFFu));
    u8(static_cast<std::uint8_t>(value >> 8));
  }

  void u32(std::uint32_t value) {
    u16(static_cast<std::uint16_t>(value & 0xFFFFu));
    u16(static_cast<std::uint16_t>(value >> 16));
  }

  void i64(std::int64_t value) {
    const auto bits = static_cast<std::uint64_t>(value);
    u32(static_cast<std::uint32_t>(bits & 0xFFFFFFFFu));
    u32(static_cast<std::uint32_t>(bits >> 32));
  }

  void text(const std::string& value) {
    u16(static_cast<std::uint16_t>(value.size()));
    for (const char ch : value) u8(static_cast<std::uint8_t>(ch));
  }

  std::size_t size() const { return data_.size(); }

  std::vector<std::uint8_t> take() && { return std::move(data_); }

private:
  std::vector<std::uint8_t> data_;
};

void patchU8(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint8_t value) {
  bytes[at] = value;
}

void patchU16(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint16_t value) {
  bytes[at] = static_cast<std::uint8_t>(value & 0xFFu);
  bytes[at + 1] = static_cast<std::uint8_t>(value >> 8);
}

void patchU32(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint32_t value) {
  patchU16(bytes, at, static_cast<std::uint16_t>(value & 0xFFFFu));
  patchU16(bytes, at + 2, static_cast<std::uint16_t>(value >> 16));
}

std::uint32_t readU32(const std::vector<std::uint8_t>& bytes, std::size_t at) {
  return static_cast<std::uint32_t>(bytes[at]) |
         static_cast<std::uint32_t>(bytes[at + 1]) << 8 |
         static_cast<std::uint32_t>(bytes[at + 2]) << 16 |
         static_cast<std::uint32_t>(bytes[at + 3]) << 24;
}

struct Fixture {
  std::vector<std::uint8_t> bytes;
  std::size_t majorVersion = 0;
  std::size_t objectNameIndex = 0;
  std::size_t declaredSize = 0;
  std::size_t countTag = 0;
};

/* A minimal but complete Fallout 4 .pex built byte by byte, so a single
   malformed field can be written without going through the writer. It holds
   one object with one variable, and one state with one function carrying a
   variable-argument callmethod. */
Fixture minimalFixture() {
  Fixture fixture;
  Bytes b;

  b.u32(0xFA57C0DEu);
  fixture.majorVersion = b.size();
  b.u8(3);
  b.u8(9);
  b.u16(2);
  b.i64(0);

  b.text("");
  b.text("");
  b.text("");

  b.u16(2);
  b.text("S");
  b.text("Int");

  b.u8(0);
  b.u16(0);
  b.u16(1);

  fixture.objectNameIndex = b.size();
  b.u16(0);
  fixture.declaredSize = b.size();
  b.u32(0);

  const auto bodyStart = b.size();
  b.u16(0);
  b.u16(0);
  b.u8(0);
  b.u32(0);
  b.u16(0);
  b.u16(0);
  b.u16(1);
  b.u16(0);
  b.u16(1);
  b.u32(0);
  b.u8(3);
  b.u32(42);
  b.u8(0);
  b.u16(0);
  b.u16(1);
  b.u16(0);
  b.u16(1);
  b.u16(0);
  b.u16(0);
  b.u16(0);
  b.u32(0);
  b.u8(0);
  b.u16(0);
  b.u16(0);
  b.u16(1);
  b.u8(23);
  for (int argument = 0; argument < 3; ++argument) {
    b.u8(1);
    b.u16(0);
  }
  fixture.countTag = b.size();
  b.u8(3);
  b.u32(1);
  b.u8(1);
  b.u16(1);

  const auto body = b.size() - bodyStart;
  fixture.bytes = std::move(b).take();
  patchU32(fixture.bytes, fixture.declaredSize,
           static_cast<std::uint32_t>(body + sizeof(std::uint32_t)));
  return fixture;
}

void expectParseError(const std::vector<std::uint8_t>& bytes, const std::string& expected) {
  try {
    static_cast<void>(PexFile::fromBytes(bytes));
    FAIL() << "expected a parse error";
  } catch (const std::runtime_error& e) {
    EXPECT_NE(std::string(e.what()).find(expected), std::string::npos) << e.what();
  } catch (...) {
    FAIL() << "expected std::runtime_error";
  }
}

void expectAnyParseError(const std::vector<std::uint8_t>& bytes) {
  try {
    static_cast<void>(PexFile::fromBytes(bytes));
    FAIL() << bytes.size() << "-byte prefix parsed";
  } catch (const std::runtime_error&) {
  } catch (...) {
    FAIL() << bytes.size() << "-byte prefix threw a non-runtime_error exception";
  }
}

}

TEST(PexHardening, TheHandBuiltFixtureIsValid) {
  const Fixture fixture = minimalFixture();
  const PexFile decoded = PexFile::fromBytes(fixture.bytes);
  ASSERT_EQ(decoded.objects.size(), 1u);
  EXPECT_EQ(decoded.objects[0].name, "S");
  EXPECT_EQ(decoded.objects[0].variables.size(), 1u);
  EXPECT_EQ(decoded.toBytes(), fixture.bytes);
}

TEST(PexHardening, EveryProperPrefixOfTheMinimalFileFailsClosed) {
  const Fixture fixture = minimalFixture();
  for (std::size_t length = 0; length < fixture.bytes.size(); ++length) {
    expectAnyParseError(
        std::vector<std::uint8_t>(fixture.bytes.begin(), fixture.bytes.begin() + length));
  }
}

TEST(PexHardening, UnsupportedMajorVersionFailsClosed) {
  Fixture fixture = minimalFixture();
  patchU8(fixture.bytes, fixture.majorVersion, 4);
  expectParseError(fixture.bytes, "Unsupported .pex format major version");
}

TEST(PexHardening, StringIndexOutOfRangeFailsClosed) {
  Fixture fixture = minimalFixture();
  patchU16(fixture.bytes, fixture.objectNameIndex, 0xFFFF);
  expectParseError(fixture.bytes, "String table index");
}

TEST(PexHardening, ObjectSizeLargerThanTheBodyFailsClosed) {
  Fixture fixture = minimalFixture();
  patchU32(fixture.bytes, fixture.declaredSize,
           readU32(fixture.bytes, fixture.declaredSize) + 4u);
  expectParseError(fixture.bytes, "but its body is");
}

TEST(PexHardening, ObjectSizeSmallerThanTheBodyFailsClosed) {
  Fixture fixture = minimalFixture();
  patchU32(fixture.bytes, fixture.declaredSize,
           readU32(fixture.bytes, fixture.declaredSize) - 8u);
  expectParseError(fixture.bytes, "but its body is");
}

TEST(PexHardening, VariableArgumentCountBeyondTheFileFailsClosed) {
  Fixture fixture = minimalFixture();
  patchU32(fixture.bytes, fixture.countTag + 1, 0x00FFFFFFu);
  expectParseError(fixture.bytes, "bytes left in the file");
}

TEST(PexHardening, NonIntegerVariableArgumentCountFailsClosed) {
  Fixture fixture = minimalFixture();
  patchU8(fixture.bytes, fixture.countTag, 5);
  expectParseError(fixture.bytes, "must be a non-negative integer");
}

TEST(PexHardening, NegativeVariableArgumentCountFailsClosed) {
  Fixture fixture = minimalFixture();
  patchU32(fixture.bytes, fixture.countTag + 1, 0xFFFFFFFFu);
  expectParseError(fixture.bytes, "must be a non-negative integer");
}
