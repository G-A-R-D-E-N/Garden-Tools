#include "fo4/lod/object_candidates.hpp"

#include "fo4/constants.hpp"
#include "fo4/environment/base_records.hpp"
#include "fo4/environment/material_swap.hpp"
#include "fo4/environment/placed.hpp"
#include "fo4/environment/world.hpp"
#include "fo4/flags.hpp"
#include "fo4/formid.hpp"
#include "fo4/lod/lod.hpp"
#include "fo4/lod/parallel.hpp"
#include "fo4/signature.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace fo4::lod {
namespace {

constexpr std::string_view k_multiref_keyword = "MultirefLOD";
// Constraint: parent record flag 0x100 (LOD Respects Enable State) tags the shape; 27 of 27 vanilla toggled shapes agree.
constexpr std::uint32_t k_lod_respects_enable_state = 1u << 8;

constexpr std::string_view k_skip_names[] = {
    "missing_position", "unsupported_base", "no_lod_model", "not_lod_base", "multiref_child", "malformed",
};
static_assert(std::size(k_skip_names) == k_lod_candidate_skip_count);

std::size_t slot_for_level(std::int32_t level) {
  switch (level) {
    case 4:
      return 0;
    case 8:
      return 1;
    case 16:
      return 2;
    case 32:
      return 3;
  }
  throw LodError("object LOD candidate level must be 4, 8, 16 or 32");
}

std::optional<std::int32_t> cell_index(float coordinate) {
  const auto cell = std::floor(static_cast<double>(coordinate) / k_cell_world_units);
  if (!(cell >= INT32_MIN && cell <= INT32_MAX)) return std::nullopt;
  return static_cast<std::int32_t>(cell);
}

// Invariant: a world holds thousands of references to a few hundred bases, so a decoded base is reused.
struct BaseCache {
  std::map<FormKey, const env::RecordRef*> winners;
  std::map<FormKey, std::optional<env::StaticDistantLod>> distant_lod;
  std::map<FormKey, std::optional<std::vector<env::StaticCollectionPart>>> collections;
  std::map<FormKey, std::optional<FormKey>> reference_bases;
  std::optional<FormKey> multiref_keyword;
};

const env::RecordRef* cached_winner(const env::Environment& environment, BaseCache& cache, const FormKey& key) {
  const auto found = cache.winners.find(key);
  if (found != cache.winners.end()) return found->second;
  const auto* winner = environment.winner(key);
  cache.winners.emplace(key, winner);
  return winner;
}

// Invariant: a base that cannot decode is cached as absent, so one malformed STAT is not decoded per reference.
const env::StaticDistantLod* cached_distant_lod(const env::Environment& environment, BaseCache& cache,
                                                const env::RecordRef& base, bool& malformed) {
  auto found = cache.distant_lod.find(base.record->form_key);
  if (found == cache.distant_lod.end()) {
    std::optional<env::StaticDistantLod> decoded;
    try {
      decoded = env::decode_static_distant_lod(environment, base);
    } catch (const env::WorldDecodeError&) {
      decoded.reset();
    }
    found = cache.distant_lod.emplace(base.record->form_key, std::move(decoded)).first;
  }
  malformed = !found->second.has_value();
  return found->second.has_value() ? &*found->second : nullptr;
}

using Basis = std::array<float, 9>;

Basis compose_basis(const Basis& outer, const Basis& inner) {
  Basis out{};
  for (std::size_t row = 0; row < 3; ++row) {
    for (std::size_t column = 0; column < 3; ++column) {
      float sum = 0.0f;
      for (std::size_t axis = 0; axis < 3; ++axis) sum += outer[row * 3 + axis] * inner[axis * 3 + column];
      out[row * 3 + column] = sum;
    }
  }
  return out;
}

std::array<float, 3> apply_basis(const Basis& basis, const std::array<float, 3>& vector) {
  return {basis[0] * vector[0] + basis[1] * vector[1] + basis[2] * vector[2],
          basis[3] * vector[0] + basis[4] * vector[1] + basis[5] * vector[2],
          basis[6] * vector[0] + basis[7] * vector[1] + basis[8] * vector[2]};
}

bool cell_of(const std::array<float, 3>& position, std::int32_t& cell_x, std::int32_t& cell_y) {
  if (!std::isfinite(position[2])) return false;
  const auto x = cell_index(position[0]);
  const auto y = cell_index(position[1]);
  if (!x || !y) return false;
  cell_x = *x;
  cell_y = *y;
  return true;
}

void skip(LodCandidateCollection& out, LodCandidateSkip reason) {
  ++out.counts.skipped[static_cast<std::size_t>(reason)];
}

std::uint32_t runtime_form_id(const env::Environment& environment, const FormKey& key) {
  std::uint32_t full = 0;
  std::uint32_t light = 0;
  for (const auto& info : environment.plugins()) {
    if (info.key == key.owner()) {
      const auto local = key.local().value();
      if (info.kind == PluginKind::light) {
        if (light > 0xFFFu) return 0;
        return 0xFE000000u | (light << 12) | (local & k_light_local_mask);
      }
      if (full > 0xFDu) return 0;
      return (full << 24) | (local & k_full_local_mask);
    }
    (info.kind == PluginKind::light ? light : full) += 1;
  }
  return 0;
}

std::uint32_t toggle_ref_of(const env::Environment& environment, const env::PlacedReferenceData& placed) {
  if (!placed.enable_parent) return 0;
  const auto* parent = environment.winner(placed.enable_parent->reference);
  if (parent == nullptr || !parent->record->header.flags.test(k_lod_respects_enable_state)) return 0;
  return runtime_form_id(environment, placed.enable_parent->reference);
}

std::vector<std::pair<std::string, std::string>> material_swap_of(const env::Environment& environment,
                                                                  const env::RecordRef& ref) {
  std::vector<std::pair<std::string, std::string>> out;
  try {
    const auto key = env::placed_material_swap(environment, ref);
    const auto* swap = key ? environment.winner(*key) : nullptr;
    if (swap == nullptr) return out;
    for (const auto& substitution : env::decode_material_swap(environment, *swap)) {
      if (substitution.replacement_material) {
        out.emplace_back(std::string{substitution.original_material}, std::string{*substitution.replacement_material});
      }
    }
  } catch (const env::WorldDecodeError&) {
    out.clear();
  }
  return out;
}

bool link_target_has_mesh(const env::Environment& environment, BaseCache& cache, const FormKey& reference,
                          std::size_t slot) {
  auto found = cache.reference_bases.find(reference);
  if (found == cache.reference_bases.end()) {
    std::optional<FormKey> base;
    if (const auto* target = cached_winner(environment, cache, reference)) {
      try {
        base = env::decode_placed_reference(environment, *target).base;
      } catch (const env::WorldDecodeError&) {
        base.reset();
      }
    }
    found = cache.reference_bases.emplace(reference, std::move(base)).first;
  }
  if (!found->second) return false;
  const auto* base = cached_winner(environment, cache, *found->second);
  if (base == nullptr || base->record->header.signature != sig_stat) return false;
  bool malformed = false;
  const auto* distant = cached_distant_lod(environment, cache, *base, malformed);
  return distant != nullptr && distant->meshes[slot].has_value();
}

// Invariant: only the first MultirefLOD link counts.
bool multiref_child(const env::Environment& environment, BaseCache& cache, const env::PlacedReferenceData& placed,
                    std::size_t slot) {
  if (!cache.multiref_keyword) return false;
  for (const auto& link : placed.linked_references) {
    if (link.keyword == *cache.multiref_keyword) return link_target_has_mesh(environment, cache, link.reference, slot);
  }
  return false;
}

// Invariant: a collection contributes one candidate per part placement with the composed transform.
void collect_scol(const env::Environment& environment, const env::RecordRef& ref,
                  const env::RecordRef& base, const env::PlacedReferenceData& placed, std::int32_t cell_x,
                  std::int32_t cell_y, std::size_t slot, bool persistent, BaseCache& cache,
                  LodCandidateCollection& out) {
  auto found = cache.collections.find(base.record->form_key);
  if (found == cache.collections.end()) {
    std::optional<std::vector<env::StaticCollectionPart>> decoded;
    try {
      decoded = env::decode_static_collection(environment, base);
    } catch (const env::WorldDecodeError&) {
      decoded.reset();
    }
    found = cache.collections.emplace(base.record->form_key, std::move(decoded)).first;
  }
  if (!found->second.has_value()) {
    skip(out, LodCandidateSkip::malformed);
    return;
  }
  const auto& parts = *found->second;
  if (parts.empty()) {
    skip(out, LodCandidateSkip::no_lod_model);
    return;
  }
  if (multiref_child(environment, cache, placed, slot)) {
    skip(out, LodCandidateSkip::multiref_child);
    return;
  }
  const auto toggle_ref = toggle_ref_of(environment, placed);
  const auto material_swap = material_swap_of(environment, ref);
  const auto parent_position = *placed.position;
  const auto parent_basis = lod_placement_basis(placed.rotation.value_or(env::Vec3{}));
  const float parent_scale = placed.scale.value_or(1.0f);
  for (const auto& part : parts) {
    const auto* part_base = cached_winner(environment, cache, part.static_record);
    if (part_base == nullptr || part_base->record->header.signature != sig_stat) {
      skip(out, LodCandidateSkip::unsupported_base);
      continue;
    }
    bool malformed = false;
    const auto* distant = cached_distant_lod(environment, cache, *part_base, malformed);
    if (distant == nullptr) {
      skip(out, LodCandidateSkip::malformed);
      continue;
    }
    const auto& mesh = distant->meshes[slot];
    if (!mesh) {
      skip(out, LodCandidateSkip::no_lod_model);
      continue;
    }
    for (const auto& placement : part.placements) {
      const auto offset = apply_basis(parent_basis, std::array<float, 3>{placement.position[0] * parent_scale,
                                                                       placement.position[1] * parent_scale,
                                                                       placement.position[2] * parent_scale});
      LodObjectCandidate candidate;
      candidate.reference = ref.record->form_key;
      candidate.base = part.static_record;
      candidate.position = {parent_position[0] + offset[0], parent_position[1] + offset[1],
                            parent_position[2] + offset[2]};
      candidate.rotation = placement.rotation;
      candidate.rotation_basis = compose_basis(parent_basis, lod_placement_basis(placement.rotation));
      candidate.scale = parent_scale * placement.scale;
      candidate.mesh = std::string(*mesh);
      candidate.class_tag = LodCandidateClass::scol_part;
      candidate.cell_x = cell_x;
      candidate.cell_y = cell_y;
      candidate.toggle_ref = toggle_ref;
      candidate.persistent = persistent;
      candidate.material_swap = material_swap;
      if (!std::isfinite(candidate.position[0]) || !std::isfinite(candidate.position[1]) ||
          !std::isfinite(candidate.position[2])) {
        skip(out, LodCandidateSkip::missing_position);
        continue;
      }
      out.candidates.push_back(std::move(candidate));
      ++out.counts.included;
    }
  }
}

void collect_reference(const env::Environment& environment, const env::RecordRef& ref, std::size_t slot,
                       bool persistent, BaseCache& cache, LodCandidateCollection& out) {
  if (ref.record->header.signature != sig_refr) return;
  ++out.counts.references_seen;
  env::PlacedReferenceData placed;
  try {
    placed = env::decode_placed_reference(environment, ref);
  } catch (const env::WorldDecodeError&) {
    skip(out, LodCandidateSkip::malformed);
    return;
  }
  if (!placed.position) {
    skip(out, LodCandidateSkip::missing_position);
    return;
  }
  const auto& position = *placed.position;
  std::int32_t cell_x = 0;
  std::int32_t cell_y = 0;
  if (!cell_of(position, cell_x, cell_y)) {
    skip(out, LodCandidateSkip::missing_position);
    return;
  }
  const auto* base = placed.base ? cached_winner(environment, cache, *placed.base) : nullptr;
  if (base == nullptr) {
    skip(out, LodCandidateSkip::unsupported_base);
    return;
  }
  const auto signature = base->record->header.signature;
  if (signature == sig_scol) {
    collect_scol(environment, ref, *base, placed, cell_x, cell_y, slot, persistent, cache, out);
    return;
  }
  if (signature != sig_stat) {
    skip(out, LodCandidateSkip::not_lod_base);
    return;
  }
  bool malformed = false;
  const auto* distant = cached_distant_lod(environment, cache, *base, malformed);
  if (distant == nullptr) {
    skip(out, LodCandidateSkip::malformed);
    return;
  }
  const auto& mesh = distant->meshes[slot];
  if (!mesh) {
    skip(out, LodCandidateSkip::no_lod_model);
    return;
  }
  if (multiref_child(environment, cache, placed, slot)) {
    skip(out, LodCandidateSkip::multiref_child);
    return;
  }
  LodObjectCandidate candidate;
  candidate.reference = ref.record->form_key;
  candidate.base = *placed.base;
  candidate.position = position;
  candidate.rotation = placed.rotation.value_or(env::Vec3{});
  candidate.scale = placed.scale.value_or(1.0f);
  candidate.toggle_ref = toggle_ref_of(environment, placed);
  candidate.persistent = persistent;
  candidate.material_swap = material_swap_of(environment, ref);
  candidate.mesh = std::string(*mesh);
  candidate.cell_x = cell_x;
  candidate.cell_y = cell_y;
  candidate.class_tag = LodCandidateClass::stat;
  out.candidates.push_back(std::move(candidate));
  ++out.counts.included;
}

void collect_cell(const env::Environment& environment, const env::RecordRef& cell, std::size_t slot,
                  bool persistent, BaseCache& cache, LodCandidateCollection& out) {
  for (const auto& child : environment.cell_children(cell.record->form_key)) {
    collect_reference(environment, *child.ref, slot, persistent, cache, out);
  }
}

void merge_into(LodCandidateCollection& into, LodCandidateCollection& from) {
  into.counts.references_seen += from.counts.references_seen;
  into.counts.included += from.counts.included;
  for (std::size_t index = 0; index < into.counts.skipped.size(); ++index) {
    into.counts.skipped[index] += from.counts.skipped[index];
  }
  for (auto& candidate : from.candidates) into.candidates.push_back(std::move(candidate));
}

}  // namespace

