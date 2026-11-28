#pragma once

// Constraint: a claim counts as an invariant only when its table entry is enforced; the measured claims are statistics that never fail a tome.

#include "fo4/previs/uvd_tile_model.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace fo4::previs {

enum class UvdClaim : std::uint8_t {
  CellPortalChain,
  CellPortalSum,
  CellAabbOrdered,
  LeafClusterCount,
  PortalFace,
  PortalOutsideTarget,
  PortalOutsideCellHier,
  PortalTargetTile,
  PortalTargetCell,
  PortalHierarchy,
  PortalPlaneBoundary,
  PortalRects,
  KdConsume,
  KdReencode,
  KdLeafNoCut,
  KdCutsInside,
  KdMapEntries,
  KdMapWidth,
  KdEveryCell,
  PlaneUnit,
  BspPlaneRange,
  BspChildRange,
  BspReachable,
  TopConsume,
  TopLeafFltMax,
  TopBox,
  TopLeafFlag,
  TopLeafSlot,
  TopReencode,
  SlotPaths,
  CellStarts,
  LodEmpty,
  LodPositive,
  HeaderLodBase,
  HeaderWorld,
  HeaderReserved,
  ObjectBoxes,
  ObjectIds,
  ObjectPadding,
  ClusterChain,
  ClusterSum,
  ClusterAabb,
  GatesPresence,
  ChildConsume,
  ChildReencode,
  ChildRows,
  KdMapWidthBsp,
  PortalReciprocity,
  ObjectDistanceBox,
  Count,
};

inline constexpr std::size_t k_uvd_claim_count = static_cast<std::size_t>(UvdClaim::Count);
inline constexpr std::size_t k_uvd_enforced_claims = 46;

struct UvdClaimInfo {
  const char* name;
  bool enforced;
};

const UvdClaimInfo& uvd_claim_info(UvdClaim claim);

struct UvdClaimCount {
  std::uint64_t pass{0};
  std::uint64_t total{0};
};

struct UvdTileViolation {
  std::string claim;
  std::uint64_t failed{0};
  std::uint64_t of{0};
  std::vector<std::string> where;
};

class UvdTileClaims {
public:
  // Constraint: where is called only for the first three failures of a claim, so a retail sweep never formats a string for a passing check.
  template <class Where>
  bool check(UvdClaim claim, bool ok, Where&& where) {
    Slot& slot = slots_[static_cast<std::size_t>(claim)];
    ++slot.count.total;
    if (ok) {
      ++slot.count.pass;
      return true;
    }
    if (slot.examples.size() < 3) {
      slot.examples.push_back(where());
    }
    return false;
  }

  void add_layout_error(const std::string& reason) {
    ++layout_failures_;
    if (layout_reasons_.size() < 3) {
      layout_reasons_.push_back(reason);
    }
  }

  UvdClaimCount count(UvdClaim claim) const { return slots_[static_cast<std::size_t>(claim)].count; }
  std::vector<UvdTileViolation> violations() const;
  std::vector<std::string> lines() const;

private:
  struct Slot {
    UvdClaimCount count;
    std::vector<std::string> examples;
  };
  std::array<Slot, k_uvd_claim_count> slots_{};
  std::uint64_t layout_failures_{0};
  std::vector<std::string> layout_reasons_;
};

struct UvdTileRoundTrip {
  bool stage1{false};
  bool stage2{false};
  bool stage3{false};
};

// Constraint: stage 1 rewrites the byte-range model, stage 2 also decodes and re-encodes every tile interior, stage 3 also re-derives the header sections.
UvdTileRoundTrip uvd_tile_round_trips(std::span<const std::byte> bytes, const UvdTileModel& model);

// Constraint: a model that cannot be decoded is reported as one violation carrying the decode reason, never as a thrown error.
void validate_uvd_tiles(const UvdTileModel& model, UvdTileClaims& claims);
std::vector<UvdTileViolation> validate_uvd_tiles(const UvdTileModel& model);

}  // namespace fo4::previs
