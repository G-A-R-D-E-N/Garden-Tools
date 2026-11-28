#include <fo4ide/layout/LayoutMath.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <utility>

namespace fo4ide::layout {

namespace {

Vec3 componentwiseMin(Vec3 a, Vec3 b) {
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
}

Vec3 componentwiseMax(Vec3 a, Vec3 b) {
    return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
}

Mat3 rotationX(double a) {
    const double c = std::cos(a);
    const double s = std::sin(a);
    Mat3 r;
    r.m[0] = 1.0; r.m[1] = 0.0; r.m[2] = 0.0;
    r.m[3] = 0.0; r.m[4] = c;   r.m[5] = -s;
    r.m[6] = 0.0; r.m[7] = s;   r.m[8] = c;
    return r;
}

Mat3 rotationY(double a) {
    const double c = std::cos(a);
    const double s = std::sin(a);
    Mat3 r;
    r.m[0] = c;   r.m[1] = 0.0; r.m[2] = s;
    r.m[3] = 0.0; r.m[4] = 1.0; r.m[5] = 0.0;
    r.m[6] = -s;  r.m[7] = 0.0; r.m[8] = c;
    return r;
}

Mat3 rotationZ(double a) {
    const double c = std::cos(a);
    const double s = std::sin(a);
    Mat3 r;
    r.m[0] = c;   r.m[1] = -s;  r.m[2] = 0.0;
    r.m[3] = s;   r.m[4] = c;   r.m[5] = 0.0;
    r.m[6] = 0.0; r.m[7] = 0.0; r.m[8] = 1.0;
    return r;
}

std::string lowerAlnum(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const unsigned char c : text) {
        if (std::isalnum(c)) out.push_back(static_cast<char>(std::tolower(c)));
    }
    return out;
}

bool contains(const std::string& haystack, std::string_view needle) {
    return haystack.find(needle) != std::string::npos;
}

Bounds placedAt(const Bounds& local, int quarterTurns, Vec3 position) {
    return transformBounds(local, Affine{quarterTurnZ(quarterTurns), position, 1.0});
}

Bounds squareAround(Vec3 centre, Vec3 extent, double z) {
    return Bounds{{centre.x - extent.x * 0.5, centre.y - extent.y * 0.5, z},
                  {centre.x + extent.x * 0.5, centre.y + extent.y * 0.5, z + extent.z}};
}

}

Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 operator*(Vec3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }

double component(Vec3 v, int axis) {
    switch (axis) {
        case 0: return v.x;
        case 1: return v.y;
        default: return v.z;
    }
}

void setComponent(Vec3& v, int axis, double value) {
    switch (axis) {
        case 0: v.x = value; break;
        case 1: v.y = value; break;
        default: v.z = value; break;
    }
}

bool Bounds::valid() const {
    return min.x <= max.x && min.y <= max.y && min.z <= max.z;
}

Vec3 Bounds::size() const { return max - min; }

Vec3 Bounds::center() const { return (min + max) * 0.5; }

Bounds emptyBounds() {
    constexpr double inf = std::numeric_limits<double>::infinity();
    return Bounds{{inf, inf, inf}, {-inf, -inf, -inf}};
}

Bounds expandBounds(Bounds bounds, Vec3 point) {
    bounds.min = componentwiseMin(bounds.min, point);
    bounds.max = componentwiseMax(bounds.max, point);
    return bounds;
}

Bounds mergeBounds(Bounds a, Bounds b) {
    return Bounds{componentwiseMin(a.min, b.min), componentwiseMax(a.max, b.max)};
}

Bounds translateBounds(Bounds bounds, Vec3 delta) {
    return Bounds{bounds.min + delta, bounds.max + delta};
}

Bounds boundsOfPoints(std::span<const double> flatXyz) {
    Bounds out = emptyBounds();
    if (flatXyz.size() % 3 != 0) return emptyBounds();
    for (std::size_t i = 0; i + 2 < flatXyz.size(); i += 3) {
        out = expandBounds(out, Vec3{flatXyz[i], flatXyz[i + 1], flatXyz[i + 2]});
    }
    return out;
}