// Invariant: workers keep their own base cache and chunks merge in cell order, so output matches a serial scan.
LodCandidateCollection collect_object_lod_candidates(const env::Environment& environment,
                                                     const FormKey& worldspace, std::int32_t level,
                                                     std::shared_ptr<const LandGrids> land) {
  const auto slot = slot_for_level(level);
  std::vector<const env::RecordRef*> cells;
  const auto* top_cell = environment.worldspace_top_cell(worldspace);
  if (top_cell != nullptr) cells.push_back(top_cell);
  for (const auto* cell : environment.worldspace_cells(worldspace)) cells.push_back(cell);
  LodCandidateCollection out;
  if (cells.empty()) return out;
  out.terrain = land ? std::move(land) : std::make_shared<const LandGrids>(collect_land_grids(environment, worldspace));
  std::optional<FormKey> multiref_keyword;
  if (const auto* keyword = environment.find_by_editor_id(k_multiref_keyword, sig_kywd)) {
    multiref_keyword = keyword->record->form_key;
  }
  const auto hardware = static_cast<std::size_t>(std::thread::hardware_concurrency());
  const auto chunks = std::min<std::size_t>(cells.size(), std::max<std::size_t>(1, hardware));
  std::vector<LodCandidateCollection> parts(chunks);
  parallel_for(chunks, [&](std::size_t index) {
    BaseCache cache;
    cache.multiref_keyword = multiref_keyword;
    const auto begin = cells.size() * index / chunks;
    const auto end = cells.size() * (index + 1) / chunks;
    for (auto cell = begin; cell < end; ++cell) {
      collect_cell(environment, *cells[cell], slot, cell == 0 && top_cell != nullptr, cache, parts[index]);
    }
  });
  for (auto& part : parts) merge_into(out, part);
  return out;
}

