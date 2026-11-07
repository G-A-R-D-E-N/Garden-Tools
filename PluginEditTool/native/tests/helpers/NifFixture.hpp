#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace fo4ide::test {

inline void putUint(std::vector<std::byte>& out, std::uint64_t value, std::size_t width) {
    for (std::size_t i = 0; i < width; ++i) out.push_back(static_cast<std::byte>(value >> (8U * i)));
}

inline void putFloat(std::vector<std::byte>& out, float value) {
    putUint(out, std::bit_cast<std::uint32_t>(value), 4);
}

inline void putText(std::vector<std::byte>& out, std::string_view text) {
    for (const char ch : text) out.push_back(static_cast<std::byte>(ch));
}

inline std::vector<std::byte> triangleShapeBlock(std::uint32_t shader, float translationX = 0.0f,
                                                 std::uint32_t alpha = 0xFFFFFFFFU) {
    std::vector<std::byte> out;
    putUint(out, 0, 4);
    putUint(out, 0, 4);
    putUint(out, 0xFFFFFFFFU, 4);
    putUint(out, 0, 4);
    for (const float value : {translationX, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f}) {
        putFloat(out, value);
    }
    putUint(out, 0xFFFFFFFFU, 4);
    for (const float value : {0.5f, 0.5f, 0.0f, 0.70710678f}) putFloat(out, value);
    putUint(out, 0xFFFFFFFFU, 4);
    putUint(out, shader, 4);
    putUint(out, alpha, 4);
    putUint(out, (std::uint64_t{1} << 54U) | 4U, 8);
    putUint(out, 1, 4);
    putUint(out, 3, 2);
    putUint(out, 54, 4);
    const float positions[9] = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    for (std::size_t vertex = 0; vertex < 3; ++vertex) {
        for (std::size_t i = 0; i < 3; ++i) putFloat(out, positions[vertex * 3 + i]);
        putUint(out, 0, 4);
    }
    putUint(out, 0, 2);
    putUint(out, 1, 2);
    putUint(out, 2, 2);
    return out;
}

struct FixtureBlock {
    std::string type;
    std::vector<std::byte> data;
};

// Requires every field read_lighting_shader consumes at BS 130; a shorter block reports a read error and refuses edits.
inline std::vector<std::byte> shaderBlock(std::uint32_t nameIndex, std::uint32_t textureSet) {
    std::vector<std::byte> out;
    putUint(out, 1, 4);
    putUint(out, nameIndex, 4);
    putUint(out, 0, 4);
    putUint(out, 0xFFFFFFFFU, 4);
    putUint(out, 0, 4);
    putUint(out, 0, 4);
    putFloat(out, 0.0f);
    putFloat(out, 0.0f);
    putFloat(out, 1.0f);
    putFloat(out, 1.0f);
    putUint(out, textureSet, 4);
    for (int i = 0; i < 3; ++i) putFloat(out, 0.0f);
    putFloat(out, 1.0f);
    putUint(out, 0xFFFFFFFFU, 4);
    putUint(out, 0, 4);
    putFloat(out, 1.0f);
    putFloat(out, 0.0f);
    putFloat(out, 0.5f);
    for (int i = 0; i < 3; ++i) putFloat(out, 0.0f);
    putFloat(out, 0.0f);
    putFloat(out, 0.0f);
    putFloat(out, 1.0f);
    putFloat(out, 1.0f);
    putFloat(out, 1.0f);
    for (int i = 0; i < 6; ++i) putFloat(out, 1.0f);
    putFloat(out, 1.0f);
    putUint(out, 0, 2);
    return out;
}

inline std::vector<std::byte> textureSetBlock(std::uint32_t slots) {
    std::vector<std::byte> out;
    putUint(out, slots, 4);
    for (std::uint32_t i = 0; i < slots; ++i) putUint(out, 0, 4);
    return out;
}

inline std::vector<std::byte> texturedSetBlock(const std::vector<std::string>& paths) {
    std::vector<std::byte> out;
    putUint(out, paths.size(), 4);
    for (const auto& path : paths) {
        putUint(out, path.size(), 4);
        putText(out, path);
    }
    return out;
}

