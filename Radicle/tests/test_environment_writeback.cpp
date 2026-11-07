#include "fo4/c_api_env.h"
#include "fo4/environment/placed.hpp"
#include "fo4/forms/placed_reference.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Sub {
  std::string signature;
  std::vector<std::byte> data;
};

struct Node {
  bool group{false};
  std::string signature;
  std::uint32_t form_id{0};
  std::vector<Sub> subrecords;
  std::uint32_t label{0};
  std::int32_t group_type{0};
  std::vector<Node> children;
};

Node rec(std::string signature, std::uint32_t form_id, std::vector<Sub> subrecords = {}) {
  Node node;
  node.signature = std::move(signature);
  node.form_id = form_id;
  node.subrecords = std::move(subrecords);
  return node;
}

Node grp(std::uint32_t label, std::int32_t type, std::vector<Node> children) {
  Node node;
  node.group = true;
  node.label = label;
  node.group_type = type;
  node.children = std::move(children);
  return node;
}

Node top(std::string_view signature, std::vector<Node> children) {
  std::uint32_t label = 0;
  std::memcpy(&label, signature.data(), 4);
  return grp(label, 0, std::move(children));
}

std::vector<std::byte> words(std::initializer_list<std::uint32_t> values) {
  fo4::test::ByteWriter out;
  for (const auto value : values) out.u32(value);
  return out.bytes;
}

std::vector<std::byte> floats(std::initializer_list<float> values) {
  fo4::test::ByteWriter out;
  for (const auto value : values) out.f32(value);
  return out.bytes;
}

std::vector<std::byte> id_then_float(std::uint32_t id, float value) {
  fo4::test::ByteWriter out;
  out.u32(id);
  out.f32(value);
  return out.bytes;
}

void write_node(fo4::test::ByteWriter& out, const Node& node) {
  if (node.group) {
    std::string label(4, '\0');
    std::memcpy(label.data(), &node.label, 4);
    const auto header = fo4::test::begin_group(out, label, node.group_type);
    for (const auto& child : node.children) write_node(out, child);
    fo4::test::end_group(out, header);
    return;
  }
  const auto header = fo4::test::begin_record(out, node.signature, 0, node.form_id);
  for (const auto& sub : node.subrecords) fo4::test::write_subrecord(out, sub.signature, sub.data);
  fo4::test::end_record(out, header);
}

std::vector<std::byte> make_plugin(std::uint32_t flags, const std::vector<std::string>& masters,
                                   const std::vector<Node>& groups) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", flags, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  for (const auto& master : masters) {
    std::vector<std::byte> name;
    for (const char ch : master) name.push_back(std::byte{static_cast<std::uint8_t>(ch)});
    name.push_back(std::byte{0});
    fo4::test::write_subrecord(out, "MAST", name);
    fo4::test::write_subrecord(out, "DATA", words({0, 0}));
  }
  fo4::test::end_record(out, tes4);
  for (const auto& group : groups) write_node(out, group);
  return out.bytes;
}

// A reference carrying every form ID shape the remap must rewrite: plain, arrays, pairs and struct slots.
Node rich_ref(std::uint32_t id, std::uint32_t base, std::uint32_t sibling, std::uint32_t lcrt, std::uint32_t keyword) {
  return rec("REFR", id,
             {{"NAME", words({base})},
              {"XLRT", words({lcrt, lcrt})},
              {"XPOD", words({id, sibling})},
              {"XORD", words({sibling})},
              {"XPWR", words({sibling, 1})},
              {"XESP", words({sibling, 1})},
              {"XLKR", words({keyword, sibling})},
              {"XAPD", {std::byte{1}}},
              {"XAPR", id_then_float(sibling, 0.5f)},
              {"XPLK", words({sibling, 0})},
              {"XWPG", words({1})},
              {"XWPN", words({id, sibling, 0})},
              {"XSCL", floats({1.5f})},
              {"DATA", floats({1, 2, 3, 0.1f, 0.2f, 0.3f})}});
}

