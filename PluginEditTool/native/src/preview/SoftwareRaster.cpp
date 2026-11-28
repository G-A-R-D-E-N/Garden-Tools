#include <fo4ide/preview/SoftwareRaster.hpp>

#include <algorithm>
#include <limits>
#include <utility>

namespace fo4ide::preview {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDegenerateArea = 1e-12;
constexpr double kEdgeEpsilon = 1e-9;
constexpr double kLineDepthBias = 1e-3;
constexpr int kMaxGridLines = 200;

std::uint8_t toByte(double value) {
    const double clamped = std::clamp(value, 0.0, 255.0);
    return static_cast<std::uint8_t>(clamped + 0.5);
}

Rgba scaled(Rgba colour, double intensity) {
    const double factor = std::clamp(intensity, 0.0, 1.0);
    return {toByte(colour.r * factor), toByte(colour.g * factor), toByte(colour.b * factor), 255};
}

double edgeFunction(double ax, double ay, double bx, double by, double px, double py) {
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

}  // namespace

Vec3 normalized(Vec3 a) {
    const double len = length(a);
    if (len < kDegenerateArea) return {0.0, 0.0, 0.0};
    return a * (1.0 / len);
}

SoftwareRaster::SoftwareRaster(int width, int height, Rgba background)
    : width_(std::max(1, width)),
      height_(std::max(1, height)),
      background_(background),
      pixels_(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_), background),
      depth_(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_),
             -std::numeric_limits<double>::infinity()) {
    setCamera(Camera{});
    setLight(towardLight_, ambient_);
}

void SoftwareRaster::setCamera(const Camera& camera) {
    camera_ = camera;
    forward_ = normalized(camera.target - camera.eye);
    if (length(forward_) == 0.0) forward_ = {0.0, 1.0, 0.0};
    Vec3 upHint = camera.up;
    right_ = cross(forward_, upHint);
    if (length(right_) < kDegenerateArea) {
        upHint = std::abs(forward_.z) < 0.9 ? Vec3{0.0, 0.0, 1.0} : Vec3{0.0, 1.0, 0.0};
        right_ = cross(forward_, upHint);
    }
    right_ = normalized(right_);
    up_ = cross(right_, forward_);
    tanHalfY_ = std::tan(camera.fovYDegrees * kPi / 360.0);
    aspect_ = static_cast<double>(width_) / static_cast<double>(height_);
}

void SoftwareRaster::setLight(Vec3 towardLight, double ambient) {
    const Vec3 direction = normalized(towardLight);
    towardLight_ = length(direction) == 0.0 ? Vec3{0.35, -0.5, 0.8} : direction;
    ambient_ = std::clamp(ambient, 0.0, 1.0);
}

SoftwareRaster::CameraVertex SoftwareRaster::toCamera(Vec3 world) const {
    const Vec3 offset = world - camera_.eye;
    return {dot(offset, right_), dot(offset, up_), dot(offset, forward_)};
}

SoftwareRaster::ScreenVertex SoftwareRaster::toScreen(const CameraVertex& point) const {
    const double halfWidth = width_ * 0.5;
    const double halfHeight = height_ * 0.5;
    if (camera_.orthographic) {
        const double halfH = std::max(camera_.orthoHalfHeight, kDegenerateArea);
        return {halfWidth + point.x / (halfH * aspect_) * halfWidth,
                halfHeight - point.y / halfH * halfHeight,
                -point.z};
    }
    const double scale = std::max(point.z, kDegenerateArea) * tanHalfY_;
    return {halfWidth + point.x / (scale * aspect_) * halfWidth,
            halfHeight - point.y / scale * halfHeight,
            1.0 / std::max(point.z, kDegenerateArea)};
}

ScreenPoint SoftwareRaster::project(Vec3 world) const {
    const CameraVertex point = toCamera(world);
    if (!camera_.orthographic && point.z < camera_.nearPlane) return {};
    const ScreenVertex screen = toScreen(point);
    return {screen.x, screen.y, screen.depth, true};
}

void SoftwareRaster::fillTriangle(const std::array<Vec3, 3>& world, Rgba colour) {
    const Vec3 normal = cross(world[1] - world[0], world[2] - world[0]);
    const double normalLength = length(normal);
    if (normalLength < kDegenerateArea) return;
    const double facing = std::abs(dot(normal, towardLight_)) / normalLength;
    const Rgba shaded = scaled(colour, ambient_ + (1.0 - ambient_) * facing);

    std::array<CameraVertex, 4> polygon{};
    std::size_t count = 0;
    std::array<CameraVertex, 3> cam{toCamera(world[0]), toCamera(world[1]), toCamera(world[2])};
    if (camera_.orthographic) {
        polygon[0] = cam[0];
        polygon[1] = cam[1];
        polygon[2] = cam[2];
        count = 3;
    } else {
        const double nearZ = camera_.nearPlane;
        for (std::size_t i = 0; i < 3; ++i) {
            const CameraVertex& current = cam[i];
            const CameraVertex& next = cam[(i + 1) % 3];
            const bool currentInside = current.z >= nearZ;
            const bool nextInside = next.z >= nearZ;
            if (currentInside) polygon[count++] = current;
            if (currentInside != nextInside) {
                const double t = (nearZ - current.z) / (next.z - current.z);
                polygon[count++] = {current.x + (next.x - current.x) * t,
                                    current.y + (next.y - current.y) * t,
                                    nearZ};
            }
        }
    }
    if (count < 3) return;

    const ScreenVertex first = toScreen(polygon[0]);
    for (std::size_t i = 1; i + 1 < count; ++i) {
        rasterizeTriangle(first, toScreen(polygon[i]), toScreen(polygon[i + 1]), shaded);
    }
}