inline std::vector<std::byte> nodeBlock(std::uint32_t nameIndex, float x, const std::vector<std::uint32_t>& children) {
    std::vector<std::byte> out;
    putUint(out, nameIndex, 4);
    putUint(out, 0, 4);
    putUint(out, 0xFFFFFFFFU, 4);
    putUint(out, 0, 4);
    for (const float value : {x, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f}) {
        putFloat(out, value);
    }
    putUint(out, 0xFFFFFFFFU, 4);
    putUint(out, children.size(), 4);
    for (const auto child : children) putUint(out, child, 4);
    return out;
}

inline std::vector<std::byte> nifFile(const std::vector<FixtureBlock>& blocks,
                                      const std::vector<std::string>& strings) {
    std::vector<std::byte> out;
    putText(out, "Gamebryo File Format, Version 20.2.0.7\n");
    putUint(out, 0x14020007U, 4);
    putUint(out, 1, 1);
    putUint(out, 12, 4);
    putUint(out, blocks.size(), 4);
    putUint(out, 130, 4);
    for (int i = 0; i < 4; ++i) putUint(out, 0, 1);
    putUint(out, blocks.size(), 2);
    for (const auto& block : blocks) {
        putUint(out, block.type.size(), 4);
        putText(out, block.type);
    }
    for (std::size_t index = 0; index < blocks.size(); ++index) putUint(out, index, 2);
    for (const auto& block : blocks) putUint(out, block.data.size(), 4);
    putUint(out, strings.size(), 4);
    putUint(out, 0, 4);
    for (const auto& text : strings) {
        putUint(out, text.size(), 4);
        putText(out, text);
    }
    putUint(out, 0, 4);
    for (const auto& block : blocks) out.insert(out.end(), block.data.begin(), block.data.end());
    putUint(out, 1, 4);
    putUint(out, 0, 4);
    return out;
}

inline std::vector<std::byte> triangleNif() {
    const std::vector<FixtureBlock> blocks{{"BSTriShape", triangleShapeBlock(0xFFFFFFFFU)}};
    return nifFile(blocks, {"Tri"});
}

inline std::vector<std::byte> materialNif(const std::string& material) {
    const std::vector<FixtureBlock> blocks{
        {"BSTriShape", triangleShapeBlock(1U)},
        {"BSLightingShaderProperty", shaderBlock(1U, 2U)},
        {"BSShaderTextureSet", textureSetBlock(2U)},
    };
    return nifFile(blocks, {"Tri", material});
}

inline std::vector<std::byte> treeNif(const std::string& material, const std::vector<std::string>& textures) {
    const std::vector<FixtureBlock> blocks{
        {"NiNode", nodeBlock(2U, 0.5f, {1U})},
        {"BSTriShape", triangleShapeBlock(2U, 0.25f)},
        {"BSLightingShaderProperty", shaderBlock(1U, 3U)},
        {"BSShaderTextureSet", texturedSetBlock(textures)},
    };
    return nifFile(blocks, {"Tri", material, "Root"});
}

inline std::vector<std::byte> effectShaderBlock(std::uint32_t nameIndex, std::uint32_t flags1,
                                                const std::string& sourceTexture,
                                                const std::vector<std::byte>& trailing = {},
                                                std::uint32_t flags2 = 0) {
    std::vector<std::byte> out;
    putUint(out, nameIndex, 4);
    putUint(out, 0, 4);
    putUint(out, 0xFFFFFFFFU, 4);
    putUint(out, flags1, 4);
    putUint(out, flags2, 4);
    for (const float value : {0.0f, 0.0f, 1.0f, 1.0f}) putFloat(out, value);
    putUint(out, sourceTexture.size(), 4);
    putText(out, sourceTexture);
    for (int i = 0; i < 4; ++i) putUint(out, 0, 1);
    for (const float value : {0.75f, 0.5f, 0.125f, 0.0625f, 0.25f, 0.5f, 0.75f, 1.0f, 2.0f, 3.0f}) putFloat(out, value);
    for (int i = 0; i < 4; ++i) putUint(out, 0, 4);
    putFloat(out, 1.0f);
    out.insert(out.end(), trailing.begin(), trailing.end());
    return out;
}

inline std::vector<std::byte> alphaPropertyBlock(std::uint16_t flags, std::uint8_t threshold = 0) {
    std::vector<std::byte> out;
    putUint(out, 0xFFFFFFFFU, 4);
    putUint(out, 0, 4);
    putUint(out, 0xFFFFFFFFU, 4);
    putUint(out, flags, 2);
    putUint(out, threshold, 1);
    return out;
}

