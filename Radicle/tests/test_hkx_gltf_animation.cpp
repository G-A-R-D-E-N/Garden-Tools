#include "fo4/hkx/gltf_animation.hpp"
#include "fo4/hkx/gltf_animation_save.hpp"
#include "fo4/hkx/layout.hpp"
#include "fo4/hkx/packfile.hpp"
#include "fo4/hkx/plain.hpp"
#include "fo4/hkx/write.hpp"
#include "gltf_import_builder.hpp"
#include "hkx_noop_check.hpp"
#include "hkx_test_fixture.hpp"

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using fo4::hkx::HkxGltfImport;
using fo4::hkx::HkxGltfOptions;
using fo4::hkx::HkxGltfStatus;
using fo4::hkx::HkxQsTransform;
using fo4::hkx::HkxSkeleton;
using fo4::hkx::import_gltf_animation;
using fo4::hkx::k_plain_units_per_metre;
using gltf_import_test::add_accessor;
using gltf_import_test::add_node;
using gltf_import_test::base64;
using gltf_import_test::bytes_of;
using gltf_import_test::empty_gltf;
using gltf_import_test::float_bytes;
using gltf_import_test::Gltf;
using gltf_import_test::k_float;
using gltf_import_test::to_glb;
using gltf_import_test::to_gltf_text;
using nlohmann::json;

namespace {

constexpr float k_units = k_plain_units_per_metre;
constexpr float k_sin45 = 0.70710678f;
constexpr double k_pi = 3.14159265358979323846;
constexpr std::size_t k_root = 0;
constexpr std::size_t k_com = 1;
constexpr std::size_t k_spine = 2;

HkxQsTransform pose(float x, float y, float z) {
  HkxQsTransform out;
  out.translation = {x, y, z};
  out.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
  out.scale = {1.0f, 1.0f, 1.0f};
  return out;
}

HkxSkeleton rig_of(std::size_t extra = 0) {
  HkxSkeleton out;
  out.name = "skeleton";
  out.bone_names = {"Root", "COM", "Spine1", "Head"};
  out.parent_indices = {-1, 0, 1, 2};
  out.reference_pose = {pose(0, 0, 0), pose(0, 0, 60), pose(0, 0, 5), pose(0, 0, 7)};
  for (std::size_t i = 0; i < extra; ++i) {
    out.bone_names.push_back("Extra" + std::to_string(i));
    out.parent_indices.push_back(0);
    out.reference_pose.push_back(pose(0, 0, 1));
  }
  return out;
}

struct Tiny {
  bool cubic{false};
  std::string com{"COM"};
  bool root_parents_com{true};
  bool armature_animated{false};
  bool two_animations{false};
  bool armature_y_up{false};
  float end_seconds{1.0f};
  std::vector<std::pair<float, std::string>> notes;
  bool root_half_turn{false};
  bool cubic_rotation{false};
  bool off_grid_peak{false};
  bool off_grid_step{false};
  std::optional<HkxQsTransform> com_rest;
  bool two_skins{false};
  bool targetless{false};
  bool zero_rotation_key{false};
  bool lamp{false};
  bool weights{false};
  std::function<void(Gltf&)> edit;
};

std::size_t scalar(Gltf& g, const std::vector<float>& v) {
  return add_accessor(g, k_float, v.size(), "SCALAR", float_bytes(v));
}
std::size_t vec3(Gltf& g, const std::vector<float>& v) {
  return add_accessor(g, k_float, v.size() / 3, "VEC3", float_bytes(v));
}
std::size_t vec4(Gltf& g, const std::vector<float>& v) {
  return add_accessor(g, k_float, v.size() / 4, "VEC4", float_bytes(v));
}

json sampler_of(std::size_t input, std::size_t output, const std::string& interpolation) {
  return {{"input", input}, {"output", output}, {"interpolation", interpolation}};
}

json channel_of(std::size_t sampler_index, int node, const std::string& path) {
  return {{"sampler", sampler_index}, {"target", {{"node", node}, {"path", path}}}};
}

Gltf tiny(const Tiny& o) {
  Gltf g = empty_gltf();
  const std::size_t times = scalar(g, {0.0f, o.end_seconds});
  const std::size_t root_translation = vec3(g, {0, 0, 0, 0, 0.1f, 0});
  const std::size_t half = scalar(g, {0.0f, 0.5f});
  std::vector<float> turn = o.root_half_turn ? std::vector<float>{k_sin45, 0.0f, 0.0f, k_sin45}
                                              : std::vector<float>{0.0f, k_sin45, 0.0f, k_sin45};
  if (o.zero_rotation_key) turn = {0.0f, 0.0f, 0.0f, 0.0f};
  std::vector<float> keys{0.0f, 0.0f, 0.0f, 1.0f};
  keys.insert(keys.end(), turn.begin(), turn.end());
  const std::size_t root_rotation = vec4(g, keys);

  std::size_t com_translation = o.cubic ? vec3(g, {0, 0, 0, 0, 0.2f, 0, 0, 0.4f, 0, 0, 0.4f, 0, 0, 0.4f, 0, 0, 0, 0})
                                        : vec3(g, {0, 0.2f, 0, 0, 0.4f, 0});
  std::size_t com_input = half;
  std::string com_interpolation = o.cubic ? "CUBICSPLINE" : "STEP";
  if (o.off_grid_peak) {
    com_input = scalar(g, {0.0f, 0.05f, 0.1f});
    com_translation = vec3(g, {0, 0, 0, 0, 0.05f, 0, 0, 0, 0});
    com_interpolation = "LINEAR";
  }
  if (o.off_grid_step) {
    com_input = scalar(g, {0.0f, 0.05f});
    com_translation = vec3(g, {0, 0, 0, 0, 0.05f, 0});
    com_interpolation = "STEP";
  }

  json samplers = json::array();
  samplers.push_back(sampler_of(times, root_translation, "LINEAR"));
  samplers.push_back(sampler_of(half, root_rotation, "STEP"));
  samplers.push_back(sampler_of(com_input, com_translation, com_interpolation));
  json channels = json::array();
  channels.push_back(channel_of(0, 0, "translation"));
  channels.push_back(channel_of(1, 0, "rotation"));
  channels.push_back(channel_of(2, 1, "translation"));
  if (o.cubic_rotation) {
    const float s60 = 0.8660254f, c60 = 0.5f;
    const std::size_t rotation =
        vec4(g, {0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0.5f, 0, 0, 0, -0.5f, 0, 0, 0, -s60, -c60, 0, 0, 0, 0});
    samplers.push_back(sampler_of(half, rotation, "CUBICSPLINE"));
    channels.push_back(channel_of(samplers.size() - 1, 1, "rotation"));
  }
  if (o.armature_animated) channels.push_back(channel_of(0, 3, "translation"));
  if (o.targetless) channels.push_back(json::object({{"sampler", 0}}));
  if (o.weights) channels.push_back(channel_of(0, 0, "weights"));
  if (o.lamp) channels.push_back(channel_of(0, o.two_skins ? 5 : 4, "translation"));

  json clip = {{"name", "tiny"}, {"samplers", samplers}, {"channels", channels}};
  if (!o.notes.empty()) {
    json list = json::array();
    for (const auto& note : o.notes) list.push_back(json{{"time", note.first}, {"text", note.second}});
    clip["extras"] = {{"annotations", list}};
  }
  g.json["animations"] = json::array();
  g.json["animations"].push_back(clip);
  if (o.two_animations) {
    json again = clip;
    again["name"] = "again";
    g.json["animations"].push_back(again);
  }

  json root = {{"name", "Root"}};
  if (o.root_parents_com) root["children"] = json::array({1});
  json com = {{"name", o.com}, {"children", json::array({2})}};
  if (o.com_rest) {
    const auto& rest = *o.com_rest;
    com["translation"] = json::array({rest.translation[0] * 0.0142875f, rest.translation[1] * 0.0142875f,
                                      rest.translation[2] * 0.0142875f});
    com["rotation"] = json::array({rest.rotation[0], rest.rotation[1], rest.rotation[2], rest.rotation[3]});
    com["scale"] = json::array({rest.scale[0], rest.scale[1], rest.scale[2]});
  }
  json armature = {{"name", "Armature"}, {"children", json::array({0})}};
  if (o.armature_y_up) {
    armature["rotation"] = json::array({-k_sin45, 0.0f, 0.0f, k_sin45});
  } else {
    armature["translation"] = json::array({0.0f, 0.5f, 0.0f});
  }
  add_node(g, root);
  add_node(g, com);
  add_node(g, json{{"name", "Spine1"}});
  add_node(g, armature);
  if (o.two_skins) add_node(g, json{{"name", "Prop"}});
  if (o.lamp) add_node(g, json{{"name", "Lamp"}});

  g.json["skins"] = json::array();
  if (o.two_skins) {
    g.json["skins"].push_back(json{{"name", "Armor"}, {"joints", json::array({0, 2})}});
    g.json["skins"].push_back(json{{"name", "Body"}, {"joints", json::array({0, 1, 2, 4})}});
  } else {
    g.json["skins"].push_back(json{{"joints", json::array({0, 1, 2})}});
  }
  if (o.edit) o.edit(g);
  return g;
}

std::vector<std::byte> serialise(Gltf& g, bool binary) { return binary ? to_glb(g) : bytes_of(to_gltf_text(g)); }

HkxGltfImport run(const Tiny& o, const HkxSkeleton& rig, HkxGltfStatus want, bool binary = false,
                  const HkxGltfOptions& options = {}) {
  Gltf g = tiny(o);
  HkxGltfImport out;
  const auto status = import_gltf_animation(serialise(g, binary), rig, options, out);
  EXPECT_EQ(status, want) << out.message;
  return out;
}

HkxGltfImport ok(const Tiny& o, bool binary = false, const HkxGltfOptions& options = {}) {
  return run(o, rig_of(), HkxGltfStatus::Ok, binary, options);
}

std::string refusal(const Tiny& o, HkxGltfStatus want) { return run(o, rig_of(), want).message; }

bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

const std::array<float, 3>& translation_at(const HkxGltfImport& r, std::size_t frame, std::size_t bone) {
  return r.animation.translations[frame * r.animation.track_count + bone];
}
const std::array<float, 4>& rotation_at(const HkxGltfImport& r, std::size_t frame, std::size_t bone) {
  return r.animation.rotations[frame * r.animation.track_count + bone];
}
const std::array<float, 3>& scale_at(const HkxGltfImport& r, std::size_t frame, std::size_t bone) {
  return r.animation.scales[frame * r.animation.track_count + bone];
}

std::array<double, 3> rotate_x_axis(const std::array<float, 4>& q) {
  const double x = q[0], y = q[1], z = q[2], w = q[3];
  return {1 - 2 * (y * y + z * z), 2 * (x * y + w * z), 2 * (x * z - w * y)};
}

using Matrix3 = std::array<double, 9>;

Matrix3 row_matrix(const std::array<double, 4>& q) {
  const double x = q[0], y = q[1], z = q[2], w = q[3];
  return {1 - 2 * (y * y + z * z), 2 * (x * y + w * z),     2 * (x * z - w * y),
          2 * (x * y - w * z),     1 - 2 * (z * z + x * x), 2 * (y * z + w * x),
          2 * (x * z + w * y),     2 * (y * z - w * x),     1 - 2 * (y * y + x * x)};
}

Matrix3 matrix_product(const Matrix3& a, const Matrix3& b) {
  Matrix3 out{};
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      for (int k = 0; k < 3; ++k) out[r * 3 + c] += a[r * 3 + k] * b[k * 3 + c];
    }
  }
  return out;
}

