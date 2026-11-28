#include "fo4/environment/world.hpp"
#include "env_node_fixture.hpp"
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

using namespace fo4::test::envnode;

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

TEST_F(WaterEdgeTest, DeletedWinningWorldspaceInputTakesNoWaterOrHeight) {
  paths_ = {write("Fallout4.esm", make_plugin({}, {top("WATR", {rec("WATR", 0x840)}),
                  top("WRLD", {rec("WRLD", 0x800, {{"NAM2", bytes_of({0x840})}, {"DNAM", floats({-2048, 450})}})})})),
            write("Override.esp", make_plugin({"Fallout4.esm"}, {top("WRLD", {rec("WRLD", 0x800, {}, 0x20)})}))};
  const auto env = fo4::env::Environment::load(paths_);
  ASSERT_TRUE(env.metadata(*env.winner(key("000800:Fallout4.esm"))).deleted);
  const auto selection = fo4::env::resolve_water(env, *env.winner(key("000800:Fallout4.esm")));
  EXPECT_TRUE(selection.uses_default_water);
  EXPECT_EQ(selection.water, key("000018:Fallout4.esm"));
  EXPECT_FALSE(selection.height.has_value());
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