inline std::vector<std::byte> bsxFlagsBlock(std::uint32_t nameIndex, std::uint32_t value) {
    std::vector<std::byte> out;
    putUint(out, nameIndex, 4);
    putUint(out, value, 4);
    return out;
}

inline std::vector<std::byte> connectParentsBlock(const std::string& parent, const std::string& name) {
    std::vector<std::byte> out;
    putUint(out, 0xFFFFFFFFU, 4);
    putUint(out, 1, 4);
    putUint(out, parent.size(), 4);
    putText(out, parent);
    putUint(out, name.size(), 4);
    putText(out, name);
    for (const float value : {0.5f, 0.25f, 0.125f, 0.0625f, 1.0f, 2.0f, 3.0f, 2.0f}) putFloat(out, value);
    return out;
}

inline std::vector<std::byte> connectChildrenBlock(bool skinned, const std::vector<std::string>& names) {
    std::vector<std::byte> out;
    putUint(out, 0xFFFFFFFFU, 4);
    putUint(out, skinned ? 1U : 0U, 1);
    putUint(out, names.size(), 4);
    for (const auto& name : names) {
        putUint(out, name.size(), 4);
        putText(out, name);
    }
    return out;
}

inline std::vector<std::byte> subIndexShapeBlock(const std::string& ssfFile) {
    auto out = triangleShapeBlock(0xFFFFFFFFU);
    for (const std::uint32_t value : {1U, 1U, 3U, 0U, 1U, 0U, 2U, 0U, 1U, 0U, 0U, 1U, 0U, 0U, 0U,
                                      1U, 3U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U}) {
        putUint(out, value, 4);
    }
    putUint(out, ssfFile.size(), 2);
    putText(out, ssfFile);
    return out;
}

inline std::vector<std::byte> effectNif(const std::vector<std::byte>& effect) {
    const std::vector<FixtureBlock> blocks{
        {"BSTriShape", triangleShapeBlock(1U, 0.0f, 2U)},
        {"BSEffectShaderProperty", effect},
        {"NiAlphaProperty", alphaPropertyBlock(0x0DU)},
        {"BSTriShape", triangleShapeBlock(4U)},
        {"BSLightingShaderProperty", shaderBlock(1U, 5U)},
        {"BSShaderTextureSet", texturedSetBlock({"lit.dds"})},
    };
    return nifFile(blocks, {"Tri", "Fx"});
}

inline std::vector<std::byte> coloredEffectNif() {
    auto shape = triangleShapeBlock(1U);
    const std::size_t head = shape.size() - 54 - 18;
    std::vector<std::byte> colored(shape.begin(), shape.begin() + static_cast<std::ptrdiff_t>(head));
    putUint(colored, (std::uint64_t{1} << 54U) | (std::uint64_t{1} << 49U) | 5U, 8);
    putUint(colored, 1, 4);
    putUint(colored, 3, 2);
    putUint(colored, 66, 4);
    const float positions[9] = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    for (std::size_t vertex = 0; vertex < 3; ++vertex) {
        for (std::size_t i = 0; i < 3; ++i) putFloat(colored, positions[vertex * 3 + i]);
        putUint(colored, 0, 4);
        putUint(colored, 0x00FF00FFU, 4);
    }
    putUint(colored, 0, 2);
    putUint(colored, 1, 2);
    putUint(colored, 2, 2);
    const std::vector<FixtureBlock> blocks{
        {"BSTriShape", colored},
        {"BSEffectShaderProperty", effectShaderBlock(1U, 0U, "", {}, 0x20U)},
    };
    return nifFile(blocks, {"Tri", "Fx"});
}

inline std::vector<std::byte> connectNif() {
    const std::vector<FixtureBlock> blocks{
        {"BSXFlags", bsxFlagsBlock(0U, 0x80U)},
        {"BSConnectPoint::Parents", connectParentsBlock("Bip01", "Weapon")},
        {"BSConnectPoint::Children", connectChildrenBlock(true, {"Hand"})},
    };
    return nifFile(blocks, {"Flag"});
}

inline std::vector<std::byte> subIndexNif() {
    const std::vector<FixtureBlock> blocks{{"BSSubIndexTriShape", subIndexShapeBlock("Seg.ssf")}};
    return nifFile(blocks, {"Tri"});
}

inline void writeBytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

}
