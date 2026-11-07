#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/field_address.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/preflight.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using fo4::Signature;
using fo4::schema::DiscriminatorStorage;
using fo4::schema::FieldAddress;
using fo4::schema::ScalarStorage;

struct Sub {
  std::string signature;
  std::vector<std::byte> data;
};

std::vector<std::byte> raw(std::initializer_list<int> values) {
  std::vector<std::byte> out;
  for (const int value : values) out.push_back(std::byte{static_cast<std::uint8_t>(value)});
  return out;
}

std::vector<std::byte> u32s(std::initializer_list<std::uint32_t> values) {
  fo4::test::ByteWriter out;
  for (const auto value : values) out.u32(value);
  return out.bytes;
}

std::vector<std::byte> text(std::string_view value, bool terminate = true) {
  std::vector<std::byte> out;
  for (const char ch : value) out.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  if (terminate) out.push_back(std::byte{0});
  return out;
}

std::vector<std::byte> make_plugin(const std::vector<Sub>& subs,
                                   std::string_view record = "TEST",
                                   std::uint32_t tes4_flags = 0) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", tes4_flags, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x801);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view("Master.esm\0", 11));
  fo4::test::write_subrecord(out, "DATA", u32s({0, 0}));
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, record, 0);
  const auto header = fo4::test::begin_record(out, record, 0, 0x01000800);
  for (const auto& sub : subs) fo4::test::write_subrecord(out, sub.signature, sub.data);
  fo4::test::end_record(out, header);
  fo4::test::end_group(out, group);
  return out.bytes;
}

fo4::Plugin load(const std::vector<Sub>& subs,
                 std::string_view record = "TEST",
                 std::uint32_t tes4_flags = 0) {
  return fo4::read_plugin(make_plugin(subs, record, tes4_flags), "Patch.esp");
}

fo4::FormKey record_key() { return fo4::FormKey::parse("000800:Patch.esp"); }

std::vector<Sub> snapshot(const fo4::Plugin& plugin) {
  const auto* record = fo4::find_record(plugin, record_key());
  std::vector<Sub> out;
  if (record == nullptr) return out;
  for (const auto& sub : record->subrecords) out.push_back({sub.signature.to_string(), sub.data});
  return out;
}

std::vector<std::byte> payload(const std::vector<Sub>& subs, std::size_t index) {
  return subs.at(index).data;
}

FieldAddress repeated() {
  FieldAddress address;
  address.repeated = true;
  return address;
}

FieldAddress strided(std::size_t stride, std::size_t array_offset = 0) {
  FieldAddress address;
  address.stride = stride;
  address.array_offset = array_offset;
  return address;
}

FieldAddress with_when(FieldAddress address,
                       std::string_view subrecord,
                       std::size_t offset,
                       DiscriminatorStorage storage,
                       std::initializer_list<std::int64_t> values) {
  address.when.subrecord = Signature::from_string(subrecord);
  address.when.offset = offset;
  address.when.storage = storage;
  address.when.count = 0;
  for (const auto value : values) address.when.equals[address.when.count++] = value;
  return address;
}

fo4::schema::ScalarFieldDescriptor scalar_field(std::string_view sub,
                                                ScalarStorage storage,
                                                double minimum,
                                                double maximum,
                                                std::size_t offset,
                                                bool embedded,
                                                FieldAddress address = {},
                                                std::size_t payload_size = 0,
                                                std::string_view record = "TEST") {
  return {Signature::from_string(record), "TEST\\Field",
          fo4::schema::ScalarDescriptor{Signature::from_string(sub), "Field", storage,
                                        fo4::schema::ScalarPresentation::identity, minimum, maximum,
                                        offset, embedded, payload_size, address},
          false};
}

fo4::schema::ScalarFieldDescriptor string_field(std::string_view sub,
                                                FieldAddress address = {},
                                                std::string_view record = "TEST") {
  return scalar_field(sub, ScalarStorage::zstring, 0, 0, 0, false, address, 0, record);
}

fo4::schema::FormLinkFieldDescriptor link_field(std::string_view sub,
                                                std::size_t offset,
                                                bool nullable,
                                                FieldAddress address = {}) {
  return {Signature::from_string("TEST"), "TEST\\Link", "Link", Signature::from_string(sub),
          offset, nullable, 0, 0, address};
}

const fo4::FormKey master_a = fo4::FormKey::parse("000801:Master.esm");
const fo4::FormKey master_b = fo4::FormKey::parse("000900:Master.esm");

}