namespace {

float bits_float(std::uint32_t bits) { return std::bit_cast<float>(bits); }

// Invariant: mirrors the engine's polynomial sine and cosine, one rounding per float operation, no fused multiply-add.
void engine_sincos(float angle, float& sine, float& cosine) {
  // Constraint: the int cast below is undefined past int range, so a huge or non-finite angle uses the C library.
  if (!std::isfinite(angle) || std::fabs(angle) >= 1e6f) {
    sine = std::sin(angle);
    cosine = std::cos(angle);
    return;
  }
  const float half = 0.5f;
  const float scaled = angle * bits_float(0x3e22f983u);
  const int turns = static_cast<int>(angle < 0.0f ? scaled - half : scaled + half);
  float x = angle - static_cast<float>(turns) * bits_float(0x40c90fdbu);
  float sign = 1.0f;
  if (x > bits_float(0x3fc90fdbu)) {
    x = bits_float(0x40490fdbu) - x;
    sign = -1.0f;
  } else if (x < bits_float(0xbfc90fdbu)) {
    x = bits_float(0xc0490fdbu) - x;
    sign = -1.0f;
  }
  const float x2 = x * x;
  const float quad = x2 * bits_float(0x348bdd11u);
  float s = bits_float(0x3638b88eu) - x2 * bits_float(0x32cd365bu);
  s = s * x2 - bits_float(0x39500bf1u);
  s = s * x2 + bits_float(0x3c088886u);
  s = s * x2 - bits_float(0x3e2aaaabu);
  s = s * x2 + 1.0f;
  sine = s * x;
  float c = bits_float(0x37cfb4c2u) - quad;
  c = c * x2 - bits_float(0x3ab609aau);
  c = c * x2 + bits_float(0x3d2aaaa3u);
  c = c * x2 - half;
  c = c * x2 + 1.0f;
  cosine = c * sign;
}

using Matrix4 = std::array<float, 16>;

Matrix4 engine_multiply(const Matrix4& a, const Matrix4& b) {
  Matrix4 out{};
  for (std::size_t row = 0; row < 4; ++row) {
    for (std::size_t lane = 0; lane < 4; ++lane) {
      out[row * 4 + lane] = (a[row * 4 + 2] * b[8 + lane] + a[row * 4] * b[lane]) +
                            (a[row * 4 + 3] * b[12 + lane] + a[row * 4 + 1] * b[4 + lane]);
    }
  }
  return out;
}

}  // namespace

