#include "fo4/forms/dialogue.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/ctda.hpp"
#include "fo4/schema/preflight.hpp"

#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

void write_header(fo4::test::ByteWriter& out,
                  std::initializer_list<std::string_view> masters,
                  std::uint32_t tes4_flags = 0) {
  const auto tes4 = fo4::test::begin_record(out, "TES4", tes4_flags, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  for (const auto master : masters) {
    const std::string name = std::string(master) + '\0';
    fo4::test::write_subrecord(out, "MAST", std::string_view{name});
    fo4::test::ByteWriter data;
    data.u32(0);
    data.u32(0);
    fo4::test::write_subrecord(out, "DATA", data.bytes);
  }
  fo4::test::end_record(out, tes4);
}

std::size_t begin_group_raw(fo4::test::ByteWriter& out, std::uint32_t label, fo4::GroupType type) {
  const auto header = out.size();
  out.sig("GRUP");
  out.u32(0);
  out.u32(label);
  out.i32(static_cast<std::int32_t>(type));
  out.u16(0);
  out.u16(0);
  out.u16(0);
  out.u16(0);
  return header;
}

void end_group_raw(fo4::test::ByteWriter& out, std::size_t header) {
  out.patch_u32(header + 4, static_cast<std::uint32_t>(out.size() - header));
}

void write_node(fo4::test::ByteWriter& out, std::string_view signature, std::uint32_t raw) {
  const auto node = fo4::test::begin_record(out, signature, 0, raw);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Node\0", 5});
  fo4::test::end_record(out, node);
}

void one_topic(fo4::test::ByteWriter& out) {
  write_node(out, "DIAL", 0x00000801u);
}

void topic_pair_and_scene(fo4::test::ByteWriter& out) {
  write_node(out, "DIAL", 0x00000801u);
  const auto topic = begin_group_raw(out, 0x00000801u, fo4::GroupType::topic_children);
  write_node(out, "INFO", 0x00000802u);
  end_group_raw(out, topic);
  write_node(out, "SCEN", 0x00000804u);
}

std::vector<std::byte> quest_plugin(std::initializer_list<std::string_view> masters,
                                    bool with_quest,
                                    const std::function<void(fo4::test::ByteWriter&)>& quest_children = {},
                                    std::uint32_t tes4_flags = 0) {
  fo4::test::ByteWriter out;
  write_header(out, masters, tes4_flags);
  if (!with_quest) return out.bytes;
  const auto quest_top = fo4::test::begin_group(out, "QUST", 0);
  const auto quest = fo4::test::begin_record(out, "QUST", 0, 0x00000800u);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Quest\0", 6});
  fo4::test::end_record(out, quest);
  if (quest_children) {
    const auto children = begin_group_raw(out, 0x00000800u, fo4::GroupType::quest_children);
    quest_children(out);
    end_group_raw(out, children);
  }
  fo4::test::end_group(out, quest_top);
  return out.bytes;
}

std::vector<std::byte> two_quest_plugin() {
  fo4::test::ByteWriter out;
  write_header(out, {});
  for (const auto raw : {0x00000800u, 0x00000801u}) {
    const auto quest_top = fo4::test::begin_group(out, "QUST", 0);
    const auto quest = fo4::test::begin_record(out, "QUST", 0, raw);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"Quest\0", 6});
    fo4::test::end_record(out, quest);
    fo4::test::end_group(out, quest_top);
  }
  return out.bytes;
}

fo4::FormKey quest_key() { return fo4::FormKey::parse("000800:Q.esp"); }

fo4::forms::DialogueBranchSpec branch_spec() {
  return {.editor_id = "RadicleBranch"};
}

fo4::forms::DialogueTopicSpec topic_spec() {
  fo4::forms::DialogueTopicSpec spec;
  spec.editor_id = "RadicleTopic";
  spec.name = "Hello";
  return spec;
}

fo4::forms::DialogueInfoSpec info_spec() {
  fo4::forms::DialogueInfoSpec spec;
  fo4::forms::DialogueResponseSpec response;
  response.text = "Hello";
  spec.responses.push_back(response);
  return spec;
}

const fo4::Group* find_group(const fo4::Plugin& plugin, fo4::GroupType type, std::uint32_t label) {
  const fo4::Group* found = nullptr;
  const auto visit = [&](const auto& self, const fo4::Group& group) -> void {
    if (group.header.group_type == static_cast<std::int32_t>(type) &&
        group.header.label_as_u32() == label) {
      EXPECT_EQ(found, nullptr);
      found = &group;
    }
    for (const auto& child : group.children) {
      if (const auto* nested = std::get_if<fo4::Group>(&child); nested != nullptr) {
        self(self, *nested);
      }
    }
  };
  for (const auto& group : plugin.groups) visit(visit, group);
  return found;
}

std::vector<std::string> top_labels(const fo4::Plugin& plugin) {
  std::vector<std::string> labels;
  for (const auto& group : plugin.groups) labels.push_back(group.header.top_label().to_string());
  return labels;
}

std::vector<std::string> child_tags(const fo4::Group& group) {
  std::vector<std::string> tags;
  for (const auto& child : group.children) {
    if (const auto* record = std::get_if<fo4::Record>(&child); record != nullptr) {
      tags.push_back(record->header.signature.to_string());
    } else {
      tags.push_back("GRUP" + std::to_string(std::get<fo4::Group>(child).header.group_type));
    }
  }
  return tags;
}