std::vector<std::byte> make_master() {
  return make_plugin(0x1, {},
                     {top("STAT", {rec("STAT", 0x810)}),
                      top("LCRT", {rec("LCRT", 0x811)}),
                      top("KYWD", {rec("KYWD", 0x812)}),
                      top("CELL", {grp(0, 2, {grp(0, 3, {rec("CELL", 0x820, {{"DATA", {std::byte{1}}}}),
                                                          grp(0x820, 6, {grp(0x820, 8, {rec("REFR", 0x821,
                                                                                           {{"NAME", words({0x810})},
                                                                                            {"DATA", floats({5, 5, 5, 0, 0, 0})}})})})})})}),
                      top("WRLD", {rec("WRLD", 0x800),
                                   grp(0x800, 1, {grp(0, 4, {grp(0, 5, {rec("CELL", 0x804, {{"XCLC", words({1, 0, 0})}}), grp(0x804, 6, {grp(0x804, 9, {rec("REFR", 0x805, {{"NAME", words({0x810})}, {"DATA", floats({0, 0, 0, 0, 0, 0})}})})}), rec("CELL", 0x801, {{"XCLC", words({0, 0, 0})}}),
                                                                         grp(0x801, 6, {grp(0x801, 9, {
                                                                                            rich_ref(0x802, 0x810, 0x803, 0x811, 0x812),
                                                                                            rec("REFR", 0x803, {{"NAME", words({0x810})},
                                                                                                                {"DATA", floats({0, 0, 0, 0, 0, 0})}})})})})})})})});
}

std::vector<std::byte> make_other() {
  return make_plugin(0x1, {},
                     {top("STAT", {rec("STAT", 0x860)}),
                      top("CELL", {grp(0, 2, {grp(0, 3, {rec("CELL", 0x850, {{"DATA", {std::byte{1}}}}),
                                                          grp(0x850, 6, {grp(0x850, 9, {rec("REFR", 0x851,
                                                                                           {{"NAME", words({0x860})},
                                                                                            {"DATA", floats({0, 0, 0, 0, 0, 0})}})})})})})})});
}

std::vector<std::byte> make_dlc() {
  return make_plugin(0x1, {"Master.esm"},
                     {top("CELL", {grp(0, 2, {grp(0, 3, {rec("CELL", 0x01000840, {{"DATA", {std::byte{1}}}}),
                                                          grp(0x01000840, 6, {grp(0x01000840, 9, {
                                                                                  rich_ref(0x01000841, 0x810, 0x01000842, 0x811, 0x812),
                                                                                  rec("REFR", 0x01000842, {{"NAME", words({0x810})},
                                                                                                           {"DATA", floats({0, 0, 0, 0, 0, 0})}})})})})})})});
}

class WritebackTest : public ::testing::Test {
protected:
  void SetUp() override {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    root_ = std::filesystem::temp_directory_path() / ("radicle_writeback_" + std::to_string(id));
    std::filesystem::create_directories(root_);
    paths_ = {write("Master.esm", make_master()), write("Other.esm", make_other()), write("DLC.esm", make_dlc())};
  }

  void TearDown() override {
    std::error_code ec;
    std::filesystem::remove_all(root_, ec);
  }

  std::filesystem::path write(std::string_view name, const std::vector<std::byte>& bytes) {
    const auto path = root_ / std::string(name);
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return path;
  }

  std::filesystem::path root_;
  std::vector<std::filesystem::path> paths_;
};

fo4::FormKey key(std::string_view plugin, std::uint32_t local) {
  return fo4::FormKey{fo4::PluginKey::from_filename(plugin), fo4::LocalFormId{local}};
}

