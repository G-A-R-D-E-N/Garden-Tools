#include "gltf_internal.hpp"

#include "fo4/archive/assets.hpp"
#include "fo4/texture/dds.hpp"
#include "fo4/texture/png.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::nif::gltf {
namespace {

constexpr std::string_view k_dds_mime = "image/vnd-ms.dds";
constexpr std::string_view k_dds_extension = ".dds";
// Invariant: NIF and BGSM texture paths are relative to the Data textures folder and may omit its prefix.
constexpr std::string_view k_textures_root = "textures/";

std::string percent_encode(std::string_view path) {
  constexpr std::string_view hex = "0123456789ABCDEF";
  std::string out;
  for (const char ch : path) {
    const auto byte = static_cast<unsigned char>(ch);
    const bool plain = (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9') ||
                       ch == '-' || ch == '.' || ch == '_' || ch == '~' || ch == '/';
    if (plain) {
      out.push_back(ch);
      continue;
    }
    out.push_back('%');
    out.push_back(hex[byte >> 4U]);
    out.push_back(hex[byte & 0xFU]);
  }
  return out;
}

std::span<const std::uint8_t> as_octets(const std::vector<std::byte>& bytes) {
  return {reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()};
}

std::vector<std::byte> to_bytes(const std::vector<std::uint8_t>& octets) {
  std::vector<std::byte> out;
  out.reserve(octets.size());
  for (const auto octet : octets) out.push_back(static_cast<std::byte>(octet));
  return out;
}

}  // namespace

std::optional<std::size_t> TextureTable::add(std::string_view raw, bool normal) {
  if (raw.empty()) return std::nullopt;
  std::string path;
  try {
    path = archive::normalize_asset_path(raw);
    if (!path.starts_with(k_textures_root)) path.insert(0, k_textures_root);
  } catch (const archive::AssetPathError& error) {
    warn("texture path skipped: " + std::string(raw) + " (" + error.what() + ")");
    return std::nullopt;
  }
  if (const auto found = index_.find(path); found != index_.end()) return found->second;
  const auto index = build(std::string(raw), path, normal);
  index_.emplace(path, index);
  return index;
}

std::optional<std::size_t> TextureTable::build(const std::string& raw, const std::string& path, bool normal) {
  nlohmann::json image;
  image["name"] = raw;
  image["extras"] = {{"fo4Path", raw}};
  if (options_.textures == GltfTextureMode::uri) {
    image["uri"] = percent_encode(path);
  } else {
    if (options_.assets == nullptr) {
      warn("texture not exported, no asset resolver: " + path);
      return std::nullopt;
    }
    const auto location = options_.assets->locate(path);
    if (!location) {
      warn("texture not found: " + path);
      return std::nullopt;
    }
    auto bytes = options_.assets->read(*location);
    if (options_.textures == GltfTextureMode::embed) {
      if (!path.ends_with(k_dds_extension)) {
        warn("texture not embedded, only DDS is embedded: " + path);
        return std::nullopt;
      }
      image["bufferView"] = bin_.add_view(bytes);
      image["mimeType"] = std::string(k_dds_mime);
    } else {
      std::string file = path;
      if (options_.textures == GltfTextureMode::png && path.ends_with(k_dds_extension)) {
        try {
          std::uint32_t width = 0;
          std::uint32_t height = 0;
          const auto rgba = texture::decode_dds_rgba(as_octets(bytes), 0, normal && texture::is_bc5(as_octets(bytes)),
                                                     width, height);
          bytes = to_bytes(texture::encode_png_rgba(rgba, width, height));
          file = path.substr(0, path.size() - k_dds_extension.size()) + ".png";
        } catch (const std::exception& error) {
          warn("texture kept as DDS, PNG conversion failed: " + path + " (" + error.what() + ")");
        }
      }
      file = claim(std::move(file));
      image["uri"] = percent_encode(file);
      out_.files.push_back({file, std::move(bytes)});
    }
  }
  images_.push_back(std::move(image));
  textures_.push_back({{"source", images_.size() - 1}});
  return textures_.size() - 1;
}

std::string TextureTable::claim(std::string name) {
  const auto slash = name.rfind('/');
  const auto dot = name.rfind('.');
  const bool has_extension = dot != std::string::npos && (slash == std::string::npos || dot > slash);
  const std::string stem = has_extension ? name.substr(0, dot) : name;
  const std::string extension = has_extension ? name.substr(dot) : std::string{};
  std::string candidate = name;
  for (std::size_t n = 2; !claimed_.insert(candidate).second; ++n) {
    candidate = stem + "_" + std::to_string(n) + extension;
  }
  return candidate;
}

void TextureTable::write(nlohmann::json& document) const {
  if (images_.empty()) return;
  document["images"] = images_;
  document["textures"] = textures_;
}

}  // namespace fo4::nif::gltf
