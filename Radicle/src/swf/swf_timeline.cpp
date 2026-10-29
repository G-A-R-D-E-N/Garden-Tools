#include "fo4/swf/swf_timeline.hpp"

#include "swf_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace fo4::swf {

namespace {

bool is_shape_code(std::uint16_t code) {
  return code == tag::kDefineShape || code == tag::kDefineShape2 || code == tag::kDefineShape3 ||
         code == tag::kDefineShape4;
}

bool is_morph_code(std::uint16_t code) {
  return code == tag::kDefineMorphShape || code == tag::kDefineMorphShape2;
}

bool is_image_code(std::uint16_t code) {
  return code == tag::kDefineBits || code == tag::kDefineBitsJpeg2 ||
         code == tag::kDefineBitsJpeg3 || code == tag::kDefineBitsJpeg4 ||
         code == tag::kDefineBitsLossless || code == tag::kDefineBitsLossless2;
}

std::optional<std::uint16_t> leading_character_id(const SwfTag& tag) {
  detail::SwfReader reader(tag.payload, 0);
  std::uint16_t value = 0;
  if (!reader.read_u16(value)) {
    return std::nullopt;
  }
  return value;
}

std::string character_kind(std::uint16_t code) {
  if (is_shape_code(code)) {
    return "shape";
  }
  if (code == tag::kDefineSprite) {
    return "sprite";
  }
  if (is_morph_code(code)) {
    return "morph shape";
  }
  if (is_image_code(code)) {
    return "image";
  }
  if (code == tag::kDefineEditText) {
    return "text field";
  }
  if (code == tag::kDefineButton || code == tag::kDefineButton2) {
    return "button";
  }
  if (code == tag::kDefineText || code == tag::kDefineText2) {
    return "text";
  }
  if (code == tag::kDefineVideoStream) {
    return "video";
  }
  return "undefined";
}

std::string frame_label_name(const SwfTag& tag) {
  detail::SwfReader reader(tag.payload, 0);
  std::string name;
  if (!reader.read_zstring(name)) {
    return std::string{};
  }
  return name;
}

std::string clean_text(std::string_view value) {
  std::string out;
  out.reserve(value.size());
  for (const char ch : value) {
    if (ch == '\r') {
      out += "\\r";
    } else if (ch == '\n') {
      out += "\\n";
    } else if (ch == '\t') {
      out += "\\t";
    } else {
      out.push_back(ch);
    }
  }
  return out;
}

void append_u_escape(std::string& out, std::uint32_t code_point) {
  constexpr char digits[] = "0123456789ABCDEF";
  out += "\\u";
  out.push_back(digits[(code_point >> 12) & 0xFu]);
  out.push_back(digits[(code_point >> 8) & 0xFu]);
  out.push_back(digits[(code_point >> 4) & 0xFu]);
  out.push_back(digits[code_point & 0xFu]);
}

bool escapes_as_u(std::uint32_t code_point) {
  return code_point == 0x26u || code_point == 0x27u || code_point == 0x2Bu ||
         code_point == 0x3Cu || code_point == 0x3Eu;
}

std::string quote_text(std::string_view value) {
  std::string out;
  out.push_back('"');
  std::size_t index = 0;
  while (index < value.size()) {
    const auto lead = static_cast<unsigned char>(value[index]);
    std::uint32_t code_point = 0xFFFDu;
    std::size_t length = 1;
    if (lead < 0x80u) {
      code_point = lead;
    } else if ((lead & 0xE0u) == 0xC0u && index + 1 < value.size()) {
      code_point = ((lead & 0x1Fu) << 6) |
                   (static_cast<unsigned char>(value[index + 1]) & 0x3Fu);
      length = 2;
    } else if ((lead & 0xF0u) == 0xE0u && index + 2 < value.size()) {
      code_point = ((lead & 0x0Fu) << 12) |
                   ((static_cast<unsigned char>(value[index + 1]) & 0x3Fu) << 6) |
                   (static_cast<unsigned char>(value[index + 2]) & 0x3Fu);
      length = 3;
    } else if ((lead & 0xF8u) == 0xF0u && index + 3 < value.size()) {
      code_point = ((lead & 0x07u) << 18) |
                   ((static_cast<unsigned char>(value[index + 1]) & 0x3Fu) << 12) |
                   ((static_cast<unsigned char>(value[index + 2]) & 0x3Fu) << 6) |
                   (static_cast<unsigned char>(value[index + 3]) & 0x3Fu);
      length = 4;
    }
    index += length;
    if (code_point == 0x22u) {
      out += "\\\"";
    } else if (code_point == 0x5Cu) {
      out += "\\\\";
    } else if (code_point == 0x08u) {
      out += "\\b";
    } else if (code_point == 0x09u) {
      out += "\\t";
    } else if (code_point == 0x0Au) {
      out += "\\n";
    } else if (code_point == 0x0Cu) {
      out += "\\f";
    } else if (code_point == 0x0Du) {
      out += "\\r";
    } else if (code_point < 0x20u || escapes_as_u(code_point)) {
      append_u_escape(out, code_point);
    } else if (code_point >= 0x7Fu) {
      if (code_point >= 0x10000u) {
        const std::uint32_t offset = code_point - 0x10000u;
        append_u_escape(out, 0xD800u + (offset >> 10));
        append_u_escape(out, 0xDC00u + (offset & 0x3FFu));
      } else {
        append_u_escape(out, code_point);
      }
    } else {
      out.push_back(static_cast<char>(code_point));
    }
  }
  out.push_back('"');
  return out;
}

std::string format_number(double value) {
  const double rounded = std::nearbyint(value * 10.0) / 10.0;
  const double fixed = rounded == 0.0 ? 0.0 : rounded;
  std::ostringstream stream;
  stream << std::fixed << std::setprecision(1) << fixed;
  std::string text = stream.str();
  const auto dot = text.find('.');
  if (dot != std::string::npos && text.size() > dot + 2) {
    text.erase(dot + 2);
  }
  if (text.size() >= 2 && text.compare(text.size() - 2, 2, ".0") == 0) {
    text.erase(text.size() - 2);
  }
  return text;
}

bool build_timeline(const std::vector<SwfTag>& tags, std::uint16_t character_id,
                    std::uint32_t declared_frames, bool require_declared, SwfTimeline& out,
                    std::string& message) {
  out = {};
  out.character_id = character_id;
  std::map<std::uint16_t, SwfDisplayObject> active;
  std::uint32_t frames = 0;
  for (std::size_t tag_index = 0; tag_index < tags.size(); ++tag_index) {
    const SwfTag& tag = tags[tag_index];
    switch (tag.code) {
      case tag::kShowFrame: {
        SwfTimelineFrame state;
        state.objects.reserve(active.size());
        for (const auto& entry : active) {
          state.objects.push_back(entry.second);
        }
        out.frames.push_back(std::move(state));
        ++frames;
        break;
      }
      case tag::kFrameLabel: {
        out.labels.push_back(frame_label_name(tag) + "@" + std::to_string(frames + 1));
        break;
      }
      case tag::kRemoveObject:
      case tag::kRemoveObject2: {
        if (!tag.removal) {
          message = "removal payload did not decode";
          return false;
        }
        active.erase(tag.removal->depth);
        break;
      }
      case tag::kPlaceObject:
      case tag::kPlaceObject2:
      case tag::kPlaceObject3: {
        if (!tag.placement) {
          message = "placement payload did not decode";
          return false;
        }
        const SwfPlacement& placement = *tag.placement;
        if (placement.class_name && !placement.character_id) {
          message = "class-only placement requires runtime resolution";
          return false;
        }
        SwfDisplayObject object;
        if (placement.move) {
          const auto existing = active.find(placement.depth);
          if (existing == active.end()) {
            message = "move targets a depth that is not live";
            return false;
          }
          object = existing->second;
        }
        if (placement.character_id) {
          object.character_id = *placement.character_id;
        }
        if (placement.matrix) {
          object.transform = transform_from_matrix(*placement.matrix);
        }
        if (placement.name) {
          object.name = *placement.name;
        }
        if (placement.ratio) {
          object.ratio = placement.ratio;
        }
        if (placement.clip_depth) {
          object.clip_depth = placement.clip_depth;
        }
        object.depth = placement.depth;
        active[placement.depth] = object;
        if (object.character_id == 0) {
          break;
        }
        SwfPlacementRow row;
        row.tag_index = tag_index;
        row.frame = frames + 1;
        row.depth = object.depth;
        row.character_id = object.character_id;
        row.name = object.name;
        row.transform = object.transform;
        out.placements.push_back(std::move(row));
        break;
      }
      default:
        break;
    }
  }
  if (require_declared && frames != declared_frames) {
    message = "timeline frame count does not match its declaration";
    return false;
  }
  out.declared_frames = require_declared ? declared_frames : frames;
  return true;
}

struct MovieIndex {
  std::map<std::uint16_t, std::string> kinds;
  std::map<std::uint16_t, SwfRect> shape_bounds;
  std::map<std::uint16_t, const SwfEditText*> edit_texts;
};

MovieIndex build_index(const SwfFile& file) {
  MovieIndex index;
  for (const SwfTag& tag : file.tags) {
    std::optional<std::uint16_t> character_id;
    if (tag.shape) {
      character_id = tag.shape->shape_id;
      index.shape_bounds.emplace(tag.shape->shape_id, tag.shape->bounds);
    } else if (tag.sprite_id) {
      character_id = tag.sprite_id;
    } else if (tag.morph_shape) {
      character_id = tag.morph_shape->character_id;
    } else if (tag.bitmap) {
      character_id = tag.bitmap->character_id;
    } else if (tag.jpeg) {
      character_id = tag.jpeg->character_id;
    } else if (tag.text) {
      character_id = tag.text->character_id;
    } else if (tag.edit_text) {
      character_id = tag.edit_text->character_id;
      index.edit_texts.emplace(tag.edit_text->character_id, &*tag.edit_text);
    } else if (tag.code == tag::kDefineButton || tag.code == tag::kDefineButton2 ||
               tag.code == tag::kDefineVideoStream) {
      character_id = leading_character_id(tag);
    }
    if (character_id) {
      index.kinds.emplace(*character_id, character_kind(tag.code));
    }
  }
  return index;
}

struct WalkRow {
  std::string path;
  std::string name;
  std::uint16_t depth{0};
  std::uint16_t character_id{0};
  std::uint32_t frame{1};
  SwfTransform transform{};
  std::string kind;
};

void walk_placements(const SwfMovie& movie, const MovieIndex& index,
                     const std::vector<SwfPlacementRow>& placements, const std::string& path,
                     const SwfTransform& parent, const SwfInspectionOptions& options,
                     std::vector<std::uint16_t>& stack, std::size_t& visits,
                     std::vector<WalkRow>& out) {
  std::vector<std::tuple<std::uint16_t, std::string, std::uint16_t>> seen;
  for (const SwfPlacementRow& placement : placements) {
    if (visits >= options.max_rows) {
      return;
    }
    const auto key = std::make_tuple(placement.depth, placement.name, placement.character_id);
    if (std::find(seen.begin(), seen.end(), key) != seen.end()) {
      continue;
    }
    seen.push_back(key);
    ++visits;
    const SwfTransform here = compose_transform(parent, placement.transform);
    const auto kind_entry = index.kinds.find(placement.character_id);
    WalkRow row;
    row.path = path;
    row.name = placement.name;
    row.depth = placement.depth;
    row.character_id = placement.character_id;
    row.frame = placement.frame;
    row.transform = here;
    row.kind = kind_entry == index.kinds.end() ? "undefined" : kind_entry->second;
    out.push_back(std::move(row));
    if (out.back().kind != "sprite" || stack.size() >= options.max_depth) {
      continue;
    }
    const auto sprite = movie.sprites.find(placement.character_id);
    if (sprite == movie.sprites.end() ||
        std::find(stack.begin(), stack.end(), placement.character_id) != stack.end()) {
      continue;
    }
    stack.push_back(placement.character_id);
    walk_placements(movie, index, sprite->second.placements,
                    path + (placement.name.empty() ? "#" + std::to_string(placement.character_id)
                                                   : placement.name) +
                        "/",
                    here, options, stack, visits, out);
    stack.pop_back();
  }
}

std::string text_field_detail(const SwfEditText& field, const SwfTransform& transform) {
  std::string detail;
  const SwfBounds box = transformed_bounds(transform, field.bounds);
  detail += ", rect (" + format_number(box.x) + ", " + format_number(box.y) + ", " +
            format_number(box.width) + " x " + format_number(box.height) + ")";
  if (!field.text.empty()) {
    detail += ", text " + quote_text(field.text);
  }
  if (!field.variable.empty()) {
    detail += ", variable " + quote_text(field.variable);
  }
  if (field.font_height_twips) {
    detail += ", " + format_number(static_cast<double>(*field.font_height_twips) / 20.0) + "pt";
  }
  std::vector<std::string> flags;
  if ((field.flags1 & 0x40u) != 0) {
    flags.emplace_back("wordwrap");
  }
  if ((field.flags1 & 0x20u) != 0) {
    flags.emplace_back("multiline");
  }
  if ((field.flags1 & 0x10u) != 0) {
    flags.emplace_back("password");
  }
  if ((field.flags1 & 0x08u) != 0) {
    flags.emplace_back("readonly");
  }
  if ((field.flags2 & 0x40u) != 0) {
    flags.emplace_back("autosize");
  }
  if ((field.flags2 & 0x02u) != 0) {
    flags.emplace_back("html");
  }
  for (std::size_t index = 0; index < flags.size(); ++index) {
    detail += index == 0 ? ", " : "/";
    detail += flags[index];
  }
  return detail;
}

}  // namespace

