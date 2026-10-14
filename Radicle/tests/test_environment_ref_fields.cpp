#include "fo4/c_api_env.h"
#include "fo4/environment/placed.hpp"
#include "fo4/forms/placed_reference.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <span>
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


fo4::FormKey key(std::string_view plugin, std::uint32_t local) {
  return fo4::FormKey{fo4::PluginKey::from_filename(plugin), fo4::LocalFormId{local}};
}

std::vector<std::byte> bytes(std::initializer_list<std::uint8_t> values) {
  std::vector<std::byte> out;
  for (const auto value : values) out.push_back(std::byte{value});
  return out;
}

std::vector<std::byte> make_master() {
  fo4::test::ByteWriter xtel;
  xtel.u32(0x823);
  for (float v : {1.0f, 2.0f, 3.0f, 0.0f, 0.0f, 0.5f}) xtel.f32(v);
  xtel.u32(0x7);
  xtel.u32(0);
  fo4::test::ByteWriter xprm;
  for (float v : {10.0f, 20.0f, 30.0f, 0.1f, 0.2f, 0.3f, 0.0f}) xprm.f32(v);
  xprm.u32(1);
  std::vector<std::byte> xown = words({0x830, 0xAABBCCDD});
  auto crime = bytes({0x00, 0x11, 0x22, 0x33});
  xown.insert(xown.end(), crime.begin(), crime.end());
  auto xesp = words({0x823});
  auto esp_tail = bytes({0x01, 0x44, 0x55, 0x66});
  xesp.insert(xesp.end(), esp_tail.begin(), esp_tail.end());
  return make_plugin(0x1, {},
                     {top("FACT", {rec("FACT", 0x830)}),
                      top("KYWD", {rec("KYWD", 0x812)}),
                      top("LCTN", {rec("LCTN", 0x813)}),
                      top("CELL", {grp(0, 2, {grp(0, 3, {rec("CELL", 0x820, {{"DATA", {std::byte{1}}}}),
                                                          grp(0x820, 6, {grp(0x820, 9, {
                                                              rec("REFR", 0x821,
                                                                  {{"EDID", {std::byte{'A'}, std::byte{0}}},
                                                                   {"NAME", words({0x823})},
                                                                   {"XPRM", xprm.bytes},
                                                                   {"XTEL", xtel.bytes},
                                                                   {"XAPD", {std::byte{1}}},
                                                                   {"XAPR", id_then_float(0x823, 0.25f)},
                                                                   {"XAPR", id_then_float(0x822, 0.75f)},
                                                                   {"XOWN", xown},
                                                                   {"XRNK", words({3})},
                                                                   {"XESP", xesp},
                                                                   {"XLKR", words({0x812, 0x823})},
                                                                   {"XLKR", words({0, 0x822})},
                                                                   {"DATA", floats({0, 0, 0, 0, 0, 0})}}),
                                                              rec("ACHR", 0x822,
                                                                  {{"NAME", words({0x823})},
                                                                   {"ONAM", {}},
                                                                   {"DATA", floats({0, 0, 0, 0, 0, 0})}}),
                                                              rec("REFR", 0x823,
                                                                  {{"NAME", words({0x823})},
                                                                   {"DATA", floats({0, 0, 0, 0, 0, 0})}})})})})})})});
}

std::vector<std::byte> make_dlc() {
  return make_plugin(0x1, {"Master.esm"}, {top("FACT", {rec("FACT", 0x01000831)})});
}

class RefFieldsTest : public ::testing::Test {
protected:
  void SetUp() override {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    root_ = std::filesystem::temp_directory_path() / ("radicle_ref_fields_" + std::to_string(id));
    std::filesystem::create_directories(root_);
    paths_ = {write("Master.esm", make_master()), write("DLC.esm", make_dlc())};
    env_ = fo4::env::Environment::load(paths_);
    patch_ = fo4::make_empty_plugin("Patch.esp");
    for (const std::uint32_t form : {0x821u, 0x822u}) {
      fo4::forms::copy_placed_reference_as_override(patch_, env_.plugin(0), key("Master.esm", form));
    }
  }

  void TearDown() override {
    std::error_code ec;
    std::filesystem::remove_all(root_, ec);
  }

  std::filesystem::path write(std::string_view name, const std::vector<std::byte>& data) {
    const auto path = root_ / std::string(name);
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    return path;
  }

