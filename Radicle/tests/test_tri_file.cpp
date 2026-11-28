#include "fo4/nif/tri_file.hpp"

#include "fo4/nif/outfit_studio.hpp"

#include "fo4/c_api.h"
#include "fo4/c_api_nif.h"
#include "fo4/c_api_nif_mesh.h"
#include "fo4/error.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

struct Morph {
  std::string name;
  float scale;
  std::vector<std::int16_t> raw;
};

struct Builder {
  std::int32_t vertices{2};
  std::int32_t triangles{1};
  std::int32_t quads{0};
  std::int32_t flags{1};
  std::int32_t modifiers{0};
  std::int32_t uvs{-1};
  std::vector<float> positions{0, 0, 0, 1, 2, 3};
  std::vector<Morph> morphs;
  std::size_t trailing{0};

  std::vector<std::byte> build() const {
    std::vector<std::byte> out;
    const auto put = [&out](const void* data, std::size_t size) {
      const auto* bytes = static_cast<const std::byte*>(data);
      out.insert(out.end(), bytes, bytes + size);
    };
    const auto put_i32 = [&](std::int32_t value) { put(&value, 4); };
    put("FRTRI003", 8);
    put_i32(vertices);
    put_i32(triangles);
    put_i32(quads);
    put_i32(0);
    put_i32(0);
    put_i32(uvs < 0 ? vertices : uvs);
    put_i32(flags);
    put_i32(static_cast<std::int32_t>(morphs.size()));
    put_i32(modifiers);
    put_i32(0);
    for (int i = 0; i < 4; ++i) put_i32(0);
    put(positions.data(), positions.size() * 4);
    const std::vector<std::int32_t> tri_block(static_cast<std::size_t>(triangles) * 3, 0);
    put(tri_block.data(), tri_block.size() * 4);
    const std::vector<float> uv_block(static_cast<std::size_t>(vertices) * 2, 0.5f);
    put(uv_block.data(), uv_block.size() * 4);
    put(tri_block.data(), tri_block.size() * 4);
    for (const auto& morph : morphs) {
      put_i32(static_cast<std::int32_t>(morph.name.size() + 1));
      put(morph.name.c_str(), morph.name.size() + 1);
      put(&morph.scale, 4);
      put(morph.raw.data(), morph.raw.size() * 2);
    }
    for (std::size_t i = 0; i < trailing; ++i) out.push_back(std::byte{0});
    return out;
  }
};

Builder two_vertex_head() {
  Builder builder;
  builder.morphs.push_back({"JawOpen", 0.001f, {0, -1000, -2000, 0, 500, 32767}});
  builder.morphs.push_back({"Pucker", 0.5f, {1, 2, 3, 4, 5, 6}});
  return builder;
}

}  // namespace

TEST(TriFile, ReadsPositionsMorphNamesAndScaledDeltas) {
  const auto bytes = two_vertex_head().build();
  const auto tri = fo4::nif::parse_tri(bytes);
  EXPECT_EQ(tri.vertex_count, 2u);
  EXPECT_EQ(tri.triangle_count, 1u);
  ASSERT_EQ(tri.positions.size(), 6u);
  EXPECT_FLOAT_EQ(tri.positions[5], 3.0f);
  ASSERT_EQ(tri.morphs.size(), 2u);
  EXPECT_EQ(tri.morphs[0].name, "JawOpen");
  ASSERT_NE(tri.find("Pucker"), nullptr);
  EXPECT_EQ(tri.find("Nope"), nullptr);
  const auto deltas = fo4::nif::tri_morph_deltas(*tri.find("JawOpen"));
  ASSERT_EQ(deltas.size(), 6u);
  EXPECT_FLOAT_EQ(deltas[1], -1.0f);
  EXPECT_FLOAT_EQ(deltas[2], -2.0f);
  EXPECT_NEAR(deltas[5], 32.767f, 1e-4);
}

