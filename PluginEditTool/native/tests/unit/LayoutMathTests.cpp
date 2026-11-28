#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <fo4ide/layout/LayoutMath.hpp>

using namespace fo4ide::layout;
using Catch::Matchers::WithinAbs;

namespace {

constexpr double kEps = 1e-9;
constexpr double kHalfPi = 1.57079632679489661923;

void requireVec(Vec3 actual, Vec3 expected) {
    REQUIRE_THAT(actual.x, WithinAbs(expected.x, kEps));
    REQUIRE_THAT(actual.y, WithinAbs(expected.y, kEps));
    REQUIRE_THAT(actual.z, WithinAbs(expected.z, kEps));
}

void requireBounds(const Bounds& actual, Bounds expected) {
    requireVec(actual.min, expected.min);
    requireVec(actual.max, expected.max);
}

Bounds box(double x0, double y0, double z0, double x1, double y1, double z1) {
    return Bounds{{x0, y0, z0}, {x1, y1, z1}};
}

}

TEST_CASE("LayoutMath.BoundsBasics") {
    const Bounds b = box(-10, -4, 0, 30, 4, 20);
    REQUIRE(b.valid());
    requireVec(b.size(), {40, 8, 20});
    requireVec(b.center(), {10, 0, 10});

    REQUIRE_FALSE(emptyBounds().valid());
    REQUIRE_FALSE(box(0, 0, 0, -1, 1, 1).valid());

    Bounds grown = emptyBounds();
    grown = expandBounds(grown, {1, 2, 3});
    grown = expandBounds(grown, {-1, 5, 0});
    requireBounds(grown, box(-1, 2, 0, 1, 5, 3));

    requireBounds(mergeBounds(box(0, 0, 0, 1, 1, 1), box(-2, 0.5, 0, 0.5, 3, 1)), box(-2, 0, 0, 1, 3, 1));
    requireBounds(translateBounds(box(0, 0, 0, 1, 1, 1), {5, -1, 2}), box(5, -1, 2, 6, 0, 3));
}

TEST_CASE("LayoutMath.BoundsOfPoints") {
    const std::vector<double> flat{0, 0, 0, 2, -1, 4, -3, 5, 1};
    requireBounds(boundsOfPoints(flat), box(-3, -1, 0, 2, 5, 4));

    const std::vector<double> ragged{0, 0, 0, 1};
    REQUIRE_FALSE(boundsOfPoints(ragged).valid());
    REQUIRE_FALSE(boundsOfPoints(std::vector<double>{}).valid());
}

TEST_CASE("LayoutMath.QuarterTurnsAreExact") {
    requireVec(apply(Affine{quarterTurnZ(1), {}, 1.0}, {1, 0, 0}), {0, 1, 0});
    requireVec(apply(Affine{quarterTurnZ(2), {}, 1.0}, {1, 0, 0}), {-1, 0, 0});
    requireVec(apply(Affine{quarterTurnZ(3), {}, 1.0}, {1, 0, 0}), {0, -1, 0});
    requireVec(apply(Affine{quarterTurnZ(-1), {}, 1.0}, {1, 0, 0}), {0, -1, 0});
    requireVec(apply(Affine{quarterTurnZ(4), {}, 1.0}, {0, 1, 0}), {0, 1, 0});
    REQUIRE(quarterTurnZ(1).m[1] == -1.0);
    REQUIRE(quarterTurnZ(3).m[3] == -1.0);
}

// Detects: layout bounds turning a reference rotated about two axes differently from the cell viewer and the preview renderer, which both use the Euler XYZ matrix Rx*Ry*Rz (web/src/util/cellInstances.ts refRotation).
TEST_CASE("LayoutMath.EulerMatchesZOnlyRotationAndComposesXYZ") {
    requireVec(apply(poseAffine({}, {0, 0, kHalfPi}, 1.0), {1, 0, 0}), {0, 1, 0});
    requireVec(apply(poseAffine({}, {kHalfPi, 0, 0}, 1.0), {0, 1, 0}), {0, 0, 1});
    requireVec(apply(poseAffine({}, {0, kHalfPi, 0}, 1.0), {1, 0, 0}), {0, 0, -1});
    requireVec(apply(poseAffine({}, {kHalfPi, 0, kHalfPi}, 1.0), {1, 0, 0}), {0, 0, 1});
    requireVec(apply(poseAffine({}, {kHalfPi, 0, kHalfPi}, 1.0), {0, 1, 0}), {-1, 0, 0});
}

TEST_CASE("LayoutMath.ApplyScaleThenRotateThenTranslate") {
    const Affine t{quarterTurnZ(1), {10, 20, 30}, 2.0};
    requireVec(apply(t, {1, 0, 0}), {10, 22, 30});
}

