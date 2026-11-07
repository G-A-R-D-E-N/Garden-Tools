#include "fo4/schema/vmad.hpp"
#include "vmad_writer_fixture.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace fo4;
using fo4::test::ByteWriter;
namespace fixture = fo4::test::vmad;

struct Frag {
  std::uint32_t index{0};
  std::uint8_t unknown{0x2A};
  std::string script{"PF_Frag"};
  std::string function;
};

void fragment_script(ByteWriter& out, std::string_view name) {
  fixture::name(out, name);
  out.u8(0);
  out.u16(0);
}

void write_fragment(ByteWriter& out, const Frag& frag) {
  out.u8(frag.unknown);
  fixture::name(out, frag.script);
  fixture::name(out, frag.function);
}

std::vector<std::byte> pack_extras(std::uint8_t flags, const std::vector<Frag>& entries) {
  ByteWriter out;
  out.u8(3);
  out.u8(flags);
  fragment_script(out, "PF_Pack");
  for (const auto& entry : entries) write_fragment(out, entry);
  return out.bytes;
}

std::vector<std::byte> indexed_extras(const std::vector<Frag>& entries) {
  ByteWriter out;
  out.u8(3);
  fragment_script(out, "PF_Indexed");
  out.u16(static_cast<std::uint16_t>(entries.size()));
  for (const auto& entry : entries) {
    out.u32(entry.index);
    write_fragment(out, entry);
  }
  return out.bytes;
}

std::span<const std::byte> view(const std::vector<std::byte>& bytes) {
  return std::span<const std::byte>(bytes);
}

}  // namespace

TEST(VmadFragmentReaders, PackDecodesEveryFlaggedEntryInFlagOrder) {
  const auto bytes = pack_extras(0x05, {{0, 0x10, "PF_Pack", "Fragment_Begin"},
                                        {0, 0x20, "PF_Pack", "Fragment_Change"}});
  const auto extras = read_pack_vmad_extras(view(bytes), 2);
  ASSERT_FALSE(extras.unsupported) << extras.unsupported_reason;
  EXPECT_TRUE(extras.fully_consumed);
  EXPECT_EQ(extras.version, 3);
  EXPECT_EQ(extras.flags, 0x05);
  EXPECT_EQ(extras.script_name, "PF_Pack");
  ASSERT_EQ(extras.fragments.size(), 2u);
  EXPECT_EQ(extras.fragments[0].flag, 0x01);
  EXPECT_EQ(extras.fragments[0].unknown, 0x10);
  EXPECT_EQ(extras.fragments[0].script, "PF_Pack");
  EXPECT_EQ(extras.fragments[0].function, "Fragment_Begin");
  EXPECT_EQ(extras.fragments[1].flag, 0x04);
  EXPECT_EQ(extras.fragments[1].unknown, 0x20);
  EXPECT_EQ(extras.fragments[1].function, "Fragment_Change");
  EXPECT_EQ(extras.fragments[1].end, bytes.size());
  EXPECT_EQ(extras.fragments[1].offset, extras.fragments[0].end);
}

TEST(VmadFragmentReaders, PackWithNoFlagsDecodesToNoFragmentsAndIsFullyConsumed) {
  const auto bytes = pack_extras(0x00, {});
  const auto extras = read_pack_vmad_extras(view(bytes), 2);
  EXPECT_FALSE(extras.unsupported);
  EXPECT_TRUE(extras.fully_consumed);
  EXPECT_TRUE(extras.fragments.empty());
}

TEST(VmadFragmentReaders, PackTruncatedAtEveryLengthReportsUnsupportedWithoutThrowing) {
  const auto bytes = pack_extras(0x03, {{0, 1, "PF_Pack", "Fragment_Begin"},
                                        {0, 2, "PF_Pack", "Fragment_End"}});
  for (std::size_t length = 0; length < bytes.size(); ++length) {
    SCOPED_TRACE(length);
    const std::vector<std::byte> prefix(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(length));
    const auto extras = read_pack_vmad_extras(view(prefix), 2);
    EXPECT_TRUE(extras.unsupported);
    EXPECT_FALSE(extras.unsupported_reason.empty());
    EXPECT_FALSE(extras.fully_consumed);
  }
}