void SoftwareRaster::rasterizeTriangle(const ScreenVertex& a,
                                       const ScreenVertex& b,
                                       const ScreenVertex& c,
                                       Rgba colour) {
    const double area = edgeFunction(a.x, a.y, b.x, b.y, c.x, c.y);
    if (std::abs(area) < kDegenerateArea) return;

    if (!std::isfinite(area)) return;
    // Constraint: the bounds are clamped to the image as doubles, because converting an off-range double to int is undefined.
    const double lastX = static_cast<double>(width_ - 1);
    const double lastY = static_cast<double>(height_ - 1);
    const int minX = static_cast<int>(std::clamp(std::floor(std::min({a.x, b.x, c.x})), 0.0, lastX));
    const int maxX = static_cast<int>(std::clamp(std::ceil(std::max({a.x, b.x, c.x})), 0.0, lastX));
    const int minY = static_cast<int>(std::clamp(std::floor(std::min({a.y, b.y, c.y})), 0.0, lastY));
    const int maxY = static_cast<int>(std::clamp(std::ceil(std::max({a.y, b.y, c.y})), 0.0, lastY));

    for (int y = minY; y <= maxY; ++y) {
        const double py = y + 0.5;
        for (int x = minX; x <= maxX; ++x) {
            const double px = x + 0.5;
            const double w0 = edgeFunction(b.x, b.y, c.x, c.y, px, py) / area;
            const double w1 = edgeFunction(c.x, c.y, a.x, a.y, px, py) / area;
            const double w2 = edgeFunction(a.x, a.y, b.x, b.y, px, py) / area;
            if (w0 < -kEdgeEpsilon || w1 < -kEdgeEpsilon || w2 < -kEdgeEpsilon) continue;
            const double depth = w0 * a.depth + w1 * b.depth + w2 * c.depth;
            plot(x, y, depth, colour, false);
        }
    }
}

void SoftwareRaster::plot(int x, int y, double depth, Rgba colour, bool lineBias) {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) return;
    const auto index = static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x);
    const double stored = depth_[index];
    const bool visible = lineBias ? depth >= stored - std::abs(stored) * kLineDepthBias : depth > stored;
    if (!visible) return;
    depth_[index] = std::max(depth, stored);
    pixels_[index] = colour;
}

void SoftwareRaster::drawLine(Vec3 from, Vec3 to, Rgba colour) {
    CameraVertex a = toCamera(from);
    CameraVertex b = toCamera(to);
    if (!camera_.orthographic) {
        const double nearZ = camera_.nearPlane;
        if (a.z < nearZ && b.z < nearZ) return;
        if (a.z < nearZ || b.z < nearZ) {
            CameraVertex& behind = a.z < nearZ ? a : b;
            const CameraVertex& front = a.z < nearZ ? b : a;
            const double t = (nearZ - behind.z) / (front.z - behind.z);
            behind = {behind.x + (front.x - behind.x) * t,
                      behind.y + (front.y - behind.y) * t,
                      nearZ};
        }
    }
    const ScreenVertex sa = toScreen(a);
    const ScreenVertex sb = toScreen(b);
    const double dx = sb.x - sa.x;
    const double dy = sb.y - sa.y;
    if (!std::isfinite(sa.x) || !std::isfinite(sa.y) || !std::isfinite(dx) || !std::isfinite(dy)) return;

    // Invariant: only the part of the segment inside the image is stepped, so the work is bounded by the image size.
    double tStart = 0.0;
    double tEnd = 1.0;
    const auto clip = [&tStart, &tEnd](double origin, double delta, double low, double high) {
        if (delta == 0.0) return origin >= low && origin <= high;
        double enter = (low - origin) / delta;
        double leave = (high - origin) / delta;
        if (enter > leave) std::swap(enter, leave);
        tStart = std::max(tStart, enter);
        tEnd = std::min(tEnd, leave);
        return tStart <= tEnd;
    };
    if (!clip(sa.x, dx, -1.0, static_cast<double>(width_)) || !clip(sa.y, dy, -1.0, static_cast<double>(height_))) {
        return;
    }

    const double span = (tEnd - tStart) * std::max(std::abs(dx), std::abs(dy));
    const int steps = std::max(1, static_cast<int>(std::ceil(span)));
    for (int i = 0; i <= steps; ++i) {
        const double t = tStart + (tEnd - tStart) * static_cast<double>(i) / steps;
        const int x = static_cast<int>(std::floor(sa.x + dx * t));
        const int y = static_cast<int>(std::floor(sa.y + dy * t));
        const double depth = sa.depth + (sb.depth - sa.depth) * t;
        plot(x, y, depth, colour, true);
    }
}

