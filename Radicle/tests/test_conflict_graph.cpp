#include "fo4/graph/conflict_graph.hpp"
#include "fo4/plugin/reader.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace {

std::vector<std::byte> make_plugin(
    std::string_view master,
    std::string_view signature,
    std::uint32_t form_id,
    std::optional<std::uint32_t> name_link = std::nullopt,
    std::uint32_t flags = 0) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);

  if (!master.empty()) {
    std::vector<std::byte> master_name;
    master_name.reserve(master.size() + 1);
    for (const char ch : master) {
      master_name.push_back(std::byte{static_cast<std::uint8_t>(ch)});
    }
    master_name.push_back(std::byte{0});
    fo4::test::write_subrecord(out, "MAST", master_name);

    fo4::test::ByteWriter data;
    data.u32(0);
    data.u32(0);
    fo4::test::write_subrecord(out, "DATA", data.bytes);
  }
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, signature, 0);
  const auto record = fo4::test::begin_record(out, signature, flags, form_id);
  if (name_link.has_value()) {
    fo4::test::ByteWriter link;
    link.u32(*name_link);
    fo4::test::write_subrecord(out, "NAME", link.bytes);
  }
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

}  // namespace

TEST(ConflictGraph, TracksOverrideWinnerByLoadOrder) {
  const auto base = fo4::read_plugin(
      make_plugin({}, "WEAP", 0x00000800),
      "Base.esp");
  const auto override_plugin = fo4::read_plugin(
      make_plugin("Base.esp", "WEAP", 0x00000800),
      "Override.esp");
  const auto ref_plugin = fo4::read_plugin(
      make_plugin("Base.esp", "MISC", 0x01000801, 0x00000800),
      "Reference.esp");

  const std::array<const fo4::Plugin*, 3> load_order{
      &base,
      &override_plugin,
      &ref_plugin,
  };
  const auto graph = fo4::ConflictGraph::build(load_order);
  const auto key = fo4::FormKey::parse("000800:Base.esp");

  const auto& versions = graph.versions(key);
  ASSERT_EQ(versions.size(), 2u);
  EXPECT_EQ(versions[0].plugin.filename(), "Base.esp");
  EXPECT_EQ(versions[1].plugin.filename(), "Override.esp");

  const auto* winner = graph.winner(key);
  ASSERT_NE(winner, nullptr);
  EXPECT_EQ(winner->plugin.filename(), "Override.esp");

  const auto conflicts = graph.conflicts();
  EXPECT_NE(std::find(conflicts.begin(), conflicts.end(), key), conflicts.end());
}

TEST(ConflictGraph, TracksInboundAndOutboundFormLinks) {
  const auto base = fo4::read_plugin(
      make_plugin({}, "WEAP", 0x00000800),
      "Base.esp");
  const auto ref_plugin = fo4::read_plugin(
      make_plugin("Base.esp", "MISC", 0x01000801, 0x00000800),
      "Reference.esp");

  const std::array<const fo4::Plugin*, 2> load_order{
      &base,
      &ref_plugin,
  };
  const auto graph = fo4::ConflictGraph::build(load_order);
  const auto target = fo4::FormKey::parse("000800:Base.esp");
  const auto source = fo4::FormKey::parse("000801:Reference.esp");

  const auto& inbound = graph.referenced_by(target);
  ASSERT_EQ(inbound.size(), 1u);
  EXPECT_EQ(inbound[0].source, source);
  EXPECT_EQ(inbound[0].plugin.filename(), "Reference.esp");
  EXPECT_EQ(inbound[0].subrecord.to_string(), "NAME");

  const auto& outbound = graph.references_from(source);
  ASSERT_EQ(outbound.size(), 1u);
  EXPECT_EQ(outbound[0].target, target);
  EXPECT_EQ(outbound[0].plugin.filename(), "Reference.esp");
  EXPECT_EQ(outbound[0].subrecord.to_string(), "NAME");
}

TEST(ConflictGraph, ReferenceIndexRebuildsWithoutDuplicatesAfterRelease) {
  const auto base = fo4::read_plugin(
      make_plugin({}, "WEAP", 0x00000800),
      "Base.esp");
  const auto ref_plugin = fo4::read_plugin(
      make_plugin("Base.esp", "MISC", 0x01000801, 0x00000800),
      "Reference.esp");

  const std::array<const fo4::Plugin*, 2> load_order{
      &base,
      &ref_plugin,
  };
  const auto graph = fo4::ConflictGraph::build(load_order);
  const auto target = fo4::FormKey::parse("000800:Base.esp");

  const auto built_size = graph.referenced_by(target).size();
  EXPECT_EQ(built_size, 1u);
  EXPECT_TRUE(graph.references_built());

  graph.release_references();
  EXPECT_FALSE(graph.references_built());

  EXPECT_EQ(graph.referenced_by(target).size(), built_size);
  EXPECT_TRUE(graph.references_built());
}

TEST(ConflictGraph, PartialFormOverridesKeepHistoryButDoNotWin) {
  constexpr std::uint32_t partial = 0x4000;
  const auto base = fo4::read_plugin(make_plugin({}, "QUST", 0x800), "Base.esp");
  const auto full = fo4::read_plugin(make_plugin("Base.esp", "QUST", 0x800), "Full.esp");
  const auto part = fo4::read_plugin(make_plugin("Base.esp", "QUST", 0x800, std::nullopt, partial), "Partial.esp");
  const auto key = fo4::FormKey::parse("000800:Base.esp");

  const std::array<const fo4::Plugin*, 3> order{&base, &full, &part};
  const auto graph = fo4::ConflictGraph::build(order);
  ASSERT_EQ(graph.versions(key).size(), 3u);
  EXPECT_EQ(graph.versions(key)[2].plugin.filename(), "Partial.esp");
  ASSERT_NE(graph.winner(key), nullptr);
  EXPECT_EQ(graph.winner(key)->plugin.filename(), "Full.esp");
  const auto conflicts = graph.conflicts();
  EXPECT_NE(std::find(conflicts.begin(), conflicts.end(), key), conflicts.end());

  const std::array<const fo4::Plugin*, 2> only_partial{&base, &part};
  EXPECT_EQ(fo4::ConflictGraph::build(only_partial).winner(key)->plugin.filename(), "Base.esp");

  const auto weap = fo4::read_plugin(make_plugin({}, "WEAP", 0x800), "Base.esp");
  const auto flagged = fo4::read_plugin(make_plugin("Base.esp", "WEAP", 0x800, std::nullopt, partial), "Partial.esp");
  const std::array<const fo4::Plugin*, 2> weapons{&weap, &flagged};
  EXPECT_EQ(fo4::ConflictGraph::build(weapons).winner(key)->plugin.filename(), "Partial.esp");
}