// Every decoded FormLink and value except the edited placement must survive the copy unchanged.
void expect_same_links(const fo4::env::PlacedReferenceData& a, const fo4::env::PlacedReferenceData& b) {
  EXPECT_EQ(a.base, b.base);
  EXPECT_EQ(a.location_ref_types, b.location_ref_types);
  ASSERT_EQ(a.portals.size(), b.portals.size());
  for (std::size_t i = 0; i < a.portals.size(); ++i) {
    EXPECT_EQ(a.portals[i].origin, b.portals[i].origin);
    EXPECT_EQ(a.portals[i].destination, b.portals[i].destination);
  }
  ASSERT_EQ(a.linked_references.size(), b.linked_references.size());
  for (std::size_t i = 0; i < a.linked_references.size(); ++i) {
    EXPECT_EQ(a.linked_references[i].keyword, b.linked_references[i].keyword);
    EXPECT_EQ(a.linked_references[i].reference, b.linked_references[i].reference);
  }
  ASSERT_EQ(a.activate_parents.size(), b.activate_parents.size());
  for (std::size_t i = 0; i < a.activate_parents.size(); ++i) {
    EXPECT_EQ(a.activate_parents[i].reference, b.activate_parents[i].reference);
    EXPECT_EQ(a.activate_parents[i].delay, b.activate_parents[i].delay);
  }
  ASSERT_EQ(a.enable_parent.has_value(), b.enable_parent.has_value());
  if (a.enable_parent) {
    EXPECT_EQ(a.enable_parent->reference, b.enable_parent->reference);
  }
}

std::uint32_t raw_u32(const fo4::Record& record, std::string_view signature, std::size_t offset) {
  for (const auto& sub : record.subrecords) {
    if (sub.signature != fo4::Signature::from_string(signature)) continue;
    std::uint32_t value = 0;
    std::memcpy(&value, sub.data.data() + offset, 4);
    return value;
  }
  ADD_FAILURE() << "missing " << signature;
  return 0;
}

radicle_env* load_env(const std::vector<std::filesystem::path>& paths) {
  std::vector<std::string> utf8;
  for (const auto& path : paths) utf8.push_back(path.string());
  std::vector<const char*> raw;
  for (const auto& path : utf8) raw.push_back(path.c_str());
  radicle_env* env = nullptr;
  return radicle_env_load(raw.data(), raw.size(), &env) == RADICLE_STATUS_OK ? env : nullptr;
}

void copy_master_reference(radicle_plugin* patch, const radicle_env* env, std::uint32_t local) {
  const radicle_record* record = nullptr;
  ASSERT_EQ(radicle_env_winner(env, {"Master.esm", local}, &record), RADICLE_STATUS_OK);
  radicle_placed_copy_status status{};
  ASSERT_EQ(radicle_plugin_copy_placed_ref_override(patch, env, record, 0, &status), RADICLE_STATUS_OK);
}

}  // namespace

TEST(PlacedReferenceRemap, RewritesArrayAndStructFormIdsWhenMastersMove) {
  const auto source = fo4::read_plugin(make_dlc(), "DLC.esm");
  auto moved = fo4::remap_plugin_masters(source, std::vector<fo4::PluginKey>{
                                                     fo4::PluginKey::from_filename("Other.esm"),
                                                     fo4::PluginKey::from_filename("Master.esm")});
  const auto* record = fo4::find_record(moved, key("DLC.esm", 0x841));
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(raw_u32(*record, "NAME", 0), 0x01000810u);
  EXPECT_EQ(raw_u32(*record, "XLRT", 4), 0x01000811u);
  EXPECT_EQ(raw_u32(*record, "XPOD", 4), 0x02000842u);
  EXPECT_EQ(raw_u32(*record, "XORD", 0), 0x02000842u);
  EXPECT_EQ(raw_u32(*record, "XPWR", 0), 0x02000842u);
  EXPECT_EQ(raw_u32(*record, "XPWR", 4), 1u);
  EXPECT_EQ(raw_u32(*record, "XAPR", 0), 0x02000842u);
  EXPECT_EQ(raw_u32(*record, "XPLK", 0), 0x02000842u);
  EXPECT_EQ(raw_u32(*record, "XWPN", 4), 0x02000842u);
  EXPECT_EQ(raw_u32(*record, "XWPN", 8), 0u);
  EXPECT_EQ(raw_u32(*record, "XLKR", 0), 0x01000812u);

  auto malformed = fo4::read_plugin(make_dlc(), "DLC.esm");
  fo4::visit_records(malformed, [](fo4::Record& r) {
    for (auto& sub : r.subrecords) {
      if (sub.signature == fo4::Signature::from_string("XAPR")) sub.data.resize(6);
    }
  });
  EXPECT_THROW(fo4::remap_plugin_masters(malformed, std::vector<fo4::PluginKey>{
                                                        fo4::PluginKey::from_filename("Other.esm"),
                                                        fo4::PluginKey::from_filename("Master.esm")}),
               fo4::FormIdError);
}

