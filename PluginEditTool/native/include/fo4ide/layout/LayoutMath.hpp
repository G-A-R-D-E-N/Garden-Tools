#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fo4ide::layout {

struct Vec3 {
    double x{0.0};
    double y{0.0};
    double z{0.0};
};

Vec3 operator+(Vec3 a, Vec3 b);
Vec3 operator-(Vec3 a, Vec3 b);
Vec3 operator*(Vec3 a, double s);
double component(Vec3 v, int axis);
void setComponent(Vec3& v, int axis, double value);

struct Bounds {
    Vec3 min{};
    Vec3 max{};

    bool valid() const;
    Vec3 size() const;
    Vec3 center() const;
};

Bounds emptyBounds();
Bounds expandBounds(Bounds bounds, Vec3 point);
Bounds mergeBounds(Bounds a, Bounds b);
Bounds translateBounds(Bounds bounds, Vec3 delta);
Bounds boundsOfPoints(std::span<const double> flatXyz);

struct Mat3 {
    double m[9]{1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
};

Mat3 multiply(const Mat3& a, const Mat3& b);
Mat3 eulerMatrix(Vec3 radians);
Mat3 quarterTurnZ(int quarterTurns);

struct Affine {
    Mat3 rotation{};
    Vec3 translation{};
    double scale{1.0};
};

Vec3 apply(const Affine& transform, Vec3 point);
Affine poseAffine(Vec3 position, Vec3 radians, double scale);

Bounds transformBounds(const Bounds& local, const Affine& transform);

struct GridEstimate {
    double grid{0.0};
    std::size_t votes{0};
    std::size_t pieces{0};
};
GridEstimate detectGrid(std::span<const Bounds> pieces);

enum class PieceCategory { unknown, floor, wall, corner, ceiling, door_frame, stairs, trim };
PieceCategory categorizeName(std::string_view name);
std::string_view categoryName(PieceCategory category);

Vec3 snapPivotToGrid(Vec3 pivot, const Bounds& local, double grid);

Vec3 buttPivotToNeighbours(Vec3 pivot, const Bounds& local, std::span<const Bounds> neighbours, double threshold);

Vec3 placeInSlot(const Bounds& local, int quarterTurns, const Bounds& slot);

int wallQuarterTurns(const Bounds& local, bool alongX);

struct OverlapPair {
    std::size_t a{0};
    std::size_t b{0};
    Vec3 depth{};
};

std::vector<OverlapPair> findOverlaps(std::span<const Bounds> items, double tolerance);

enum class AlignMode { floor, grid, distribute };

struct AlignRequest {
    AlignMode mode{AlignMode::floor};
    double grid{0.0};
    int axis{0};
    bool snapX{true};
    bool snapY{true};
};
std::optional<std::vector<Vec3>> alignDeltas(std::span<const Bounds> items, const AlignRequest& request,
                                             std::string& error);

struct RoomKit {
    std::optional<Bounds> floor;
    std::optional<Bounds> wall;
    std::optional<Bounds> corner;
    std::optional<Bounds> ceiling;
    std::optional<Bounds> door;
};

inline constexpr int k_room_max_dimension = 64;

struct RoomSize {
    int cellsX{0};
    int cellsY{0};
    int heightCells{1};
};

struct RoomPlacement {
    std::string role;
    PieceCategory category{PieceCategory::unknown};
    int quarterTurns{0};
    Vec3 position{};
    Bounds world{};
};

struct RoomPlan {
    std::vector<RoomPlacement> placements;
    Bounds footprint{};
    std::string error;
};

RoomPlan planRoom(const RoomKit& kit, RoomSize size, Vec3 origin);

}
