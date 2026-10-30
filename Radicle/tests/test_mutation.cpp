#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/form_links.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::vector<std::byte> make_plugin(bool with_edid) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x801);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, "MISC", 0);
  const auto record = fo4::test::begin_record(out, "MISC", 0, 0x00000800);
  if (with_edid) {
    fo4::test::write_subrecord(out, "EDID", std::string_view{"OldEditorId\0", 12});
  }
  fo4::test::write_subrecord(out, "FULL", std::string_view{"Preserved\0", 10});
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

std::vector<std::byte> make_source_override(std::string_view editor_id = "SourceOverride") {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x801);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Master.esm\0", 11});
  fo4::test::ByteWriter data;
  data.u32(0);
  data.u32(0);
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, "WEAP", 0);
  const auto record = fo4::test::begin_record(out, "WEAP", 0, 0x00000800);
  std::vector<std::byte> edid;
  for (const char ch : editor_id) edid.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  edid.push_back(std::byte{0});
  fo4::test::write_subrecord(out, "EDID", edid);
  fo4::test::ByteWriter name;
  name.u32(0x00000801);
  fo4::test::write_subrecord(out, "NAME", name.bytes);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

std::vector<std::byte> make_target_plugin() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x811);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, "MISC", 0);
  const auto record = fo4::test::begin_record(out, "MISC", 0, 0x00000810);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"TargetLocal\0", 12});
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

std::vector<std::byte> make_lifecycle_plugin(std::uint32_t next_form_id = 0x900,
                                             bool light = false,
                                             bool with_master = false,
                                             float version = 1.0f) {
  fo4::test::ByteWriter out;
  const auto flags = light ? fo4::RecordFlags::small_file : 0u;
  const auto tes4 = fo4::test::begin_record(out, "TES4", flags, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(version);
  hedr.u32(2);  // one top-level GRUP + one MISC record
  hedr.u32(next_form_id);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  if (with_master) {
    fo4::test::ByteWriter data;
    data.u32(0);
    data.u32(0);
    fo4::test::write_subrecord(out, "MAST", std::string_view{"Fallout4.esm\0", 13});
    fo4::test::write_subrecord(out, "DATA", data.bytes);
  }
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, "MISC", 0);
  const auto record = fo4::test::begin_record(out, "MISC", 0, (with_master ? 0x01000000u : 0u) | 0x800u);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Existing\0", 9});
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

std::vector<std::byte> make_two_record_lifecycle_plugin() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(3);  // one top-level GRUP + two MISC records
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, "MISC", 0);
  for (const auto& [form_id, editor_id] : {
           std::pair<std::uint32_t, std::string_view>{0x00000800, "First"},
           std::pair<std::uint32_t, std::string_view>{0x00000801, "Second"},
       }) {
    const auto record = fo4::test::begin_record(out, "MISC", 0, form_id);
    const std::string value{editor_id};
    fo4::test::write_subrecord(out, "EDID", value + '\0');
    fo4::test::end_record(out, record);
  }
  fo4::test::end_group(out, group);
  return out.bytes;
}

std::vector<std::byte> make_full_light_plugin(std::uint32_t next_form_id) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", fo4::RecordFlags::small_file, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2049u);
  hedr.u32(next_form_id);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, "MISC", 0);
  for (std::uint32_t local = 0x800u; local <= 0xFFFu; ++local) {
    const auto record = fo4::test::begin_record(out, "MISC", 0, local);
    fo4::test::end_record(out, record);
  }
  fo4::test::end_group(out, group);
  return out.bytes;
}

std::string editor_id(const fo4::Record& record) {
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature != fo4::sig_edid) continue;
    const auto* begin = reinterpret_cast<const char*>(subrecord.data.data());
    const auto* end = begin + subrecord.data.size();
    const auto* nul = std::find(begin, end, '\0');
    return std::string(begin, nul);
  }
  return {};
}

}  // namespace

TEST(Mutation, ReplacesExistingEditorIdAndPreservesOtherSubrecords) {
  auto plugin = fo4::read_plugin(make_plugin(true), "Patch.esp");
  const auto key = fo4::FormKey::parse("000800:Patch.esp");

  ASSERT_TRUE(fo4::set_record_editor_id(plugin, key, "NewEditorId"));
  const auto reloaded = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* record = fo4::find_record(reloaded, key);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(editor_id(*record), "NewEditorId");
  ASSERT_EQ(record->subrecords.size(), 2u);
  EXPECT_EQ(record->subrecords[1].signature.to_string(), "FULL");
}

