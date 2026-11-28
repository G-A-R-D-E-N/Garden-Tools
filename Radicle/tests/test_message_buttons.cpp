#include "fo4/forms/message_buttons.hpp"
#include "fo4/flags.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "condition_scope_fixture.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using fo4::test::scope::Body;
using fo4::test::scope::ctda_bytes;
using fo4::test::scope::zstring;

fo4::FormKey mesg_key() { return fo4::FormKey::parse("000800:Cond.esp"); }

Bytes u32_bytes(std::uint32_t value) {
  fo4::test::ByteWriter out;
  out.u32(value);
  return out.bytes;
}

std::vector<std::byte> make_localized_plugin(const Body& body, std::uint32_t tes4_flags) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", tes4_flags, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(0x802);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  const auto group = fo4::test::begin_group(out, "MESG", 0);
  const auto record = fo4::test::begin_record(out, "MESG", 0, 0x00000800);
  for (const auto& [sig, data] : body) fo4::test::write_subrecord(out, sig, std::span<const std::byte>{data});
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  const auto misc_group = fo4::test::begin_group(out, "MISC", 0);
  const auto misc = fo4::test::begin_record(out, "MISC", 0, 0x00000801);
  fo4::test::end_record(out, misc);
  fo4::test::end_group(out, misc_group);
  return out.bytes;
}

fo4::Plugin reparse(const fo4::Plugin& plugin) {
  return fo4::read_plugin(fo4::write_plugin(plugin), "Cond.esp");
}

std::vector<std::string> signatures(const fo4::Plugin& plugin) {
  std::vector<std::string> out;
  for (const auto& subrecord : fo4::find_record(plugin, mesg_key())->subrecords) {
    out.push_back(subrecord.signature.to_string());
  }
  return out;
}

std::vector<Bytes> payloads(const fo4::Plugin& plugin, std::string_view signature) {
  std::vector<Bytes> out;
  for (const auto& subrecord : fo4::find_record(plugin, mesg_key())->subrecords) {
    if (subrecord.signature.to_string() == signature) out.push_back(subrecord.data);
  }
  return out;
}

std::uint32_t word(const Bytes& data) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[i])) << (8 * i);
  }
  return value;
}

fo4::schema::LocalizedString inline_text(std::string text) {
  fo4::schema::LocalizedString value;
  value.text = std::move(text);
  return value;
}

fo4::schema::LocalizedString string_id(std::uint32_t id) {
  fo4::schema::LocalizedString value;
  value.id = id;
  return value;
}

Body base_body(std::uint32_t flags) {
  return {{"DESC", zstring("Body")}, {"INAM", Bytes(4)}, {"DNAM", u32_bytes(flags)}};
}

Body with_tail(Body body, Body tail) {
  body.insert(body.end(), tail.begin(), tail.end());
  return body;
}

}  // namespace

TEST(MessageButtons, ReplacesButtonsAndTheirConditionsAndSetsTheMessageBoxBit) {
  auto plugin = fo4::test::scope::make_plugin(
      "MESG", with_tail(base_body(0x2u), {{"ITXT", zstring("Old")}, {"CTDA", ctda_bytes(1)}, {"CIS1", zstring("p")},
                                          {"ITXT", zstring("Older")}, {"CTDA", ctda_bytes(2)}}));
  const std::vector<fo4::schema::LocalizedString> texts{inline_text("Use"), inline_text("Cancel")};
  EXPECT_EQ(fo4::forms::set_message_buttons(plugin, mesg_key(), texts), fo4::forms::MessageButtonsWrite::written);
  const auto after = reparse(plugin);
  EXPECT_EQ(signatures(after), (std::vector<std::string>{"EDID", "DESC", "INAM", "DNAM", "ITXT", "ITXT"}));
  EXPECT_EQ(payloads(after, "ITXT"), (std::vector<Bytes>{zstring("Use"), zstring("Cancel")}));
  EXPECT_EQ(word(payloads(after, "DNAM").at(0)), 0x3u);
}

TEST(MessageButtons, AnEmptyListClearsTheButtonsAndStillSetsTheBit) {
  auto plugin = fo4::test::scope::make_plugin(
      "MESG", with_tail(base_body(0), {{"ITXT", zstring("Old")}, {"CTDA", ctda_bytes(1)}}));
  EXPECT_EQ(fo4::forms::set_message_buttons(plugin, mesg_key(), {}), fo4::forms::MessageButtonsWrite::written);
  const auto after = reparse(plugin);
  EXPECT_EQ(signatures(after), (std::vector<std::string>{"EDID", "DESC", "INAM", "DNAM"}));
  EXPECT_EQ(word(payloads(after, "DNAM").at(0)), 0x1u);
}