SwfTransform transform_from_matrix(const SwfMatrix& matrix) noexcept {
  SwfTransform transform;
  transform.scale_x = static_cast<double>(matrix.scale_x) / 65536.0;
  transform.scale_y = static_cast<double>(matrix.scale_y) / 65536.0;
  transform.rotate_skew0 = static_cast<double>(matrix.rotate_skew0) / 65536.0;
  transform.rotate_skew1 = static_cast<double>(matrix.rotate_skew1) / 65536.0;
  transform.translate_x = static_cast<double>(matrix.translate_x) / 20.0;
  transform.translate_y = static_cast<double>(matrix.translate_y) / 20.0;
  return transform;
}

SwfTransform compose_transform(const SwfTransform& parent, const SwfTransform& child) noexcept {
  SwfTransform out;
  out.scale_x = parent.scale_x * child.scale_x + parent.rotate_skew1 * child.rotate_skew0;
  out.rotate_skew0 = parent.rotate_skew0 * child.scale_x + parent.scale_y * child.rotate_skew0;
  out.rotate_skew1 = parent.scale_x * child.rotate_skew1 + parent.rotate_skew1 * child.scale_y;
  out.scale_y = parent.rotate_skew0 * child.rotate_skew1 + parent.scale_y * child.scale_y;
  out.translate_x =
      parent.scale_x * child.translate_x + parent.rotate_skew1 * child.translate_y + parent.translate_x;
  out.translate_y =
      parent.rotate_skew0 * child.translate_x + parent.scale_y * child.translate_y + parent.translate_y;
  return out;
}