TEST(Mutation, InsertsMissingEditorId) {
  auto plugin = fo4::read_plugin(make_plugin(false), "Patch.esp");
  const auto key = fo4::FormKey::parse("000800:Patch.esp");

  ASSERT_TRUE(fo4::set_record_editor_id(plugin, key, "InsertedId"));
  const auto reloaded = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* record = fo4::find_record(reloaded, key);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(editor_id(*record), "InsertedId");
  ASSERT_EQ(record->subrecords.size(), 2u);
  EXPECT_EQ(record->subrecords[0].signature.to_string(), "EDID");
}

TEST(Mutation, NoOpStillReportsRecordFound) {
  auto plugin = fo4::read_plugin(make_plugin(true), "Patch.esp");
  const auto key = fo4::FormKey::parse("000800:Patch.esp");
  const auto original = fo4::test::original_bytes(plugin);
  ASSERT_TRUE(fo4::set_record_editor_id(plugin, key, "OldEditorId"));
  // A same-value write must not dirty the serialized payload.
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
}

TEST(Mutation, MissingRecordReturnsFalseAndDoesNotDirtyPlugin) {
  auto plugin = fo4::read_plugin(make_plugin(true), "Patch.esp");
  const auto original = fo4::test::original_bytes(plugin);
  EXPECT_FALSE(fo4::set_record_editor_id(
      plugin, fo4::FormKey::parse("000999:Patch.esp"), "Nope"));
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
}

TEST(Mutation, CopyOverrideAddsMasterAndReencodesDestinationSafely) {
  const auto source = fo4::read_plugin(make_source_override(), "Source.esp");
  auto target = fo4::read_plugin(make_target_plugin(), "Target.esp");
  const auto override_key = fo4::FormKey::parse("000800:Master.esm");
  const auto local_key = fo4::FormKey::parse("000810:Target.esp");

  ASSERT_EQ(fo4::copy_record_as_override(target, source, override_key, false),
            fo4::RecordCopyStatus::inserted);
  ASSERT_EQ(target.header.masters.size(), 1u);
  EXPECT_EQ(target.header.masters[0].key.filename(), "Master.esm");

  const auto bytes = fo4::write_plugin(target);
  const auto reloaded = fo4::read_plugin(bytes, "Target.esp");
  ASSERT_EQ(reloaded.header.masters.size(), 1u);
  EXPECT_EQ(reloaded.header.masters[0].key.filename(), "Master.esm");

  const auto* copied = fo4::find_record(reloaded, override_key);
  ASSERT_NE(copied, nullptr);
  EXPECT_EQ(copied->header.form_id, fo4::RawFormId{0x00000800});
  EXPECT_EQ(editor_id(*copied), "SourceOverride");

  const auto* local = fo4::find_record(reloaded, local_key);
  ASSERT_NE(local, nullptr);
  EXPECT_EQ(local->header.form_id, fo4::RawFormId{0x01000810});

  const auto links = fo4::collect_form_links(reloaded);
  const auto copied_link = std::find_if(links.begin(), links.end(), [&](const auto& link) {
    return link.record == override_key && link.subrecord.to_string() == "NAME";
  });
  ASSERT_NE(copied_link, links.end());
  EXPECT_EQ(copied_link->key, fo4::FormKey::parse("000801:Master.esm"));
  EXPECT_EQ(copied_link->raw, fo4::RawFormId{0x00000801});
}

TEST(Mutation, CopyOverrideRequiresExplicitOverwriteForExistingTarget) {
  const auto source = fo4::read_plugin(make_source_override("FirstVersion"), "Source.esp");
  auto target = fo4::read_plugin(make_target_plugin(), "Target.esp");
  const auto key = fo4::FormKey::parse("000800:Master.esm");

  ASSERT_EQ(fo4::copy_record_as_override(target, source, key, false),
            fo4::RecordCopyStatus::inserted);
  const auto before = fo4::write_plugin(target);

  const auto second = fo4::read_plugin(make_source_override("SecondVersion"), "Source.esp");
  EXPECT_EQ(fo4::copy_record_as_override(target, second, key, false),
            fo4::RecordCopyStatus::already_exists);
  EXPECT_EQ(fo4::write_plugin(target), before);

  ASSERT_EQ(fo4::copy_record_as_override(target, second, key, true),
            fo4::RecordCopyStatus::replaced);
  const auto reloaded = fo4::read_plugin(fo4::write_plugin(target), "Target.esp");
  const auto* replaced = fo4::find_record(reloaded, key);
  ASSERT_NE(replaced, nullptr);
  EXPECT_EQ(editor_id(*replaced), "SecondVersion");
}

