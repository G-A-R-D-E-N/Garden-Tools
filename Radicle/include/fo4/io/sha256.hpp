#pragma once

#include <cstddef>
#include <span>
#include <string>

namespace fo4::io {

std::string sha256_hex(std::span<const std::byte> bytes);

}
