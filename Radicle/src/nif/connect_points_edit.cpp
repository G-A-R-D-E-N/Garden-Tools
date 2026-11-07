#include "fo4/nif/connect_points.hpp"

#include "connect_points_internal.hpp"
#include "fo4/error.hpp"
#include "fo4/nif/nif.hpp"
#include "write_model.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <initializer_list>
#include <limits>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace fo4::nif {
namespace {

nlohmann::json parse_ops(std::string_view text) {
  nlohmann::json parsed;
  try {
    parsed = nlohmann::json::parse(text.begin(), text.end());
  } catch (const std::exception& error) {
    throw parse_error(0, std::string{"connect point ops are not valid JSON: "} + error.what());
  }
  if (!parsed.is_array()) throw parse_error(0, "connect point ops must be a JSON array");
  return parsed;
}

std::uint32_t block_of(const nlohmann::json& op) {
  if (!op.is_object() || !op.contains("block") || !op.at("block").is_number_integer()) {
    throw parse_error(0, "connect point op needs an integer block");
  }
  const auto block = op.at("block").get<std::int64_t>();
  if (block < 0 || block > static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max())) {
    throw parse_error(0, "connect point op block " + std::to_string(block) + " is out of range");
  }
  return static_cast<std::uint32_t>(block);
}

std::string text_of(const nlohmann::json& op, std::string_view key) {
  if (!op.contains(key) || !op.at(key).is_string()) {
    throw parse_error(0, "connect point op needs a text '" + std::string(key) + "'");
  }
  return op.at(key).get<std::string>();
}

const nlohmann::json& value_of(const nlohmann::json& op) {
  if (!op.contains("value")) throw parse_error(0, "connect point op needs a value");
  return op.at("value");
}

std::string text_value(const nlohmann::json& raw, std::string_view field) {
  if (!raw.is_string()) throw parse_error(0, "connect point field '" + std::string(field) + "' needs text");
  return raw.get<std::string>();
}

float finite_float(const nlohmann::json& raw, std::string_view field) {
  if (!raw.is_number()) throw parse_error(0, "connect point field '" + std::string(field) + "' needs a number");
  const auto value = raw.get<double>();
  // Invariant: the range check runs before the narrowing cast, which is undefined for a double past FLT_MAX.
  if (!std::isfinite(value) || std::fabs(value) > static_cast<double>(std::numeric_limits<float>::max())) {
    throw parse_error(0, "connect point field '" + std::string(field) + "' must be a finite 32-bit float");
  }
  return static_cast<float>(value);
}

void float_array(const nlohmann::json& raw, std::string_view field, float* out, std::size_t count) {
  if (!raw.is_array() || raw.size() != count) {
    throw parse_error(0, "connect point field '" + std::string(field) + "' needs " + std::to_string(count) + " numbers");
  }
  for (std::size_t i = 0; i < count; ++i) out[i] = finite_float(raw[i], field);
}

void only_keys(const nlohmann::json& raw, std::initializer_list<std::string_view> allowed) {
  for (const auto& item : raw.items()) {
    if (std::find(allowed.begin(), allowed.end(), item.key()) == allowed.end()) {
      throw parse_error(0, "connect point value has no field '" + item.key() + "'");
    }
  }
}

ConnectPoint point_value(const nlohmann::json& raw) {
  if (!raw.is_object()) throw parse_error(0, "connect point add needs an object value");
  only_keys(raw, {"parent", "name", "rotation", "translation", "scale"});
  ConnectPoint point;
  if (raw.contains("parent")) point.parent = text_value(raw.at("parent"), "parent");
  if (raw.contains("name")) point.name = text_value(raw.at("name"), "name");
  if (raw.contains("rotation")) float_array(raw.at("rotation"), "rotation", point.rotation, 4);
  if (raw.contains("translation")) float_array(raw.at("translation"), "translation", point.translation, 3);
  if (raw.contains("scale")) point.scale = finite_float(raw.at("scale"), "scale");
  return point;
}

// Invariant: an index is checked against the list length before it reaches vector::insert or erase.
std::size_t index_of(const nlohmann::json& op, std::size_t limit, std::uint32_t block) {
  if (!op.contains("index") || !op.at("index").is_number_integer()) {
    throw parse_error(0, "connect point op needs an integer index");
  }
  const auto raw = op.at("index").get<std::int64_t>();
  if (raw < 0 || static_cast<std::uint64_t>(raw) >= limit) {
    throw parse_error(0, "connect point index " + std::to_string(raw) + " is out of range for block " +
                             std::to_string(block));
  }
  return static_cast<std::size_t>(raw);
}