TEST(FieldWriterStorage, SignedByteBounds) {
  auto plugin = load({{"DATA", raw({0, 0, 0, 0})}});
  const auto field = scalar_field("DATA", ScalarStorage::i8, -128, 127, 1, true);

  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), field, "-128"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(payload(snapshot(plugin), 0), raw({0, 0x80, 0, 0}));
  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), field, "127"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(payload(snapshot(plugin), 0), raw({0, 0x7F, 0, 0}));
  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), field, "127"), fo4::ScalarMutationStatus::unchanged);
  EXPECT_THROW(fo4::set_record_scalar(plugin, record_key(), field, "128"), fo4::schema::ScalarError);
  EXPECT_THROW(fo4::set_record_scalar(plugin, record_key(), field, "-129"), fo4::schema::ScalarError);
  EXPECT_EQ(payload(snapshot(plugin), 0), raw({0, 0x7F, 0, 0}));

  const auto narrow = scalar_field("DATA", ScalarStorage::i8, -10, 10, 1, true);
  EXPECT_THROW(fo4::set_record_scalar(plugin, record_key(), narrow, "11"), fo4::schema::ScalarError);
}

TEST(FieldWriterStorage, SignedWordBounds) {
  auto plugin = load({{"DATA", raw({9, 9, 0, 0, 9, 9})}});
  const auto field = scalar_field("DATA", ScalarStorage::i16, -32768, 32767, 2, true);

  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), field, "-2"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(payload(snapshot(plugin), 0), raw({9, 9, 0xFE, 0xFF, 9, 9}));
  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), field, "-32768"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(payload(snapshot(plugin), 0), raw({9, 9, 0x00, 0x80, 9, 9}));
  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), field, "32767"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(payload(snapshot(plugin), 0), raw({9, 9, 0xFF, 0x7F, 9, 9}));
  EXPECT_THROW(fo4::set_record_scalar(plugin, record_key(), field, "32768"), fo4::schema::ScalarError);
  EXPECT_THROW(fo4::set_record_scalar(plugin, record_key(), field, "-32769"), fo4::schema::ScalarError);

  const auto decoded = fo4::schema::decode_scalar(raw({0xFE, 0xFF}), field.scalar);
  EXPECT_EQ(decoded, "-2");
}

TEST(FieldWriterPayloadSize, MismatchIsAbsentWithoutThrowing) {
  auto plugin = load({{"DATA", raw({1, 2, 3, 4, 5, 6})}});
  const auto field = scalar_field("DATA", ScalarStorage::u16, 0, 65535, 0, true, {}, 8);
  const auto before = snapshot(plugin);

  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), field, "9"), fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(snapshot(plugin)[0].data, before[0].data);
  EXPECT_EQ(fo4::record_field_count(plugin, record_key(), field), std::optional<std::size_t>{1});
  EXPECT_TRUE(fo4::schema::scalar_field_bytes(before[0].data, field.scalar).empty());

  auto exact = load({{"DATA", raw({1, 2, 3, 4, 5, 6, 7, 8})}});
  EXPECT_EQ(fo4::set_record_scalar(exact, record_key(), field, "9"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(payload(snapshot(exact), 0), raw({9, 0, 3, 4, 5, 6, 7, 8}));
}

TEST(FieldWriterRepeated, EditsOneOccurrenceAndLeavesTheOthersIdentical) {
  auto plugin = load({{"EDID", text("Probe")},
                      {"ZZZZ", u32s({1})},
                      {"KEEP", raw({7, 7})},
                      {"ZZZZ", u32s({2})},
                      {"ZZZZ", u32s({3})}});
  const auto field = scalar_field("ZZZZ", ScalarStorage::u32, 0, 1000, 0, false, repeated());
  const auto before = snapshot(plugin);

  EXPECT_EQ(fo4::record_field_count(plugin, record_key(), field), std::optional<std::size_t>{3});
  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), field, 1, "20"), fo4::ScalarMutationStatus::updated);

  const auto after = snapshot(plugin);
  ASSERT_EQ(after.size(), before.size());
  for (std::size_t i = 0; i < before.size(); ++i) {
    if (i == 3) continue;
    EXPECT_EQ(after[i].data, before[i].data) << "subrecord " << i;
    EXPECT_EQ(after[i].signature, before[i].signature);
  }
  EXPECT_EQ(after[3].data, u32s({20}));

  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), field, 3, "1"), fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), field, "11"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(payload(snapshot(plugin), 1), u32s({11}));
  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), field, 2, "3"), fo4::ScalarMutationStatus::unchanged);
}

