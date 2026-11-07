#include "gltf_read.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::nif::gltf_detail {
namespace {

constexpr std::uint32_t k_glb_magic = 0x46546C67U;
constexpr std::uint32_t k_glb_version = 2;
constexpr std::uint32_t k_json_chunk = 0x4E4F534AU;
constexpr std::uint32_t k_bin_chunk = 0x004E4942U;
constexpr std::uint32_t k_i8 = 5120;
constexpr std::uint32_t k_u8 = 5121;
constexpr std::uint32_t k_i16 = 5122;
constexpr std::uint32_t k_u16 = 5123;
constexpr std::uint32_t k_u32 = 5125;
constexpr std::uint32_t k_f32 = 5126;

std::uint32_t read_u32(std::span<const std::byte> bytes, std::size_t at) {
  if (at + 4 > bytes.size()) throw std::runtime_error("glTF binary container is truncated");
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) value |= std::to_integer<std::uint32_t>(bytes[at + i]) << (8U * i);
  return value;
}

int hex_value(char ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
  if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
  return -1;
}

int base64_value(char ch) {
  if (ch >= 'A' && ch <= 'Z') return ch - 'A';
  if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
  if (ch >= '0' && ch <= '9') return ch - '0' + 52;
  if (ch == '+') return 62;
  if (ch == '/') return 63;
  return -1;
}

std::vector<std::byte> decode_base64(std::string_view text) {
  std::vector<std::byte> out;
  std::uint32_t buffer = 0;
  int bits = 0;
  for (const char ch : text) {
    if (ch == '=') break;
    if (ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t') continue;
    const int value = base64_value(ch);
    if (value < 0) throw std::runtime_error("glTF data URI is not valid base64");
    buffer = (buffer << 6U) | static_cast<std::uint32_t>(value);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(static_cast<std::byte>((buffer >> static_cast<std::uint32_t>(bits)) & 0xFFU));
    }
  }
  return out;
}

std::vector<std::byte> load_buffer(std::string_view uri, const GltfUriLoader& load_uri) {
  if (uri.starts_with("data:")) {
    const auto comma = uri.find(',');
    if (comma == std::string_view::npos || uri.substr(0, comma).find(";base64") == std::string_view::npos) {
      throw std::runtime_error("glTF data URI must be base64");
    }
    return decode_base64(uri.substr(comma + 1));
  }
  if (!load_uri) {
    throw std::runtime_error("glTF buffer '" + std::string(uri) +
                             "' is an external file; import the .gltf from its own path so it can be read");
  }
  return load_uri(decode_uri(uri));
}

std::size_t components_of(std::string_view type) {
  if (type == "SCALAR") return 1;
  if (type == "VEC2") return 2;
  if (type == "VEC3") return 3;
  if (type == "VEC4") return 4;
  if (type == "MAT4") return 16;
  throw std::runtime_error("glTF accessor type " + std::string(type) + " is not supported");
}

std::size_t width_of(std::uint32_t component) {
  switch (component) {
    case k_i8:
    case k_u8:
      return 1;
    case k_i16:
    case k_u16:
      return 2;
    case k_u32:
    case k_f32:
      return 4;
    default:
      throw std::runtime_error("glTF component type " + std::to_string(component) + " is not supported");
  }
}

double component_value(const std::vector<std::byte>& buffer, std::size_t at, std::uint32_t component, bool normalized) {
  const auto width = width_of(component);
  std::uint32_t bits = 0;
  for (std::size_t i = 0; i < width; ++i) bits |= std::to_integer<std::uint32_t>(buffer[at + i]) << (8U * i);
  switch (component) {
    case k_f32:
      return std::bit_cast<float>(bits);
    case k_u8:
      return normalized ? bits / 255.0 : static_cast<double>(bits);
    case k_u16:
      return normalized ? bits / 65535.0 : static_cast<double>(bits);
    case k_u32:
      return normalized ? bits / 4294967295.0 : static_cast<double>(bits);
    case k_i8: {
      const auto value = static_cast<std::int8_t>(bits & 0xFFU);
      return normalized ? std::max(value / 127.0, -1.0) : static_cast<double>(value);
    }
    case k_i16: {
      const auto value = static_cast<std::int16_t>(bits & 0xFFFFU);
      return normalized ? std::max(value / 32767.0, -1.0) : static_cast<double>(value);
    }
    default:
      throw std::runtime_error("glTF component type " + std::to_string(component) + " is not supported");
  }
}

}

