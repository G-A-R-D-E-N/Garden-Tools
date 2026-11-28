#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include <fo4ide/core/Result.hpp>
#include <fo4ide/preview/SoftwareRaster.hpp>

namespace fo4ide::preview {

inline constexpr double kBoundsInfinity = std::numeric_limits<double>::infinity();

using Matrix3 = std::array<std::array<double, 3>, 3>;

struct Affine {
    Matrix3 m{{{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}}};
    Vec3 t{};
};

Vec3 apply(const Affine& affine, Vec3 point);
Affine compose(const Affine& outer, const Affine& inner);
Affine poseAffine(Vec3 position, Vec3 eulerXyzRadians, double scale);
Affine rowMajorAffine(const std::array<double, 9>& rotation, Vec3 translation, double scale);

struct Bounds {
    Vec3 min{kBoundsInfinity, kBoundsInfinity, kBoundsInfinity};
    Vec3 max{-kBoundsInfinity, -kBoundsInfinity, -kBoundsInfinity};

    bool empty() const noexcept { return min.x > max.x; }
    void include(Vec3 point) noexcept;
    void include(const Bounds& other) noexcept;
    Vec3 centre() const noexcept { return empty() ? Vec3{} : (min + max) * 0.5; }
    Vec3 size() const noexcept { return empty() ? Vec3{} : max - min; }
    double radius() const noexcept;
};

struct MeshShape {
    std::vector<Vec3> verts;
    std::vector<std::array<std::uint32_t, 3>> tris;
    std::optional<Affine> local;
};

struct ModelMesh {
    std::vector<MeshShape> shapes;
    std::string error;
};

using GeometryMap = std::unordered_map<std::string, ModelMesh>;

struct ScolPart {
    std::string modelPath;
    std::vector<Affine> placements;
};

struct PlacedReference {
    std::string formKey;
    std::string baseType;
    std::string modelPath;
    bool isMarker{false};
    std::string editorId;
    std::string baseEditorId;
    bool light{false};
    bool decal{false};
    bool primitive{false};
    int primitiveType{0};
    Vec3 primitiveBounds{};
    Vec3 position{};
    Affine pose{};
    std::vector<ScolPart> scolParts;
};

struct CellData {
    std::string cellFormKey;
    std::vector<PlacedReference> references;
};

Result<CellData> parseCellReferences(const nlohmann::json& root);
Result<GeometryMap> parseGeometry(const nlohmann::json& root);
ModelMesh parseModelMesh(const nlohmann::json& entry);
const ModelMesh* findModel(const GeometryMap& geometry, std::string_view modelPath);

struct WalkBudget {
    std::size_t maxReferences{0};
    std::size_t maxTriangles{0};
};

struct WalkStats {
    std::size_t walked{0};
    std::size_t drawn{0};
    std::size_t missingGeometry{0};
    std::size_t overBudget{0};
    std::size_t markers{0};
    std::size_t triangles{0};
};

struct WorldTriangle {
    std::array<Vec3, 3> corners{};
    std::size_t reference{0};
};

using TriangleVisitor = std::function<void(const WorldTriangle&)>;

WalkStats forEachWorldTriangle(const CellData& cell,
                               const GeometryMap& geometry,
                               WalkBudget budget,
                               const TriangleVisitor& visit);

enum class LayerKind { none, room, trigger, marker, effect };

struct LayerSet {
    bool room{true};
    bool trigger{true};
    bool marker{true};
    bool effect{true};
};

struct CellScan {
    Bounds bounds;
    Bounds positions;
    std::vector<Bounds> referenceBounds;
    WalkStats stats;
};

CellScan scanCell(const CellData& cell, const GeometryMap& geometry, WalkBudget budget, const LayerSet& layers = LayerSet{});

struct CameraRequest {
    std::string preset{"iso"};
    std::optional<Vec3> eye;
    std::optional<Vec3> target;
    std::optional<double> fovDegrees;
};

Result<CameraRequest> parseCameraRequest(const nlohmann::json& value);
Camera frameCamera(const CameraRequest& request, const Bounds& bounds, int width, int height);

struct RenderOptions {
    int width{800};
    int height{600};
    CameraRequest camera;
    std::vector<std::string> highlightKeys;
    LayerSet layers;
    WalkBudget budget{4000, 3000000};
};

Result<RenderOptions> parseRenderOptions(const nlohmann::json& args, int defaultWidth, int defaultHeight);

struct RenderedImage {
    int width{0};
    int height{0};
    std::vector<std::uint8_t> rgba;
    nlohmann::json report;
};

Result<RenderedImage> renderCell(const CellData& cell, const GeometryMap& geometry, const RenderOptions& options);
Result<RenderedImage> renderModel(const ModelMesh& mesh, const RenderOptions& options);
Result<std::string> encodePngBase64(const RenderedImage& image);
nlohmann::json layoutSummary(const CellData& cell, const GeometryMap& geometry);

}  // namespace fo4ide::preview