std::size_t insert_index_of(const nlohmann::json& op, std::size_t size, std::uint32_t block) {
  return op.contains("index") ? index_of(op, size + 1, block) : size;
}

bool stored_in_block(const NifFile& nif, std::uint32_t block, std::string_view parent) {
  const auto stored = read_connect_parents(nif, block);
  return std::any_of(stored.points.begin(), stored.points.end(),
                     [&](const ConnectPoint& point) { return point.parent == parent; });
}

void apply_parents_op(const NifFile& nif, std::uint32_t block, ConnectParents& value, const nlohmann::json& op) {
  const auto action = text_of(op, "op");
  if (action == "add") {
    auto point = point_value(value_of(op));
    connect_detail::require_parent_node(nif, block, point.parent);
    const auto index = insert_index_of(op, value.points.size(), block);
    value.points.insert(value.points.begin() + static_cast<std::ptrdiff_t>(index), std::move(point));
    return;
  }
  if (action == "remove") {
    const auto index = index_of(op, value.points.size(), block);
    value.points.erase(value.points.begin() + static_cast<std::ptrdiff_t>(index));
    return;
  }
  if (action != "set") throw parse_error(0, "connect point op '" + action + "' is not add, remove or set");
  auto& point = value.points[index_of(op, value.points.size(), block)];
  const auto field = text_of(op, "field");
  const auto& raw = value_of(op);
  if (field == "parent") {
    auto parent = text_value(raw, field);
    if (!stored_in_block(nif, block, parent)) connect_detail::require_parent_node(nif, block, parent);
    point.parent = std::move(parent);
  } else if (field == "name") {
    point.name = text_value(raw, field);
  } else if (field == "rotation") {
    float_array(raw, field, point.rotation, 4);
  } else if (field == "translation") {
    float_array(raw, field, point.translation, 3);
  } else if (field == "scale") {
    point.scale = finite_float(raw, field);
  } else {
    throw parse_error(0, "connect point field '" + field + "' is not editable");
  }
}

void apply_children_op(std::uint32_t block, ConnectChildren& value, const nlohmann::json& op) {
  const auto action = text_of(op, "op");
  if (action == "add") {
    const auto index = insert_index_of(op, value.names.size(), block);
    value.names.insert(value.names.begin() + static_cast<std::ptrdiff_t>(index), text_value(value_of(op), "name"));
    return;
  }
  if (action == "remove") {
    const auto index = index_of(op, value.names.size(), block);
    value.names.erase(value.names.begin() + static_cast<std::ptrdiff_t>(index));
    return;
  }
  if (action != "set") throw parse_error(0, "connect point op '" + action + "' is not add, remove or set");
  const auto field = text_of(op, "field");
  if (field == "skinned") {
    const auto& raw = value_of(op);
    if (!raw.is_boolean()) throw parse_error(0, "connect point field 'skinned' needs true or false");
    value.skinned = raw.get<bool>();
  } else if (field == "name") {
    value.names[index_of(op, value.names.size(), block)] = text_value(value_of(op), field);
  } else {
    throw parse_error(0, "connect point field '" + field + "' is not editable");
  }
}

}  // namespace

std::vector<std::byte> apply_connect_point_edits_json(const NifFile& nif, std::string_view text) {
  std::map<std::uint32_t, ConnectParents> parents;
  std::map<std::uint32_t, ConnectChildren> children;
  for (const auto& op : parse_ops(text)) {
    const auto block = block_of(op);
    if (block >= nif.blocks().size()) {
      throw parse_error(0, "connect point op targets block " + std::to_string(block) + ", which is not in the file");
    }
    const auto& info = nif.blocks()[block];
    if (info.type == k_connect_parents_type) {
      auto found = parents.find(block);
      if (found == parents.end()) found = parents.emplace(block, read_connect_parents(nif, block)).first;
      apply_parents_op(nif, block, found->second, op);
    } else if (info.type == k_connect_children_type) {
      auto found = children.find(block);
      if (found == children.end()) found = children.emplace(block, read_connect_children(nif, block)).first;
      apply_children_op(block, found->second, op);
    } else {
      throw parse_error(info.offset, "block " + std::to_string(block) + " is " + info.type +
                                         ", not a connect point block");
    }
  }
  auto model = write_detail::load_model(nif);
  for (const auto& [block, value] : parents) model.blocks[block] = connect_detail::encode_parents(nif, block, value);
  for (const auto& [block, value] : children) model.blocks[block] = connect_detail::encode_children(nif, block, value);
  auto bytes = write_detail::serialize(model);
  read_nif(bytes);
  return bytes;
}

}  // namespace fo4::nif
