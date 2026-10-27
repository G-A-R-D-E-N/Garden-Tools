#pragma once

#include <fo4/abc/abc.hpp>

#include <cstddef>
#include <string>
#include <string_view>

namespace fo4::abc {

struct AbcTextError {
  std::size_t line{0};
  std::string message;
};

/* Requires render_abc_text output; a bad line must return InconsistentModel with *error. */
AbcStatus assemble_abc_text(std::string_view text, AbcFile& out, AbcTextError* error = nullptr);

}  // namespace fo4::abc
