#include <fo4/avm2/swf_runtime.hpp>

#include <fo4/avm2/display.hpp>
#include <fo4/avm2/fonts.hpp>
#include <fo4/swf/swf.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace fo4::avm2 {

namespace {

std::string tag_string(std::span<const std::byte> payload) {
  std::string text;
  for (const std::byte raw : payload) {
    const char character = static_cast<char>(static_cast<unsigned char>(raw));
    if (character == '\0') {
      break;
    }
    text.push_back(character);
  }
  return text;
}

double fixed_to_double(std::int32_t value) {
  return static_cast<double>(value) / 65536.0;
}

double twips_to_pixels(std::int32_t value) {
  return static_cast<double>(value) / 20.0;
}

SwfPlacementRow placement_row(const swf::SwfPlacement& placement) {
  SwfPlacementRow row;
  row.kind = SwfPlacementRow::Kind::Place;
  row.depth = placement.depth;
  row.character_id = placement.character_id.value_or(0);
  row.name = placement.name.value_or(std::string{});
  if (placement.matrix.has_value()) {
    const swf::SwfMatrix& matrix = *placement.matrix;
    row.scale_x = fixed_to_double(matrix.scale_x);
    row.scale_y = fixed_to_double(matrix.scale_y);
    row.translate_x = twips_to_pixels(matrix.translate_x);
    row.translate_y = twips_to_pixels(matrix.translate_y);
  }
  if (placement.clip_depth.has_value()) {
    row.has_clip_depth = true;
    row.clip_depth = *placement.clip_depth;
  }
  return row;
}

void read_symbol_classes(std::span<const std::byte> payload,
                         std::map<std::uint16_t, std::string>& classes,
                         std::string& root_class_name) {
  if (payload.size() < 2) {
    return;
  }
  const std::size_t count = static_cast<std::size_t>(payload[0]) |
                            (static_cast<std::size_t>(payload[1]) << 8u);
  std::size_t offset = 2;
  for (std::size_t index = 0; index < count && offset + 2 <= payload.size(); ++index) {
    const std::uint16_t character = static_cast<std::uint16_t>(
        static_cast<unsigned char>(payload[offset]) |
        (static_cast<unsigned char>(payload[offset + 1]) << 8u));
    offset += 2;
    std::string name;
    while (offset < payload.size() && payload[offset] != std::byte{0}) {
      name.push_back(static_cast<char>(static_cast<unsigned char>(payload[offset])));
      ++offset;
    }
    ++offset;
    classes[character] = name;
    if (character == 0) {
      root_class_name = name;
    }
  }
}

void collect_timeline(const std::vector<swf::SwfTag>& tags, std::uint32_t declared_frames,
                      std::size_t depth, const SwfRuntimeOptions& options, SwfMovieData& data,
                      SwfTimelineRows& out) {
  std::uint32_t frame_index = 0;
  out.frames.emplace_back();
  for (const swf::SwfTag& tag : tags) {
    switch (tag.code) {
      case swf::tag::kPlaceObject:
      case swf::tag::kPlaceObject2:
      case swf::tag::kPlaceObject3: {
        if (tag.placement.has_value()) {
          out.frames[frame_index].push_back(placement_row(*tag.placement));
        }
        break;
      }
      case swf::tag::kRemoveObject:
      case swf::tag::kRemoveObject2: {
        if (tag.removal.has_value()) {
          SwfPlacementRow row;
          row.kind = SwfPlacementRow::Kind::Remove;
          row.depth = tag.removal->depth;
          out.frames[frame_index].push_back(row);
        }
        break;
      }
      case swf::tag::kShowFrame: {
        ++frame_index;
        out.frames.emplace_back();
        break;
      }
      case swf::tag::kFrameLabel: {
        TimelineFrameLabel label;
        label.name = tag_string(tag.payload);
        label.frame = frame_index + 1;
        if (!label.name.empty()) {
          out.labels.push_back(std::move(label));
        }
        break;
      }
      case swf::tag::kDefineFont2:
      case swf::tag::kDefineFont3: {
        if (tag.font.has_value()) {
          FontResource font;
          font.character_id = tag.font->font_id;
          font.name = tag.font->name;
          font.units_per_em = tag.font->units_per_em;
          font.has_layout = tag.font->has_layout;
          font.glyphs.resize(tag.font->glyph_count);
          for (std::size_t index = 0; index < font.glyphs.size(); ++index) {
            font.glyphs[index].glyph_id = static_cast<std::uint16_t>(index);
            if (index < tag.font->glyph_codes.size()) {
              font.glyphs[index].code = tag.font->glyph_codes[index];
            }
            if (index < tag.font->advances.size()) {
              font.glyphs[index].advance = tag.font->advances[index];
            }
          }
          data.fonts.push_back(std::move(font));
        }
        break;
      }
      case swf::tag::kSymbolClass: {
        read_symbol_classes(tag.payload, data.symbol_classes, data.root_class_name);
        break;
      }
      case swf::tag::kDefineSprite: {
        if (!tag.sprite_id.has_value() || depth >= options.max_sprite_depth) {
          break;
        }
        SwfTimelineRows sprite_rows;
        collect_timeline(tag.children, tag.sprite_frame_count.value_or(0), depth + 1, options,
                         data, sprite_rows);
        data.sprites[*tag.sprite_id] = std::move(sprite_rows);
        break;
      }
      default:
        break;
    }
  }
  out.frame_count = std::max<std::uint32_t>(declared_frames, frame_index);
  if (out.frames.size() > out.frame_count) {
    out.frames.resize(out.frame_count);
  }
  while (out.frames.size() < out.frame_count) {
    out.frames.emplace_back();
  }
  if (out.frame_count == 0) {
    out.frame_count = 1;
    out.frames.emplace_back();
  }
}

SwfTimelineRows collect_root(const swf::SwfFile& file, const SwfRuntimeOptions& options,
                             SwfMovieData& data) {
  SwfTimelineRows rows;
  collect_timeline(file.tags, file.header.frame_count, 0, options, data, rows);
  return rows;
}

}  // namespace

std::shared_ptr<SwfMovieData> read_swf_movie_data(const swf::SwfFile& file,
                                                  const SwfRuntimeOptions& options) {
  auto data = std::make_shared<SwfMovieData>();
  if (file.tags.size() > options.max_tags) {
    return nullptr;
  }
  data->frame_rate = file.header.frame_rate_fps();
  data->stage_width = twips_to_pixels(file.header.frame_size.x_max - file.header.frame_size.x_min);
  data->stage_height = twips_to_pixels(file.header.frame_size.y_max - file.header.frame_size.y_min);
  data->root = collect_root(file, options, *data);
  return data;
}

}  // namespace fo4::avm2