TEST(PlacedReferenceRemap, AcceptsReferenceOnlySplineAndReflectionLinks) {
  auto source = fo4::read_plugin(make_dlc(), "DLC.esm");
  fo4::visit_records(source, [](fo4::Record& r) {
    for (auto& sub : r.subrecords) {
      if (sub.signature == fo4::Signature::from_string("XPLK") || sub.signature == fo4::Signature::from_string("XPWR")) {
        sub.data.resize(4);
      }
    }
  });
  const auto moved = fo4::remap_plugin_masters(source, std::vector<fo4::PluginKey>{
                                                           fo4::PluginKey::from_filename("Other.esm"),
                                                           fo4::PluginKey::from_filename("Master.esm")});
  const auto* record = fo4::find_record(moved, key("DLC.esm", 0x841));
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(raw_u32(*record, "XPLK", 0), 0x02000842u);
  EXPECT_EQ(raw_u32(*record, "XPWR", 0), 0x02000842u);
}

TEST(PlacedReferenceRemap, LinksPastTheMasterTableStayWithTheOwningPlugin) {
  auto source = fo4::read_plugin(make_dlc(), "DLC.esm");
  fo4::visit_records(source, [](fo4::Record& r) {
    for (auto& sub : r.subrecords) {
      if (sub.signature == fo4::Signature::from_string("NAME")) {
        const std::uint32_t past_table = 0x05000810u;
        std::memcpy(sub.data.data(), &past_table, 4);
      }
    }
  });
  const auto moved = fo4::remap_plugin_masters(source, std::vector<fo4::PluginKey>{
                                                           fo4::PluginKey::from_filename("Other.esm"),
                                                           fo4::PluginKey::from_filename("Master.esm")});
  const auto* record = fo4::find_record(moved, key("DLC.esm", 0x841));
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(raw_u32(*record, "NAME", 0), 0x02000810u);
}

TEST(PlacedReferenceRemap, RewritesTheOwnershipOwner) {
  auto source = fo4::read_plugin(make_dlc(), "DLC.esm");
  fo4::visit_records(source, [](fo4::Record& r) {
    if (r.header.signature != fo4::Signature::from_string("REFR")) return;
    fo4::Subrecord xown;
    xown.signature = fo4::Signature::from_string("XOWN");
    xown.data.resize(12);
    const std::uint32_t owner = 0x01000842u;
    std::memcpy(xown.data.data(), &owner, 4);
    r.subrecords.insert(r.subrecords.end() - 1, std::move(xown));
  });
  const auto moved = fo4::remap_plugin_masters(source, std::vector<fo4::PluginKey>{
                                                           fo4::PluginKey::from_filename("Other.esm"),
                                                           fo4::PluginKey::from_filename("Master.esm")});
  const auto* record = fo4::find_record(moved, key("DLC.esm", 0x841));
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(raw_u32(*record, "XOWN", 0), 0x02000842u);
}

