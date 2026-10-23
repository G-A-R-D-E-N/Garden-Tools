#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/subrecord_authoring.hpp"
#include "fo4/plugin/subrecord_copy.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/plugin_key.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <variant>
#include <vector>

namespace {

constexpr auto k_armo = fo4::Signature::from_string("ARMO");
constexpr auto k_icon = fo4::Signature::from_string("ICON");
constexpr auto k_full = fo4::Signature::from_string("FULL");
constexpr auto k_eitm = fo4::Signature::from_string("EITM");

std::vector<std::byte> payload(std::uint8_t value) {
  return {std::byte{value}, std::byte{value}, std::byte{0}};
}

fo4::Record& only_record(fo4::Plugin& plugin) {
  return std::get<fo4::Record>(plugin.groups.front().children.front());
}

std::vector<std::vector<std::byte>> icons(const fo4::Plugin& plugin, const fo4::FormKey& key) {
  std::vector<std::vector<std::byte>> out;
  const auto* record = fo4::find_record(plugin, key);
  if (record == nullptr) return out;
  for (const auto& sub : record->subrecords) {
    if (sub.signature == k_icon) out.push_back(sub.data);
  }
  return out;
}

std::vector<std::vector<std::byte>> fulls(const fo4::Plugin& plugin, const fo4::FormKey& key) {
  std::vector<std::vector<std::byte>> out;
  const auto* record = fo4::find_record(plugin, key);
  if (record == nullptr) return out;
  for (const auto& sub : record->subrecords) {
    if (sub.signature == k_full) out.push_back(sub.data);
  }
  return out;
}

std::vector<std::vector<std::byte>> subrecords(const fo4::Plugin& plugin, const fo4::FormKey& key, fo4::Signature signature) {
  std::vector<std::vector<std::byte>> out;
  const auto* record = fo4::find_record(plugin, key);
  if (record == nullptr) return out;
  for (const auto& sub : record->subrecords) {
    if (sub.signature == signature) out.push_back(sub.data);
  }
  return out;
}

}  // namespace

TEST(SubrecordCopy, ReplacesAnOccurrenceTheDestinationAlreadyHas) {
  auto source = fo4::make_empty_plugin("Source.esp");
  const auto sourceKey = fo4::create_record(source, k_armo, "SourceArmor").key;
  only_record(source).subrecords.push_back({k_icon, payload(7), false});

  auto target = fo4::make_empty_plugin("Target.esp");
  const auto targetKey = fo4::create_record(target, k_armo, "TargetArmor").key;
  only_record(target).subrecords.push_back({k_icon, payload(3), false});

  ASSERT_EQ(fo4::copy_record_subrecord(target, targetKey, source, sourceKey, k_icon, 0),
            fo4::SubrecordCopyStatus::copied);
  const auto after = icons(target, targetKey);
  ASSERT_EQ(after.size(), 1u);
  EXPECT_EQ(after.front(), payload(7));
}

TEST(SubrecordCopy, AddsAMissingOccurrenceInSchemaOrderWhenTheDestinationLacksIt) {
  auto source = fo4::make_empty_plugin("Source.esp");
  const auto sourceKey = fo4::create_record(source, k_armo, "SourceArmor").key;
  only_record(source).subrecords.push_back({k_icon, payload(7), false});

  auto target = fo4::make_empty_plugin("Target.esp");
  const auto targetKey = fo4::create_record(target, k_armo, "TargetArmor").key;
  ASSERT_TRUE(icons(target, targetKey).empty());

  ASSERT_EQ(fo4::copy_record_subrecord(target, targetKey, source, sourceKey, k_icon, 0),
            fo4::SubrecordCopyStatus::copied);
  const auto after = icons(target, targetKey);
  ASSERT_EQ(after.size(), 1u);
  EXPECT_EQ(after.front(), payload(7));
}

