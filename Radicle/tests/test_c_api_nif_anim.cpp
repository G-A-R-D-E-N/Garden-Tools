#include "fo4/c_api_nif_anim.h"

#include "fo4/c_api.h"
#include "fo4/c_api_nif.h"
#include "nif_build.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

class AnimHandle {
 public:
  explicit AnimHandle(const std::vector<std::byte>& bytes) {
    const auto* data = reinterpret_cast<const uint8_t*>(bytes.data());
    EXPECT_EQ(radicle_nif_anim_read(data, bytes.size(), &anim_), RADICLE_STATUS_OK) << radicle_last_error();
    EXPECT_NE(anim_, nullptr);
  }
  ~AnimHandle() {
    if (anim_ != nullptr) radicle_nif_anim_destroy(anim_);
  }
  radicle_nif_anim* get() const { return anim_; }

 private:
  radicle_nif_anim* anim_ = nullptr;
};

std::string anim_text(radicle_nif_anim* anim, size_t index) {
  uint8_t* bytes = nullptr;
  size_t size = 0;
  EXPECT_EQ(radicle_nif_anim_sequence_name(anim, index, &bytes, &size), RADICLE_STATUS_OK);
  std::string out(reinterpret_cast<const char*>(bytes), size);
  radicle_free_bytes(bytes);
  return out;
}

nif_build::Block sequence_block() {
  nif_build::Writer w;
  w.i32(0);
  w.u32(1);
  w.u32(1);
  w.i32(1);
  w.i32(3);
  w.u8(10);
  w.i32(1);
  w.i32(2);
  w.i32(3);
  w.i32(4);
  w.i32(5);
  w.f32(1.0f);
  w.i32(4);
  w.u32(2);
  w.f32(1.0f);
  w.f32(0.0f);
  w.f32(2.5f);
  w.i32(-1);
  w.i32(-1);
  w.u16(0);
  return {"NiControllerSequence", w.bytes()};
}

nif_build::Block interpolator_block() {
  nif_build::Writer w;
  w.f32(1.0f);
  w.f32(2.0f);
  w.f32(3.0f);
  w.f32(1.0f);
  w.f32(0.0f);
  w.f32(0.0f);
  w.f32(0.0f);
  w.f32(1.0f);
  w.i32(2);
  return {"NiTransformInterpolator", w.bytes()};
}

nif_build::Block transform_data_block() {
  nif_build::Writer w;
  w.u32(2);
  w.u32(1);
  w.f32(0.0f);
  w.f32(1.0f);
  w.f32(0.0f);
  w.f32(0.0f);
  w.f32(0.0f);
  w.f32(2.5f);
  w.f32(1.0f);
  w.f32(0.0f);
  w.f32(0.0f);
  w.f32(0.0f);
  w.u32(2);
  w.u32(1);
  w.f32(0.0f);
  w.f32(1.0f);
  w.f32(2.0f);
  w.f32(3.0f);
  w.f32(2.5f);
  w.f32(4.0f);
  w.f32(5.0f);
  w.f32(6.0f);
  w.u32(2);
  w.u32(1);
  w.f32(0.0f);
  w.f32(1.0f);
  w.f32(2.5f);
  w.f32(2.0f);
  return {"NiTransformData", w.bytes()};
}

nif_build::Block controller_block() {
  nif_build::Writer w;
  w.i32(-1);
  w.u16(0);
  w.f32(1.0f);
  w.f32(0.0f);
  w.f32(0.0f);
  w.f32(0.0f);
  w.i32(-1);
  w.i32(1);
  return {"NiTransformController", w.bytes()};
}

nif_build::Block text_keys_block() {
  nif_build::Writer w;
  w.i32(6);
  w.u32(1);
  w.f32(1.25f);
  w.i32(9);
  return {"NiTextKeyExtraData", w.bytes()};
}

nif_build::Block graph_block() {
  nif_build::Writer w;
  w.i32(7);
  w.i32(8);
  w.u8(1);
  return {"BSBehaviorGraphExtraData", w.bytes()};
}

nif_build::Block manager_block() {
  nif_build::Writer w;
  w.i32(-1);
  w.u16(0);
  for (int i = 0; i < 4; ++i) w.f32(1.0f);
  w.i32(-1);
  w.u8(1);
  w.u32(1);
  w.i32(0);
  w.i32(-1);
  return {"NiControllerManager", w.bytes()};
}

