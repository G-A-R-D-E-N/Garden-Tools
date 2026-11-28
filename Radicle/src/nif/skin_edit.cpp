#include "fo4/nif/skin.hpp"

#include "skin_weights.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::nif {
namespace {

using shape_detail::Layout;
using shape_detail::Vec3;
using skin_detail::Skin;
using skin_detail::Skins;
using write_detail::Bytes;

constexpr float k_minimum_cell = 1e-4f;
constexpr float k_bone_tolerance_fraction = 0.02f;

[[noreturn]] void malformed(const std::string& text) { throw std::invalid_argument(text); }

[[noreturn]] void refused(const std::string& text) { throw std::runtime_error(text); }

float lane_of(const Vec3& point, std::uint8_t lane) { return point[lane]; }

// Invariant: reflecting through the plane of one lane is its own inverse, so partners pair both ways.
Vec3 reflect(const Vec3& point, std::uint8_t lane) {
  Vec3 out = point;
  out[lane] = -out[lane];
  return out;
}

double squared_distance(const Vec3& a, const Vec3& b) {
  double sum = 0.0;
  for (std::size_t axis = 0; axis < 3; ++axis) {
    const double offset = static_cast<double>(a[axis]) - static_cast<double>(b[axis]);
    sum += offset * offset;
  }
  return sum;
}

float extent_of(const std::vector<Vec3>& points) {
  if (points.empty()) return 1.0f;
  Vec3 low = points.front();
  Vec3 high = points.front();
  for (const auto& point : points) {
    for (std::size_t axis = 0; axis < 3; ++axis) {
      low[axis] = std::min(low[axis], point[axis]);
      high[axis] = std::max(high[axis], point[axis]);
    }
  }
  const float size = std::max(high[0] - low[0], std::max(high[1] - low[1], high[2] - low[2]));
  return std::max(1e-3f, size);
}

bool starts_word(std::string_view name, std::size_t at) {
  if (at == 0) return true;
  const unsigned char before = static_cast<unsigned char>(name[at - 1]);
  if (!std::isalnum(before)) return true;
  const unsigned char here = static_cast<unsigned char>(name[at]);
  return std::isupper(here) && !std::isupper(before);
}

std::vector<std::string> flipped_names(std::string_view name) {
  std::vector<std::string> out;
  for (std::size_t at = 0; at < name.size(); ++at) {
    const char here = name[at];
    char other = '\0';
    if (here == 'L') other = 'R';
    if (here == 'R') other = 'L';
    if (here == 'l') other = 'r';
    if (here == 'r') other = 'l';
    if (other == '\0' || !starts_word(name, at)) continue;
    std::string flipped{name};
    flipped[at] = other;
    out.push_back(std::move(flipped));
  }
  return out;
}

std::string lower(std::string_view text) {
  std::string out{text};
  for (char& value : out) value = static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
  return out;
}

// Invariant: each entry is the reflected vertex's candidate count, so 0 and 2+ explain a skip.
std::vector<std::int32_t> vertex_partners(const std::vector<Vec3>& points, std::uint8_t lane, float tolerance,
                                          std::vector<std::int32_t>& partner, std::vector<float>& apart) {
  partner.assign(points.size(), -1);
  apart.assign(points.size(), 0.0f);
  std::vector<std::int32_t> candidates(points.size(), 0);
  const float cell = std::max(tolerance, k_minimum_cell) * 2.0f;
  auto bucket_of = [cell](const Vec3& point) {
    return std::array<std::int64_t, 3>{static_cast<std::int64_t>(std::floor(point[0] / cell)),
                                       static_cast<std::int64_t>(std::floor(point[1] / cell)),
                                       static_cast<std::int64_t>(std::floor(point[2] / cell))};
  };
  std::map<std::array<std::int64_t, 3>, std::vector<std::size_t>> grid;
  for (std::size_t index = 0; index < points.size(); ++index) grid[bucket_of(points[index])].push_back(index);

  for (std::size_t index = 0; index < points.size(); ++index) {
    const auto wanted = reflect(points[index], lane);
    const auto centre = bucket_of(wanted);
    std::int64_t found = -1;
    std::int32_t count = 0;
    double nearest = std::numeric_limits<double>::infinity();
    for (std::int64_t dx = -1; dx <= 1; ++dx) {
      for (std::int64_t dy = -1; dy <= 1; ++dy) {
        for (std::int64_t dz = -1; dz <= 1; ++dz) {
          const auto bucket = grid.find({centre[0] + dx, centre[1] + dy, centre[2] + dz});
          if (bucket == grid.end()) continue;
          for (const auto other : bucket->second) {
            if (other == index) continue;
            const auto distance = squared_distance(points[other], wanted);
            if (distance > static_cast<double>(tolerance) * static_cast<double>(tolerance)) continue;
            ++count;
            if (distance >= nearest) continue;
            nearest = distance;
            found = static_cast<std::int64_t>(other);
          }
        }
      }
    }
    candidates[index] = count;
    if (count != 1) continue;
    partner[index] = static_cast<std::int32_t>(found);
    apart[index] = static_cast<float>(std::sqrt(nearest));
  }
  return candidates;
}

std::vector<Vec3> bone_points(const Shape& shape, const Skins& skins, const std::vector<Vec3>& points,
                              std::vector<bool>& known) {
  std::vector<Vec3> centres(shape.bones.size());
  std::vector<float> totals(shape.bones.size(), 0.0f);
  for (std::size_t vertex = 0; vertex < skins.size() && vertex < points.size(); ++vertex) {
    for (std::size_t slot = 0; slot < k_max_influences_per_vertex; ++slot) {
      const float weight = skins[vertex].weight[slot];
      if (!(weight > 0.0f)) continue;
      const auto bone = static_cast<std::size_t>(skins[vertex].bone[slot]);
      if (bone >= centres.size()) continue;
      for (std::size_t axis = 0; axis < 3; ++axis) centres[bone][axis] += points[vertex][axis] * weight;
      totals[bone] += weight;
    }
  }
  for (std::size_t bone = 0; bone < centres.size(); ++bone) {
    if (totals[bone] > 0.0f) {
      for (std::size_t axis = 0; axis < 3; ++axis) centres[bone][axis] /= totals[bone];
      continue;
    }
    known[bone] = false;
  }
  return centres;
}

SkinEditReport report_of(const SkinEditReport& report, std::vector<std::string> skipped) {
  SkinEditReport out = report;
  out.skipped = std::move(skipped);
  return out;
}

Skin skin_from(const std::vector<Influence>& kept) {
  Skin skin;
  for (std::size_t slot = 0; slot < kept.size() && slot < k_max_influences_per_vertex; ++slot) {
    skin.bone[slot] = static_cast<std::uint8_t>(kept[slot].bone);
    skin.weight[slot] = kept[slot].weight;
  }
  return skin;
}

std::vector<Influence> checked_influences(const Shape& shape, const std::vector<Influence>& influences) {
  std::vector<Influence> kept;
  for (const auto& influence : influences) {
    if (!std::isfinite(influence.weight)) malformed("weights must be finite");
    if (!(influence.weight > 0.0f)) continue;
    if (influence.bone < 0 || static_cast<std::size_t>(influence.bone) >= shape.bones.size() ||
        influence.bone > 255) {
      malformed("this shape is weighted to " + std::to_string(shape.bones.size()) + " bone(s), so there is no bone " +
                std::to_string(influence.bone));
    }
    kept.push_back(influence);
  }
  if (kept.size() > k_max_influences_per_vertex) {
    malformed("a vertex holds " + std::to_string(k_max_influences_per_vertex) + " influences and this names " +
              std::to_string(kept.size()));
  }
  std::sort(kept.begin(), kept.end(),
            [](const Influence& a, const Influence& b) { return a.weight > b.weight; });
  for (std::size_t index = 1; index < kept.size(); ++index) {
    if (kept[index - 1].bone == kept[index].bone) malformed("the same bone appears twice on one vertex");
  }
  return kept;
}

std::vector<Influence> positives_of(const Skin& skin) {
  std::vector<Influence> found;
  for (std::size_t slot = 0; slot < k_max_influences_per_vertex; ++slot) {
    if (!(skin.weight[slot] > 0.0f)) continue;
    found.push_back(Influence{static_cast<std::int32_t>(skin.bone[slot]), skin.weight[slot]});
  }
  return found;
}

// Invariant: a rebuilt vertex keeps the weight it had before the copy, so the sum still holds.
Skin rebuilt_copy(const std::vector<Influence>& influences, std::int32_t from_bone, std::int32_t to_bone,
                  bool keep_source) {
  std::vector<Influence> rebuilt;
  float source = 0.0f;
  for (const auto& influence : influences) {
    if (influence.bone == from_bone) {
      source += influence.weight;
      if (!keep_source) continue;
    }
    rebuilt.push_back(influence);
  }
  if (!(source > 0.0f)) return skin_from(influences);

  bool landed = false;
  for (auto& influence : rebuilt) {
    if (influence.bone != to_bone) continue;
    influence.weight += source;
    landed = true;
  }
  if (!landed) rebuilt.push_back(Influence{to_bone, source});
  if (rebuilt.size() > k_max_influences_per_vertex) {
    std::sort(rebuilt.begin(), rebuilt.end(),
              [](const Influence& a, const Influence& b) { return a.weight > b.weight; });
    rebuilt.resize(k_max_influences_per_vertex);
  }
  return skin_from(rebuilt);
}

}  // namespace