// Invariant: the engine's row-vector matrix (Z * Y) * X of the negated angles, returned transposed for column vectors.
std::array<float, 9> lod_placement_basis(const std::array<float, 3>& rotation) {
  float sx, cx, sy, cy, sz, cz;
  engine_sincos(-rotation[0], sx, cx);
  engine_sincos(-rotation[1], sy, cy);
  engine_sincos(-rotation[2], sz, cz);
  const Matrix4 x{1, 0, 0, 0, 0, cx, sx, 0, 0, -sx, cx, 0, 0, 0, 0, 1};
  const Matrix4 y{cy, 0, -sy, 0, 0, 1, 0, 0, sy, 0, cy, 0, 0, 0, 0, 1};
  const Matrix4 z{cz, sz, 0, 0, -sz, cz, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  const auto m = engine_multiply(engine_multiply(z, y), x);
  return {m[0], m[4], m[8], m[1], m[5], m[9], m[2], m[6], m[10]};
}

std::vector<LodObject> to_lod_objects(const LodCandidateCollection& collection) {
  std::vector<LodObject> objects;
  objects.reserve(collection.candidates.size());
  for (const auto& candidate : collection.candidates) {
    objects.push_back(LodObject{candidate.position[0], candidate.position[1], candidate.mesh});
  }
  return objects;
}

std::string_view lod_candidate_class_name(LodCandidateClass value) {
  switch (value) {
    case LodCandidateClass::stat:
      return "stat";
    case LodCandidateClass::scol_part:
      return "scol_part";
  }
  return "stat";
}

std::string_view lod_candidate_skip_name(LodCandidateSkip reason) {
  const auto index = static_cast<std::size_t>(reason);
  return index < k_lod_candidate_skip_count ? k_skip_names[index] : "unknown";
}

}  // namespace fo4::lod
