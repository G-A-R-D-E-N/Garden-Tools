#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using fo4::Signature;
using fo4::test::ByteWriter;

constexpr std::string_view k_key = "000800:Patch.esp";

Bytes zstr(std::string_view text) {
  ByteWriter out;
  out.raw(text);
  out.u8(0);
  return out.bytes;
}

Bytes response(std::uint8_t number) {
  ByteWriter out;
  out.u32(0x810);
  out.u8(number);
  out.u32(0x811);
  out.u8(0xAB);
  out.u16(90);
  out.u32(0xFFFFFFFF);
  out.u32(42);
  return out.bytes;
}

Bytes make_plugin(std::initializer_list<std::byte> nam9) {
  ByteWriter out;
  out.bytes = fo4::test::make_tes4_plugin(0, 1.0f, {"Fallout4.esm"});
  const auto group = fo4::test::begin_group(out, "INFO", 0);
  const auto record = fo4::test::begin_record(out, "INFO", 0, 0x01000800u);
  fo4::test::write_subrecord(out, "EDID", zstr("RadicleInfo"));
  fo4::test::write_subrecord(out, "TRDA", response(1));
  for (const auto* member : {"NAM1", "NAM2", "NAM3", "NAM4"}) {
    fo4::test::write_subrecord(out, member, Bytes{std::byte{0}});
  }
  fo4::test::write_subrecord(out, "NAM9", Bytes(nam9));
  fo4::test::write_subrecord(out, "WZMD", Bytes{});
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

fo4::schema::ScalarFieldDescriptor row() {
  const auto field = fo4::schema::scalar_field_descriptor(
      fo4::Signature::from_string("INFO"), "INFO\\ResponseBytes");
  EXPECT_TRUE(field.has_value());
  return field.value_or(fo4::schema::ScalarFieldDescriptor{});
}

Bytes nam9_payload(const fo4::Plugin& plugin, const fo4::FormKey& record_key) {
  const auto* record = fo4::find_record(plugin, record_key);
  EXPECT_NE(record, nullptr);
  if (record == nullptr) return {};
  for (const auto& subrecord : record->subrecords) {
    if (subrecord.signature == fo4::Signature::from_string("NAM9")) return subrecord.data;
  }
  return {};
}

std::string nam9_message(const fo4::Plugin& plugin, const fo4::FormKey& record_key) {
  const auto* record = fo4::find_record(plugin, record_key);
  EXPECT_NE(record, nullptr);
  if (record == nullptr) return {};
  for (const auto& subrecord : record->subrecords) {
    if (subrecord.signature != fo4::Signature::from_string("NAM9")) continue;
    return fo4::schema::decode_scalar(subrecord.data, row().scalar);
  }
  return {};
}

std::optional<Bytes> subrecord_payload(const fo4::Plugin& plugin,
                                      const fo4::FormKey& record_key,
                                      Signature signature) {
  const auto* record = fo4::find_record(plugin, record_key);
  EXPECT_NE(record, nullptr);
  if (record == nullptr) return std::nullopt;
  for (const auto& subrecord : record->subrecords) {
    if (subrecord.signature == signature) return subrecord.data;
  }
  return std::nullopt;
}

std::vector<Signature> subrecord_signatures(const fo4::Plugin& plugin,
                                           const fo4::FormKey& record_key) {
  std::vector<Signature> out;
  const auto* record = fo4::find_record(plugin, record_key);
  EXPECT_NE(record, nullptr);
  if (record == nullptr) return out;
  for (const auto& subrecord : record->subrecords) out.push_back(subrecord.signature);
  return out;
}

template <typename Fn>
std::string thrown_message(Fn&& fn) {
  try {
    fn();
  } catch (const std::exception& error) {
    return error.what();
  }
  return {};
}

}  // namespace

// Constraint: the row must read the payload itself as lowercase hex; the regression this detects is a byte-array read that decodes as text or echoes the raw span.
TEST(InfoResponseBytes, ReadsThePayloadAsLowercaseHex) {
  auto plugin = fo4::read_plugin(
      make_plugin({std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE}, std::byte{0xEF}}), "Patch.esp");
  const auto record_key = fo4::FormKey::parse(k_key);
  const auto field = row();

  EXPECT_EQ(field.scalar.storage, fo4::schema::ScalarStorage::bytes);
  EXPECT_EQ(field.scalar.subrecord, fo4::Signature::from_string("NAM9"));
  EXPECT_EQ(nam9_message(plugin, record_key), "deadbeef");
  EXPECT_EQ(nam9_payload(plugin, record_key).size(), std::size_t{4});
  EXPECT_EQ(fo4::record_field_count(plugin, record_key, field), std::optional<std::size_t>{1});
}

