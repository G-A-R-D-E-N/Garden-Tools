#pragma once

#include <expected>

#include <fo4ide/core/Error.hpp>

namespace fo4ide {

template <class T>
using Result = std::expected<T, Error>;

}
