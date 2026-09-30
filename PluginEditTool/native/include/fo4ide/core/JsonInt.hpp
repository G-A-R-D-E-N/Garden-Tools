#pragma once

#include <cstdint>
#include <limits>
#include <optional>

#include <nlohmann/json.hpp>

namespace fo4ide::core {

inline std::optional<int> jsonInt(const nlohmann::json& value) {
    if (!value.is_number_integer()) return std::nullopt;
    if (value.is_number_unsigned()) {
        const auto number = value.get<std::uint64_t>();
        if (number > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) return std::nullopt;
        return static_cast<int>(number);
    }
    const auto number = value.get<std::int64_t>();
    if (number < std::numeric_limits<int>::min() || number > std::numeric_limits<int>::max()) {
        return std::nullopt;
    }
    return static_cast<int>(number);
}

}
