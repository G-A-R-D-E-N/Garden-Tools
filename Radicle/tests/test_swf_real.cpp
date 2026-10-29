#include "fo4/archive/ba2.hpp"
#include "fo4/swf/swf.hpp"
#include "fo4/swf/swf_characters.hpp"
#include "fo4/swf/swf_svg.hpp"
#include "real_data_build.hpp"
#include "real_data_movies.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <system_error>
#include <vector>

namespace {

struct MovieStats {
  std::size_t total{0};
  std::size_t movies{0};
  std::size_t gfx{0};
  std::size_t compressed{0};
  std::size_t action_script3{0};
  std::size_t lzma{0};
  std::size_t symbols{0};
  std::size_t abc_blocks{0};
  std::size_t shape_movies{0};
  std::size_t bitmap_movies{0};
  std::size_t font_movies{0};
  std::size_t text_movies{0};
  std::size_t sound_movies{0};
  std::size_t exporter_info_movies{0};
  std::size_t external_image_movies{0};
  std::size_t font_texture_movies{0};
  std::size_t shape_count{0};
  std::size_t shape_fill_styles{0};
  std::size_t shape_line_styles{0};
  std::size_t shape_gradient_stops{0};
  std::size_t shape_line_fills{0};
  std::size_t shape_records{0};
  std::size_t morph_count{0};
  std::size_t morph2_count{0};
  std::size_t morph_fill_pairs{0};
  std::size_t morph_line_pairs{0};
  std::size_t morph_gradient_stops{0};
  std::size_t morph_line_fills{0};
  std::size_t morph_records{0};
  std::size_t morph_unequal_streams{0};
  std::size_t morph_kind_mismatches{0};
  std::size_t morph_end_style_changes{0};
  std::size_t bitmap_count{0};
  std::size_t jpeg_count{0};
  std::size_t jpeg_tables_count{0};
  std::size_t art_failures{0};
  std::size_t characters{0};
  std::size_t character_failures{0};
  std::size_t edit_text_tags{0};
  std::size_t edit_text_layouts{0};
  std::map<std::string, std::size_t> versions;

