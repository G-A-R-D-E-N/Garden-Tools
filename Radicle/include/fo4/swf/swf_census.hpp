#pragma once

#include <fo4/swf/swf.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::swf {

enum class CensusKind : std::uint8_t {
  flash_class = 0,      /* a flash.* public package name in a class/type position */
  flash_member,         /* a flash.* public package name in a property or method position */
  scaleform_class,      /* a scaleform.* public package name in a class/type position */
  scaleform_member,     /* a scaleform.* public package name in a property or method position */
  builtin,              /* an unqualified class or type the movie does not declare, or Vector.<T> */
  member,               /* an unqualified property or method the movie does not declare */
  scope,                /* an unqualified findproperty/findpropstrict/finddef the movie does not declare */
};

const char* census_kind_name(CensusKind kind) noexcept;

/* count must be the number of reference occurrences, not distinct call sites. */
struct CensusReference {
  CensusKind kind{CensusKind::builtin};
  std::string name;
  std::uint32_t count{0};
};

/* url must end in .swf or .xml, exactly as the movie spells it. */
struct CensusLoad {
  std::string url;
  std::uint32_t count{0};
};

struct MovieCensus {
  std::string movie;
  std::size_t abc_blocks{0};
  std::size_t parsed_blocks{0};
  std::vector<CensusReference> references;  /* sorted by (census_kind_name, name) */
  std::vector<CensusLoad> loads;            /* sorted by url */
};

/* A block that does not parse must be counted without being read; rows must stay sorted. */
MovieCensus census_movie(std::string_view movie, const SwfFile& file);

/* Two calls with equal inputs must return equal text; the real-data test compares it byte for byte. */
std::string census_text(std::span<const MovieCensus> movies, std::string_view game_build);

}  // namespace fo4::swf