SwfBounds transformed_bounds(const SwfTransform& transform, const SwfRect& bounds) noexcept {
  const double x0 = static_cast<double>(bounds.x_min) / 20.0;
  const double x1 = static_cast<double>(bounds.x_max) / 20.0;
  const double y0 = static_cast<double>(bounds.y_min) / 20.0;
  const double y1 = static_cast<double>(bounds.y_max) / 20.0;
  const double xs[4] = {
      transform.scale_x * x0 + transform.rotate_skew1 * y0 + transform.translate_x,
      transform.scale_x * x1 + transform.rotate_skew1 * y0 + transform.translate_x,
      transform.scale_x * x0 + transform.rotate_skew1 * y1 + transform.translate_x,
      transform.scale_x * x1 + transform.rotate_skew1 * y1 + transform.translate_x};
  const double ys[4] = {
      transform.rotate_skew0 * x0 + transform.scale_y * y0 + transform.translate_y,
      transform.rotate_skew0 * x1 + transform.scale_y * y0 + transform.translate_y,
      transform.rotate_skew0 * x0 + transform.scale_y * y1 + transform.translate_y,
      transform.rotate_skew0 * x1 + transform.scale_y * y1 + transform.translate_y};
  const double min_x = std::min(std::min(xs[0], xs[1]), std::min(xs[2], xs[3]));
  const double max_x = std::max(std::max(xs[0], xs[1]), std::max(xs[2], xs[3]));
  const double min_y = std::min(std::min(ys[0], ys[1]), std::min(ys[2], ys[3]));
  const double max_y = std::max(std::max(ys[0], ys[1]), std::max(ys[2], ys[3]));
  SwfBounds out;
  out.x = min_x;
  out.y = min_y;
  out.width = max_x - min_x;
  out.height = max_y - min_y;
  return out;
}

