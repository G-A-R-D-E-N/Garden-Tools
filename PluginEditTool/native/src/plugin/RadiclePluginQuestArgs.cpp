#include "RadiclePluginQuestInternal.hpp"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace fo4ide::plugin::questinternal {

namespace {

bool parseEntireUnsigned32(std::string_view text, std::uint32_t& value) {
    if (text.empty()) return false;
    if (text.front() == '+' || text.front() == '-') return false;
    int base = 10;
    auto view = text;
    if (view.size() >= 2 && view[0] == '0' && (view[1] == 'x' || view[1] == 'X')) {
        base = 16;
        view.remove_prefix(2);
        if (view.empty()) return false;
    }
    std::uint64_t parsed = 0;
    const auto [ptr, error] = std::from_chars(
        view.data(), view.data() + view.size(), parsed, base);
    if (error != std::errc{} || ptr != view.data() + view.size()) return false;
    if (parsed > std::numeric_limits<std::uint32_t>::max()) return false;
    value = static_cast<std::uint32_t>(parsed);
    return true;
}

}

std::string questStringArg(const nlohmann::json& args, std::size_t index) {
    if (!args.is_array() || index >= args.size()) return {};
    if (args[index].is_string()) return args[index].get<std::string>();
    return {};
}

const nlohmann::json* questArrayArg(const nlohmann::json& args, std::size_t index) {
    if (!args.is_array() || index >= args.size()) return nullptr;
    if (args[index].is_array()) return &args[index];
    if (args[index].is_string()) {
        static thread_local nlohmann::json parsed;
        try {
            parsed = nlohmann::json::parse(args[index].get<std::string>());
        } catch (const std::exception&) {
            return nullptr;
        }
        if (!parsed.is_array()) return nullptr;
        return &parsed;
    }
    return nullptr;
}

bool questUnsigned(const nlohmann::json& object,
                   const char* key,
                   std::uint32_t defaultValue,
                   std::uint32_t& value) {
    const auto found = object.find(key);
    if (found == object.end() || found->is_null()) {
        value = defaultValue;
        return true;
    }
    if (found->is_number_unsigned()) {
        const auto parsed = found->get<std::uint64_t>();
        if (parsed > std::numeric_limits<std::uint32_t>::max()) return false;
        value = static_cast<std::uint32_t>(parsed);
        return true;
    }
    if (found->is_number_integer()) {
        const auto parsed = found->get<std::int64_t>();
        if (parsed < 0 ||
            parsed > static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max())) {
            return false;
        }
        value = static_cast<std::uint32_t>(parsed);
        return true;
    }
    if (found->is_string()) {
        return parseEntireUnsigned32(found->get<std::string>(), value);
    }
    return false;
}

bool questInt32(const nlohmann::json& object, const char* key, std::optional<std::int32_t>& value) {
    const auto found = object.find(key);
    if (found == object.end() || found->is_null()) {
        value = std::nullopt;
        return true;
    }
    if (found->is_number_integer()) {
        const auto parsed = found->get<std::int64_t>();
        if (parsed < std::numeric_limits<std::int32_t>::min() ||
            parsed > std::numeric_limits<std::int32_t>::max()) {
            return false;
        }
        value = static_cast<std::int32_t>(parsed);
        return true;
    }
    return false;
}

bool questFloat(const nlohmann::json& object,
                const char* key,
                float defaultValue,
                float& value) {
    const auto found = object.find(key);
    if (found == object.end() || found->is_null()) {
        value = defaultValue;
        return true;
    }
    if (!found->is_number()) return false;
    const auto parsed = found->get<double>();
    if (!std::isfinite(parsed)) return false;
    const auto converted = static_cast<float>(parsed);
    if (!std::isfinite(converted)) return false;
    value = converted;
    return true;
}

bool questU8(const nlohmann::json& object, const char* key, std::optional<std::uint8_t>& value) {
    const auto found = object.find(key);
    if (found == object.end() || found->is_null()) {
        value = std::nullopt;
        return true;
    }
    if (found->is_number_unsigned()) {
        const auto parsed = found->get<std::uint64_t>();
        if (parsed > 255) return false;
        value = static_cast<std::uint8_t>(parsed);
        return true;
    }
    if (found->is_number_integer()) {
        const auto parsed = found->get<std::int64_t>();
        if (parsed < 0 || parsed > 255) return false;
        value = static_cast<std::uint8_t>(parsed);
        return true;
    }
    return false;
}

bool questOptionalString(const nlohmann::json& object,
                         const char* key,
                         std::optional<std::string>& value) {
    const auto found = object.find(key);
    if (found == object.end() || found->is_null()) {
        value = std::nullopt;
        return true;
    }
    if (!found->is_string()) return false;
    value = found->get<std::string>();
    return true;
}

std::optional<Error> rejectUnknownKeys(const nlohmann::json& object,
                                       bool (*known)(std::string_view),
                                       std::string_view label,
                                       std::string_view errorType) {
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (!known(it.key())) {
            return Error{
                std::string(errorType),
                "Unsupported " + std::string(label) + " field '" + it.key() + "'"
            };
        }
    }
    return std::nullopt;
}

bool jsonObjectPresent(const nlohmann::json& object, const char* key) {
    return object.contains(key) && !object[key].is_null();
}

}