  fo4::env::Environment reload(const fo4::Plugin& patch) {
    const auto patch_path = root_ / "Patch.esp";
    fo4::write_plugin_file(patch, patch_path);
    auto with_patch = paths_;
    with_patch.push_back(patch_path);
    return fo4::env::Environment::load(with_patch);
  }

  std::filesystem::path root_;
  std::vector<std::filesystem::path> paths_;
  fo4::env::Environment env_;
  fo4::Plugin patch_;
};

std::vector<std::string> order_of(const fo4::Record& record) {
  std::vector<std::string> out;
  for (const auto& sub : record.subrecords) out.push_back(sub.signature.to_string());
  return out;
}

const fo4::Subrecord* find_sub(const fo4::Record& record, std::string_view signature) {
  for (const auto& sub : record.subrecords) {
    if (sub.signature == fo4::Signature::from_string(signature)) return &sub;
  }
  return nullptr;
}

}  // namespace

TEST_F(RefFieldsTest, EditsEveryWritebackFieldAndReloads) {
  namespace f = fo4::forms;
  const auto refr = key("Master.esm", 0x821);
  const auto achr = key("Master.esm", 0x822);
  ASSERT_TRUE(f::set_placed_reference_editor_id(patch_, refr, "Moved"));
  ASSERT_TRUE(f::set_placed_reference_editor_id(patch_, achr, "NewNpc"));
  ASSERT_TRUE(f::set_placed_reference_record_flags(patch_, refr, 0x800 | fo4::RecordFlags::persistent));
  ASSERT_TRUE(f::set_placed_reference_comments(patch_, refr, std::string_view{"note"}));
  ASSERT_TRUE(f::set_placed_reference_location_reference(patch_, refr, key("Master.esm", 0x813)));
  ASSERT_TRUE(f::set_placed_reference_ownership(patch_, refr, key("Master.esm", 0x830), true, std::nullopt));
  ASSERT_TRUE(f::set_placed_reference_ownership(patch_, achr, key("Master.esm", 0x830), false, 2));
  ASSERT_TRUE(f::set_placed_reference_enable_parent(patch_, refr, key("Master.esm", 0x822), 0x00));
  ASSERT_TRUE(f::set_placed_reference_enable_parent(patch_, achr, key("Master.esm", 0x823), 0x01));
  const std::vector<f::LinkedReferenceEdit> links{{std::nullopt, key("Master.esm", 0x822)},
                                                  {key("Master.esm", 0x812), key("Master.esm", 0x823)},
                                                  {std::nullopt, key("Master.esm", 0x821)}};
  ASSERT_TRUE(f::set_placed_reference_linked_references(patch_, refr, links));
  ASSERT_TRUE(f::set_placed_reference_linked_references(patch_, achr, std::span(links).first(1)));
  EXPECT_EQ(f::set_placed_reference_teleport(patch_, refr, key("Master.esm", 0x822), {4, 5, 6, 0, 0, 1}),
            f::PresentFieldEdit::updated);
  EXPECT_EQ(f::set_placed_reference_teleport(patch_, achr, key("Master.esm", 0x821), {}),
            f::PresentFieldEdit::field_absent);
  EXPECT_EQ(f::set_placed_reference_primitive(patch_, refr, {1, 2, 3}, 2), f::PresentFieldEdit::updated);
  EXPECT_EQ(f::set_placed_reference_activate_parent_delay(patch_, refr, 1, 2.5f), f::PresentFieldEdit::updated);
  EXPECT_EQ(f::set_placed_reference_activate_parent_delay(patch_, refr, 2, 1.0f), f::PresentFieldEdit::field_absent);
  EXPECT_EQ(f::set_placed_reference_activate_parent_delay(patch_, key("Master.esm", 0x823), 0, 1.0f),
            f::PresentFieldEdit::record_missing);
  ASSERT_TRUE(f::set_placed_reference_scale(patch_, achr, 1.25f));

  const auto* r = fo4::find_record(patch_, refr);
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(order_of(*r), (std::vector<std::string>{"EDID", "NAME", "XPRM", "XTEL", "XAPD", "XAPR", "XAPR", "XLRL",
                                                    "XOWN", "XESP", "XLKR", "XLKR", "XLKR", "DATA", "MNAM"}));
  EXPECT_EQ(std::vector<std::byte>(find_sub(*r, "XOWN")->data.begin() + 4, find_sub(*r, "XOWN")->data.end()),
            (bytes({0xDD, 0xCC, 0xBB, 0xAA, 0x01, 0x11, 0x22, 0x33})));
  EXPECT_EQ(std::vector<std::byte>(find_sub(*r, "XESP")->data.begin() + 4, find_sub(*r, "XESP")->data.end()),
            (bytes({0x00, 0x44, 0x55, 0x66})));
  const auto* a = fo4::find_record(patch_, achr);
  EXPECT_EQ(order_of(*a), (std::vector<std::string>{"EDID", "NAME", "XLKR", "XESP", "XOWN", "XRNK", "XSCL", "ONAM",
                                                    "DATA"}));

  const auto reloaded = reload(patch_);
  const auto* winner = reloaded.winner(refr);
  ASSERT_EQ(winner->plugin, 2u);
  EXPECT_EQ(reloaded.metadata(*winner).editor_id, std::optional<std::string_view>{"Moved"});
  EXPECT_EQ(winner->record->header.flags.value(), 0x800u);
  const auto placed = fo4::env::decode_placed_reference(reloaded, *winner);
  EXPECT_EQ(placed.comments, std::optional<std::string_view>{"note"});
  EXPECT_EQ(placed.location_reference, key("Master.esm", 0x813));
  ASSERT_TRUE(placed.ownership.has_value());
  EXPECT_EQ(placed.ownership->owner, key("Master.esm", 0x830));
  EXPECT_EQ(placed.ownership->no_crime, std::optional<bool>{true});
  EXPECT_FALSE(placed.faction_rank.has_value());
  EXPECT_EQ(placed.enable_parent->reference, key("Master.esm", 0x822));
  ASSERT_EQ(placed.linked_references.size(), 3u);
  EXPECT_TRUE(placed.linked_references[0].keyword.owner().empty());
  EXPECT_EQ(placed.linked_references[1].keyword, key("Master.esm", 0x812));
  EXPECT_EQ(placed.linked_references[2].reference, refr);
  EXPECT_EQ(placed.teleport->door, key("Master.esm", 0x822));
  EXPECT_EQ(placed.teleport->position, (fo4::env::Vec3{4, 5, 6}));
  EXPECT_EQ(placed.teleport->flags, 7u);
  EXPECT_EQ(placed.primitive->bounds, (fo4::env::Vec3{1, 2, 3}));
  EXPECT_EQ(placed.primitive->type, 2u);
  EXPECT_EQ(placed.activate_parents.at(0).delay, 0.25f);
  EXPECT_EQ(placed.activate_parents.at(1).delay, 2.5f);

  const auto npc = fo4::env::decode_placed_reference(reloaded, *reloaded.winner(achr));
  EXPECT_EQ(npc.faction_rank, std::optional<std::int32_t>{2});
  EXPECT_EQ(npc.scale, std::optional<float>{1.25f});
  EXPECT_EQ(npc.enable_parent->flags, std::optional<std::uint8_t>{1});

  ASSERT_TRUE(f::set_placed_reference_ownership(patch_, achr, std::nullopt, false, std::nullopt));
  ASSERT_TRUE(f::set_placed_reference_comments(patch_, refr, std::nullopt));
  ASSERT_TRUE(f::set_placed_reference_location_reference(patch_, refr, std::nullopt));
  ASSERT_TRUE(f::set_placed_reference_enable_parent(patch_, refr, std::nullopt, 0));
  ASSERT_TRUE(f::set_placed_reference_editor_id(patch_, refr, ""));
  ASSERT_TRUE(f::set_placed_reference_linked_references(patch_, refr, {}));
  EXPECT_EQ(order_of(*fo4::find_record(patch_, refr)),
            (std::vector<std::string>{"NAME", "XPRM", "XTEL", "XAPD", "XAPR", "XAPR", "XOWN", "DATA"}));
  EXPECT_EQ(find_sub(*fo4::find_record(patch_, achr), "XRNK"), nullptr);
}