TEST_CASE("LayoutMath.TransformBoundsAxisAlignedAndRotated") {
    const Bounds local = box(0, 0, 0, 2, 1, 3);
    requireBounds(transformBounds(local, Affine{quarterTurnZ(0), {5, 5, 5}, 1.0}), box(5, 5, 5, 7, 6, 8));
    requireBounds(transformBounds(local, Affine{quarterTurnZ(1), {}, 1.0}), box(-1, 0, 0, 0, 2, 3));
    requireBounds(transformBounds(local, Affine{quarterTurnZ(0), {}, 3.0}), box(0, 0, 0, 6, 3, 9));
    REQUIRE_FALSE(transformBounds(emptyBounds(), Affine{}).valid());
}

TEST_CASE("LayoutMath.DetectGridUsesModalRoundedExtent") {
    const std::vector<Bounds> pieces{box(0, 0, 0, 256.2, 256, 300), box(0, 0, 0, 255.8, 128, 300),
                                     box(0, 0, 0, 256, 16, 300), box(0, 0, 0, 128, 128, 16)};
    const auto estimate = detectGrid(pieces);
    REQUIRE(estimate.grid == 256.0);
    REQUIRE(estimate.votes == 3);
    REQUIRE(estimate.pieces == 4);
}

TEST_CASE("LayoutMath.DetectGridTieGoesToLargerValue") {
    const std::vector<Bounds> pieces{box(0, 0, 0, 128, 1, 1), box(0, 0, 0, 256, 1, 1)};
    const auto estimate = detectGrid(pieces);
    REQUIRE(estimate.grid == 256.0);
    REQUIRE(estimate.votes == 1);
}

TEST_CASE("LayoutMath.DetectGridEmptyIsZero") {
    const auto estimate = detectGrid(std::vector<Bounds>{});
    REQUIRE(estimate.grid == 0.0);
    REQUIRE(estimate.pieces == 0);
    REQUIRE(estimate.votes == 0);
}

TEST_CASE("LayoutMath.CategorizeNamesByKeyword") {
    REQUIRE(categorizeName("Interior_Floor_256") == PieceCategory::floor);
    REQUIRE(categorizeName("VaultWall256Plain") == PieceCategory::wall);
    REQUIRE(categorizeName("WallCorner_Inner") == PieceCategory::corner);
    REQUIRE(categorizeName("CeilingLight01") == PieceCategory::ceiling);
    REQUIRE(categorizeName("Vault_DoorFrame_A") == PieceCategory::door_frame);
    REQUIRE(categorizeName("door_frame") == PieceCategory::door_frame);
    REQUIRE(categorizeName("StairsUp") == PieceCategory::stairs);
    REQUIRE(categorizeName("WallTrim") == PieceCategory::trim);
    REQUIRE(categorizeName("FloorBaseboard") == PieceCategory::trim);
    REQUIRE(categorizeName("Lamp") == PieceCategory::unknown);
    REQUIRE(categorizeName("") == PieceCategory::unknown);
    REQUIRE(categoryName(PieceCategory::door_frame) == "door_frame");
    REQUIRE(categoryName(PieceCategory::unknown) == "unknown");
}

TEST_CASE("LayoutMath.SnapPivotToGridAlignsMinimumCorner") {
    const Bounds local = box(-10, -5, 0, 10, 5, 20);
    const Vec3 snapped = snapPivotToGrid({103, 49, 7}, local, 128);
    requireVec(snapped, {138, 5, 7});
    requireVec(snapPivotToGrid({1, 2, 3}, local, 0.0), {1, 2, 3});
}

TEST_CASE("LayoutMath.ButtSlidesOntoNearestNeighbourFace") {
    const std::vector<Bounds> neighbours{box(0, 0, 0, 256, 256, 300)};
    const Bounds local = box(0, -8, 0, 256, 8, 300);
    const Vec3 butted = buttPivotToNeighbours({262, 4, 0}, local, neighbours, 16);
    requireVec(butted, {256, 4, 0});

    requireVec(buttPivotToNeighbours({290, 4, 0}, local, neighbours, 16), {290, 4, 0});
}

TEST_CASE("LayoutMath.ButtIgnoresNeighboursWithoutSharedExtent") {
    const std::vector<Bounds> neighbours{box(0, 1000, 0, 256, 1256, 300)};
    const Bounds local = box(0, -8, 0, 256, 8, 300);
    requireVec(buttPivotToNeighbours({262, 4, 0}, local, neighbours, 16), {262, 4, 0});
}