TEST_F(WritebackTest, OverridesEditsSavesAndReloadsWithParity) {
  const auto env = fo4::env::Environment::load(paths_);
  auto patch = fo4::make_empty_plugin("Patch.esp");
  EXPECT_EQ(patch.header.next_form_id, 0x800u);
  EXPECT_FALSE(patch.header.tes4_flags.is_master());

  const auto dlc_ref = key("DLC.esm", 0x841);
  const auto master_ref = key("Master.esm", 0x802);
  ASSERT_EQ(fo4::forms::copy_placed_reference_as_override(patch, env.plugin(2), dlc_ref),
            fo4::forms::PlacedReferenceCopyStatus::inserted);
  ASSERT_EQ(fo4::forms::copy_placed_reference_as_override(patch, env.plugin(1), key("Other.esm", 0x851)),
            fo4::forms::PlacedReferenceCopyStatus::inserted);
  ASSERT_EQ(fo4::forms::copy_placed_reference_as_override(patch, env.plugin(0), master_ref),
            fo4::forms::PlacedReferenceCopyStatus::inserted);
  EXPECT_EQ(fo4::forms::copy_placed_reference_as_override(patch, env.plugin(0), master_ref),
            fo4::forms::PlacedReferenceCopyStatus::already_exists);
  ASSERT_EQ(fo4::forms::copy_placed_reference_as_override(patch, env.plugin(0), key("Master.esm", 0x821)),
            fo4::forms::PlacedReferenceCopyStatus::inserted);
  const auto created = fo4::forms::copy_placed_reference_as_new(patch, env.plugin(0), key("Master.esm", 0x821));
  ASSERT_EQ(created.status, fo4::forms::PlacedReferenceCopyNewStatus::created);
  EXPECT_EQ(created.new_reference, key("Patch.esp", 0x800));

  ASSERT_TRUE(fo4::forms::set_placed_reference_transform(patch, master_ref, {10, 20, 30, 1, 2, 3}));
  ASSERT_TRUE(fo4::forms::set_placed_reference_scale(patch, master_ref, 2.0f));
  ASSERT_TRUE(fo4::forms::set_placed_reference_scale(patch, dlc_ref, std::nullopt));
  ASSERT_TRUE(fo4::forms::set_placed_reference_scale(patch, key("Master.esm", 0x821), 0.5f));
  EXPECT_FALSE(fo4::forms::set_placed_reference_scale(patch, key("Master.esm", 0x803), 1.0f));

  std::vector<fo4::PluginKey> order;
  for (const auto& info : env.plugins()) order.push_back(info.key);
  patch = fo4::sort_plugin_masters(std::move(patch), order);
  ASSERT_EQ(patch.header.masters.size(), 3u);
  EXPECT_EQ(patch.header.masters[0].key, fo4::PluginKey::from_filename("Master.esm"));
  EXPECT_EQ(patch.header.masters[1].key, fo4::PluginKey::from_filename("Other.esm"));
  EXPECT_EQ(patch.header.masters[2].key, fo4::PluginKey::from_filename("DLC.esm"));
  EXPECT_THROW(fo4::sort_plugin_masters(patch, std::span<const fo4::PluginKey>(order).first(1)), fo4::FormIdError);

  const auto patch_path = root_ / "Patch.esp";
  fo4::write_plugin_file(patch, patch_path);
  const auto reread = fo4::read_plugin_file(patch_path);
  EXPECT_EQ(fo4::write_plugin(reread), fo4::write_plugin(patch));

  auto with_patch = paths_;
  with_patch.push_back(patch_path);
  const auto reloaded = fo4::env::Environment::load(with_patch);
  ASSERT_EQ(reloaded.versions(dlc_ref).size(), 2u);

  const auto dlc_source = fo4::env::decode_placed_reference(env, *env.winner(dlc_ref));
  const auto dlc_patched = fo4::env::decode_placed_reference(reloaded, *reloaded.winner(dlc_ref));
  EXPECT_EQ(reloaded.winner(dlc_ref)->plugin, 3u);
  expect_same_links(dlc_source, dlc_patched);
  EXPECT_EQ(dlc_patched.activate_parents.at(0).reference, key("DLC.esm", 0x842));
  EXPECT_EQ(dlc_patched.position, dlc_source.position);
  EXPECT_FALSE(dlc_patched.scale.has_value());
  EXPECT_EQ(raw_u32(*reloaded.winner(dlc_ref)->record, "XAPR", 0), 0x02000842u);

  const auto master_patched = fo4::env::decode_placed_reference(reloaded, *reloaded.winner(master_ref));
  expect_same_links(fo4::env::decode_placed_reference(env, *env.winner(master_ref)), master_patched);
  EXPECT_EQ(master_patched.position, (fo4::env::Vec3{10, 20, 30}));
  EXPECT_EQ(master_patched.rotation, (fo4::env::Vec3{1, 2, 3}));
  EXPECT_EQ(master_patched.scale, std::optional<float>{2.0f});

  const auto children = reloaded.cell_children(key("Master.esm", 0x801));
  ASSERT_EQ(children.size(), 2u);
  EXPECT_EQ(children[0].ref->plugin, 3u);
  const auto interior = reloaded.cell_children(key("Master.esm", 0x820));
  ASSERT_EQ(interior.size(), 2u);
  EXPECT_EQ(interior[1].ref->record->form_key, key("Patch.esp", 0x800));
  EXPECT_TRUE(interior[1].persistent);
  EXPECT_EQ(fo4::env::decode_placed_reference(reloaded, *reloaded.winner(key("Master.esm", 0x821))).scale,
            std::optional<float>{0.5f});
}