fo4::FormKey child_key(const fo4::Group& group, std::size_t index) {
  const auto* record = std::get_if<fo4::Record>(&group.children.at(index));
  return record != nullptr ? record->form_key : fo4::FormKey{};
}

const fo4::Subrecord* find_subrecord(const fo4::Record& record, std::string_view signature) {
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature.to_string() == signature) return &subrecord;
  }
  return nullptr;
}

std::uint32_t read_u32(const fo4::Subrecord& subrecord) {
  std::uint32_t value = 0;
  std::memcpy(&value, subrecord.data.data(), sizeof(value));
  return value;
}

std::optional<std::uint32_t> u32_of(const fo4::Record& record, std::string_view signature) {
  const auto* subrecord = find_subrecord(record, signature);
  if (subrecord == nullptr || subrecord->data.size() < sizeof(std::uint32_t)) return std::nullopt;
  return read_u32(*subrecord);
}

std::string text_of(const fo4::Subrecord& subrecord) {
  std::string text;
  for (const auto byte : subrecord.data) {
    if (byte == std::byte{0}) break;
    text.push_back(static_cast<char>(byte));
  }
  return text;
}

std::vector<std::string> sub_tags(const fo4::Record& record) {
  std::vector<std::string> tags;
  for (const auto& subrecord : record.subrecords) tags.push_back(subrecord.signature.to_string());
  return tags;
}

std::vector<std::pair<std::string, std::vector<std::byte>>> subrecords_of(const fo4::Record& record) {
  std::vector<std::pair<std::string, std::vector<std::byte>>> out;
  for (const auto& subrecord : record.subrecords) {
    out.emplace_back(subrecord.signature.to_string(), subrecord.data);
  }
  return out;
}

void rich_info(fo4::test::ByteWriter& out) {
  const auto info = fo4::test::begin_record(out, "INFO", 0, 0x00000802u);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Rich\0", 5});
  fo4::test::write_subrecord(out, "ENAM", std::string_view{"\x01\x00\x05\x00", 4});
  fo4::test::write_subrecord(out, "TPIC", std::string_view{"\x01\x08\x00\x00", 4});
  fo4::test::write_subrecord(out, "PNAM", std::string_view{"\0\0\0\0", 4});
  fo4::test::write_subrecord(out, "DNAM", std::string_view{"\x07\x00", 2});
  fo4::test::write_subrecord(out, "GNAM", std::string_view{"\x09\x00\x00\x00", 4});
  fo4::test::write_subrecord(out, "IOVR", std::string_view{"\x0a\x00\x00\x00", 4});
  fo4::test::write_subrecord(out, "TRDA", std::string(20, '\0'));
  fo4::test::write_subrecord(out, "NAM1", std::string_view{"Old text\0", 9});
  fo4::test::write_subrecord(out, "NAM2", std::string_view{"\0", 1});
  fo4::test::write_subrecord(out, "SNAM", std::string(4, '\0'));
  fo4::test::write_subrecord(out, "LNAM", std::string(4, '\0'));
  fo4::test::write_subrecord(out, "TNAM", std::string_view{"\x05\x00", 2});
  fo4::test::write_subrecord(out, "SRAF", std::string(4, '\0'));
  fo4::test::write_subrecord(out, "WZMD", std::string_view{});
  const auto old_condition = fo4::serialize_ctda_condition(fo4::CtdaCondition{});
  fo4::test::write_subrecord(out, "CTDA", std::span<const std::byte>{old_condition});
  fo4::test::write_subrecord(out, "NAM0", std::string_view{"\0", 1});
  fo4::test::write_subrecord(out, "INAM", std::string_view{"\x01\x00\x00\x00", 4});
  fo4::test::end_record(out, info);
}

void bare_info(fo4::test::ByteWriter& out) {
  const auto info = fo4::test::begin_record(out, "INFO", 0, 0x00000802u);
  fo4::test::write_subrecord(out, "ENAM", std::string_view{"\x01\x00\x05\x00", 4});
  fo4::test::write_subrecord(out, "PNAM", std::string_view{"\0\0\0\0", 4});
  fo4::test::write_subrecord(out, "RNAM", std::string_view{"\0", 1});
  fo4::test::write_subrecord(out, "NAM0", std::string_view{"\0", 1});
  fo4::test::write_subrecord(out, "INAM", std::string_view{"\x01\x00\x00\x00", 4});
  fo4::test::end_record(out, info);
}

void inam_info(fo4::test::ByteWriter& out) {
  const auto info = fo4::test::begin_record(out, "INFO", 0, 0x00000802u);
  fo4::test::write_subrecord(out, "ENAM", std::string_view{"\x01\x00\x05\x00", 4});
  fo4::test::write_subrecord(out, "PNAM", std::string_view{"\0\0\0\0", 4});
  fo4::test::write_subrecord(out, "INAM", std::string_view{"\x01\x00\x00\x00", 4});
  fo4::test::end_record(out, info);
}

std::function<void(fo4::test::ByteWriter&)> topic_with(void (*info)(fo4::test::ByteWriter&)) {
  return [info](fo4::test::ByteWriter& out) {
    write_node(out, "DIAL", 0x00000801u);
    const auto topic = begin_group_raw(out, 0x00000801u, fo4::GroupType::topic_children);
    info(out);
    end_group_raw(out, topic);
  };
}

}

