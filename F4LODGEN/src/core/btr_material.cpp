#include "f4lod/btr_material.hpp"

#include "f4lod/nif.hpp"

#include <cstring>
#include <fstream>
#include <limits>
#include <utility>
#include <vector>

namespace f4lod {
namespace {

class Cursor {
public:
    Cursor(const std::vector<unsigned char>& data, std::size_t begin, std::size_t end)
        : data_(data), pos_(begin), end_(end <= data.size() ? end : data.size()) {}

    [[nodiscard]] std::size_t remaining() const noexcept {
        return pos_ <= end_ ? end_ - pos_ : 0;
    }

    bool u32(std::uint32_t& value) {
        if (remaining() < 4U) return false;
        value = static_cast<std::uint32_t>(data_[pos_]) |
                (static_cast<std::uint32_t>(data_[pos_ + 1U]) << 8U) |
                (static_cast<std::uint32_t>(data_[pos_ + 2U]) << 16U) |
                (static_cast<std::uint32_t>(data_[pos_ + 3U]) << 24U);
        pos_ += 4U;
        return true;
    }

    bool i32(std::int32_t& value) {
        std::uint32_t bits = 0;
        if (!u32(bits)) return false;
        std::memcpy(&value, &bits, sizeof(value));
        return true;
    }

    bool f32(float& value) {
        std::uint32_t bits = 0;
        if (!u32(bits)) return false;
        std::memcpy(&value, &bits, sizeof(value));
        return true;
    }

    bool skip(std::size_t bytes) {
        if (bytes > remaining()) return false;
        pos_ += bytes;
        return true;
    }

    bool sized_string(std::string& value, std::size_t max_length = 4096U) {
        std::uint32_t size = 0;
        if (!u32(size) || size > max_length || size > remaining()) return false;
        value.assign(reinterpret_cast<const char*>(data_.data() + pos_), size);
        pos_ += size;
        return true;
    }

    std::vector<unsigned char> take_remaining() {
        std::vector<unsigned char> bytes;
        bytes.insert(bytes.end(), data_.begin() + static_cast<std::ptrdiff_t>(pos_),
                     data_.begin() + static_cast<std::ptrdiff_t>(end_));
        pos_ = end_;
        return bytes;
    }

private:
    const std::vector<unsigned char>& data_;
    std::size_t pos_{};
    std::size_t end_{};
};

std::vector<unsigned char> read_file(const std::filesystem::path& path, std::string& error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "Could not open BTR file.";
        return {};
    }
    input.seekg(0, std::ios::end);
    const auto end = input.tellg();
    if (end < 0) {
        error = "Could not determine BTR file size.";
        return {};
    }
    const auto size64 = static_cast<std::uint64_t>(end);
    if (size64 > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        error = "BTR file is too large for this process.";
        return {};
    }
    input.seekg(0, std::ios::beg);
    std::vector<unsigned char> data(static_cast<std::size_t>(size64));
    if (!data.empty()) {
        input.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (input.gcount() != static_cast<std::streamsize>(data.size())) {
            error = "BTR file is truncated.";
            return {};
        }
    }
    return data;
}

bool parse_object_net(Cursor& cursor, std::int32_t& name_index) {
    if (!cursor.i32(name_index)) return false;
    std::uint32_t extra_count = 0;
    if (!cursor.u32(extra_count) || extra_count > 1'000'000U) return false;
    if (static_cast<std::uint64_t>(extra_count) * 4ULL > cursor.remaining()) return false;
    if (!cursor.skip(static_cast<std::size_t>(extra_count) * 4U)) return false;
    std::int32_t controller = -1;
    return cursor.i32(controller);
}

bool read_vec2(Cursor& cursor, float (&values)[2]) {
    return cursor.f32(values[0]) && cursor.f32(values[1]);
}

bool read_vec3(Cursor& cursor, float (&values)[3]) {
    return cursor.f32(values[0]) && cursor.f32(values[1]) && cursor.f32(values[2]);
}

bool resolve_string_index(
    const NifInspection& nif,
    std::int32_t index,
    std::string& value,
    std::string& error,
    const char* field_name) {
    if (index < 0) {
        value.clear();
        return true;
    }
    if (static_cast<std::size_t>(index) >= nif.strings.size()) {
        error = std::string(field_name) + " string index is out of range.";
        return false;
    }
    value = nif.strings[static_cast<std::size_t>(index)];
    return true;
}

} // namespace