std::array<double, 4> normal(std::array<double, 4> q) {
  const double length = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
  for (double& value : q) value /= length;
  return q;
}

std::vector<std::byte> glb_of(const std::string& json_text, const std::vector<std::byte>& bin) {
  std::string text = json_text;
  while (text.size() % 4 != 0) text.push_back(' ');
  std::vector<std::byte> payload = bin;
  while (payload.size() % 4 != 0) payload.push_back(std::byte{0});
  std::vector<std::byte> out;
  const auto put = [&out](std::uint32_t value) {
    for (std::uint32_t shift = 0; shift < 32; shift += 8) out.push_back(static_cast<std::byte>(value >> shift));
  };
  put(0x46546C67U);
  put(2);
  put(static_cast<std::uint32_t>(12 + 8 + text.size() + 8 + payload.size()));
  put(static_cast<std::uint32_t>(text.size()));
  put(0x4E4F534AU);
  for (const char ch : text) out.push_back(static_cast<std::byte>(ch));
  put(static_cast<std::uint32_t>(payload.size()));
  put(0x004E4942U);
  out.insert(out.end(), payload.begin(), payload.end());
  return out;
}

std::string short_buffer_json(const std::string& buffer) {
  return "{\"asset\":{\"version\":\"2.0\"},\"nodes\":[{\"name\":\"Root\"}],"
         "\"animations\":[{\"samplers\":[{\"input\":0,\"output\":0,\"interpolation\":\"LINEAR\"}],"
         "\"channels\":[{\"sampler\":0,\"target\":{\"node\":0,\"path\":\"translation\"}}]}],"
         "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"SCALAR\"}],"
         "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":12}],\"buffers\":[" +
         buffer + "]}";
}

}  // namespace

TEST(HkxGltfAnimation, HandWrittenGltfImportsWithTheGltfToHavokConvention) {
  for (const bool binary : {false, true}) {
    const auto r = ok(Tiny{}, binary);
    const auto& animation = r.animation;

    ASSERT_EQ(animation.frame_count, 31u);
    ASSERT_EQ(animation.track_count, 4u);
    EXPECT_NEAR(animation.duration, 1.0f, 1e-5f);
    EXPECT_EQ(r.frame_rate, fo4::hkx::k_gltf_min_frame_rate);
    EXPECT_EQ(r.animated, (std::vector<std::string>{"Root", "COM"}));
    EXPECT_EQ(r.reference_only, (std::vector<std::string>{"Spine1", "Head"}));
    EXPECT_TRUE(r.unmapped.empty());
    EXPECT_EQ(r.rest_mismatch, (std::vector<std::string>{"Root", "COM"}));

    EXPECT_NEAR(translation_at(r, 0, k_root)[2], 0.5f * k_units, 1e-3f);
    EXPECT_NEAR(translation_at(r, 15, k_root)[2], 0.55f * k_units, 1e-3f);
    EXPECT_NEAR(translation_at(r, 30, k_root)[2], 0.6f * k_units, 1e-3f);
    const auto& mid = translation_at(r, 15, k_root);
    EXPECT_LT(mid[0] * mid[0] + mid[1] * mid[1], 1e-6f) << "root moved off the Havok Z axis";

    ASSERT_EQ(r.motion.samples.size(), 31u);
    EXPECT_NEAR(r.motion.samples[14][3], 0.0f, 1e-4f);
    EXPECT_NEAR(r.motion.samples[15][3], static_cast<float>(k_pi / 2), 1e-4f);
    for (const auto& sample : r.motion.samples) {
      EXPECT_LT(std::fabs(sample[0]) + std::fabs(sample[1]) + std::fabs(sample[2]), 1e-4f);
    }
    for (const std::size_t frame : {std::size_t{14}, std::size_t{15}}) {
      const auto x = rotate_x_axis(rotation_at(r, frame, k_root));
      EXPECT_NEAR(x[0], 1.0, 1e-4);
      EXPECT_NEAR(x[1], 0.0, 1e-4);
      EXPECT_NEAR(x[2], 0.0, 1e-4);
    }

    EXPECT_NEAR(translation_at(r, 14, k_com)[1], 0.2f * k_units, 1e-3f);
    EXPECT_NEAR(translation_at(r, 15, k_com)[1], 0.4f * k_units, 1e-3f);
    const auto& com = translation_at(r, 15, k_com);
    EXPECT_LT(com[0] * com[0] + com[2] * com[2], 1e-6f) << "a child bone was converted like the root";

    EXPECT_EQ(translation_at(r, 7, k_spine), (std::array<float, 3>{0.0f, 0.0f, 5.0f}));
    EXPECT_EQ(rotation_at(r, 7, k_spine), (std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}));
  }
}