std::vector<Influence> influences_of(const Shape& shape, std::uint32_t vertex) {
  const auto count = shape.verts.size() / 3;
  const auto slots = count * k_max_influences_per_vertex;
  if (shape.weights.size() != slots || shape.bone_indices.size() != slots) {
    malformed("the shape's weight arrays do not cover its " + std::to_string(count) + " vertices");
  }
  if (static_cast<std::size_t>(vertex) >= count) {
    malformed("there is no vertex " + std::to_string(vertex));
  }
  std::vector<Influence> found;
  for (std::size_t slot = 0; slot < k_max_influences_per_vertex; ++slot) {
    const auto at = static_cast<std::size_t>(vertex) * k_max_influences_per_vertex + slot;
    if (!(shape.weights[at] > 0.0f)) continue;
    found.push_back(Influence{static_cast<std::int32_t>(shape.bone_indices[at]), shape.weights[at]});
  }
  std::sort(found.begin(), found.end(),
            [](const Influence& a, const Influence& b) { return a.weight > b.weight; });
  return found;
}

BoneInfluence influenced_by(const Shape& shape, std::int32_t bone, float minimum) {
  BoneInfluence found;
  found.bone = bone;
  found.name = bone >= 0 && static_cast<std::size_t>(bone) < shape.bones.size()
                   ? shape.bones[static_cast<std::size_t>(bone)].name
                   : "bone " + std::to_string(bone);
  if (!std::isfinite(minimum) || minimum < 0.0f) malformed("minimum must be at least zero");

  const auto count = shape.verts.size() / 3;
  const auto slots = count * k_max_influences_per_vertex;
  if (bone < 0 || shape.weights.size() != slots || shape.bone_indices.size() != slots) return found;

  bool first = true;
  for (std::size_t vertex = 0; vertex < count; ++vertex) {
    for (std::size_t slot = 0; slot < k_max_influences_per_vertex; ++slot) {
      const auto at = vertex * k_max_influences_per_vertex + slot;
      if (static_cast<std::int32_t>(shape.bone_indices[at]) != bone) continue;
      const float weight = shape.weights[at];
      if (!(weight >= minimum) || !(weight > 0.0f)) continue;
      found.vertices.push_back(static_cast<std::uint32_t>(vertex));
      found.weight += weight;
      for (std::size_t axis = 0; axis < 3; ++axis) {
        const float value = shape.verts[vertex * 3 + axis];
        found.min[axis] = first ? value : std::min(found.min[axis], value);
        found.max[axis] = first ? value : std::max(found.max[axis], value);
      }
      first = false;
      break;
    }
  }
  return found;
}

