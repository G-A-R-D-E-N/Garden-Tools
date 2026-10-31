#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include <fo4/c_api.h>
#include <nlohmann/json.hpp>

namespace fo4ide::app::swftarget {

inline const char* statusName(radicle_status status) {
    // Constraint: the names must match Status.ToString, because the panel reads the status field.
    switch (status) {
        case RADICLE_STATUS_OK: return "Ok";
        case RADICLE_STATUS_INVALID_ARGUMENT: return "InvalidArgument";
        case RADICLE_STATUS_IO_ERROR: return "IoError";
        case RADICLE_STATUS_PARSE_ERROR: return "ParseError";
        case RADICLE_STATUS_NOT_FOUND: return "NotFound";
        case RADICLE_STATUS_OUT_OF_MEMORY: return "OutOfMemory";
        case RADICLE_STATUS_INTERNAL_ERROR: return "InternalError";
    }
    return "InternalError";
}

inline nlohmann::json errorText(const std::string& message) {
    return nlohmann::json(nlohmann::json{{"error", message}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
}

inline nlohmann::json radicleError(radicle_status status) {
    const char* detail = radicle_last_error();
    nlohmann::json body{{"error", detail ? detail : "unknown Radicle error"}};
    if (status != RADICLE_STATUS_OK) body["status"] = statusName(status);
    return nlohmann::json(body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
}

inline std::string takeError() {
    const char* detail = radicle_last_error();
    return detail ? detail : "";
}

inline nlohmann::json errorStatus(radicle_status status, const std::string& detail) {
    nlohmann::json body{{"error", detail.empty() ? "unknown Radicle error" : detail}};
    if (status != RADICLE_STATUS_OK) body["status"] = statusName(status);
    return nlohmann::json(body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
}

struct BytesFree {
    void operator()(std::uint8_t* value) const noexcept {
        if (value != nullptr) radicle_free_bytes(value);
    }
};

using BytesPtr = std::unique_ptr<std::uint8_t, BytesFree>;

inline std::string takeText(std::uint8_t* bytes, std::size_t size) {
    BytesPtr holder(bytes);
    if (bytes == nullptr) return {};
    return std::string(reinterpret_cast<const char*>(bytes), size);
}

}  // namespace fo4ide::app::swftarget