TEST_F(RefFieldsTest, RecordFlagsNeverToggleDeleted) {
  namespace f = fo4::forms;
  const auto refr = key("Master.esm", 0x821);
  ASSERT_TRUE(f::set_placed_reference_record_flags(patch_, refr, 0x800 | fo4::RecordFlags::deleted));
  const auto* r = fo4::find_record(patch_, refr);
  ASSERT_NE(r, nullptr);
  EXPECT_FALSE(r->header.flags.is_deleted());
  EXPECT_NE(r->header.flags.value() & 0x800u, 0u);
}

TEST_F(RefFieldsTest, RejectsBadInputWithoutChangingThePlugin) {
  namespace f = fo4::forms;
  const auto refr = key("Master.esm", 0x821);
  const auto before = fo4::write_plugin(patch_);
  EXPECT_THROW(f::set_placed_reference_ownership(patch_, refr, key("DLC.esm", 0x831), false, std::nullopt),
               f::PlacedReferenceError);
  EXPECT_THROW(f::set_placed_reference_editor_id(patch_, refr, std::string_view("a\0b", 3)), f::PlacedReferenceError);
  EXPECT_THROW(f::set_placed_reference_scale(patch_, refr, std::numeric_limits<float>::infinity()),
               f::PlacedReferenceError);
  auto duplicate = patch_;
  fo4::visit_records(duplicate, [](fo4::Record& record) {
    if (record.header.signature == fo4::Signature::from_string("REFR")) {
      record.subrecords.push_back({fo4::Signature::from_string("XESP"), words({0x823, 0}), false});
    }
  });
  EXPECT_THROW(f::set_placed_reference_enable_parent(duplicate, refr, std::nullopt, 0), f::PlacedReferenceError);
  EXPECT_FALSE(f::set_placed_reference_comments(patch_, key("Master.esm", 0x823), std::string_view{"x"}));
  auto cell_edit = patch_;
  EXPECT_THROW(f::set_placed_reference_comments(cell_edit, key("Master.esm", 0x820), std::string_view{"x"}),
               f::PlacedReferenceError);
  EXPECT_EQ(fo4::write_plugin(patch_), before);
}