TEST(DialogueAuthoring, CreatesQuestChildrenGroupRightAfterQuestWhenMissing) {
  auto plugin = fo4::read_plugin(quest_plugin({}, true), "Q.esp");

  const auto result = fo4::forms::create_dialogue_branch(plugin, quest_key(), branch_spec());
  EXPECT_EQ(result.status, fo4::forms::DialogueAuthorStatus::ok);
  EXPECT_EQ(top_labels(plugin), std::vector<std::string>{"QUST"});
  ASSERT_EQ(plugin.groups.size(), 1u);
  EXPECT_EQ(child_tags(plugin.groups[0]), (std::vector<std::string>{"QUST", "GRUP10"}));

  const auto* children = find_group(plugin, fo4::GroupType::quest_children, 0x00000800u);
  ASSERT_NE(children, nullptr);
  EXPECT_EQ(child_tags(*children), std::vector<std::string>{"DLBR"});
  EXPECT_EQ(child_key(*children, 0), result.key);
}

TEST(DialogueAuthoring, PlacesTopicAfterLastTopicGroupAndBeforeScene) {
  auto plugin = fo4::read_plugin(quest_plugin({}, true, topic_pair_and_scene), "Q.esp");

  const auto result = fo4::forms::create_dialogue_topic(plugin, quest_key(), topic_spec());
  EXPECT_EQ(result.status, fo4::forms::DialogueAuthorStatus::ok);
  EXPECT_EQ(top_labels(plugin), std::vector<std::string>{"QUST"});

  const auto* children = find_group(plugin, fo4::GroupType::quest_children, 0x00000800u);
  ASSERT_NE(children, nullptr);
  EXPECT_EQ(child_tags(*children),
            (std::vector<std::string>{"DIAL", "GRUP7", "DIAL", "SCEN"}));
  EXPECT_EQ(child_key(*children, 2), result.key);
  EXPECT_EQ(find_group(plugin, fo4::GroupType::topic_children, result.key.local().value()), nullptr);

  const auto* dial = fo4::find_record(plugin, result.key);
  ASSERT_NE(dial, nullptr);
  EXPECT_EQ(u32_of(*dial, "TIFC"), 0u);
}

TEST(DialogueAuthoring, PlacesBranchBeforeFirstTopic) {
  auto plugin = fo4::read_plugin(quest_plugin({}, true, one_topic), "Q.esp");

  const auto result = fo4::forms::create_dialogue_branch(plugin, quest_key(), branch_spec());
  EXPECT_EQ(result.status, fo4::forms::DialogueAuthorStatus::ok);

  const auto* children = find_group(plugin, fo4::GroupType::quest_children, 0x00000800u);
  ASSERT_NE(children, nullptr);
  EXPECT_EQ(child_tags(*children), (std::vector<std::string>{"DLBR", "DIAL"}));
  EXPECT_EQ(child_key(*children, 0), result.key);
}

TEST(DialogueAuthoring, FirstInfoCreatesAdjacentTopicGroupAndNullPnam) {
  auto plugin = fo4::read_plugin(quest_plugin({}, true), "Q.esp");
  const auto topic = fo4::forms::create_dialogue_topic(plugin, quest_key(), topic_spec());
  ASSERT_EQ(topic.status, fo4::forms::DialogueAuthorStatus::ok);

  const auto info = fo4::forms::create_dialogue_info(plugin, topic.key, info_spec());
  EXPECT_EQ(info.status, fo4::forms::DialogueAuthorStatus::ok);

  const auto* children = find_group(plugin, fo4::GroupType::quest_children, 0x00000800u);
  ASSERT_NE(children, nullptr);
  EXPECT_EQ(child_tags(*children), (std::vector<std::string>{"DIAL", "GRUP7"}));

  const auto* topic_group =
      find_group(plugin, fo4::GroupType::topic_children, topic.key.local().value());
  ASSERT_NE(topic_group, nullptr);
  EXPECT_EQ(child_tags(*topic_group), std::vector<std::string>{"INFO"});
  EXPECT_EQ(child_key(*topic_group, 0), info.key);

  const auto* dial = fo4::find_record(plugin, topic.key);
  ASSERT_NE(dial, nullptr);
  EXPECT_EQ(u32_of(*dial, "TIFC"), 1u);

  const auto* first = fo4::find_record(plugin, info.key);
  ASSERT_NE(first, nullptr);
  EXPECT_EQ(u32_of(*first, "PNAM"), 0u);
}

TEST(DialogueAuthoring, SecondInfoAppendsLastAndChainsPnamToFirst) {
  auto plugin = fo4::read_plugin(quest_plugin({}, true), "Q.esp");
  const auto topic = fo4::forms::create_dialogue_topic(plugin, quest_key(), topic_spec());
  const auto first = fo4::forms::create_dialogue_info(plugin, topic.key, info_spec());
  const auto second = fo4::forms::create_dialogue_info(plugin, topic.key, info_spec());
  EXPECT_EQ(second.status, fo4::forms::DialogueAuthorStatus::ok);

  const auto* topic_group =
      find_group(plugin, fo4::GroupType::topic_children, topic.key.local().value());
  ASSERT_NE(topic_group, nullptr);
  EXPECT_EQ(child_tags(*topic_group), (std::vector<std::string>{"INFO", "INFO"}));
  EXPECT_EQ(child_key(*topic_group, 0), first.key);
  EXPECT_EQ(child_key(*topic_group, 1), second.key);

  const auto* dial = fo4::find_record(plugin, topic.key);
  ASSERT_NE(dial, nullptr);
  EXPECT_EQ(u32_of(*dial, "TIFC"), 2u);

  const auto* later = fo4::find_record(plugin, second.key);
  ASSERT_NE(later, nullptr);
  EXPECT_EQ(u32_of(*later, "PNAM"), first.key.local().value());
}