TEST(HkxGltfAnimation, ResamplesCubicSplineKeysAlongTheirTangents) {
  Tiny options;
  options.cubic = true;
  const auto r = ok(options);

  EXPECT_NEAR(translation_at(r, 7, k_com)[1], (0.2f + 0.4f * 7 / 30.0f) * k_units, 1e-3f);
  EXPECT_NEAR(translation_at(r, 15, k_com)[1], 0.4f * k_units, 1e-3f);
  EXPECT_NEAR(translation_at(r, 30, k_com)[1], 0.4f * k_units, 1e-3f);
}

TEST(HkxGltfAnimation, EvaluatesCubicRotationKeysAsStoredInsteadOfShortestPath) {
  Tiny options;
  options.cubic_rotation = true;
  const auto r = ok(options);

  const double s = (7 / 30.0) / 0.5, s2 = s * s, s3 = s2 * s;
  const double h00 = 2 * s3 - 3 * s2 + 1, h10 = s3 - 2 * s2 + s, h01 = -2 * s3 + 3 * s2, h11 = s3 - s2;
  const std::array<double, 4> start{0, 0, 0, 1}, start_tangent{0, 0, 0.5, 0}, end_tangent{0, 0, -0.5, 0},
      end{0, 0, -0.8660254, -0.5};
  std::array<double, 4> spec{};
  for (std::size_t i = 0; i < 4; ++i) {
    spec[i] = h00 * start[i] + h10 * 0.5 * start_tangent[i] + h01 * end[i] + h11 * 0.5 * end_tangent[i];
  }
  spec = normal(spec);
  const auto& baked = rotation_at(r, 7, k_com);
  const double dot = spec[0] * baked[0] + spec[1] * baked[1] + spec[2] * baked[2] + spec[3] * baked[3];
  EXPECT_LT(1.0 - std::fabs(dot), 1e-5);
}

TEST(HkxGltfAnimation, RefusesAnAnimatedBoneTheSkeletonDoesNotHave) {
  Tiny options;
  options.com = "Helper";
  const auto message = refusal(options, HkxGltfStatus::MissingBone);
  EXPECT_TRUE(has(message, "does not have: Helper")) << message;
}

TEST(HkxGltfAnimation, BlenderArmatureYUpRotationCancelsTheRootConversion) {
  Tiny options;
  options.armature_y_up = true;
  const auto r = ok(options);

  const auto& q = rotation_at(r, 0, k_root);
  EXPECT_NEAR(std::fabs(q[3]), 1.0f, 1e-4f);
  EXPECT_NEAR(q[0], 0.0f, 1e-4f);
  EXPECT_NEAR(q[1], 0.0f, 1e-4f);
  EXPECT_NEAR(q[2], 0.0f, 1e-4f);
  const auto& t = translation_at(r, 0, k_root);
  EXPECT_LT(std::fabs(t[0]) + std::fabs(t[1]) + std::fabs(t[2]), 1e-3f);
}

TEST(HkxGltfAnimation, RefusesAParentThatDiffersFromTheSkeleton) {
  Tiny options;
  options.root_parents_com = false;
  const auto message = refusal(options, HkxGltfStatus::HierarchyMismatch);
  EXPECT_TRUE(has(message, "sits under nothing in the glTF but under Root")) << message;
}

TEST(HkxGltfAnimation, RefusesAnimatedNodeAboveTheBones) {
  Tiny options;
  options.armature_animated = true;
  const auto message = refusal(options, HkxGltfStatus::AnimatedNonBone);
  EXPECT_TRUE(has(message, "is not a bone")) << message;
}

TEST(HkxGltfAnimation, CountsWeightChannelsAndNonBoneChannelsWithoutBakingThem) {
  Tiny options;
  options.weights = true;
  options.lamp = true;
  const auto r = ok(options);
  EXPECT_EQ(r.weight_channels, 1u);
  EXPECT_EQ(r.non_bone_channels, 1u);
  EXPECT_EQ(r.animated, (std::vector<std::string>{"Root", "COM"}));
}

TEST(HkxGltfAnimation, RefusesMoreThanOneAnimationAndListsTheNames) {
  Tiny options;
  options.two_animations = true;
  const auto message = refusal(options, HkxGltfStatus::AmbiguousAnimation);
  EXPECT_TRUE(has(message, "2 animations (tiny, again)")) << message;
}

TEST(HkxGltfAnimation, ImportsTheNamedAnimationAndRefusesAMissingName) {
  Tiny options;
  options.two_animations = true;
  HkxGltfOptions named;
  named.animation = "again";
  EXPECT_EQ(ok(options, false, named).animation.frame_count, 31u);

  HkxGltfOptions missing;
  missing.animation = "missing";
  const auto r = run(options, rig_of(), HkxGltfStatus::NoAnimation, false, missing);
  EXPECT_TRUE(has(r.message, "no animation named 'missing'. It holds: tiny, again.")) << r.message;
}

TEST(HkxGltfAnimation, RefusesSeveralSkinsWithoutAChoiceAndListsThem) {
  Tiny options;
  options.two_skins = true;
  const auto message = refusal(options, HkxGltfStatus::AmbiguousSkin);
  EXPECT_TRUE(has(message, "holds 2 skins: 0 (Armor), 1 (Body).")) << message;
}

TEST(HkxGltfAnimation, ImportsTheChosenSkinsJointsAndNamesItsTracks) {
  Tiny options;
  options.two_skins = true;
  for (const char* choice : {"1", "Body"}) {
    HkxGltfOptions chosen;
    chosen.skin = choice;
    const auto r = ok(options, false, chosen);
    EXPECT_EQ(r.animated, (std::vector<std::string>{"Root", "COM"}));
    EXPECT_EQ(r.unmapped, (std::vector<std::string>{"Prop"}));
  }
}

TEST(HkxGltfAnimation, RefusesAnimatedJointThatBelongsOnlyToAnotherSkin) {
  Tiny options;
  options.two_skins = true;
  HkxGltfOptions chosen;
  chosen.skin = "Armor";
  const auto r = run(options, rig_of(), HkxGltfStatus::AnimatedNonBone, false, chosen);
  EXPECT_TRUE(has(r.message, "node 'COM' is animated and is a joint of another skin")) << r.message;
}

TEST(HkxGltfAnimation, RefusesASkinChoiceThatTwoSkinsShareByTheirRootJoint) {
  Tiny options;
  options.two_skins = true;
  HkxGltfOptions chosen;
  chosen.skin = "Root";
  const auto r = run(options, rig_of(), HkxGltfStatus::AmbiguousSkin, false, chosen);
  EXPECT_TRUE(has(r.message, "'Root' matches 2 skins, so the choice would be a guess")) << r.message;
}

TEST(HkxGltfAnimation, ReturnsAnnotationsSortedAndRefusesOneOutsideTheClip) {
  Tiny options;
  options.notes = {{0.5f, "HitFrame"}, {0.25f, "footstep"}};
  const auto r = ok(options);
  ASSERT_EQ(r.annotations.size(), 2u);
  EXPECT_EQ(r.annotations[0].text, "footstep");
  EXPECT_EQ(r.annotations[1].text, "HitFrame");
  EXPECT_NEAR(r.annotations[0].time, 0.25f, 1e-4f);

  Tiny late;
  late.notes = {{2.0f, "late"}};
  const auto message = refusal(late, HkxGltfStatus::BadAnnotation);
  EXPECT_TRUE(has(message, "'late' at 2.000 s is outside the clip")) << message;
}

TEST(HkxGltfAnimation, RefusesARootThatTurnsHalfATurnAboutAHorizontalAxis) {
  Tiny options;
  options.root_half_turn = true;
  const auto message = refusal(options, HkxGltfStatus::UnsupportedRoot);
  EXPECT_TRUE(has(message, "turns half a turn about a horizontal axis at frame 15")) << message;
}