BtrLightingShaderInspection inspect_btr_lighting_shader(
    const std::filesystem::path& path,
    std::int32_t block_index) {
    BtrLightingShaderInspection result;
    result.block_index = block_index;

    const auto nif = inspect_nif(path);
    if (!nif.valid) {
        result.error = nif.error;
        return result;
    }
    if (!nif.is_fallout4()) {
        result.error = "Lighting shader is not inside a Fallout 4 12/130 NIF stream.";
        return result;
    }
    if (block_index < 0 || static_cast<std::size_t>(block_index) >= nif.blocks.size()) {
        result.error = "Lighting shader block index is out of range.";
        return result;
    }
    const auto& block = nif.blocks[static_cast<std::size_t>(block_index)];
    if (block.type != "BSLightingShaderProperty") {
        result.error = "Referenced property block is not BSLightingShaderProperty.";
        return result;
    }

    std::string file_error;
    const auto data = read_file(path, file_error);
    if (!file_error.empty()) {
        result.error = std::move(file_error);
        return result;
    }
    if (block.offset + block.size > data.size()) {
        result.error = "Lighting shader block extends past the BTR file.";
        return result;
    }

    Cursor cursor(data, static_cast<std::size_t>(block.offset),
                  static_cast<std::size_t>(block.offset + block.size));
    if (!cursor.u32(result.shader_type) || !parse_object_net(cursor, result.name_index) ||
        !cursor.u32(result.shader_flags_1) || !cursor.u32(result.shader_flags_2) ||
        !read_vec2(cursor, result.uv_offset) || !read_vec2(cursor, result.uv_scale) ||
        !cursor.i32(result.texture_set) || !read_vec3(cursor, result.emissive_color) ||
        !cursor.f32(result.emissive_multiple) || !cursor.i32(result.wet_material_index) ||
        !cursor.u32(result.texture_clamp_mode) || !cursor.f32(result.alpha) ||
        !cursor.f32(result.refraction_strength) || !cursor.f32(result.glossiness) ||
        !read_vec3(cursor, result.specular_color) || !cursor.f32(result.specular_strength) ||
        !cursor.f32(result.subsurface_rolloff) || !cursor.f32(result.unknown_float_1) ||
        !cursor.f32(result.backlight_power) || !cursor.f32(result.grayscale_to_palette_scale) ||
        !cursor.f32(result.fresnel_power) || !cursor.f32(result.wetness_spec_scale) ||
        !cursor.f32(result.wetness_spec_power) || !cursor.f32(result.wetness_min_var) ||
        !cursor.f32(result.wetness_env_map_scale) || !cursor.f32(result.wetness_fresnel_power) ||
        !cursor.f32(result.wetness_metalness)) {
        result.error = "BSLightingShaderProperty is truncated in Fallout 4 material metadata.";
        return result;
    }

    if (!resolve_string_index(nif, result.name_index, result.name, result.error, "Shader name")) {
        return result;
    }
    if (!resolve_string_index(
            nif, result.wet_material_index, result.wet_material_name,
            result.error, "Wet material")) {
        return result;
    }

    result.type_specific_tail = cursor.take_remaining();
    result.valid = true;
    return result;
}

BtrTextureSetInspection inspect_btr_texture_set(
    const std::filesystem::path& path,
    std::int32_t block_index) {
    BtrTextureSetInspection result;
    result.block_index = block_index;

    const auto nif = inspect_nif(path);
    if (!nif.valid) {
        result.error = nif.error;
        return result;
    }
    if (block_index < 0 || static_cast<std::size_t>(block_index) >= nif.blocks.size()) {
        result.error = "Texture-set block index is out of range.";
        return result;
    }
    const auto& block = nif.blocks[static_cast<std::size_t>(block_index)];
    if (block.type != "BSShaderTextureSet") {
        result.error = "Referenced texture-set block is not BSShaderTextureSet.";
        return result;
    }

    std::string file_error;
    const auto data = read_file(path, file_error);
    if (!file_error.empty()) {
        result.error = std::move(file_error);
        return result;
    }
    if (block.offset + block.size > data.size()) {
        result.error = "Texture-set block extends past the BTR file.";
        return result;
    }

    Cursor cursor(data, static_cast<std::size_t>(block.offset),
                  static_cast<std::size_t>(block.offset + block.size));
    std::int32_t count = 0;
    if (!cursor.i32(count) || count < 0 || count > 1024) {
        result.error = "BSShaderTextureSet declares an invalid texture count.";
        return result;
    }
    result.textures.reserve(static_cast<std::size_t>(count));
    for (std::int32_t i = 0; i < count; ++i) {
        std::string texture;
        if (!cursor.sized_string(texture)) {
            result.error = "BSShaderTextureSet contains a truncated texture path.";
            return result;
        }
        result.textures.push_back(std::move(texture));
    }
    if (cursor.remaining() != 0U) {
        result.error = "BSShaderTextureSet contains trailing bytes.";
        return result;
    }

    result.valid = true;
    return result;
}

BtrMaterialInspection inspect_btr_material(
    const std::filesystem::path& path,
    std::int32_t property_block_index) {
    BtrMaterialInspection result;
    result.lighting = inspect_btr_lighting_shader(path, property_block_index);
    if (!result.lighting.valid) {
        result.error = result.lighting.error;
        return result;
    }

    if (result.lighting.texture_set >= 0) {
        result.texture_set = inspect_btr_texture_set(path, result.lighting.texture_set);
        if (!result.texture_set.valid) {
            result.error = result.texture_set.error;
            return result;
        }
    }

    result.valid = true;
    return result;
}

} // namespace f4lod