TEST(VmadFragmentReaders, PackTrailingGarbageDecodesButIsNotFullyConsumed) {
  auto bytes = pack_extras(0x01, {{0, 7, "PF_Pack", "Fragment_Begin"}});
  const auto clean_size = bytes.size();
  bytes.push_back(std::byte{0xAA});
  bytes.push_back(std::byte{0xBB});
  bytes.push_back(std::byte{0xCC});
  const auto extras = read_pack_vmad_extras(view(bytes), 2);
  EXPECT_FALSE(extras.unsupported);
  EXPECT_FALSE(extras.fully_consumed);
  ASSERT_EQ(extras.fragments.size(), 1u);
  EXPECT_EQ(extras.fragments[0].end, clean_size);
}

TEST(VmadFragmentReaders, PackWithVariableFragmentPropertyIsUnsupported) {
  ByteWriter out;
  out.u8(3);
  out.u8(0x01);
  fixture::name(out, "PF_Pack");
  out.u8(0);
  out.u16(1);
  fixture::name(out, "Var");
  out.u8(6);
  out.u8(0);
  write_fragment(out, {0, 1, "PF_Pack", "Fragment_Begin"});
  const auto extras = read_pack_vmad_extras(view(out.bytes), 2);
  EXPECT_TRUE(extras.unsupported);
  EXPECT_FALSE(extras.unsupported_reason.empty());
  EXPECT_FALSE(extras.fully_consumed);
}

TEST(VmadFragmentReaders, PackEmptySpanReportsUnsupported) {
  const auto extras = read_pack_vmad_extras(std::span<const std::byte>{}, 2);
  EXPECT_TRUE(extras.unsupported);
  EXPECT_FALSE(extras.fully_consumed);
}

TEST(VmadFragmentReaders, PerkDecodesIndexKeyedFragments) {
  const auto bytes = indexed_extras({{100, 0x2A, "PF_Perk", "Fragment_100"},
                                     {0x80000001u, 0x01, "PF_Perk", "Fragment_Large"}});
  const auto extras = read_perk_vmad_extras(view(bytes), 2);
  ASSERT_FALSE(extras.unsupported) << extras.unsupported_reason;
  EXPECT_TRUE(extras.fully_consumed);
  EXPECT_EQ(extras.script_name, "PF_Indexed");
  ASSERT_EQ(extras.fragments.size(), 2u);
  EXPECT_EQ(extras.fragments[0].index, 100u);
  EXPECT_EQ(extras.fragments[0].unknown, 0x2A);
  EXPECT_EQ(extras.fragments[0].script, "PF_Perk");
  EXPECT_EQ(extras.fragments[0].function, "Fragment_100");
  EXPECT_EQ(extras.fragments[1].index, 0x80000001u);
  EXPECT_EQ(extras.fragments[1].function, "Fragment_Large");
  EXPECT_EQ(extras.fragments[1].end, bytes.size());
}

TEST(VmadFragmentReaders, PerkTruncatedAtEveryLengthReportsUnsupportedWithoutThrowing) {
  const auto bytes = indexed_extras({{1, 0, "PF_Perk", "Fragment_1"}, {2, 0, "PF_Perk", "Fragment_2"}});
  for (std::size_t length = 0; length < bytes.size(); ++length) {
    SCOPED_TRACE(length);
    const std::vector<std::byte> prefix(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(length));
    const auto extras = read_perk_vmad_extras(view(prefix), 2);
    EXPECT_TRUE(extras.unsupported);
    EXPECT_FALSE(extras.fully_consumed);
  }
}