TEST(TriFile, EngineFrameIsXZMinusYOverUnits) {
  const std::vector<float> values{1.0f, 2.0f, 3.0f};
  const auto engine = fo4::nif::tri_to_engine_frame(values, 2.0f);
  ASSERT_EQ(engine.size(), 3u);
  EXPECT_FLOAT_EQ(engine[0], 0.5f);
  EXPECT_FLOAT_EQ(engine[1], 1.5f);
  EXPECT_FLOAT_EQ(engine[2], -1.0f);
  const auto defaulted = fo4::nif::tri_to_engine_frame(values, 0.0f);
  EXPECT_NEAR(defaulted[0], 1.0f / 69.99125f, 1e-7);
  EXPECT_THROW(fo4::nif::tri_to_engine_frame(values, -1.0f), std::invalid_argument);
  EXPECT_THROW(fo4::nif::tri_to_engine_frame(std::vector<float>{1.0f, 2.0f}, 1.0f), std::invalid_argument);
}

TEST(TriFile, RefusesBadMagicTruncationAndTrailingBytes) {
  const auto bytes = two_vertex_head().build();
  auto bad_magic = bytes;
  bad_magic[0] = std::byte{'X'};
  EXPECT_THROW(fo4::nif::parse_tri(bad_magic), fo4::parse_error);
  const std::vector<std::byte> truncated(bytes.begin(), bytes.end() - 3);
  EXPECT_THROW(fo4::nif::parse_tri(truncated), fo4::parse_error);
  EXPECT_THROW(fo4::nif::parse_tri(std::vector<std::byte>(10)), fo4::parse_error);
  auto padded = two_vertex_head();
  padded.trailing = 1;
  EXPECT_THROW(fo4::nif::parse_tri(padded.build()), fo4::parse_error);
}

TEST(TriFile, RefusesLayoutsNoShippedFileUses) {
  using Mutation = void (*)(Builder&);
  const Mutation mutations[] = {[](Builder& b) { b.quads = 1; }, [](Builder& b) { b.flags = 0; },
                                [](Builder& b) { b.modifiers = 1; }, [](Builder& b) { b.uvs = 1; },
                                [](Builder& b) { b.vertices = 0; }};
  for (const auto mutate : mutations) {
    auto builder = two_vertex_head();
    mutate(builder);
    EXPECT_THROW(fo4::nif::parse_tri(builder.build()), fo4::parse_error);
  }
}

TEST(TriFile, RefusesAMorphNameLengthThatRunsPastTheFile) {
  auto bytes = two_vertex_head().build();
  const std::size_t name_length_offset = 64 + 24 + 12 + 16 + 12;
  const std::int32_t huge = 100000;
  std::memcpy(bytes.data() + name_length_offset, &huge, 4);
  EXPECT_THROW(fo4::nif::parse_tri(bytes), fo4::parse_error);
}

TEST(TriFile, RefusesAMorphNameLengthInRangeThatRunsPastTheFile) {
  auto bytes = two_vertex_head().build();
  const std::size_t name_length_offset = 64 + 24 + 12 + 16 + 12;
  const std::int32_t in_range = 200;
  std::memcpy(bytes.data() + name_length_offset, &in_range, 4);
  EXPECT_LT(bytes.size() - name_length_offset - 4, static_cast<std::size_t>(in_range));
  EXPECT_THROW(fo4::nif::parse_tri(bytes), fo4::parse_error);
}

TEST(TriFile, AHugeMorphCountOnAShortFileIsAParseErrorNotAnAllocationFailure) {
  auto builder = two_vertex_head();
  builder.morphs.clear();
  auto bytes = builder.build();
  const std::int32_t huge = 0x7fffffff;
  std::memcpy(bytes.data() + 8 + 7 * 4, &huge, 4);
  EXPECT_THROW(fo4::nif::parse_tri(bytes), fo4::parse_error);
  radicle_tri* tri = reinterpret_cast<radicle_tri*>(1);
  EXPECT_EQ(radicle_tri_read(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), &tri), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(tri, nullptr);
}

