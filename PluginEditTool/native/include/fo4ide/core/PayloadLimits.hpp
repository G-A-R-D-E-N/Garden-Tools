#pragma once

#include <cstddef>
#include <cstdint>

namespace fo4ide::core {

// Constraint: caps the file bytes Open reads; Radicle's kMaxBodyBytes (swf_internal.hpp) is the same 256 MiB but caps the decompressed body.
inline constexpr std::uintmax_t kMaxSwfFileBytes = 256ull * 1024ull * 1024ull;
inline constexpr std::size_t kMaxDroppedFileBytes = 16 * 1024 * 1024;
inline constexpr std::size_t kRequestEnvelopeBytes = 1024 * 1024;
inline constexpr std::size_t kMaxRequestBytes =
    (kMaxDroppedFileBytes + 2) / 3 * 4 + kRequestEnvelopeBytes;

}
