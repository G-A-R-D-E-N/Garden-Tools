#pragma once

#include "fo4/nif/nif_import.hpp"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::nif::gltf_detail {

struct Document {
  nlohmann::json json;
  std::vector<std::vector<std::byte>> buffers;
};

// Requires a .glb whose JSON chunk comes first, or glTF JSON whose buffers are data: URIs or readable through load_uri.
Document open_document(std::span<const std::byte> bytes, const GltfUriLoader& load_uri);
// Requires the accessor to hold the named glTF type; a sparse accessor is refused by name.
std::vector<double> read_accessor(const Document& doc, std::size_t accessor, std::string_view type);
std::string accessor_type(const Document& doc, std::size_t accessor);
// Requires index below the array's length; the message names the array and the index.
const nlohmann::json& element(const nlohmann::json& json, std::string_view array, std::size_t index);
// Returns the URI with its %XX escapes decoded.
std::string decode_uri(std::string_view uri);

}
