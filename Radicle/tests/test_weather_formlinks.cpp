#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/form_links.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct WthrSub {
  std::string_view signature;
  std::vector<std::byte> data;
};

std::vector<std::byte> u32_bytes(std::uint32_t value) {
  fo4::test::ByteWriter out;
  out.u32(value);
  return out.bytes;
}

std::vector<std::byte> u16_pair(std::uint16_t first, std::uint16_t second) {
  fo4::test::ByteWriter out;
  out.u16(first);
  out.u16(second);
  return out.bytes;
}

std::vector<std::byte> float_bits(float value) {
  fo4::test::ByteWriter out;
  out.f32(value);
  return out.bytes;
}

std::vector<std::byte> zeros(std::size_t count) {
  return std::vector<std::byte>(count, std::byte{0});
}

std::uint32_t read_u32(const std::vector<std::byte>& data, std::size_t offset) {
  std::uint32_t value = 0;
  std::memcpy(&value, data.data() + offset, 4);
  return value;
}

void write_master(fo4::test::ByteWriter& out, std::string_view filename) {
  std::string mast{filename};
  mast.push_back('\0');
  fo4::test::write_subrecord(out, "MAST", mast);
  fo4::test::ByteWriter data;
  data.u32(0);
  data.u32(0);
  fo4::test::write_subrecord(out, "DATA", data.bytes);
}

std::vector<std::byte> make_weather_plugin(const std::vector<WthrSub>& subs) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x801);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  write_master(out, "Fallout4.esm");
  write_master(out, "DropMe.esm");
  write_master(out, "KeepMe.esm");
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, "WTHR", 0);
  const auto record = fo4::test::begin_record(out, "WTHR", 0, 0x03000800);
  for (const auto& sub : subs) {
    fo4::test::write_subrecord(out, sub.signature, sub.data);
  }
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

std::vector<fo4::PluginKey> target_masters() {
  return {fo4::PluginKey::from_filename("Fallout4.esm"),
          fo4::PluginKey::from_filename("KeepMe.esm")};
}

const fo4::Subrecord* find_sub(const fo4::Record& record, std::string_view signature) {
  for (const auto& sub : record.subrecords) {
    if (sub.signature.to_string() == signature) return &sub;
  }
  return nullptr;
}

std::vector<fo4::FormLinkHit> hits_for(const fo4::Plugin& plugin,
                                       const fo4::FormKey& key,
                                       std::string_view subrecord) {
  std::vector<fo4::FormLinkHit> result;
  for (const auto& hit : fo4::collect_form_links(plugin)) {
    if (hit.record == key && hit.subrecord.to_string() == subrecord) {
      result.push_back(hit);
    }
  }
  return result;
}

const fo4::FormKey k_weather_key = fo4::FormKey::parse("000800:WeatherPatch.esp");

std::vector<WthrSub> mixed_subrecords() {
  fo4::test::ByteWriter imsp;
  for (std::uint32_t slot = 0; slot < 8; ++slot) {
    imsp.u32(0x02000900u + slot);
  }
  fo4::test::ByteWriter snam;
  snam.u32(0x02000820u);
  snam.u32(0x0000002Au);

  fo4::test::ByteWriter unam;
  unam.u32(0x02000840u);
  unam.f32(0.25f);
  unam.u32(0x02000850u);

  return {
      {"LNAM", u32_bytes(0x0000ABCDu)},
      {"MNAM", u32_bytes(0x02000810u)},
      {"GNAM", u32_bytes(0x02000860u)},
      {"VNAM", float_bits(1.0f)},
      {"WNAM", float_bits(2.0f)},
      {"NAM1", u32_bytes(0x12345678u)},
      {"PNAM", u16_pair(0x0200u, 0x0812u)},
      {"JNAM", u16_pair(0x0200u, 0x0813u)},
      {"SNAM", snam.bytes},
      {"TNAM", u32_bytes(0x02000830u)},
      {"IMSP", imsp.bytes},
      {"UNAM", unam.bytes},
  };
}

}