SwfMovie build_movie(const SwfFile& file) {
  SwfMovie movie;
  movie.root.character_id = 0;
  if (!build_timeline(file.tags, 0, file.header.frame_count, true, movie.root, movie.message)) {
    movie.status = SwfTimelineStatus::malformed;
    return movie;
  }
  for (const SwfTag& tag : file.tags) {
    if (tag.code != tag::kDefineSprite || !tag.sprite_id) {
      continue;
    }
    SwfTimeline timeline;
    const std::uint32_t declared = tag.sprite_frame_count.value_or(0);
    if (!build_timeline(tag.children, *tag.sprite_id, declared, false, timeline, movie.message)) {
      movie.status = SwfTimelineStatus::malformed;
      return movie;
    }
    if (!movie.sprites.emplace(*tag.sprite_id, std::move(timeline)).second) {
      movie.message = "duplicate sprite definition";
      movie.status = SwfTimelineStatus::malformed;
      return movie;
    }
  }
  movie.status = SwfTimelineStatus::ok;
  return movie;
}

SwfCensus census(const SwfFile& file) {
  SwfCensus counts;
  for (const SwfTag& tag : file.tags) {
    if (is_shape_code(tag.code)) {
      ++counts.shapes;
    } else if (tag.code == tag::kDefineSprite) {
      ++counts.sprites;
    } else if (is_morph_code(tag.code)) {
      ++counts.morphs;
    } else if (is_image_code(tag.code)) {
      ++counts.images;
    } else if (tag.code == tag::kDefineEditText) {
      ++counts.text_fields;
    } else if (tag.code == tag::kDefineButton || tag.code == tag::kDefineButton2) {
      ++counts.buttons;
    } else if (tag.code == tag::kDefineVideoStream) {
      ++counts.videos;
    } else if (tag.code == tag::kSymbolClass && tag.symbols) {
      counts.symbols += static_cast<std::uint32_t>(tag.symbols->size());
    } else if (tag.code == tag::kImportAssets || tag.code == tag::kImportAssets2) {
      ++counts.imports;
    }
  }
  counts.root_frames = file.header.frame_count;
  const SwfMovie movie = build_movie(file);
  if (movie.status == SwfTimelineStatus::ok) {
    counts.root_labels = static_cast<std::uint32_t>(movie.root.labels.size());
    for (const auto& entry : movie.sprites) {
      counts.sprite_labels += static_cast<std::uint32_t>(entry.second.labels.size());
      if (!entry.second.labels.empty()) {
        ++counts.labelled_sprite_definitions;
      }
    }
  }
  return counts;
}