TEST_F(RefFieldsTest, NoCrimeWritesAWholeBoolByte) {
  const auto refr = key("Master.esm", 0x821);
  fo4::visit_records(patch_, [](fo4::Record& record) {
    for (auto& sub : record.subrecords) {
      if (sub.signature == fo4::Signature::from_string("XOWN")) sub.data[8] = std::byte{0x03};
    }
  });
  ASSERT_TRUE(fo4::forms::set_placed_reference_ownership(patch_, refr, key("Master.esm", 0x830), false, std::nullopt));
  EXPECT_EQ(std::vector<std::byte>(find_sub(*fo4::find_record(patch_, refr), "XOWN")->data.begin() + 8,
                                   find_sub(*fo4::find_record(patch_, refr), "XOWN")->data.end()),
            (bytes({0x00, 0x11, 0x22, 0x33})));
}

TEST_F(RefFieldsTest, EmptyCommentsRemoveMnamAndSurviveReload) {
  const auto refr = key("Master.esm", 0x821);
  ASSERT_TRUE(fo4::forms::set_placed_reference_comments(patch_, refr, std::string_view{"note"}));
  ASSERT_TRUE(fo4::forms::set_placed_reference_comments(patch_, refr, std::string_view{}));
  EXPECT_EQ(find_sub(*fo4::find_record(patch_, refr), "MNAM"), nullptr);
  ASSERT_TRUE(fo4::forms::set_placed_reference_comments(patch_, refr, std::string_view{"note"}));
  const auto path = (root_ / "Patch.esp").string();
  fo4::write_plugin_file(patch_, path);
  radicle_plugin* patch = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(path.c_str(), &patch), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_plugin_set_placed_comments(patch, {"Master.esm", 0x821}, ""), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_plugin_write_file(patch, path.c_str()), RADICLE_STATUS_OK);
  radicle_plugin_destroy(patch);
  const auto reloaded = reload(fo4::read_plugin_file(path));
  EXPECT_FALSE(fo4::env::decode_placed_reference(reloaded, *reloaded.winner(refr)).comments.has_value());
}

