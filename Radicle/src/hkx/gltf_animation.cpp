#include "fo4/hkx/gltf_animation.hpp"

#include "../nif/gltf_read.hpp"
#include "fo4/hkx/plain.hpp"
#include "gltf_animation_math.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fo4::hkx {
namespace {

using nlohmann::json;
using nif::gltf_detail::Document;
using gltf_math::axis_z;
using gltf_math::conjugate;
using gltf_math::decompose;
using gltf_math::dot;
using gltf_math::identity;
using gltf_math::Mat;
using gltf_math::multiply;
using gltf_math::negate;
using gltf_math::normalised;
using gltf_math::Quat;
using gltf_math::rotation_matrix;
using gltf_math::rotation_x_quarter_turn;
using gltf_math::scale_matrix;
using gltf_math::slerp;
using gltf_math::translation_matrix;
using gltf_math::usable_rotation;
using gltf_math::Vec3;

constexpr std::int64_t k_float_component = 5126;
constexpr float k_rest_position_tolerance = 0.01f;
constexpr float k_rest_angle_tolerance = 0.001f;
constexpr float k_rest_scale_tolerance = 0.001f;
constexpr double k_pi = 3.14159265358979323846;

struct Refusal {
  HkxGltfStatus status;
  std::string message;
};

[[noreturn]] void refuse(HkxGltfStatus status, std::string message) { throw Refusal{status, std::move(message)}; }

std::string fixed3(double value) {
  char text[64];
  std::snprintf(text, sizeof text, "%.3f", value);
  return text;
}

std::string join(const std::vector<std::string>& items) {
  std::string out;
  for (std::size_t i = 0; i < items.size(); ++i) out += (i == 0 ? "" : ", ") + items[i];
  return out;
}

struct Node {
  std::string name;
  int parent{-1};
  Vec3 translation{0.0, 0.0, 0.0};
  Quat rotation{0.0, 0.0, 0.0, 1.0};
  Vec3 scale{1.0, 1.0, 1.0};
};

struct Accessor {
  std::vector<double> values;
  std::size_t count{0};
  std::size_t width{0};
};

struct Channel {
  int node{0};
  std::string path;
  std::string interpolation{"LINEAR"};
  const Accessor* times{nullptr};
  const Accessor* values{nullptr};
};

struct Clip {
  std::string name;
  std::vector<Channel> channels;
  std::vector<HkxAnimationAnnotation> annotations;
};

struct Skin {
  std::string name;
  std::vector<int> joints;
};

struct Scene {
  std::vector<Node> nodes;
  std::vector<Skin> skins;
  std::vector<Clip> clips;
  std::map<std::size_t, Accessor> accessors;
};

const json* array_at(const json& parent, const char* name) {
  const auto found = parent.find(name);
  return found != parent.end() && found->is_array() ? &*found : nullptr;
}

std::int64_t int_of(const json& parent, const char* name, std::int64_t fallback) {
  const auto found = parent.find(name);
  if (found == parent.end() || !found->is_number()) return fallback;
  if (!found->is_number_integer()) refuse(HkxGltfStatus::Malformed, std::string(name) + " is not a whole number.");
  return found->get<std::int64_t>();
}

std::string text_of(const json& parent, const char* name) {
  const auto found = parent.find(name);
  return found != parent.end() && found->is_string() ? found->get<std::string>() : std::string{};
}

std::optional<std::vector<double>> numbers_of(const json& parent, const char* name, std::size_t expected) {
  const auto found = parent.find(name);
  if (found == parent.end()) return std::nullopt;
  if (!found->is_array() || found->size() != expected) {
    refuse(HkxGltfStatus::Malformed,
           "a node's " + std::string(name) + " does not hold " + std::to_string(expected) + " values.");
  }
  std::vector<double> out;
  for (const auto& value : *found) {
    if (!value.is_number()) refuse(HkxGltfStatus::Malformed, "a node's " + std::string(name) + " holds a non-number.");
    out.push_back(value.get<double>());
  }
  return out;
}

Node read_node(const json& node) {
  Node out;
  out.name = text_of(node, "name");
  if (const auto t = numbers_of(node, "translation", 3)) out.translation = {(*t)[0], (*t)[1], (*t)[2]};
  if (const auto q = numbers_of(node, "rotation", 4)) {
    const Quat rotation{(*q)[0], (*q)[1], (*q)[2], (*q)[3]};
    if (!usable_rotation(rotation)) {
      refuse(HkxGltfStatus::BadRotation, "a node's rotation is zero, tiny or not finite, so it has no direction.");
    }
    out.rotation = normalised(rotation);
  }
  if (const auto s = numbers_of(node, "scale", 3)) out.scale = {(*s)[0], (*s)[1], (*s)[2]};
  if (const auto m = numbers_of(node, "matrix", 16)) {
    Mat matrix{};
    for (std::size_t i = 0; i < 16; ++i) matrix[i] = (*m)[i];
    if (!decompose(matrix, out.scale, out.rotation, out.translation)) {
      refuse(HkxGltfStatus::DegenerateTransform, "node '" + out.name + "' has a degenerate matrix.");
    }
  }
  return out;
}

const Accessor& read_accessor(const Document& doc, Scene& scene, std::int64_t index) {
  const auto cached = scene.accessors.find(static_cast<std::size_t>(index));
  if (index >= 0 && cached != scene.accessors.end()) return cached->second;

  const json* accessors = array_at(doc.json, "accessors");
  const json* views = array_at(doc.json, "bufferViews");
  if (accessors == nullptr || index < 0 || static_cast<std::size_t>(index) >= accessors->size()) {
    refuse(HkxGltfStatus::Malformed, "an animation names accessor " + std::to_string(index) + ", which does not exist.");
  }
  const json& accessor = (*accessors)[static_cast<std::size_t>(index)];
  if (accessor.contains("sparse")) {
    refuse(HkxGltfStatus::SparseAccessor,
           "sparse accessors are not read, so the glTF was refused rather than read as zeros.");
  }
  if (int_of(accessor, "componentType", 0) != k_float_component) {
    refuse(HkxGltfStatus::UnsupportedAccessor, "animation data must be 32-bit floats.");
  }
  const auto normalized = accessor.find("normalized");
  if (normalized != accessor.end() && normalized->is_boolean() && normalized->get<bool>()) {
    refuse(HkxGltfStatus::UnsupportedAccessor, "normalized animation data is not read.");
  }

  const std::string type = text_of(accessor, "type");
  std::size_t width = 0;
  if (type == "SCALAR") {
    width = 1;
  } else if (type == "VEC3") {
    width = 3;
  } else if (type == "VEC4") {
    width = 4;
  } else {
    refuse(HkxGltfStatus::UnsupportedAccessor, "an animation accessor of type " + type + " is not read.");
  }

  const auto count = int_of(accessor, "count", -1);
  const auto view = int_of(accessor, "bufferView", -1);
  if (count < 0 || view < 0 || views == nullptr || static_cast<std::size_t>(view) >= views->size()) {
    refuse(HkxGltfStatus::Malformed, "an animation accessor has no usable buffer view.");
  }

  Accessor decoded;
  decoded.values = nif::gltf_detail::read_accessor(doc, static_cast<std::size_t>(index), type);
  decoded.count = static_cast<std::size_t>(count);
  decoded.width = width;
  return scene.accessors.emplace(static_cast<std::size_t>(index), std::move(decoded)).first->second;
}

std::vector<HkxAnimationAnnotation> read_annotations(const json& animation, std::size_t index) {
  std::vector<HkxAnimationAnnotation> notes;
  const auto extras = animation.find("extras");
  if (extras == animation.end() || !extras->is_object()) return notes;
  const auto list = extras->find("annotations");
  if (list == extras->end()) return notes;
  if (!list->is_array()) {
    refuse(HkxGltfStatus::BadAnnotation,
           "animation " + std::to_string(index) + " has extras.annotations that is not a list.");
  }
  std::size_t note = 0;
  for (const auto& item : *list) {
    const auto time = item.find("time");
    const auto text = item.find("text");
    if (time == item.end() || !time->is_number() || text == item.end() || !text->is_string()) {
      refuse(HkxGltfStatus::BadAnnotation, "annotation " + std::to_string(note) + " of animation " +
                                               std::to_string(index) + " needs a numeric time and a text.");
    }
    notes.push_back({static_cast<float>(time->get<double>()), text->get<std::string>()});
    ++note;
  }
  return notes;
}

void read_channels(const json& animation, const Document& doc, Scene& scene, Clip& clip) {
  const json* samplers = array_at(animation, "samplers");
  const json* channels = array_at(animation, "channels");
  if (channels == nullptr) return;
  for (const auto& channel : *channels) {
    const auto target = channel.find("target");
    if (target == channel.end()) {
      refuse(HkxGltfStatus::Malformed,
             "an animation channel has no target, so it names no node. Refused rather than dropping it.");
    }
    const auto node = int_of(*target, "node", -1);
    const std::string path = text_of(*target, "path");
    if (node < 0 || static_cast<std::size_t>(node) >= scene.nodes.size()) {
      refuse(HkxGltfStatus::Malformed,
             "an animation channel targets node " + std::to_string(node) + ", which does not exist.");
    }
    if (path == "weights") {
      Channel weights;
      weights.node = static_cast<int>(node);
      weights.path = path;
      clip.channels.push_back(std::move(weights));
      continue;
    }

    const auto sampler = int_of(channel, "sampler", -1);
    const std::size_t sampler_count = samplers == nullptr ? 0 : samplers->size();
    if (sampler < 0 || static_cast<std::size_t>(sampler) >= sampler_count) {
      refuse(HkxGltfStatus::Malformed,
             "an animation channel names sampler " + std::to_string(sampler) + ", which does not exist.");
    }
    const json& entry = (*samplers)[static_cast<std::size_t>(sampler)];
    std::string interpolation = text_of(entry, "interpolation");
    if (interpolation.empty()) interpolation = "LINEAR";

    const Accessor& times = read_accessor(doc, scene, int_of(entry, "input", -1));
    const Accessor& values = read_accessor(doc, scene, int_of(entry, "output", -1));
    const std::size_t per_key = interpolation == "CUBICSPLINE" ? 3 : 1;
    if (times.width != 1 || times.count < 1 || values.count != times.count * per_key) {
      refuse(HkxGltfStatus::Malformed, "a " + path + " sampler has " + std::to_string(values.count) + " value(s) for " +
                                           std::to_string(times.count) + " key(s).");
    }

    std::size_t width = 0;
    if (path == "translation" || path == "scale") {
      width = 3;
    } else if (path == "rotation") {
      width = 4;
    } else {
      refuse(HkxGltfStatus::UnsupportedChannel, "animation channels that target " + path + " are not read.");
    }
    if (values.width != width) {
      refuse(HkxGltfStatus::Malformed,
             "a " + path + " sampler has " + std::to_string(values.width) + "-component values, not " +
                 std::to_string(width) + ".");
    }

    if (path == "rotation") {
      const std::size_t first = interpolation == "CUBICSPLINE" ? 1 : 0;
      for (std::size_t k = 0; k < times.count; ++k) {
        const std::size_t at = (k * per_key + first) * 4;
        const Quat key{values.values[at], values.values[at + 1], values.values[at + 2], values.values[at + 3]};
        if (!usable_rotation(key)) {
          refuse(HkxGltfStatus::BadRotation, "a rotation sampler's key " + std::to_string(k) +
                                                 " is zero, tiny or not finite, so it has no direction.");
        }
      }
    }
    for (std::size_t k = 1; k < times.values.size(); ++k) {
      if (!(times.values[k] > times.values[k - 1])) {
        refuse(HkxGltfStatus::BadKeyTimes, "a " + path + " sampler's key times do not increase.");
      }
    }

    Channel read;
    read.node = static_cast<int>(node);
    read.path = path;
    read.interpolation = interpolation;
    read.times = &times;
    read.values = &values;
    clip.channels.push_back(std::move(read));
  }
}

void read_scene(const Document& doc, Scene& scene) {
  const json& root = doc.json;
  const json* nodes = array_at(root, "nodes");
  if (nodes != nullptr) {
    for (const auto& node : *nodes) scene.nodes.push_back(read_node(node));
    for (std::size_t i = 0; i < nodes->size(); ++i) {
      const json* children = array_at((*nodes)[i], "children");
      if (children == nullptr) continue;
      for (const auto& entry : *children) {
        if (!entry.is_number_integer()) refuse(HkxGltfStatus::Malformed, "a node lists a child that is not a whole number.");
        const auto child = entry.get<std::int64_t>();
        if (child < 0 || static_cast<std::size_t>(child) >= scene.nodes.size()) {
          refuse(HkxGltfStatus::Malformed,
                 "node " + std::to_string(i) + " lists child " + std::to_string(child) + ", which does not exist.");
        }
        if (scene.nodes[static_cast<std::size_t>(child)].parent >= 0) {
          refuse(HkxGltfStatus::Malformed, "node " + std::to_string(child) + " is listed under two parents.");
        }
        scene.nodes[static_cast<std::size_t>(child)].parent = static_cast<int>(i);
      }
    }
  }

  for (std::size_t i = 0; i < scene.nodes.size(); ++i) {
    std::size_t steps = 0;
    for (int p = scene.nodes[i].parent; p >= 0; p = scene.nodes[static_cast<std::size_t>(p)].parent) {
      if (++steps > scene.nodes.size()) refuse(HkxGltfStatus::Malformed, "the glTF node hierarchy has a cycle.");
    }
  }

  if (const json* skins = array_at(root, "skins")) {
    for (const auto& entry : *skins) {
      Skin skin;
      skin.name = text_of(entry, "name");
      if (const json* joints = array_at(entry, "joints")) {
        for (const auto& joint : *joints) {
          if (!joint.is_number_integer()) refuse(HkxGltfStatus::Malformed, "a skin lists a joint that is not a whole number.");
          const auto index = joint.get<std::int64_t>();
          if (index < 0 || static_cast<std::size_t>(index) >= scene.nodes.size()) {
            refuse(HkxGltfStatus::Malformed, "a skin names joint " + std::to_string(index) + ", which does not exist.");
          }
          skin.joints.push_back(static_cast<int>(index));
        }
      }
      scene.skins.push_back(std::move(skin));
    }
  }

  if (const json* animations = array_at(root, "animations")) {
    for (std::size_t index = 0; index < animations->size(); ++index) {
      const json& animation = (*animations)[index];
      Clip clip;
      clip.name = text_of(animation, "name");
      clip.annotations = read_annotations(animation, index);
      read_channels(animation, doc, scene, clip);
      scene.clips.push_back(std::move(clip));
    }
  }
}

std::vector<int> ancestors_of(const Scene& scene, int node) {
  std::vector<int> out;
  for (int parent = scene.nodes[static_cast<std::size_t>(node)].parent; parent >= 0;
       parent = scene.nodes[static_cast<std::size_t>(parent)].parent) {
    out.push_back(parent);
  }
  return out;
}

int joint_parent(const Scene& scene, const std::vector<char>& joints, int node) {
  for (const int ancestor : ancestors_of(scene, node)) {
    if (joints[static_cast<std::size_t>(ancestor)] != 0) return ancestor;
  }
  return -1;
}

Quat unit(const Quat& q) {
  if (!usable_rotation(q)) {
    refuse(HkxGltfStatus::BadRotation,
           "a rotation is zero, tiny or not finite, so it has no direction. Refused rather than substituting identity.");
  }
  return normalised(q);
}

Mat local_matrix(const Vec3& translation, const Quat& rotation, const Vec3& scale) {
  const double units = k_plain_units_per_metre;
  return multiply(multiply(scale_matrix(scale), rotation_matrix(unit(rotation))),
                  translation_matrix({translation[0] * units, translation[1] * units, translation[2] * units}));
}

struct Pose {
  Vec3 translation{0.0, 0.0, 0.0};
  Quat rotation{0.0, 0.0, 0.0, 1.0};
  Vec3 scale{1.0, 1.0, 1.0};
};

bool havok_pose(const Scene& scene, const std::vector<char>& joints, int node, const Vec3& translation,
                const Quat& rotation, const Vec3& scale, Pose& out) {
  Mat helpers = identity();
  for (const int ancestor : ancestors_of(scene, node)) {
    if (joints[static_cast<std::size_t>(ancestor)] != 0) break;
    const Node& helper = scene.nodes[static_cast<std::size_t>(ancestor)];
    helpers = multiply(helpers, local_matrix(helper.translation, helper.rotation, helper.scale));
  }
  Mat world = multiply(local_matrix(translation, rotation, scale), helpers);
  if (joint_parent(scene, joints, node) < 0) world = multiply(world, rotation_x_quarter_turn());
  return decompose(world, out.scale, out.rotation, out.translation);
}

struct Located {
  std::size_t before{0};
  std::size_t after{0};
  double fraction{0.0};
};

Located locate(const std::vector<double>& times, double time) {
  const std::size_t last = times.size() - 1;
  if (time <= times[0]) return {0, 0, 0.0};
  if (time >= times[last]) return {last, last, 0.0};
  const auto next = std::upper_bound(times.begin(), times.end(), time);
  const auto i = static_cast<std::size_t>(next - times.begin()) - 1;
  if (times[i] == time) return {i, i, 0.0};
  return {i, i + 1, (time - times[i]) / (times[i + 1] - times[i])};
}

std::array<double, 4> hermite(const Channel& channel, std::size_t width, const Located& at) {
  const auto& times = channel.times->values;
  const auto& values = channel.values->values;
  const double gap = times[at.after] - times[at.before];
  const auto element = [&](std::size_t index, std::size_t component) { return values[index * width + component]; };
  const double s = at.fraction, s2 = s * s, s3 = s2 * s;
  const double h00 = 2 * s3 - 3 * s2 + 1, h10 = s3 - 2 * s2 + s, h01 = -2 * s3 + 3 * s2, h11 = s3 - s2;
  std::array<double, 4> out{};
  for (std::size_t c = 0; c < width; ++c) {
    out[c] = h00 * element(3 * at.before + 1, c) + h10 * gap * element(3 * at.before + 2, c) +
             h01 * element(3 * at.after + 1, c) + h11 * gap * element(3 * at.after, c);
  }
  return out;
}

Vec3 vec3_at(const Channel& channel, double time) {
  const Located at = locate(channel.times->values, time);
  if (channel.interpolation == "CUBICSPLINE") {
    const auto spline = hermite(channel, 3, at);
    return {spline[0], spline[1], spline[2]};
  }
  const auto& values = channel.values->values;
  const auto key = [&](std::size_t k) { return Vec3{values[k * 3], values[k * 3 + 1], values[k * 3 + 2]}; };
  if (channel.interpolation == "STEP") return key(at.before);
  const Vec3 a = key(at.before), b = key(at.after);
  return {a[0] + (b[0] - a[0]) * at.fraction, a[1] + (b[1] - a[1]) * at.fraction, a[2] + (b[2] - a[2]) * at.fraction};
}

Quat quat_at(const Channel& channel, double time) {
  const Located at = locate(channel.times->values, time);
  if (channel.interpolation == "CUBICSPLINE") {
    const auto spline = hermite(channel, 4, at);
    return unit({spline[0], spline[1], spline[2], spline[3]});
  }
  const auto& values = channel.values->values;
  const auto key = [&](std::size_t k) {
    return unit({values[k * 4], values[k * 4 + 1], values[k * 4 + 2], values[k * 4 + 3]});
  };
  if (channel.interpolation == "STEP") return key(at.before);
  Quat end = key(at.after);
  const Quat start = key(at.before);
  if (dot(start, end) < 0.0) end = negate(end);
  return unit(slerp(start, end, at.fraction));
}

struct Sampler {
  const Channel* translation{nullptr};
  const Channel* rotation{nullptr};
  const Channel* scale{nullptr};
};

Vec3 to_vec3(const std::array<float, 3>& v) { return {v[0], v[1], v[2]}; }

Quat to_quat(const std::array<float, 4>& q) { return {q[0], q[1], q[2], q[3]}; }

Pose pose_at(const Scene& scene, const std::vector<char>& joints, int node, const Sampler& channels,
             const HkxQsTransform& reference, double time) {
  const Node& bone = scene.nodes[static_cast<std::size_t>(node)];
  const Vec3 translation = channels.translation != nullptr ? vec3_at(*channels.translation, time) : bone.translation;
  const Quat rotation = channels.rotation != nullptr ? quat_at(*channels.rotation, time) : bone.rotation;
  const Vec3 scale = channels.scale != nullptr ? vec3_at(*channels.scale, time) : bone.scale;

  Pose havok;
  if (!havok_pose(scene, joints, node, translation, rotation, scale, havok)) {
    refuse(HkxGltfStatus::DegenerateTransform,
           "bone '" + bone.name + "' has a degenerate transform at " + fixed3(time) + " s.");
  }
  Pose out;
  out.translation = channels.translation != nullptr ? havok.translation : to_vec3(reference.translation);
  out.rotation = channels.rotation != nullptr ? havok.rotation : to_quat(reference.rotation);
  out.scale = channels.scale != nullptr ? havok.scale : to_vec3(reference.scale);
  return out;
}

double angle_between(const Quat& a, const Quat& b) {
  const Quat relative = multiply(conjugate(unit(a)), unit(b));
  const double vector = std::sqrt(relative[0] * relative[0] + relative[1] * relative[1] + relative[2] * relative[2]);
  return 2.0 * std::atan2(vector, std::fabs(relative[3]));
}

double gap_between(const Vec3& a, const Vec3& b) {
  return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
}

std::unordered_map<std::string, std::size_t> bone_indices(const HkxSkeleton& skeleton) {
  if (skeleton.bone_names.empty()) refuse(HkxGltfStatus::InvalidSkeleton, "the skeleton has no bones.");
  std::unordered_map<std::string, std::size_t> bones;
  for (std::size_t bone = 0; bone < skeleton.bone_names.size(); ++bone) {
    if (!bones.emplace(skeleton.bone_names[bone], bone).second) {
      refuse(HkxGltfStatus::InvalidSkeleton, "the skeleton names two bones '" + skeleton.bone_names[bone] +
                                                 "', so a glTF bone cannot be matched to one.");
    }
  }
  return bones;
}

std::string label_of(const std::string& name) { return name.empty() ? "(unnamed)" : name; }

const Clip& choose_animation(const Scene& scene, const std::optional<std::string>& name) {
  if (scene.clips.empty()) refuse(HkxGltfStatus::NoAnimation, "the glTF holds no animation.");
  std::vector<std::string> labels;
  for (const Clip& clip : scene.clips) labels.push_back(label_of(clip.name));
  const std::string names = join(labels);
  if (!name) {
    if (scene.clips.size() == 1) return scene.clips[0];
    refuse(HkxGltfStatus::AmbiguousAnimation, "the glTF holds " + std::to_string(scene.clips.size()) + " animations (" +
                                                  names + "). Name the one to import, or export one at a time.");
  }
  std::vector<const Clip*> matches;
  for (const Clip& clip : scene.clips) {
    if (clip.name == *name) matches.push_back(&clip);
  }
  if (matches.size() == 1) return *matches[0];
  if (matches.size() > 1) {
    refuse(HkxGltfStatus::AmbiguousAnimation, "the glTF holds " + std::to_string(matches.size()) +
                                                  " animations named '" + *name + "', so the choice would be a guess.");
  }
  refuse(HkxGltfStatus::NoAnimation, "the glTF has no animation named '" + *name + "'. It holds: " + names + ".");
}

bool has_root_named(const Scene& scene, const Skin& skin, const std::string& name) {
  std::vector<char> set(scene.nodes.size(), 0);
  for (const int joint : skin.joints) set[static_cast<std::size_t>(joint)] = 1;
  for (const int joint : skin.joints) {
    if (joint_parent(scene, set, joint) < 0 && scene.nodes[static_cast<std::size_t>(joint)].name == name) return true;
  }
  return false;
}

std::size_t choose_skin(const Scene& scene, const std::optional<std::string>& choice) {
  if (scene.skins.empty()) {
    refuse(HkxGltfStatus::NoSkin, "the glTF has no skin, so it names no bones. Export the armature with its skin.");
  }
  std::vector<std::string> labels;
  for (std::size_t i = 0; i < scene.skins.size(); ++i) {
    labels.push_back(std::to_string(i) + " (" + label_of(scene.skins[i].name) + ")");
  }
  const std::string names = join(labels);
  if (!choice) {
    if (scene.skins.size() == 1) return 0;
    refuse(HkxGltfStatus::AmbiguousSkin, "the glTF holds " + std::to_string(scene.skins.size()) + " skins: " + names +
                                             ". Name the one to import, by its index or its name.");
  }
  std::vector<std::size_t> matches;
  for (std::size_t i = 0; i < scene.skins.size(); ++i) {
    if (scene.skins[i].name == *choice || std::to_string(i) == *choice || has_root_named(scene, scene.skins[i], *choice)) {
      matches.push_back(i);
    }
  }
  if (matches.size() == 1) return matches[0];
  if (matches.size() > 1) {
    refuse(HkxGltfStatus::AmbiguousSkin, "'" + *choice + "' matches " + std::to_string(matches.size()) +
                                             " skins, so the choice would be a guess. Choose one by its index: " + names + ".");
  }
  refuse(HkxGltfStatus::NoSkin, "the glTF has no skin named or numbered '" + *choice + "'. It holds: " + names + ".");
}

std::vector<HkxAnimationAnnotation> clip_notes(const Clip& clip, float frame_rate, float duration) {
  const float slack = 0.5f / frame_rate;
  std::vector<HkxAnimationAnnotation> notes;
  for (const HkxAnimationAnnotation& note : clip.annotations) {
    if (note.text.empty()) {
      refuse(HkxGltfStatus::BadAnnotation,
             "an annotation at " + fixed3(note.time) + " s has no text, so there is no event to write.");
    }
    if (!(note.time >= 0 && note.time <= duration + slack)) {
      refuse(HkxGltfStatus::BadAnnotation, "annotation '" + note.text + "' at " + fixed3(note.time) +
                                               " s is outside the clip, which runs 0 to " + fixed3(duration) + " s.");
    }
    notes.push_back({std::min(note.time, duration), note.text});
  }
  std::stable_sort(notes.begin(), notes.end(),
                   [](const HkxAnimationAnnotation& a, const HkxAnimationAnnotation& b) { return a.time < b.time; });
  return notes;
}

std::size_t root_bone(const HkxSkeleton& skeleton) {
  std::vector<std::size_t> roots;
  for (std::size_t bone = 0; bone < skeleton.bone_names.size(); ++bone) {
    if (skeleton.parent_indices[bone] < 0) roots.push_back(bone);
  }
  if (roots.size() != 1) {
    refuse(HkxGltfStatus::UnsupportedRoot, "the skeleton has " + std::to_string(roots.size()) +
                                               " root bone(s), so the glTF's root motion has no single root to come from.");
  }
  return roots[0];
}

double wrap_angle(double angle) { return angle - 2.0 * k_pi * std::nearbyint(angle / (2.0 * k_pi)); }

void split_root(HkxInterleavedAnimation& animation, std::size_t root, const std::string& name, float duration,
                HkxExtractedMotion& motion) {
  const std::size_t frames = animation.frame_count;
  const std::size_t tracks = animation.track_count;
  std::vector<double> planar_x(frames), planar_y(frames), height(frames), turns(frames);
  std::vector<Quat> swing(frames);
  double turn = 0.0;
  for (std::size_t f = 0; f < frames; ++f) {
    const Quat q = to_quat(animation.rotations[f * tracks + root]);
    if (std::sqrt(q[2] * q[2] + q[3] * q[3]) < 1e-6) {
      refuse(HkxGltfStatus::UnsupportedRoot,
             "bone '" + name + "' turns half a turn about a horizontal axis at frame " + std::to_string(f) +
                 ", so its turn about up has no direction. Refused rather than guessing the root motion.");
    }
    const double raw = 2.0 * std::atan2(q[2], q[3]);
    turn = f == 0 ? wrap_angle(raw) : turn + wrap_angle(raw - turn);

    Quat upright = normalised(multiply(conjugate(axis_z(turn)), q));
    if (f > 0 && dot(swing[f - 1], upright) < 0.0) upright = negate(upright);
    swing[f] = upright;
    turns[f] = turn;
    planar_x[f] = animation.translations[f * tracks + root][0];
    planar_y[f] = animation.translations[f * tracks + root][1];
    height[f] = animation.translations[f * tracks + root][2];
  }

  motion.kind = HkxMotionKind::Default;
  motion.class_name = "hkaDefaultAnimatedReferenceFrame";
  motion.duration = duration;
  const double start_x = planar_x[0], start_y = planar_y[0];
  const Quat start_turn = axis_z(turns[0]);
  for (std::size_t f = 0; f < frames; ++f) {
    const double relative = turns[f] - turns[0];
    const double rotated_x = start_x * std::cos(relative) - start_y * std::sin(relative);
    const double rotated_y = start_x * std::sin(relative) + start_y * std::cos(relative);
    motion.samples.push_back({static_cast<float>(planar_x[f] - rotated_x), static_cast<float>(planar_y[f] - rotated_y),
                              0.0f, static_cast<float>(relative)});
    const Quat rotation = normalised(multiply(start_turn, swing[f]));
    animation.translations[f * tracks + root] = {static_cast<float>(start_x), static_cast<float>(start_y),
                                                 static_cast<float>(height[f])};
    animation.rotations[f * tracks + root] = {static_cast<float>(rotation[0]), static_cast<float>(rotation[1]),
                                              static_cast<float>(rotation[2]), static_cast<float>(rotation[3])};
  }
}

struct Sampling {
  float position{0.0f};
  float rotation{0.0f};
  float scale{0.0f};
};

Sampling bake_bone(const Scene& scene, const std::vector<char>& joints, int node, const Sampler& channels,
                   const HkxQsTransform& reference, float frame_rate, HkxInterleavedAnimation& animation,
                   std::size_t bone) {
  const std::size_t frames = animation.frame_count;
  const std::size_t tracks = animation.track_count;
  std::optional<Quat> previous;
  for (std::size_t f = 0; f < frames; ++f) {
    const float time = static_cast<float>(f) / frame_rate;
    const Pose pose = pose_at(scene, joints, node, channels, reference, time);
    Quat rotation = pose.rotation;
    if (previous && dot(*previous, rotation) < 0.0) rotation = negate(rotation);
    previous = rotation;

    const std::size_t at = f * tracks + bone;
    animation.translations[at] = {static_cast<float>(pose.translation[0]), static_cast<float>(pose.translation[1]),
                                  static_cast<float>(pose.translation[2])};
    animation.rotations[at] = {static_cast<float>(rotation[0]), static_cast<float>(rotation[1]),
                               static_cast<float>(rotation[2]), static_cast<float>(rotation[3])};
    animation.scales[at] = {static_cast<float>(pose.scale[0]), static_cast<float>(pose.scale[1]),
                            static_cast<float>(pose.scale[2])};
  }

  std::vector<double> keys;
  for (const Channel* channel : {channels.translation, channels.rotation, channels.scale}) {
    if (channel != nullptr) keys.insert(keys.end(), channel->times->values.begin(), channel->times->values.end());
  }
  std::sort(keys.begin(), keys.end());
  keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
  std::vector<double> probes = keys;
  for (std::size_t i = 1; i < keys.size(); ++i) probes.push_back((keys[i - 1] + keys[i]) / 2.0);

  const std::size_t last_frame = frames - 1;
  Sampling worst;
  for (const double time : probes) {
    const Pose want = pose_at(scene, joints, node, channels, reference, time);
    const double at = std::clamp(time * frame_rate, 0.0, static_cast<double>(last_frame));
    const auto before = static_cast<std::size_t>(std::floor(at));
    const std::size_t after = std::min(before + 1, last_frame);
    const double fraction = at - static_cast<double>(before);

    const auto lerp3 = [&](const std::array<float, 3>& a, const std::array<float, 3>& b) {
      return Vec3{a[0] + (b[0] - a[0]) * fraction, a[1] + (b[1] - a[1]) * fraction, a[2] + (b[2] - a[2]) * fraction};
    };
    const Vec3 baked_translation =
        lerp3(animation.translations[before * tracks + bone], animation.translations[after * tracks + bone]);
    const Quat baked_rotation = slerp(to_quat(animation.rotations[before * tracks + bone]),
                                      to_quat(animation.rotations[after * tracks + bone]), fraction);
    const Vec3 baked_scale = lerp3(animation.scales[before * tracks + bone], animation.scales[after * tracks + bone]);

    worst.position = std::max(worst.position, static_cast<float>(gap_between(want.translation, baked_translation)));
    worst.rotation = std::max(worst.rotation, static_cast<float>(angle_between(want.rotation, baked_rotation)));
    worst.scale = std::max(worst.scale, static_cast<float>(gap_between(want.scale, baked_scale)));
  }
  return worst;
}

struct JointChannels {
  int node{0};
  Sampler channels;
};

void build(const Scene& scene, const HkxSkeleton& skeleton, const HkxGltfOptions& options, HkxGltfImport& out) {
  const std::size_t bone_count = skeleton.bone_names.size();
  if (skeleton.parent_indices.size() != bone_count) {
    refuse(HkxGltfStatus::InvalidSkeleton, "the skeleton has " + std::to_string(bone_count) + " bone(s) but " +
                                               std::to_string(skeleton.parent_indices.size()) +
                                               " parent index(es). Refused rather than guessing a hierarchy.");
  }
  if (skeleton.reference_pose.size() != bone_count) {
    refuse(HkxGltfStatus::InvalidSkeleton, "the skeleton has " + std::to_string(bone_count) + " bone(s) but " +
                                               std::to_string(skeleton.reference_pose.size()) +
                                               " reference pose(s). Refused rather than posing bones at a guessed rest.");
  }
  const auto bones = bone_indices(skeleton);

  const Skin& skin = scene.skins[choose_skin(scene, options.skin)];
  std::vector<char> is_joint(scene.nodes.size(), 0), other_joint(scene.nodes.size(), 0);
  for (const int joint : skin.joints) is_joint[static_cast<std::size_t>(joint)] = 1;
  for (const Skin& other : scene.skins) {
    for (const int joint : other.joints) {
      if (is_joint[static_cast<std::size_t>(joint)] == 0) other_joint[static_cast<std::size_t>(joint)] = 1;
    }
  }
  std::unordered_set<std::string> joint_names;
  for (const int joint : skin.joints) {
    const std::string& name = scene.nodes[static_cast<std::size_t>(joint)].name;
    if (name.empty()) {
      refuse(HkxGltfStatus::BadJoint,
             "joint node " + std::to_string(joint) + " has no name, so it cannot be matched to a skeleton bone.");
    }
    if (!joint_names.insert(name).second) {
      refuse(HkxGltfStatus::BadJoint, "the glTF has two joints named '" + name + "', so a match would be a guess.");
    }
  }

  const Clip& clip = choose_animation(scene, options.animation);
  std::vector<JointChannels> by_joint;
  std::map<int, std::size_t> slot;
  for (const Channel& channel : clip.channels) {
    if (channel.path == "weights") {
      ++out.weight_channels;
      continue;
    }
    const std::string& name = scene.nodes[static_cast<std::size_t>(channel.node)].name;
    if (is_joint[static_cast<std::size_t>(channel.node)] == 0) {
      if (other_joint[static_cast<std::size_t>(channel.node)] != 0) {
        refuse(HkxGltfStatus::AnimatedNonBone,
               "node '" + name + "' is animated and is a joint of another skin, not of the chosen one, so its motion " +
                   "cannot land in this skin. Choose that skin instead.");
      }
      for (const int joint : skin.joints) {
        const auto up = ancestors_of(scene, joint);
        if (std::find(up.begin(), up.end(), channel.node) != up.end()) {
          refuse(HkxGltfStatus::AnimatedNonBone,
                 "node '" + name + "' is animated, is not a bone, and has bones below it. Only bones can carry " +
                     "animation here, so bake its motion into the bones.");
        }
      }
      ++out.non_bone_channels;
      continue;
    }

    if (channel.interpolation != "LINEAR" && channel.interpolation != "STEP" && channel.interpolation != "CUBICSPLINE") {
      refuse(HkxGltfStatus::UnsupportedChannel,
             "bone '" + name + "' uses " + channel.interpolation + " keys for its " + channel.path +
                 ". Only LINEAR, STEP and CUBICSPLINE keys are read, so re-export with one of those.");
    }
    auto found = slot.find(channel.node);
    if (found == slot.end()) {
      found = slot.emplace(channel.node, by_joint.size()).first;
      by_joint.push_back({channel.node, {}});
    }
    const Channel*& target = channel.path == "translation" ? by_joint[found->second].channels.translation
                             : channel.path == "rotation"  ? by_joint[found->second].channels.rotation
                                                           : by_joint[found->second].channels.scale;
    if (target != nullptr) {
      refuse(HkxGltfStatus::DuplicateChannel, "bone '" + name + "' has two " + channel.path + " channels.");
    }
    target = &channel;
  }

  if (by_joint.empty()) {
    refuse(HkxGltfStatus::NoAnimatedBone, "no channel animates a bone of the skin, so there is nothing to write.");
  }

  std::vector<std::string> missing;
  for (const JointChannels& joint : by_joint) {
    const std::string& name = scene.nodes[static_cast<std::size_t>(joint.node)].name;
    if (bones.find(name) == bones.end()) missing.push_back(name);
  }
  if (!missing.empty()) {
    refuse(HkxGltfStatus::MissingBone, "the glTF animates bone(s) the skeleton does not have: " + join(missing) +
                                           ". Refused rather than guessing a pairing; rename them to the skeleton's " +
                                           "names or remove them.");
  }

  std::map<std::size_t, const JointChannels*> bone_node;
  for (const JointChannels& joint : by_joint) {
    const std::string& name = scene.nodes[static_cast<std::size_t>(joint.node)].name;
    const std::size_t bone = bones.at(name);
    bone_node[bone] = &joint;

    const int jointParent = joint_parent(scene, is_joint, joint.node);
    const std::optional<std::string> gltf_parent =
        jointParent >= 0 ? std::optional<std::string>(scene.nodes[static_cast<std::size_t>(jointParent)].name)
                         : std::nullopt;
    const int skeleton_parent = skeleton.parent_indices[bone];
    const std::optional<std::string> rig_parent =
        skeleton_parent >= 0 && static_cast<std::size_t>(skeleton_parent) < bone_count
            ? std::optional<std::string>(skeleton.bone_names[static_cast<std::size_t>(skeleton_parent)])
            : std::nullopt;
    if (gltf_parent != rig_parent) {
      refuse(HkxGltfStatus::HierarchyMismatch,
             "bone '" + name + "' sits under " + gltf_parent.value_or("nothing") + " in the glTF but under " +
                 rig_parent.value_or("nothing") + " in the skeleton. Refused rather than guessing a pairing.");
    }

    const Node& local = scene.nodes[static_cast<std::size_t>(joint.node)];
    const HkxQsTransform& reference = skeleton.reference_pose[bone];
    Pose rest;
    if (!havok_pose(scene, is_joint, joint.node, local.translation, local.rotation, local.scale, rest) ||
        gap_between(rest.translation, to_vec3(reference.translation)) > k_rest_position_tolerance ||
        angle_between(rest.rotation, to_quat(reference.rotation)) > k_rest_angle_tolerance ||
        gap_between(rest.scale, to_vec3(reference.scale)) > k_rest_scale_tolerance) {
      out.rest_mismatch.push_back(name);
    }
  }

  float step = std::numeric_limits<float>::max();
  float end = 0.0f;
  for (const JointChannels& joint : by_joint) {
    for (const Channel* channel : {joint.channels.translation, joint.channels.rotation, joint.channels.scale}) {
      if (channel == nullptr) continue;
      const auto& times = channel->times->values;
      for (std::size_t k = 1; k < times.size(); ++k) {
        step = std::min(step, static_cast<float>(times[k]) - static_cast<float>(times[k - 1]));
      }
      end = std::max(end, static_cast<float>(times.back()));
    }
  }

  const float frame_rate =
      step < std::numeric_limits<float>::max()
          ? std::max(k_gltf_min_frame_rate, std::ceil(1.0f / step - 1e-3f))
          : k_gltf_min_frame_rate;
  const float span = end * frame_rate;
  if (!(span < static_cast<float>(k_gltf_max_frames))) {
    refuse(HkxGltfStatus::ClipTooLarge, "the animation runs " + fixed3(end) + " s, more than the supported " +
                                            std::to_string(k_gltf_max_frames) + " frames at " +
                                            std::to_string(frame_rate) + " per second.");
  }
  const auto frames = static_cast<std::size_t>(std::max(0.0f, std::ceil(span - 1e-3f))) + 1;
  if (frames < 2) {
    refuse(HkxGltfStatus::ClipTooLarge, "the animated keys span less than two frames, so there is no interval to store.");
  }
  if (frames * bone_count > k_gltf_max_frame_tracks) {
    refuse(HkxGltfStatus::ClipTooLarge,
           "the clip would be " + std::to_string(frames) + " frame(s) over " + std::to_string(bone_count) +
               " track(s), which is " + std::to_string(frames * bone_count) + " frame-tracks, more than the " +
               std::to_string(k_gltf_max_frame_tracks) + " the read-back allows. Refused before baking.");
  }

  const float duration = static_cast<float>(frames - 1) / frame_rate;
  out.annotations = clip_notes(clip, frame_rate, duration);
  const std::size_t root = root_bone(skeleton);

  HkxInterleavedAnimation& animation = out.animation;
  animation.duration = duration;
  animation.frame_count = frames;
  animation.track_count = bone_count;
  animation.translations.assign(frames * bone_count, {});
  animation.rotations.assign(frames * bone_count, {});
  animation.scales.assign(frames * bone_count, {});

  Sampling sampling;
  for (std::size_t bone = 0; bone < bone_count; ++bone) {
    const HkxQsTransform& reference = skeleton.reference_pose[bone];
    const auto animated = bone_node.find(bone);
    if (animated != bone_node.end()) {
      out.animated.push_back(skeleton.bone_names[bone]);
      const Sampling error = bake_bone(scene, is_joint, animated->second->node, animated->second->channels, reference,
                                       frame_rate, animation, bone);
      sampling.position = std::max(sampling.position, error.position);
      sampling.rotation = std::max(sampling.rotation, error.rotation);
      sampling.scale = std::max(sampling.scale, error.scale);
    } else {
      out.reference_only.push_back(skeleton.bone_names[bone]);
      for (std::size_t f = 0; f < frames; ++f) {
        animation.translations[f * bone_count + bone] = reference.translation;
        animation.rotations[f * bone_count + bone] = reference.rotation;
        animation.scales[f * bone_count + bone] = reference.scale;
      }
    }
  }

  if (bone_node.count(root) != 0) {
    split_root(animation, root, skeleton.bone_names[root], duration, out.motion);
  } else {
    out.motion.kind = HkxMotionKind::Default;
    out.motion.class_name = "hkaDefaultAnimatedReferenceFrame";
    out.motion.duration = duration;
    out.motion.samples.assign(frames, {0.0f, 0.0f, 0.0f, 0.0f});
  }

  for (const int joint : skin.joints) {
    const std::string& name = scene.nodes[static_cast<std::size_t>(joint)].name;
    if (bones.find(name) == bones.end()) out.unmapped.push_back(name);
  }
  out.frame_rate = frame_rate;
  out.sampling_position = sampling.position;
  out.sampling_rotation = sampling.rotation;
  out.sampling_scale = sampling.scale;
}

void check_buffers(Document& doc) {
  const json* buffers = array_at(doc.json, "buffers");
  if (buffers == nullptr) return;
  for (std::size_t i = 0; i < doc.buffers.size(); ++i) {
    const auto held = static_cast<std::int64_t>(doc.buffers[i].size());
    const auto declared = int_of((*buffers)[i], "byteLength", held);
    if (declared < 0 || declared > held) {
      refuse(HkxGltfStatus::Malformed, "buffer " + std::to_string(i) + " declares " + std::to_string(declared) +
                                           " byte(s), but holds " + std::to_string(held) + ".");
    }
    doc.buffers[i].resize(static_cast<std::size_t>(declared));
  }
}

}  // namespace