std::vector<std::byte> anim_bytes(bool with_manager = false) {
  std::vector<nif_build::Block> blocks{
      sequence_block(), interpolator_block(), transform_data_block(), controller_block(), text_keys_block(),
      graph_block(),
  };
  if (with_manager) blocks.push_back(manager_block());
  return nif_build::build_nif({"SeqName", "Node", "Prop", "CtrlType", "CtrlId", "InterpId", "TK", "GN", "graph.hkx", "Open"},
                              blocks, {0});
}

// Invariant: the sequence table must keep node name and priority for every controlled block.
TEST(CapiNifAnim, ReadsSequenceControlledBlocksAndKeys) {
  AnimHandle anim(anim_bytes());
  size_t count = 0;
  ASSERT_EQ(radicle_nif_anim_sequence_count(anim.get(), &count), RADICLE_STATUS_OK);
  EXPECT_EQ(count, 1U);
  EXPECT_EQ(anim_text(anim.get(), 0), "SeqName");
  radicle_nif_anim_sequence seq{};
  ASSERT_EQ(radicle_nif_anim_read_sequence(anim.get(), 0, &seq), RADICLE_STATUS_OK);
  EXPECT_EQ(seq.block, 0U);
  EXPECT_EQ(seq.controlled_count, 1U);
  EXPECT_EQ(seq.cycle, 2U);
  EXPECT_FLOAT_EQ(seq.frequency, 1.0f);
  EXPECT_FLOAT_EQ(seq.start_time, 0.0f);
  EXPECT_FLOAT_EQ(seq.stop_time, 2.5f);
  radicle_nif_anim_controlled entry{};
  ASSERT_EQ(radicle_nif_anim_read_controlled(anim.get(), 0, 0, &entry), RADICLE_STATUS_OK);
  EXPECT_EQ(entry.interpolator_ref, 1);
  EXPECT_EQ(entry.controller_ref, 3);
  EXPECT_EQ(entry.priority, 10);
  uint8_t* node = nullptr;
  size_t node_size = 0;
  ASSERT_EQ(radicle_nif_anim_controlled_node(anim.get(), 0, 0, &node, &node_size), RADICLE_STATUS_OK);
  EXPECT_EQ(std::string(reinterpret_cast<const char*>(node), node_size), "Node");
  radicle_free_bytes(node);
}