TEST(DialogueAuthoring, RemovingMiddleInfoRelinksNextPnamAndLowersTifc) {
  auto plugin = fo4::read_plugin(quest_plugin({}, true), "Q.esp");
  const auto topic = fo4::forms::create_dialogue_topic(plugin, quest_key(), topic_spec());
  const auto first = fo4::forms::create_dialogue_info(plugin, topic.key, info_spec());
  const auto middle = fo4::forms::create_dialogue_info(plugin, topic.key, info_spec());
  const auto last = fo4::forms::create_dialogue_info(plugin, topic.key, info_spec());

  EXPECT_EQ(fo4::forms::remove_dialogue_info(plugin, middle.key),
            fo4::forms::DialogueAuthorStatus::ok);
  EXPECT_EQ(fo4::find_record(plugin, middle.key), nullptr);

  const auto* topic_group =
      find_group(plugin, fo4::GroupType::topic_children, topic.key.local().value());
  ASSERT_NE(topic_group, nullptr);
  EXPECT_EQ(child_tags(*topic_group), (std::vector<std::string>{"INFO", "INFO"}));
  EXPECT_EQ(child_key(*topic_group, 0), first.key);
  EXPECT_EQ(child_key(*topic_group, 1), last.key);

  const auto* dial = fo4::find_record(plugin, topic.key);
  ASSERT_NE(dial, nullptr);
  EXPECT_EQ(u32_of(*dial, "TIFC"), 2u);

  const auto* relinked = fo4::find_record(plugin, last.key);
  ASSERT_NE(relinked, nullptr);
  EXPECT_EQ(u32_of(*relinked, "PNAM"), first.key.local().value());
}

TEST(DialogueAuthoring, RemovingFirstInfoNullsNextPnamAndLowersTifc) {
  auto plugin = fo4::read_plugin(quest_plugin({}, true), "Q.esp");
  const auto topic = fo4::forms::create_dialogue_topic(plugin, quest_key(), topic_spec());
  const auto first = fo4::forms::create_dialogue_info(plugin, topic.key, info_spec());
  const auto second = fo4::forms::create_dialogue_info(plugin, topic.key, info_spec());

  EXPECT_EQ(fo4::forms::remove_dialogue_info(plugin, first.key),
            fo4::forms::DialogueAuthorStatus::ok);
  EXPECT_EQ(fo4::find_record(plugin, first.key), nullptr);

  const auto* topic_group =
      find_group(plugin, fo4::GroupType::topic_children, topic.key.local().value());
  ASSERT_NE(topic_group, nullptr);
  EXPECT_EQ(child_tags(*topic_group), std::vector<std::string>{"INFO"});
  EXPECT_EQ(child_key(*topic_group, 0), second.key);

  const auto* dial = fo4::find_record(plugin, topic.key);
  ASSERT_NE(dial, nullptr);
  EXPECT_EQ(u32_of(*dial, "TIFC"), 1u);

  const auto* relinked = fo4::find_record(plugin, second.key);
  ASSERT_NE(relinked, nullptr);
  EXPECT_EQ(u32_of(*relinked, "PNAM"), 0u);
}

TEST(DialogueAuthoring, RemovingLastInfoErasesTopicGroup) {
  auto plugin = fo4::read_plugin(quest_plugin({}, true), "Q.esp");
  const auto topic = fo4::forms::create_dialogue_topic(plugin, quest_key(), topic_spec());
  const auto info = fo4::forms::create_dialogue_info(plugin, topic.key, info_spec());
  ASSERT_NE(find_group(plugin, fo4::GroupType::topic_children, topic.key.local().value()), nullptr);

  EXPECT_EQ(fo4::forms::remove_dialogue_info(plugin, info.key),
            fo4::forms::DialogueAuthorStatus::ok);
  EXPECT_EQ(find_group(plugin, fo4::GroupType::topic_children, topic.key.local().value()), nullptr);

  const auto* children = find_group(plugin, fo4::GroupType::quest_children, 0x00000800u);
  ASSERT_NE(children, nullptr);
  EXPECT_EQ(child_tags(*children), std::vector<std::string>{"DIAL"});
  const auto* dial = fo4::find_record(plugin, topic.key);
  ASSERT_NE(dial, nullptr);
  EXPECT_EQ(u32_of(*dial, "TIFC"), 0u);
}

TEST(DialogueAuthoring, SetBranchStartWritesTopicIntoBranchSnam) {
  auto plugin = fo4::read_plugin(quest_plugin({}, true), "Q.esp");
  const auto branch = fo4::forms::create_dialogue_branch(plugin, quest_key(), branch_spec());
  const auto topic = fo4::forms::create_dialogue_topic(plugin, quest_key(), topic_spec());

  EXPECT_EQ(fo4::forms::set_dialogue_branch_start(plugin, branch.key, topic.key),
            fo4::forms::DialogueAuthorStatus::ok);

  const auto* record = fo4::find_record(plugin, branch.key);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(u32_of(*record, "SNAM"), topic.key.local().value());
}