std::string inspect_swf(const SwfFile& file, std::string_view name,
                        const SwfInspectionOptions& options) {
  const SwfMovie movie = build_movie(file);
  std::string out;
  out += "## ";
  out += clean_text(name);
  out += "\n";
  if (movie.status != SwfTimelineStatus::ok) {
    out += "(unreadable movie: " + movie.message + ")\n";
    return out;
  }
  const SwfCensus counts = census(file);
  const double stage_width =
      static_cast<double>(file.header.frame_size.x_max - file.header.frame_size.x_min) / 20.0;
  const double stage_height =
      static_cast<double>(file.header.frame_size.y_max - file.header.frame_size.y_min) / 20.0;
  out += "Stage " + format_number(stage_width) + " x " + format_number(stage_height) + " at " +
         format_number(file.header.frame_rate_fps()) + " fps, " +
         std::to_string(file.header.frame_count) + " root frame(s).\n";
  for (const SwfTag& tag : file.tags) {
    if (tag.code != tag::kSymbolClass || !tag.symbols) {
      continue;
    }
    for (const SwfSymbol& symbol : *tag.symbols) {
      if (symbol.tag_id == 0) {
        out += "Document class: " + clean_text(symbol.name) + "\n";
      }
    }
  }
  std::string imports;
  for (const SwfTag& tag : file.tags) {
    if ((tag.code != tag::kImportAssets && tag.code != tag::kImportAssets2) || !tag.imports) {
      continue;
    }
    if (!imports.empty()) {
      imports += ", ";
    }
    imports += clean_text(tag.imports->source_movie) + " (" +
               std::to_string(tag.imports->symbols.size()) + " symbols)";
  }
  if (!imports.empty()) {
    out += "Imports: " + imports + "\n";
  }
  if (!movie.root.labels.empty()) {
    std::string labels;
    for (const std::string& label : movie.root.labels) {
      if (!labels.empty()) {
        labels += ", ";
      }
      labels += clean_text(label);
    }
    out += "Root frame labels: " + labels + "\n";
  }
  out += "Totals: " + std::to_string(counts.shapes) + " shapes, " +
         std::to_string(counts.sprites) + " sprites, " + std::to_string(counts.morphs) +
         " morph shapes, " + std::to_string(counts.images) + " images, " +
         std::to_string(counts.text_fields) + " text fields.\n";
  out += "Frame-label census: " + std::to_string(counts.root_labels) + " root labels, " +
         std::to_string(counts.sprite_labels) + " sprite labels in " +
         std::to_string(counts.labelled_sprite_definitions) + " of " +
         std::to_string(counts.sprites) +
         " sprite definitions (including unnamed/unplaced).\n";
  const MovieIndex index = build_index(file);
  std::vector<WalkRow> rows;
  std::vector<std::uint16_t> stack;
  std::size_t visits = 0;
  walk_placements(movie, index, movie.root.placements, std::string{}, SwfTransform{}, options, stack,
                  visits, rows);
  for (const WalkRow& row : rows) {
    if (row.name.empty() && row.kind != "text field") {
      continue;
    }
    std::string detail = row.kind + " #" + std::to_string(row.character_id) + ", first frame " +
                         std::to_string(row.frame) + ", at (" + format_number(row.transform.translate_x) +
                         ", " + format_number(row.transform.translate_y) + ")";
    const auto bounds = index.shape_bounds.find(row.character_id);
    if (bounds != index.shape_bounds.end()) {
      const SwfBounds box = transformed_bounds(row.transform, bounds->second);
      detail += ", size " + format_number(box.width) + " x " + format_number(box.height);
    }
    const auto field = index.edit_texts.find(row.character_id);
    if (field != index.edit_texts.end()) {
      detail += text_field_detail(*field->second, row.transform);
    }
    const std::string label = row.path + (row.name.empty() ? "(unnamed)" : row.name);
    out += "- " + clean_text(label) + ": " + clean_text(detail) + "\n";
    if (row.kind == "sprite") {
      const auto sprite = movie.sprites.find(row.character_id);
      if (sprite != movie.sprites.end() && !sprite->second.labels.empty()) {
        std::string states;
        for (const std::string& state : sprite->second.labels) {
          if (!states.empty()) {
            states += ", ";
          }
          states += clean_text(state);
        }
        out += "    states: " + states + "\n";
      }
    }
  }
  std::map<std::string, std::uint32_t> unnamed;
  for (const WalkRow& row : rows) {
    if (row.path.empty() && row.name.empty() && row.kind != "text field") {
      ++unnamed[row.kind];
    }
  }
  if (!unnamed.empty()) {
    std::string summary;
    for (const auto& entry : unnamed) {
      if (!summary.empty()) {
        summary += ", ";
      }
      summary += std::to_string(entry.second) + " " + entry.first;
    }
    out += "Unnamed top-level art: " + summary + "\n";
  }
  if (visits >= options.max_rows) {
    out += "(list truncated at " + std::to_string(options.max_rows) + " placements)\n";
  }
  return out;
}

}  // namespace fo4::swf