TEST(FieldWriterRepeated, IndexIsThePhysicalOccurrenceEvenWhenAnEarlierOneIsInvalid) {
  auto plugin = load({{"ZZZZ", u32s({1})}, {"ZZZZ", raw({2, 0})}, {"ZZZZ", u32s({3})}});
  const auto field = scalar_field("ZZZZ", ScalarStorage::u32, 0, 1000, 0, true, repeated());
  const auto before = snapshot(plugin);

  EXPECT_EQ(fo4::record_field_count(plugin, record_key(), field), std::optional<std::size_t>{3});
  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), field, 1, "9"), fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(snapshot(plugin)[1].data, before[1].data);
  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), field, 2, "9"), fo4::ScalarMutationStatus::updated);
  const auto after = snapshot(plugin);
  EXPECT_EQ(after[0].data, before[0].data);
  EXPECT_EQ(after[1].data, before[1].data);
  EXPECT_EQ(after[2].data, u32s({9}));
}

TEST(FieldWriterRepeated, NonRepeatedFieldStillRejectsDuplicates) {
  auto plugin = load({{"ZZZZ", u32s({1})}, {"ZZZZ", u32s({2})}});
  const auto field = scalar_field("ZZZZ", ScalarStorage::u32, 0, 1000, 0, false);
  EXPECT_THROW(fo4::set_record_scalar(plugin, record_key(), field, "5"), fo4::schema::ScalarError);
  EXPECT_THROW(fo4::record_field_count(plugin, record_key(), field), fo4::schema::ScalarError);
  auto occurrences = field;
  occurrences.scalar.address.repeated = true;
  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), occurrences, 1, "5"),
            fo4::ScalarMutationStatus::updated);
}

TEST(FieldWriterStride, EditsOneElementOfAPackedArray) {
  fo4::test::ByteWriter body;
  body.u32(0xAABBCCDD);
  for (std::uint32_t e = 0; e < 3; ++e) {
    body.u32(0x100 + e);
    body.u32(10 + e);
  }
  auto plugin = load({{"ARRY", body.bytes}, {"KEEP", raw({1})}});
  const auto field = scalar_field("ARRY", ScalarStorage::u32, 0, 1000, 4, true, strided(8, 4));
  const auto before = snapshot(plugin);

  EXPECT_EQ(fo4::record_field_count(plugin, record_key(), field), std::optional<std::size_t>{3});
  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), field, 2, "99"), fo4::ScalarMutationStatus::updated);

  auto expected = before[0].data;
  expected[4 + 2 * 8 + 4] = std::byte{99};
  EXPECT_EQ(payload(snapshot(plugin), 0), expected);
  EXPECT_EQ(payload(snapshot(plugin), 1), before[1].data);

  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), field, 3, "1"), fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), field, 0, "10"), fo4::ScalarMutationStatus::unchanged);
}

TEST(FieldWriterStride, MisalignedOrShortPayloadsAreAbsent) {
  const auto field = scalar_field("ARRY", ScalarStorage::u32, 0, 1000, 4, true, strided(8, 4));
  for (const std::size_t size : {std::size_t{2}, std::size_t{4}, std::size_t{4 + 24 + 1}, std::size_t{4 + 7}}) {
    std::vector<std::byte> body(size, std::byte{0x11});
    auto plugin = load({{"ARRY", body}});
    EXPECT_EQ(fo4::record_field_count(plugin, record_key(), field), std::optional<std::size_t>{0}) << size;
    EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), field, 0, "5"),
              fo4::ScalarMutationStatus::subrecord_missing) << size;
    EXPECT_EQ(payload(snapshot(plugin), 0), body) << size;
  }

  const auto plain = scalar_field("ARRY", ScalarStorage::u32, 0, 1000, 4, true, strided(8));
  auto plugin = load({{"ARRY", std::vector<std::byte>(16, std::byte{0})}});
  EXPECT_EQ(fo4::record_field_count(plugin, record_key(), plain), std::optional<std::size_t>{2});
}

TEST(FieldWriterWhen, SameElementDiscriminatorSelectsElements) {
  auto plugin = load({{"ARRY", raw({1, 0, 5, 0, 2, 0, 6, 0, 3, 0, 7, 0, 1, 0, 8, 0})}});
  const auto field = scalar_field("ARRY", ScalarStorage::u16, 0, 1000, 2, true,
                                  with_when(strided(4), "", 0, DiscriminatorStorage::u8, {1, 3}));

  EXPECT_EQ(fo4::record_field_count(plugin, record_key(), field), std::optional<std::size_t>{4});
  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), field, 1, "70"), fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), field, 2, "70"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(payload(snapshot(plugin), 0),
            raw({1, 0, 5, 0, 2, 0, 6, 0, 3, 0, 70, 0, 1, 0, 8, 0}));
  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), field, 4, "1"), fo4::ScalarMutationStatus::subrecord_missing);
}

