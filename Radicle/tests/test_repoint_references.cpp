#include "fo4/graph/conflict_graph.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/form_links.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace {

std::vector<std::byte> make_plugin(std::string_view master,
                                   std::string_view signature,
                                   std::uint32_t form_id,
                                   std::optional<std::uint32_t> name_link = std::nullopt) {
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
  const auto record = fo4::test::begin_record(out, signature, 0, form_id);
  if (name_link.has_value()) {
    fo4::test::ByteWriter link;
    link.u32(*name_link);
    fo4::test::write_subrecord(out, "NAME", link.bytes);
  }
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

const fo4::FormKey k_patch_record = fo4::FormKey::parse("000800:Patch.esp");
const fo4::FormKey k_moved_record = fo4::FormKey::parse("000900:Patch.esp");

fo4::FormKey reference_key() { return fo4::FormKey::parse("000801:Reference.esp"); }

fo4::Plugin make_patch() {
  return fo4::read_plugin(make_plugin({}, "WEAP", 0x000800), "Patch.esp");
}

fo4::Plugin make_reference() {
  return fo4::read_plugin(make_plugin("Patch.esp", "MISC", 0x01000801, 0x00000800),
                          "Reference.esp");
}

std::size_t links_to(const fo4::Plugin& plugin, const fo4::FormKey& key) {
  std::size_t found = 0;
  for (const auto& link : fo4::collect_form_links(plugin)) {
    if (link.key == key) ++found;
  }
  return found;
}

}  // namespace

TEST(ReferenceRepoint, RepointsReferencesToTheRenumberedRecord) {
  auto patch = make_patch();
  auto reference = make_reference();
  ASSERT_EQ(fo4::renumber_local_record(patch, k_patch_record, fo4::LocalFormId{0x900}),
            fo4::RecordRenumberStatus::renumbered);
  const auto before = fo4::write_plugin(reference);

  std::array<fo4::Plugin*, 2> order{&patch, &reference};
  const auto result = fo4::repoint_record_references(order, k_patch_record, k_moved_record);

  EXPECT_EQ(result.status, fo4::ReferenceRepointStatus::repointed);
  EXPECT_EQ(result.references, 1u);
  ASSERT_EQ(result.plugins.size(), 1u);
  EXPECT_EQ(result.plugins[0].filename(), "Reference.esp");

  EXPECT_EQ(links_to(reference, k_patch_record), 0u);
  const auto links = fo4::collect_form_links(reference);
  ASSERT_EQ(links.size(), 1u);
  EXPECT_EQ(links[0].record, reference_key());
  EXPECT_EQ(links[0].key, k_moved_record);
  EXPECT_EQ(links[0].raw.value(), 0x00000900u);
  EXPECT_NE(fo4::write_plugin(reference), before);

  const std::array<const fo4::Plugin*, 2> const_order{&patch, &reference};
  const auto graph = fo4::ConflictGraph::build(const_order);
  ASSERT_EQ(graph.referenced_by(k_moved_record).size(), 1u);
  EXPECT_EQ(graph.referenced_by(k_moved_record)[0].source, reference_key());
  EXPECT_TRUE(graph.referenced_by(k_patch_record).empty());

  const auto reloaded = fo4::read_plugin(fo4::write_plugin(reference), "Reference.esp");
  EXPECT_EQ(links_to(reloaded, k_moved_record), 1u);
  EXPECT_EQ(links_to(reloaded, k_patch_record), 0u);
}

TEST(ReferenceRepoint, LeavesPluginsWithoutReferencesUntouched) {
  auto patch = make_patch();
  auto reference = make_reference();
  ASSERT_EQ(fo4::renumber_local_record(patch, k_patch_record, fo4::LocalFormId{0x900}),
            fo4::RecordRenumberStatus::renumbered);
  const auto before = fo4::write_plugin(reference);

  std::array<fo4::Plugin*, 2> order{&patch, &reference};
  const auto result = fo4::repoint_record_references(
      order, fo4::FormKey::parse("000900:Other.esp"), fo4::FormKey::parse("000901:Other.esp"));

  EXPECT_EQ(result.status, fo4::ReferenceRepointStatus::no_references);
  EXPECT_EQ(result.references, 0u);
  EXPECT_TRUE(result.plugins.empty());
  EXPECT_EQ(fo4::write_plugin(reference), before);
  EXPECT_EQ(links_to(reference, k_patch_record), 1u);
}

TEST(ReferenceRepoint, RefusesKeysOwnedByDifferentPlugins) {
  auto patch = make_patch();
  auto reference = make_reference();
  const auto before = fo4::write_plugin(reference);

  std::array<fo4::Plugin*, 2> order{&patch, &reference};
  const auto result = fo4::repoint_record_references(
      order, k_patch_record, fo4::FormKey::parse("000900:Other.esp"));

  EXPECT_EQ(result.status, fo4::ReferenceRepointStatus::mismatched_owners);
  EXPECT_EQ(result.references, 0u);
  EXPECT_TRUE(result.plugins.empty());
  EXPECT_EQ(fo4::write_plugin(reference), before);
  EXPECT_EQ(links_to(reference, k_patch_record), 1u);
}

TEST(ReferenceRepoint, RepointsEveryReferencingPluginOfTheOrder) {
  auto patch = make_patch();
  auto first = make_reference();
  auto second = fo4::read_plugin(make_plugin("Patch.esp", "MISC", 0x01000802, 0x00000800),
                                 "Second.esp");
  ASSERT_EQ(fo4::renumber_local_record(patch, k_patch_record, fo4::LocalFormId{0x900}),
            fo4::RecordRenumberStatus::renumbered);

  std::array<fo4::Plugin*, 3> order{&patch, &first, &second};
  const auto result = fo4::repoint_record_references(order, k_patch_record, k_moved_record);

  EXPECT_EQ(result.status, fo4::ReferenceRepointStatus::repointed);
  EXPECT_EQ(result.references, 2u);
  ASSERT_EQ(result.plugins.size(), 2u);
  EXPECT_EQ(result.plugins[0].filename(), "Reference.esp");
  EXPECT_EQ(result.plugins[1].filename(), "Second.esp");
  EXPECT_EQ(links_to(first, k_moved_record), 1u);
  EXPECT_EQ(links_to(second, k_moved_record), 1u);
  EXPECT_EQ(links_to(first, k_patch_record), 0u);
  EXPECT_EQ(links_to(second, k_patch_record), 0u);
}