TEST(MessageButtons, InsertsAMissingDnamAtItsAuthoringPositionBeforeTheButtons) {
  auto plugin = fo4::test::scope::make_plugin(
      "MESG", {{"DESC", zstring("Body")}, {"INAM", Bytes(4)}, {"TNAM", u32_bytes(5)}, {"NNAM", zstring("n")}});
  const std::vector<fo4::schema::LocalizedString> texts{inline_text("Ok")};
  EXPECT_EQ(fo4::forms::set_message_buttons(plugin, mesg_key(), texts), fo4::forms::MessageButtonsWrite::written);
  const auto after = reparse(plugin);
  EXPECT_EQ(signatures(after),
            (std::vector<std::string>{"EDID", "DESC", "INAM", "DNAM", "TNAM", "NNAM", "ITXT"}));
  EXPECT_EQ(word(payloads(after, "DNAM").at(0)), 0x1u);
}

TEST(MessageButtons, ALocalizedPluginTakesStringIdsAndRefusesInlineText) {
  auto plugin = fo4::read_plugin(make_localized_plugin(base_body(0), fo4::RecordFlags::localized), "Cond.esp");
  const std::vector<fo4::schema::LocalizedString> ids{string_id(0x1234), string_id(0x1235)};
  EXPECT_EQ(fo4::forms::set_message_buttons(plugin, mesg_key(), ids), fo4::forms::MessageButtonsWrite::written);
  const auto after = reparse(plugin);
  EXPECT_EQ(payloads(after, "ITXT"), (std::vector<Bytes>{u32_bytes(0x1234), u32_bytes(0x1235)}));

  const std::vector<fo4::schema::LocalizedString> text{inline_text("Use")};
  EXPECT_EQ(fo4::forms::set_message_buttons(plugin, mesg_key(), text),
            fo4::forms::MessageButtonsWrite::localization_mismatch);
  auto plain = fo4::test::scope::make_plugin("MESG", base_body(0));
  EXPECT_EQ(fo4::forms::set_message_buttons(plain, mesg_key(), ids),
            fo4::forms::MessageButtonsWrite::localization_mismatch);
}

TEST(MessageButtons, RefusalsChangeNothing) {
  auto plugin = fo4::test::scope::make_plugin(
      "MESG", with_tail(base_body(0x2u), {{"ITXT", zstring("Old")}, {"CTDA", ctda_bytes(1)}}));
  const auto before = fo4::write_plugin(plugin);
  const std::vector<fo4::schema::LocalizedString> good{inline_text("Use")};
  EXPECT_EQ(fo4::forms::set_message_buttons(plugin, fo4::FormKey::parse("000999:Cond.esp"), good),
            fo4::forms::MessageButtonsWrite::record_missing);
  const std::vector<fo4::schema::LocalizedString> empty_text{inline_text("")};
  EXPECT_EQ(fo4::forms::set_message_buttons(plugin, mesg_key(), empty_text),
            fo4::forms::MessageButtonsWrite::invalid_text);
  const std::vector<fo4::schema::LocalizedString> nul_text{inline_text(std::string("a\0b", 3))};
  EXPECT_EQ(fo4::forms::set_message_buttons(plugin, mesg_key(), nul_text),
            fo4::forms::MessageButtonsWrite::invalid_text);
  const std::vector<fo4::schema::LocalizedString> many(65536, inline_text("x"));
  EXPECT_EQ(fo4::forms::set_message_buttons(plugin, mesg_key(), many),
            fo4::forms::MessageButtonsWrite::too_many_buttons);
  EXPECT_EQ(fo4::write_plugin(plugin), before);

  auto foreign = fo4::test::scope::make_plugin(
      "MESG", with_tail(base_body(0), {{"ITXT", zstring("Old")}, {"ZZZZ", Bytes(1)}}));
  const auto foreign_before = fo4::write_plugin(foreign);
  EXPECT_EQ(fo4::forms::set_message_buttons(foreign, mesg_key(), good),
            fo4::forms::MessageButtonsWrite::unexpected_layout);
  EXPECT_EQ(fo4::write_plugin(foreign), foreign_before);

  auto doubled = fo4::test::scope::make_plugin("MESG", {{"DNAM", u32_bytes(0)}, {"DNAM", u32_bytes(0)}});
  EXPECT_EQ(fo4::forms::set_message_buttons(doubled, mesg_key(), good),
            fo4::forms::MessageButtonsWrite::unexpected_layout);
  auto short_dnam = fo4::test::scope::make_plugin("MESG", {{"DNAM", Bytes(2)}});
  EXPECT_EQ(fo4::forms::set_message_buttons(short_dnam, mesg_key(), good),
            fo4::forms::MessageButtonsWrite::unexpected_layout);
  auto unordered = fo4::test::scope::make_plugin("MESG", {{"ZZZZ", Bytes(1)}});
  EXPECT_EQ(fo4::forms::set_message_buttons(unordered, mesg_key(), good),
            fo4::forms::MessageButtonsWrite::no_insertion_point);

  auto other = fo4::read_plugin(make_localized_plugin(base_body(0), 0), "Cond.esp");
  EXPECT_EQ(fo4::forms::set_message_buttons(other, fo4::FormKey::parse("000801:Cond.esp"), good),
            fo4::forms::MessageButtonsWrite::unsupported_record);
}
