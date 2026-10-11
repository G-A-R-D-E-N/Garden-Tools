#pragma once

#include <cstddef>

namespace fo4ide::core {

inline constexpr std::size_t kMaxDroppedFileBytes = 16 * 1024 * 1024;
inline constexpr std::size_t kRequestEnvelopeBytes = 1024 * 1024;
inline constexpr std::size_t kMaxRequestBytes =
    (kMaxDroppedFileBytes + 2) / 3 * 4 + kRequestEnvelopeBytes;

}