TEST(SubrecordCopy, RefusesAnOccurrenceTheSourceDoesNotHaveWithoutWriting) {
  auto source = fo4::make_empty_plugin("Source.esp");
  const auto sourceKey = fo4::create_record(source, k_armo, "SourceArmor").key;

  auto target = fo4::make_empty_plugin("Target.esp");
  const auto targetKey = fo4::create_record(target, k_armo, "TargetArmor").key;

  EXPECT_EQ(fo4::copy_record_subrecord(target, targetKey, source, sourceKey, k_icon, 0),
            fo4::SubrecordCopyStatus::source_occurrence_missing);
  EXPECT_TRUE(icons(target, targetKey).empty());
}

TEST(SubrecordCopy, RefusesALocalizedStringIdAcrossPluginsWithoutWriting) {
  auto source = fo4::set_plugin_localized_flag(fo4::make_empty_plugin("Source.esp"), true);
  const auto sourceKey = fo4::create_record(source, k_armo, "SourceArmor").key;
  only_record(source).subrecords.push_back(
      {k_full, {std::byte{1}, std::byte{0}, std::byte{0}, std::byte{0}}, false});

  auto target = fo4::set_plugin_localized_flag(fo4::make_empty_plugin("Target.esp"), true);
  const auto targetKey = fo4::create_record(target, k_armo, "TargetArmor").key;
  const std::vector<std::byte> targetId{std::byte{9}, std::byte{0}, std::byte{0}, std::byte{0}};
  only_record(target).subrecords.push_back({k_full, targetId, false});

  EXPECT_EQ(fo4::copy_record_subrecord(target, targetKey, source, sourceKey, k_full, 0),
            fo4::SubrecordCopyStatus::localized_mismatch);
  const auto after = fulls(target, targetKey);
  ASSERT_EQ(after.size(), 1u);
  EXPECT_EQ(after.front(), targetId);
}

TEST(SubrecordCopy, RefusesAnInlineStringIntoALocalizedPluginWithoutWriting) {
  const std::vector<std::byte> sourceText{std::byte{'A'}, std::byte{'x'}, std::byte{0}};
  auto source = fo4::make_empty_plugin("Source.esp");
  const auto sourceKey = fo4::create_record(source, k_armo, "SourceArmor").key;
  only_record(source).subrecords.push_back({k_full, sourceText, false});

  auto target = fo4::set_plugin_localized_flag(fo4::make_empty_plugin("Target.esp"), true);
  const auto targetKey = fo4::create_record(target, k_armo, "TargetArmor").key;
  const std::vector<std::byte> targetId{std::byte{9}, std::byte{0}, std::byte{0}, std::byte{0}};
  only_record(target).subrecords.push_back({k_full, targetId, false});

  EXPECT_EQ(fo4::copy_record_subrecord(target, targetKey, source, sourceKey, k_full, 0),
            fo4::SubrecordCopyStatus::localized_mismatch);
  const auto after = fulls(target, targetKey);
  ASSERT_EQ(after.size(), 1u);
  EXPECT_EQ(after.front(), targetId);
}

TEST(SubrecordCopy, AllowsAnInlineStringBetweenNonLocalizedPlugins) {
  const std::vector<std::byte> sourceText{std::byte{'A'}, std::byte{'x'}, std::byte{0}};
  auto source = fo4::make_empty_plugin("Source.esp");
  const auto sourceKey = fo4::create_record(source, k_armo, "SourceArmor").key;
  only_record(source).subrecords.push_back({k_full, sourceText, false});

  auto target = fo4::make_empty_plugin("Target.esp");
  const auto targetKey = fo4::create_record(target, k_armo, "TargetArmor").key;
  only_record(target).subrecords.push_back(
      {k_full, {std::byte{'O'}, std::byte{0}}, false});

  ASSERT_EQ(fo4::copy_record_subrecord(target, targetKey, source, sourceKey, k_full, 0),
            fo4::SubrecordCopyStatus::copied);
  const auto after = fulls(target, targetKey);
  ASSERT_EQ(after.size(), 1u);
  EXPECT_EQ(after.front(), sourceText);
}

