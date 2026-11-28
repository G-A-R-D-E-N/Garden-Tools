#pragma once

#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include <fo4/plugin/reader.hpp>

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

namespace fo4ide::plugin::cell_args {

inline std::int64_t integerValue(const nlohmann::json& value, const std::string& name,
                                 std::int64_t minimum, std::int64_t maximum) {
    if (!value.is_number_integer()) throw std::invalid_argument(name + " must be an integer");
    const auto number = value.get<std::int64_t>();
    if (number < minimum || number > maximum) throw std::invalid_argument(name + " is out of range");
    return number;
}

inline float floatValue(const nlohmann::json& value, const std::string& name) {
    if (!value.is_number()) throw std::invalid_argument(name + " must be a number");
    return value.get<float>();
}

inline std::optional<fo4::FormKey> linkValue(const nlohmann::json& value, const std::string& name) {
    if (value.is_null()) return std::nullopt;
    if (!value.is_string()) throw std::invalid_argument(name + " must be a FormKey or null");
    return fo4::FormKey::parse(value.get<std::string>());
}

}  // namespace fo4ide::plugin::cell_args