TEST_F(WritebackTest, CApiWritesOverridesThatReloadIntoTheEnvironment) {
  std::vector<std::string> utf8;
  for (const auto& path : paths_) utf8.push_back(path.string());
  std::vector<const char*> raw;
  for (const auto& path : utf8) raw.push_back(path.c_str());
  radicle_env* env = nullptr;
  ASSERT_EQ(radicle_env_load(raw.data(), raw.size(), &env), RADICLE_STATUS_OK);

  radicle_plugin* patch = nullptr;
  EXPECT_EQ(radicle_plugin_create("", &patch), RADICLE_STATUS_INVALID_ARGUMENT);
  ASSERT_EQ(radicle_plugin_create("Patch.esp", &patch), RADICLE_STATUS_OK);

  const radicle_record* dlc_ref = nullptr;
  ASSERT_EQ(radicle_env_winner(env, {"DLC.esm", 0x841}, &dlc_ref), RADICLE_STATUS_OK);
  const radicle_record* master_ref = nullptr;
  ASSERT_EQ(radicle_env_winner(env, {"Master.esm", 0x802}, &master_ref), RADICLE_STATUS_OK);
  const radicle_record* stat = nullptr;
  ASSERT_EQ(radicle_env_winner(env, {"Master.esm", 0x810}, &stat), RADICLE_STATUS_OK);

  radicle_placed_copy_status status{};
  ASSERT_EQ(radicle_plugin_copy_placed_ref_override(patch, env, dlc_ref, 0, &status), RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_PLACED_COPY_INSERTED);
  ASSERT_EQ(radicle_plugin_copy_placed_ref_override(patch, env, master_ref, 0, &status), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_plugin_copy_placed_ref_override(patch, env, master_ref, 1, &status), RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_PLACED_COPY_REPLACED);
  EXPECT_EQ(radicle_plugin_copy_placed_ref_override(patch, env, stat, 0, &status), RADICLE_STATUS_INVALID_ARGUMENT);

  radicle_form_key created{};
  ASSERT_EQ(radicle_plugin_copy_placed_ref_new(patch, env, master_ref, &created), RADICLE_STATUS_OK);
  EXPECT_STREQ(created.plugin, "Patch.esp");
  EXPECT_EQ(created.local_id, 0x800u);

  const float position[3] = {7, 8, 9};
  const float rotation[3] = {0, 0, 1};
  ASSERT_EQ(radicle_plugin_set_placed_transform(patch, {"Master.esm", 0x802}, position, rotation), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_placed_transform(patch, {"Master.esm", 0x803}, position, rotation),
            RADICLE_STATUS_NOT_FOUND);
  ASSERT_EQ(radicle_plugin_set_placed_scale(patch, {"Master.esm", 0x802}, 0, 0.0f), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_plugin_sort_masters(patch, env), RADICLE_STATUS_OK);

  const auto patch_path = (root_ / "Patch.esp").string();
  ASSERT_EQ(radicle_plugin_write_file(patch, patch_path.c_str()), RADICLE_STATUS_OK);
  radicle_plugin_destroy(patch);
  radicle_env_destroy(env);

  raw.push_back(patch_path.c_str());
  ASSERT_EQ(radicle_env_load(raw.data(), raw.size(), &env), RADICLE_STATUS_OK);
  const radicle_record* winner = nullptr;
  ASSERT_EQ(radicle_env_winner(env, {"Master.esm", 0x802}, &winner), RADICLE_STATUS_OK);
  radicle_placed_ref* placed = nullptr;
  ASSERT_EQ(radicle_placed_ref_decode(env, winner, &placed), RADICLE_STATUS_OK);
  EXPECT_EQ(placed->position[0], 7.0f);
  EXPECT_EQ(placed->rotation[2], 1.0f);
  EXPECT_EQ(placed->has_scale, 0);
  radicle_placed_ref_free(placed);

  ASSERT_EQ(radicle_env_winner(env, {"DLC.esm", 0x841}, &winner), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_placed_ref_decode(env, winner, &placed), RADICLE_STATUS_OK);
  ASSERT_EQ(placed->activate_parent_count, 1u);
  EXPECT_STREQ(placed->activate_parents[0].reference.plugin, "DLC.esm");
  EXPECT_EQ(placed->activate_parents[0].reference.local_id, 0x842u);
  radicle_placed_ref_free(placed);
  radicle_env_destroy(env);
}

