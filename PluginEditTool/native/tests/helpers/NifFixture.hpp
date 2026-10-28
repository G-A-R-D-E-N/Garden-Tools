#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
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

inline std::vector<std::byte> triangleShapeBlock() {
    std::vector<std::byte> out;
    putUint(out, 0, 4);
    putUint(out, 0, 4);
    putUint(out, 0xFFFFFFFFU, 4);
    putUint(out, 0, 4);
    for (const float value : {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f}) {
        putFloat(out, value);
    }
    putUint(out, 0xFFFFFFFFU, 4);
    for (int i = 0; i < 4; ++i) putFloat(out, 0.0f);
    putUint(out, 0xFFFFFFFFU, 4);
    putUint(out, 0xFFFFFFFFU, 4);
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

inline std::vector<std::byte> triangleNif() {
    const auto shape = triangleShapeBlock();
    std::vector<std::byte> out;
    putText(out, "Gamebryo File Format, Version 20.2.0.7\n");
    putUint(out, 0x14020007U, 4);
    putUint(out, 1, 1);
    putUint(out, 12, 4);
    putUint(out, 1, 4);
    putUint(out, 130, 4);
    for (int i = 0; i < 4; ++i) putUint(out, 0, 1);
    putUint(out, 1, 2);
    putUint(out, 10, 4);
    putText(out, "BSTriShape");
    putUint(out, 0, 2);
    putUint(out, shape.size(), 4);
    putUint(out, 1, 4);
    putUint(out, 0, 4);
    putUint(out, 3, 4);
    putText(out, "Tri");
    putUint(out, 0, 4);
    out.insert(out.end(), shape.begin(), shape.end());
    putUint(out, 1, 4);
    putUint(out, 0, 4);
    return out;
}

inline void writeBytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

}