// Detects: the manager read folding block, cumulative and the sequence list into one struct must keep each field and the list.
TEST(CapiNifAnim, ReadsControllerManagers) {
  AnimHandle anim(anim_bytes(true));
  size_t count = 0;
  ASSERT_EQ(radicle_nif_anim_manager_count(anim.get(), &count), RADICLE_STATUS_OK);
  ASSERT_EQ(count, 1U);
  radicle_nif_anim_manager manager{};
  ASSERT_EQ(radicle_nif_anim_read_manager(anim.get(), 0, &manager), RADICLE_STATUS_OK);
  EXPECT_EQ(manager.block, 6U);
  EXPECT_EQ(manager.cumulative, 1);
  ASSERT_EQ(manager.sequence_count, 1U);
  ASSERT_NE(manager.sequences, nullptr);
  EXPECT_EQ(manager.sequences[0], 0U);
  EXPECT_EQ(radicle_nif_anim_read_manager(anim.get(), 1, &manager), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_nif_anim_read_manager(anim.get(), 0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_nif_anim_read_manager(nullptr, 0, &manager), RADICLE_STATUS_INVALID_ARGUMENT);
}

// Invariant: text keys must keep time and event name, and the graph must keep its file.
TEST(CapiNifAnim, ReadsTextKeysAndBehaviourGraph) {
  AnimHandle anim(anim_bytes());
  size_t key_count = 0;
  ASSERT_EQ(radicle_nif_anim_text_key_count(anim.get(), 4, &key_count), RADICLE_STATUS_OK);
  ASSERT_EQ(key_count, 1U);
  radicle_nif_anim_text_key key{};
  ASSERT_EQ(radicle_nif_anim_read_text_key(anim.get(), 4, 0, &key), RADICLE_STATUS_OK);
  EXPECT_FLOAT_EQ(key.time, 1.25f);
  uint8_t* text = nullptr;
  size_t text_size = 0;
  ASSERT_EQ(radicle_nif_anim_text_key_text(anim.get(), 4, 0, &text, &text_size), RADICLE_STATUS_OK);
  EXPECT_EQ(std::string(reinterpret_cast<const char*>(text), text_size), "Open");
  radicle_free_bytes(text);
  size_t graph_count = 0;
  ASSERT_EQ(radicle_nif_anim_graph_count(anim.get(), &graph_count), RADICLE_STATUS_OK);
  ASSERT_EQ(graph_count, 1U);
  radicle_nif_anim_graph graph{};
  ASSERT_EQ(radicle_nif_anim_read_graph(anim.get(), 0, &graph), RADICLE_STATUS_OK);
  EXPECT_EQ(graph.block, 5U);
  EXPECT_EQ(graph.controls_base_skeleton, 1);
  uint8_t* file = nullptr;
  size_t file_size = 0;
  ASSERT_EQ(radicle_nif_anim_graph_file(anim.get(), 0, &file, &file_size), RADICLE_STATUS_OK);
  EXPECT_EQ(std::string(reinterpret_cast<const char*>(file), file_size), "graph.hkx");
  radicle_free_bytes(file);
}

// Invariant: evaluate must lerp translation between the bracketing keys at time t.
TEST(CapiNifAnim, EvaluatesSequenceAtTimeT) {
  AnimHandle anim(anim_bytes());
  size_t count = 0;
  ASSERT_EQ(radicle_nif_anim_evaluate_count(anim.get(), 0, 1.25f, &count), RADICLE_STATUS_OK);
  ASSERT_EQ(count, 1U);
  uint8_t* node = nullptr;
  size_t node_size = 0;
  ASSERT_EQ(radicle_nif_anim_evaluate_node(anim.get(), 0, 1.25f, 0, &node, &node_size), RADICLE_STATUS_OK);
  EXPECT_EQ(std::string(reinterpret_cast<const char*>(node), node_size), "Node");
  radicle_free_bytes(node);
  radicle_nif_anim_pose pose{};
  ASSERT_EQ(radicle_nif_anim_evaluate_pose(anim.get(), 0, 1.25f, 0, &pose), RADICLE_STATUS_OK);
  EXPECT_FLOAT_EQ(pose.translation[0], 2.5f);
  EXPECT_FLOAT_EQ(pose.translation[1], 3.5f);
  EXPECT_FLOAT_EQ(pose.translation[2], 4.5f);
  EXPECT_FLOAT_EQ(pose.scale, 1.5f);
}

nif_build::Block value_interpolator(std::vector<float> pose, std::int32_t data, const char* type) {
  nif_build::Writer w;
  for (const float value : pose) w.f32(value);
  w.i32(data);
  return {type, w.bytes()};
}

nif_build::Block value_data(const char* type, std::uint32_t interpolation, const std::vector<std::vector<float>>& keys) {
  nif_build::Writer w;
  w.u32(static_cast<std::uint32_t>(keys.size()));
  if (!keys.empty()) w.u32(interpolation);
  for (const auto& key : keys) {
    for (const float value : key) w.f32(value);
  }
  return {type, w.bytes()};
}

std::vector<std::byte> value_key_bytes() {
  std::vector<nif_build::Block> blocks{
      value_interpolator({0.5f}, 1, "NiFloatInterpolator"),
      value_data("NiFloatData", 2, {{0.0f, 1.0f, 0.1f, 0.2f}, {1.0f, 2.0f, 0.3f, 0.4f}}),
      value_interpolator({1.0f, 2.0f, 3.0f}, 3, "NiPoint3Interpolator"),
      value_data("NiPosData", 1, {{0.0f, 1.0f, 2.0f, 3.0f}, {2.0f, 4.0f, 5.0f, 6.0f}}),
      value_interpolator({9.0f, 8.0f, 7.0f}, -1, "NiPoint3Interpolator"),
      value_interpolator({0.0f, 0.0f, 0.0f}, 6, "NiPoint3Interpolator"),
      value_data("NiPosData", 3, {{0.5f, 7.0f, 8.0f, 9.0f, 0.1f, 0.2f, 0.3f}}),
      value_interpolator({0.0f}, 3, "NiFloatInterpolator"),
  };
  return nif_build::build_nif({}, blocks, {});
}

// Invariant: float and point3 interpolators must expose their key values, tangents and TBC through the C ABI.
TEST(CapiNifAnim, ReadsFloatAndPoint3ValueKeys) {
  AnimHandle anim(value_key_bytes());
  radicle_nif_anim_value_key_info info{};
  radicle_nif_anim_value_key key{};

  ASSERT_EQ(radicle_nif_anim_read_value_keys(anim.get(), 0, &info), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(info.components, 1U);
  EXPECT_EQ(info.interpolation, 2U);
  EXPECT_EQ(info.data_ref, 1);
  EXPECT_FLOAT_EQ(info.pose[0], 0.5f);
  ASSERT_EQ(info.key_count, 2U);
  ASSERT_EQ(radicle_nif_anim_read_value_key(anim.get(), 0, 1, &key), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_FLOAT_EQ(key.time, 1.0f);
  EXPECT_FLOAT_EQ(key.value[0], 2.0f);
  EXPECT_FLOAT_EQ(key.value[1], 0.0f);
  EXPECT_FLOAT_EQ(key.forward[0], 0.3f);
  EXPECT_FLOAT_EQ(key.backward[0], 0.4f);

  ASSERT_EQ(radicle_nif_anim_read_value_keys(anim.get(), 2, &info), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(info.components, 3U);
  EXPECT_EQ(info.interpolation, 1U);
  ASSERT_EQ(info.key_count, 2U);
  ASSERT_EQ(radicle_nif_anim_read_value_key(anim.get(), 2, 1, &key), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_FLOAT_EQ(key.time, 2.0f);
  EXPECT_FLOAT_EQ(key.value[0], 4.0f);
  EXPECT_FLOAT_EQ(key.value[1], 5.0f);
  EXPECT_FLOAT_EQ(key.value[2], 6.0f);
  EXPECT_FLOAT_EQ(key.forward[0], 0.0f);
  EXPECT_FLOAT_EQ(key.tbc[0], 0.0f);

  ASSERT_EQ(radicle_nif_anim_read_value_keys(anim.get(), 4, &info), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(info.components, 3U);
  EXPECT_EQ(info.interpolation, 0U);
  EXPECT_EQ(info.data_ref, -1);
  EXPECT_EQ(info.key_count, 0U);
  EXPECT_FLOAT_EQ(info.pose[0], 9.0f);
  EXPECT_FLOAT_EQ(info.pose[2], 7.0f);
  EXPECT_EQ(radicle_nif_anim_read_value_key(anim.get(), 4, 0, &key), RADICLE_STATUS_INVALID_ARGUMENT);

  ASSERT_EQ(radicle_nif_anim_read_value_key(anim.get(), 5, 0, &key), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_FLOAT_EQ(key.time, 0.5f);
  EXPECT_FLOAT_EQ(key.value[2], 9.0f);
  EXPECT_FLOAT_EQ(key.tbc[0], 0.1f);
  EXPECT_FLOAT_EQ(key.tbc[1], 0.2f);
  EXPECT_FLOAT_EQ(key.tbc[2], 0.3f);
  EXPECT_EQ(radicle_nif_anim_read_value_key(anim.get(), 2, 2, &key), RADICLE_STATUS_INVALID_ARGUMENT);
}

using KeyRows = std::vector<std::vector<float>>;

void write_rows(nif_build::Writer& w, std::uint32_t type, const KeyRows& rows) {
  w.u32(static_cast<std::uint32_t>(rows.size()));
  if (rows.empty()) return;
  w.u32(type);
  for (const auto& row : rows) {
    for (const float value : row) w.f32(value);
  }
}

radicle_nif_anim_pose keyed_pose(std::uint32_t rotation_type, const KeyRows& rotation, std::uint32_t translation_type,
                                 const KeyRows& translation, std::uint32_t scale_type, const KeyRows& scale, float time) {
  nif_build::Writer w;
  write_rows(w, rotation_type, rotation);
  write_rows(w, translation_type, translation);
  write_rows(w, scale_type, scale);
  std::vector<nif_build::Block> blocks{
      sequence_block(), interpolator_block(), {"NiTransformData", w.bytes()}, controller_block(), text_keys_block(),
      graph_block(),
  };
  AnimHandle anim(nif_build::build_nif(
      {"SeqName", "Node", "Prop", "CtrlType", "CtrlId", "InterpId", "TK", "GN", "graph.hkx", "Open"}, blocks, {0}));
  radicle_nif_anim_pose pose{};
  EXPECT_EQ(radicle_nif_anim_evaluate_pose(anim.get(), 0, time, 0, &pose), RADICLE_STATUS_OK) << radicle_last_error();
  return pose;
}

// Invariant: a quadratic key segment is a cubic Hermite in per-segment units, from the left key's backward tangent to the right key's forward tangent; detects tangents applied per unit time instead of per segment, forward and backward swapped, or ends not clamped.
TEST(CapiNifAnim, EvaluatesQuadraticKeysAsPerSegmentHermite) {
  const float unused = 777.0f;
  const KeyRows scale{{0.0f, 0.0f, unused, 0.0f}, {1.0f, 1.0f, 3.0f, 6.0f}, {3.0f, 27.0f, 54.0f, unused}};
  const KeyRows move{{0.0f, 0.0f, 10.0f, -4.0f, unused, unused, unused, 8.0f, 0.0f, 16.0f},
                     {2.0f, 4.0f, 10.0f, 8.0f, 0.0f, 0.0f, 8.0f, unused, unused, unused}};
  const auto at = [&](float time) { return keyed_pose(1, {}, 2, move, 2, scale, time); };

  const auto mid = at(1.0f);
  EXPECT_NEAR(mid.translation[0], 3.0f, 1e-5f);
  EXPECT_NEAR(mid.translation[1], 10.0f, 1e-5f);
  EXPECT_NEAR(mid.translation[2], 3.0f, 1e-5f);
  EXPECT_NEAR(mid.scale, 1.0f, 1e-6f);
  EXPECT_NEAR(at(0.5f).scale, 0.125f, 1e-5f);
  EXPECT_NEAR(at(2.0f).scale, 8.0f, 1e-4f);
  EXPECT_NEAR(at(2.5f).scale, 15.625f, 1e-4f);
  EXPECT_FLOAT_EQ(at(-1.0f).scale, 0.0f);
  EXPECT_FLOAT_EQ(at(9.0f).scale, 27.0f);
}

// Invariant: TBC with all three words zero is non-uniform Catmull-Rom, and the words are read as tension, continuity, bias (Gamebryo stream order, not nif.xml); detects a wrong tangent formula, end-key tangents, time scaling on uneven spacing, or the second and third word swapped.
TEST(CapiNifAnim, EvaluatesTbcKeysAsKochanekBartels) {
  const float t0[3] = {0.0f, 0.0f, 0.0f};
  const auto row = [&](float time, float x, const float* tbc) {
    return std::vector<float>{time, x, 0.0f, 0.0f, tbc[0], tbc[1], tbc[2]};
  };
  const auto move = [&](float time, KeyRows rows) { return keyed_pose(1, {}, 3, rows, 1, {}, time).translation[0]; };

  const KeyRows uniform{row(0.0f, 0.0f, t0), row(1.0f, 1.0f, t0), row(2.0f, 4.0f, t0), row(3.0f, 9.0f, t0)};
  EXPECT_NEAR(move(1.5f, uniform), 2.25f, 1e-5f);
  EXPECT_NEAR(move(0.5f, uniform), 0.375f, 1e-5f);
  EXPECT_NEAR(move(2.5f, uniform), 6.375f, 1e-5f);
  const KeyRows uneven{row(0.0f, 0.0f, t0), row(1.0f, 1.0f, t0), row(3.0f, 9.0f, t0), row(4.0f, 16.0f, t0)};
  EXPECT_NEAR(move(2.0f, uneven), 4.5f, 1e-5f);

  const float tension[3] = {1.0f, 0.0f, 0.0f};
  const float continuity[3] = {0.0f, 1.0f, 0.0f};
  const float bias[3] = {0.0f, 0.0f, 1.0f};
  const auto three = [&](const float* middle) { return KeyRows{row(0.0f, 0.0f, t0), row(1.0f, 1.0f, middle), row(2.0f, 4.0f, t0)}; };
  EXPECT_NEAR(move(0.5f, three(t0)), 0.375f, 1e-5f);
  EXPECT_NEAR(move(0.5f, three(tension)), 0.625f, 1e-5f);
  EXPECT_NEAR(move(0.5f, three(bias)), 0.5f, 1e-5f);
  EXPECT_NEAR(move(0.5f, three(continuity)), 0.25f, 1e-5f);
}

// Invariant: vanilla hold-then-step TBC keys (GammaGun: value 1 held, words (0,-1,0) on the key before the step) stay flat through the hold; detects the nif.xml word order, which treats the second word as bias and bulges the hold by about 0.14.
TEST(CapiNifAnim, EvaluatesTbcHoldSegmentsWithoutBulging) {
  const auto row = [](float time, float x, float t, float c, float b) { return std::vector<float>{time, x, 0.0f, 0.0f, t, c, b}; };
  const KeyRows rows{row(0.0f, 1.0f, 0.0f, 0.0f, 0.0f), row(0.7f, 1.0f, 0.0f, -1.0f, 0.0f), row(0.7333f, 0.4f, -1.0f, -1.0f, 0.0f),
                     row(2.0f, 0.4f, 0.0f, 0.0f, 0.0f)};
  for (const float time : {0.2f, 0.35f, 0.6f}) EXPECT_NEAR(keyed_pose(1, {}, 3, rows, 1, {}, time).translation[0], 1.0f, 1e-5f);
  EXPECT_NEAR(keyed_pose(1, {}, 3, rows, 1, {}, 1.2f).translation[0], 0.4f, 1e-5f);
}

// Invariant: a Const segment holds the left key until the next key's time and never switches at the midpoint (Gamebryo step key); detects a midpoint switch or interpolation, on the vec3 and the float channel.
TEST(CapiNifAnim, ConstKeysHoldTheLeftValueUntilTheNextKey) {
  const KeyRows move{{0.0f, 1.0f, 2.0f, 3.0f}, {2.0f, 5.0f, 6.0f, 7.0f}};
  const KeyRows scale{{0.0f, 4.0f}, {2.0f, 8.0f}};
  for (const float time : {0.5f, 1.0f, 1.5f, 1.999f}) {
    const auto pose = keyed_pose(1, {}, 5, move, 5, scale, time);
    EXPECT_FLOAT_EQ(pose.translation[0], 1.0f) << time;
    EXPECT_FLOAT_EQ(pose.translation[2], 3.0f) << time;
    EXPECT_FLOAT_EQ(pose.scale, 4.0f) << time;
  }
  const auto at_key = keyed_pose(1, {}, 5, move, 5, scale, 2.0f);
  EXPECT_FLOAT_EQ(at_key.translation[0], 5.0f);
  EXPECT_FLOAT_EQ(at_key.scale, 8.0f);
}

// Invariant: a float TBC channel (scale) goes through the same tangents as a vector channel; detects the float path skipping tangents, or reading the TBC tail at the wrong offset.
TEST(CapiNifAnim, EvaluatesFloatTbcKeys) {
  const KeyRows scale{{0.0f, 0.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f, 0.0f, 0.0f}, {3.0f, 9.0f, 0.0f, 0.0f, 0.0f},
                      {4.0f, 16.0f, 0.0f, 0.0f, 0.0f}};
  EXPECT_NEAR(keyed_pose(1, {}, 1, {}, 3, scale, 2.0f).scale, 4.5f, 1e-5f);
}

// Invariant: quadratic and TBC quaternion keys sample as shortest-arc slerp; nif.xml gives quaternion keys no tangents; detects a squad or TBC rotation path being switched on without a game comparison.
TEST(CapiNifAnim, QuaternionQuadraticAndTbcKeysSampleAsSlerp) {
  const float c = 0.70710678f;
  const float tbc[3] = {0.7f, -0.3f, 0.2f};
  const KeyRows quadratic{{0.0f, 1.0f, 0.0f, 0.0f, 0.0f}, {1.0f, c, 0.0f, 0.0f, c}, {2.0f, 0.0f, 0.0f, 0.0f, 1.0f}};
  KeyRows tbc_rows;
  for (const auto& row : quadratic) {
    auto copy = row;
    copy.insert(copy.end(), tbc, tbc + 3);
    tbc_rows.push_back(copy);
  }
  for (const auto& pose : {keyed_pose(2, quadratic, 1, {}, 1, {}, 0.5f), keyed_pose(3, tbc_rows, 1, {}, 1, {}, 0.5f)}) {
    EXPECT_NEAR(pose.rotation[0], 0.92387953f, 1e-5f);
    EXPECT_NEAR(pose.rotation[1], 0.0f, 1e-5f);
    EXPECT_NEAR(pose.rotation[2], 0.0f, 1e-5f);
    EXPECT_NEAR(pose.rotation[3], 0.38268343f, 1e-5f);
  }
}

// Invariant: value keys must refuse a block that is not a float or point3 interpolator and a data block of the wrong kind.
TEST(CapiNifAnim, RejectsValueKeysOnOtherBlocks) {
  AnimHandle anim(value_key_bytes());
  radicle_nif_anim_value_key_info info{};
  radicle_nif_anim_value_key key{};
  EXPECT_EQ(radicle_nif_anim_read_value_keys(anim.get(), 1, &info), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(radicle_nif_anim_read_value_keys(anim.get(), 7, &info), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(radicle_nif_anim_read_value_keys(anim.get(), 99, &info), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(radicle_nif_anim_read_value_keys(nullptr, 0, &info), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_nif_anim_read_value_keys(anim.get(), 0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_nif_anim_read_value_key(anim.get(), 0, 0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_nif_anim_read_value_key(anim.get(), 7, 0, &key), RADICLE_STATUS_PARSE_ERROR);
}

nif_build::Block light_block(const char* type, std::int32_t name, const fo4::nif::Transform& xf, std::uint32_t flags,
                             const std::vector<float>& payload) {
  nif_build::Writer w;
  w.i32(name);
  w.u32(0);
  w.i32(-1);
  w.u32(flags);
  w.transform(xf);
  w.i32(-1);
  for (const float value : payload) w.f32(value);
  return {type, w.bytes()};
}

const std::vector<float> k_point_payload{4.5f,  0.1f,  0.2f,  0.3f,  0.4f, 0.5f, 0.6f,
                                         70.0f, 80.0f, 90.0f, 0.25f, 1.5f, 2.5f};
const std::vector<float> k_base_payload{0.75f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f};

radicle_nif* read_light_nif(const std::vector<nif_build::Block>& blocks, const std::vector<std::string>& names) {
  const auto bytes = nif_build::build_nif(names, blocks, {0});
  radicle_nif* nif = nullptr;
  EXPECT_EQ(radicle_nif_read(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), &nif), RADICLE_STATUS_OK)
      << radicle_last_error();
  return nif;
}

std::string light_text(radicle_status (*read)(const radicle_nif*, size_t, uint8_t**, size_t*), const radicle_nif* nif,
                       size_t index) {
  uint8_t* bytes = nullptr;
  size_t size = 0;
  EXPECT_EQ(read(nif, index, &bytes, &size), RADICLE_STATUS_OK) << radicle_last_error();
  std::string out(reinterpret_cast<const char*>(bytes), size);
  radicle_free_bytes(bytes);
  return out;
}

// Invariant: the light list must keep block order, name, parent, flags and local transform, and skip non-light blocks.
TEST(CapiNifLights, ListsLightBlocksWithTransformAndParent) {
  fo4::nif::Transform lamp;
  lamp.t[0] = 1.0f;
  lamp.t[1] = 2.0f;
  lamp.t[2] = 3.0f;
  lamp.s = 2.0f;
  std::vector<nif_build::Block> blocks{
      nif_build::node_block("NiNode", fo4::nif::Transform{}, {1}),
      light_block("NiPointLight", 1, lamp, 4, k_point_payload),
      light_block("NiDirectionalLight", 2, fo4::nif::Transform{}, 0, k_base_payload),
  };
  const auto bytes = nif_build::build_nif({"Root", "Lamp", "Sun"}, blocks, {0});
  radicle_nif* nif = nullptr;
  ASSERT_EQ(radicle_nif_read(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), &nif), RADICLE_STATUS_OK)
      << radicle_last_error();
  size_t count = 0;
  ASSERT_EQ(radicle_nif_light_count(nif, &count), RADICLE_STATUS_OK);
  ASSERT_EQ(count, 2U);
  radicle_nif_light light{};
  ASSERT_EQ(radicle_nif_read_light(nif, 0, &light), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(light.block, 1U);
  EXPECT_EQ(light.parent_block, 0);
  EXPECT_EQ(light.flags, 4U);
  EXPECT_FLOAT_EQ(light.translation[0], 1.0f);
  EXPECT_FLOAT_EQ(light.translation[1], 2.0f);
  EXPECT_FLOAT_EQ(light.translation[2], 3.0f);
  EXPECT_FLOAT_EQ(light.scale, 2.0f);
  EXPECT_EQ(light_text(radicle_nif_light_name, nif, 0), "Lamp");
  EXPECT_EQ(light_text(radicle_nif_light_type, nif, 0), "NiPointLight");
  ASSERT_EQ(radicle_nif_read_light(nif, 1, &light), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(light.block, 2U);
  EXPECT_EQ(light.parent_block, -1);
  EXPECT_EQ(light_text(radicle_nif_light_name, nif, 1), "Sun");
  EXPECT_EQ(light_text(radicle_nif_light_type, nif, 1), "NiDirectionalLight");
  EXPECT_EQ(radicle_nif_read_light(nif, 2, &light), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_nif_light_count(nullptr, &count), RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_nif_destroy(nif);
}

// Invariant: light data must keep the file order dimmer, ambient, diffuse, third triple, then attenuation on point and spot lights only; detects two fields swapping or an attenuation read on a directional light.
TEST(CapiNifLights, ReadsDimmerColoursAndAttenuationInFileOrder) {
  std::vector<nif_build::Block> blocks{
      nif_build::node_block("NiNode", fo4::nif::Transform{}, {1, 2}),
      light_block("NiPointLight", 1, fo4::nif::Transform{}, 0, k_point_payload),
      light_block("NiDirectionalLight", 2, fo4::nif::Transform{}, 0, k_base_payload),
  };
  radicle_nif* nif = read_light_nif(blocks, {"Root", "Lamp", "Sun"});
  ASSERT_NE(nif, nullptr);
  radicle_nif_light_data data{};
  ASSERT_EQ(radicle_nif_read_light_data(nif, 0, &data), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(data.has_colors, 1);
  EXPECT_FLOAT_EQ(data.dimmer, 4.5f);
  EXPECT_FLOAT_EQ(data.ambient_color[0], 0.1f);
  EXPECT_FLOAT_EQ(data.ambient_color[2], 0.3f);
  EXPECT_FLOAT_EQ(data.diffuse_color[0], 0.4f);
  EXPECT_FLOAT_EQ(data.diffuse_color[2], 0.6f);
  EXPECT_FLOAT_EQ(data.specular_or_radius[0], 70.0f);
  EXPECT_FLOAT_EQ(data.specular_or_radius[2], 90.0f);
  EXPECT_EQ(data.has_attenuation, 1);
  EXPECT_FLOAT_EQ(data.attenuation[0], 0.25f);
  EXPECT_FLOAT_EQ(data.attenuation[1], 1.5f);
  EXPECT_FLOAT_EQ(data.attenuation[2], 2.5f);
  ASSERT_EQ(radicle_nif_read_light_data(nif, 1, &data), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(data.has_colors, 1);
  EXPECT_FLOAT_EQ(data.dimmer, 0.75f);
  EXPECT_FLOAT_EQ(data.specular_or_radius[1], 8.0f);
  EXPECT_EQ(data.has_attenuation, 0);
  EXPECT_FLOAT_EQ(data.attenuation[0], 0.0f);
  EXPECT_EQ(radicle_nif_read_light_data(nif, 2, &data), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_nif_read_light_data(nif, 0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_nif_read_light_data(nullptr, 0, &data), RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_nif_destroy(nif);
}

// Invariant: a light block too short for its payload reports no colours and no attenuation and still loads; detects a truncated light block throwing out of the whole light list.
TEST(CapiNifLights, ShortLightBlocksReportNoColoursOrAttenuation) {
  const std::vector<float> no_attenuation(k_point_payload.begin(), k_point_payload.begin() + 10);
  const std::vector<float> truncated(k_point_payload.begin(), k_point_payload.begin() + 4);
  std::vector<nif_build::Block> blocks{
      nif_build::node_block("NiNode", fo4::nif::Transform{}, {1, 2}),
      light_block("NiPointLight", 1, fo4::nif::Transform{}, 0, no_attenuation),
      light_block("NiAmbientLight", 2, fo4::nif::Transform{}, 0, truncated),
  };
  radicle_nif* nif = read_light_nif(blocks, {"Root", "Lamp", "Ambient"});
  ASSERT_NE(nif, nullptr);
  radicle_nif_light_data data{};
  ASSERT_EQ(radicle_nif_read_light_data(nif, 0, &data), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(data.has_colors, 1);
  EXPECT_FLOAT_EQ(data.dimmer, 4.5f);
  EXPECT_EQ(data.has_attenuation, 0);
  ASSERT_EQ(radicle_nif_read_light_data(nif, 1, &data), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(data.has_colors, 0);
  EXPECT_FLOAT_EQ(data.dimmer, 0.0f);
  radicle_nif_destroy(nif);
}

// Invariant: bad handles, null outs and past-the-end indices must answer INVALID_ARGUMENT.
TEST(CapiNifAnim, RejectsBadAnimationInput) {
  AnimHandle anim(anim_bytes());
  size_t count = 0;
  EXPECT_EQ(radicle_nif_anim_sequence_count(nullptr, &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_nif_anim_sequence_count(anim.get(), nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_nif_anim_read_sequence(anim.get(), 9, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_nif_anim_sequence seq{};
  EXPECT_EQ(radicle_nif_anim_read_sequence(anim.get(), 9, &seq), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_nif_anim_evaluate_count(anim.get(), 99, 0.0f, &count), RADICLE_STATUS_PARSE_ERROR);
}
}  // namespace
