#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace fo4::papyrus {

// Invariant: sha256Hex returns the lower-case hex digest of the UTF-8 bytes, matching the C# digest form.
std::string sha256Hex(std::string_view bytes);

}  // namespace fo4::papyrus