TEST_F(RefFieldsTest, CApiWritesTextAsWindows1252) {
  const auto path = (root_ / "Patch.esp").string();
  fo4::write_plugin_file(patch_, path);
  radicle_plugin* patch = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(path.c_str(), &patch), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_plugin_set_placed_comments(patch, {"Master.esm", 0x821}, "Caf\xC3\xA9"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_placed_editor_id(patch, {"Master.esm", 0x821}, "\xD0\xB6"), RADICLE_STATUS_INVALID_ARGUMENT);
  ASSERT_EQ(radicle_plugin_write_file(patch, path.c_str()), RADICLE_STATUS_OK);
  radicle_plugin_destroy(patch);
  const auto written = fo4::read_plugin_file(path);
  const auto* mnam = find_sub(*fo4::find_record(written, key("Master.esm", 0x821)), "MNAM");
  ASSERT_NE(mnam, nullptr);
  EXPECT_EQ(mnam->data, bytes({'C', 'a', 'f', 0xE9, 0}));
}

TEST_F(RefFieldsTest, CApiRejectsOwnersItCannotWriteFaithfully) {
  const auto light = write("Light.esl", make_plugin(0x201, {"Master.esm"}, {top("FACT", {rec("FACT", 0x01000801)})}));
  const auto later = write("Later.esp", make_plugin(0, {"Master.esm"}, {top("FACT", {rec("FACT", 0x01000900)})}));
  const auto patch_path = root_ / "Patch.esp";
  fo4::write_plugin_file(patch_, patch_path);
  const auto stray_path = root_ / "Stray.esp";
  fo4::write_plugin_file(fo4::remap_plugin_masters(patch_, {{fo4::PluginKey::from_filename("Master.esm"), fo4::PluginKind::full},
                                                           {fo4::PluginKey::from_filename("Other.esm"), fo4::PluginKind::full}}),
                         stray_path);
  std::vector<std::string> utf8;
  for (const auto& path : {paths_[0], paths_[1], light, patch_path, later}) utf8.push_back(path.string());
  std::vector<const char*> raw;
  for (const auto& path : utf8) raw.push_back(path.c_str());
  radicle_env* env = nullptr;
  ASSERT_EQ(radicle_env_load(raw.data(), raw.size(), &env), RADICLE_STATUS_OK);
  radicle_plugin* patch = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(patch_path.string().c_str(), &patch), RADICLE_STATUS_OK);
  const radicle_form_key ref{"Master.esm", 0x821};

  EXPECT_EQ(radicle_plugin_set_placed_enable_parent(patch, env, ref, {"Light.esl", 0x1801}, 0),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_placed_location_reference(patch, env, ref, {"Data/", 0x813}),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_placed_ownership(patch, env, ref, {"Later.esp", 0x900}, 0, 0, 0),
            RADICLE_STATUS_INVALID_ARGUMENT);
  ASSERT_EQ(radicle_plugin_set_placed_enable_parent(patch, env, ref, {"Light.esl", 0x801}, 0), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_plugin_set_placed_ownership(patch, env, ref, {"dlc.ESM", 0x831}, 0, 0, 0), RADICLE_STATUS_OK);
  const auto out = (root_ / "Out.esp").string();
  ASSERT_EQ(radicle_plugin_write_file(patch, out.c_str()), RADICLE_STATUS_OK);
  radicle_plugin_destroy(patch);
  std::vector<std::string> masters;
  for (const auto& master : fo4::read_plugin_file(out).header.masters) masters.emplace_back(master.key.filename());
  EXPECT_EQ(masters, (std::vector<std::string>{"Master.esm", "DLC.esm", "Light.esl"}));

  ASSERT_EQ(radicle_plugin_read_file(stray_path.string().c_str(), &patch), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_placed_ownership(patch, env, ref, {"DLC.esm", 0x831}, 0, 0, 0),
            RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_plugin_destroy(patch);
  radicle_env_destroy(env);
}