TEST(HkxGltfAnimation, SplitsARootThatStartsAwayFromTheOriginAndTurns) {
  Tiny options;
  options.armature_y_up = true;
  std::vector<float> times, translations, rotations;
  std::vector<std::array<double, 4>> originals;
  for (int f = 0; f <= 30; ++f) {
    times.push_back(static_cast<float>(f) / 30.0f);
    translations.insert(translations.end(),
                        {5.0f + 0.3f * static_cast<float>(f), 3.0f + 0.2f * static_cast<float>(f), 0.25f});
    const double turn = 0.6 + 0.02 * f, tilt = 0.2;
    const std::array<double, 4> a{0, 0, std::sin(turn / 2), std::cos(turn / 2)};
    const std::array<double, 4> b{std::sin(tilt / 2), 0, 0, std::cos(tilt / 2)};
    const std::array<double, 4> combined{a[3] * b[0] + b[3] * a[0] + (a[1] * b[2] - a[2] * b[1]),
                                         a[3] * b[1] + b[3] * a[1] + (a[2] * b[0] - a[0] * b[2]),
                                         a[3] * b[2] + b[3] * a[2] + (a[0] * b[1] - a[1] * b[0]),
                                         a[3] * b[3] - (a[0] * b[0] + a[1] * b[1] + a[2] * b[2])};
    originals.push_back(normal(combined));
    for (const double value : originals.back()) rotations.push_back(static_cast<float>(value));
  }
  options.edit = [&](Gltf& g) {
    const auto input = scalar(g, times);
    const auto move = vec3(g, translations);
    const auto spin = vec4(g, rotations);
    json samplers = json::array();
    samplers.push_back(sampler_of(input, move, "LINEAR"));
    samplers.push_back(sampler_of(input, spin, "LINEAR"));
    json channels = json::array();
    channels.push_back(channel_of(0, 0, "translation"));
    channels.push_back(channel_of(1, 0, "rotation"));
    json clip = {{"name", "walk"}, {"samplers", samplers}, {"channels", channels}};
    g.json["animations"] = json::array();
    g.json["animations"].push_back(clip);
  };
  const auto r = ok(options);
  ASSERT_EQ(r.animation.frame_count, 31u);
  ASSERT_EQ(r.motion.samples.size(), 31u);
  EXPECT_EQ(r.motion.samples[0], (std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f}));

  for (std::size_t f = 0; f < 31; ++f) {
    const double relative = r.motion.samples[f][3];
    EXPECT_NEAR(relative, 0.02 * static_cast<double>(f), 1e-4);

    const auto& t = translation_at(r, f, k_root);
    const double world_x = t[0] * std::cos(relative) - t[1] * std::sin(relative) + r.motion.samples[f][0];
    const double world_y = t[0] * std::sin(relative) + t[1] * std::cos(relative) + r.motion.samples[f][1];
    EXPECT_NEAR(world_x, (5.0 + 0.3 * static_cast<double>(f)) * k_units, 1e-2) << "frame " << f;
    EXPECT_NEAR(world_y, (3.0 + 0.2 * static_cast<double>(f)) * k_units, 1e-2) << "frame " << f;
    EXPECT_NEAR(t[2], 0.25f * k_units, 1e-2) << "frame " << f;

    const auto& q = rotation_at(r, f, k_root);
    const auto placed = matrix_product(row_matrix({q[0], q[1], q[2], q[3]}),
                                       row_matrix({0.0, 0.0, std::sin(relative / 2), std::cos(relative / 2)}));
    const auto want = row_matrix(originals[f]);
    for (std::size_t i = 0; i < 9; ++i) EXPECT_NEAR(placed[i], want[i], 1e-4) << "frame " << f << " cell " << i;
  }
}

TEST(HkxGltfAnimation, RefusesAClipOverTheFrameTrackLimitBeforeBaking) {
  Tiny options;
  options.end_seconds = 1500.0f;
  Gltf g = tiny(options);
  HkxGltfImport out;
  EXPECT_EQ(import_gltf_animation(serialise(g, false), rig_of(100), {}, out), HkxGltfStatus::ClipTooLarge);
  EXPECT_TRUE(has(out.message, "4194304")) << out.message;
  EXPECT_TRUE(has(out.message, "45001 frame(s)")) << out.message;
  EXPECT_TRUE(out.animation.translations.empty());
}

TEST(HkxGltfAnimation, RefusesKeyTimesThatDoNotIncrease) {
  Tiny options;
  options.end_seconds = 0.0f;
  EXPECT_TRUE(has(refusal(options, HkxGltfStatus::BadKeyTimes), "key times do not increase"));
}

TEST(HkxGltfAnimation, RefusesAChannelWithoutATargetInsteadOfSkippingIt) {
  Tiny options;
  options.targetless = true;
  EXPECT_TRUE(has(refusal(options, HkxGltfStatus::Malformed), "no target"));
}

TEST(HkxGltfAnimation, RefusesANodeRotationWithNoDirectionInsteadOfIdentity) {
  for (const float w : {0.0f, 1e-7f}) {
    HkxQsTransform rest = pose(0, 0, 60);
    rest.rotation = {0.0f, 0.0f, 0.0f, w};
    Tiny options;
    options.com_rest = rest;
    EXPECT_TRUE(has(refusal(options, HkxGltfStatus::BadRotation), "rotation is zero, tiny or not finite"));
  }
}

TEST(HkxGltfAnimation, RefusesAZeroRotationKeyInsteadOfBakingIdentity) {
  Tiny options;
  options.zero_rotation_key = true;
  EXPECT_TRUE(has(refusal(options, HkxGltfStatus::BadRotation), "key 1 is zero"));
}

TEST(HkxGltfAnimation, NormalisesAValidNonUnitNodeRotationBeforeTheRestCheck) {
  const auto direction = normal({0.1, 0.2, 0.3, 0.9});
  HkxQsTransform rest = pose(0, 0, 60);
  rest.rotation = {static_cast<float>(direction[0]), static_cast<float>(direction[1]),
                   static_cast<float>(direction[2]), static_cast<float>(direction[3])};
  HkxQsTransform doubled = rest;
  for (float& value : doubled.rotation) value *= 2.0f;
  Tiny options;
  options.com_rest = doubled;
  HkxSkeleton rig = rig_of();
  rig.reference_pose[k_com] = rest;
  Gltf g = tiny(options);
  HkxGltfImport out;
  ASSERT_EQ(import_gltf_animation(serialise(g, false), rig, {}, out), HkxGltfStatus::Ok) << out.message;
  EXPECT_EQ(std::count(out.rest_mismatch.begin(), out.rest_mismatch.end(), "COM"), 0);
}

TEST(HkxGltfAnimation, RestScaleMismatchIsReportedAndTheSkeletonScaleIsKept) {
  HkxQsTransform rest = pose(0, 0, 60);
  HkxQsTransform doubled = rest;
  doubled.scale = {2.0f, 2.0f, 2.0f};
  HkxSkeleton rig = rig_of();
  rig.reference_pose[k_com] = rest;

  Tiny matching;
  matching.com_rest = rest;
  Gltf a = tiny(matching);
  HkxGltfImport same;
  ASSERT_EQ(import_gltf_animation(serialise(a, false), rig, {}, same), HkxGltfStatus::Ok) << same.message;
  EXPECT_EQ(std::count(same.rest_mismatch.begin(), same.rest_mismatch.end(), "COM"), 0);

  Tiny scaled;
  scaled.com_rest = doubled;
  Gltf b = tiny(scaled);
  HkxGltfImport differs;
  ASSERT_EQ(import_gltf_animation(serialise(b, false), rig, {}, differs), HkxGltfStatus::Ok) << differs.message;
  EXPECT_EQ(std::count(differs.rest_mismatch.begin(), differs.rest_mismatch.end(), "COM"), 1);
  EXPECT_EQ(scale_at(differs, 0, k_com), (std::array<float, 3>{1.0f, 1.0f, 1.0f}));
}

TEST(HkxGltfAnimation, ReportsSamplingLossForAnOffGridPeak) {
  Tiny options;
  options.off_grid_peak = true;
  const auto r = ok(options);
  EXPECT_NEAR(r.sampling_position, 0.05f / 3.0f * k_units, 0.05f);
}

TEST(HkxGltfAnimation, ReportsSamplingLossForAnOffGridStep) {
  Tiny options;
  options.off_grid_step = true;
  const auto r = ok(options);
  EXPECT_NEAR(r.sampling_position, 0.05f / 2.0f * k_units, 0.05f);
  EXPECT_NEAR(translation_at(r, 1, k_com)[1], 0.0f, 0.05f);
  EXPECT_NEAR(translation_at(r, 2, k_com)[1], 0.05f * k_units, 0.05f);
}