TEST(FieldWriterWhen, OtherSubrecordDiscriminator) {
  const auto field = scalar_field("DATA", ScalarStorage::u32, 0, 1000, 4, true,
                                  with_when({}, "CNAM", 0, DiscriminatorStorage::u8, {2}));
  const auto data = u32s({0xAAAAAAAA, 1});

  {
    auto plugin = load({{"CNAM", raw({2})}, {"DATA", data}});
    EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), field, "9"), fo4::ScalarMutationStatus::updated);
    EXPECT_EQ(payload(snapshot(plugin), 1), u32s({0xAAAAAAAA, 9}));
  }
  const Sub body{"DATA", data};
  const std::vector<std::vector<Sub>> absent{
      std::vector<Sub>{Sub{"CNAM", raw({1})}, body},
      std::vector<Sub>{body},
      std::vector<Sub>{Sub{"CNAM", raw({2})}, Sub{"CNAM", raw({2})}, body},
      std::vector<Sub>{Sub{"CNAM", {}}, body},
  };
  for (const auto& subs : absent) {
    auto plugin = load(subs);
    const auto before = snapshot(plugin);
    EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), field, "9"), fo4::ScalarMutationStatus::subrecord_missing);
    EXPECT_EQ(fo4::record_field_count(plugin, record_key(), field), std::optional<std::size_t>{1});
    const auto after = snapshot(plugin);
    ASSERT_EQ(after.size(), before.size());
    for (std::size_t i = 0; i < before.size(); ++i) {
      EXPECT_EQ(after[i].data, before[i].data);
    }
  }
}

TEST(FieldWriterWhen, DiscriminatorWidthsAndSign) {
  const auto signed_field = scalar_field("DATA", ScalarStorage::u8, 0, 255, 0, true,
                                         with_when({}, "CNAM", 0, DiscriminatorStorage::i32, {-1}));
  auto negative = load({{"CNAM", u32s({0xFFFFFFFF})}, {"DATA", raw({0})}});
  EXPECT_EQ(fo4::set_record_scalar(negative, record_key(), signed_field, "4"), fo4::ScalarMutationStatus::updated);

  const auto wide_field = scalar_field("DATA", ScalarStorage::u8, 0, 255, 0, true,
                                       with_when({}, "CNAM", 2, DiscriminatorStorage::u16, {0x0102}));
  auto wide = load({{"CNAM", raw({0, 0, 2, 1})}, {"DATA", raw({0})}});
  EXPECT_EQ(fo4::set_record_scalar(wide, record_key(), wide_field, "4"), fo4::ScalarMutationStatus::updated);

  auto short_disc = load({{"CNAM", raw({0, 0, 2})}, {"DATA", raw({0})}});
  EXPECT_EQ(fo4::set_record_scalar(short_disc, record_key(), wide_field, "4"), fo4::ScalarMutationStatus::subrecord_missing);
}

