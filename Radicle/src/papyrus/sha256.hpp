#pragma once

#include "fo4/io/sha256.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace fo4::papyrus {

// Invariant: sha256Hex returns the lower-case hex digest of the UTF-8 bytes, matching the C# digest form.
inline std::string sha256Hex(std::string_view bytes) {
    return fo4::io::sha256_hex(std::as_bytes(std::span(bytes.data(), bytes.size())));
}

}  // namespace fo4::papyrus