TEST(Mutation, CreateLocalRecordAllocatesMonotonicallyAndReusesTopGroup) {
  auto plugin = fo4::read_plugin(make_lifecycle_plugin(), "Lifecycle.esp");
  const auto eyes = fo4::Signature::from_string("EYES");

  const auto first = fo4::create_local_record(plugin, eyes);
  const auto second = fo4::create_local_record(plugin, eyes);
  EXPECT_EQ(first, fo4::FormKey::parse("000900:Lifecycle.esp"));
  EXPECT_EQ(second, fo4::FormKey::parse("000901:Lifecycle.esp"));
  EXPECT_EQ(plugin.header.next_form_id, 0x902u);

  std::size_t eyes_groups = 0;
  std::size_t eyes_records = 0;
  for (const auto& group : plugin.groups) {
    if (group.header.group_type != 0 || group.header.top_label() != eyes) continue;
    ++eyes_groups;
    eyes_records += group.children.size();
  }
  EXPECT_EQ(eyes_groups, 1u);
  EXPECT_EQ(eyes_records, 2u);

  const auto reloaded = fo4::read_plugin(fo4::write_plugin(plugin), "Lifecycle.esp");
  EXPECT_EQ(reloaded.header.next_form_id, 0x902u);
  EXPECT_EQ(reloaded.header.record_count, 5u);
  EXPECT_NE(fo4::find_record(reloaded, first), nullptr);
  EXPECT_NE(fo4::find_record(reloaded, second), nullptr);
}

TEST(Mutation, CreateLocalRecordSkipsAStaleCollidingNextFormId) {
  auto plugin = fo4::read_plugin(make_lifecycle_plugin(0x800), "Lifecycle.esp");
  const auto created =
      fo4::create_local_record(plugin, fo4::Signature::from_string("EYES"));
  EXPECT_EQ(created, fo4::FormKey::parse("000801:Lifecycle.esp"));
  EXPECT_EQ(plugin.header.next_form_id, 0x802u);
}

TEST(Mutation, DeleteLocalRecordDoesNotRecycleNextFormId) {
  auto plugin = fo4::read_plugin(make_lifecycle_plugin(), "Lifecycle.esp");
  const auto eyes = fo4::Signature::from_string("EYES");
  const auto first = fo4::create_local_record(plugin, eyes);
  const auto second = fo4::create_local_record(plugin, eyes);

  ASSERT_EQ(fo4::delete_local_record(plugin, first), fo4::RecordDeleteStatus::deleted);
  EXPECT_EQ(plugin.header.next_form_id, 0x902u);
  EXPECT_EQ(fo4::delete_local_record(plugin, fo4::FormKey::parse("000123:Other.esm")),
            fo4::RecordDeleteStatus::not_local);

  const auto reloaded = fo4::read_plugin(fo4::write_plugin(plugin), "Lifecycle.esp");
  EXPECT_EQ(reloaded.header.next_form_id, 0x902u);
  EXPECT_EQ(reloaded.header.record_count, 4u);
  EXPECT_EQ(fo4::find_record(reloaded, first), nullptr);
  EXPECT_NE(fo4::find_record(reloaded, second), nullptr);

  const auto third = fo4::create_local_record(plugin, eyes);
  EXPECT_EQ(third, fo4::FormKey::parse("000902:Lifecycle.esp"));
}

TEST(Mutation, DeleteLocalRecordRebuildsHedrWithoutPriorMutation) {
  auto plugin = fo4::read_plugin(make_two_record_lifecycle_plugin(), "Lifecycle.esp");
  const auto deleted = fo4::FormKey::parse("000800:Lifecycle.esp");

  ASSERT_EQ(fo4::delete_local_record(plugin, deleted), fo4::RecordDeleteStatus::deleted);

  const auto reloaded = fo4::read_plugin(fo4::write_plugin(plugin), "Lifecycle.esp");
  EXPECT_EQ(reloaded.header.record_count, 2u);
  EXPECT_EQ(fo4::find_record(reloaded, deleted), nullptr);
  EXPECT_NE(fo4::find_record(reloaded, fo4::FormKey::parse("000801:Lifecycle.esp")), nullptr);
}