  MovieStats& operator+=(const MovieStats& other) {
    total += other.total;
    movies += other.movies;
    gfx += other.gfx;
    compressed += other.compressed;
    action_script3 += other.action_script3;
    lzma += other.lzma;
    symbols += other.symbols;
    abc_blocks += other.abc_blocks;
    shape_movies += other.shape_movies;
    bitmap_movies += other.bitmap_movies;
    font_movies += other.font_movies;
    text_movies += other.text_movies;
    sound_movies += other.sound_movies;
    exporter_info_movies += other.exporter_info_movies;
    external_image_movies += other.external_image_movies;
    font_texture_movies += other.font_texture_movies;
    shape_count += other.shape_count;
    shape_fill_styles += other.shape_fill_styles;
    shape_line_styles += other.shape_line_styles;
    shape_gradient_stops += other.shape_gradient_stops;
    shape_line_fills += other.shape_line_fills;
    shape_records += other.shape_records;
    morph_count += other.morph_count;
    morph2_count += other.morph2_count;
    morph_fill_pairs += other.morph_fill_pairs;
    morph_line_pairs += other.morph_line_pairs;
    morph_gradient_stops += other.morph_gradient_stops;
    morph_line_fills += other.morph_line_fills;
    morph_records += other.morph_records;
    morph_unequal_streams += other.morph_unequal_streams;
    morph_kind_mismatches += other.morph_kind_mismatches;
    morph_end_style_changes += other.morph_end_style_changes;
    bitmap_count += other.bitmap_count;
    jpeg_count += other.jpeg_count;
    jpeg_tables_count += other.jpeg_tables_count;
    art_failures += other.art_failures;
    characters += other.characters;
    character_failures += other.character_failures;
    edit_text_tags += other.edit_text_tags;
    edit_text_layouts += other.edit_text_layouts;
    for (const auto& [key, count] : other.versions) {
      versions[key] += count;
    }
    return *this;
  }
};

unsigned shape_version(std::uint16_t code) {
  switch (code) {
    case fo4::swf::tag::kDefineShape:
      return 1;
    case fo4::swf::tag::kDefineShape2:
      return 2;
    case fo4::swf::tag::kDefineShape3:
      return 3;
    default:
      return 4;
  }
}

bool shape_fill_is_consistent(const fo4::swf::SwfFillStyle& fill) {
  const bool solid = fill.type == 0x00;
  const bool gradient = fill.type == 0x10 || fill.type == 0x12 || fill.type == 0x13;
  const bool bitmap = fill.type == 0x40 || fill.type == 0x41 || fill.type == 0x42 || fill.type == 0x43;
  if (!solid && !gradient && !bitmap) {
    return false;
  }
  if (solid) {
    return !fill.matrix && fill.stops.empty() && !fill.focal_point;
  }
  if (!fill.matrix) {
    return false;
  }
  if (bitmap) {
    return fill.stops.empty() && !fill.focal_point;
  }
  if (fill.focal_point.has_value() != (fill.type == 0x13)) {
    return false;
  }
  return fill.stops.size() <= 15;
}

bool shape_line_is_consistent(const fo4::swf::SwfLineStyle& line, unsigned version) {
  if (version < 4) {
    return line.flags == 0 && !line.miter_limit && !line.fill;
  }
  if (line.miter_limit.has_value() != (((line.flags >> 12) & 0x03u) == 2u)) {
    return false;
  }
  if (line.fill.has_value() != ((line.flags & 0x0800u) != 0)) {
    return false;
  }
  return !line.fill || (shape_fill_is_consistent(*line.fill) && line.color == 0);
}

void check_shape_styles(const fo4::swf::SwfShape& shape, unsigned version, MovieStats& stats) {
  for (const auto& fill : shape.fill_styles) {
    ++stats.shape_fill_styles;
    stats.shape_gradient_stops += fill.stops.size();
    if (!shape_fill_is_consistent(fill)) {
      ++stats.art_failures;
    }
  }
  for (const auto& line : shape.line_styles) {
    ++stats.shape_line_styles;
    stats.shape_line_fills += line.fill.has_value() ? 1u : 0u;
    if (!shape_line_is_consistent(line, version)) {
      ++stats.art_failures;
    }
  }
}

bool record_is_consistent(const fo4::swf::SwfShapeRecord& record, std::size_t fills, std::size_t lines) {
  switch (record.kind) {
    case fo4::swf::SwfShapeRecordKind::move:
      return record.control_x == 0 && record.control_y == 0 && record.fill0 == 0 && record.fill1 == 0 &&
             record.line == 0;
    case fo4::swf::SwfShapeRecordKind::style_change:
      return record.x == 0 && record.y == 0 && record.control_x == 0 && record.control_y == 0 &&
             std::size_t{record.fill0} <= fills && std::size_t{record.fill1} <= fills &&
             std::size_t{record.line} <= lines;
    case fo4::swf::SwfShapeRecordKind::straight_edge:
      return record.control_x == 0 && record.control_y == 0 && record.fill0 == 0 && record.fill1 == 0 &&
             record.line == 0;
    case fo4::swf::SwfShapeRecordKind::curved_edge:
      return record.fill0 == 0 && record.fill1 == 0 && record.line == 0;
  }
  return false;
}

bool shape_record_is_consistent(const fo4::swf::SwfShape& shape, const fo4::swf::SwfShapeRecord& record) {
  return record_is_consistent(record, shape.fill_styles.size(), shape.line_styles.size());
}

void check_shape_records(const fo4::swf::SwfShape& shape, MovieStats& stats) {
  std::size_t moves = 0;
  std::size_t styles = 0;
  std::size_t straight = 0;
  std::size_t curved = 0;
  for (const auto& record : shape.records) {
    ++stats.shape_records;
    if (!shape_record_is_consistent(shape, record)) {
      ++stats.art_failures;
    }
    switch (record.kind) {
      case fo4::swf::SwfShapeRecordKind::move:
        ++moves;
        break;
      case fo4::swf::SwfShapeRecordKind::style_change:
        ++styles;
        break;
      case fo4::swf::SwfShapeRecordKind::straight_edge:
        ++straight;
        break;
      case fo4::swf::SwfShapeRecordKind::curved_edge:
        ++curved;
        break;
    }
  }
  if (straight + curved != shape.edge_count || curved != shape.curved_edge_count ||
      moves > shape.style_change_count || styles > shape.style_change_count ||
      moves + styles < shape.style_change_count) {
    ++stats.art_failures;
  }
}

void check_morph_records(const fo4::swf::SwfMorphShape& morph, MovieStats& stats) {
  const auto check_stream = [&](const std::vector<fo4::swf::SwfShapeRecord>& records,
                                std::uint32_t edge_count) {
    std::size_t straight = 0;
    std::size_t curved = 0;
    for (const auto& record : records) {
      ++stats.morph_records;
      if (!record_is_consistent(record, morph.fill_styles.size(), morph.line_styles.size())) {
        ++stats.art_failures;
      }
      straight += record.kind == fo4::swf::SwfShapeRecordKind::straight_edge ? 1u : 0u;
      curved += record.kind == fo4::swf::SwfShapeRecordKind::curved_edge ? 1u : 0u;
    }
    if (straight + curved != edge_count) {
      ++stats.art_failures;
    }
  };
  check_stream(morph.start_records, morph.start_edge_count);
  check_stream(morph.end_records, morph.end_edge_count);
  const auto drawable_count = [](const std::vector<fo4::swf::SwfShapeRecord>& records) {
    return static_cast<std::size_t>(
        std::count_if(records.begin(), records.end(), [](const fo4::swf::SwfShapeRecord& record) {
          return record.kind != fo4::swf::SwfShapeRecordKind::style_change;
        }));
  };
  for (const auto& record : morph.end_records) {
    if (record.kind == fo4::swf::SwfShapeRecordKind::style_change) {
      ++stats.morph_end_style_changes;
    }
  }
  const auto start_drawables = drawable_count(morph.start_records);
  const auto end_drawables = drawable_count(morph.end_records);
  if (morph.start_records.size() != morph.end_records.size()) {
    ++stats.morph_unequal_streams;
  }
  std::size_t start_at = 0;
  std::size_t end_at = 0;
  while (start_at < morph.start_records.size() && end_at < morph.end_records.size()) {
    if (morph.start_records[start_at].kind == fo4::swf::SwfShapeRecordKind::style_change) {
      ++start_at;
      continue;
    }
    if (morph.end_records[end_at].kind == fo4::swf::SwfShapeRecordKind::style_change) {
      ++end_at;
      continue;
    }
    if (morph.start_records[start_at].kind != morph.end_records[end_at].kind) {
      ++stats.morph_kind_mismatches;
    }
    ++start_at;
    ++end_at;
  }
  const auto unpaired = start_drawables > end_drawables ? start_drawables - end_drawables : 0u;
  const auto blended = fo4::swf::interpolate_morph_commands(morph, 0.5);
  if (blended.size() != morph.start_records.size() - unpaired) {
    ++stats.art_failures;
  }
  for (std::size_t index = 0; index < blended.size(); ++index) {
    const auto& from = morph.start_records[index];
    if (from.kind == fo4::swf::SwfShapeRecordKind::style_change) {
      if (blended[index].kind != from.kind || blended[index].fill0 != from.fill0 ||
          blended[index].fill1 != from.fill1 || blended[index].line != from.line) {
        ++stats.art_failures;
      }
      continue;
    }
    if (!record_is_consistent(blended[index], morph.fill_styles.size(), morph.line_styles.size()) ||
        (blended[index].kind != from.kind &&
         blended[index].kind != fo4::swf::SwfShapeRecordKind::curved_edge)) {
      ++stats.art_failures;
    }
  }
}

bool morph_fill_pair_is_consistent(const fo4::swf::SwfMorphFillStyle& fill) {
  const bool solid = fill.type == 0x00;
  const bool gradient = fill.type == 0x10 || fill.type == 0x12 || fill.type == 0x13;
  const bool bitmap = fill.type == 0x40 || fill.type == 0x41 || fill.type == 0x42 || fill.type == 0x43;
  if (!solid && !gradient && !bitmap) {
    return false;
  }
  if (solid) {
    return !fill.start_matrix && !fill.end_matrix && fill.stops.empty() &&
           !fill.start_focal_point && !fill.end_focal_point;
  }
  if (!fill.start_matrix || !fill.end_matrix) {
    return false;
  }
  if (bitmap) {
    return fill.stops.empty() && !fill.start_focal_point && !fill.end_focal_point;
  }
  if (fill.start_focal_point.has_value() != (fill.type == 0x13) ||
      fill.end_focal_point.has_value() != (fill.type == 0x13)) {
    return false;
  }
  return fill.stops.size() <= 15;
}

bool morph_line_pair_is_consistent(const fo4::swf::SwfMorphLineStyle& line, unsigned version) {
  if (version < 2) {
    return line.flags == 0 && !line.start_miter_limit && !line.end_miter_limit && !line.fill;
  }
  const bool miter = ((line.flags >> 12) & 0x03u) == 2u;
  if (line.start_miter_limit.has_value() != miter || line.end_miter_limit.has_value() != miter) {
    return false;
  }
  const bool fill = (line.flags & 0x0800u) != 0;
  if (line.fill.has_value() != fill) {
    return false;
  }
  return !fill || (line.start_color == 0 && line.end_color == 0);
}

void check_morph_styles(const fo4::swf::SwfMorphShape& morph, MovieStats& stats) {
  if (morph.fill_styles.size() != morph.fill_style_count ||
      morph.line_styles.size() != morph.line_style_count) {
    ++stats.art_failures;
  }
  for (const auto& fill : morph.fill_styles) {
    ++stats.morph_fill_pairs;
    stats.morph_gradient_stops += fill.stops.size();
    if (!morph_fill_pair_is_consistent(fill)) {
      ++stats.art_failures;
    }
  }
  for (const auto& line : morph.line_styles) {
    ++stats.morph_line_pairs;
    stats.morph_line_fills += line.fill.has_value() ? 1u : 0u;
    if (!morph_line_pair_is_consistent(line, morph.version)) {
      ++stats.art_failures;
    }
  }
}

void check_art(const fo4::swf::SwfTag& tag, MovieStats& stats) {
  switch (tag.code) {
    case fo4::swf::tag::kDefineShape:
    case fo4::swf::tag::kDefineShape2:
    case fo4::swf::tag::kDefineShape3:
    case fo4::swf::tag::kDefineShape4:
      ++stats.shape_count;
      if (!tag.shape) {
        ++stats.art_failures;
      } else {
        check_shape_styles(*tag.shape, shape_version(tag.code), stats);
        check_shape_records(*tag.shape, stats);
      }
      break;
    case fo4::swf::tag::kDefineMorphShape:
    case fo4::swf::tag::kDefineMorphShape2:
      ++stats.morph_count;
      stats.morph2_count += tag.code == fo4::swf::tag::kDefineMorphShape2 ? 1u : 0u;
      if (!tag.morph_shape) {
        ++stats.art_failures;
      } else {
        check_morph_styles(*tag.morph_shape, stats);
        check_morph_records(*tag.morph_shape, stats);
      }
      break;
    case fo4::swf::tag::kDefineBitsLossless:
    case fo4::swf::tag::kDefineBitsLossless2:
      ++stats.bitmap_count;
      if (!tag.bitmap) ++stats.art_failures;
      break;
    case fo4::swf::tag::kDefineBits:
    case fo4::swf::tag::kDefineBitsJpeg2:
    case fo4::swf::tag::kDefineBitsJpeg3:
    case fo4::swf::tag::kDefineBitsJpeg4:
      ++stats.jpeg_count;
      if (!tag.jpeg) ++stats.art_failures;
      break;
    case fo4::swf::tag::kJpegTables:
      ++stats.jpeg_tables_count;
      if (!tag.jpeg_tables) ++stats.art_failures;
      break;
    default:
      break;
  }
  for (const auto& child : tag.children) {
    check_art(child, stats);
  }
}

void check_movie(const std::string& name, std::span<const std::byte> bytes, MovieStats& stats) {
  ++stats.total;
  const auto result = fo4::swf::read_swf(bytes);
  if (result.status == fo4::swf::SwfStatus::unsupported_compression) {
    ++stats.lzma;
    return;
  }
  EXPECT_TRUE(result.ok()) << name << ": " << fo4::swf::status_name(result.status) << " "
                           << result.message;
  if (!result.ok()) {
    return;
  }
  ++stats.movies;
  const auto summary = fo4::swf::summarize(result.file);
  stats.gfx += summary.is_gfx ? 1u : 0u;
  stats.compressed += summary.compressed ? 1u : 0u;
  stats.action_script3 += summary.action_script3 ? 1u : 0u;
  stats.symbols += summary.symbol_classes.size();
  stats.abc_blocks += summary.abc_blocks.size();
  stats.shape_movies += summary.shape_tags != 0 ? 1u : 0u;
  stats.bitmap_movies += summary.bitmap_tags != 0 ? 1u : 0u;
  stats.font_movies += summary.font_tags != 0 ? 1u : 0u;
  stats.text_movies += summary.text_tags != 0 ? 1u : 0u;
  stats.sound_movies += summary.sound_tags != 0 ? 1u : 0u;
  stats.exporter_info_movies += summary.exporter_info_tags != 0 ? 1u : 0u;
  stats.external_image_movies += summary.external_image_tags != 0 ? 1u : 0u;
  stats.font_texture_movies += summary.font_texture_tags != 0 ? 1u : 0u;
  ++stats.versions["v" + std::to_string(summary.version) + (summary.is_gfx ? "-gfx" : "-swf") +
                    (summary.compressed ? "-zlib" : "-raw")];
  for (const auto& tag : result.file.tags) {
    check_art(tag, stats);
    if (tag.code == fo4::swf::tag::kDefineEditText) {
      ++stats.edit_text_tags;
      if (tag.edit_text && tag.edit_text->layout) {
        ++stats.edit_text_layouts;
      }
    }
  }
  fo4::swf::SwfCharacterModel characters;
  std::string character_message;
  if (!fo4::swf::build_character_model(result.file, characters, character_message)) {
    ++stats.character_failures;
    EXPECT_TRUE(false) << name << ": " << character_message;
  } else {
    stats.characters += characters.characters.size();
  }
  const std::vector<std::byte> original(bytes.begin(), bytes.end());
  EXPECT_EQ(fo4::swf::write_swf(result.file), original) << name;
}

MovieStats scan_archive(const std::filesystem::path& path) {
  MovieStats stats;
  const auto archive = fo4::archive::read_ba2_general_file(path);
  for (const auto& entry : archive.entries) {
    if (!is_movie(entry.path)) {
      continue;
    }
    check_movie(entry.path, fo4::archive::extract_ba2_general_entry(entry), stats);
  }
  return stats;
}

void record_archive_art(const std::string& key, const MovieStats& stats) {
  testing::Test::RecordProperty(key + "_movies", std::to_string(stats.movies));
  testing::Test::RecordProperty(key + "_shape_tags", std::to_string(stats.shape_count));
  testing::Test::RecordProperty(key + "_shape_fill_styles", std::to_string(stats.shape_fill_styles));
  testing::Test::RecordProperty(key + "_shape_line_fills", std::to_string(stats.shape_line_fills));
  testing::Test::RecordProperty(key + "_morph_tags", std::to_string(stats.morph_count));
  testing::Test::RecordProperty(key + "_morph2_tags", std::to_string(stats.morph2_count));
  testing::Test::RecordProperty(key + "_morph_fill_pairs", std::to_string(stats.morph_fill_pairs));
  testing::Test::RecordProperty(key + "_morph_line_pairs", std::to_string(stats.morph_line_pairs));
  testing::Test::RecordProperty(key + "_lossless_tags", std::to_string(stats.bitmap_count));
  testing::Test::RecordProperty(key + "_art_failures", std::to_string(stats.art_failures));
}

std::string version_histogram(const MovieStats& stats) {
  std::string text;
  for (const auto& [key, count] : stats.versions) {
    if (!text.empty()) {
      text += ' ';
    }
    text += key + "=" + std::to_string(count);
  }
  return text;
}

struct SvgStats {
  std::size_t shapes{0};
  std::size_t failures{0};
};

bool svg_is_well_formed(const std::string& markup) {
  std::string svg = markup;
  for (auto at = svg.find("base64,"); at != std::string::npos; at = svg.find("base64,", at)) {
    const auto end = svg.find('"', at);
    svg.erase(at, end == std::string::npos ? end : end - at);
  }
  if (svg.find("nan") != std::string::npos || svg.find("inf") != std::string::npos) {
    return false;
  }
  std::vector<std::string> open;
  for (std::size_t at = svg.find('<'); at != std::string::npos; at = svg.find('<', at)) {
    const auto close = svg.find('>', at);
    if (close == std::string::npos) {
      return false;
    }
    const std::string tag = svg.substr(at + 1, close - at - 1);
    at = close;
    if (tag.empty()) {
      return false;
    }
    if (tag.front() == '/') {
      if (open.empty() || open.back() != tag.substr(1)) {
        return false;
      }
      open.pop_back();
    } else if (tag.back() != '/') {
      open.push_back(tag.substr(0, tag.find_first_of(" />")));
    }
  }
  return open.empty();
}

void check_svg_tag(const std::string& name, const fo4::swf::SwfFile& file,
                   const fo4::swf::SwfTag& tag, SvgStats& stats) {
  if (tag.shape) {
    ++stats.shapes;
    if (!svg_is_well_formed(fo4::swf::shape_to_svg(*tag.shape, file))) {
      ++stats.failures;
      ADD_FAILURE() << name << ": shape " << tag.shape->shape_id << " exports malformed SVG";
    }
  }
  for (const auto& child : tag.children) {
    check_svg_tag(name, file, child, stats);
  }
}

void check_svg_movie(const std::string& name, std::span<const std::byte> bytes, SvgStats& stats) {
  const auto result = fo4::swf::read_swf(bytes);
  if (result.status == fo4::swf::SwfStatus::unsupported_compression) {
    return;
  }
  EXPECT_TRUE(result.ok()) << name << ": " << fo4::swf::status_name(result.status);
  if (!result.ok()) {
    return;
  }
  for (const auto& tag : result.file.tags) {
    check_svg_tag(name, result.file, tag, stats);
  }
}

}  // namespace

