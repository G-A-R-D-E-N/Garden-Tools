#include "f4lod/btr_writer.hpp"

#include <meshoptimizer.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace f4lod {
namespace {

struct Vec3 { float x{}; float y{}; float z{}; };

struct ShapePayload {
    bool valid{};
    std::string error;
    std::vector<unsigned char> data;
    std::vector<Vec3> local_positions;
    TerrainBtrTransform transform;
};

void u8(std::vector<unsigned char>& out, std::uint8_t value) { out.push_back(value); }
void u16(std::vector<unsigned char>& out, std::uint16_t value) {
    u8(out, static_cast<std::uint8_t>(value & 0xFFU));
    u8(out, static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
}
void u32(std::vector<unsigned char>& out, std::uint32_t value) {
    u8(out, static_cast<std::uint8_t>(value & 0xFFU));
    u8(out, static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    u8(out, static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
    u8(out, static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
}
void i32(std::vector<unsigned char>& out, std::int32_t value) { u32(out, static_cast<std::uint32_t>(value)); }
void f32(std::vector<unsigned char>& out, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    u32(out, bits);
}
void vec3(std::vector<unsigned char>& out, const Vec3& value) { f32(out, value.x); f32(out, value.y); f32(out, value.z); }
void rotation(std::vector<unsigned char>& out, const float (&value)[9]) { for (float component : value) f32(out, component); }

constexpr float kIdentityRotation[9]{1,0,0, 0,1,0, 0,0,1};

void avobject_prefix(std::vector<unsigned char>& out, std::int32_t name_index,
                     std::uint16_t flags, std::uint16_t flags2,
                     const Vec3& translation, const float (&rot)[9], float scale) {
    i32(out, name_index); u32(out, 0U); i32(out, -1);
    u16(out, flags); u16(out, flags2); vec3(out, translation); rotation(out, rot); f32(out, scale); i32(out, -1);
}

Vec3 subtract(const TerrainVertex& a, const TerrainVertex& b) { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
Vec3 cross(const Vec3& a, const Vec3& b) { return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x}; }
void add(Vec3& a, const Vec3& b) { a.x += b.x; a.y += b.y; a.z += b.z; }
Vec3 normalized(Vec3 value) {
    const float l2 = value.x*value.x + value.y*value.y + value.z*value.z;
    if (!(l2 > 0.0F) || !std::isfinite(l2)) return {0,0,1};
    const float inv = 1.0F / std::sqrt(l2);
    return {value.x*inv, value.y*inv, value.z*inv};
}
std::vector<Vec3> generate_normals(const TerrainSimplifiedMesh& mesh) {
    std::vector<Vec3> normals(mesh.vertices.size());
    for (std::size_t i = 0; i < mesh.indices.size(); i += 3U) {
        const auto ia=mesh.indices[i], ib=mesh.indices[i+1], ic=mesh.indices[i+2];
        const auto face = cross(subtract(mesh.vertices[ib], mesh.vertices[ia]), subtract(mesh.vertices[ic], mesh.vertices[ia]));
        add(normals[ia], face); add(normals[ib], face); add(normals[ic], face);
    }
    for (auto& n : normals) n = normalized(n);
    return normals;
}
std::uint8_t float_to_nif_byte(float value) {
    value = std::clamp(value, -1.0F, 1.0F);
    return static_cast<std::uint8_t>(std::floor(((value + 1.0F) * 0.5F) * 255.0F + 0.5F));
}

bool finite_transform(const TerrainBtrTransform& t) noexcept {
    if (!std::isfinite(t.translation_x) || !std::isfinite(t.translation_y) || !std::isfinite(t.translation_z) ||
        !std::isfinite(t.scale) || t.scale <= 0.0F) return false;
    for (float v : t.rotation) if (!std::isfinite(v)) return false;
    return true;
}
Vec3 inverse_transform_point(const TerrainVertex& v, const TerrainBtrTransform& t) {
    const float dx=(v.x-t.translation_x)/t.scale, dy=(v.y-t.translation_y)/t.scale, dz=(v.z-t.translation_z)/t.scale;
    return {t.rotation[0]*dx+t.rotation[3]*dy+t.rotation[6]*dz,
            t.rotation[1]*dx+t.rotation[4]*dy+t.rotation[7]*dz,
            t.rotation[2]*dx+t.rotation[5]*dy+t.rotation[8]*dz};
}
Vec3 inverse_transform_direction(const Vec3& v, const TerrainBtrTransform& t) {
    return normalized({t.rotation[0]*v.x+t.rotation[3]*v.y+t.rotation[6]*v.z,
                       t.rotation[1]*v.x+t.rotation[4]*v.y+t.rotation[7]*v.z,
                       t.rotation[2]*v.x+t.rotation[5]*v.y+t.rotation[8]*v.z});
}
bool valid_property_ref(std::int32_t ref, std::size_t count) noexcept {
    return ref == -1 || (ref >= 0 && static_cast<std::size_t>(ref) < count);
}

bool valid_recovered_lod(std::uint32_t lod) noexcept {
    return lod == 4U || lod == 8U || lod == 16U || lod == 32U;
}

float world_mid_z(const TerrainSimplifiedMesh& mesh) {
    float min_z = std::numeric_limits<float>::infinity();
    float max_z = -std::numeric_limits<float>::infinity();
    for (const auto& vertex : mesh.vertices) {
        min_z = std::min(min_z, vertex.z);
        max_z = std::max(max_z, vertex.z);
    }
    return (min_z + max_z) * 0.5F;
}

float world_height_span(const TerrainSimplifiedMesh& mesh) {
    float min_z = std::numeric_limits<float>::infinity();
    float max_z = -std::numeric_limits<float>::infinity();
    for (const auto& vertex : mesh.vertices) {
        min_z = std::min(min_z, vertex.z);
        max_z = std::max(max_z, vertex.z);
    }
    return max_z - min_z;
}

TerrainBtrTransform recovered_shape_transform(
    const TerrainBtrBuildOptions& options,
    float mid_z) {
    TerrainBtrTransform transform;
    transform.translation_x = 0.0F;
    transform.translation_y = 0.0F;
    transform.translation_z = mid_z + options.z_shift;
    transform.scale = static_cast<float>(options.lod_level);
    return transform;
}

Vec3 recovered_local_point(
    const TerrainVertex& vertex,
    const TerrainBtrBuildOptions& options,
    float mid_z) {
    const float level = static_cast<float>(options.lod_level);
    return {
        (vertex.x - options.block_origin_x) / level,
        (vertex.y - options.block_origin_y) / level,
        (vertex.z - mid_z) / level,
    };
}

std::vector<unsigned char> make_root_node(std::int32_t name_index) {
    std::vector<unsigned char> out;
    avobject_prefix(out, name_index, 14U, 0U, {}, kIdentityRotation, 1.0F);
    u32(out, 1U); i32(out, 1); i32(out, 2); u32(out, 0U);
    return out;
}
std::vector<unsigned char> make_multibound() { std::vector<unsigned char> out; i32(out, 3); return out; }

std::vector<unsigned char> make_world_aabb(const TerrainSimplifiedMesh& mesh) {
    float minx=std::numeric_limits<float>::infinity(), miny=minx, minz=minx;
    float maxx=-minx, maxy=-minx, maxz=-minx;
    for (const auto& v : mesh.vertices) {
        minx=std::min(minx,v.x); miny=std::min(miny,v.y); minz=std::min(minz,v.z);
        maxx=std::max(maxx,v.x); maxy=std::max(maxy,v.y); maxz=std::max(maxz,v.z);
    }
    std::vector<unsigned char> out;
    vec3(out,{(minx+maxx)*0.5F,(miny+maxy)*0.5F,(minz+maxz)*0.5F});
    vec3(out,{(maxx-minx)*0.5F,(maxy-miny)*0.5F,(maxz-minz)*0.5F});
    return out;
}

std::vector<unsigned char> make_recovered_aabb(
    const std::vector<Vec3>& local_positions,
    std::uint32_t lod_level) {
    float minx=std::numeric_limits<float>::infinity(), miny=minx, minz=minx;
    float maxx=-minx, maxy=-minx, maxz=-minx;
    for (const auto& v : local_positions) {
        minx=std::min(minx,v.x); miny=std::min(miny,v.y); minz=std::min(minz,v.z);
        maxx=std::max(maxx,v.x); maxy=std::max(maxy,v.y); maxz=std::max(maxz,v.z);
    }
    const float scale = static_cast<float>(lod_level);
    std::vector<unsigned char> out;
    vec3(out,{(minx+maxx)*0.5F*scale,(miny+maxy)*0.5F*scale,(minz+maxz)*0.5F*scale});
    vec3(out,{(maxx-minx)*0.5F*scale,(maxy-miny)*0.5F*scale,(maxz-minz)*0.5F*scale});
    return out;
}

ShapePayload make_shape(const TerrainSimplifiedMesh& mesh,
                        const TerrainBtrBuildOptions& options,
                        std::int32_t name_index) {
    ShapePayload result;
    if (mesh.vertices.size() > 65535U || mesh.indices.size()%3U != 0U) {
        result.error = "Terrain mesh cannot be represented by one FO4 BSTriShape."; return result;
    }
    if (options.use_recovered_terrain_block_space && !valid_recovered_lod(options.lod_level)) {
        result.error = "Recovered FO4 terrain block space requires LOD4, LOD8, LOD16, or LOD32.";
        return result;
    }
    if (!options.use_recovered_terrain_block_space && !finite_transform(options.shape_transform)) {
        result.error = "BTR shape transform is invalid."; return result;
    }
    if (!std::isfinite(options.uv_span_x) || !std::isfinite(options.uv_span_y) || options.uv_span_x==0 || options.uv_span_y==0) {
        result.error = "BTR UV spans must be finite and non-zero."; return result;
    }
    for (auto index : mesh.indices) if (index >= mesh.vertices.size() || index > 65535U) {
        result.error = "Terrain mesh contains an invalid FO4 index."; return result;
    }

    const float recovered_mid_z = options.use_recovered_terrain_block_space
        ? world_mid_z(mesh)
        : 0.0F;
    result.transform = options.use_recovered_terrain_block_space
        ? recovered_shape_transform(options, recovered_mid_z)
        : options.shape_transform;

    const auto world_normals = generate_normals(mesh);
    std::vector<Vec3> local_normals;
    result.local_positions.reserve(mesh.vertices.size()); local_normals.reserve(mesh.vertices.size());
    for (std::size_t i=0;i<mesh.vertices.size();++i) {
        const auto p = options.use_recovered_terrain_block_space
            ? recovered_local_point(mesh.vertices[i], options, recovered_mid_z)
            : inverse_transform_point(mesh.vertices[i], result.transform);
        result.local_positions.push_back(p);
        local_normals.push_back(options.use_recovered_terrain_block_space
            ? normalized(world_normals[i])
            : inverse_transform_direction(world_normals[i], result.transform));
    }

    // TerrainLOD uses half positions by default and forces full precision only
    // when the local terrain height span exceeds 131008 units.
    bool full = options.position_precision == TerrainBtrVertexPrecision::full;
    if (options.use_recovered_terrain_block_space) {
        full = world_height_span(mesh) / static_cast<float>(options.lod_level) > 131008.0F;
    }
    if (!full) {
        for (const auto& p : result.local_positions) {
            if (std::fabs(p.x)>65504 || std::fabs(p.y)>65504 || std::fabs(p.z)>65504) {
                result.error = "Half-precision BTR position exceeds fp16 range."; return result;
            }
        }
    }

    Vec3 center{};
    for (const auto& v : result.local_positions) add(center,v);
    const float inv = 1.0F/static_cast<float>(result.local_positions.size()); center.x*=inv; center.y*=inv; center.z*=inv;
    float radius2=0;
    for (const auto& v : result.local_positions) {
        const float dx=v.x-center.x,dy=v.y-center.y,dz=v.z-center.z;
        radius2=std::max(radius2,dx*dx+dy*dy+dz*dz);
    }

    const std::uint16_t flags = static_cast<std::uint16_t>(16U|32U|128U|(full?16384U:0U));
    const std::uint32_t stride = full ? 24U : 16U;
    const auto triangles=static_cast<std::uint32_t>(mesh.indices.size()/3U);
    const auto vertices=static_cast<std::uint16_t>(mesh.vertices.size());
    const std::uint64_t data_size=static_cast<std::uint64_t>(vertices)*stride+static_cast<std::uint64_t>(triangles)*6ULL;
    if (data_size > std::numeric_limits<std::uint32_t>::max()) { result.error="Terrain BTR dataSize exceeds 32-bit range."; return result; }

    auto& out = result.data;
    avobject_prefix(out,name_index,options.av_flags,options.av_flags2,
                    {result.transform.translation_x,result.transform.translation_y,result.transform.translation_z},
                    result.transform.rotation,result.transform.scale);
    vec3(out,center); f32(out,std::sqrt(radius2));
    i32(out,-1); i32(out,options.shader_property_0); i32(out,options.shader_property_1);
    // BSTriShape.Write derives vertexSize from enabled component groups. For
    // position + UV + normal it is 5 regardless of half/full position storage.
    u8(out,5U); u8(out,full?4U:2U);
    u8(out,options.vertex_flag3); u8(out,options.vertex_flag4); u8(out,options.vertex_flag5);
    u16(out,flags); u8(out,options.vertex_flag8); u32(out,triangles); u16(out,vertices); u32(out,static_cast<std::uint32_t>(data_size));

    for (std::size_t i=0;i<result.local_positions.size();++i) {
        const auto& p=result.local_positions[i];
        if (full) { f32(out,p.x); f32(out,p.y); f32(out,p.z); f32(out,0); }
        else { u16(out,meshopt_quantizeHalf(p.x)); u16(out,meshopt_quantizeHalf(p.y)); u16(out,meshopt_quantizeHalf(p.z)); u16(out,0); }

        float u=0.0F, v=0.0F;
        if (options.use_recovered_terrain_block_space) {
            u = p.x / 4096.0F;
            v = 1.0F - p.y / 4096.0F;
        } else {
            u = (mesh.vertices[i].x-options.uv_origin_x)/options.uv_span_x;
            v = (mesh.vertices[i].y-options.uv_origin_y)/options.uv_span_y;
        }
        u16(out,meshopt_quantizeHalf(u)); u16(out,meshopt_quantizeHalf(v));
        u8(out,float_to_nif_byte(local_normals[i].x)); u8(out,float_to_nif_byte(local_normals[i].y));
        u8(out,float_to_nif_byte(local_normals[i].z)); u8(out,0);
    }
    for (auto index:mesh.indices) u16(out,static_cast<std::uint16_t>(index));
    // FO4 terrain land is a plain BSTriShape. The 12-byte
    // numTriangles2/numA/numB suffix belongs only to BSSubIndexTriShape (used
    // by LOD4 water/object paths) and must not be appended to land geometry.
    result.valid = true;
    return result;
}

} // namespace

TerrainBtrDocument build_fallout4_terrain_btr_document(const TerrainSimplifiedMesh& mesh,
                                                        const TerrainBtrBuildOptions& options) {
    TerrainBtrDocument result;
    if (!mesh.valid) { result.error="Cannot build BTR from invalid terrain mesh: "+mesh.error; return result; }
    if (mesh.vertices.empty() || mesh.indices.empty()) { result.error="Cannot build BTR from empty terrain mesh."; return result; }
    const std::size_t final_count=4U+options.additional_blocks.size();
    if (!valid_property_ref(options.shader_property_0,final_count)||!valid_property_ref(options.shader_property_1,final_count)) {
        result.error="BTR shader property reference is out of range."; return result;
    }
    if ((options.shader_property_0>=0&&options.shader_property_0<4)||(options.shader_property_1>=0&&options.shader_property_1<4)) {
        result.error="BTR shader properties may not reference reserved blocks 0-3."; return result;
    }

    const std::uint32_t expected_reserved_strings = options.use_recovered_terrain_block_space ? 2U : 1U;
    if (options.reserved_string_count != expected_reserved_strings) {
        result.error = "BTR reserved string count does not match the selected terrain writer mode.";
        return result;
    }
    const std::int32_t root_name_index = 0;
    const std::int32_t shape_name_index = options.use_recovered_terrain_block_space ? 1 : 0;
    auto shape=make_shape(mesh,options,shape_name_index);
    if (!shape.valid) { result.error=std::move(shape.error); return result; }

    result.nif.creator="F4LODGen";
    if (options.use_recovered_terrain_block_space) {
        result.nif.strings.push_back("chunk");
        result.nif.strings.push_back("Land");
    } else {
        result.nif.strings.push_back(options.shape_name);
    }
    result.nif.strings.insert(result.nif.strings.end(),options.additional_strings.begin(),options.additional_strings.end());
    result.nif.blocks.push_back({"BSMultiBoundNode",make_root_node(root_name_index)});
    result.nif.blocks.push_back({"BSTriShape",std::move(shape.data)});
    result.nif.blocks.push_back({"BSMultiBound",make_multibound()});
    result.nif.blocks.push_back({"BSMultiBoundAABB",
        options.use_recovered_terrain_block_space
            ? make_recovered_aabb(shape.local_positions, options.lod_level)
            : make_world_aabb(mesh)});
    result.nif.blocks.insert(result.nif.blocks.end(),options.additional_blocks.begin(),options.additional_blocks.end());
    result.nif.roots={0};
    result.vertex_count=mesh.vertices.size(); result.triangle_count=mesh.indices.size()/3U;
    result.renderable=options.shader_property_0>=0||options.shader_property_1>=0; result.valid=true;
    return result;
}

TerrainBtrWriteResult write_fallout4_terrain_btr(const std::filesystem::path& path,
                                                  const TerrainSimplifiedMesh& mesh,
                                                  const TerrainBtrBuildOptions& options) {
    TerrainBtrWriteResult result;
    const auto document=build_fallout4_terrain_btr_document(mesh,options);
    if (!document.valid) { result.error=document.error; return result; }
    const auto written=write_fallout4_nif(path,document.nif);
    if (!written.valid) { result.error=written.error; return result; }
    result.bytes_written=written.bytes_written; result.renderable=document.renderable; result.valid=true; return result;
}

const char* terrain_btr_vertex_precision_name(TerrainBtrVertexPrecision precision) noexcept {
    switch (precision) { case TerrainBtrVertexPrecision::half:return "half"; case TerrainBtrVertexPrecision::full:return "full"; }
    return "unknown";
}

} // namespace f4lod