TEST(FieldWriterString, SetsReplacesAndReportsUnchanged) {
  auto plugin = load({{"STRG", text("abc")}, {"KEEP", raw({4})}});
  const auto field = string_field("STRG");

  EXPECT_EQ(fo4::set_record_string(plugin, record_key(), field, 0, "hello"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(payload(snapshot(plugin), 0), text("hello"));
  EXPECT_EQ(fo4::set_record_string(plugin, record_key(), field, 0, "hello"), fo4::ScalarMutationStatus::unchanged);
  EXPECT_EQ(fo4::set_record_string(plugin, record_key(), field, 0, ""), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(payload(snapshot(plugin), 0), raw({0}));
  EXPECT_EQ(payload(snapshot(plugin), 1), raw({4}));
  EXPECT_EQ(fo4::record_field_count(plugin, record_key(), field), std::optional<std::size_t>{1});
  EXPECT_EQ(fo4::set_record_string(plugin, record_key(), field, 1, "x"), fo4::ScalarMutationStatus::subrecord_missing);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(payload(snapshot(reread), 0), raw({0}));
}

TEST(FieldWriterString, NativeWriterStoresTheBytesItIsGiven) {
  auto plugin = load({{"STRG", text("abc")}});
  const std::string stored = "caf\xE9";
  EXPECT_EQ(fo4::set_record_string(plugin, record_key(), string_field("STRG"), 0, stored),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(payload(snapshot(plugin), 0), raw({'c', 'a', 'f', 0xE9, 0}));
}

TEST(FieldWriterString, RepeatedOccurrencesAreAddressedByIndex) {
  auto plugin = load({{"STRG", text("one")}, {"STRG", text("two")}, {"STRG", text("three")}});
  const auto field = string_field("STRG", repeated());

  EXPECT_EQ(fo4::record_field_count(plugin, record_key(), field), std::optional<std::size_t>{3});
  EXPECT_EQ(fo4::set_record_string(plugin, record_key(), field, 1, "2"), fo4::ScalarMutationStatus::updated);
  const auto after = snapshot(plugin);
  EXPECT_EQ(after[0].data, text("one"));
  EXPECT_EQ(after[1].data, text("2"));
  EXPECT_EQ(after[2].data, text("three"));
}

TEST(FieldWriterString, RefusesEmbeddedNulAndMalformedPayloads) {
  auto plugin = load({{"STRG", text("abc")}});
  const auto field = string_field("STRG");
  EXPECT_THROW(fo4::set_record_string(plugin, record_key(), field, 0, std::string_view("a\0b", 3)),
               fo4::schema::ScalarError);
  EXPECT_EQ(payload(snapshot(plugin), 0), text("abc"));

  auto unterminated = load({{"STRG", text("abc", false)}});
  EXPECT_THROW(fo4::set_record_string(unterminated, record_key(), field, 0, "x"), fo4::schema::ScalarError);
  auto doubled = load({{"STRG", raw({'a', 0, 'b', 0})}});
  EXPECT_THROW(fo4::set_record_string(doubled, record_key(), field, 0, "x"), fo4::schema::ScalarError);
  auto empty = load({{"STRG", {}}});
  EXPECT_THROW(fo4::set_record_string(empty, record_key(), field, 0, "x"), fo4::schema::ScalarError);
}

TEST(FieldWriterString, RefusesLocalizedFieldsInLocalizedPlugins) {
  const auto field = string_field("FULL", {}, "ARMO");
  const std::uint32_t localized_flag = 0x80;

  auto localized = load({{"FULL", u32s({7})}}, "ARMO", localized_flag);
  const auto before = snapshot(localized);
  EXPECT_THROW(fo4::set_record_string(localized, record_key(), field, 0, "x"), fo4::schema::ScalarError);
  EXPECT_EQ(snapshot(localized)[0].data, before[0].data);

  auto inline_text = load({{"FULL", text("Old")}}, "ARMO");
  EXPECT_EQ(fo4::set_record_string(inline_text, record_key(), field, 0, "New"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(payload(snapshot(inline_text), 0), text("New"));
}

TEST(FieldWriterString, StringAndScalarSettersStayApart) {
  auto plugin = load({{"STRG", text("abc")}, {"DATA", u32s({1})}});
  EXPECT_THROW(fo4::set_record_scalar(plugin, record_key(), string_field("STRG"), 0, "x"), fo4::schema::ScalarError);
  EXPECT_THROW(fo4::set_record_string(plugin, record_key(),
                                      scalar_field("DATA", ScalarStorage::u32, 0, 9, 0, false), 0, "1"),
               fo4::schema::ScalarError);
  EXPECT_THROW(fo4::set_record_string(plugin, record_key(), "TEST\\Unknown", 0, "x"), fo4::schema::ScalarError);
  EXPECT_EQ(fo4::set_record_scalar(plugin, fo4::FormKey::parse("000999:Patch.esp"), string_field("STRG"), 0, "x"),
            fo4::ScalarMutationStatus::record_missing);
}

TEST(FieldWriterString, RegisteredRaceStringsAreAddressable) {
  auto plugin = load({{"HNAM", text("old.dds")},
                      {"NAME", text("A")},
                      {"NAME", text("B")},
                      {"PHTN", text("P0")}},
                     "RACE");
  EXPECT_EQ(fo4::record_field_count(plugin, record_key(), "RACE\\BipedObjectNames"),
            std::optional<std::size_t>{2});
  EXPECT_EQ(fo4::record_field_count(plugin, record_key(), "RACE\\HairColorLookupTexture"),
            std::optional<std::size_t>{1});
  EXPECT_EQ(fo4::record_field_count(plugin, record_key(), "RACE\\HairColorExtendedLookupTexture"),
            std::optional<std::size_t>{0});
  EXPECT_EQ(fo4::set_record_string(plugin, record_key(), "RACE\\BipedObjectNames", 1, "Body"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(payload(snapshot(plugin), 2), text("Body"));
  EXPECT_EQ(payload(snapshot(plugin), 1), text("A"));
  EXPECT_THROW(fo4::record_field_count(plugin, record_key(), "RACE\\Missing"), fo4::schema::ScalarError);
}

TEST(FieldWriterString, PreflightAcceptsRepeatedAndFlagsBrokenStrings) {
  fo4::Record record;
  record.header.signature = Signature::from_string("RACE");
  record.form_key = record_key();
  const auto add = [&](std::string_view sig, std::vector<std::byte> data) {
    fo4::Subrecord sub;
    sub.signature = Signature::from_string(sig);
    sub.data = std::move(data);
    record.subrecords.push_back(std::move(sub));
  };
  add("NAME", text("A"));
  add("NAME", text("B"));
  add("HNAM", text("x.dds"));
  EXPECT_TRUE(fo4::schema::preflight_record(record).empty());

  add("HNAM", text("y.dds"));
  add("NAME", text("C", false));
  const auto issues = fo4::schema::preflight_record(record);
  ASSERT_EQ(issues.size(), 2u);
  EXPECT_EQ(issues[0].path, "RACE\\BipedObjectNames");
  EXPECT_EQ(issues[0].code, "invalid_scalar_storage");
  EXPECT_EQ(issues[1].path, "RACE\\HairColorLookupTexture");
  EXPECT_EQ(issues[1].code, "duplicate_scalar_subrecord");
}

TEST(FieldWriterFormLink, RepeatedOccurrenceAndNullHandling) {
  auto plugin = load({{"LNKS", u32s({0x801})}, {"LNKS", u32s({0x802})}});
  const auto nullable = link_field("LNKS", 0, true, repeated());
  const auto before = snapshot(plugin);

  EXPECT_EQ(fo4::record_field_count(plugin, record_key(), nullable), std::optional<std::size_t>{2});
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, record_key(), nullable, 1, master_b),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(payload(snapshot(plugin), 0), before[0].data);
  EXPECT_EQ(payload(snapshot(plugin), 1), u32s({0x900}));
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, record_key(), nullable, 1, master_b),
            fo4::FormLinkFieldMutationStatus::unchanged);

  EXPECT_EQ(fo4::set_record_formlink_field(plugin, record_key(), nullable, 0, fo4::FormKey{}),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(payload(snapshot(plugin), 0), u32s({0}));

  const auto strict = link_field("LNKS", 0, false, repeated());
  EXPECT_THROW(fo4::set_record_formlink_field(plugin, record_key(), strict, 1, fo4::FormKey{}),
               fo4::schema::FormLinkFieldError);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, record_key(), strict, 2, master_a),
            fo4::FormLinkFieldMutationStatus::subrecord_missing);
  EXPECT_THROW(fo4::set_record_formlink_field(plugin, record_key(), strict, 0, fo4::FormKey::parse("000900:Other.esm")),
               fo4::FormIdError);
}

TEST(FieldWriterFormLink, NonFittingOccurrenceThrowsAndLaterOnesStayAddressable) {
  auto plugin = load({{"LNKS", u32s({0x801})}, {"LNKS", raw({1, 2})}, {"LNKS", u32s({0x802})}});
  const auto field = link_field("LNKS", 0, false, repeated());
  EXPECT_EQ(fo4::record_field_count(plugin, record_key(), field), std::optional<std::size_t>{3});
  EXPECT_THROW(fo4::set_record_formlink_field(plugin, record_key(), field, 1, master_b),
               fo4::schema::FormLinkFieldError);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, record_key(), field, 2, master_b),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(payload(snapshot(plugin), 2), u32s({0x900}));
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, record_key(), field, 3, master_b),
            fo4::FormLinkFieldMutationStatus::subrecord_missing);
}

TEST(FieldWriterResolver, MisalignedArrayIsFlaggedOnlyForTheExpectedPayloadSize) {
  fo4::Record record;
  fo4::Subrecord sub;
  sub.signature = Signature::from_string("ARRY");
  sub.data.assign(13, std::byte{0});
  record.subrecords.push_back(sub);
  fo4::schema::FieldShape shape;
  shape.subrecord = sub.signature;
  shape.offset = 0;
  shape.width = 4;

  auto resolved = fo4::schema::resolve_field_slots(record, shape, strided(8, 4));
  EXPECT_TRUE(resolved.misaligned);
  EXPECT_TRUE(resolved.slots.empty());
  EXPECT_FALSE(resolved.malformed);

  shape.payload_size = 12;
  resolved = fo4::schema::resolve_field_slots(record, shape, strided(8, 4));
  EXPECT_FALSE(resolved.misaligned);

  record.subrecords[0].data.assign(12, std::byte{0});
  shape.payload_size = 0;
  EXPECT_FALSE(fo4::schema::resolve_field_slots(record, shape, strided(8, 4)).misaligned);
}

TEST(FieldWriterString, SignatureLookupSkipsRepeatedAndStringRows) {
  EXPECT_FALSE(fo4::schema::scalar_descriptor(Signature::from_string("NAME")).has_value());
  EXPECT_FALSE(fo4::schema::scalar_descriptor(Signature::from_string("HNAM")).has_value());
  EXPECT_FALSE(fo4::schema::scalar_descriptor(Signature::from_string("XHLT")).has_value());
  EXPECT_FALSE(fo4::schema::scalar_descriptor(Signature::from_string("COCT")).has_value());
  EXPECT_TRUE(fo4::schema::scalar_descriptor(Signature::from_string("IDLC")).has_value());
}

TEST(FieldWriterFormLink, StrideAndDiscriminator) {
  fo4::test::ByteWriter body;
  body.u32(0xFEEDFACE);
  for (std::uint32_t e = 0; e < 3; ++e) {
    body.u32(0x801 + e);
    body.u32(e);
  }
  auto plugin = load({{"ARRY", body.bytes}, {"CNAM", raw({5})}});
  const auto before = snapshot(plugin);
  const auto guarded = link_field("ARRY", 0, false, with_when(strided(8, 4), "CNAM", 0, DiscriminatorStorage::u8, {5}));

  EXPECT_EQ(fo4::record_field_count(plugin, record_key(), guarded), std::optional<std::size_t>{3});
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, record_key(), guarded, 2, master_b),
            fo4::FormLinkFieldMutationStatus::updated);
  auto expected = before[0].data;
  expected[4 + 2 * 8] = std::byte{0x00};
  expected[4 + 2 * 8 + 1] = std::byte{0x09};
  EXPECT_EQ(payload(snapshot(plugin), 0), expected);

  const auto mismatched = link_field("ARRY", 0, false, with_when(strided(8, 4), "CNAM", 0, DiscriminatorStorage::u8, {6}));
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, record_key(), mismatched, 0, master_b),
            fo4::FormLinkFieldMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::record_field_count(plugin, record_key(), mismatched), std::optional<std::size_t>{3});
}