TEST(SwfRealData, ParsesEveryShippedMovieAndRoundTripsByteIdentically) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder";
  }
  record_game_build(detect_game_build(dir));
  const std::filesystem::path data_dir(dir);

  MovieStats stats;
  const auto interface_dir = data_dir / "Interface";
  std::error_code error;
  if (std::filesystem::is_directory(interface_dir, error)) {
    for (const auto& entry : std::filesystem::directory_iterator(interface_dir, error)) {
      if (!entry.is_regular_file(error)) {
        continue;
      }
      const auto name = entry.path().filename().string();
      if (!is_movie(name)) {
        continue;
      }
      check_movie(name, read_file(entry.path()), stats);
    }
  }

  if (stats.movies == 0 && stats.lzma == 0) {
    const auto archive_path = data_dir / "Fallout4 - Interface.ba2";
    if (!std::filesystem::exists(archive_path)) {
      GTEST_SKIP() << "no Interface movies and no Fallout4 - Interface.ba2 in FO4_DATA_DIR";
    }
    const auto interface = scan_archive(archive_path);
    record_archive_art("interface", interface);
    stats += interface;
  }

  struct DlcArchive {
    const char* key;
    const char* file;
  };
  const DlcArchive dlc_archives[] = {
      {"dlccoast", "DLCCoast - Main.ba2"},
      {"dlcnukaworld", "DLCNukaWorld - Main.ba2"},
      {"dlcrobot", "DLCRobot - Main.ba2"},
  };
  for (const auto& dlc : dlc_archives) {
    const auto path = data_dir / dlc.file;
    if (!std::filesystem::exists(path)) {
      testing::Test::RecordProperty(std::string(dlc.key) + "_movies", std::string{"absent"});
      continue;
    }
    const auto local = scan_archive(path);
    record_archive_art(dlc.key, local);
    EXPECT_EQ(local.art_failures, 0u) << dlc.file;
    EXPECT_GT(local.movies + local.lzma, 0u) << dlc.file;
    stats += local;
  }

  EXPECT_GT(stats.movies, 0u);
  EXPECT_EQ(stats.movies + stats.lzma, stats.total);
  // Regression: every shipped shape, morph, bitmap and JPEG tag must decode.
  EXPECT_EQ(stats.art_failures, 0u);
  EXPECT_EQ(stats.character_failures, 0u) << "character_model_failures";
  EXPECT_GT(stats.edit_text_tags, 0u) << "edit_text_tags";
  EXPECT_GT(stats.shape_count, 0u);
  EXPECT_GT(stats.bitmap_count, 0u);
  EXPECT_GT(stats.morph_fill_pairs, 0u);
  EXPECT_GT(stats.morph_records, 0u);
  EXPECT_GT(stats.shape_fill_styles, 0u);
  EXPECT_GT(stats.shape_line_styles, 0u);
  EXPECT_GT(stats.shape_records, 0u);
  RecordProperty("shape_tags", std::to_string(stats.shape_count));
  RecordProperty("shape_fill_styles", std::to_string(stats.shape_fill_styles));
  RecordProperty("shape_line_styles", std::to_string(stats.shape_line_styles));
  RecordProperty("shape_gradient_stops", std::to_string(stats.shape_gradient_stops));
  RecordProperty("shape_line_fills", std::to_string(stats.shape_line_fills));
  RecordProperty("shape_records", std::to_string(stats.shape_records));
  RecordProperty("morph_tags", std::to_string(stats.morph_count));
  RecordProperty("morph2_tags", std::to_string(stats.morph2_count));
  RecordProperty("morph_fill_pairs", std::to_string(stats.morph_fill_pairs));
  RecordProperty("morph_line_pairs", std::to_string(stats.morph_line_pairs));
  RecordProperty("morph_gradient_stops", std::to_string(stats.morph_gradient_stops));
  RecordProperty("morph_line_fills", std::to_string(stats.morph_line_fills));
  RecordProperty("morph_records", std::to_string(stats.morph_records));
  RecordProperty("morph_unequal_streams", std::to_string(stats.morph_unequal_streams));
  RecordProperty("morph_kind_mismatches", std::to_string(stats.morph_kind_mismatches));
  RecordProperty("morph_end_style_changes", std::to_string(stats.morph_end_style_changes));
  RecordProperty("lossless_tags", std::to_string(stats.bitmap_count));
  RecordProperty("jpeg_tags", std::to_string(stats.jpeg_count));
  RecordProperty("jpeg_tables_tags", std::to_string(stats.jpeg_tables_count));
  RecordProperty("art_failures", std::to_string(stats.art_failures));
  RecordProperty("characters", std::to_string(stats.characters));
  RecordProperty("edit_text_tags", std::to_string(stats.edit_text_tags));
  RecordProperty("edit_text_layouts", std::to_string(stats.edit_text_layouts));
  RecordProperty("character_model_failures", std::to_string(stats.character_failures));
  RecordProperty("movies", std::to_string(stats.movies));
  RecordProperty("gfx_movies", std::to_string(stats.gfx));
  RecordProperty("compressed_movies", std::to_string(stats.compressed));
  RecordProperty("action_script3_movies", std::to_string(stats.action_script3));
  RecordProperty("lzma_movies", std::to_string(stats.lzma));
  RecordProperty("symbol_classes", std::to_string(stats.symbols));
  RecordProperty("abc_blocks", std::to_string(stats.abc_blocks));
  RecordProperty("shape_movies", std::to_string(stats.shape_movies));
  RecordProperty("bitmap_movies", std::to_string(stats.bitmap_movies));
  RecordProperty("font_movies", std::to_string(stats.font_movies));
  RecordProperty("text_movies", std::to_string(stats.text_movies));
  RecordProperty("sound_movies", std::to_string(stats.sound_movies));
  RecordProperty("exporter_info_movies", std::to_string(stats.exporter_info_movies));
  RecordProperty("external_image_movies", std::to_string(stats.external_image_movies));
  RecordProperty("font_texture_movies", std::to_string(stats.font_texture_movies));
  RecordProperty("version_histogram", version_histogram(stats));
}