TEST(SubrecordCopy, RefusesALinkToARecordInAPluginThatIsNotAMasterWithoutWriting) {
  auto source = fo4::remap_plugin_masters(fo4::make_empty_plugin("Source.esp"),
                                          {fo4::PluginKey::from_filename("Fallout4.esm"),
                                           fo4::PluginKey::from_filename("Stranger.esm")});
  const auto sourceKey = fo4::create_record(source, k_armo, "SourceArmor").key;
  only_record(source).subrecords.push_back({k_eitm, {std::byte{0}, std::byte{8}, std::byte{0}, std::byte{1}}, false});

  auto target = fo4::remap_plugin_masters(fo4::make_empty_plugin("Target.esp"),
                                          {fo4::PluginKey::from_filename("Fallout4.esm")});
  const auto targetKey = fo4::create_record(target, k_armo, "TargetArmor").key;
  const std::vector<std::byte> targetLink{std::byte{9}, std::byte{9}, std::byte{9}, std::byte{9}};
  only_record(target).subrecords.push_back({k_eitm, targetLink, false});

  EXPECT_EQ(fo4::copy_record_subrecord(target, targetKey, source, sourceKey, k_eitm, 0),
            fo4::SubrecordCopyStatus::owner_not_master);
  EXPECT_EQ(subrecords(target, targetKey, k_eitm), std::vector<std::vector<std::byte>>{targetLink});
}

TEST(SubrecordCopy, ReencodesALinkToASharedMasterToTheDestinationsOwnMasterIndex) {
  auto source = fo4::remap_plugin_masters(fo4::make_empty_plugin("Source.esp"),
                                          {fo4::PluginKey::from_filename("Fallout4.esm"),
                                           fo4::PluginKey::from_filename("Shared.esm")});
  const auto sourceKey = fo4::create_record(source, k_armo, "SourceArmor").key;
  only_record(source).subrecords.push_back({k_eitm, {std::byte{0}, std::byte{8}, std::byte{0}, std::byte{1}}, false});

  auto target = fo4::remap_plugin_masters(fo4::make_empty_plugin("Target.esp"),
                                          {fo4::PluginKey::from_filename("Shared.esm"),
                                           fo4::PluginKey::from_filename("Fallout4.esm")});
  const auto targetKey = fo4::create_record(target, k_armo, "TargetArmor").key;
  only_record(target).subrecords.push_back({k_eitm, {std::byte{9}, std::byte{9}, std::byte{9}, std::byte{9}}, false});

  ASSERT_EQ(fo4::copy_record_subrecord(target, targetKey, source, sourceKey, k_eitm, 0),
            fo4::SubrecordCopyStatus::copied);
  const std::vector<std::byte> expected{std::byte{0}, std::byte{8}, std::byte{0}, std::byte{0}};
  EXPECT_EQ(subrecords(target, targetKey, k_eitm), std::vector<std::vector<std::byte>>{expected});
}

TEST(SubrecordCopy, ReportsMissingRecordsAndUnreachableOccurrencesWithoutWriting) {
  auto source = fo4::make_empty_plugin("Source.esp");
  const auto sourceKey = fo4::create_record(source, k_armo, "SourceArmor").key;
  only_record(source).subrecords.push_back({k_icon, payload(7), false});
  only_record(source).subrecords.push_back({k_icon, payload(8), false});

  auto target = fo4::make_empty_plugin("Target.esp");
  const auto targetKey = fo4::create_record(target, k_armo, "TargetArmor").key;

  EXPECT_EQ(fo4::copy_record_subrecord(target, fo4::FormKey::parse("000999:Target.esp"), source, sourceKey, k_icon, 0),
            fo4::SubrecordCopyStatus::record_missing);
  EXPECT_EQ(fo4::copy_record_subrecord(target, targetKey, source, fo4::FormKey::parse("000999:Source.esp"), k_icon, 0),
            fo4::SubrecordCopyStatus::source_missing);
  EXPECT_EQ(fo4::copy_record_subrecord(target, targetKey, source, sourceKey, k_icon, 1),
            fo4::SubrecordCopyStatus::occurrence_unreachable);
  EXPECT_TRUE(icons(target, targetKey).empty());
}