// Light-plugin creation is allowed now that the range is proven from the
// engine rather than guessed. See #26 and docs/ESL_FORMIDS.md.
TEST(Mutation, CreateLocalRecordAllocatesInALightPlugin) {
  auto plugin = fo4::read_plugin(make_lifecycle_plugin(0x900, true), "Lifecycle.esl");
  const auto key = fo4::create_local_record(plugin, fo4::Signature::from_string("EYES"));
  EXPECT_EQ(key, fo4::FormKey::parse("000900:Lifecycle.esl"));
  EXPECT_EQ(plugin.header.next_form_id, 0x901u);
  EXPECT_NE(fo4::find_record(plugin, key), nullptr);
}

TEST(Mutation, LightPluginRangeStartsAtOneAndStopsAtTwelveBits) {
  auto light = fo4::read_plugin(make_lifecycle_plugin(0x900, true, true), "Lifecycle.esl");
  const auto [llo, lhi] = fo4::local_form_id_range(light);
  EXPECT_EQ(llo, 0x001u);
  EXPECT_EQ(lhi, 0xFFFu);

  auto full = fo4::read_plugin(make_lifecycle_plugin(0x900, false), "Lifecycle.esp");
  const auto [flo, fhi] = fo4::local_form_id_range(full);
  EXPECT_EQ(flo, 0x800u);
  EXPECT_EQ(fhi, 0xFFFFFFu);
}

TEST(Mutation, EslExtensionWithoutLightFlagUsesLightRange) {
  const auto extension = fo4::read_plugin(make_lifecycle_plugin(0x900, false, true), "Lifecycle.esl");
  EXPECT_FALSE(extension.header.tes4_flags.is_small_file());
  EXPECT_EQ(fo4::local_form_id_range(extension), (std::pair<std::uint32_t, std::uint32_t>{0x001u, 0xFFFu}));
}

TEST(Mutation, LightPluginKeepsTheFullFloorWithoutHedrOneAndAMaster) {
  const auto masterless = fo4::read_plugin(make_lifecycle_plugin(0x900, true), "Lifecycle.esl");
  EXPECT_FALSE(fo4::allows_hardcoded_range(masterless));
  EXPECT_EQ(fo4::local_form_id_range(masterless), (std::pair<std::uint32_t, std::uint32_t>{0x800u, 0xFFFu}));

  const auto old_header = fo4::read_plugin(make_lifecycle_plugin(0x900, true, true, 0.95f), "Lifecycle.esl");
  EXPECT_FALSE(fo4::allows_hardcoded_range(old_header));
  EXPECT_EQ(fo4::local_form_id_range(old_header), (std::pair<std::uint32_t, std::uint32_t>{0x800u, 0xFFFu}));

  EXPECT_TRUE(fo4::allows_hardcoded_range(fo4::read_plugin(make_lifecycle_plugin(0x900, true, true), "Lifecycle.esl")));

  auto plugin = fo4::read_plugin(make_lifecycle_plugin(0x001, true), "Lifecycle.esl");
  EXPECT_EQ(fo4::create_local_record(plugin, fo4::Signature::from_string("EYES")).local().value(), 0x801u);
}

// A light plugin whose HEDR nextFormID sits below the full-plugin floor must
// allocate from its own floor, not be rejected.
TEST(Mutation, LightPluginAllocatesBelowTheFullPluginFloor) {
  auto plugin = fo4::read_plugin(make_lifecycle_plugin(0x001, true, true), "Lifecycle.esl");
  const auto key = fo4::create_local_record(plugin, fo4::Signature::from_string("EYES"));
  EXPECT_EQ(key.local().value(), 0x001u);
  EXPECT_LT(key.local().value(), 0x800u);
}

// 0x1000 would be masked to 0x000 by the engine, aliasing onto another record,
// so the writer has to refuse it -- the engine never reports the collision.
TEST(Mutation, LightPluginExhaustsAtTwelveBits) {
  auto plugin = fo4::read_plugin(make_lifecycle_plugin(0x1000, true), "Lifecycle.esl");
  const auto original = fo4::test::original_bytes(plugin);
  EXPECT_THROW(
      static_cast<void>(
          fo4::create_local_record(plugin, fo4::Signature::from_string("EYES"))),
      fo4::FormIdError);
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
}

