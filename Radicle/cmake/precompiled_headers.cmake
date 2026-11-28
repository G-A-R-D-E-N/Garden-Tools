option(RADICLE_PRECOMPILE_HEADERS "Precompile the common standard headers for fo4 and the Radicle test executables" OFF)

set(FO4_PCH_HEADERS
  <algorithm> <array> <cstddef> <cstdint> <cstring> <filesystem> <map> <memory>
  <optional> <span> <stdexcept> <string> <string_view> <utility> <vector>)
set(FO4_TEST_PCH_HEADERS ${FO4_PCH_HEADERS} <gtest/gtest.h>)

function(fo4_use_pch target)
  if(RADICLE_PRECOMPILE_HEADERS)
    target_precompile_headers(${target} PRIVATE ${ARGN})
  endif()
endfunction()

# Requires: the owner has the fewest compile definitions, and all targets share compiler flags.
function(fo4_reuse_pch target owner)
  if(RADICLE_PRECOMPILE_HEADERS)
    target_precompile_headers(${target} REUSE_FROM ${owner})
  endif()
endfunction()
