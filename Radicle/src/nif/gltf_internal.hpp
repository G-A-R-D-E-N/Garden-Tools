#pragma once

#include "fo4/nif/gltf_export.hpp"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fo4::nif::gltf {

class BinaryChunk {
public:
  std::size_t add_view(const std::vector<std::byte>& data);
  std::size_t add_accessor(const std::vector<std::byte>& data, std::uint32_t component, std::size_t count,
                           std::string_view type, nlohmann::json bounds = nlohmann::json::object());
  void finish(nlohmann::json& document);
  const std::vector<std::byte>& bytes() const noexcept { return bytes_; }

private:
  std::vector<std::byte> bytes_;
  nlohmann::json views_ = nlohmann::json::array();
  nlohmann::json accessors_ = nlohmann::json::array();
};

class TextureTable {
public:
  TextureTable(const GltfOptions& options, BinaryChunk& bin, GltfExport& out) : options_(options), bin_(bin), out_(out) {}

  std::optional<std::size_t> add(std::string_view path, bool normal);
  void write(nlohmann::json& document) const;

private:
  std::optional<std::size_t> build(const std::string& raw, const std::string& path, bool normal);
  std::string claim(std::string name);
  void warn(std::string message) { out_.warnings.push_back(std::move(message)); }

  const GltfOptions& options_;
  BinaryChunk& bin_;
  GltfExport& out_;
  std::unordered_map<std::string, std::optional<std::size_t>> index_;
  std::unordered_set<std::string> claimed_;
  nlohmann::json images_ = nlohmann::json::array();
  nlohmann::json textures_ = nlohmann::json::array();
};

nlohmann::json shape_material(const NifFile& nif, const Shape& shape, const GltfOptions& options, TextureTable& textures,
                              GltfExport& out);

}  // namespace fo4::nif::gltf