TEST(WeatherFormLinkRemap, ProvenSlotsRemapWhileQuarantinedCollisionsStayByteIdentical) {
  const auto subs = mixed_subrecords();
  auto plugin = fo4::read_plugin(make_weather_plugin(subs), "WeatherPatch.esp");

  plugin = fo4::remap_plugin_masters(std::move(plugin), target_masters());
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "WeatherPatch.esp");
  const auto* record = fo4::find_record(reread, k_weather_key);
  ASSERT_NE(record, nullptr);

  const auto* lnam = find_sub(*record, "LNAM");
  const auto* mnam = find_sub(*record, "MNAM");
  const auto* gnam = find_sub(*record, "GNAM");
  const auto* vnam = find_sub(*record, "VNAM");
  const auto* wnam = find_sub(*record, "WNAM");
  const auto* nam1 = find_sub(*record, "NAM1");
  const auto* pnam = find_sub(*record, "PNAM");
  const auto* jnam = find_sub(*record, "JNAM");
  const auto* snam = find_sub(*record, "SNAM");
  const auto* tnam = find_sub(*record, "TNAM");
  const auto* imsp = find_sub(*record, "IMSP");
  const auto* unam = find_sub(*record, "UNAM");
  ASSERT_NE(lnam, nullptr);
  ASSERT_NE(mnam, nullptr);
  ASSERT_NE(gnam, nullptr);
  ASSERT_NE(vnam, nullptr);
  ASSERT_NE(wnam, nullptr);
  ASSERT_NE(nam1, nullptr);
  ASSERT_NE(pnam, nullptr);
  ASSERT_NE(jnam, nullptr);
  ASSERT_NE(snam, nullptr);
  ASSERT_NE(tnam, nullptr);
  ASSERT_NE(imsp, nullptr);
  ASSERT_NE(unam, nullptr);

  ASSERT_EQ(mnam->data.size(), 4u);
  EXPECT_EQ(read_u32(mnam->data, 0), 0x01000810u);
  ASSERT_EQ(gnam->data.size(), 4u);
  EXPECT_EQ(read_u32(gnam->data, 0), 0x01000860u);
  ASSERT_EQ(tnam->data.size(), 4u);
  EXPECT_EQ(read_u32(tnam->data, 0), 0x01000830u);

  ASSERT_EQ(snam->data.size(), 8u);
  EXPECT_EQ(read_u32(snam->data, 0), 0x01000820u);
  EXPECT_EQ(read_u32(snam->data, 4), 0x0000002Au);

  ASSERT_EQ(imsp->data.size(), 32u);
  for (std::uint32_t slot = 0; slot < 8; ++slot) {
    EXPECT_EQ(read_u32(imsp->data, slot * 4u), 0x01000900u + slot);
  }

  ASSERT_EQ(unam->data.size(), 12u);
  EXPECT_EQ(read_u32(unam->data, 0), 0x01000840u);
  EXPECT_EQ(read_u32(unam->data, 4), read_u32(float_bits(0.25f), 0));
  EXPECT_EQ(read_u32(unam->data, 8), 0x01000850u);

  EXPECT_EQ(lnam->data, u32_bytes(0x0000ABCDu));
  EXPECT_EQ(vnam->data, float_bits(1.0f));
  EXPECT_EQ(wnam->data, float_bits(2.0f));
  EXPECT_EQ(nam1->data, u32_bytes(0x12345678u));
  EXPECT_EQ(pnam->data, u16_pair(0x0200u, 0x0812u));
  EXPECT_EQ(jnam->data, u16_pair(0x0200u, 0x0813u));
}

TEST(WeatherFormLinkRemap, QuarantinedSignaturesReportNoLinksAtProvenOffsetsOnly) {
  auto plugin = fo4::read_plugin(make_weather_plugin(mixed_subrecords()),
                                       "WeatherPatch.esp");

  for (const auto* signature : {"LNAM", "VNAM", "WNAM", "NAM1", "PNAM", "JNAM"}) {
    EXPECT_TRUE(hits_for(plugin, k_weather_key, signature).empty()) << signature;
  }

  const auto mnam = hits_for(plugin, k_weather_key, "MNAM");
  const auto gnam = hits_for(plugin, k_weather_key, "GNAM");
  const auto snam = hits_for(plugin, k_weather_key, "SNAM");
  const auto tnam = hits_for(plugin, k_weather_key, "TNAM");
  const auto imsp = hits_for(plugin, k_weather_key, "IMSP");
  const auto unam = hits_for(plugin, k_weather_key, "UNAM");
  ASSERT_EQ(mnam.size(), 1u);
  ASSERT_EQ(gnam.size(), 1u);
  ASSERT_EQ(snam.size(), 1u);
  ASSERT_EQ(tnam.size(), 1u);
  ASSERT_EQ(imsp.size(), 8u);
  ASSERT_EQ(unam.size(), 2u);
  EXPECT_EQ(mnam[0].offset, 0u);
  EXPECT_EQ(gnam[0].offset, 0u);
  EXPECT_EQ(snam[0].offset, 0u);
  EXPECT_EQ(tnam[0].offset, 0u);
  for (std::size_t slot = 0; slot < imsp.size(); ++slot) {
    EXPECT_EQ(imsp[slot].offset, slot * 4u);
  }
  EXPECT_EQ(unam[0].offset, 0u);
  EXPECT_EQ(unam[1].offset, 8u);
}

