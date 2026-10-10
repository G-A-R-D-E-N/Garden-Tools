#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace condition_test {

inline std::filesystem::path tempRoot() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / ("fo4ide_cond_backend_" + std::to_string(id));
}

struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

inline void writeBytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    REQUIRE(stream.good());
}

inline std::vector<std::byte> makeZString(std::string_view text) {
    std::vector<std::byte> out;
    out.reserve(text.size() + 1);
    for (const char ch : text) {
        out.push_back(std::byte{static_cast<std::uint8_t>(ch)});
    }
    out.push_back(std::byte{0});
    return out;
}

inline std::vector<std::byte> makeCtda(std::uint8_t flagsByte, std::uint32_t comparison,
                                       std::uint16_t function, std::uint16_t unknown2,
                                       std::int32_t param1, std::int32_t param2,
                                       std::uint32_t runOn, std::uint32_t reference,
                                       std::int32_t unknown3) {
    fo4::test::ByteWriter ctda;
    ctda.u8(flagsByte);
    ctda.u8(0);
    ctda.u8(0);
    ctda.u8(0);
    ctda.u32(comparison);
    ctda.u16(function);
    ctda.u16(unknown2);
    ctda.u32(static_cast<std::uint32_t>(param1));
    ctda.u32(static_cast<std::uint32_t>(param2));
    ctda.u32(runOn);
    ctda.u32(reference);
    ctda.u32(static_cast<std::uint32_t>(unknown3));
    return ctda.bytes;
}

inline nlohmann::json pluginEntry(std::string name,
                                  const std::filesystem::path& path,
                                  std::string source) {
    return {
        {"name", std::move(name)},
        {"path", path.string()},
        {"source", std::move(source)},
        {"resolved", true}
    };
}

inline nlohmann::json parseStringResult(const fo4ide::Result<nlohmann::json>& result) {
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    return nlohmann::json::parse(result->get<std::string>());
}

}  // namespace condition_test
