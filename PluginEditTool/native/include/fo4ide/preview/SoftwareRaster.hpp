#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fo4ide::preview {

struct Vec3 {
    double x{0.0};
    double y{0.0};
    double z{0.0};
};

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline double dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline double length(Vec3 a) { return std::sqrt(dot(a, a)); }
Vec3 normalized(Vec3 a);

struct Rgba {
    std::uint8_t r{0};
    std::uint8_t g{0};
    std::uint8_t b{0};
    std::uint8_t a{255};
};

struct Camera {
    Vec3 eye{};
    Vec3 target{};
    Vec3 up{0.0, 0.0, 1.0};
    bool orthographic{false};
    double fovYDegrees{40.0};
    double orthoHalfHeight{1.0};
    double nearPlane{1.0};
};

struct ScreenPoint {
    double x{0.0};
    double y{0.0};
    double depth{0.0};
    bool visible{false};
};

class SoftwareRaster {
public:
    SoftwareRaster(int width, int height, Rgba background);

    void setCamera(const Camera& camera);
    const Camera& camera() const noexcept { return camera_; }
    void setLight(Vec3 towardLight, double ambient);

    int width() const noexcept { return width_; }
    int height() const noexcept { return height_; }

    ScreenPoint project(Vec3 world) const;
    void fillTriangle(const std::array<Vec3, 3>& world, Rgba colour);
    void drawLine(Vec3 from, Vec3 to, Rgba colour);
    void drawBox(Vec3 minCorner, Vec3 maxCorner, Rgba colour);

    Rgba pixel(int x, int y) const;
    std::size_t countDrawnPixels() const;
    std::vector<std::uint8_t> rgba() const;

private:
    struct CameraVertex {
        double x{0.0};
        double y{0.0};
        double z{0.0};
    };
    struct ScreenVertex {
        double x{0.0};
        double y{0.0};
        double depth{0.0};
    };

    CameraVertex toCamera(Vec3 world) const;
    ScreenVertex toScreen(const CameraVertex& point) const;
    void rasterizeTriangle(const ScreenVertex& a, const ScreenVertex& b, const ScreenVertex& c, Rgba colour);
    void plot(int x, int y, double depth, Rgba colour, bool lineBias);

    int width_{0};
    int height_{0};
    Rgba background_{};
    Camera camera_{};
    Vec3 right_{1.0, 0.0, 0.0};
    Vec3 up_{0.0, 1.0, 0.0};
    Vec3 forward_{0.0, 1.0, 0.0};
    double tanHalfY_{1.0};
    double aspect_{1.0};
    Vec3 towardLight_{0.35, -0.5, 0.8};
    double ambient_{0.35};
    std::vector<Rgba> pixels_;
    std::vector<double> depth_;
};

struct LegendEntry {
    std::string label;
    Rgba colour;
};

Rgba colourForRecordType(std::string_view recordType);
Rgba markerColour();
Rgba highlightColour();
Rgba gridColour();
Rgba backgroundColour();
std::vector<LegendEntry> recordTypeLegend();

void drawGroundGrid(SoftwareRaster& raster,
                    double centerX,
                    double centerY,
                    double halfExtent,
                    double spacing,
                    double z,
                    Rgba colour);
void drawAxes(SoftwareRaster& raster, Vec3 origin, double extent);

}  // namespace fo4ide::preview