TEST(HkxGltfAnimation, RefusesASkeletonWithoutAReferencePoseForEachBone) {
  HkxSkeleton rig = rig_of();
  rig.reference_pose.clear();
  const auto r = run(Tiny{}, rig, HkxGltfStatus::InvalidSkeleton);
  EXPECT_TRUE(has(r.message, "reference pose")) << r.message;
}

TEST(HkxGltfAnimation, RefusesASkeletonThatNamesTwoBonesAlike) {
  HkxSkeleton rig = rig_of();
  rig.bone_names[3] = "Root";
  const auto r = run(Tiny{}, rig, HkxGltfStatus::InvalidSkeleton);
  EXPECT_TRUE(has(r.message, "names two bones 'Root'")) << r.message;
}

TEST(HkxGltfAnimation, RefusesUnnamedAndRepeatedJointNames) {
  Tiny unnamed;
  unnamed.edit = [](Gltf& g) { g.json["nodes"][2].erase("name"); };
  EXPECT_TRUE(has(refusal(unnamed, HkxGltfStatus::BadJoint), "has no name"));

  Tiny repeated;
  repeated.edit = [](Gltf& g) { g.json["nodes"][2]["name"] = "COM"; };
  EXPECT_TRUE(has(refusal(repeated, HkxGltfStatus::BadJoint), "two joints named 'COM'"));
}

TEST(HkxGltfAnimation, RefusesAFileWithoutAnAnimationOrASkin) {
  Tiny none;
  none.edit = [](Gltf& g) { g.json["animations"] = json::array(); };
  EXPECT_TRUE(has(refusal(none, HkxGltfStatus::NoAnimation), "holds no animation"));

  Tiny bare;
  bare.edit = [](Gltf& g) { g.json["skins"] = json::array(); };
  EXPECT_TRUE(has(refusal(bare, HkxGltfStatus::NoSkin), "has no skin"));
}

TEST(HkxGltfAnimation, RefusesAChannelThatDuplicatesAPath) {
  Tiny options;
  options.edit = [](Gltf& g) { g.json["animations"][0]["channels"].push_back(channel_of(0, 0, "translation")); };
  EXPECT_TRUE(has(refusal(options, HkxGltfStatus::DuplicateChannel), "two translation channels"));
}

TEST(HkxGltfAnimation, RefusesNonTrsChannelsAndUnknownInterpolation) {
  Tiny path;
  path.edit = [](Gltf& g) { g.json["animations"][0]["channels"][0]["target"]["path"] = "pointer"; };
  EXPECT_TRUE(has(refusal(path, HkxGltfStatus::UnsupportedChannel), "pointer"));

  Tiny interpolation;
  interpolation.edit = [](Gltf& g) { g.json["animations"][0]["samplers"][0]["interpolation"] = "BEZIER"; };
  EXPECT_TRUE(has(refusal(interpolation, HkxGltfStatus::UnsupportedChannel), "BEZIER"));
}

TEST(HkxGltfAnimation, RefusesSparseNormalizedAndNonFloatAccessorsByName) {
  Tiny sparse;
  sparse.edit = [](Gltf& g) { g.json["accessors"][1]["sparse"] = json::object({{"count", 1}}); };
  EXPECT_TRUE(has(refusal(sparse, HkxGltfStatus::SparseAccessor), "sparse"));

  Tiny normalized;
  normalized.edit = [](Gltf& g) { g.json["accessors"][1]["normalized"] = true; };
  EXPECT_TRUE(has(refusal(normalized, HkxGltfStatus::UnsupportedAccessor), "normalized"));

  Tiny integer;
  integer.edit = [](Gltf& g) { g.json["accessors"][1]["componentType"] = 5123; };
  EXPECT_TRUE(has(refusal(integer, HkxGltfStatus::UnsupportedAccessor), "32-bit floats"));

  Tiny wrong_type;
  wrong_type.edit = [](Gltf& g) { g.json["accessors"][1]["type"] = "VEC2"; };
  EXPECT_TRUE(has(refusal(wrong_type, HkxGltfStatus::UnsupportedAccessor), "VEC2"));

  Tiny no_view;
  no_view.edit = [](Gltf& g) { g.json["accessors"][1].erase("bufferView"); };
  EXPECT_TRUE(has(refusal(no_view, HkxGltfStatus::Malformed), "no usable buffer view"));
}

TEST(HkxGltfAnimation, RefusesMalformedJsonAsMalformed) {
  HkxGltfImport out;
  EXPECT_EQ(import_gltf_animation(bytes_of("{ \"nodes\": [ "), rig_of(), {}, out), HkxGltfStatus::Malformed);
  EXPECT_FALSE(out.message.empty());
}

TEST(HkxGltfAnimation, RefusesAViewThatRunsPastTheBufferDeclaredLength) {
  const std::vector<std::byte> bin(12, std::byte{0});
  const auto embedded = bytes_of(
      short_buffer_json("{\"byteLength\":10,\"uri\":\"data:application/octet-stream;base64," + base64(bin) + "\"}"));
  const auto binary = glb_of(short_buffer_json("{\"byteLength\":10}"), bin);
  for (const auto& file : {embedded, binary}) {
    HkxGltfImport out;
    EXPECT_EQ(import_gltf_animation(file, rig_of(), {}, out), HkxGltfStatus::Malformed);
    EXPECT_TRUE(has(out.message, "past the end of its buffer")) << out.message;
  }
}

TEST(HkxGltfAnimation, RefusesAnExternalBufferThatEscapesTheGltfFolder) {
  for (const char* uri : {"../secret.bin", "..%2Fsecret.bin", "..%5Csecret.bin", "%2Fetc%2Fsecret.bin"}) {
    int loads = 0;
    HkxGltfOptions options;
    options.load_uri = [&loads](std::string_view) {
      ++loads;
      return std::vector<std::byte>(4, std::byte{0});
    };
    const auto text =
        std::string("{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"uri\":\"") + uri + "\",\"byteLength\":4}]}";
    HkxGltfImport out;
    EXPECT_EQ(import_gltf_animation(bytes_of(text), rig_of(), options, out), HkxGltfStatus::Malformed) << uri;
    EXPECT_EQ(loads, 0) << uri;
  }
}

TEST(HkxGltfAnimation, ReadsAnExternalBufferBesideTheGltf) {
  std::string requested;
  HkxGltfOptions options;
  options.load_uri = [&requested](std::string_view uri) {
    requested = std::string(uri);
    return std::vector<std::byte>(4, std::byte{0});
  };
  const std::string text =
      "{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"uri\":\"buffer%200.bin\",\"byteLength\":4}]}";
  HkxGltfImport out;
  EXPECT_EQ(import_gltf_animation(bytes_of(text), rig_of(), options, out), HkxGltfStatus::NoSkin) << out.message;
  EXPECT_EQ(requested, "buffer 0.bin");
}

TEST(HkxGltfAnimation, StatusNamesAreDistinct) {
  std::vector<std::string> names;
  for (std::uint8_t i = 0; i <= static_cast<std::uint8_t>(HkxGltfStatus::UnsupportedRoot); ++i) {
    names.emplace_back(fo4::hkx::hkx_gltf_status_name(static_cast<HkxGltfStatus>(i)));
  }
  std::sort(names.begin(), names.end());
  EXPECT_EQ(std::adjacent_find(names.begin(), names.end()), names.end());
  EXPECT_EQ(std::count(names.begin(), names.end(), "Unknown"), 0);
}

namespace hx = fo4::hkx;
namespace ht = fo4::test::hkx;