Document open_document(std::span<const std::byte> bytes, const GltfUriLoader& load_uri) {
  Document doc;
  std::vector<std::byte> bin;
  bool has_bin = false;
  if (bytes.size() >= 4 && read_u32(bytes, 0) == k_glb_magic) {
    if (bytes.size() < 20 || read_u32(bytes, 4) != k_glb_version || read_u32(bytes, 8) != bytes.size() ||
        read_u32(bytes, 16) != k_json_chunk) {
      throw std::runtime_error("glTF binary must be version 2 with a JSON chunk first and a matching length");
    }
    const auto json_length = static_cast<std::size_t>(read_u32(bytes, 12));
    if (20 + json_length > bytes.size()) throw std::runtime_error("glTF JSON chunk runs past the end of the file");
    const auto text = bytes.subspan(20, json_length);
    doc.json = nlohmann::json::parse(std::string_view(reinterpret_cast<const char*>(text.data()), text.size()));
    const std::size_t at = 20 + json_length;
    if (at < bytes.size()) {
      const auto length = static_cast<std::size_t>(read_u32(bytes, at));
      if (read_u32(bytes, at + 4) != k_bin_chunk || at + 8 + length > bytes.size()) {
        throw std::runtime_error("glTF binary's second chunk is not a complete BIN chunk");
      }
      const auto chunk = bytes.subspan(at + 8, length);
      bin.assign(chunk.begin(), chunk.end());
      has_bin = true;
    }
  } else {
    doc.json = nlohmann::json::parse(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
  }
  const auto buffers = doc.json.value("buffers", nlohmann::json::array());
  for (std::size_t i = 0; i < buffers.size(); ++i) {
    if (buffers[i].contains("uri")) {
      doc.buffers.push_back(load_buffer(buffers[i].at("uri").get<std::string>(), load_uri));
    } else if (i == 0 && has_bin) {
      doc.buffers.push_back(std::move(bin));
    } else {
      throw std::runtime_error("glTF buffer " + std::to_string(i) + " has no URI and is not the GLB binary chunk");
    }
  }
  return doc;
}

const nlohmann::json& element(const nlohmann::json& json, std::string_view array, std::size_t index) {
  const auto key = std::string(array);
  const auto found = json.find(key);
  if (found == json.end() || !found->is_array() || index >= found->size()) {
    throw std::runtime_error("glTF " + key + " index " + std::to_string(index) + " does not exist");
  }
  return (*found)[index];
}

std::string accessor_type(const Document& doc, std::size_t accessor) {
  return element(doc.json, "accessors", accessor).at("type").get<std::string>();
}

std::vector<double> read_accessor(const Document& doc, std::size_t index, std::string_view type) {
  const auto& accessor = element(doc.json, "accessors", index);
  const auto label = "glTF accessor " + std::to_string(index);
  if (accessor.contains("sparse")) throw std::runtime_error(label + " uses sparse storage, which is not supported");
  const auto actual = accessor.at("type").get<std::string>();
  if (actual != type) throw std::runtime_error(label + " is " + actual + ", expected " + std::string(type));
  const auto components = components_of(actual);
  const auto component = accessor.at("componentType").get<std::uint32_t>();
  const auto width = width_of(component);
  const auto count = accessor.at("count").get<std::size_t>();
  if (count == 0) return {};
  if (!accessor.contains("bufferView")) return std::vector<double>(count * components, 0.0);

  const auto& view = element(doc.json, "bufferViews", accessor.at("bufferView").get<std::size_t>());
  const auto buffer_index = view.at("buffer").get<std::size_t>();
  if (buffer_index >= doc.buffers.size()) throw std::runtime_error(label + " reads a buffer that does not exist");
  const auto& buffer = doc.buffers[buffer_index];
  const std::size_t element_bytes = components * width;
  const auto stride = view.value("byteStride", element_bytes);
  if (stride < element_bytes) throw std::runtime_error(label + " has a byte stride smaller than its element");
  const auto base = view.value("byteOffset", std::size_t{0}) + accessor.value("byteOffset", std::size_t{0});
  if (base > buffer.size() || buffer.size() - base < element_bytes ||
      (count - 1) > (buffer.size() - base - element_bytes) / stride) {
    throw std::runtime_error(label + " runs past the end of its buffer");
  }
  std::vector<double> values(count * components, 0.0);
  const bool normalized = accessor.value("normalized", false);
  for (std::size_t i = 0; i < count; ++i) {
    for (std::size_t c = 0; c < components; ++c) {
      values[i * components + c] = component_value(buffer, base + i * stride + c * width, component, normalized);
    }
  }
  return values;
}

std::string decode_uri(std::string_view uri) {
  std::string out;
  for (std::size_t i = 0; i < uri.size(); ++i) {
    if (uri[i] == '%' && i + 2 < uri.size() && hex_value(uri[i + 1]) >= 0 && hex_value(uri[i + 2]) >= 0) {
      out.push_back(static_cast<char>(hex_value(uri[i + 1]) * 16 + hex_value(uri[i + 2])));
      i += 2;
    } else {
      out.push_back(uri[i]);
    }
  }
  return out;
}

}