Mat3 multiply(const Mat3& a, const Mat3& b) {
    Mat3 out;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            double sum = 0.0;
            for (int k = 0; k < 3; ++k) sum += a.m[r * 3 + k] * b.m[k * 3 + c];
            out.m[r * 3 + c] = sum;
        }
    }
    return out;
}

// Invariant: the Euler XYZ matrix Rx*Ry*Rz, the order the cell viewer (refRotation) and the preview renderer use.
Mat3 eulerMatrix(Vec3 radians) {
    return multiply(rotationX(radians.x), multiply(rotationY(radians.y), rotationZ(radians.z)));
}

Mat3 quarterTurnZ(int quarterTurns) {
    const int turns = ((quarterTurns % 4) + 4) % 4;
    Mat3 r;
    switch (turns) {
        case 1: r.m[0] = 0.0; r.m[1] = -1.0; r.m[3] = 1.0; r.m[4] = 0.0; break;
        case 2: r.m[0] = -1.0; r.m[4] = -1.0; break;
        case 3: r.m[0] = 0.0; r.m[1] = 1.0; r.m[3] = -1.0; r.m[4] = 0.0; break;
        default: break;
    }
    return r;
}

Vec3 apply(const Affine& transform, Vec3 point) {
    const auto& m = transform.rotation.m;
    const Vec3 rotated{m[0] * point.x + m[1] * point.y + m[2] * point.z,
                       m[3] * point.x + m[4] * point.y + m[5] * point.z,
                       m[6] * point.x + m[7] * point.y + m[8] * point.z};
    return rotated * transform.scale + transform.translation;
}

Affine poseAffine(Vec3 position, Vec3 radians, double scale) {
    return Affine{eulerMatrix(radians), position, scale};
}

Bounds transformBounds(const Bounds& local, const Affine& transform) {
    if (!local.valid()) return emptyBounds();
    Bounds out = emptyBounds();
    for (int corner = 0; corner < 8; ++corner) {
        const Vec3 point{(corner & 1) ? local.max.x : local.min.x,
                         (corner & 2) ? local.max.y : local.min.y,
                         (corner & 4) ? local.max.z : local.min.z};
        out = expandBounds(out, apply(transform, point));
    }
    return out;
}

GridEstimate detectGrid(std::span<const Bounds> pieces) {
    std::map<long long, std::size_t> counts;
    GridEstimate estimate;
    for (const auto& piece : pieces) {
        if (!piece.valid()) continue;
        const Vec3 size = piece.size();
        const long long rounded = std::llround(std::max(size.x, size.y));
        if (rounded < 1) continue;
        ++counts[rounded];
        ++estimate.pieces;
    }
    for (const auto& [value, votes] : counts) {
        if (votes >= estimate.votes) {
            estimate.votes = votes;
            estimate.grid = static_cast<double>(value);
        }
    }
    return estimate;
}

PieceCategory categorizeName(std::string_view name) {
    const std::string key = lowerAlnum(name);
    if (contains(key, "corner")) return PieceCategory::corner;
    if (contains(key, "door")) return PieceCategory::door_frame;
    if (contains(key, "stair")) return PieceCategory::stairs;
    if (contains(key, "trim") || contains(key, "molding") || contains(key, "baseboard")) return PieceCategory::trim;
    if (contains(key, "ceiling")) return PieceCategory::ceiling;
    if (contains(key, "floor")) return PieceCategory::floor;
    if (contains(key, "wall")) return PieceCategory::wall;
    return PieceCategory::unknown;
}

std::string_view categoryName(PieceCategory category) {
    switch (category) {
        case PieceCategory::floor: return "floor";
        case PieceCategory::wall: return "wall";
        case PieceCategory::corner: return "corner";
        case PieceCategory::ceiling: return "ceiling";
        case PieceCategory::door_frame: return "door_frame";
        case PieceCategory::stairs: return "stairs";
        case PieceCategory::trim: return "trim";
        case PieceCategory::unknown: break;
    }
    return "unknown";
}