const char* hkx_gltf_status_name(HkxGltfStatus status) {
  switch (status) {
    case HkxGltfStatus::Ok: return "Ok";
    case HkxGltfStatus::Malformed: return "Malformed";
    case HkxGltfStatus::SparseAccessor: return "SparseAccessor";
    case HkxGltfStatus::UnsupportedAccessor: return "UnsupportedAccessor";
    case HkxGltfStatus::UnsupportedChannel: return "UnsupportedChannel";
    case HkxGltfStatus::InvalidSkeleton: return "InvalidSkeleton";
    case HkxGltfStatus::NoSkin: return "NoSkin";
    case HkxGltfStatus::AmbiguousSkin: return "AmbiguousSkin";
    case HkxGltfStatus::NoAnimation: return "NoAnimation";
    case HkxGltfStatus::AmbiguousAnimation: return "AmbiguousAnimation";
    case HkxGltfStatus::BadJoint: return "BadJoint";
    case HkxGltfStatus::MissingBone: return "MissingBone";
    case HkxGltfStatus::HierarchyMismatch: return "HierarchyMismatch";
    case HkxGltfStatus::AnimatedNonBone: return "AnimatedNonBone";
    case HkxGltfStatus::DuplicateChannel: return "DuplicateChannel";
    case HkxGltfStatus::NoAnimatedBone: return "NoAnimatedBone";
    case HkxGltfStatus::BadRotation: return "BadRotation";
    case HkxGltfStatus::BadKeyTimes: return "BadKeyTimes";
    case HkxGltfStatus::BadAnnotation: return "BadAnnotation";
    case HkxGltfStatus::ClipTooLarge: return "ClipTooLarge";
    case HkxGltfStatus::DegenerateTransform: return "DegenerateTransform";
    case HkxGltfStatus::UnsupportedRoot: return "UnsupportedRoot";
  }
  return "Unknown";
}

HkxGltfStatus import_gltf_animation(std::span<const std::byte> bytes, const HkxSkeleton& skeleton,
                                    const HkxGltfOptions& options, HkxGltfImport& out) {
  out = HkxGltfImport{};
  try {
    HkxGltfUriLoader beside_gltf;
    if (options.load_uri) {
      beside_gltf = [&options](std::string_view path) {
        const bool rooted = !path.empty() && (path.front() == '/' || path.front() == '\\');
        const bool drive = path.size() >= 2 && path[1] == ':';
        if (rooted || drive) {
          throw std::runtime_error("glTF buffer '" + std::string(path) + "' must be a relative file beside the glTF");
        }
        return options.load_uri(path);
      };
    }
    Document doc = nif::gltf_detail::open_document(bytes, beside_gltf);
    check_buffers(doc);
    Scene scene;
    read_scene(doc, scene);
    build(scene, skeleton, options, out);
    return HkxGltfStatus::Ok;
  } catch (const Refusal& refusal) {
    out = HkxGltfImport{};
    out.message = refusal.message;
    return refusal.status;
  } catch (const std::exception& error) {
    out = HkxGltfImport{};
    out.message = error.what();
    return HkxGltfStatus::Malformed;
  }
}

}  // namespace fo4::hkx