TEST(FieldWriterFormLink, UniqueFieldKeepsLegacyBehaviour) {
  auto plugin = load({{"LNKS", u32s({0x801})}});
  const auto field = link_field("LNKS", 0, false);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, record_key(), field, master_b),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, record_key(), field, 1, master_a),
            fo4::FormLinkFieldMutationStatus::subrecord_missing);

  auto mismatch = field;
  mismatch.record = Signature::from_string("OTHR");
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, record_key(), mismatch, master_a),
            fo4::FormLinkFieldMutationStatus::record_mismatch);

  auto duplicated = load({{"LNKS", u32s({1})}, {"LNKS", u32s({2})}});
  EXPECT_THROW(fo4::set_record_formlink_field(duplicated, record_key(), field, master_a),
               fo4::schema::FormLinkFieldError);

  auto short_payload = load({{"LNKS", raw({1, 2})}});
  EXPECT_THROW(fo4::set_record_formlink_field(short_payload, record_key(), field, master_a),
               fo4::schema::FormLinkFieldError);
}

TEST(FieldWriterCount, PathOverloadCoversScalarAndFormLinkFields) {
  auto plugin = load({{"XHLT", u32s({50})}}, "REFR");
  EXPECT_EQ(fo4::record_field_count(plugin, record_key(), "REFR\\HealthPercent"), std::optional<std::size_t>{1});
  EXPECT_EQ(fo4::record_field_count(plugin, record_key(), "REFR\\Scale"), std::optional<std::size_t>{0});
  EXPECT_EQ(fo4::record_field_count(plugin, fo4::FormKey::parse("000999:Patch.esp"), "REFR\\Scale"), std::nullopt);
  EXPECT_THROW(fo4::record_field_count(plugin, record_key(), "REFR\\Nope"), fo4::schema::ScalarError);
  EXPECT_THROW(fo4::record_field_count(plugin, record_key(), "EXPL\\Force"), fo4::schema::ScalarError);

  auto expl = load({{"DATA", std::vector<std::byte>(84, std::byte{0})}}, "EXPL");
  EXPECT_EQ(fo4::record_field_count(expl, record_key(), "EXPL\\Light"), std::optional<std::size_t>{1});
}