TEST(DialogueAuthoring, BranchAndTopicMustShareQuest) {
  auto plugin = fo4::read_plugin(two_quest_plugin(), "Q.esp");
  const auto quest_a = fo4::FormKey::parse("000800:Q.esp");
  const auto quest_b = fo4::FormKey::parse("000801:Q.esp");
  const auto branch = fo4::forms::create_dialogue_branch(plugin, quest_a, branch_spec());
  ASSERT_EQ(branch.status, fo4::forms::DialogueAuthorStatus::ok);
  const auto before = fo4::write_plugin(plugin);

  auto cross = topic_spec();
  cross.branch = branch.key;
  EXPECT_EQ(fo4::forms::create_dialogue_topic(plugin, quest_b, cross).status,
            fo4::forms::DialogueAuthorStatus::structure_invalid);
  EXPECT_EQ(fo4::write_plugin(plugin), before);

  EXPECT_EQ(fo4::forms::create_dialogue_topic(plugin, quest_a, cross).status,
            fo4::forms::DialogueAuthorStatus::ok);
  const auto other = fo4::forms::create_dialogue_topic(plugin, quest_b, topic_spec());
  ASSERT_EQ(other.status, fo4::forms::DialogueAuthorStatus::ok);
  const auto before_start = fo4::write_plugin(plugin);
  EXPECT_EQ(fo4::forms::set_dialogue_branch_start(plugin, branch.key, other.key),
            fo4::forms::DialogueAuthorStatus::structure_invalid);
  EXPECT_EQ(fo4::write_plugin(plugin), before_start);
}