TEST_F(WritebackTest, CApiMoveWithinACellReportsMoved) {
  auto* env = load_env(paths_);
  ASSERT_NE(env, nullptr);
  radicle_plugin* patch = nullptr;
  ASSERT_EQ(radicle_plugin_create("Patch.esp", &patch), RADICLE_STATUS_OK);
  copy_master_reference(patch, env, 0x802);

  const float position[3] = {4000.0f, 4095.0f, 30.0f};
  const float rotation[3] = {0.0f, 0.0f, 1.0f};
  radicle_placed_move_status status = RADICLE_PLACED_MOVE_RELOCATED;
  ASSERT_EQ(radicle_plugin_move_placed_ref(patch, {"Master.esm", 0x802}, position, rotation, &status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_PLACED_MOVE_MOVED);

  const auto patch_path = root_ / "Patch.esp";
  ASSERT_EQ(radicle_plugin_write_file(patch, patch_path.string().c_str()), RADICLE_STATUS_OK);
  radicle_plugin_destroy(patch);
  radicle_env_destroy(env);

  auto with_patch = paths_;
  with_patch.push_back(patch_path);
  const auto reloaded = fo4::env::Environment::load(with_patch);
  EXPECT_EQ(fo4::env::decode_placed_reference(reloaded, *reloaded.winner(key("Master.esm", 0x802))).position,
            (fo4::env::Vec3{4000, 4095, 30}));
}

TEST_F(WritebackTest, CApiMoveAcrossCellsReportsRelocated) {
  auto* env = load_env(paths_);
  ASSERT_NE(env, nullptr);
  radicle_plugin* patch = nullptr;
  ASSERT_EQ(radicle_plugin_create("Patch.esp", &patch), RADICLE_STATUS_OK);
  copy_master_reference(patch, env, 0x802);
  copy_master_reference(patch, env, 0x805);

  const float position[3] = {4106.0f, 20.0f, 30.0f};
  const float rotation[3] = {0.0f, 0.0f, 1.0f};
  radicle_placed_move_status status = RADICLE_PLACED_MOVE_MOVED;
  ASSERT_EQ(radicle_plugin_move_placed_ref(patch, {"Master.esm", 0x802}, position, rotation, &status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_PLACED_MOVE_RELOCATED);

  const auto patch_path = root_ / "Patch.esp";
  ASSERT_EQ(radicle_plugin_write_file(patch, patch_path.string().c_str()), RADICLE_STATUS_OK);
  radicle_plugin_destroy(patch);
  radicle_env_destroy(env);

  auto with_patch = paths_;
  with_patch.push_back(patch_path);
  const auto reloaded = fo4::env::Environment::load(with_patch);
  const auto children = reloaded.cell_children(key("Master.esm", 0x804));
  EXPECT_TRUE(std::any_of(children.begin(), children.end(), [](const auto& child) {
    return child.ref->record->form_key == key("Master.esm", 0x802);
  }));
  EXPECT_EQ(fo4::env::decode_placed_reference(reloaded, *reloaded.winner(key("Master.esm", 0x802))).position,
            (fo4::env::Vec3{4106, 20, 30}));
}

TEST_F(WritebackTest, CApiMoveReportsAMissingReferenceAsNotFound) {
  radicle_plugin* patch = nullptr;
  ASSERT_EQ(radicle_plugin_create("Patch.esp", &patch), RADICLE_STATUS_OK);

  const float position[3] = {4000.0f, 4095.0f, 30.0f};
  const float rotation[3] = {0.0f, 0.0f, 0.0f};
  radicle_placed_move_status status = RADICLE_PLACED_MOVE_RELOCATED;
  EXPECT_EQ(radicle_plugin_move_placed_ref(patch, {"Master.esm", 0x803}, position, rotation, &status),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_STREQ(radicle_last_error(), "plugin has no such record");
  EXPECT_EQ(status, RADICLE_PLACED_MOVE_RELOCATED);
  radicle_plugin_destroy(patch);
}

TEST_F(WritebackTest, CApiMoveWithoutATargetCellFailsAndKeepsTheRecord) {
  auto* env = load_env(paths_);
  ASSERT_NE(env, nullptr);
  radicle_plugin* patch = nullptr;
  ASSERT_EQ(radicle_plugin_create("Patch.esp", &patch), RADICLE_STATUS_OK);
  copy_master_reference(patch, env, 0x802);

  const float position[3] = {12298.0f, 20.0f, 30.0f};
  const float rotation[3] = {0.0f, 0.0f, 0.0f};
  radicle_placed_move_status status = RADICLE_PLACED_MOVE_RELOCATED;
  EXPECT_EQ(radicle_plugin_move_placed_ref(patch, {"Master.esm", 0x802}, position, rotation, &status),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_STREQ(radicle_last_error(), "plugin has no exterior CELL at the new position; create it first");
  EXPECT_EQ(status, RADICLE_PLACED_MOVE_RELOCATED);

  const auto patch_path = root_ / "Patch.esp";
  ASSERT_EQ(radicle_plugin_write_file(patch, patch_path.string().c_str()), RADICLE_STATUS_OK);
  radicle_plugin_destroy(patch);
  radicle_env_destroy(env);

  auto with_patch = paths_;
  with_patch.push_back(patch_path);
  const auto reloaded = fo4::env::Environment::load(with_patch);
  EXPECT_EQ(fo4::env::decode_placed_reference(reloaded, *reloaded.winner(key("Master.esm", 0x802))).position,
            (fo4::env::Vec3{1, 2, 3}));
}

TEST_F(WritebackTest, CApiMoveRejectsNullArguments) {
  radicle_plugin* patch = nullptr;
  ASSERT_EQ(radicle_plugin_create("Patch.esp", &patch), RADICLE_STATUS_OK);

  const float position[3] = {4000.0f, 4095.0f, 30.0f};
  const float rotation[3] = {0.0f, 0.0f, 0.0f};
  radicle_placed_move_status status = RADICLE_PLACED_MOVE_MOVED;
  EXPECT_EQ(radicle_plugin_move_placed_ref(nullptr, {"Master.esm", 0x802}, position, rotation, &status),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_move_placed_ref(patch, {"Master.esm", 0x802}, nullptr, rotation, &status),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_move_placed_ref(patch, {"Master.esm", 0x802}, position, nullptr, &status),
            RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_plugin_destroy(patch);
}

TEST_F(WritebackTest, CApiMoveAcceptsANullOutStatus) {
  auto* env = load_env(paths_);
  ASSERT_NE(env, nullptr);
  radicle_plugin* patch = nullptr;
  ASSERT_EQ(radicle_plugin_create("Patch.esp", &patch), RADICLE_STATUS_OK);
  copy_master_reference(patch, env, 0x802);

  const float position[3] = {4000.0f, 4095.0f, 30.0f};
  const float rotation[3] = {0.0f, 0.0f, 0.0f};
  EXPECT_EQ(radicle_plugin_move_placed_ref(patch, {"Master.esm", 0x802}, position, rotation, nullptr),
            RADICLE_STATUS_OK);
  radicle_plugin_destroy(patch);
  radicle_env_destroy(env);
}