TEST(TriFile, RefusesAMorphWhoseNameIsEmptyBeforeItsFirstNul) {
  auto builder = two_vertex_head();
  builder.morphs[0].name = std::string(1, static_cast<char>(0)) + "JawOpen";
  EXPECT_THROW(fo4::nif::parse_tri(builder.build()), fo4::parse_error);
}

TEST(TriFileCApi, RoundTripsCountsNamesPositionsAndDeltas) {
  const auto bytes = two_vertex_head().build();
  radicle_tri* tri = nullptr;
  ASSERT_EQ(radicle_tri_read(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), &tri), RADICLE_STATUS_OK);
  ASSERT_NE(tri, nullptr);
  uint32_t vertices = 0, triangles = 0, morphs = 0;
  ASSERT_EQ(radicle_tri_counts(tri, &vertices, &triangles, &morphs), RADICLE_STATUS_OK);
  EXPECT_EQ(vertices, 2u);
  EXPECT_EQ(triangles, 1u);
  EXPECT_EQ(morphs, 2u);
  const char* name = nullptr;
  ASSERT_EQ(radicle_tri_morph_name(tri, 1, &name), RADICLE_STATUS_OK);
  EXPECT_STREQ(name, "Pucker");
  EXPECT_EQ(radicle_tri_morph_name(tri, 2, &name), RADICLE_STATUS_INVALID_ARGUMENT);

  float* positions = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_tri_positions(tri, 1.0f, &positions, &count), RADICLE_STATUS_OK);
  ASSERT_EQ(count, 6u);
  EXPECT_FLOAT_EQ(positions[3], 1.0f);
  EXPECT_FLOAT_EQ(positions[4], 3.0f);
  EXPECT_FLOAT_EQ(positions[5], -2.0f);
  radicle_free_floats(positions);

  float* deltas = nullptr;
  ASSERT_EQ(radicle_tri_morph_deltas(tri, 0, 1.0f, &deltas, &count), RADICLE_STATUS_OK);
  ASSERT_EQ(count, 6u);
  EXPECT_FLOAT_EQ(deltas[0], 0.0f);
  EXPECT_NEAR(deltas[2], 1.0f, 1e-5);
  EXPECT_NEAR(deltas[4], 32.767f, 1e-4);
  EXPECT_NEAR(deltas[5], -0.5f, 1e-5);
  radicle_free_floats(deltas);

  EXPECT_EQ(radicle_tri_morph_deltas(tri, 2, 1.0f, &deltas, &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(deltas, nullptr);
  EXPECT_EQ(radicle_tri_morph_deltas(tri, 0, -1.0f, &deltas, &count), RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_tri_free(tri);
}

TEST(TriFileCApi, RefusesBadInputWithoutAHandle) {
  radicle_tri* tri = reinterpret_cast<radicle_tri*>(1);
  const uint8_t junk[16] = {};
  EXPECT_EQ(radicle_tri_read(junk, sizeof(junk), &tri), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(tri, nullptr);
  EXPECT_EQ(radicle_tri_read(nullptr, 4, &tri), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_tri_read(junk, 0, &tri), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_tri_read(junk, sizeof(junk), nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  uint32_t a = 0, b = 0, c = 0;
  EXPECT_EQ(radicle_tri_counts(nullptr, &a, &b, &c), RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_tri_free(nullptr);
}

namespace {
namespace osd_test {

struct Entry {
  std::string name;
  std::vector<std::pair<std::uint16_t, std::array<float, 3>>> offsets;
};

void put(std::vector<std::byte>& out, const void* data, std::size_t size) {
  const auto* bytes = static_cast<const std::byte*>(data);
  out.insert(out.end(), bytes, bytes + size);
}

std::vector<std::byte> build_osd(const std::vector<Entry>& entries, std::uint32_t version = 1) {
  std::vector<std::byte> out;
  const std::uint8_t magic[4] = {0x00, 0x44, 0x53, 0x4F};
  put(out, magic, 4);
  const auto count = static_cast<std::uint32_t>(entries.size());
  put(out, &version, 4);
  put(out, &count, 4);
  for (const auto& entry : entries) {
    const auto name_size = static_cast<std::uint8_t>(entry.name.size());
    put(out, &name_size, 1);
    put(out, entry.name.data(), entry.name.size());
    const auto diffs = static_cast<std::uint16_t>(entry.offsets.size());
    put(out, &diffs, 2);
    for (const auto& [vertex, delta] : entry.offsets) {
      put(out, &vertex, 2);
      put(out, delta.data(), 12);
    }
  }
  return out;
}

std::span<const std::byte> view(const std::vector<std::byte>& bytes, std::size_t size) {
  return std::span<const std::byte>(bytes.data(), size);
}

const std::vector<Entry>& sample_entries() {
  static const std::vector<Entry> entries{
      {"CuirassBreasts", {{3, {1.0f, 2.0f, 3.0f}}, {0, {-1.0f, 0.0f, 0.5f}}}},
      {"CuirassButt", {{1, {0.0f, 0.0f, 1.0f}}}},
      {"Unused", {}},
  };
  return entries;
}

std::string sample_osp() {
  return std::string("\xEF\xBB\xBF") + R"(<?xml version="1.0" encoding="UTF-8"?>
<SliderSetInfo version="1">
  <SliderSet name="Test &amp; Set">
    <DataFolder>Fold</DataFolder>
    <SourceFile>Body.nif</SourceFile>
    <OutputPath>meshes\armor\Test</OutputPath>
    <OutputFile GenWeights="true">Body</OutputFile>
    <Shape target="BaseShape" DataFolder="Ref">BaseShape</Shape>
    <Shape target="Cuirass">CuirassGeo</Shape>
    <Slider name="Breasts" invert="true" small="20" big="100.5">
      <Data name="RefBreasts" target="BaseShape">RefBreasts.bsd</Data>
      <Data name="DifferentAttr" target="Cuirass" local="true">Body.osd\CuirassBreasts</Data>
    </Slider>
    <Slider name="Butt" zap="false" hidden="TRUE">
      <Data name="Butt" target="Cuirass" local="true">Sub/Body.OSD\CuirassButt</Data>
      <Data name="FolderBsd" target="BaseShape">Sub\Foo.bsd</Data>
    </Slider>
  </SliderSet>
</SliderSetInfo>)";
}

std::string wrap(const std::string& inner, const std::string& version = "1") {
  return "<SliderSetInfo version=\"" + version + "\"><SliderSet name=\"S\">" + inner + "</SliderSet></SliderSetInfo>";
}

}  // namespace osd_test
}  // namespace

TEST(OutfitStudioOsd, ReadsDataSetsInFileOrderWithUnsortedVertexIndices) {
  // Must catch a wrong record stride or field order, or sorted vertices moving offsets onto the wrong vertices.
  const auto bytes = osd_test::build_osd(osd_test::sample_entries());
  const auto file = fo4::nif::parse_osd(bytes);
  EXPECT_EQ(file.version, 1U);
  ASSERT_EQ(file.sets.size(), 3U);
  EXPECT_EQ(file.sets[0].name, "CuirassBreasts");
  ASSERT_EQ(file.sets[0].offsets.size(), 2U);
  EXPECT_EQ(file.sets[0].offsets[0].vertex, 3U);
  EXPECT_EQ(file.sets[0].offsets[0].delta, (std::array<float, 3>{1.0f, 2.0f, 3.0f}));
  EXPECT_EQ(file.sets[0].offsets[1].vertex, 0U);
  EXPECT_EQ(file.sets[0].offsets[1].delta, (std::array<float, 3>{-1.0f, 0.0f, 0.5f}));
  EXPECT_TRUE(file.find("Unused")->offsets.empty());
  EXPECT_EQ(file.find("CuirassButt")->offsets[0].vertex, 1U);
  EXPECT_EQ(file.find("Missing"), nullptr);
  EXPECT_TRUE(fo4::nif::parse_osd(osd_test::build_osd({})).sets.empty());
}

TEST(OutfitStudioOsd, RefusesUnknownHeadersAndEveryTruncationOrCorruption) {
  // Must catch a reader that guesses past an unknown magic or version, trusts the set count, or reads past untrusted bytes.
  const auto good = osd_test::build_osd(osd_test::sample_entries());
  for (std::size_t size = 0; size < good.size(); ++size) {
    EXPECT_THROW(fo4::nif::parse_osd(osd_test::view(good, size)), fo4::parse_error) << "prefix " << size;
  }
  auto reversed_magic = good;
  std::swap(reversed_magic[0], reversed_magic[3]);
  std::swap(reversed_magic[1], reversed_magic[2]);
  EXPECT_THROW(fo4::nif::parse_osd(reversed_magic), fo4::parse_error);
  EXPECT_THROW(fo4::nif::parse_osd(osd_test::build_osd(osd_test::sample_entries(), 2)), fo4::parse_error);
  EXPECT_THROW(fo4::nif::parse_osd(osd_test::build_osd(osd_test::sample_entries(), 0)), fo4::parse_error);

  auto huge_count = osd_test::build_osd({});
  huge_count[8] = huge_count[9] = huge_count[10] = huge_count[11] = std::byte{0xFF};
  EXPECT_THROW(fo4::nif::parse_osd(huge_count), fo4::parse_error);

  auto trailing = good;
  trailing.push_back(std::byte{0});
  EXPECT_THROW(fo4::nif::parse_osd(trailing), fo4::parse_error);

  auto long_name = good;
  long_name[12] = std::byte{0xFF};
  EXPECT_THROW(fo4::nif::parse_osd(long_name), fo4::parse_error);

  auto long_diff_count = good;
  long_diff_count[12 + 1 + 14] = std::byte{0xFF};
  long_diff_count[12 + 1 + 14 + 1] = std::byte{0xFF};
  EXPECT_THROW(fo4::nif::parse_osd(long_diff_count), fo4::parse_error);

  EXPECT_THROW(fo4::nif::parse_osd(osd_test::build_osd({{"A", {}}, {"A", {}}})), fo4::parse_error);
  EXPECT_THROW(fo4::nif::parse_osd(osd_test::build_osd({{"A", {{7, {0, 0, 1}}, {7, {0, 1, 0}}}}})),
               fo4::parse_error);
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float inf = std::numeric_limits<float>::infinity();
  EXPECT_THROW(fo4::nif::parse_osd(osd_test::build_osd({{"A", {{0, {nan, 0, 0}}}}})), fo4::parse_error);
  EXPECT_THROW(fo4::nif::parse_osd(osd_test::build_osd({{"A", {{0, {0, inf, 0}}}}})), fo4::parse_error);
}

TEST(OutfitStudioOsp, ReadsAProjectWithAByteOrderMarkAndResolvesEachSliderToItsDataSet) {
  // Must catch a byte order mark failing the XML parse, the name attribute used as the key instead of the text after the last separator, or the shape name matched where the target belongs.
  const auto osp = fo4::nif::parse_osp(osd_test::sample_osp());
  const auto osd = fo4::nif::parse_osd(osd_test::build_osd(osd_test::sample_entries()));
  EXPECT_EQ(osp.version, 1U);
  ASSERT_EQ(osp.sets.size(), 1U);
  const auto& set = *osp.find_set("Test & Set");
  EXPECT_EQ(set.data_folder, "Fold");
  EXPECT_EQ(set.source_file, "Body.nif");
  EXPECT_EQ(set.output_path, "meshes\\armor\\Test");
  EXPECT_EQ(set.output_file, "Body");
  ASSERT_EQ(set.shapes.size(), 2U);
  EXPECT_EQ(set.find_shape("CuirassGeo")->target, "Cuirass");
  EXPECT_EQ(set.find_shape("BaseShape")->data_folder, "Ref");
  ASSERT_EQ(set.sliders.size(), 2U);
  const auto& breasts = *set.find_slider("Breasts");
  EXPECT_TRUE(breasts.invert);
  EXPECT_FALSE(breasts.hidden);
  EXPECT_FLOAT_EQ(breasts.small_value, 20.0f);
  EXPECT_FLOAT_EQ(breasts.big_value, 100.5f);
  ASSERT_EQ(breasts.data.size(), 2U);
  EXPECT_TRUE(breasts.data[0].legacy_bsd);
  EXPECT_EQ(breasts.data[0].file, "RefBreasts.bsd");
  EXPECT_TRUE(breasts.data[0].name_in_file.empty());
  EXPECT_FALSE(breasts.data[1].legacy_bsd);
  EXPECT_TRUE(breasts.data[1].local);
  EXPECT_EQ(breasts.data[1].name, "DifferentAttr");
  EXPECT_EQ(breasts.data[1].file, "Body.osd");
  EXPECT_EQ(breasts.data[1].name_in_file, "CuirassBreasts");
  const auto& butt = *set.find_slider("Butt");
  EXPECT_TRUE(butt.hidden);
  EXPECT_FALSE(butt.zap);
  // Must catch a legacy .bsd reference written with a folder path falling into the .osd branch and refusing the whole project.
  ASSERT_EQ(butt.data.size(), 2U);
  EXPECT_TRUE(butt.data[1].legacy_bsd);
  EXPECT_EQ(butt.data[1].file, "Sub\\Foo.bsd");
  EXPECT_TRUE(butt.data[1].name_in_file.empty());

  const auto* diff = fo4::nif::find_osd_diff(osd, "Body.osd", set, "Breasts", "CuirassGeo");
  ASSERT_NE(diff, nullptr);
  EXPECT_EQ(diff->name, "CuirassBreasts");
  const auto* butt_diff = fo4::nif::find_osd_diff(osd, "Meshes/BODY.osd", set, "Butt", "CuirassGeo");
  ASSERT_NE(butt_diff, nullptr);
  EXPECT_EQ(butt_diff->name, "CuirassButt");
  EXPECT_EQ(fo4::nif::find_osd_diff(osd, "Body.osd", set, "Breasts", "BaseShape"), nullptr);
  EXPECT_EQ(fo4::nif::find_osd_diff(osd, "Other.osd", set, "Breasts", "CuirassGeo"), nullptr);
  EXPECT_EQ(fo4::nif::find_osd_diff(osd, "Body.osd", set, "Missing", "CuirassGeo"), nullptr);
  EXPECT_EQ(fo4::nif::find_osd_diff(osd, "Body.osd", set, "Breasts", "Cuirass"), nullptr);

  std::vector<float> positions(12, 0.0f);
  EXPECT_FALSE(fo4::nif::apply_osd_diff(positions, *diff, 0.5f).has_value());
  EXPECT_FLOAT_EQ(positions[9], 0.5f);
  EXPECT_FLOAT_EQ(positions[10], 1.0f);
  EXPECT_FLOAT_EQ(positions[11], 1.5f);
  EXPECT_FLOAT_EQ(positions[0], -0.5f);
  EXPECT_FLOAT_EQ(positions[2], 0.25f);
  EXPECT_FLOAT_EQ(positions[3], 0.0f);
}

TEST(OutfitStudioOsp, RefusesVersionsLayoutsAndValuesThatNoTwoSourcesConfirm) {
  // Must catch a reader that guesses at an unknown project version, a missing shape target, an unrecognized data file or a malformed flag.
  const std::string slider_open = "<Slider name=\"A\">";
  EXPECT_NO_THROW(fo4::nif::parse_osp(osd_test::wrap("<Shape target=\"T\">S</Shape>" + slider_open +
                                                       "<Data target=\"T\">F.osd\\N</Data></Slider>")));
  const std::vector<std::string> refused{
      "",
      "<SliderSetInfo version=\"1\"",
      "<SliderSetInfo version=\"1\"/>",
      "<Other version=\"1\"><SliderSet name=\"S\"/></Other>",
      "<SliderSetInfo><SliderSet name=\"S\"/></SliderSetInfo>",
      osd_test::wrap("", "0"),
      osd_test::wrap("", "2"),
      osd_test::wrap("<Shape>S</Shape>"),
      osd_test::wrap("<Shape target=\"T\"></Shape>"),
      osd_test::wrap("<Shape target=\"T\">S</Shape><Shape target=\"T\">S</Shape>"),
      osd_test::wrap(slider_open + "<Data target=\"T\">F.osd</Data></Slider>"),
      osd_test::wrap(slider_open + "<Data target=\"T\">F.txt\\N</Data></Slider>"),
      osd_test::wrap(slider_open + "<Data target=\"T\">F.osd\\</Data></Slider>"),
      osd_test::wrap(slider_open + "<Data>F.osd\\N</Data></Slider>"),
      osd_test::wrap(slider_open + "<Data target=\"T\"/></Slider>"),
      osd_test::wrap(slider_open + "</Slider>" + slider_open + "</Slider>"),
      osd_test::wrap("<Slider name=\"A\" invert=\"yes\"/>"),
      osd_test::wrap("<Slider name=\"A\" big=\"abc\"/>"),
      osd_test::wrap("<Slider name=\"A\" big=\"1e999\"/>"),
      osd_test::wrap("<Slider/>"),
  };
  for (const auto& text : refused) {
    EXPECT_THROW(fo4::nif::parse_osp(text), fo4::parse_error) << text;
  }
}

TEST(OutfitStudioOsd, ApplyAddsWeightedOffsetsAndRefusesWithoutTouchingThePositions) {
  // Must catch an out-of-range vertex index writing past the position array, or a half-applied diff left behind on refusal.
  fo4::nif::OsdDataSet diff;
  diff.offsets = {{1, {1.0f, 2.0f, 3.0f}}, {0, {0.5f, 0.0f, 0.0f}}};
  std::vector<float> positions{1, 1, 1, 2, 2, 2};
  EXPECT_FALSE(fo4::nif::apply_osd_diff(positions, diff, -2.0f).has_value());
  EXPECT_EQ(positions, (std::vector<float>{0.0f, 1.0f, 1.0f, 0.0f, -2.0f, -4.0f}));

  diff.offsets.push_back({2, {9.0f, 9.0f, 9.0f}});
  positions = {1, 1, 1, 2, 2, 2};
  const auto before = positions;
  const auto reason = fo4::nif::apply_osd_diff(positions, diff, 1.0f);
  ASSERT_TRUE(reason.has_value());
  EXPECT_NE(reason->find("vertex 2"), std::string::npos);
  EXPECT_EQ(positions, before);
  EXPECT_TRUE(fo4::nif::apply_osd_diff(positions, diff, std::numeric_limits<float>::quiet_NaN()).has_value());
  std::vector<float> ragged(5, 0.0f);
  EXPECT_TRUE(fo4::nif::apply_osd_diff(ragged, diff, 1.0f).has_value());
  EXPECT_EQ(positions, before);

  // Must catch a finite offset scaled past the float range that writes infinity into the positions without a refusal.
  fo4::nif::OsdDataSet overflow;
  overflow.offsets.push_back({0, {3e38f, 0.0f, 0.0f}});
  EXPECT_TRUE(fo4::nif::apply_osd_diff(positions, overflow, 4.0f).has_value());
  EXPECT_EQ(positions, before);
}