SkeletonPairs skeleton_pairs(std::span<const std::string> names) {
  std::map<std::string, std::int32_t> by_name;
  for (std::size_t index = 0; index < names.size(); ++index) {
    by_name.emplace(lower(names[index]), static_cast<std::int32_t>(index));
  }
  SkeletonPairs pairing;
  pairing.partner.resize(names.size());
  for (std::size_t index = 0; index < names.size(); ++index) {
    pairing.partner[index] = static_cast<std::int32_t>(index);
  }

  for (std::size_t index = 0; index < names.size(); ++index) {
    const auto flipped = flipped_names(names[index]);
    if (flipped.empty()) continue;
    std::vector<std::int32_t> found;
    for (const auto& name : flipped) {
      const auto entry = by_name.find(lower(name));
      if (entry == by_name.end() || entry->second == static_cast<std::int32_t>(index)) continue;
      if (std::find(found.begin(), found.end(), entry->second) == found.end()) found.push_back(entry->second);
    }
    if (found.empty()) continue;
    if (found.size() > 1) {
      pairing.partner[index] = -1;
      pairing.ambiguous.push_back(names[index]);
      continue;
    }
    pairing.partner[index] = found.front();
  }

  for (std::size_t index = 0; index < names.size(); ++index) {
    const auto other = pairing.partner[index];
    if (other < 0 || other == static_cast<std::int32_t>(index)) continue;
    if (pairing.partner[static_cast<std::size_t>(other)] == static_cast<std::int32_t>(index)) continue;
    pairing.partner[index] = -1;
    pairing.ambiguous.push_back(names[index]);
  }

  for (std::size_t index = 0; index < names.size(); ++index) {
    const auto other = pairing.partner[index];
    if (other > static_cast<std::int32_t>(index)) ++pairing.pairs;
  }
  return pairing;
}

