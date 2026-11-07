#pragma once

#include "fo4/io/path_text.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace radicle_parity {

inline std::string fnv(std::span<const std::byte> data, bool present) {
  if (!present) return "-";
  std::uint64_t h = 0xCBF29CE484222325ull;
  for (const auto b : data) h = (h ^ std::to_integer<std::uint8_t>(b)) * 0x100000001B3ull;
  return std::format("{:016X}", h);
}

inline std::string dds_format(std::span<const std::byte> dds) {
  const std::string fourcc(reinterpret_cast<const char*>(dds.data()) + 84, 4);
  if (fourcc == "DXT1") return "bc1";
  if (fourcc == "DXT3") return "bc2";
  if (fourcc == "DXT5") return "bc3";
  if (fourcc == "ATI1" || fourcc == "BC4U") return "bc4u";
  if (fourcc == "ATI2" || fourcc == "BC5U") return "bc5u";
  if (fourcc == "BC4S") return "bc4s";
  if (fourcc == "BC5S") return "bc5s";
  if (fourcc != "DX10" || dds.size() < 148) return "fourcc:" + fourcc;
  std::uint32_t dxgi = 0;
  std::memcpy(&dxgi, dds.data() + 128, 4);
  if (dxgi >= 70 && dxgi <= 72) return "bc1";
  if (dxgi >= 73 && dxgi <= 75) return "bc2";
  if (dxgi >= 76 && dxgi <= 78) return "bc3";
  if (dxgi == 79 || dxgi == 80) return "bc4u";
  if (dxgi == 81) return "bc4s";
  if (dxgi == 82 || dxgi == 83) return "bc5u";
  if (dxgi == 84) return "bc5s";
  return std::format("dxgi:{}", dxgi);
}

inline std::string dds_payload(std::span<const std::byte> dds) {
  if (dds.size() < 128 || std::memcmp(dds.data(), "DDS ", 4) != 0) return "-";
  const std::size_t header = std::memcmp(dds.data() + 84, "DX10", 4) == 0 ? 148 : 128;
  if (dds.size() < header) return "-";
  return std::format("{}/{}:{}", dds_format(dds), dds.size() - header, fnv(dds.subspan(header), true));
}

inline std::filesystem::path utf8_path(std::string_view text) { return std::u8string(text.begin(), text.end()); }

inline std::vector<std::filesystem::path> read_order(const std::filesystem::path& file) {
  std::vector<std::filesystem::path> paths;
  std::ifstream in(file);
  if (!in) throw std::runtime_error("cannot open load-order input");
  for (std::string line; std::getline(in, line);) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) continue;
    const auto tab = line.find('\t');
    if (tab == std::string::npos || tab + 1 == line.size()) throw std::runtime_error("load-order row requires a tab and plugin path");
    paths.push_back(utf8_path(std::string_view(line).substr(tab + 1)));
  }
  return paths;
}

using Args = std::vector<std::filesystem::path>;

template <class Char>
int guarded_main(int argc, Char** argv, int (*run)(const Args&)) {
  try {
    return run(Args(argv, argv + argc));
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
  }
}

}  // namespace radicle_parity
