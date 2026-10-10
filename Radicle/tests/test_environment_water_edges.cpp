#include "fo4/environment/world.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

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
  std::uint32_t flags{0};
  std::vector<Sub> subrecords;
  std::uint32_t label{0};
  std::int32_t group_type{0};
  std::vector<Node> children;
};

Node rec(std::string signature, std::uint32_t form_id, std::vector<Sub> subrecords = {},
         std::uint32_t flags = 0) {
  Node node;
  node.signature = std::move(signature);
  node.form_id = form_id;
  node.flags = flags;
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

std::vector<std::byte> bytes_of(std::initializer_list<std::uint32_t> words) {
  fo4::test::ByteWriter out;
  for (const auto word : words) out.u32(word);
  return out.bytes;
}

std::vector<std::byte> floats(std::initializer_list<float> values) {
  fo4::test::ByteWriter out;
  for (const auto value : values) out.f32(value);
  return out.bytes;
}

std::vector<std::byte> zstring(std::string_view text) {
  std::vector<std::byte> bytes;
  for (const char ch : text) bytes.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  bytes.push_back(std::byte{0});
  return bytes;
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
  const auto header = fo4::test::begin_record(out, node.signature, node.flags, node.form_id);
  for (const auto& sub : node.subrecords) fo4::test::write_subrecord(out, sub.signature, sub.data);
  fo4::test::end_record(out, header);
}

std::vector<std::byte> make_plugin(const std::vector<std::string>& masters,
                                   const std::vector<Node>& groups) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  for (const auto& master : masters) {
    fo4::test::write_subrecord(out, "MAST", zstring(master));
    fo4::test::ByteWriter data;
    data.u32(0);
    data.u32(0);
    fo4::test::write_subrecord(out, "DATA", data.bytes);
  }
  fo4::test::end_record(out, tes4);
  for (const auto& group : groups) write_node(out, group);
  return out.bytes;
}

Node parented_world(std::uint32_t id, std::uint32_t parent, std::uint8_t flags) {
  return rec("WRLD", id, {{"WNAM", bytes_of({parent})}, {"PNAM", {std::byte{flags}, std::byte{0}}}});
}

class WaterEdgeTest : public ::testing::Test {
protected:
  void SetUp() override {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    root_ = std::filesystem::temp_directory_path() / ("radicle_water_edges_" + std::to_string(id));
    std::filesystem::create_directories(root_);
  }

  void TearDown() override {
    std::error_code ec;
    std::filesystem::remove_all(root_, ec);
  }

  std::filesystem::path write(std::string_view name, const std::vector<std::byte>& bytes) {
    const auto path = root_ / std::string(name);
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    return path;
  }

  std::filesystem::path root_;
  std::vector<std::filesystem::path> paths_;
};

fo4::FormKey key(std::string_view text) {
  return fo4::FormKey::parse(text);
}

}  // namespace

TEST_F(WaterEdgeTest, WrldParentCycleThroughBit0Throws) {
  paths_ = {write("Fallout4.esm", make_plugin({}, {top("WRLD", {parented_world(0x800, 0x801, 1),
                                                                parented_world(0x801, 0x800, 1)})}))};
  const auto env = fo4::env::Environment::load(paths_);
  EXPECT_THROW(fo4::env::resolve_water(env, *env.winner(key("000800:Fallout4.esm"))),
               fo4::env::WorldDecodeError);
  EXPECT_THROW(fo4::env::resolve_water(env, *env.winner(key("000801:Fallout4.esm"))),
               fo4::env::WorldDecodeError);
}

TEST_F(WaterEdgeTest, DeletedWinningWorldspaceInputResolvesDefaultWaterAtZero) {
  paths_ = {write("Fallout4.esm", make_plugin({}, {top("WATR", {rec("WATR", 0x840)}),
                  top("WRLD", {rec("WRLD", 0x800, {{"NAM2", bytes_of({0x840})}, {"DNAM", floats({-2048, 450})}})})})),
            write("Override.esp", make_plugin({"Fallout4.esm"}, {top("WRLD", {rec("WRLD", 0x800, {}, 0x20)})}))};
  const auto env = fo4::env::Environment::load(paths_);
  ASSERT_TRUE(env.metadata(*env.winner(key("000800:Fallout4.esm"))).deleted);
  const auto selection = fo4::env::resolve_water(env, *env.winner(key("000800:Fallout4.esm")));
  EXPECT_TRUE(selection.uses_default_water);
  EXPECT_EQ(selection.water, key("000018:Fallout4.esm"));
  ASSERT_TRUE(selection.height.has_value());
  EXPECT_EQ(*selection.height, 0.0f);
}

TEST_F(WaterEdgeTest, CellInDeletedWinningWorldspaceResolvesDefaultWater) {
  paths_ = {write("Fallout4.esm", make_plugin({}, {top("WATR", {rec("WATR", 0x18), rec("WATR", 0x840)}),
                  top("WRLD", {rec("WRLD", 0x800, {{"NAM2", bytes_of({0x840})}, {"DNAM", floats({-2048, 450})}}),
                               grp(0x800, 1, {grp(0, 4, {grp(0, 5, {rec("CELL", 0x810, {{"DATA", {std::byte{0x02}}}})})})})})})),
            write("Override.esp", make_plugin({"Fallout4.esm"}, {top("WRLD", {rec("WRLD", 0x800, {}, 0x20)})}))};
  const auto env = fo4::env::Environment::load(paths_);
  ASSERT_TRUE(env.metadata(*env.winner(key("000800:Fallout4.esm"))).deleted);
  const auto selection = fo4::env::resolve_water(env, *env.winner(key("000810:Fallout4.esm")));
  EXPECT_TRUE(selection.uses_default_water);
  EXPECT_EQ(selection.water, key("000018:Fallout4.esm"));
  ASSERT_NE(selection.record, nullptr);
  EXPECT_FALSE(selection.height.has_value());
}