TEST(DialogueAuthoring, SetBranchStartRejectsMissingTopicAndWritesNothing) {
  auto plugin = fo4::read_plugin(quest_plugin({}, true), "Q.esp");
  const auto branch = fo4::forms::create_dialogue_branch(plugin, quest_key(), branch_spec());
  const auto before = fo4::write_plugin(plugin);

  EXPECT_EQ(fo4::forms::set_dialogue_branch_start(
                plugin, branch.key, fo4::FormKey::parse("000A00:Q.esp")),
            fo4::forms::DialogueAuthorStatus::topic_missing);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(DialogueAuthoring, MissingQuestReturnsQuestMissingAndWritesNothing) {
  auto plugin = fo4::read_plugin(quest_plugin({}, false), "Q.esp");
  const auto before = fo4::write_plugin(plugin);

  EXPECT_EQ(fo4::forms::create_dialogue_branch(plugin, quest_key(), branch_spec()).status,
            fo4::forms::DialogueAuthorStatus::quest_missing);
  EXPECT_EQ(fo4::forms::create_dialogue_topic(plugin, quest_key(), topic_spec()).status,
            fo4::forms::DialogueAuthorStatus::quest_missing);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(DialogueAuthoring, MasterOwnedQuestIsRefusedAsNotLocal) {
  auto plugin = fo4::read_plugin(quest_plugin({"Base.esm"}, false), "Q.esp");
  const auto before = fo4::write_plugin(plugin);

  EXPECT_EQ(fo4::forms::create_dialogue_branch(
                plugin, fo4::FormKey::parse("000800:Base.esm"), branch_spec())
                .status,
            fo4::forms::DialogueAuthorStatus::not_local);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(DialogueAuthoring, ThreeCharSubtypeIsRefusedAsInvalidArgument) {
  auto plugin = fo4::read_plugin(quest_plugin({}, true), "Q.esp");
  const auto before = fo4::write_plugin(plugin);

  auto spec = topic_spec();
  spec.subtype_name = "GRE";
  EXPECT_EQ(fo4::forms::create_dialogue_topic(plugin, quest_key(), spec).status,
            fo4::forms::DialogueAuthorStatus::invalid_argument);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(DialogueAuthoring, EmbeddedNulInEditorIdIsRefusedAndWritesNothing) {
  auto plugin = fo4::read_plugin(quest_plugin({}, true), "Q.esp");
  const auto before = fo4::write_plugin(plugin);

  auto topic = topic_spec();
  topic.editor_id = std::string("Bad\0Topic", 9);
  EXPECT_EQ(fo4::forms::create_dialogue_topic(plugin, quest_key(), topic).status,
            fo4::forms::DialogueAuthorStatus::invalid_argument);

  auto branch = branch_spec();
  branch.editor_id = std::string("Bad\0Branch", 10);
  EXPECT_EQ(fo4::forms::create_dialogue_branch(plugin, quest_key(), branch).status,
            fo4::forms::DialogueAuthorStatus::invalid_argument);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(DialogueAuthoring, ResponseTextAndConditionSurviveWriteAndRead) {
  auto plugin = fo4::read_plugin(quest_plugin({}, true), "Q.esp");
  const auto topic = fo4::forms::create_dialogue_topic(plugin, quest_key(), topic_spec());

  fo4::forms::DialogueInfoSpec spec;
  fo4::forms::DialogueResponseSpec response;
  response.text = "Hello there";
  spec.responses.push_back(response);
  fo4::CtdaCondition condition;
  condition.function = 72;
  condition.comparison_raw = 0x3f800000u;
  condition.reference = 0x800u;
  spec.conditions.push_back(condition);
  const auto info = fo4::forms::create_dialogue_info(plugin, topic.key, spec);
  ASSERT_EQ(info.status, fo4::forms::DialogueAuthorStatus::ok);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Q.esp");
  const auto* record = fo4::find_record(reread, info.key);
  ASSERT_NE(record, nullptr);
  const auto* text = find_subrecord(*record, "NAM1");
  ASSERT_NE(text, nullptr);
  EXPECT_EQ(text_of(*text), "Hello there");
  const auto* ctda = find_subrecord(*record, "CTDA");
  ASSERT_NE(ctda, nullptr);
  EXPECT_EQ(ctda->data, fo4::serialize_ctda_condition(condition));

  const auto* topic_group =
      find_group(reread, fo4::GroupType::topic_children, topic.key.local().value());
  ASSERT_NE(topic_group, nullptr);
  EXPECT_EQ(child_key(*topic_group, 0), info.key);
}

TEST(DialogueAuthoring, CreatedRecordsPassAuthoringPreflight) {
  auto plugin = fo4::read_plugin(quest_plugin({}, true), "Q.esp");
  const auto branch = fo4::forms::create_dialogue_branch(plugin, quest_key(), branch_spec());
  const auto topic = fo4::forms::create_dialogue_topic(plugin, quest_key(), topic_spec());
  const auto info = fo4::forms::create_dialogue_info(plugin, topic.key, info_spec());
  EXPECT_EQ(branch.status, fo4::forms::DialogueAuthorStatus::ok);
  EXPECT_EQ(topic.status, fo4::forms::DialogueAuthorStatus::ok);
  EXPECT_EQ(info.status, fo4::forms::DialogueAuthorStatus::ok);

  for (const auto& key : std::vector<fo4::FormKey>{branch.key, topic.key, info.key}) {
    for (const auto& issue : fo4::schema::preflight_authoring_record(plugin, key)) {
      if (issue.severity == fo4::schema::PreflightSeverity::error) {
        ADD_FAILURE() << key.to_string() << ' ' << issue.code << ' ' << issue.path;
      }
    }
  }
}

TEST(DialogueAuthoring, UpdateReplacesResponsesConditionsAndEnamAndKeepsTheRestOfTheChain) {
  auto plugin = fo4::read_plugin(quest_plugin({}, true), "Q.esp");
  const auto topic = fo4::forms::create_dialogue_topic(plugin, quest_key(), topic_spec());
  const auto first = fo4::forms::create_dialogue_info(plugin, topic.key, info_spec());
  const auto second = fo4::forms::create_dialogue_info(plugin, topic.key, info_spec());
  ASSERT_EQ(first.status, fo4::forms::DialogueAuthorStatus::ok);
  ASSERT_EQ(second.status, fo4::forms::DialogueAuthorStatus::ok);
  const auto topic_before = subrecords_of(*fo4::find_record(plugin, topic.key));
  const auto second_before = subrecords_of(*fo4::find_record(plugin, second.key));

  fo4::forms::DialogueInfoSpec spec;
  fo4::forms::DialogueResponseSpec response;
  response.text = "Changed";
  response.number = 2;
  spec.responses.push_back(response);
  spec.flags = 0x0200;
  spec.reset_hours = 24;
  fo4::CtdaCondition condition;
  condition.function = 72;
  condition.comparison_raw = 0x3f800000u;
  condition.reference = 0x800u;
  spec.conditions.push_back(condition);
  EXPECT_EQ(fo4::forms::update_dialogue_info(plugin, first.key, spec),
            fo4::forms::DialogueAuthorStatus::ok);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Q.esp");
  const auto* updated = fo4::find_record(reread, first.key);
  ASSERT_NE(updated, nullptr);
  EXPECT_EQ(sub_tags(*updated),
            (std::vector<std::string>{"ENAM", "PNAM", "TRDA", "NAM1", "NAM2", "NAM3", "NAM4",
                                      "CTDA", "NAM0", "INAM"}));
  EXPECT_EQ(u32_of(*updated, "ENAM"), 0x00180200u);
  EXPECT_EQ(u32_of(*updated, "PNAM"), 0u);
  EXPECT_EQ(text_of(*find_subrecord(*updated, "NAM1")), "Changed");
  const auto* trda = find_subrecord(*updated, "TRDA");
  ASSERT_NE(trda, nullptr);
  EXPECT_EQ(trda->data.at(4), std::byte{2});
  EXPECT_EQ(find_subrecord(*updated, "CTDA")->data, fo4::serialize_ctda_condition(condition));

  EXPECT_EQ(subrecords_of(*fo4::find_record(reread, topic.key)), topic_before);
  EXPECT_EQ(subrecords_of(*fo4::find_record(reread, second.key)), second_before);
  const auto* topic_group =
      find_group(reread, fo4::GroupType::topic_children, topic.key.local().value());
  ASSERT_NE(topic_group, nullptr);
  EXPECT_EQ(child_key(*topic_group, 0), first.key);
  EXPECT_EQ(child_key(*topic_group, 1), second.key);
}

TEST(DialogueAuthoring, UpdateKeepsHeadSubrecordsAndPlacesNewResponsesBeforeNam0Inam) {
  auto plugin = fo4::read_plugin(quest_plugin({}, true, topic_with(rich_info)), "Q.esp");
  const auto key = fo4::FormKey::parse("000802:Q.esp");
  const auto before = subrecords_of(*fo4::find_record(plugin, key));

  fo4::forms::DialogueInfoSpec spec;
  for (const auto* text : {"New one", "New two"}) {
    fo4::forms::DialogueResponseSpec response;
    response.text = text;
    spec.responses.push_back(response);
  }
  EXPECT_EQ(fo4::forms::update_dialogue_info(plugin, key, spec), fo4::forms::DialogueAuthorStatus::ok);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Q.esp");
  const auto* updated = fo4::find_record(reread, key);
  ASSERT_NE(updated, nullptr);
  EXPECT_EQ(sub_tags(*updated),
            (std::vector<std::string>{"EDID", "ENAM", "TPIC", "PNAM", "DNAM", "GNAM", "IOVR", "TRDA",
                                      "NAM1", "NAM2", "NAM3", "NAM4", "TRDA", "NAM1", "NAM2", "NAM3",
                                      "NAM4", "NAM0", "INAM"}));
  const auto after = subrecords_of(*updated);
  EXPECT_EQ(after.front(), before.front());
  EXPECT_EQ(std::vector(after.begin() + 2, after.begin() + 7),
            std::vector(before.begin() + 2, before.begin() + 7));
  EXPECT_EQ(text_of(*find_subrecord(*updated, "NAM1")), "New one");
}

TEST(DialogueAuthoring, UpdateRefusesMasterOwnedInfoAndWritesNothing) {
  auto plugin = fo4::read_plugin(quest_plugin({}, true), "Q.esp");
  const auto topic = fo4::forms::create_dialogue_topic(plugin, quest_key(), topic_spec());
  ASSERT_EQ(topic.status, fo4::forms::DialogueAuthorStatus::ok);
  ASSERT_EQ(fo4::forms::create_dialogue_info(plugin, topic.key, info_spec()).status,
            fo4::forms::DialogueAuthorStatus::ok);
  const auto before = fo4::write_plugin(plugin);

  EXPECT_EQ(fo4::forms::update_dialogue_info(
                plugin, fo4::FormKey::parse("000802:Base.esm"), info_spec()),
            fo4::forms::DialogueAuthorStatus::not_local);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(DialogueAuthoring, UpdateOfMissingOrNonInfoKeyIsInfoMissingAndWritesNothing) {
  auto plugin = fo4::read_plugin(quest_plugin({}, true), "Q.esp");
  const auto topic = fo4::forms::create_dialogue_topic(plugin, quest_key(), topic_spec());
  const auto before = fo4::write_plugin(plugin);

  EXPECT_EQ(fo4::forms::update_dialogue_info(plugin, topic.key, info_spec()),
            fo4::forms::DialogueAuthorStatus::info_missing);
  EXPECT_EQ(fo4::forms::update_dialogue_info(
                plugin, fo4::FormKey::parse("000A00:Q.esp"), info_spec()),
            fo4::forms::DialogueAuthorStatus::info_missing);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(DialogueAuthoring, ReplacingResponsesDropsIdleCameraAndWaitSubrecords) {
  auto plugin = fo4::read_plugin(quest_plugin({}, true, topic_with(rich_info)), "Q.esp");
  const auto key = fo4::FormKey::parse("000802:Q.esp");

  fo4::forms::DialogueInfoPatch patch;
  fo4::forms::DialogueResponseSpec response;
  response.text = "New";
  patch.responses = std::vector<fo4::forms::DialogueResponseSpec>{response};
  EXPECT_EQ(fo4::forms::update_dialogue_info(plugin, key, patch),
            fo4::forms::DialogueAuthorStatus::ok);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Q.esp");
  const auto* updated = fo4::find_record(reread, key);
  ASSERT_NE(updated, nullptr);
  EXPECT_EQ(sub_tags(*updated),
            (std::vector<std::string>{"EDID", "ENAM", "TPIC", "PNAM", "DNAM", "GNAM", "IOVR", "TRDA",
                                      "NAM1", "NAM2", "NAM3", "NAM4", "CTDA", "NAM0", "INAM"}));
  EXPECT_EQ(text_of(*find_subrecord(*updated, "NAM1")), "New");
}

TEST(DialogueAuthoring, ConditionsOnlyPatchKeepsResponsesFlagsAndResetHours) {
  auto plugin = fo4::read_plugin(quest_plugin({}, true, topic_with(rich_info)), "Q.esp");
  const auto key = fo4::FormKey::parse("000802:Q.esp");
  auto expected = subrecords_of(*fo4::find_record(plugin, key));

  fo4::CtdaCondition condition;
  condition.function = 72;
  condition.comparison_raw = 0x3f800000u;
  condition.reference = 0x800u;
  fo4::forms::DialogueInfoPatch patch;
  patch.conditions = std::vector<fo4::CtdaCondition>{condition};
  EXPECT_EQ(fo4::forms::update_dialogue_info(plugin, key, patch),
            fo4::forms::DialogueAuthorStatus::ok);

  for (auto& [signature, data] : expected) {
    if (signature == "CTDA") data = fo4::serialize_ctda_condition(condition);
  }
  EXPECT_EQ(subrecords_of(*fo4::find_record(plugin, key)), expected);
}

TEST(DialogueAuthoring, FlagsOnlyPatchRewritesOnlyTheFlagsHalfOfEnam) {
  auto plugin = fo4::read_plugin(quest_plugin({}, true, topic_with(rich_info)), "Q.esp");
  const auto key = fo4::FormKey::parse("000802:Q.esp");
  auto expected = subrecords_of(*fo4::find_record(plugin, key));

  fo4::forms::DialogueInfoPatch patch;
  patch.flags = std::uint16_t{0x0100};
  EXPECT_EQ(fo4::forms::update_dialogue_info(plugin, key, patch),
            fo4::forms::DialogueAuthorStatus::ok);

  for (auto& [signature, data] : expected) {
    if (signature == "ENAM") {
      data = {std::byte{0x00}, std::byte{0x01}, std::byte{0x05}, std::byte{0x00}};
    }
  }
  EXPECT_EQ(subrecords_of(*fo4::find_record(plugin, key)), expected);
}

TEST(DialogueAuthoring, EmptyPatchListsClearTheirRuns) {
  auto plugin = fo4::read_plugin(quest_plugin({}, true, topic_with(rich_info)), "Q.esp");
  const auto key = fo4::FormKey::parse("000802:Q.esp");

  fo4::forms::DialogueInfoPatch patch;
  patch.responses = std::vector<fo4::forms::DialogueResponseSpec>{};
  patch.conditions = std::vector<fo4::CtdaCondition>{};
  EXPECT_EQ(fo4::forms::update_dialogue_info(plugin, key, patch),
            fo4::forms::DialogueAuthorStatus::ok);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Q.esp");
  const auto* updated = fo4::find_record(reread, key);
  ASSERT_NE(updated, nullptr);
  EXPECT_EQ(sub_tags(*updated),
            (std::vector<std::string>{"EDID", "ENAM", "TPIC", "PNAM", "DNAM", "GNAM", "IOVR", "NAM0",
                                      "INAM"}));
  EXPECT_EQ(u32_of(*updated, "ENAM"), 0x00050001u);
}

TEST(DialogueAuthoring, NewRunsGoBeforeTheFirstAfterConditionsSubrecordWhenNoOldRunExists) {
  auto plugin = fo4::read_plugin(quest_plugin({}, true, topic_with(bare_info)), "Q.esp");
  const auto key = fo4::FormKey::parse("000802:Q.esp");

  fo4::forms::DialogueResponseSpec response;
  response.text = "Only";
  fo4::forms::DialogueInfoPatch responses;
  responses.responses = std::vector<fo4::forms::DialogueResponseSpec>{response};
  EXPECT_EQ(fo4::forms::update_dialogue_info(plugin, key, responses),
            fo4::forms::DialogueAuthorStatus::ok);
  EXPECT_EQ(sub_tags(*fo4::find_record(plugin, key)),
            (std::vector<std::string>{"ENAM", "PNAM", "TRDA", "NAM1", "NAM2", "NAM3", "NAM4", "RNAM",
                                      "NAM0", "INAM"}));

  fo4::CtdaCondition condition;
  condition.function = 72;
  condition.comparison_raw = 0x3f800000u;
  condition.reference = 0x800u;
  fo4::forms::DialogueInfoPatch conditions;
  conditions.conditions = std::vector<fo4::CtdaCondition>{condition};
  EXPECT_EQ(fo4::forms::update_dialogue_info(plugin, key, conditions),
            fo4::forms::DialogueAuthorStatus::ok);
  EXPECT_EQ(sub_tags(*fo4::find_record(plugin, key)),
            (std::vector<std::string>{"ENAM", "PNAM", "TRDA", "NAM1", "NAM2", "NAM3", "NAM4", "CTDA",
                                      "RNAM", "NAM0", "INAM"}));
}

TEST(DialogueAuthoring, NewRunsGoBeforeInamWhenInamIsTheOnlyTailSubrecord) {
  auto plugin = fo4::read_plugin(quest_plugin({}, true, topic_with(inam_info)), "Q.esp");
  const auto key = fo4::FormKey::parse("000802:Q.esp");

  fo4::forms::DialogueResponseSpec response;
  response.text = "Only";
  fo4::forms::DialogueInfoPatch responses;
  responses.responses = std::vector<fo4::forms::DialogueResponseSpec>{response};
  EXPECT_EQ(fo4::forms::update_dialogue_info(plugin, key, responses),
            fo4::forms::DialogueAuthorStatus::ok);
  EXPECT_EQ(sub_tags(*fo4::find_record(plugin, key)),
            (std::vector<std::string>{"ENAM", "PNAM", "TRDA", "NAM1", "NAM2", "NAM3", "NAM4", "INAM"}));

  fo4::CtdaCondition condition;
  condition.function = 72;
  condition.comparison_raw = 0x3f800000u;
  condition.reference = 0x800u;
  fo4::forms::DialogueInfoPatch conditions;
  conditions.conditions = std::vector<fo4::CtdaCondition>{condition};
  EXPECT_EQ(fo4::forms::update_dialogue_info(plugin, key, conditions),
            fo4::forms::DialogueAuthorStatus::ok);
  EXPECT_EQ(sub_tags(*fo4::find_record(plugin, key)),
            (std::vector<std::string>{"ENAM", "PNAM", "TRDA", "NAM1", "NAM2", "NAM3", "NAM4", "CTDA",
                                      "INAM"}));
}

TEST(DialogueAuthoring, LocalizedPluginRefusesResponseTextAndWritesNothing) {
  auto plugin = fo4::read_plugin(quest_plugin({}, true, {}, 0x80u), "Q.esp");
  fo4::forms::DialogueTopicSpec topic;
  topic.editor_id = "RadicleTopic";
  const auto dial = fo4::forms::create_dialogue_topic(plugin, quest_key(), topic);
  ASSERT_EQ(dial.status, fo4::forms::DialogueAuthorStatus::ok);
  const auto info =
      fo4::forms::create_dialogue_info(plugin, dial.key, fo4::forms::DialogueInfoSpec{});
  ASSERT_EQ(info.status, fo4::forms::DialogueAuthorStatus::ok);
  const auto before = fo4::write_plugin(plugin);

  EXPECT_EQ(fo4::forms::create_dialogue_info(plugin, dial.key, info_spec()).status,
            fo4::forms::DialogueAuthorStatus::localized_unsupported);
  EXPECT_EQ(fo4::forms::update_dialogue_info(plugin, info.key, info_spec()),
            fo4::forms::DialogueAuthorStatus::localized_unsupported);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}