Vec3 snapPivotToGrid(Vec3 pivot, const Bounds& local, double grid) {
    if (grid <= 0.0 || !local.valid()) return pivot;
    const double minX = pivot.x + local.min.x;
    const double minY = pivot.y + local.min.y;
    pivot.x = std::round(minX / grid) * grid - local.min.x;
    pivot.y = std::round(minY / grid) * grid - local.min.y;
    return pivot;
}

Vec3 buttPivotToNeighbours(Vec3 pivot, const Bounds& local, std::span<const Bounds> neighbours, double threshold) {
    if (!local.valid()) return pivot;
    for (int axis = 0; axis < 3; ++axis) {
        std::optional<double> best;
        for (const auto& neighbour : neighbours) {
            if (!neighbour.valid()) continue;
            bool shares = true;
            for (int other = 0; other < 3; ++other) {
                if (other == axis) continue;
                const double lo = component(pivot, other) + component(local.min, other);
                const double hi = component(pivot, other) + component(local.max, other);
                const double overlap = std::min(hi, component(neighbour.max, other)) -
                                       std::max(lo, component(neighbour.min, other));
                if (overlap <= 0.0) shares = false;
            }
            if (!shares) continue;
            const double candidateMin = component(pivot, axis) + component(local.min, axis);
            const double candidateMax = component(pivot, axis) + component(local.max, axis);
            const double deltas[2] = {component(neighbour.max, axis) - candidateMin,
                                      component(neighbour.min, axis) - candidateMax};
            for (const double delta : deltas) {
                if (std::fabs(delta) > threshold) continue;
                if (!best || std::fabs(delta) < std::fabs(*best)) best = delta;
            }
        }
        if (best) setComponent(pivot, axis, component(pivot, axis) + *best);
    }
    return pivot;
}

Vec3 placeInSlot(const Bounds& local, int quarterTurns, const Bounds& slot) {
    const Bounds rotated = transformBounds(local, Affine{quarterTurnZ(quarterTurns), Vec3{}, 1.0});
    const Vec3 slotCentre = slot.center();
    const Vec3 rotatedCentre = rotated.center();
    return Vec3{slotCentre.x - rotatedCentre.x, slotCentre.y - rotatedCentre.y, slot.min.z - rotated.min.z};
}

int wallQuarterTurns(const Bounds& local, bool alongX) {
    const Vec3 size = local.size();
    if (alongX) return size.x >= size.y ? 0 : 1;
    return size.x >= size.y ? 1 : 0;
}

std::vector<OverlapPair> findOverlaps(std::span<const Bounds> items, double tolerance) {
    std::vector<std::size_t> order(items.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        return items[a].min.x < items[b].min.x;
    });

    std::vector<OverlapPair> pairs;
    for (std::size_t i = 0; i < order.size(); ++i) {
        const Bounds& first = items[order[i]];
        if (!first.valid()) continue;
        for (std::size_t k = i + 1; k < order.size(); ++k) {
            const Bounds& second = items[order[k]];
            if (second.min.x >= first.max.x - tolerance) break;
            if (!second.valid()) continue;
            Vec3 depth{std::min(first.max.x, second.max.x) - std::max(first.min.x, second.min.x),
                       std::min(first.max.y, second.max.y) - std::max(first.min.y, second.min.y),
                       std::min(first.max.z, second.max.z) - std::max(first.min.z, second.min.z)};
            if (depth.x <= tolerance || depth.y <= tolerance || depth.z <= tolerance) continue;
            OverlapPair pair;
            pair.a = std::min(order[i], order[k]);
            pair.b = std::max(order[i], order[k]);
            pair.depth = depth;
            pairs.push_back(pair);
        }
    }
    std::sort(pairs.begin(), pairs.end(), [](const OverlapPair& x, const OverlapPair& y) {
        return x.a != y.a ? x.a < y.a : x.b < y.b;
    });
    return pairs;
}

