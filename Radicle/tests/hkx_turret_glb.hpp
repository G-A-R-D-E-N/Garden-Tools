#pragma once

#include "fo4/hkx/skeleton.hpp"
#include "gltf_import_builder.hpp"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace fo4::test::hkx {

// Requires a skeleton with one root at bone 0 and every parent before its child; each bone becomes a joint under an Armature node.
inline std::vector<std::byte> turret_animation_glb(const fo4::hkx::HkxSkeleton& skeleton, std::size_t animated_bone) {
  using nlohmann::json;
  namespace gl = gltf_import_test;
  gl::Gltf g = gl::empty_gltf();
  const std::size_t times = gl::add_accessor(g, gl::k_float, 2, "SCALAR", gl::float_bytes({0.0f, 0.2f}));
  const std::size_t turn =
      gl::add_accessor(g, gl::k_float, 2, "VEC4", gl::float_bytes({0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0871557f, 0.9961947f}));

  const std::size_t bones = skeleton.bone_names.size();
  json joints = json::array();
  for (std::size_t bone = 0; bone < bones; ++bone) {
    json children = json::array();
    for (std::size_t child = 0; child < bones; ++child) {
      if (skeleton.parent_indices[child] == static_cast<std::int16_t>(bone)) children.push_back(child);
    }
    json node = {{"name", skeleton.bone_names[bone]}};
    if (!children.empty()) node["children"] = children;
    gl::add_node(g, node);
    joints.push_back(bone);
  }
  gl::add_node(g, json{{"name", "Armature"}, {"children", json::array({0})}});
  g.json["skins"] = json::array();
  g.json["skins"].push_back(json{{"joints", joints}});

  const json sampler = {{"input", times}, {"output", turn}, {"interpolation", "LINEAR"}};
  const json channel = {{"sampler", 0}, {"target", {{"node", animated_bone}, {"path", "rotation"}}}};
  g.json["animations"] = json::array();
  g.json["animations"].push_back(
      json{{"name", "turret"}, {"samplers", json::array({sampler})}, {"channels", json::array({channel})}});
  return gl::to_glb(g);
}

}  // namespace fo4::test::hkx