TEST_F(RefFieldsTest, CApiAddsMastersForNewLinksAndKeepsLoadOrder) {
  std::vector<std::string> utf8;
  for (const auto& path : paths_) utf8.push_back(path.string());
  std::vector<const char*> raw;
  for (const auto& path : utf8) raw.push_back(path.c_str());
  radicle_env* env = nullptr;
  ASSERT_EQ(radicle_env_load(raw.data(), raw.size(), &env), RADICLE_STATUS_OK);
  radicle_plugin* patch = nullptr;
  ASSERT_EQ(radicle_plugin_create("Patch.esp", &patch), RADICLE_STATUS_OK);
  const radicle_record* refr = nullptr;
  ASSERT_EQ(radicle_env_winner(env, {"Master.esm", 0x821}, &refr), RADICLE_STATUS_OK);
  radicle_placed_copy_status status{};
  ASSERT_EQ(radicle_plugin_copy_placed_ref_override(patch, env, refr, 0, &status), RADICLE_STATUS_OK);

  const radicle_form_key ref{"Master.esm", 0x821};
  ASSERT_EQ(radicle_plugin_set_placed_ownership(patch, env, ref, {"DLC.esm", 0x831}, 0, 1, 4), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_placed_ownership(patch, env, ref, {"Nope.esm", 0x1}, 0, 0, 0),
            RADICLE_STATUS_INVALID_ARGUMENT);
  const radicle_linked_ref links[] = {{{nullptr, 0}, {"Master.esm", 0x822}}};
  ASSERT_EQ(radicle_plugin_set_placed_linked_refs(patch, env, ref, links, 1), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_plugin_set_placed_editor_id(patch, ref, "CApi"), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_plugin_set_placed_comments(patch, ref, "c"), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_plugin_set_placed_record_flags(patch, ref, 0x800), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_plugin_set_placed_location_reference(patch, env, ref, {"Master.esm", 0x813}), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_plugin_set_placed_enable_parent(patch, env, ref, {nullptr, 0}, 0), RADICLE_STATUS_OK);
  const float position[3] = {9, 9, 9};
  const float rotation[3] = {0, 0, 0};
  ASSERT_EQ(radicle_plugin_set_placed_teleport(patch, env, ref, {"Master.esm", 0x823}, position, rotation),
            RADICLE_STATUS_OK);
  const float bounds[3] = {5, 5, 5};
  ASSERT_EQ(radicle_plugin_set_placed_primitive(patch, ref, bounds, 3), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_plugin_set_placed_activate_parent_delay(patch, ref, 0, 9.0f), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_placed_activate_parent_delay(patch, ref, 5, 9.0f), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_placed_comments(patch, {"Master.esm", 0x820}, "x"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_placed_comments(patch, {"Master.esm", 0x999}, "x"), RADICLE_STATUS_NOT_FOUND);

  const auto path = (root_ / "Patch.esp").string();
  ASSERT_EQ(radicle_plugin_write_file(patch, path.c_str()), RADICLE_STATUS_OK);
  radicle_plugin_destroy(patch);
  const auto written = fo4::read_plugin_file(path);
  ASSERT_EQ(written.header.masters.size(), 2u);
  EXPECT_EQ(written.header.masters[0].key, fo4::PluginKey::from_filename("Master.esm"));
  EXPECT_EQ(written.header.masters[1].key, fo4::PluginKey::from_filename("DLC.esm"));

  radicle_env_destroy(env);
  raw.push_back(path.c_str());
  ASSERT_EQ(radicle_env_load(raw.data(), raw.size(), &env), RADICLE_STATUS_OK);
  const radicle_record* winner = nullptr;
  ASSERT_EQ(radicle_env_winner(env, ref, &winner), RADICLE_STATUS_OK);
  radicle_placed_ref* placed = nullptr;
  ASSERT_EQ(radicle_placed_ref_decode(env, winner, &placed), RADICLE_STATUS_OK);
  EXPECT_STREQ(placed->owner.plugin, "DLC.esm");
  EXPECT_EQ(placed->owner.local_id, 0x831u);
  EXPECT_EQ(placed->faction_rank, 4);
  EXPECT_EQ(placed->linked_ref_count, 1u);
  EXPECT_STREQ(placed->comments, "c");
  EXPECT_EQ(placed->has_enable_parent, 0);
  EXPECT_EQ(placed->teleport_position[0], 9.0f);
  EXPECT_EQ(placed->primitive_type, 3u);
  EXPECT_EQ(placed->activate_parents[0].delay, 9.0f);
  radicle_placed_ref_free(placed);
  radicle_record_info info{};
  ASSERT_EQ(radicle_record_get_info(env, winner, &info), RADICLE_STATUS_OK);
  EXPECT_STREQ(info.editor_id, "CApi");
  EXPECT_EQ(info.record_flags, 0x800u);
  radicle_env_destroy(env);
}
