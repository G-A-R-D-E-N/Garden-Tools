#pragma once

#include <fo4/avm2/value.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace fo4::avm2 {

/* A SharedObject body must be AMF3 so Flash and Scaleform can read it back. */
std::vector<std::uint8_t> encode_amf3(const Value& value);

/* Requires a complete AMF3 body; a trailing byte or a truncated value must refuse the whole buffer. */
bool decode_amf3(std::span<const std::uint8_t> bytes, Value& out);

/* A SharedObject name must be storage-safe, so a slash or a NUL is refused before any host call. */
bool shared_object_name_ok(std::string_view name);

}  // namespace fo4::avm2