TEST_CASE("LayoutMath.PlaceInSlotCentresRotatedBoxAndSitsOnSlotFloor") {
    const Bounds local = box(-64, -8, 0, 64, 8, 300);
    const Bounds slot = box(0, -8, 16, 256, 8, 316);
    requireVec(placeInSlot(local, 0, slot), {128, 0, 16});

    requireVec(placeInSlot(local, 1, box(-8, 0, 16, 8, 256, 316)), {0, 128, 16});
}

TEST_CASE("LayoutMath.WallQuarterTurnsMatchLongAxis") {
    REQUIRE(wallQuarterTurns(box(0, 0, 0, 256, 16, 300), true) == 0);
    REQUIRE(wallQuarterTurns(box(0, 0, 0, 256, 16, 300), false) == 1);
    REQUIRE(wallQuarterTurns(box(0, 0, 0, 16, 256, 300), true) == 1);
    REQUIRE(wallQuarterTurns(box(0, 0, 0, 16, 256, 300), false) == 0);
}

TEST_CASE("LayoutMath.OverlapSweepHonoursTolerance") {
    const std::vector<Bounds> items{box(0, 0, 0, 10, 10, 10), box(5, 0, 0, 15, 10, 10),
                                    box(100, 100, 100, 110, 110, 110), box(10, 0, 0, 20, 10, 10)};
    const auto pairs = findOverlaps(items, 1.0);
    REQUIRE(pairs.size() == 2);
    REQUIRE(pairs[0].a == 0);
    REQUIRE(pairs[0].b == 1);
    requireVec(pairs[0].depth, {5, 10, 10});
    REQUIRE(pairs[1].a == 1);
    REQUIRE(pairs[1].b == 3);
    requireVec(pairs[1].depth, {5, 10, 10});

    const std::vector<Bounds> shallow{box(0, 0, 0, 10, 10, 10), box(9.5, 0, 0, 20, 10, 10)};
    REQUIRE(findOverlaps(shallow, 1.0).empty());
    REQUIRE(findOverlaps(shallow, 0.1).size() == 1);
}

TEST_CASE("LayoutMath.AlignFloorLowersToLowestMinimum") {
    const std::vector<Bounds> items{box(0, 0, 0, 1, 1, 1), box(0, 0, 10, 1, 1, 11), box(0, 0, -5, 1, 1, -4)};
    std::string error;
    const auto deltas = alignDeltas(items, AlignRequest{AlignMode::floor}, error);
    REQUIRE(deltas.has_value());
    REQUIRE(error.empty());
    requireVec((*deltas)[0], {0, 0, -5});
    requireVec((*deltas)[1], {0, 0, -15});
    requireVec((*deltas)[2], {0, 0, 0});
}

TEST_CASE("LayoutMath.AlignGridSnapsMinimumCorner") {
    const std::vector<Bounds> items{box(100, 3, 0, 110, 9, 1), box(-70, 1000, 0, -60, 1010, 1)};
    std::string error;
    AlignRequest request{AlignMode::grid, 128.0, 0, true, true};
    const auto both = alignDeltas(items, request, error);
    REQUIRE(both.has_value());
    requireVec((*both)[0], {28, -3, 0});
    requireVec((*both)[1], {-58, 24, 0});

    request.snapY = false;
    const auto xOnly = alignDeltas(items, request, error);
    REQUIRE(xOnly.has_value());
    requireVec((*xOnly)[0], {28, 0, 0});

    AlignRequest bad{AlignMode::grid, 0.0, 0, true, true};
    REQUIRE_FALSE(alignDeltas(items, bad, error).has_value());
    REQUIRE_FALSE(error.empty());
}

TEST_CASE("LayoutMath.AlignDistributeSpacesInteriorCentres") {
    const std::vector<Bounds> items{box(100, 0, 0, 100, 1, 1), box(0, 0, 0, 0, 1, 1), box(10, 0, 0, 10, 1, 1)};
    std::string error;
    AlignRequest request{AlignMode::distribute, 0.0, 0, true, true};
    const auto deltas = alignDeltas(items, request, error);
    REQUIRE(deltas.has_value());
    requireVec((*deltas)[0], {0, 0, 0});
    requireVec((*deltas)[1], {0, 0, 0});
    requireVec((*deltas)[2], {40, 0, 0});
}

TEST_CASE("LayoutMath.AlignRefusesTooFewOrBadInput") {
    std::string error;
    const std::vector<Bounds> two{box(0, 0, 0, 1, 1, 1), box(5, 0, 0, 6, 1, 1)};
    REQUIRE_FALSE(alignDeltas(two, AlignRequest{AlignMode::distribute, 0.0, 0, true, true}, error).has_value());
    REQUIRE_FALSE(error.empty());

    error.clear();
    REQUIRE_FALSE(alignDeltas(std::vector<Bounds>{}, AlignRequest{}, error).has_value());
    REQUIRE_FALSE(error.empty());
}