std::optional<SkinEditKind> parse_skin_edit_kind(std::string_view name) noexcept {
  if (name == "set_influences") return SkinEditKind::set_influences;
  if (name == "copy_bone") return SkinEditKind::copy_bone;
  if (name == "mirror_weights") return SkinEditKind::mirror_weights;
  return std::nullopt;
}

const char* skin_edit_kind_name(SkinEditKind kind) noexcept {
  switch (kind) {
    case SkinEditKind::set_influences:
      return "set_influences";
    case SkinEditKind::copy_bone:
      return "copy_bone";
    case SkinEditKind::mirror_weights:
      return "mirror_weights";
  }
  return "";
}

std::vector<std::byte> apply_skin_edit(const NifFile& nif, const SkinEditRequest& request, SkinEditReport* report) {
  if (request.block >= nif.blocks().size()) malformed("there is no block " + std::to_string(request.block));
  if (!is_shape_block_type(nif.blocks()[request.block].type)) malformed("block must be a tri-shape block");

  const auto shape = read_shape(nif, request.block);
  if (shape.verts.empty()) refused("block " + std::to_string(request.block) + " has no vertex data");
  const auto layout = shape_detail::read_layout(nif, request.block);
  if (!layout.skinned || shape.skin_ref < 0) {
    refused("block " + std::to_string(request.block) + " has no skin");
  }
  if (shape.bones.empty()) refused("the skin has no bones");

  const auto before = skin_detail::read_skins(shape);
  auto after = before;
  SkinEditReport result;
  result.block = request.block;
  std::vector<std::string> skipped;

  switch (request.kind) {
    case SkinEditKind::set_influences: {
      if (static_cast<std::size_t>(request.vertex) >= before.size()) {
        malformed("there is no vertex " + std::to_string(request.vertex));
      }
      after[request.vertex] = skin_from(checked_influences(shape, request.influences));
      break;
    }
    case SkinEditKind::copy_bone: {
      const auto bones = static_cast<std::int32_t>(shape.bones.size());
      if (request.from_bone < 0 || request.from_bone >= bones) malformed("from_bone is not a bone of this shape");
      if (request.to_bone < 0 || request.to_bone >= bones) malformed("to_bone is not a bone of this shape");
      if (request.from_bone == request.to_bone) malformed("a bone cannot be copied onto itself");
      for (std::size_t vertex = 0; vertex < after.size(); ++vertex) {
        after[vertex] = rebuilt_copy(positives_of(before[vertex]), request.from_bone, request.to_bone,
                                     request.keep_source);
      }
      break;
    }
    case SkinEditKind::mirror_weights: {
      if (request.lane > 2) malformed("the mirror plane is X, Y or Z");
      if (!(request.vertex_tolerance > 0.0f)) malformed("vertex_tolerance must be above zero");
      const auto points = skin_detail::global_vertex_points(nif, request.block, shape.verts);
      const float tolerance = request.vertex_tolerance;
      const float bone_tolerance =
          request.bone_tolerance > 0.0f ? request.bone_tolerance : extent_of(points) * k_bone_tolerance_fraction;

      std::vector<std::string> names;
      names.reserve(shape.bones.size());
      for (const auto& bone : shape.bones) names.push_back(bone.name);
      const auto guessed = skeleton_pairs(names);

      std::vector<bool> known(shape.bones.size(), true);
      const auto centres = bone_points(shape, before, points, known);
      auto confirmed = guessed.partner;
      std::vector<std::string> dropped;
      auto drop = [&confirmed, &skipped, &names, &dropped, &centres, lane = request.lane](std::size_t bone) {
        if (confirmed[bone] < 0) return;
        confirmed[bone] = -1;
        if (bone < names.size()) skipped.push_back(names[bone]);
        if (bone < centres.size()) {
          dropped.push_back(names[bone] + " at " + std::to_string(lane_of(centres[bone], lane)));
        }
      };
      for (std::size_t bone = 0; bone < confirmed.size(); ++bone) {
        const auto other = confirmed[bone];
        if (other < 0 || !known[bone]) continue;
        if (other == static_cast<std::int32_t>(bone)) {
          if (std::fabs(lane_of(centres[bone], request.lane)) > bone_tolerance) drop(bone);
          continue;
        }
        if (!known[static_cast<std::size_t>(other)]) continue;
        // Constraint: a seeded target vertex pulls its bone's centroid, so opposite sides of the plane is the test.
        const float mine = lane_of(centres[bone], request.lane);
        const float theirs = lane_of(centres[static_cast<std::size_t>(other)], request.lane);
        const bool opposite = (mine > bone_tolerance && theirs < -bone_tolerance) ||
                              (mine < -bone_tolerance && theirs > bone_tolerance);
        const bool both_on_plane = std::fabs(mine) <= bone_tolerance && std::fabs(theirs) <= bone_tolerance;
        if (opposite || both_on_plane) continue;
        drop(bone);
        drop(static_cast<std::size_t>(other));
      }
      for (std::size_t bone = 0; bone < confirmed.size(); ++bone) {
        const auto other = confirmed[bone];
        if (other < 0) continue;
        if (other != static_cast<std::int32_t>(bone) &&
            confirmed[static_cast<std::size_t>(other)] != static_cast<std::int32_t>(bone)) {
          drop(bone);
        }
      }
      result.unconfirmed_bone = skipped.size();

      std::vector<std::int32_t> partner;
      std::vector<float> apart;
      const auto candidates = vertex_partners(points, request.lane, tolerance, partner, apart);

      for (std::size_t vertex = 0; vertex < after.size(); ++vertex) {
        const float side = lane_of(points[vertex], request.lane);
        if (request.from_positive_side ? side >= 0.0f : side <= 0.0f) continue;
        const auto source = partner[vertex];
        if (source < 0) {
          if (candidates[vertex] == 0) {
            ++result.no_partner_vertex;
          } else {
            ++result.ambiguous_vertex;
          }
          continue;
        }
        auto mirrored = positives_of(before[static_cast<std::size_t>(source)]);
        bool usable = !mirrored.empty();
        for (auto& influence : mirrored) {
          if (influence.bone < 0 || static_cast<std::size_t>(influence.bone) >= confirmed.size()) {
            usable = false;
            break;
          }
          const auto other = confirmed[static_cast<std::size_t>(influence.bone)];
          if (other < 0) {
            usable = false;
            break;
          }
          influence.bone = other;
        }
        if (!usable) {
          if (!mirrored.empty()) ++result.no_partner_bone;
          continue;
        }
        after[vertex] = skin_from(mirrored);
        ++result.mirrored;
        result.worst = std::max(result.worst, apart[static_cast<std::size_t>(source)]);
      }
      // Constraint: a mirror that writes nothing must name its counters, or the writer's refusal hides why.
      if (result.mirrored == 0) {
        std::string why;
        for (const auto& entry : dropped) why += (why.empty() ? "" : ", ") + entry;
        refused("the mirror wrote no vertex: " + std::to_string(result.no_partner_vertex) +
                " have no partner vertex, " + std::to_string(result.ambiguous_vertex) + " are ambiguous, " +
                std::to_string(result.no_partner_bone) + " partner on a bone with no opposite number, " +
                std::to_string(skipped.size()) + " bones skipped" + (why.empty() ? "" : " (" + why + ")"));
      }
      break;
    }
  }

  auto model = write_detail::load_model(nif);
  mesh_detail::Delta delta;
  if (const auto reason = skin_detail::write_skin_bytes(model.blocks[request.block], layout, before, after, &delta)) {
    refused(*reason);
  }
  result.vertices_changed = delta.vertices_changed;
  result.max_weight_change = delta.max_weight_change;
  result = report_of(result, std::move(skipped));

  const auto bytes = write_detail::serialize(model);
  const auto reread = read_shape(read_nif(bytes), request.block);
  if (reread.verts.size() != shape.verts.size() || reread.tris.size() != shape.tris.size()) {
    throw std::runtime_error("internal error: the edited shape reads back with different counts");
  }
  if (report != nullptr) *report = std::move(result);
  return bytes;
}

}  // namespace fo4::nif