namespace {

struct TemplateSpec {
  bool frame{true};
  bool old_notes{true};
  bool moving_frame{true};
  bool z_up{true};
  std::vector<std::int16_t> map{1, 0, 2};
  bool float_map{false};
  bool shared{false};
  bool extra_clip{false};
  bool trailing{false};
  bool unlinked{false};
  bool odd_frame{false};
  std::string skeleton_name{"skeleton"};
};

void store_float(std::vector<std::byte>& data, std::size_t at, float value) {
  std::memcpy(data.data() + at, &value, sizeof value);
}

void store_i16(std::vector<std::byte>& data, std::size_t at, std::int16_t value) {
  std::memcpy(data.data() + at, &value, sizeof value);
}

void store_text(std::vector<std::byte>& data, const std::string& text) {
  for (const char c : text) data.push_back(static_cast<std::byte>(c));
  data.push_back(std::byte{0});
}

void pad16(std::vector<std::byte>& data) { data.resize((data.size() + 15) / 16 * 16, std::byte{0xFF}); }

void store_array(std::vector<std::byte>& data, std::size_t field, std::size_t count) {
  ht::patch_u32(data, field + 8, static_cast<std::uint32_t>(count));
  ht::patch_u32(data, field + 12, 0x80000000u | static_cast<std::uint32_t>(count));
}

hx::HkxInterleavedAnimation identity_clip(std::size_t tracks) {
  hx::HkxInterleavedAnimation clip;
  clip.duration = 1.0f;
  clip.frame_count = 2;
  clip.track_count = tracks;
  clip.translations.assign(2 * tracks, {0.0f, 0.0f, 0.0f});
  clip.rotations.assign(2 * tracks, {0.0f, 0.0f, 0.0f, 1.0f});
  clip.scales.assign(2 * tracks, {1.0f, 1.0f, 1.0f});
  return clip;
}

// Invariant: binding with a non-identity map, optional frame, an unrelated object, then the clip with its notes last.
std::vector<std::byte> template_of(const TemplateSpec& spec) {
  const hx::HkxClassLayout& binding = *hx::find_layout("hkaAnimationBinding");
  const hx::HkxClassLayout& frame = *hx::find_layout("hkaDefaultAnimatedReferenceFrame");
  const hx::HkxClassLayout& other = *hx::find_layout("hkaFootstepAnalysisInfoContainer");
  std::vector<std::byte> names;
  const auto binding_name = ht::add_class_name(names, binding.signature, std::string(binding.name));
  const auto frame_name = ht::add_class_name(names, frame.signature, std::string(frame.name));
  const auto other_name = ht::add_class_name(names, other.signature, std::string(other.name));

  std::vector<std::byte> data(binding.size, std::byte{0});
  pad16(data);
  const std::size_t map_at = data.size();
  data.resize(map_at + 16, std::byte{0xFF});
  for (std::size_t i = 0; i < spec.map.size(); ++i) store_i16(data, map_at + 2 * i, spec.map[i]);
  const std::size_t skeleton_at = data.size();
  store_text(data, spec.skeleton_name);
  pad16(data);

  std::vector<std::uint32_t> local;
  std::vector<std::uint32_t> virtuals{0, 0, binding_name};
  ht::link(local, ht::member_at("hkaAnimationBinding", "originalSkeletonName"), skeleton_at);
  const std::size_t map_field = ht::member_at("hkaAnimationBinding", "transformTrackToBoneIndices");
  ht::link(local, map_field, map_at);
  store_array(data, map_field, spec.map.size());
  if (spec.float_map) {
    const std::size_t floats_field = ht::member_at("hkaAnimationBinding", "floatTrackToFloatSlotIndices");
    ht::link(local, floats_field, map_at);
    store_array(data, floats_field, 1);
  }

  std::size_t frame_at = 0;
  if (spec.frame) {
    frame_at = data.size();
    data.resize(frame_at + frame.size, std::byte{0});
    const float up_y = spec.z_up ? 0.0f : 1.0f;
    store_float(data, frame_at + ht::member_at("hkaDefaultAnimatedReferenceFrame", "up") + 4, up_y);
    store_float(data, frame_at + ht::member_at("hkaDefaultAnimatedReferenceFrame", "up") + 8, spec.z_up ? 1.0f : 0.0f);
    store_float(data, frame_at + ht::member_at("hkaDefaultAnimatedReferenceFrame", "forward") + 4, spec.z_up ? 1.0f : 0.0f);
    store_float(data, frame_at + ht::member_at("hkaDefaultAnimatedReferenceFrame", "forward") + 8, spec.z_up ? 0.0f : 1.0f);
    store_float(data, frame_at + ht::member_at("hkaDefaultAnimatedReferenceFrame", "duration"), 1.0f);
    const std::size_t samples_field = frame_at + ht::member_at("hkaDefaultAnimatedReferenceFrame", "referenceFrameSamples");
    const std::size_t samples_at = data.size();
    data.resize(samples_at + 32, std::byte{0});
    if (spec.moving_frame) {
      store_float(data, samples_at, 1.0f);
      store_float(data, samples_at + 4, 2.0f);
      store_float(data, samples_at + 12, 0.5f);
      store_float(data, samples_at + 16, 3.0f);
    }
    store_array(data, samples_field, 2);
    ht::link(local, samples_field, samples_at);
    virtuals.insert(virtuals.end(), {static_cast<std::uint32_t>(frame_at), 0u, frame_name});
  }
  const std::size_t other_at = data.size();
  data.resize(other_at + other.size, std::byte{0x5A});
  virtuals.insert(virtuals.end(), {static_cast<std::uint32_t>(other_at), 0u, other_name});

  const auto base = ht::build_packfile({
      ht::TestSection{"__classnames__", names, {}, {}, {}},
      ht::TestSection{"__types__", {}, {}, {}, {}},
      ht::TestSection{"__data__", data, local, {}, virtuals},
  });
  hx::HkxAppend appended;
  EXPECT_EQ(hx::append_spline_animation(base, identity_clip(3), hx::HkxSplineOptions{}, appended), hx::HkxStatus::Ok);

  hx::HkxPackfile pack;
  EXPECT_EQ(hx::read_packfile(appended.bytes, pack), hx::HkxStatus::Ok);
  if (pack.objects.size() <= appended.object_index) return {};
  const auto clip_at = pack.objects[appended.object_index].offset;
  hx::HkxSection& section = pack.sections[pack.data_section];
  if (!spec.unlinked) {
    section.local_fixups.push_back({static_cast<std::uint32_t>(ht::member_at("hkaAnimationBinding", "animation")), clip_at});
  }
  if (spec.frame || spec.odd_frame) {
    section.global_fixups.push_back({static_cast<std::uint32_t>(clip_at + ht::member_at("hkaAnimation", "extractedMotion")),
                                     static_cast<std::uint32_t>(pack.data_section),
                                     static_cast<std::uint32_t>(spec.odd_frame ? other_at : frame_at)});
  }
  if (spec.shared) section.local_fixups.push_back({static_cast<std::uint32_t>(other_at), clip_at + 16});
  if (spec.old_notes) {
    pad16(section.data);
    const std::size_t track_at = section.data.size();
    section.data.resize(track_at + 24, std::byte{0});
    pad16(section.data);
    const std::size_t note_at = section.data.size();
    section.data.resize(note_at + 16, std::byte{0});
    const std::size_t text_at = section.data.size();
    store_text(section.data, "OldEvent");
    store_float(section.data, note_at, 0.25f);
    store_array(section.data, track_at + 8, 1);
    const std::size_t tracks_field = clip_at + ht::member_at("hkaAnimation", "annotationTracks");
    store_array(section.data, tracks_field, 1);
    section.local_fixups.push_back({static_cast<std::uint32_t>(note_at + 8), static_cast<std::uint32_t>(text_at)});
    section.local_fixups.push_back({static_cast<std::uint32_t>(track_at + 8), static_cast<std::uint32_t>(note_at)});
    section.local_fixups.push_back({static_cast<std::uint32_t>(tracks_field), static_cast<std::uint32_t>(track_at)});
  }
  std::sort(section.local_fixups.begin(), section.local_fixups.end(),
            [](const hx::HkxLocalFixup& a, const hx::HkxLocalFixup& b) { return a.source < b.source; });
  std::sort(section.global_fixups.begin(), section.global_fixups.end(),
            [](const hx::HkxRemoteFixup& a, const hx::HkxRemoteFixup& b) { return a.source < b.source; });
  std::vector<std::byte> rebuilt;
  EXPECT_EQ(hx::rewrite_packfile(appended.bytes, pack, rebuilt), hx::HkxStatus::Ok);
  if (spec.trailing) {
    hx::HkxAppend after;
    EXPECT_EQ(hx::append_object(rebuilt, std::string(other.name), other.signature,
                                std::vector<std::byte>(other.size, std::byte{0x5A}), after),
              hx::HkxStatus::Ok);
    rebuilt = std::move(after.bytes);
  }
  if (!spec.extra_clip) return rebuilt;
  hx::HkxAppend extra;
  EXPECT_EQ(hx::append_interleaved_animation(rebuilt, identity_clip(1), extra), hx::HkxStatus::Ok);
  return extra.bytes;
}

Tiny noted() {
  Tiny out;
  out.notes = {{0.25f, "Hit"}, {0.5f, "Land"}};
  return out;
}

hx::HkxGltfSaveOptions replacing() {
  hx::HkxGltfSaveOptions out;
  out.replace_annotations = true;
  out.replace_motion = true;
  return out;
}

std::size_t count_class(const hx::HkxPackfile& pack, std::string_view name) {
  return static_cast<std::size_t>(std::count_if(pack.objects.begin(), pack.objects.end(),
                                                [&](const hx::HkxObjectView& object) { return object.class_name == name; }));
}

}  // namespace