TEST_CASE("LayoutMath.RoomPlanCountsFloorWallsAndFootprint") {
    RoomKit kit;
    kit.floor = box(0, 0, 0, 256, 256, 16);
    kit.wall = box(0, 0, 0, 256, 16, 300);
    const auto plan = planRoom(kit, RoomSize{2, 2, 1}, {0, 0, 0});
    REQUIRE(plan.error.empty());
    REQUIRE(plan.placements.size() == 12);
    requireBounds(plan.footprint, box(0, 0, 0, 512, 512, 316));

    const RoomPlacement* farFloor = nullptr;
    for (const auto& placement : plan.placements) {
        if (placement.role == "floor[1,1]") farFloor = &placement;
    }
    REQUIRE(farFloor != nullptr);
    REQUIRE(farFloor->category == PieceCategory::floor);
    requireVec(farFloor->position, {256, 256, 0});
    requireBounds(farFloor->world, box(256, 256, 0, 512, 512, 16));

    const RoomPlacement* south = nullptr;
    for (const auto& placement : plan.placements) {
        if (placement.role == "wall_south[0]") south = &placement;
    }
    REQUIRE(south != nullptr);
    REQUIRE(south->quarterTurns == 0);
    requireBounds(south->world, box(0, -8, 16, 256, 8, 316));
}

TEST_CASE("LayoutMath.RoomPlanDoorReplacesCentreTileOfSouthWallOnly") {
    RoomKit kit;
    kit.floor = box(0, 0, 0, 256, 256, 16);
    kit.wall = box(0, 0, 0, 256, 16, 300);
    kit.door = box(0, 0, 0, 128, 16, 300);
    const auto plan = planRoom(kit, RoomSize{2, 1, 1}, {0, 0, 0});
    REQUIRE(plan.error.empty());
    REQUIRE(plan.placements.size() == 8);
    int doors = 0;
    for (const auto& placement : plan.placements) {
        if (placement.category == PieceCategory::door_frame) {
            ++doors;
            REQUIRE(placement.role == "door_south[1]");
        }
        if (placement.role == "wall_north[1]") REQUIRE(placement.category == PieceCategory::wall);
    }
    REQUIRE(doors == 1);
}

TEST_CASE("LayoutMath.RoomPlanCornersAndCeilingAndTallRooms") {
    RoomKit kit;
    kit.floor = box(0, 0, 0, 256, 256, 16);
    kit.wall = box(0, 0, 0, 256, 16, 300);
    kit.corner = box(0, 0, 0, 16, 16, 300);
    kit.ceiling = box(0, 0, 0, 256, 256, 16);
    const auto plan = planRoom(kit, RoomSize{1, 1, 2}, {0, 0, 0});
    REQUIRE(plan.error.empty());
    REQUIRE(plan.placements.size() == 1 + 2 * 8 + 1);
    requireBounds(plan.footprint, box(0, 0, 0, 256, 256, 616));

    const RoomPlacement* ceiling = nullptr;
    const RoomPlacement* corner = nullptr;
    for (const auto& placement : plan.placements) {
        if (placement.role == "ceiling[0,0]") ceiling = &placement;
        if (placement.role == "corner_se[1]") corner = &placement;
    }
    REQUIRE(ceiling != nullptr);
    REQUIRE(corner != nullptr);
    REQUIRE_THAT(ceiling->world.min.z, WithinAbs(616.0, kEps));
    REQUIRE(corner->category == PieceCategory::corner);
    REQUIRE(corner->quarterTurns == 3);
    REQUIRE_THAT(corner->world.center().x, WithinAbs(256.0, kEps));
    REQUIRE_THAT(corner->world.center().y, WithinAbs(0.0, kEps));
    REQUIRE_THAT(corner->world.min.z, WithinAbs(316.0, kEps));
}

TEST_CASE("LayoutMath.RoomPlanRefusesBadInput") {
    RoomKit kit;
    kit.wall = box(0, 0, 0, 256, 16, 300);
    REQUIRE_FALSE(planRoom(kit, RoomSize{1, 1, 1}, {}).error.empty());

    kit.floor = box(0, 0, 0, 256, 256, 16);
    REQUIRE_FALSE(planRoom(kit, RoomSize{0, 1, 1}, {}).error.empty());
    REQUIRE_FALSE(planRoom(kit, RoomSize{1, 1, 0}, {}).error.empty());

    kit.wall = box(0, 0, 0, 0, 0, 0);
    REQUIRE_FALSE(planRoom(kit, RoomSize{1, 1, 1}, {}).error.empty());

    kit.wall = std::nullopt;
    REQUIRE_FALSE(planRoom(kit, RoomSize{1, 1, 1}, {}).error.empty());
}