TEST(InfoResponseBytes, SetReplacesTheWholePayloadAtEveryLength) {
  // Constraint: a bytes write must replace the payload rather than patch a fixed slot, so the length follows the value.
  auto plugin = fo4::read_plugin(make_plugin({std::byte{0xDE}, std::byte{0xAD}}), "Patch.esp");
  const auto record_key = fo4::FormKey::parse(k_key);
  const auto field = row();

  ASSERT_EQ(fo4::set_record_scalar(plugin, record_key, field, 0, "00112233"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(nam9_payload(plugin, record_key).size(), std::size_t{4});
  EXPECT_EQ(nam9_message(plugin, record_key), "00112233");

  auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(nam9_message(reread, record_key), "00112233");
  EXPECT_EQ(nam9_payload(reread, record_key).size(), std::size_t{4});

  ASSERT_EQ(fo4::set_record_scalar(reread, record_key, field, 0, "00"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(nam9_payload(reread, record_key).size(), std::size_t{1});
  EXPECT_EQ(nam9_message(reread, record_key), "00");

  ASSERT_EQ(fo4::set_record_scalar(reread, record_key, field, 0, ""),
            fo4::ScalarMutationStatus::updated);
  EXPECT_TRUE(nam9_payload(reread, record_key).empty());
  EXPECT_EQ(nam9_message(reread, record_key), "");

  const auto emptied = fo4::read_plugin(fo4::write_plugin(reread), "Patch.esp");
  EXPECT_TRUE(nam9_payload(emptied, record_key).empty());
  EXPECT_EQ(nam9_message(emptied, record_key), "");
}

TEST(InfoResponseBytes, RefusesMalformedHexWithTheExactMessages) {
  // Constraint: a malformed or odd-length hex value must be refused before it can truncate the payload or shift the bytes after the bad pair.
  auto plugin = fo4::read_plugin(make_plugin({std::byte{0xDE}, std::byte{0xAD}}), "Patch.esp");
  const auto record_key = fo4::FormKey::parse(k_key);
  const auto field = row();

  EXPECT_EQ(thrown_message([&] { fo4::set_record_scalar(plugin, record_key, field, 0, "0g"); }),
            "Response NAM9 bytes is not hexadecimal");
  EXPECT_EQ(thrown_message([&] { fo4::set_record_scalar(plugin, record_key, field, 0, "0"); }),
            "Response NAM9 bytes is not an even-length hex string");
  EXPECT_EQ(thrown_message([&] { fo4::set_record_scalar(plugin, record_key, field, 0, "abc"); }),
            "Response NAM9 bytes is not an even-length hex string");
  EXPECT_EQ(nam9_message(plugin, record_key), "dead");
  EXPECT_EQ(nam9_payload(plugin, record_key).size(), std::size_t{2});

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(nam9_message(reread, record_key), "dead");
}

// Constraint: a bytes write must leave the response grouping and its zero-byte marker alone; the regression this detects is a write that drops WZMD or breaks TRDA,NAM1..NAM4.
TEST(InfoResponseBytes, SetKeepsTheResponseShapeAndTheMarkerIntact) {
  auto plugin = fo4::read_plugin(make_plugin({}), "Patch.esp");
  const auto record_key = fo4::FormKey::parse(k_key);
  const auto field = row();
  const auto trda = Signature::from_string("TRDA");
  const auto nam9 = Signature::from_string("NAM9");

  ASSERT_EQ(fo4::set_record_scalar(plugin, record_key, field, 0, "cafebabe"),
            fo4::ScalarMutationStatus::updated);
  const std::vector<Signature> shape{Signature::from_string("EDID"), trda,
                                     Signature::from_string("NAM1"),
                                     Signature::from_string("NAM2"),
                                     Signature::from_string("NAM3"),
                                     Signature::from_string("NAM4"), nam9,
                                     Signature::from_string("WZMD")};
  EXPECT_EQ(subrecord_signatures(plugin, record_key), shape)
      << "the NAM9 write changed the response's member order";
  const auto prefix = subrecord_payload(plugin, record_key, trda);
  ASSERT_TRUE(prefix.has_value()) << "the NAM9 write dropped the TRDA prefix";
  EXPECT_EQ(*prefix, response(1)) << "the NAM9 write changed the TRDA prefix bytes";
  const auto marker = subrecord_payload(plugin, record_key, Signature::from_string("WZMD"));
  ASSERT_TRUE(marker.has_value()) << "the NAM9 write dropped the zero-byte WZMD marker";
  EXPECT_TRUE(marker->empty()) << "the zero-byte WZMD marker gained a payload";
  EXPECT_EQ(subrecord_payload(plugin, record_key, nam9), std::optional<Bytes>{Bytes{std::byte{0xCA},
                                                                                    std::byte{0xFE},
                                                                                    std::byte{0xBA},
                                                                                    std::byte{0xBE}}});

  const auto number = fo4::schema::scalar_field_descriptor(
      Signature::from_string("INFO"), "INFO\\ResponseNumber");
  ASSERT_TRUE(number.has_value());
  EXPECT_EQ(fo4::record_field_count(plugin, record_key, *number), std::optional<std::size_t>{1});
  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key, *number, 0, "2"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(nam9_message(plugin, record_key), "cafebabe");
  EXPECT_EQ(subrecord_signatures(plugin, record_key), shape)
      << "a response-number write changed the member order after a NAM9 write";
}
