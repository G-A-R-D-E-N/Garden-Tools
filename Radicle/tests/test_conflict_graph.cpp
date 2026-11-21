#include "fo4/graph/conflict_graph.hpp"
#include "fo4/plugin/reader.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <vector>

namespace {

void write_tes4(fo4::test::ByteWriter& out, std::string_view master) {
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
}

void write_name_link(fo4::test::ByteWriter& out, std::uint32_t link_id) {
  fo4::test::ByteWriter link;
  link.u32(link_id);
  fo4::test::write_subrecord(out, "NAME", link.bytes);
}

std::vector<std::byte> make_plugin(
    std::string_view master,
    std::string_view signature,
    std::uint32_t form_id,
    std::optional<std::uint32_t> name_link = std::nullopt,
    std::uint32_t flags = 0) {
  fo4::test::ByteWriter out;
  write_tes4(out, master);
  const auto group = fo4::test::begin_group(out, signature, 0);
  const auto record = fo4::test::begin_record(out, signature, flags, form_id);
  if (name_link.has_value()) {
    write_name_link(out, *name_link);
  }
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

struct LinkRecord {
  std::uint32_t form_id;
  std::vector<std::uint32_t> links;
};

std::vector<std::byte> make_link_plugin(std::string_view master, const std::vector<LinkRecord>& records) {
  fo4::test::ByteWriter out;
  write_tes4(out, master);
  const auto group = fo4::test::begin_group(out, "MISC", 0);
  for (const auto& entry : records) {
    const auto record = fo4::test::begin_record(out, "MISC", 0, entry.form_id);
    for (const auto link : entry.links) {
      write_name_link(out, link);
    }
    fo4::test::end_record(out, record);
  }
  fo4::test::end_group(out, group);
  return out.bytes;
}

using Row = std::tuple<std::string, std::string, std::size_t>;

template <class Hits, class Other>
std::vector<Row> rows_of(const Hits& hits, Other other_of) {
  std::vector<Row> rows;
  for (const auto& hit : hits) {
    EXPECT_EQ(hit.subrecord.to_string(), "NAME");
    EXPECT_EQ(hit.offset, 0u);
    rows.emplace_back(other_of(hit).to_string(), std::string(hit.plugin.filename()), hit.load_order);
  }
  return rows;
}

std::vector<Row> inbound_rows(const fo4::ConflictGraph& graph, std::string_view key) {
  return rows_of(graph.referenced_by(fo4::FormKey::parse(key)), [](const auto& hit) { return hit.source; });
}

std::vector<Row> outbound_rows(const fo4::ConflictGraph& graph, std::string_view key) {
  return rows_of(graph.references_from(fo4::FormKey::parse(key)), [](const auto& hit) { return hit.target; });
}

struct ThreePlugins {
  fo4::Plugin base = fo4::read_plugin(make_plugin({}, "WEAP", 0x00000800), "Base.esp");
  fo4::Plugin zeta = fo4::read_plugin(
      make_link_plugin("Base.esp", {{0x01000801, {0x00000800, 0x01000802, 0x00000800}}, {0x01000802, {0x00000800}}}),
      "Zeta.esp");
  fo4::Plugin mid = fo4::read_plugin(
      make_link_plugin("Base.esp", {{0x01000801, {0x00000800, 0x00000000, 0x00000801}}}), "Mid.esp");
  fo4::Plugin alpha = fo4::read_plugin(make_link_plugin("Base.esp", {{0x01000801, {0x00000800}}}), "Alpha.esp");
  std::array<const fo4::Plugin*, 6> load_order{&base, nullptr, &zeta, nullptr, &mid, &alpha};
};

const std::vector<Row> k_base_inbound{
    {"000801:Zeta.esp", "Zeta.esp", 2},
    {"000801:Zeta.esp", "Zeta.esp", 2},
    {"000802:Zeta.esp", "Zeta.esp", 2},
    {"000801:Mid.esp", "Mid.esp", 4},
    {"000801:Alpha.esp", "Alpha.esp", 5},
};
const std::vector<Row> k_zeta_outbound{
    {"000800:Base.esp", "Zeta.esp", 2},
    {"000802:Zeta.esp", "Zeta.esp", 2},
    {"000800:Base.esp", "Zeta.esp", 2},
};

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

TEST(ConflictGraph, ReferenceScanLeavesUntouchedRecordsUnmaterialized) {
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
  const auto* ref_record = fo4::find_record(ref_plugin, fo4::RawFormId{0x01000801});
  ASSERT_NE(ref_record, nullptr);
  ASSERT_FALSE(ref_record->subrecords.materialized());

  ASSERT_EQ(graph.referenced_by(target).size(), 1u);
  EXPECT_FALSE(ref_record->subrecords.materialized());
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

TEST(ConflictGraph, ReferenceHitsKeepLoadOrderThenEmissionOrder) {
  const ThreePlugins plugins;
  const auto graph = fo4::ConflictGraph::build(plugins.load_order);

  EXPECT_EQ(inbound_rows(graph, "000800:Base.esp"), k_base_inbound);
  EXPECT_EQ(inbound_rows(graph, "000802:Zeta.esp"), (std::vector<Row>{{"000801:Zeta.esp", "Zeta.esp", 2}}));
  EXPECT_EQ(inbound_rows(graph, "000801:Base.esp"), (std::vector<Row>{{"000801:Mid.esp", "Mid.esp", 4}}));
  EXPECT_EQ(outbound_rows(graph, "000801:Zeta.esp"), k_zeta_outbound);
  EXPECT_EQ(outbound_rows(graph, "000801:Mid.esp"),
            (std::vector<Row>{{"000800:Base.esp", "Mid.esp", 4}, {"000801:Base.esp", "Mid.esp", 4}}));
}

TEST(ConflictGraph, ReferenceHitFieldsMatchIndependentExpectation) {
  const ThreePlugins plugins;
  const auto graph = fo4::ConflictGraph::build(plugins.load_order);

  const auto inbound = graph.referenced_by(fo4::FormKey::parse("000800:Base.esp"));
  ASSERT_EQ(inbound.size(), 5u);
  EXPECT_EQ(inbound[3].source, fo4::FormKey::parse("000801:Mid.esp"));
  EXPECT_EQ(inbound[3].plugin, fo4::PluginKey::from_filename("Mid.esp"));
  EXPECT_EQ(inbound[3].subrecord, fo4::sig_name);
  EXPECT_EQ(inbound[3].offset, 0u);
  EXPECT_EQ(inbound[3].load_order, 4u);
  EXPECT_EQ(inbound[4].load_order, 5u);

  const auto outbound = graph.references_from(fo4::FormKey::parse("000801:Zeta.esp"));
  ASSERT_EQ(outbound.size(), 3u);
  EXPECT_EQ(outbound[1].target, fo4::FormKey::parse("000802:Zeta.esp"));
  EXPECT_EQ(outbound[1].plugin, fo4::PluginKey::from_filename("Zeta.esp"));
  EXPECT_EQ(outbound[1].subrecord, fo4::sig_name);
  EXPECT_EQ(outbound[1].offset, 0u);
  EXPECT_EQ(outbound[1].load_order, 2u);
}

TEST(ConflictGraph, ReferenceIndexSkipsLinksWithoutIdentity) {
  const auto base = fo4::read_plugin(make_plugin({}, "WEAP", 0x00000800), "Base.esp");
  const auto nulled = fo4::read_plugin(make_plugin("Base.esp", "MISC", 0x01000801, 0x00000000), "Null.esp");
  const std::array<const fo4::Plugin*, 2> load_order{&base, &nulled};
  const auto graph = fo4::ConflictGraph::build(load_order);

  EXPECT_TRUE(graph.referenced_by(fo4::FormKey{}).empty());
  EXPECT_TRUE(graph.references_from(fo4::FormKey::parse("000801:Null.esp")).empty());
  EXPECT_TRUE(graph.referenced_by(fo4::FormKey::parse("000800:Base.esp")).empty());
}

TEST(ConflictGraph, ReleasedReferenceIndexRebuildsIdenticalHits) {
  const ThreePlugins plugins;
  const auto graph = fo4::ConflictGraph::build(plugins.load_order);

  const auto inbound = inbound_rows(graph, "000800:Base.esp");
  const auto outbound = outbound_rows(graph, "000801:Zeta.esp");
  ASSERT_EQ(inbound, k_base_inbound);
  ASSERT_EQ(outbound, k_zeta_outbound);

  graph.release_references();
  EXPECT_FALSE(graph.references_built());
  graph.release_references();
  EXPECT_FALSE(graph.references_built());

  EXPECT_EQ(outbound_rows(graph, "000801:Zeta.esp"), outbound);
  EXPECT_TRUE(graph.references_built());
  EXPECT_EQ(inbound_rows(graph, "000800:Base.esp"), inbound);
}

TEST(ConflictGraph, UnreferencedKeyHasNoHitsInEitherDirection) {
  const ThreePlugins plugins;
  const auto graph = fo4::ConflictGraph::build(plugins.load_order);

  EXPECT_TRUE(graph.referenced_by(fo4::FormKey::parse("000801:Alpha.esp")).empty());
  EXPECT_TRUE(graph.references_from(fo4::FormKey::parse("000800:Base.esp")).empty());
  EXPECT_TRUE(graph.referenced_by(fo4::FormKey::parse("000FFF:Absent.esp")).empty());
  EXPECT_TRUE(graph.references_from(fo4::FormKey::parse("000FFF:Absent.esp")).empty());
  EXPECT_FALSE(graph.has_references(fo4::FormKey::parse("000801:Alpha.esp")));
  EXPECT_FALSE(graph.has_references(fo4::FormKey::parse("000FFF:Absent.esp")));
  EXPECT_TRUE(graph.has_references(fo4::FormKey::parse("000800:Base.esp")));
}

TEST(ConflictGraph, ConcurrentFirstReferenceQueryBuildsOnce) {
  const ThreePlugins plugins;
  const auto graph = fo4::ConflictGraph::build(plugins.load_order);
  ASSERT_FALSE(graph.references_built());

  constexpr std::size_t thread_count = 4;
  std::array<std::vector<Row>, thread_count> inbound;
  std::array<std::vector<Row>, thread_count> outbound;
  std::atomic<bool> go{false};
  std::vector<std::thread> threads;
  for (std::size_t i = 0; i < thread_count; ++i) {
    threads.emplace_back([&, i] {
      while (!go.load()) std::this_thread::yield();
      inbound[i] = inbound_rows(graph, "000800:Base.esp");
      outbound[i] = outbound_rows(graph, "000801:Zeta.esp");
    });
  }
  go.store(true);
  for (auto& thread : threads) thread.join();

  EXPECT_TRUE(graph.references_built());
  for (std::size_t i = 0; i < thread_count; ++i) {
    EXPECT_EQ(inbound[i], k_base_inbound);
    EXPECT_EQ(outbound[i], k_zeta_outbound);
  }
}