void SoftwareRaster::drawBox(Vec3 minCorner, Vec3 maxCorner, Rgba colour) {
    const std::array<Vec3, 8> corners{{
        {minCorner.x, minCorner.y, minCorner.z},
        {maxCorner.x, minCorner.y, minCorner.z},
        {maxCorner.x, maxCorner.y, minCorner.z},
        {minCorner.x, maxCorner.y, minCorner.z},
        {minCorner.x, minCorner.y, maxCorner.z},
        {maxCorner.x, minCorner.y, maxCorner.z},
        {maxCorner.x, maxCorner.y, maxCorner.z},
        {minCorner.x, maxCorner.y, maxCorner.z},
    }};
    constexpr std::array<std::array<int, 2>, 12> edges{{
        {0, 1}, {1, 2}, {2, 3}, {3, 0},
        {4, 5}, {5, 6}, {6, 7}, {7, 4},
        {0, 4}, {1, 5}, {2, 6}, {3, 7},
    }};
    for (const auto& edge : edges) {
        drawLine(corners[static_cast<std::size_t>(edge[0])], corners[static_cast<std::size_t>(edge[1])], colour);
    }
}

Rgba SoftwareRaster::pixel(int x, int y) const {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) return background_;
    return pixels_[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x)];
}

std::size_t SoftwareRaster::countDrawnPixels() const {
    return static_cast<std::size_t>(std::count_if(depth_.begin(), depth_.end(), [](double depth) {
        return std::isfinite(depth);
    }));
}

std::vector<std::uint8_t> SoftwareRaster::rgba() const {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(pixels_.size() * 4);
    for (const Rgba& pixel : pixels_) {
        bytes.push_back(pixel.r);
        bytes.push_back(pixel.g);
        bytes.push_back(pixel.b);
        bytes.push_back(pixel.a);
    }
    return bytes;
}

namespace {

struct PaletteEntry {
    std::string_view recordType;
    std::string_view label;
    Rgba colour;
};

constexpr std::array<PaletteEntry, 11> kPalette{{
    {"STAT", "Static", {128, 150, 170, 255}},
    {"MSTT", "Moveable static", {120, 160, 130, 255}},
    {"SCOL", "Static collection", {90, 170, 200, 255}},
    {"DOOR", "Door", {190, 130, 60, 255}},
    {"LIGH", "Light", {250, 215, 80, 255}},
    {"ACTI", "Activator", {120, 200, 230, 255}},
    {"CONT", "Container", {150, 100, 70, 255}},
    {"FURN", "Furniture", {170, 140, 110, 255}},
    {"NPC_", "NPC", {230, 110, 190, 255}},
    {"MARKER", "Marker", {230, 60, 60, 255}},
    {"OTHER", "Other", {190, 190, 190, 255}},
}};

}  // namespace

Rgba colourForRecordType(std::string_view recordType) {
    for (const auto& entry : kPalette) {
        if (entry.recordType == recordType) return entry.colour;
    }
    return kPalette.back().colour;
}

Rgba markerColour() { return colourForRecordType("MARKER"); }

Rgba highlightColour() { return {0, 255, 160, 255}; }

Rgba gridColour() { return {70, 74, 84, 255}; }

Rgba backgroundColour() { return {24, 26, 30, 255}; }

std::vector<LegendEntry> recordTypeLegend() {
    std::vector<LegendEntry> rows;
    rows.reserve(kPalette.size() + 1);
    for (const auto& entry : kPalette) {
        rows.push_back({std::string(entry.label), entry.colour});
    }
    rows.push_back({"Highlight", highlightColour()});
    return rows;
}

void drawGroundGrid(SoftwareRaster& raster,
                    double centerX,
                    double centerY,
                    double halfExtent,
                    double spacing,
                    double z,
                    Rgba colour) {
    if (spacing <= 0.0 || halfExtent <= 0.0) return;
    const int lines = std::min(kMaxGridLines, static_cast<int>(std::ceil(halfExtent / spacing)));
    const double step = halfExtent / std::max(1, lines);
    for (int i = -lines; i <= lines; ++i) {
        const double offset = i * step;
        raster.drawLine({centerX + offset, centerY - halfExtent, z},
                        {centerX + offset, centerY + halfExtent, z},
                        colour);
        raster.drawLine({centerX - halfExtent, centerY + offset, z},
                        {centerX + halfExtent, centerY + offset, z},
                        colour);
    }
}

void drawAxes(SoftwareRaster& raster, Vec3 origin, double extent) {
    raster.drawLine(origin, origin + Vec3{extent, 0.0, 0.0}, {230, 70, 70, 255});
    raster.drawLine(origin, origin + Vec3{0.0, extent, 0.0}, {90, 220, 90, 255});
    raster.drawLine(origin, origin + Vec3{0.0, 0.0, extent}, {80, 140, 255, 255});
}

}  // namespace fo4ide::preview
