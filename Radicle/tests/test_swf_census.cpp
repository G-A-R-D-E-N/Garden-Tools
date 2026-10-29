#include <fo4/swf/swf.hpp>
#include <fo4/swf/swf_census.hpp>

#include "abc_test_support.hpp"
#include "swf_movie_builder.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace {

using swf_test::SwfBuilder;

std::vector<std::byte> abc_movie() {
  const auto abc = fo4::test::build_abc();
  SwfBuilder file;
  file.rect(0, 11000, 0, 8000, 15);
  file.u16(0x0C00);
  file.u16(2);

  SwfBuilder abc_tag;
  abc_tag.u32(0);
  abc_tag.text("frame 1");
  abc_tag.bytes(std::span<const std::byte>(reinterpret_cast<const std::byte*>(abc.data()),
                                           abc.size()));
  file.tag(82, abc_tag.body, false);

  file.tag(1);
  file.tag(0);
  return swf_test::wrap_body(file.body, 10, false, false);
}

std::vector<std::byte> empty_movie() {
  SwfBuilder file;
  file.rect(0, 11000, 0, 8000, 15);
  file.u16(0x0C00);
  file.u16(1);
  file.tag(1);
  file.tag(0);
  return swf_test::wrap_body(file.body, 10, false, false);
}

fo4::swf::SwfFile read(const std::vector<std::byte>& bytes) {
  const auto result = fo4::swf::read_swf(bytes);
  EXPECT_TRUE(result.ok()) << fo4::swf::status_name(result.status) << " " << result.message;
  return result.file;
}

std::string header_for(std::size_t movies, std::size_t blocks, std::size_t parsed,
                       std::string_view game_build) {
  return "# Radicle SWF ABC census: the engine-provided surface every Programs movie references.\n"
         "# Generated from Fallout4 - Interface.ba2; names and counts only, no game data.\n"
         "# game_build " +
         std::string{game_build} + "\n# movies " + std::to_string(movies) + "\n# abc_blocks " +
         std::to_string(blocks) + "\n# abc_parsed " + std::to_string(parsed) + "\n";
}

}  // namespace

TEST(SwfCensus, BinsOnlyTheSurfaceTheMovieDoesNotDefine) {
  const auto file = read(abc_movie());
  const fo4::swf::MovieCensus census = fo4::swf::census_movie("test.swf", file);

  EXPECT_EQ(census.movie, "test.swf");
  EXPECT_EQ(census.abc_blocks, 1u);
  EXPECT_EQ(census.parsed_blocks, 1u);

  /* Declared names must never reach a bin; every engine name the synthetic ABC uses must. */
  EXPECT_EQ(census.references.size(), 8u);

  const std::string text = fo4::swf::census_text(std::span<const fo4::swf::MovieCensus>(&census, 1),
                                                 "test");
  const std::string expected = header_for(1, 1, 1, "test") +
                               "movie test.swf\n"
                               "abc 1\n"
                               "parsed 1\n"
                               "builtin Object 1\n"
                               "builtin addEventListener 1\n"
                               "builtin click 1\n"
                               "builtin handler 1\n"
                               "builtin void 6\n"
                               "flash-class flash.external::ExternalInterface 1\n"
                               "flash-member flash.external::call 1\n"
                               "member addEventListener 1\n";
  EXPECT_EQ(text, expected);
}

TEST(SwfCensus, KeepsTheDeclaredNamesOutOfEveryBin) {
  const auto file = read(abc_movie());
  const fo4::swf::MovieCensus census = fo4::swf::census_movie("test.swf", file);
  for (const std::string forbidden : {"Test", "onClick", "counter", "Inner", "helper"}) {
    for (const fo4::swf::CensusReference& reference : census.references) {
      EXPECT_NE(reference.name, forbidden) << "declared name reached the census: " << forbidden;
    }
  }
}

TEST(SwfCensus, CountsNoReferencesInAMovieWithoutBytecode) {
  const auto file = read(empty_movie());
  const fo4::swf::MovieCensus census = fo4::swf::census_movie("empty.swf", file);
  EXPECT_EQ(census.abc_blocks, 0u);
  EXPECT_EQ(census.parsed_blocks, 0u);
  EXPECT_TRUE(census.references.empty());
  EXPECT_TRUE(census.loads.empty());
  const std::string text = fo4::swf::census_text(std::span<const fo4::swf::MovieCensus>(&census, 1),
                                                 "test");
  EXPECT_EQ(text, header_for(1, 0, 0, "test") + "movie empty.swf\nabc 0\nparsed 0\n");
}

TEST(SwfCensus, IsDeterministic) {
  const auto file = read(abc_movie());
  const fo4::swf::MovieCensus first = fo4::swf::census_movie("test.swf", file);
  const fo4::swf::MovieCensus second = fo4::swf::census_movie("test.swf", read(abc_movie()));
  EXPECT_EQ(fo4::swf::census_text(std::span<const fo4::swf::MovieCensus>(&first, 1), "test"),
            fo4::swf::census_text(std::span<const fo4::swf::MovieCensus>(&second, 1), "test"));
}

TEST(SwfCensusKind, NamesTheBinsTheManifestCompares) {
  EXPECT_STREQ(fo4::swf::census_kind_name(fo4::swf::CensusKind::flash_class), "flash-class");
  EXPECT_STREQ(fo4::swf::census_kind_name(fo4::swf::CensusKind::flash_member), "flash-member");
  EXPECT_STREQ(fo4::swf::census_kind_name(fo4::swf::CensusKind::scaleform_class),
               "scaleform-class");
  EXPECT_STREQ(fo4::swf::census_kind_name(fo4::swf::CensusKind::scaleform_member),
               "scaleform-member");
  EXPECT_STREQ(fo4::swf::census_kind_name(fo4::swf::CensusKind::builtin), "builtin");
  EXPECT_STREQ(fo4::swf::census_kind_name(fo4::swf::CensusKind::member), "member");
  EXPECT_STREQ(fo4::swf::census_kind_name(fo4::swf::CensusKind::scope), "scope");
}
