#pragma once

#include <array>
#include <cstddef>
#include <functional>

namespace fo4::nif::uv::detail {

// Invariant: a grid cell is a floored double kept as a double, never cast to an integer; -0 folds to +0 so equal cells hash alike.
template <std::size_t N>
struct Cell {
  std::array<double, N> at{};
  bool operator==(const Cell&) const = default;
};

template <std::size_t N>
Cell<N> make_cell(std::array<double, N> raw) {
  for (double& x : raw) x += 0.0;
  return Cell<N>{raw};
}

template <std::size_t N>
struct CellHash {
  std::size_t operator()(const Cell<N>& cell) const {
    std::size_t h = 1469598103934665603ull;
    for (const double x : cell.at) h = (h ^ std::hash<double>{}(x)) * 1099511628211ull + 0x9e3779b97f4a7c15ull;
    return h;
  }
};

}  // namespace fo4::nif::uv::detail