TEST(FieldWriterRealData, RegisteredRaceStringsMatchEveryShippedRace) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to the Fallout 4 Data folder to run this";
  }
  record_game_build(detect_game_build(dir));

  std::size_t races = 0;
  std::size_t biped_names = 0;
  std::size_t phoneme_names = 0;
  std::size_t lookups = 0;
  std::vector<std::string> failures;
  for (const char* name : {"Fallout4.esm", "DLCCoast.esm", "DLCNukaWorld.esm", "DLCRobot.esm",
                           "DLCworkshop01.esm", "DLCworkshop02.esm", "DLCworkshop03.esm"}) {
    const auto path = std::filesystem::path{dir} / name;
    if (!std::filesystem::exists(path)) continue;
    auto plugin = fo4::read_plugin_file(path);
    for (const auto* record : fo4::collect_records(plugin)) {
      if (record->header.signature != Signature::from_string("RACE")) continue;
      ++races;
      for (const auto& issue : fo4::schema::preflight_record(*record)) {
        if (issue.path.rfind("RACE\\", 0) == 0 && issue.code != "scalar_value_out_of_range" &&
            (issue.path.find("Names") != std::string::npos || issue.path.find("Texture") != std::string::npos)) {
          failures.push_back(std::string{name} + " " + record->form_key.to_string() + " " + issue.path + " " + issue.code);
        }
      }
      for (const auto path_name : {"RACE\\BipedObjectNames", "RACE\\PhonemeTargetNames",
                                   "RACE\\HairColorLookupTexture", "RACE\\HairColorExtendedLookupTexture"}) {
        const auto count = fo4::record_field_count(plugin, record->form_key, path_name);
        ASSERT_TRUE(count.has_value());
        const std::string_view view{path_name};
        if (view == "RACE\\BipedObjectNames") biped_names += *count;
        if (view == "RACE\\PhonemeTargetNames") phoneme_names += *count;
        if (view.find("Texture") != std::string_view::npos) lookups += *count;
        if (*count == 0) continue;
        for (const std::size_t index : {std::size_t{0}, *count - 1}) {
          const auto field = *fo4::schema::scalar_field_descriptor(path_name);
          const auto slots = fo4::schema::resolve_field_slots(
              *record, fo4::schema::scalar_field_shape(field.scalar), field.scalar.address);
          const auto bytes = fo4::schema::scalar_slot_bytes(
              record->subrecords[slots.slots[index].subrecord].data, slots.slots[index], field.scalar);
          const auto current = fo4::schema::decode_scalar(bytes, field.scalar);
          if (fo4::set_record_string(plugin, record->form_key, path_name, index, current) !=
              fo4::ScalarMutationStatus::unchanged) {
            failures.push_back(std::string{name} + " " + record->form_key.to_string() + " " + path_name + " rewrite changed");
          }
        }
      }
    }
  }
  EXPECT_TRUE(failures.empty()) << failures.front() << " (" << failures.size() << " failures)";
  RecordProperty("races", static_cast<int>(races));
  RecordProperty("biped_names", static_cast<int>(biped_names));
  RecordProperty("phoneme_names", static_cast<int>(phoneme_names));
  RecordProperty("lookup_textures", static_cast<int>(lookups));
  std::cout << "RACE records " << races << ", biped names " << biped_names << ", phoneme names "
            << phoneme_names << ", lookup textures " << lookups << "\n";
  EXPECT_GT(races, 0u);
}

