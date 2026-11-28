#pragma once

#include <span>
#include <stdexcept>

namespace fo4::math {

class InterpolationError : public std::invalid_argument {
public:
  using std::invalid_argument::invalid_argument;
};

float pchip(std::span<const float> xs, std::span<const float> ys, float x);

}  // namespace fo4::math
