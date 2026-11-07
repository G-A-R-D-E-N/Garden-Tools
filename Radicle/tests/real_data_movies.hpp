#pragma once

#include <cctype>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

inline std::string lower_extension(const std::string& path) {
  std::string extension = std::filesystem::path(path).extension().string();
  for (auto& ch : extension) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return extension;
}

inline bool is_movie(const std::string& path) {
  const auto extension = lower_extension(path);
  return extension == ".swf" || extension == ".gfx";
}

inline std::vector<std::byte> read_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::vector<std::byte> bytes;
  char buffer[4096];
  while (in.read(buffer, static_cast<std::streamsize>(sizeof(buffer))) || in.gcount() > 0) {
    const auto count = in.gcount();
    for (std::streamsize index = 0; index < count; ++index) {
      bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(buffer[index])));
    }
  }
  return bytes;
}