TEST(Mutation, ReserveLocalFormKeyAdvancesHedrWithoutCreatingRecord) {
  auto plugin = fo4::read_plugin(make_lifecycle_plugin(0x900), "Lifecycle.esp");
  const auto before = fo4::collect_records(plugin).size();

  const auto key = fo4::reserve_local_form_key(plugin);
  EXPECT_EQ(key, fo4::FormKey::parse("000900:Lifecycle.esp"));
  EXPECT_EQ(fo4::find_record(plugin, key), nullptr);
  EXPECT_EQ(fo4::collect_records(plugin).size(), before);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Lifecycle.esp");
  EXPECT_EQ(reread.header.next_form_id, 0x901u);
  EXPECT_EQ(fo4::collect_records(reread).size(), before);
}

TEST(Mutation, ReserveLocalFormKeySkipsOccupiedCandidate) {
  auto plugin = fo4::read_plugin(make_lifecycle_plugin(0x800), "Lifecycle.esp");
  const auto key = fo4::reserve_local_form_key(plugin);
  EXPECT_EQ(key, fo4::FormKey::parse("000801:Lifecycle.esp"));
}

TEST(Mutation, ReserveLocalFormKeyScanExhaustsFromNextToCeiling) {
  auto plugin = fo4::read_plugin(make_full_light_plugin(0xFFCu), "Lifecycle.esl");
  try {
    static_cast<void>(fo4::reserve_local_form_key(plugin));
    ADD_FAILURE() << "reserve_local_form_key returned a key on an exhausted range";
  } catch (const fo4::FormIdError& error) {
    EXPECT_NE(std::string{error.what()}.find("no local"), std::string::npos);
  }
}

TEST(Mutation, ReserveLocalFormKeyReportsAFullLightPluginAndAnOverflowedNextSeparately) {
  const auto reserveError = [](std::uint32_t next_form_id) {
    auto plugin = fo4::read_plugin(make_full_light_plugin(next_form_id), "Lifecycle.esl");
    try {
      static_cast<void>(fo4::reserve_local_form_key(plugin));
    } catch (const fo4::FormIdError& error) {
      return std::string{error.what()};
    }
    return std::string{};
  };
  EXPECT_NE(reserveError(0x1000u).find("no local "), std::string::npos);
  EXPECT_NE(reserveError(0x1001u).find("is outside this plugin's local "), std::string::npos);
}

TEST(Mutation, LightPluginRefusesFfPrefixedHedrNextAboveTheLightCeiling) {
  auto plugin = fo4::read_plugin(make_lifecycle_plugin(0xFF002F1Au, true, true), "Lifecycle.esl");
  const auto original = fo4::test::original_bytes(plugin);
  try {
    static_cast<void>(fo4::create_local_record(plugin, fo4::Signature::from_string("EYES")));
    ADD_FAILURE() << "create_local_record allocated under a 0xFF-prefixed HEDR next";
  } catch (const fo4::FormIdError& error) {
    EXPECT_NE(std::string{error.what()}.find("is outside this plugin's local"), std::string::npos);
  }
  EXPECT_EQ(plugin.header.next_form_id, 0xFF002F1Au);
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
}

TEST(Mutation, FullPluginMasksFfPrefixedHedrNext) {
  auto plugin = fo4::read_plugin(make_lifecycle_plugin(0xFF002F1Au, false), "Lifecycle.esp");
  const auto key = fo4::create_local_record(plugin, fo4::Signature::from_string("EYES"));
  EXPECT_EQ(key.local().value(), 0x2F1Au);
  EXPECT_EQ(plugin.header.next_form_id, 0x2F1Bu);
}

TEST(Mutation, FullPluginClampsMaskedHedrNextBelowTheFloor) {
  auto plugin = fo4::read_plugin(make_lifecycle_plugin(0xFF000001u, false), "Lifecycle.esp");
  const auto key = fo4::reserve_local_form_key(plugin);
  EXPECT_EQ(key.local().value(), 0x801u);
  EXPECT_EQ(plugin.header.next_form_id, 0x802u);
}

TEST(Mutation, LightPluginMasksFfPrefixedHedrNextInRange) {
  auto plugin = fo4::read_plugin(make_lifecycle_plugin(0xFF000ABCu, true, true), "Lifecycle.esl");
  const auto key = fo4::reserve_local_form_key(plugin);
  EXPECT_EQ(key.local().value(), 0xABCu);
  EXPECT_EQ(plugin.header.next_form_id, 0xABDu);
}

TEST(Mutation, LoweredLightNextIgnoresTheFfPrefix) {
  auto plugin = fo4::read_plugin(make_lifecycle_plugin(0xFF000ABCu, true, true), "Lifecycle.esl");
  EXPECT_FALSE(fo4::lowered_light_next_form_id(plugin).has_value());
}