TEST(VmadFragmentReaders, PerkTrailingGarbageDecodesButIsNotFullyConsumed) {
  auto bytes = indexed_extras({{5, 0, "PF_Perk", "Fragment_5"}});
  const auto clean_size = bytes.size();
  bytes.push_back(std::byte{0x01});
  const auto extras = read_perk_vmad_extras(view(bytes), 2);
  EXPECT_FALSE(extras.unsupported);
  EXPECT_FALSE(extras.fully_consumed);
  ASSERT_EQ(extras.fragments.size(), 1u);
  EXPECT_EQ(extras.fragments[0].end, clean_size);
}

TEST(VmadFragmentReaders, PerkWithCountLargerThanPayloadIsUnsupported) {
  ByteWriter out;
  out.u8(3);
  fragment_script(out, "PF_Indexed");
  out.u16(3);
  out.u32(1);
  write_fragment(out, {0, 0, "PF_Perk", "Fragment_1"});
  const auto extras = read_perk_vmad_extras(view(out.bytes), 2);
  EXPECT_TRUE(extras.unsupported);
  EXPECT_FALSE(extras.fully_consumed);
}

TEST(VmadFragmentReaders, TermDecodesIndexKeyedFragments) {
  const auto bytes = indexed_extras({{0, 0x00, "PF_Term", "Fragment_Zero"},
                                     {7, 0x03, "PF_Term", "Fragment_Seven"}});
  const auto extras = read_term_vmad_extras(view(bytes), 2);
  ASSERT_FALSE(extras.unsupported) << extras.unsupported_reason;
  EXPECT_TRUE(extras.fully_consumed);
  ASSERT_EQ(extras.fragments.size(), 2u);
  EXPECT_EQ(extras.fragments[0].index, 0u);
  EXPECT_EQ(extras.fragments[1].index, 7u);
  EXPECT_EQ(extras.fragments[1].unknown, 0x03);
  EXPECT_EQ(extras.fragments[1].script, "PF_Term");
  EXPECT_EQ(extras.fragments[1].function, "Fragment_Seven");
  EXPECT_EQ(extras.fragments[1].end, bytes.size());
}

TEST(VmadFragmentReaders, TermTruncatedAtEveryLengthReportsUnsupportedWithoutThrowing) {
  const auto bytes = indexed_extras({{1, 0, "PF_Term", "Fragment_1"}});
  for (std::size_t length = 0; length < bytes.size(); ++length) {
    SCOPED_TRACE(length);
    const std::vector<std::byte> prefix(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(length));
    const auto extras = read_term_vmad_extras(view(prefix), 2);
    EXPECT_TRUE(extras.unsupported);
    EXPECT_FALSE(extras.fully_consumed);
  }
}

TEST(VmadFragmentReaders, TermTrailingGarbageDecodesButIsNotFullyConsumed) {
  auto bytes = indexed_extras({{3, 0, "PF_Term", "Fragment_3"}});
  const auto clean_size = bytes.size();
  bytes.push_back(std::byte{0xFF});
  bytes.push_back(std::byte{0xFE});
  const auto extras = read_term_vmad_extras(view(bytes), 2);
  EXPECT_FALSE(extras.unsupported);
  EXPECT_FALSE(extras.fully_consumed);
  ASSERT_EQ(extras.fragments.size(), 1u);
  EXPECT_EQ(extras.fragments[0].end, clean_size);
}

TEST(VmadFragmentReaders, TermUnknownObjectFormatInScriptEntryIsUnsupported) {
  ByteWriter out;
  out.u8(3);
  fixture::name(out, "PF_Term");
  out.u8(0);
  out.u16(1);
  fixture::name(out, "Target");
  out.u8(1);
  out.u8(0);
  out.u32(0x801);
  const auto extras = read_term_vmad_extras(view(out.bytes), 9);
  EXPECT_TRUE(extras.unsupported);
  EXPECT_FALSE(extras.fully_consumed);
}