std::optional<std::vector<Vec3>> alignDeltas(std::span<const Bounds> items, const AlignRequest& request,
                                             std::string& error) {
    if (items.empty()) {
        error = "Align needs at least one placed reference";
        return std::nullopt;
    }
    for (const auto& item : items) {
        if (!item.valid()) {
            error = "Align needs placed references with valid bounds";
            return std::nullopt;
        }
    }
    std::vector<Vec3> deltas(items.size());

    switch (request.mode) {
        case AlignMode::floor: {
            double lowest = items.front().min.z;
            for (const auto& item : items) lowest = std::min(lowest, item.min.z);
            for (std::size_t i = 0; i < items.size(); ++i) deltas[i].z = lowest - items[i].min.z;
            return deltas;
        }
        case AlignMode::grid: {
            if (request.grid <= 0.0) {
                error = "Grid align needs a positive grid size";
                return std::nullopt;
            }
            for (std::size_t i = 0; i < items.size(); ++i) {
                if (request.snapX) {
                    deltas[i].x = std::round(items[i].min.x / request.grid) * request.grid - items[i].min.x;
                }
                if (request.snapY) {
                    deltas[i].y = std::round(items[i].min.y / request.grid) * request.grid - items[i].min.y;
                }
            }
            return deltas;
        }
        case AlignMode::distribute: {
            if (items.size() < 3) {
                error = "Distribute needs at least three placed references";
                return std::nullopt;
            }
            if (request.axis < 0 || request.axis > 2) {
                error = "Distribute axis must be x, y or z";
                return std::nullopt;
            }
            std::vector<std::size_t> order(items.size());
            std::iota(order.begin(), order.end(), std::size_t{0});
            std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
                return component(items[a].center(), request.axis) < component(items[b].center(), request.axis);
            });
            const double first = component(items[order.front()].center(), request.axis);
            const double last = component(items[order.back()].center(), request.axis);
            const auto count = static_cast<double>(order.size() - 1);
            for (std::size_t k = 1; k + 1 < order.size(); ++k) {
                const std::size_t index = order[k];
                const double target = first + static_cast<double>(k) * (last - first) / count;
                setComponent(deltas[index], request.axis, target - component(items[index].center(), request.axis));
            }
            return deltas;
        }
    }
    error = "Unknown align mode";
    return std::nullopt;
}

