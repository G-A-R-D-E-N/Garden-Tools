#include "NifUvOps.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include <fo4/c_api.h>
#include <fo4/c_api_nif_mesh.h>

namespace fo4ide::app {

namespace {

using json = nlohmann::json;
using Floats = std::vector<float>;
using Indices = std::vector<std::uint32_t>;

struct UvOpError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct FloatsFree {
    void operator()(float* values) const { radicle_free_floats(values); }
};
struct RefsFree {
    void operator()(std::uint32_t* values) const { radicle_free_node_refs(values); }
};
using FloatsOut = std::unique_ptr<float, FloatsFree>;
using RefsOut = std::unique_ptr<std::uint32_t, RefsFree>;

[[noreturn]] void refuse(const std::string& message) { throw UvOpError(message); }

void check(radicle_status status, const char* what) {
    if (status == RADICLE_STATUS_OK) return;
    const char* detail = radicle_last_error();
    refuse(std::string(what) + " failed: " + (detail != nullptr && *detail != '\0' ? detail : "unknown error"));
}

template <typename T>
json arrayOf(const T* values, std::size_t count) {
    json out = json::array();
    for (std::size_t i = 0; i < count; ++i) {
        if constexpr (std::is_floating_point_v<T>) {
            if (!std::isfinite(values[i])) refuse("result not finite");
        }
        out.push_back(values[i]);
    }
    return out;
}

Floats floats(const json& body, const char* key) {
    Floats out;
    const auto it = body.find(key);
    if (it == body.end() || it->is_null()) return out;
    if (!it->is_array()) refuse(std::string(key) + " must be an array of numbers");
    out.reserve(it->size());
    for (const auto& item : *it) {
        if (!item.is_number()) refuse(std::string(key) + " must hold only numbers");
        const auto value = static_cast<float>(item.get<double>());
        if (!std::isfinite(value)) refuse(std::string(key) + " holds a value that is not a finite float");
        out.push_back(value);
    }
    return out;
}

std::uint32_t wholeNumber(const json& item, const char* key) {
    if (!item.is_number_integer()) refuse(std::string(key) + " must hold only whole numbers");
    if (item.is_number_unsigned()) {
        const auto value = item.get<std::uint64_t>();
        if (value > std::numeric_limits<std::uint32_t>::max()) refuse(std::string(key) + " holds a value that is too large");
        return static_cast<std::uint32_t>(value);
    }
    const auto value = item.get<std::int64_t>();
    if (value < 0) refuse(std::string(key) + " holds a negative value");
    return static_cast<std::uint32_t>(value);
}

Indices indices(const json& body, const char* key) {
    Indices out;
    const auto it = body.find(key);
    if (it == body.end() || it->is_null()) return out;
    if (!it->is_array()) refuse(std::string(key) + " must be an array of whole numbers");
    out.reserve(it->size());
    for (const auto& item : *it) out.push_back(wholeNumber(item, key));
    return out;
}

std::uint32_t whole(const json& body, const char* key) {
    const auto it = body.find(key);
    if (it == body.end()) refuse(std::string(key) + " is required");
    return wholeNumber(*it, key);
}

double number(const json& body, const char* key) {
    const auto it = body.find(key);
    if (it == body.end() || !it->is_number()) refuse(std::string(key) + " is required and must be a number");
    const double value = it->get<double>();
    if (!std::isfinite(value)) refuse(std::string(key) + " must be finite");
    return value;
}

bool flag(const json& body, const char* key) {
    const auto it = body.find(key);
    return it != body.end() && it->is_boolean() && it->get<bool>();
}

struct Uvs {
    Floats values;
    std::size_t count = 0;
};

Uvs readUvs(const json& body) {
    Uvs uvs{floats(body, "uvs"), 0};
    if (uvs.values.size() % 2 != 0) refuse("uvs must hold 2 floats per vertex");
    uvs.count = uvs.values.size() / 2;
    return uvs;
}

Floats readPositions(const json& body, std::size_t count) {
    auto positions = floats(body, "positions");
    if (positions.size() != 3 * count) refuse("positions must hold 3 floats per UV vertex");
    return positions;
}

Indices islandIds(std::size_t count, const Indices& tris) {
    std::uint32_t* raw = nullptr;
    std::size_t n = 0;
    const auto status = radicle_uv_islands(count, tris.data(), tris.size(), &raw, &n);
    RefsOut owned(raw);
    check(status, "UV island numbering");
    return Indices(raw, raw + n);
}

template <typename Call>
json uvsResult(const char* what, Call call) {
    float* raw = nullptr;
    std::size_t n = 0;
    const auto status = call(&raw, &n);
    FloatsOut owned(raw);
    check(status, what);
    return {{"uvs", arrayOf(raw, n)}};
}

template <typename Call>
json listResult(const char* what, const char* key, Call call) {
    std::uint32_t* raw = nullptr;
    std::size_t n = 0;
    const auto status = call(&raw, &n);
    RefsOut owned(raw);
    check(status, what);
    return {{key, arrayOf(raw, n)}};
}

json packOp(const json& body) {
    const auto uvs = readUvs(body);
    const auto tris = indices(body, "tris");
    const auto selection = indices(body, "selection");
    const auto ids = islandIds(uvs.count, tris);
    const double margin = number(body, "margin");
    float* raw = nullptr;
    std::size_t n = 0;
    std::uint8_t fits = 0;
    const auto status = radicle_uv_pack(uvs.values.data(), uvs.count, ids.data(), selection.data(), selection.size(),
                                        margin, &raw, &n, &fits);
    FloatsOut owned(raw);
    check(status, "UV pack");
    return {{"fits", fits != 0}, {"uvs", arrayOf(raw, n)}};
}

json smartProjectOp(const json& body) {
    const auto uvs = readUvs(body);
    const auto positions = readPositions(body, uvs.count);
    const auto tris = indices(body, "tris");
    const auto selection = indices(body, "selection");
    const auto ids = islandIds(uvs.count, tris);
    const double margin = number(body, "margin");
    float* raw = nullptr;
    std::size_t n = 0;
    std::uint32_t projected = 0, skipped = 0;
    std::uint8_t fits = 0;
    const auto status = radicle_uv_smart_project(uvs.values.data(), positions.data(), uvs.count, tris.data(), tris.size(),
                                                 ids.data(), selection.data(), selection.size(), margin, &raw, &n,
                                                 &projected, &skipped, &fits);
    FloatsOut owned(raw);
    check(status, "UV smart project");
    return {{"fits", fits != 0}, {"projected", projected}, {"skipped", skipped}, {"uvs", arrayOf(raw, n)}};
}

json lscmUnwrapOp(const json& body) {
    const auto uvs = readUvs(body);
    const auto positions = readPositions(body, uvs.count);
    const auto tris = indices(body, "tris");
    const auto selection = indices(body, "selection");
    const auto pinVertices = indices(body, "pin_vertices");
    const auto pinUvs = floats(body, "pin_uvs");
    if (pinUvs.size() != 2 * pinVertices.size()) refuse("pin_uvs must hold 2 floats per pin vertex");
    const auto ids = islandIds(uvs.count, tris);
    float* raw = nullptr;
    std::size_t n = 0;
    std::uint32_t unwrapped = 0, skipped = 0;
    const auto status = radicle_uv_lscm_unwrap(uvs.values.data(), positions.data(), uvs.count, tris.data(), tris.size(),
                                               ids.data(), selection.data(), selection.size(), pinVertices.data(),
                                               pinUvs.data(), pinVertices.size(), &raw, &n, &unwrapped, &skipped);
    FloatsOut owned(raw);
    check(status, "UV unwrap");
    return {{"unwrapped", unwrapped}, {"skipped", skipped}, {"uvs", arrayOf(raw, n)}};
}

json run(std::string_view op, const json& body) {
    if (!body.is_object()) refuse("the UV operation arguments must be an object");
    if (op == "islands") {
        const auto tris = indices(body, "tris");
        const auto ids = islandIds(whole(body, "vertex_count"), tris);
        return {{"ids", arrayOf(ids.data(), ids.size())}};
    }
    if (op == "pack") return packOp(body);
    if (op == "smart_project") return smartProjectOp(body);
    if (op == "lscm_unwrap") return lscmUnwrapOp(body);
    if (op == "tri_stretch") {
        const auto uvs = readUvs(body);
        const auto positions = readPositions(body, uvs.count);
        const auto tris = indices(body, "tris");
        float* raw = nullptr;
        std::size_t n = 0;
        const auto status = radicle_uv_tri_stretch(uvs.values.data(), positions.data(), uvs.count, tris.data(), tris.size(),
                                                   &raw, &n);
        FloatsOut owned(raw);
        check(status, "UV stretch");
        return {{"values", arrayOf(raw, n)}};
    }
    if (op == "average_scale") {
        const auto uvs = readUvs(body);
        const auto positions = readPositions(body, uvs.count);
        const auto tris = indices(body, "tris");
        const auto selection = indices(body, "selection");
        const auto ids = islandIds(uvs.count, tris);
        return uvsResult("UV average scale", [&](float** out, std::size_t* n) {
            return radicle_uv_average_scale(uvs.values.data(), positions.data(), uvs.count, tris.data(), tris.size(),
                                            ids.data(), selection.data(), selection.size(), out, n);
        });
    }
    if (op == "relax") {
        const auto uvs = readUvs(body);
        const auto tris = indices(body, "tris");
        const auto selection = indices(body, "selection");
        const double iterations = number(body, "iterations");
        return uvsResult("UV relax", [&](float** out, std::size_t* n) {
            return radicle_uv_relax(uvs.values.data(), uvs.count, tris.data(), tris.size(), selection.data(),
                                    selection.size(), iterations, out, n);
        });
    }
    if (op == "stitch") {
        const auto uvs = readUvs(body);
        const auto positions = readPositions(body, uvs.count);
        const auto tris = indices(body, "tris");
        const auto selection = indices(body, "selection");
        const auto ids = islandIds(uvs.count, tris);
        const double eps = number(body, "eps");
        return uvsResult("UV stitch", [&](float** out, std::size_t* n) {
            return radicle_uv_stitch(uvs.values.data(), positions.data(), uvs.count, ids.data(), selection.data(),
                                     selection.size(), eps, out, n);
        });
    }
    if (op == "overlapping_faces") {
        const auto uvs = readUvs(body);
        const auto tris = indices(body, "tris");
        const auto hidden = indices(body, "hidden");
        return listResult("UV overlap", "faces", [&](std::uint32_t** out, std::size_t* n) {
            return radicle_uv_overlapping_faces(uvs.values.data(), uvs.count, tris.data(), tris.size(), hidden.data(),
                                                hidden.size(), out, n);
        });
    }
    if (op == "seams_in_selection") {
        const auto tris = indices(body, "tris");
        const auto selection = indices(body, "selection");
        const auto hidden = indices(body, "hidden");
        return listResult("UV seam marking", "pairs", [&](std::uint32_t** out, std::size_t* n) {
            std::size_t pairs = 0;
            const auto status = radicle_uv_seams_in_selection(tris.data(), tris.size(), selection.data(),
                                                              selection.size(), hidden.data(), hidden.size(), out, &pairs);
            *n = 2 * pairs;
            return status;
        });
    }
    if (op == "split_plan") {
        const auto count = whole(body, "vertex_count");
        const auto tris = indices(body, "tris");
        const auto seams = indices(body, "seams");
        if (seams.size() % 2 != 0) refuse("seams must hold 2 vertices per edge");
        std::uint32_t* sources = nullptr;
        std::uint32_t* planned = nullptr;
        std::size_t sourceCount = 0, triCount = 0;
        const auto status = radicle_uv_split_plan(count, tris.data(), tris.size(), seams.data(), seams.size() / 2,
                                                  &sources, &sourceCount, &planned, &triCount);
        RefsOut ownedSources(sources);
        RefsOut ownedTris(planned);
        check(status, "UV split plan");
        return {{"sources", arrayOf(sources, sourceCount)}, {"tris", arrayOf(planned, triCount)}};
    }
    const auto uvs = readUvs(body);
    const auto selection = indices(body, "selection");
    if (op == "merge_at_centre") {
        return uvsResult("UV merge", [&](float** out, std::size_t* n) {
            return radicle_uv_merge_at_centre(uvs.values.data(), uvs.count, selection.data(), selection.size(), out, n);
        });
    }
    if (op == "merge_by_distance") {
        const double distance = number(body, "distance");
        return uvsResult("UV weld", [&](float** out, std::size_t* n) {
            return radicle_uv_merge_by_distance(uvs.values.data(), uvs.count, selection.data(), selection.size(),
                                                distance, out, n);
        });
    }
    if (op == "align") {
        const auto axis = whole(body, "axis");
        const auto to = whole(body, "to");
        return uvsResult("UV align", [&](float** out, std::size_t* n) {
            return radicle_uv_align(uvs.values.data(), uvs.count, selection.data(), selection.size(), axis, to, out, n);
        });
    }
    if (op == "snap_grid") {
        const double step = number(body, "step");
        return uvsResult("UV grid snap", [&](float** out, std::size_t* n) {
            return radicle_uv_snap_grid(uvs.values.data(), uvs.count, selection.data(), selection.size(), step, out, n);
        });
    }
    if (op == "snap_pixels") {
        const double width = number(body, "width");
        const double height = number(body, "height");
        const auto centre = static_cast<std::uint8_t>(flag(body, "centre") ? 1 : 0);
        return uvsResult("UV pixel snap", [&](float** out, std::size_t* n) {
            return radicle_uv_snap_pixels(uvs.values.data(), uvs.count, selection.data(), selection.size(), width,
                                          height, centre, out, n);
        });
    }
    if (op == "clamp_tile") {
        return uvsResult("UV clamp", [&](float** out, std::size_t* n) {
            return radicle_uv_clamp_tile(uvs.values.data(), uvs.count, selection.data(), selection.size(), out, n);
        });
    }
    refuse("unknown UV operation '" + std::string(op) + "'");
}

}

Result<std::string> nifUvOp(std::string_view op, const nlohmann::json& body) {
    try {
        return run(op, body).dump();
    } catch (const std::exception& ex) {
        return std::unexpected(Error{"UvOpFailed", ex.what()});
    }
}

}
