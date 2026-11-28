#include "fo4/previs/uvd_tile_check.hpp"

#include "fo4/previs/uvd_tile_body.hpp"
#include "fo4/previs/uvd_tile_header.hpp"
#include "uvd_tile_check_internal.hpp"

#include <algorithm>
#include <string>

namespace fo4::previs {

namespace {

constexpr std::array<UvdClaimInfo, k_uvd_claim_count> k_claims{{
    {"cell: portal starts chain", true},
    {"cell: portal counts sum to the portal region", true},
    {"cell: aabb pairs ordered (lo <= hi)", true},
    {"leaf tile: cluster count == distinct cell clusters", true},
    {"portal: face <= 5", true},
    {"portal: outside <=> all-ones target", true},
    {"portal: outside => cell 0 and no hierarchy", true},
    {"portal: target slot holds a tile", true},
    {"portal: target cell in range", true},
    {"portal: hierarchy <=> target tile is not a leaf", true},
    {"portal: cross-tile same-level plane on the face boundary", true},
    {"portal: same-level non-door rects ordered", true},
    {"kd: node symbols consume exactly n nodes", true},
    {"kd: node region re-encodes from symbols", true},
    {"kd: leaf entries have no stored cut", true},
    {"kd: stored cuts inside their region", true},
    {"kd: leaf map entries address a cell, a bsp root or solid", true},
    {"kd: map width == bitlen(max(cells-1, bsp-1)) + 1 (no bsp)", true},
    {"kd: every cell appears in the leaf map (no bsp)", true},
    {"plane: finite unit normal", true},
    {"bsp: plane index in range", true},
    {"bsp: children in range", true},
    {"bsp: every node reachable from a leaf-map root", true},
    {"top kd: node symbols consume exactly n nodes", true},
    {"top kd: leaf <=> FLT_MAX cut", true},
    {"top kd: tile box == node region (bit-exact)", true},
    {"top kd: tile leaf flag <=> kd leaf node", true},
    {"top kd: every kd leaf slot holds a tile", true},
    {"top kd: nodes re-encode from symbols", true},
    {"slot paths: re-derived from the tree == stored", true},
    {"cell starts: re-derived from the tiles == stored", true},
    {"lod: -1 iff empty slot", true},
    {"lod: finite and positive for tiles", true},
    {"header: lod base 512 == banner T", true},
    {"header: world box finite and ordered", true},
    {"header: reserved word is zero", true},
    {"objects: boxes finite and ordered", true},
    {"objects: ids distinct", true},
    {"objects: distance padding words (w == 0, last == FLT_MAX or 2^24)", true},
    {"clusters: portal starts chain", true},
    {"clusters: counts sum to the portal region", true},
    {"clusters: aabb ordered (lo <= hi)", true},
    {"gates: count > 0 iff records and trailer exist", true},
    {"child: tree node symbols consume exactly n nodes", true},
    {"child: tree node region re-encodes from symbols", true},
    {"child: leaf rows == leaf tiles", true},
    {"kd: map width == bitlen(max(cells-1, bsp-1)) + 1 (with bsp)", false},
    {"portal: cross-tile link has a reverse link (face ^ 1) in the target (measured)", false},
    {"objects: distance record xyz == box", false},
}};

}  // namespace

const UvdClaimInfo& uvd_claim_info(UvdClaim claim) { return k_claims[static_cast<std::size_t>(claim)]; }

std::vector<UvdTileViolation> UvdTileClaims::violations() const {
  std::vector<UvdTileViolation> out;
  if (layout_failures_ != 0) {
    out.push_back({"layout: a region cannot be decoded", layout_failures_, layout_failures_, layout_reasons_});
  }
  for (std::size_t i = 0; i < k_uvd_claim_count; ++i) {
    const Slot& slot = slots_[i];
    if (!k_claims[i].enforced || slot.count.pass == slot.count.total) {
      continue;
    }
    out.push_back({k_claims[i].name, slot.count.total - slot.count.pass, slot.count.total, slot.examples});
  }
  return out;
}

std::vector<std::string> UvdTileClaims::lines() const {
  std::vector<std::string> out;
  for (std::size_t i = 0; i < k_uvd_claim_count; ++i) {
    const UvdClaimCount& c = slots_[i].count;
    if (c.total == 0) {
      continue;
    }
    out.push_back(std::string(k_claims[i].enforced ? "invariant " : "measured  ") + k_claims[i].name + ": " + std::to_string(c.pass) + "/" + std::to_string(c.total));
  }
  return out;
}

UvdTileRoundTrip uvd_tile_round_trips(std::span<const std::byte> bytes, const UvdTileModel& model) {
  const auto same = [&bytes](const UvdBytes& written) { return std::equal(written.begin(), written.end(), bytes.begin(), bytes.end()); };
  UvdTileRoundTrip r;
  try {
    r.stage1 = same(write_uvd_tiles(model));
    const UvdBodies bodies = decode_uvd_bodies(model);
    const UvdHeader header = decode_uvd_header(model);
    const UvdTileModel rebuilt = encode_uvd_bodies(bodies, model);
    r.stage2 = same(write_uvd_tiles(rebuilt));
    r.stage3 = same(write_uvd_tiles(encode_uvd_header(header, rebuilt)));
  } catch (const UvdTileLayoutError&) {
    return r;
  }
  return r;
}

void validate_uvd_tiles(const UvdTileModel& model, UvdTileClaims& claims) {
  try {
    const UvdBodies bodies = decode_uvd_bodies(model);
    const UvdHeader header = decode_uvd_header(model);
    detail::validate_uvd_bodies(model, bodies, claims);
    detail::validate_uvd_header(model, header, claims);
  } catch (const UvdTileLayoutError& e) {
    claims.add_layout_error(e.reason());
  }
}

std::vector<UvdTileViolation> validate_uvd_tiles(const UvdTileModel& model) {
  UvdTileClaims claims;
  validate_uvd_tiles(model, claims);
  return claims.violations();
}

}  // namespace fo4::previs