// Constraint: the saved clip must replace the template clip (regression: two clips, unlinked binding, stale notes).
TEST(HkxGltfSave, ReplacesTheTemplateClipAndKeepsBindingNotesAndRootMotionAcrossAReopen) {
  const auto imported = ok(noted());
  const auto source = template_of(TemplateSpec{});
  hx::HkxGltfSave saved;
  ASSERT_EQ(hx::save_gltf_animation(source, rig_of(), imported, replacing(), saved), hx::HkxGltfSaveStatus::Ok)
      << saved.report.message;

  const auto& report = saved.report;
  EXPECT_EQ(report.tracks, 3u);
  EXPECT_EQ(report.frames, 31u);
  EXPECT_EQ(report.animated, (std::vector<std::string>{"COM", "Root"}));
  EXPECT_EQ(report.reference_tracks, (std::vector<std::string>{"Spine1"}));
  EXPECT_EQ(report.untracked_bones, (std::vector<std::string>{"Head"}));
  EXPECT_EQ(report.annotations_replaced, 1u);
  EXPECT_EQ(report.annotations_written, 2u);
  EXPECT_TRUE(report.motion_replaced);
  EXPECT_EQ(report.motion_samples, 31u);
  EXPECT_TRUE(report.binding_linked);
  EXPECT_EQ(report.skeleton_match, "match");

  hx::HkxPackfile reopened;
  ASSERT_EQ(hx::read_packfile(saved.bytes, reopened), hx::HkxStatus::Ok);
  EXPECT_EQ(reopened.objects.size(), 4u);
  EXPECT_EQ(count_class(reopened, "hkaSplineCompressedAnimation"), 1u);
  EXPECT_EQ(count_class(reopened, "hkaDefaultAnimatedReferenceFrame"), 1u);
  EXPECT_EQ(count_class(reopened, "hkaAnimationBinding"), 1u);

  std::uint32_t clip_at = 0;
  for (const auto& object : reopened.objects) {
    if (object.class_name == "hkaFootstepAnalysisInfoContainer") {
      ASSERT_GE(object.data.size(), 32u);
      EXPECT_TRUE(std::all_of(object.data.begin(), object.data.begin() + 32, [](std::byte b) { return b == std::byte{0x5A}; }))
          << "the unrelated object changed";
    }
    if (object.class_name == "hkaSplineCompressedAnimation") clip_at = object.offset;
  }

  hx::HkxAnimation clip;
  ASSERT_EQ(hx::read_animation(reopened, clip), hx::HkxStatus::Ok);
  ASSERT_EQ(clip.kind, hx::HkxAnimationKind::Spline);
  ASSERT_EQ(clip.frame_count, 31u);
  ASSERT_EQ(clip.tracks.size(), 3u);
  const std::array<std::size_t, 3> bone_of_track{1, 0, 2};
  for (std::size_t t = 0; t < 3; ++t) {
    for (std::size_t f = 0; f < clip.frame_count; ++f) {
      const std::size_t at = f * imported.animation.track_count + bone_of_track[t];
      for (std::size_t i = 0; i < 3; ++i) {
        EXPECT_NEAR(clip.tracks[t].translations[f][i], imported.animation.translations[at][i], 0.05f) << t << "/" << f;
      }
      const auto& want = imported.animation.rotations[at];
      const auto& got = clip.tracks[t].rotations[f];
      const float dot = std::fabs(want[0] * got[0] + want[1] * got[1] + want[2] * got[2] + want[3] * got[3]);
      EXPECT_GT(dot, 0.99999f) << t << "/" << f;
    }
  }
  ASSERT_EQ(clip.annotation_tracks.size(), 1u);
  ASSERT_EQ(clip.annotation_tracks[0].annotations.size(), imported.annotations.size());
  for (std::size_t i = 0; i < imported.annotations.size(); ++i) {
    EXPECT_EQ(clip.annotation_tracks[0].annotations[i].time, imported.annotations[i].time);
    EXPECT_EQ(clip.annotation_tracks[0].annotations[i].text, imported.annotations[i].text);
  }

  hx::HkxAnimationBinding binding;
  ASSERT_EQ(hx::read_binding(reopened, binding), hx::HkxStatus::Ok);
  ASSERT_TRUE(binding.animation_offset.has_value());
  EXPECT_EQ(*binding.animation_offset, clip_at);
  EXPECT_EQ(binding.transform_track_to_bone_indices, (std::vector<std::int16_t>{1, 0, 2}));
  EXPECT_EQ(binding.original_skeleton_name, "skeleton");

  hx::HkxExtractedMotion motion;
  ASSERT_EQ(hx::read_extracted_motion(reopened, motion), hx::HkxStatus::Ok);
  EXPECT_EQ(motion.kind, hx::HkxMotionKind::Default);
  EXPECT_EQ(motion.up, (std::array<float, 3>{0.0f, 0.0f, 1.0f}));
  EXPECT_EQ(motion.samples, imported.motion.samples);
}

// Constraint: the old clip may sit before other objects (regression: cutting a clip that is not the last object shifts or drops its neighbours).
TEST(HkxGltfSave, ReplacesATemplateClipThatIsNotTheLastObjectAndKeepsItsNeighbours) {
  TemplateSpec spec;
  spec.trailing = true;
  hx::HkxGltfSave saved;
  ASSERT_EQ(hx::save_gltf_animation(template_of(spec), rig_of(), ok(noted()), replacing(), saved), hx::HkxGltfSaveStatus::Ok)
      << saved.report.message;
  hx::HkxPackfile reopened;
  ASSERT_EQ(hx::read_packfile(saved.bytes, reopened), hx::HkxStatus::Ok);
  EXPECT_EQ(count_class(reopened, "hkaFootstepAnalysisInfoContainer"), 2u);
  EXPECT_EQ(count_class(reopened, "hkaSplineCompressedAnimation"), 1u);
  hx::HkxAnimationBinding binding;
  ASSERT_EQ(hx::read_binding(reopened, binding), hx::HkxStatus::Ok);
  EXPECT_TRUE(binding.animation_offset.has_value());
}