struct MalformedFormLinkCase {
  const char* name;
  std::string_view signature;
  std::vector<std::byte> data;
};

std::ostream& operator<<(std::ostream& out, const MalformedFormLinkCase& value) {
  return out << value.name;
}

const std::array<MalformedFormLinkCase, 11> kMalformedFormLinkCases{{
    {"mnam-2-bytes", "MNAM", zeros(2)},
    {"nnam-8-bytes", "NNAM", zeros(8)},
    {"gnam-8-bytes", "GNAM", zeros(8)},
    {"tnam-8-bytes", "TNAM", zeros(8)},
    {"snam-7-bytes", "SNAM", zeros(7)},
    {"snam-12-bytes", "SNAM", zeros(12)},
    {"unam-16-bytes", "UNAM", zeros(16)},
    {"imsp-4-bytes", "IMSP", zeros(4)},
    {"imsp-12-bytes", "IMSP", zeros(12)},
    {"wgdr-16-bytes", "WGDR", zeros(16)},
    {"znam-4-bytes-not-a-wthr-link", "ZNAM", u32_bytes(0x02000812u)},
}};

class WthrMalformedFormLink : public testing::TestWithParam<MalformedFormLinkCase> {};

TEST_P(WthrMalformedFormLink, StrictRemapFailsClosedAndLeavesPluginUntouched) {
  const auto& param = GetParam();
  auto plugin = fo4::read_plugin(
      make_weather_plugin({{param.signature, param.data}}), "WeatherPatch.esp");

  EXPECT_TRUE(hits_for(plugin, k_weather_key, param.signature).empty());

  const auto before = fo4::write_plugin(plugin);
  EXPECT_THROW(fo4::remap_plugin_masters(plugin, target_masters()), fo4::FormIdError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

INSTANTIATE_TEST_SUITE_P(WeatherFormLinkRemap, WthrMalformedFormLink,
                         testing::ValuesIn(kMalformedFormLinkCases));

TEST(WeatherFormLinkRemap, WellFormedPayloadsOfTheSameSignaturesDoNotThrow) {
  fo4::test::ByteWriter imsp;
  for (std::uint32_t slot = 0; slot < 4; ++slot) {
    imsp.u32(0x02000900u + slot);
  }
  fo4::test::ByteWriter snam;
  snam.u32(0x02000820u);
  snam.u32(0x0000002Au);
  fo4::test::ByteWriter wgdr;
  for (std::uint32_t slot = 0; slot < 8; ++slot) {
    wgdr.u32(0x02000A00u + slot);
  }
  fo4::test::ByteWriter unam;
  unam.u32(0x02000840u);
  unam.f32(0.25f);
  unam.u32(0x02000850u);

  const std::vector<WthrSub> subs{
      {"MNAM", u32_bytes(0x02000810u)},
      {"NNAM", u32_bytes(0x02000811u)},
      {"GNAM", u32_bytes(0x02000812u)},
      {"TNAM", u32_bytes(0x02000830u)},
      {"SNAM", snam.bytes},
      {"WGDR", wgdr.bytes},
      {"IMSP", imsp.bytes},
      {"UNAM", unam.bytes},
  };

  auto plugin = fo4::read_plugin(make_weather_plugin(subs), "WeatherPatch.esp");
  ASSERT_NO_THROW(plugin = fo4::remap_plugin_masters(std::move(plugin), target_masters()));

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "WeatherPatch.esp");
  const auto* record = fo4::find_record(reread, k_weather_key);
  ASSERT_NE(record, nullptr);

  const auto* nnam = find_sub(*record, "NNAM");
  const auto* snam_after = find_sub(*record, "SNAM");
  const auto* wgdr_after = find_sub(*record, "WGDR");
  const auto* unam_after = find_sub(*record, "UNAM");
  ASSERT_NE(nnam, nullptr);
  ASSERT_NE(snam_after, nullptr);
  ASSERT_NE(wgdr_after, nullptr);
  ASSERT_NE(unam_after, nullptr);

  EXPECT_EQ(read_u32(nnam->data, 0), 0x01000811u);
  EXPECT_EQ(read_u32(snam_after->data, 0), 0x01000820u);
  EXPECT_EQ(read_u32(snam_after->data, 4), 0x0000002Au);
  for (std::uint32_t slot = 0; slot < 8; ++slot) {
    EXPECT_EQ(read_u32(wgdr_after->data, slot * 4u), 0x01000A00u + slot);
  }
  ASSERT_EQ(unam_after->data.size(), 12u);
  EXPECT_EQ(read_u32(unam_after->data, 0), 0x01000840u);
  EXPECT_EQ(read_u32(unam_after->data, 8), 0x01000850u);
}
