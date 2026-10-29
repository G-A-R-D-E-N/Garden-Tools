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

inline std::vector<std::byte> triangleShapeBlock(std::uint32_t shader, float translationX = 0.0f) {
    std::vector<std::byte> out;
    putUint(out, 0, 4);
    putUint(out, 0, 4);
    putUint(out, 0xFFFFFFFFU, 4);
    putUint(out, 0, 4);
    for (const float value : {translationX, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f}) {
        putFloat(out, value);
    }
    putUint(out, 0xFFFFFFFFU, 4);
    for (int i = 0; i < 4; ++i) putFloat(out, 0.0f);
    putUint(out, 0xFFFFFFFFU, 4);
    putUint(out, shader, 4);
    putUint(out, 0xFFFFFFFFU, 4);
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

inline std::vector<std::byte> shaderBlock(std::uint32_t nameIndex, std::uint32_t textureSet) {
    std::vector<std::byte> out;
    putUint(out, 1, 4);
    putUint(out, nameIndex, 4);
    putUint(out, 0, 4);
    putUint(out, 0xFFFFFFFFU, 4);
    for (int i = 0; i < 6; ++i) putUint(out, 0, 4);
    putUint(out, textureSet, 4);
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

inline void writeBytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

}