// Constraint: the template's own samples saved back over it must read as the same clip (regression: a save that loses the bound track map, the notes or the root-motion frame, or drifts a track past the spline budget, which a replacement with different data cannot show).
TEST(HkxGltfSave, SavingTheTemplatesOwnSamplesBackKeepsItsClipBindingNotesAndMotion) {
  const auto source = template_of(TemplateSpec{});
  hx::HkxAnimation was;
  hx::HkxGltfImport own;
  ASSERT_TRUE(ht::import_of_clip(source, rig_of().bone_names.size(), was, own));
  ASSERT_FALSE(own.annotations.empty());
  hx::HkxGltfSave saved;
  ASSERT_EQ(hx::save_gltf_animation(source, rig_of(), own, replacing(), saved), hx::HkxGltfSaveStatus::Ok)
      << saved.report.message;

  hx::HkxPackfile original;
  hx::HkxPackfile reopened;
  ASSERT_EQ(hx::read_packfile(source, original), hx::HkxStatus::Ok);
  ASSERT_EQ(hx::read_packfile(saved.bytes, reopened), hx::HkxStatus::Ok);
  EXPECT_EQ(reopened.objects.size(), original.objects.size());
  hx::HkxAnimation now;
  ASSERT_EQ(hx::read_animation(reopened, now), hx::HkxStatus::Ok);
  ht::ClipGap gap;
  ASSERT_TRUE(ht::clip_gap(was, now, gap));
  EXPECT_LE(gap.position, 0.05f);
  EXPECT_LE(gap.rotation, 0.002f);
  EXPECT_LE(gap.scale, 0.002f);
  std::vector<hx::HkxAnimationAnnotation> notes;
  for (const auto& track : now.annotation_tracks) notes.insert(notes.end(), track.annotations.begin(), track.annotations.end());
  ASSERT_EQ(notes.size(), own.annotations.size());
  for (std::size_t i = 0; i < notes.size(); ++i) {
    EXPECT_EQ(notes[i].time, own.annotations[i].time);
    EXPECT_EQ(notes[i].text, own.annotations[i].text);
  }
  hx::HkxAnimationBinding was_binding;
  hx::HkxAnimationBinding now_binding;
  ASSERT_EQ(hx::read_binding(original, was_binding), hx::HkxStatus::Ok);
  ASSERT_EQ(hx::read_binding(reopened, now_binding), hx::HkxStatus::Ok);
  EXPECT_EQ(now_binding.transform_track_to_bone_indices, was_binding.transform_track_to_bone_indices);
  EXPECT_EQ(now_binding.original_skeleton_name, was_binding.original_skeleton_name);
  hx::HkxExtractedMotion motion;
  ASSERT_EQ(hx::read_extracted_motion(reopened, motion), hx::HkxStatus::Ok);
  EXPECT_EQ(motion.samples, own.motion.samples);
  EXPECT_EQ(motion.duration, own.motion.duration);
  EXPECT_EQ(hx::repack_check(saved.bytes).verdict, hx::HkxRepackVerdict::Equal);
}

// Constraint: a template without notes or frame must still link (regression: save only works with every optional object).
TEST(HkxGltfSave, LinksAClipWithNoNotesOverATemplateWithNoNotesAndNoRootMotionFrame) {
  auto imported = ok(Tiny{});
  imported.annotations.clear();
  imported.motion.samples.assign(imported.animation.frame_count, {0.0f, 0.0f, 0.0f, 0.0f});
  TemplateSpec spec;
  spec.frame = false;
  spec.old_notes = false;
  hx::HkxGltfSave saved;
  ASSERT_EQ(hx::save_gltf_animation(template_of(spec), rig_of(), imported, hx::HkxGltfSaveOptions{}, saved),
            hx::HkxGltfSaveStatus::Ok)
      << saved.report.message;
  EXPECT_FALSE(saved.report.motion_replaced);

  hx::HkxPackfile reopened;
  ASSERT_EQ(hx::read_packfile(saved.bytes, reopened), hx::HkxStatus::Ok);
  EXPECT_EQ(reopened.objects.size(), 3u);
  hx::HkxAnimation clip;
  ASSERT_EQ(hx::read_animation(reopened, clip), hx::HkxStatus::Ok);
  EXPECT_TRUE(clip.annotation_tracks.empty() || clip.annotation_tracks[0].annotations.empty());
  hx::HkxAnimationBinding binding;
  ASSERT_EQ(hx::read_binding(reopened, binding), hx::HkxStatus::Ok);
  EXPECT_TRUE(binding.animation_offset.has_value());
  hx::HkxExtractedMotion motion;
  ASSERT_EQ(hx::read_extracted_motion(reopened, motion), hx::HkxStatus::Ok);
  EXPECT_EQ(motion.kind, hx::HkxMotionKind::None);
}

// Constraint: anything not placed exactly must be refused with no bytes (regression: half file, dropped bone, lost notes).
TEST(HkxGltfSave, RefusesWhatItCannotPlaceExactlyAndHandsBackNoBytes) {
  struct RefusalCase {
    const char* what;
    TemplateSpec spec;
    hx::HkxGltfSaveOptions options;
    hx::HkxGltfSaveStatus want;
  };
  std::vector<RefusalCase> cases;
  const auto add = [&cases](const char* what, TemplateSpec spec, hx::HkxGltfSaveOptions options,
                            hx::HkxGltfSaveStatus want) { cases.push_back({what, std::move(spec), options, want}); };
  hx::HkxGltfSaveOptions no_notes = replacing();
  no_notes.replace_annotations = false;
  hx::HkxGltfSaveOptions no_motion = replacing();
  no_motion.replace_motion = false;
  hx::HkxGltfSaveOptions over_budget = replacing();
  over_budget.max_rotation_error = -1.0f;
  TemplateSpec unmapped;
  unmapped.map = {1, 2, 3};
  TemplateSpec repeated;
  repeated.map = {1, 1, 2};
  TemplateSpec out_of_range;
  out_of_range.map = {1, 0, 9};
  TemplateSpec floats;
  floats.float_map = true;
  TemplateSpec sideways;
  sideways.z_up = false;
  TemplateSpec no_frame;
  no_frame.frame = false;
  TemplateSpec shared;
  shared.shared = true;
  TemplateSpec two_clips;
  two_clips.extra_clip = true;
  TemplateSpec other_skeleton;
  other_skeleton.skeleton_name = "other";
  TemplateSpec unlinked;
  unlinked.unlinked = true;
  TemplateSpec odd_frame;
  odd_frame.frame = false;
  odd_frame.odd_frame = true;
  add("template binding is for another skeleton", other_skeleton, replacing(), hx::HkxGltfSaveStatus::SkeletonMismatch);
  add("template binding names no clip", unlinked, replacing(), hx::HkxGltfSaveStatus::TemplateNoBinding);
  add("template frame is not a default frame", odd_frame, replacing(), hx::HkxGltfSaveStatus::UnsupportedMotion);
  add("notes would be replaced", TemplateSpec{}, no_notes, hx::HkxGltfSaveStatus::AnnotationsReplaced);
  add("root motion would be replaced", TemplateSpec{}, no_motion, hx::HkxGltfSaveStatus::MotionReplaced);
  add("animated bone has no template track", unmapped, replacing(), hx::HkxGltfSaveStatus::UnmappedBone);
  add("template drives one bone twice", repeated, replacing(), hx::HkxGltfSaveStatus::TemplateTrackMap);
  add("template drives a bone the skeleton lacks", out_of_range, replacing(), hx::HkxGltfSaveStatus::TemplateTrackMap);
  add("template binds float tracks", floats, replacing(), hx::HkxGltfSaveStatus::TemplateFloatTracks);
  add("template frame is not Z up", sideways, replacing(), hx::HkxGltfSaveStatus::UnsupportedMotion);
  add("moving root and no frame to carry it", no_frame, replacing(), hx::HkxGltfSaveStatus::UnsupportedMotion);
  add("clip data shared with another object", shared, replacing(), hx::HkxGltfSaveStatus::SharedData);
  add("read-back over the error budget", TemplateSpec{}, over_budget, hx::HkxGltfSaveStatus::OverBudget);
  add("template holds two clips", two_clips, replacing(), hx::HkxGltfSaveStatus::TemplateNoClip);

  const auto imported = ok(noted());
  for (const RefusalCase& c : cases) {
    hx::HkxGltfSave saved;
    EXPECT_EQ(hx::save_gltf_animation(template_of(c.spec), rig_of(), imported, c.options, saved), c.want) << c.what;
    EXPECT_TRUE(saved.bytes.empty()) << c.what;
    EXPECT_FALSE(saved.report.message.empty()) << c.what;
  }

  // Regression: a note text over 255 bytes must be refused by set_animation_annotations, not truncated by the reader later.
  auto long_note = imported;
  long_note.annotations.front().text.assign(300, 'x');
  hx::HkxGltfSave refused;
  EXPECT_EQ(hx::save_gltf_animation(template_of(TemplateSpec{}), rig_of(), long_note, replacing(), refused),
            hx::HkxGltfSaveStatus::WriteFailed);
  EXPECT_TRUE(refused.bytes.empty());

  hx::HkxGltfSave saved;
  EXPECT_EQ(hx::save_gltf_animation(template_of(TemplateSpec{}), rig_of(1), imported, replacing(), saved),
            hx::HkxGltfSaveStatus::BadImport);
  EXPECT_TRUE(saved.bytes.empty());
  const std::vector<std::byte> garbage(64, std::byte{0});
  EXPECT_EQ(hx::save_gltf_animation(garbage, rig_of(), imported, replacing(), saved), hx::HkxGltfSaveStatus::BadTemplate);
  EXPECT_TRUE(saved.bytes.empty());
}