RoomPlan planRoom(const RoomKit& kit, RoomSize size, Vec3 origin) {
    RoomPlan plan;
    if (!kit.floor || !kit.floor->valid()) {
        plan.error = "Room plan needs a floor piece with valid bounds";
        return plan;
    }
    if (!kit.wall || !kit.wall->valid()) {
        plan.error = "Room plan needs a wall piece with valid bounds";
        return plan;
    }
    if (size.cellsX < 1 || size.cellsY < 1 || size.heightCells < 1) {
        plan.error = "Room size needs at least one cell in X, Y and height";
        return plan;
    }
    if (size.cellsX > k_room_max_dimension || size.cellsY > k_room_max_dimension ||
        size.heightCells > k_room_max_dimension) {
        plan.error = "Room size is capped at 64 cells per dimension";
        return plan;
    }

    const Bounds& floor = *kit.floor;
    const Bounds& wall = *kit.wall;
    const Vec3 floorSize = floor.size();
    const Vec3 wallSize = wall.size();
    const double tileX = floorSize.x;
    const double tileY = floorSize.y;
    const double wallThickness = std::min(wallSize.x, wallSize.y);
    if (tileX <= 0.0 || tileY <= 0.0 || wallThickness <= 0.0 || wallSize.z <= 0.0) {
        plan.error = "Room kit pieces need positive sizes";
        return plan;
    }

    const double floorHeight = floorSize.z;
    const double wallHeight = wallSize.z;
    const double baseZ = origin.z + floorHeight;
    const double roomEndX = origin.x + size.cellsX * tileX;
    const double roomEndY = origin.y + size.cellsY * tileY;
    const int doorIndex = size.cellsX / 2;

    const auto add = [&](std::string role, PieceCategory category, const Bounds& local, int quarterTurns,
                         const Bounds& slot) {
        RoomPlacement placement;
        placement.role = std::move(role);
        placement.category = category;
        placement.quarterTurns = quarterTurns;
        placement.position = placeInSlot(local, quarterTurns, slot);
        placement.world = placedAt(local, quarterTurns, placement.position);
        plan.placements.push_back(std::move(placement));
    };

    for (int i = 0; i < size.cellsX; ++i) {
        for (int j = 0; j < size.cellsY; ++j) {
            const Bounds slot{{origin.x + i * tileX, origin.y + j * tileY, origin.z},
                              {origin.x + (i + 1) * tileX, origin.y + (j + 1) * tileY, origin.z + floorHeight}};
            add("floor[" + std::to_string(i) + "," + std::to_string(j) + "]", PieceCategory::floor, floor, 0, slot);
        }
    }

    for (int row = 0; row < size.heightCells; ++row) {
        const double low = baseZ + row * wallHeight;
        const double high = low + wallHeight;
        const bool hasDoor = kit.door && kit.door->valid() && row == 0;
        const auto wallSlot = [&](double x0, double x1, double y0, double y1) {
            return Bounds{{x0, y0, low}, {x1, y1, high}};
        };
        const double halfThickness = wallThickness * 0.5;
        for (int i = 0; i < size.cellsX; ++i) {
            const double x0 = origin.x + i * tileX;
            const double x1 = x0 + tileX;
            const std::string index = std::to_string(i);
            if (hasDoor && i == doorIndex) {
                add("door_south[" + index + "]", PieceCategory::door_frame, *kit.door,
                    wallQuarterTurns(*kit.door, true),
                    wallSlot(x0, x1, origin.y - halfThickness, origin.y + halfThickness));
            } else {
                add("wall_south[" + index + "]", PieceCategory::wall, wall, wallQuarterTurns(wall, true),
                    wallSlot(x0, x1, origin.y - halfThickness, origin.y + halfThickness));
            }
            add("wall_north[" + index + "]", PieceCategory::wall, wall, wallQuarterTurns(wall, true),
                wallSlot(x0, x1, roomEndY - halfThickness, roomEndY + halfThickness));
        }
        for (int j = 0; j < size.cellsY; ++j) {
            const double y0 = origin.y + j * tileY;
            const double y1 = y0 + tileY;
            add("wall_west[" + std::to_string(j) + "]", PieceCategory::wall, wall, wallQuarterTurns(wall, false),
                wallSlot(origin.x - halfThickness, origin.x + halfThickness, y0, y1));
            add("wall_east[" + std::to_string(j) + "]", PieceCategory::wall, wall, wallQuarterTurns(wall, false),
                wallSlot(roomEndX - halfThickness, roomEndX + halfThickness, y0, y1));
        }

        if (kit.corner && kit.corner->valid()) {
            const Vec3 cornerSize = kit.corner->size();
            const double side = std::max(cornerSize.x, cornerSize.y);
            const Vec3 extent{side, side, cornerSize.z};
            struct Corner {
                const char* name;
                Vec3 centre;
                int quarterTurns;
            };
            const Corner corners[] = {
                {"sw", {origin.x, origin.y, low}, 0},
                {"se", {roomEndX, origin.y, low}, 3},
                {"ne", {roomEndX, roomEndY, low}, 2},
                {"nw", {origin.x, roomEndY, low}, 1},
            };
            for (const auto& corner : corners) {
                const Bounds slot = squareAround(corner.centre, extent, low);
                add("corner_" + std::string(corner.name) + "[" + std::to_string(row) + "]", PieceCategory::corner,
                    *kit.corner, corner.quarterTurns, slot);
            }
        }
    }

    if (kit.ceiling && kit.ceiling->valid()) {
        const double ceilingZ = baseZ + size.heightCells * wallHeight;
        const Vec3 ceilingSize = kit.ceiling->size();
        for (int i = 0; i < size.cellsX; ++i) {
            for (int j = 0; j < size.cellsY; ++j) {
                const Bounds slot{{origin.x + i * tileX, origin.y + j * tileY, ceilingZ},
                                  {origin.x + (i + 1) * tileX, origin.y + (j + 1) * tileY, ceilingZ + ceilingSize.z}};
                add("ceiling[" + std::to_string(i) + "," + std::to_string(j) + "]", PieceCategory::ceiling,
                    *kit.ceiling, 0, slot);
            }
        }
    }

    plan.footprint = Bounds{origin, {roomEndX, roomEndY, baseZ + size.heightCells * wallHeight}};
    return plan;
}

}