TEST(SwfRealData, InterfaceCorpusDoActionCountAndNoLzmaCompression) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder";
  }
  record_game_build(detect_game_build(dir));
  const auto archive_path =
      std::filesystem::path(dir) / "Fallout4 - Interface.ba2";
  if (!std::filesystem::exists(archive_path)) {
    GTEST_SKIP() << "no Fallout4 - Interface.ba2 in FO4_DATA_DIR";
  }
  const auto archive = fo4::archive::read_ba2_general_file(archive_path);
  std::size_t movies = 0;
  std::size_t lzma_movies = 0;
  std::size_t do_action_tags = 0;
  for (const auto& entry : archive.entries) {
    if (!is_movie(entry.path)) {
      continue;
    }
    ++movies;
    const auto bytes = fo4::archive::extract_ba2_general_entry(entry);
    if (bytes.size() >= 3 && bytes[0] == static_cast<std::byte>('Z') &&
        bytes[1] == static_cast<std::byte>('W') && bytes[2] == static_cast<std::byte>('S')) {
      ++lzma_movies;
      continue;
    }
    const auto read = fo4::swf::read_swf(bytes);
    if (!read.ok()) {
      continue;
    }
    const auto count_do_action = [](const auto& self, const auto& tags) -> std::size_t {
      std::size_t total = 0;
      for (const auto& tag : tags) {
        if (tag.code == fo4::swf::tag::kDoAction) {
          ++total;
        }
        total += self(self, tag.children);
      }
      return total;
    };
    do_action_tags += count_do_action(count_do_action, read.file.tags);
  }
  RecordProperty("movies", std::to_string(movies));
  RecordProperty("lzma_movies", std::to_string(lzma_movies));
  RecordProperty("do_action_tags", std::to_string(do_action_tags));
  EXPECT_GT(movies, 0u);
  EXPECT_EQ(lzma_movies, 0u);
  EXPECT_EQ(do_action_tags, 82u);
}

TEST(SwfRealData, ExportsEveryShapeToWellFormedSvg) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder";
  }
  const auto archive_path = std::filesystem::path(dir) / "Fallout4 - Interface.ba2";
  if (!std::filesystem::exists(archive_path)) {
    GTEST_SKIP() << "no Fallout4 - Interface.ba2 in FO4_DATA_DIR";
  }
  const auto archive = fo4::archive::read_ba2_general_file(archive_path);
  SvgStats stats;
  for (const auto& entry : archive.entries) {
    if (is_movie(entry.path)) {
      check_svg_movie(entry.path, fo4::archive::extract_ba2_general_entry(entry), stats);
    }
  }
  RecordProperty("svg_shapes", std::to_string(stats.shapes));
  RecordProperty("svg_failures", std::to_string(stats.failures));
  EXPECT_GT(stats.shapes, 0u);
  EXPECT_EQ(stats.failures, 0u);
}