TEST(FieldWriterFormLink, RequestedReplacementDoesNotResolveOldRawValue) {
  auto plugin = load({{"LINK", u32s({0x04001234})}, {"ZZZZ", raw({1, 2, 3})}});
  const auto field = link_field("LINK", 0, true);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, record_key(), field, 0, fo4::FormKey{}),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, record_key(), field, 0, fo4::FormKey{}),
            fo4::FormLinkFieldMutationStatus::unchanged);
  const auto saved = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(payload(snapshot(saved), 0), u32s({0}));
  EXPECT_EQ(payload(snapshot(saved), 1), raw({1, 2, 3}));
}

TEST(FieldWriterScalar, MissingDiscriminatorMatchesOnlyWhenExplicitlyEnabled) {
  auto address = with_when({}, "CNAM", 0, DiscriminatorStorage::u32, {7});
  address.when.match_absent = true;
  const auto field = scalar_field("DATA", ScalarStorage::u32, 0, 1000, 0, false, address);
  auto absent = load({{"DATA", u32s({1})}});
  EXPECT_EQ(fo4::set_record_scalar(absent, record_key(), field, 0, "2"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(payload(snapshot(absent), 0), u32s({2}));
  for (const auto& subs : std::vector<std::vector<Sub>>{
         {{"CNAM", u32s({8})}, {"DATA", u32s({1})}},
         {{"CNAM", raw({7})}, {"DATA", u32s({1})}},
         {{"CNAM", u32s({7})}, {"CNAM", u32s({7})}, {"DATA", u32s({1})}}}) {
    auto refused = load(subs);
    const auto before = fo4::write_plugin(refused);
    EXPECT_EQ(fo4::set_record_scalar(refused, record_key(), field, 0, "2"), fo4::ScalarMutationStatus::subrecord_missing);
    EXPECT_EQ(fo4::write_plugin(refused), before);
  }
  address.when.match_absent = false;
  const auto strict = scalar_field("DATA", ScalarStorage::u32, 0, 1000, 0, false, address);
  EXPECT_EQ(fo4::set_record_scalar(absent, record_key(), strict, 0, "3"), fo4::ScalarMutationStatus::subrecord_missing);
}
