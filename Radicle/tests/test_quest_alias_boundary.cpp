#include "fo4/forms/quest.hpp"
#include "fo4/plugin/reader.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string_view>
#include <vector>

namespace {

void write_u32_subrecord(fo4::test::ByteWriter& out,
                         std::string_view signature,
                         std::uint32_t value) {
  fo4::test::ByteWriter data;
  data.u32(value);
  fo4::test::write_subrecord(out, signature, data.bytes);
}

std::vector<std::byte> quest_bytes(bool second_alias_after_tail,
                                   bool duplicate_id) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, "QUST", 0);
  const auto quest = fo4::test::begin_record(out, "QUST", 0, 0x00000800, 131);
  write_u32_subrecord(out, "ANAM", 10);

  write_u32_subrecord(out, "ALST", 1);
  fo4::test::write_subrecord(out, "ALID", std::string_view{"One\0", 4});
  write_u32_subrecord(out, "FNAM", 0);
  fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});

  if (duplicate_id) {
    write_u32_subrecord(out, "ALLS", 1);
    write_u32_subrecord(out, "FNAM", 0);
    fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});
  }

  fo4::test::write_subrecord(out, "NNAM", std::string_view{"Tail\0", 5});

  if (second_alias_after_tail) {
    write_u32_subrecord(out, "ALCS", 9);
    const std::vector<std::byte> count{std::byte{1}};
    fo4::test::write_subrecord(out, "ALMI", count);
  }

  fo4::test::end_record(out, quest);
  fo4::test::end_group(out, group);
  return out.bytes;
}

}

TEST(QuestAliasBoundary, RejectsSecondAliasArrayAfterTail) {
  const auto plugin = fo4::read_plugin(
      quest_bytes(true, false), "Quest.esp");
  EXPECT_THROW(
      static_cast<void>(fo4::forms::decode_quest_aliases(
          plugin, fo4::FormKey::parse("000800:Quest.esp"))),
      fo4::forms::QuestError);
}

TEST(QuestAliasBoundary, RejectsDuplicateAliasIdsDuringDecode) {
  const auto plugin = fo4::read_plugin(
      quest_bytes(false, true), "Quest.esp");
  EXPECT_THROW(
      static_cast<void>(fo4::forms::decode_quest_aliases(
          plugin, fo4::FormKey::parse("000800:Quest.esp"))),
      fo4::forms::QuestError);
}
