#include "fo4/swf/swf.hpp"

#include "swf_internal.hpp"

#include <zlib.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fo4::swf {

namespace {

using detail::byte_at;

std::uint32_t read_u32_at(std::span<const std::byte> bytes, std::size_t offset) noexcept {
  return static_cast<std::uint32_t>(byte_at(bytes, offset)) |
         (static_cast<std::uint32_t>(byte_at(bytes, offset + 1)) << 8) |
         (static_cast<std::uint32_t>(byte_at(bytes, offset + 2)) << 16) |
         (static_cast<std::uint32_t>(byte_at(bytes, offset + 3)) << 24);
}

struct SignatureInfo {
  SwfFormat format{SwfFormat::swf};
  SwfCompression compression{SwfCompression::none};
};

std::optional<SignatureInfo> parse_signature(std::span<const std::byte> bytes) noexcept {
  const auto first = static_cast<char>(byte_at(bytes, 0));
  const auto second = static_cast<char>(byte_at(bytes, 1));
  const auto third = static_cast<char>(byte_at(bytes, 2));
  if (first == 'F' && second == 'W' && third == 'S') {
    return SignatureInfo{SwfFormat::swf, SwfCompression::none};
  }
  if (first == 'C' && second == 'W' && third == 'S') {
    return SignatureInfo{SwfFormat::swf, SwfCompression::zlib};
  }
  if (first == 'Z' && second == 'W' && third == 'S') {
    return SignatureInfo{SwfFormat::swf, SwfCompression::lzma};
  }
  if (first == 'G' && second == 'F' && third == 'X') {
    return SignatureInfo{SwfFormat::gfx, SwfCompression::none};
  }
  if (first == 'C' && second == 'F' && third == 'X') {
    return SignatureInfo{SwfFormat::gfx, SwfCompression::zlib};
  }
  return std::nullopt;
}

bool inflate_body(std::span<const std::byte> input, std::vector<std::byte>& out,
                  std::string& message) {
  if (input.size() > std::numeric_limits<uInt>::max()) {
    message = "compressed SWF body is too large for zlib";
    return false;
  }
  z_stream stream{};
  stream.next_in = reinterpret_cast<Bytef*>(const_cast<std::byte*>(input.data()));
  stream.avail_in = static_cast<uInt>(input.size());
  std::vector<std::byte> chunk(64u * 1024u);
  if (inflateInit(&stream) != Z_OK) {
    message = "zlib inflateInit failed";
    return false;
  }
  int code = Z_OK;
  bool overflow = false;
  try {
    while (code == Z_OK) {
      stream.next_out = reinterpret_cast<Bytef*>(chunk.data());
      stream.avail_out = static_cast<uInt>(chunk.size());
      code = inflate(&stream, Z_NO_FLUSH);
      const auto produced = static_cast<std::size_t>(chunk.size() - stream.avail_out);
      if (out.size() + produced > detail::kMaxBodyBytes) {
        overflow = true;
        break;
      }
      out.insert(out.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(produced));
    }
  } catch (...) {
    inflateEnd(&stream);
    throw;
  }
  inflateEnd(&stream);
  if (overflow) {
    message = "decompressed SWF body exceeds the size cap";
    return false;
  }
  if (code != Z_STREAM_END) {
    message = "zlib failed to decompress the SWF body";
    return false;
  }
  return true;
}

void collect_tags(const std::vector<SwfTag>& tags, SwfSummary& summary, unsigned depth) {
  if (depth > detail::kMaxTagDepth) {
    return;
  }
  for (const auto& current : tags) {
    ++summary.tag_count;
    if (current.symbols.has_value()) {
      for (const auto& symbol : *current.symbols) {
        summary.symbol_classes.push_back(symbol);
      }
    }
    if (current.exports.has_value()) {
      for (const auto& exported : *current.exports) {
        summary.export_names.push_back(exported.name);
      }
    }
    if (current.abc.has_value()) {
      summary.abc_blocks.push_back(
          SwfAbcInfo{current.abc->name, static_cast<std::uint32_t>(current.abc->payload_size)});
    }
    if (current.attributes.has_value()) {
      summary.action_script3 = current.attributes->action_script3;
      summary.has_metadata = current.attributes->has_metadata;
      summary.use_network = current.attributes->use_network;
    }
    if (current.shape.has_value()) {
      ++summary.shape_tags;
    }
    if (current.bitmap.has_value()) {
      ++summary.bitmap_tags;
    }
    if (current.font.has_value()) {
      ++summary.font_tags;
    }
    if (current.text.has_value()) {
      ++summary.text_tags;
    }
    if (current.sound.has_value()) {
      ++summary.sound_tags;
    }
    if (current.exporter_info.has_value()) {
      ++summary.exporter_info_tags;
    }
    if (current.external_image.has_value()) {
      ++summary.external_image_tags;
    }
    if (current.font_texture.has_value()) {
      ++summary.font_texture_tags;
    }
    collect_tags(current.children, summary, depth + 1);
  }
}

}  // namespace

std::string_view status_name(SwfStatus status) noexcept {
  switch (status) {
    case SwfStatus::ok:
      return "ok";
    case SwfStatus::empty_input:
      return "empty_input";
    case SwfStatus::bad_signature:
      return "bad_signature";
    case SwfStatus::truncated_header:
      return "truncated_header";
    case SwfStatus::unsupported_compression:
      return "unsupported_compression";
    case SwfStatus::decompress_failed:
      return "decompress_failed";
    case SwfStatus::truncated_tag:
      return "truncated_tag";
    case SwfStatus::malformed_tag:
      return "malformed_tag";
    case SwfStatus::too_many_tags:
      return "too_many_tags";
    case SwfStatus::out_of_memory:
      return "out_of_memory";
  }
  return "unknown";
}

std::string_view tag_code_name(std::uint16_t code) noexcept {
  switch (code) {
    case tag::kEnd:
      return "End";
    case tag::kShowFrame:
      return "ShowFrame";
    case tag::kDefineShape:
      return "DefineShape";
    case tag::kPlaceObject:
      return "PlaceObject";
    case tag::kRemoveObject:
      return "RemoveObject";
    case tag::kDefineBits:
      return "DefineBits";
    case tag::kDefineButton:
      return "DefineButton";
    case tag::kJpegTables:
      return "JPEGTables";
    case tag::kSetBackgroundColor:
      return "SetBackgroundColor";
    case tag::kDefineFont:
      return "DefineFont";
    case tag::kDefineText:
      return "DefineText";
    case tag::kDoAction:
      return "DoAction";
    case tag::kDefineFontInfo:
      return "DefineFontInfo";
    case tag::kDefineSound:
      return "DefineSound";
    case tag::kStartSound:
      return "StartSound";
    case tag::kDefineButtonSound:
      return "DefineButtonSound";
    case tag::kSoundStreamHead:
      return "SoundStreamHead";
    case tag::kSoundStreamBlock:
      return "SoundStreamBlock";
    case tag::kDefineBitsLossless:
      return "DefineBitsLossless";
    case tag::kDefineBitsJpeg2:
      return "DefineBitsJPEG2";
    case tag::kDefineShape2:
      return "DefineShape2";
    case tag::kProtect:
      return "Protect";
    case tag::kPlaceObject2:
      return "PlaceObject2";
    case tag::kRemoveObject2:
      return "RemoveObject2";
    case tag::kDefineShape3:
      return "DefineShape3";
    case tag::kDefineText2:
      return "DefineText2";
    case tag::kDefineButton2:
      return "DefineButton2";
    case tag::kDefineBitsJpeg3:
      return "DefineBitsJPEG3";
    case tag::kDefineBitsLossless2:
      return "DefineBitsLossless2";
    case tag::kDefineEditText:
      return "DefineEditText";
    case tag::kDefineSprite:
      return "DefineSprite";
    case tag::kFrameLabel:
      return "FrameLabel";
    case tag::kSoundStreamHead2:
      return "SoundStreamHead2";
    case tag::kDefineMorphShape:
      return "DefineMorphShape";
    case tag::kDefineFont2:
      return "DefineFont2";
    case tag::kExportAssets:
      return "ExportAssets";
    case tag::kImportAssets:
      return "ImportAssets";
    case tag::kEnableDebugger:
      return "EnableDebugger";
    case tag::kDoInitAction:
      return "DoInitAction";
    case tag::kDefineVideoStream:
      return "DefineVideoStream";
    case tag::kVideoFrame:
      return "VideoFrame";
    case tag::kDefineFontInfo2:
      return "DefineFontInfo2";
    case tag::kEnableDebugger2:
      return "EnableDebugger2";
    case tag::kScriptLimits:
      return "ScriptLimits";
    case tag::kSetTabIndex:
      return "SetTabIndex";
    case tag::kFileAttributes:
      return "FileAttributes";
    case tag::kPlaceObject3:
      return "PlaceObject3";
    case tag::kImportAssets2:
      return "ImportAssets2";
    case tag::kDoAbc2:
      return "DoABC2";
    case tag::kDefineFontAlignZones:
      return "DefineFontAlignZones";
    case tag::kCsmTextSettings:
      return "CSMTextSettings";
    case tag::kDefineFont3:
      return "DefineFont3";
    case tag::kSymbolClass:
      return "SymbolClass";
    case tag::kMetadata:
      return "Metadata";
    case tag::kDefineScalingGrid:
      return "DefineScalingGrid";
    case tag::kDoAbc:
      return "DoABC";
    case tag::kDefineShape4:
      return "DefineShape4";
    case tag::kDefineMorphShape2:
      return "DefineMorphShape2";
    case tag::kDefineSceneAndFrameLabelData:
      return "DefineSceneAndFrameLabelData";
    case tag::kDefineBinaryData:
      return "DefineBinaryData";
    case tag::kDefineFontName:
      return "DefineFontName";
    case tag::kStartSound2:
      return "StartSound2";
    case tag::kDefineBitsJpeg4:
      return "DefineBitsJPEG4";
    case tag::kDefineFont4:
      return "DefineFont4";
    case tag::kExporterInfo:
      return "ExporterInfo";
    case tag::kDefineExternalImage:
      return "DefineExternalImage";
    case tag::kDefineExternalImageV4:
      return "DefineExternalImage";
    case tag::kFontTextureInfo:
      return "FontTextureInfo";
    default:
      return "Unknown";
  }
}

void SwfTag::clear_decoded() {
  children.clear();
  attributes.reset();
  metadata.reset();
  script_limits.reset();
  background_color.reset();
  sprite_id.reset();
  sprite_frame_count.reset();
  symbols.reset();
  exports.reset();
  imports.reset();
  abc.reset();
  action.reset();
  binary_data.reset();
  shape.reset();
  bitmap.reset();
  font.reset();
  text.reset();
  edit_text.reset();
  sound.reset();
  exporter_info.reset();
  external_image.reset();
  font_texture.reset();
  placement.reset();
  removal.reset();
  morph_shape.reset();
  jpeg.reset();
  jpeg_tables.reset();
}

SwfReadResult read_swf(std::span<const std::byte> bytes) {
  return read_swf(bytes, detail::kMaxTags);
}

SwfReadResult read_swf(std::span<const std::byte> bytes, std::size_t max_tags) {
  SwfReadResult result;
  if (bytes.empty()) {
    result.status = SwfStatus::empty_input;
    result.message = "empty SWF input";
    return result;
  }
  if (bytes.size() < 8) {
    result.status = SwfStatus::truncated_header;
    result.message = "SWF header needs 8 bytes";
    return result;
  }
  const auto signature = parse_signature(bytes);
  if (!signature.has_value()) {
    result.status = SwfStatus::bad_signature;
    result.message = "unknown SWF signature " + detail::to_text(bytes.first(3));
    return result;
  }

  auto& header = result.file.header;
  header.format = signature->format;
  header.compression = signature->compression;
  header.version = byte_at(bytes, 3);
  header.file_length = read_u32_at(bytes, 4);

  if (header.compression == SwfCompression::lzma) {
    result.status = SwfStatus::unsupported_compression;
    result.message = "LZMA (ZWS) SWF compression is not supported";
    return result;
  }

  std::vector<std::byte> body;
  std::vector<std::byte> extra;
  std::size_t source_length = 0;
  if (header.compression == SwfCompression::zlib) {
    try {
      if (!inflate_body(bytes.subspan(8), body, result.message)) {
        result.status = SwfStatus::decompress_failed;
        return result;
      }
    } catch (const std::bad_alloc&) {
      result.status = SwfStatus::out_of_memory;
      result.message = "SWF body ran out of memory while inflating";
      return result;
    }
    source_length = body.size();
    result.file.source_compressed_body.assign(bytes.begin() + 8, bytes.end());
    result.file.source_compressed_body_known = true;
  } else {
    const auto declared =
        header.file_length > 8 ? static_cast<std::size_t>(header.file_length) - 8 : std::size_t{0};
    const auto available = bytes.size() - 8;
    const auto take = declared < available ? declared : available;
    body.assign(bytes.begin() + 8, bytes.begin() + static_cast<std::ptrdiff_t>(8 + take));
    if (take < available) {
      extra.assign(bytes.begin() + static_cast<std::ptrdiff_t>(8 + take), bytes.end());
    }
    source_length = available;
  }

  const auto declared_body =
      header.file_length > 8 ? static_cast<std::size_t>(header.file_length) - 8 : std::size_t{0};
  result.declared_length_matches = source_length == declared_body;

  SwfRect rect;
  std::size_t after_rect = 0;
  if (!detail::read_rect(body, 0, rect, after_rect)) {
    result.status = SwfStatus::truncated_header;
    result.message = "SWF frame size rectangle is truncated";
    return result;
  }
  header.frame_size = rect;

  detail::SwfReader reader(body, after_rect);
  std::uint16_t frame_rate = 0;
  std::uint16_t frame_count = 0;
  if (!reader.read_u16(frame_rate) || !reader.read_u16(frame_count)) {
    result.status = SwfStatus::truncated_header;
    result.message = "SWF frame rate or frame count is truncated";
    return result;
  }
  header.frame_rate = frame_rate;
  header.frame_count = frame_count;

  std::size_t stop = 0;
  std::size_t tags_left = max_tags;
  SwfStatus walk_status = SwfStatus::ok;
  try {
    walk_status = detail::decode_tag_stream(body, reader.position(), body.size(), 0,
                                            result.file.tags, stop, tags_left);
  } catch (const std::bad_alloc&) {
    result.status = SwfStatus::out_of_memory;
    result.message = "SWF tag decoding ran out of memory";
    return result;
  }
  if (stop < body.size()) {
    result.file.trailing_bytes.assign(body.begin() + static_cast<std::ptrdiff_t>(stop), body.end());
  }
  result.file.trailing_bytes.insert(result.file.trailing_bytes.end(), extra.begin(), extra.end());
  body.insert(body.end(), extra.begin(), extra.end());
  result.file.source_body = std::move(body);

  if (walk_status != SwfStatus::ok) {
    result.status = walk_status;
    result.message = walk_status == SwfStatus::truncated_tag   ? "SWF tag stream is truncated"
                     : walk_status == SwfStatus::too_many_tags ? "SWF tag count exceeds the limit"
                                                               : "a SWF tag payload did not decode";
  } else if (!result.declared_length_matches && result.message.empty()) {
    result.message = "declared SWF length does not match the body size";
  }
  return result;
}

std::span<const std::byte> abc_payload(const SwfTag& source) noexcept {
  if (!source.abc.has_value()) {
    return {};
  }
  const auto& abc = *source.abc;
  if (abc.payload_offset > source.payload.size() ||
      abc.payload_size > source.payload.size() - abc.payload_offset) {
    return {};
  }
  return std::span<const std::byte>(source.payload).subspan(abc.payload_offset, abc.payload_size);
}

std::vector<SwfAbcBlock> abc_blocks(const SwfFile& file) {
  std::vector<SwfAbcBlock> blocks;
  for (std::size_t index = 0; index < file.tags.size(); ++index) {
    const auto& current = file.tags[index];
    if (!current.abc.has_value()) {
      continue;
    }
    SwfAbcBlock block;
    block.tag_index = index;
    block.flags = current.abc->flags;
    block.name = current.abc->name;
    block.payload = abc_payload(current);
    blocks.push_back(std::move(block));
  }
  return blocks;
}

SwfSummary summarize(const SwfFile& file) {
  SwfSummary summary;
  summary.version = file.header.version;
  summary.compressed = file.header.compression != SwfCompression::none;
  summary.is_gfx = file.header.format == SwfFormat::gfx;
  summary.frame_count = file.header.frame_count;
  collect_tags(file.tags, summary, 0);
  if (!summary.abc_blocks.empty()) {
    summary.action_script3 = true;
  }
  return summary;
}

bool replace_tag_payload(SwfFile& file, std::size_t tag_index, std::span<const std::byte> payload) {
  if (tag_index >= file.tags.size() ||
      payload.size() > std::numeric_limits<std::uint32_t>::max()) {
    return false;
  }
  auto& target = file.tags[tag_index];
  target.payload.assign(payload.begin(), payload.end());
  target.length = static_cast<std::uint32_t>(target.payload.size());
  target.clear_decoded();
  std::size_t tags_left = detail::kMaxTags;
  detail::decode_tag_payload(target.payload, target, 0, tags_left);
  return true;
}

bool apply_tag_plan(SwfFile& file, const SwfTagPlan& plan, SwfTagPlanResult& out) {
  const std::size_t count = file.tags.size();
  const bool has_end = count > 0 && file.tags.back().code == tag::kEnd;
  const std::size_t insert_limit = has_end ? count - 1 : count;

  std::vector<std::size_t> replaced_indices;
  for (const auto& replacement : plan.replacements) {
    if (replacement.tag_index >= count ||
        (has_end && replacement.tag_index == count - 1) ||
        replacement.payload.size() > std::numeric_limits<std::uint32_t>::max() ||
        std::find(replaced_indices.begin(), replaced_indices.end(), replacement.tag_index) !=
            replaced_indices.end()) {
      return false;
    }
    replaced_indices.push_back(replacement.tag_index);
  }

  std::vector<std::size_t> removed_indices;
  for (const auto& removal : plan.removals) {
    if (removal.tag_index >= count || (has_end && removal.tag_index == count - 1) ||
        std::find(removed_indices.begin(), removed_indices.end(), removal.tag_index) !=
            removed_indices.end() ||
        std::find(replaced_indices.begin(), replaced_indices.end(), removal.tag_index) !=
            replaced_indices.end()) {
      return false;
    }
    removed_indices.push_back(removal.tag_index);
  }

  for (const auto& insertion : plan.insertions) {
    if (insertion.before_tag_index > insert_limit || insertion.tag.code > 0x03FFu ||
        insertion.tag.code == tag::kEnd ||
        insertion.tag.payload.size() > std::numeric_limits<std::uint32_t>::max()) {
      return false;
    }
  }

  if (count - plan.removals.size() + plan.insertions.size() > detail::kMaxTags) {
    return false;
  }

  bool changed = !plan.insertions.empty() || !plan.removals.empty();
  for (const auto& replacement : plan.replacements) {
    if (file.tags[replacement.tag_index].payload != replacement.payload) {
      changed = true;
      break;
    }
  }
  if (!changed) {
    out = {};
    return true;
  }

  std::vector<SwfTag> next_tags = file.tags;
  for (const auto& replacement : plan.replacements) {
    SwfTag& target = next_tags[replacement.tag_index];
    target.payload = replacement.payload;
    target.length = static_cast<std::uint32_t>(target.payload.size());
    target.clear_decoded();
    std::size_t tags_left = detail::kMaxTags;
    if (detail::decode_tag_payload(target.payload, target, 0, tags_left) != SwfStatus::ok) {
      return false;
    }
  }

  struct PendingInsert {
    std::size_t plan_index;
    const SwfTag* tag;
  };
  std::vector<std::vector<PendingInsert>> inserts_at(count + 1);
  for (std::size_t index = 0; index < plan.insertions.size(); ++index) {
    inserts_at[plan.insertions[index].before_tag_index].push_back(
        PendingInsert{index, &plan.insertions[index].tag});
  }

  std::vector<SwfTag> next;
  next.reserve(count - plan.removals.size() + plan.insertions.size());
  std::vector<std::size_t> inserted_tag_indices(plan.insertions.size(), 0);
  bool failed = false;
  const auto append_inserts = [&](std::size_t position) {
    for (const PendingInsert& entry : inserts_at[position]) {
      SwfTag inserted = *entry.tag;
      inserted.length = static_cast<std::uint32_t>(inserted.payload.size());
      inserted.offset = 0;
      inserted.long_header = false;
      inserted.children.clear();
      inserted.clear_decoded();
      std::size_t tags_left = detail::kMaxTags;
      if (detail::decode_tag_payload(inserted.payload, inserted, 0, tags_left) != SwfStatus::ok) {
        failed = true;
        return;
      }
      inserted_tag_indices[entry.plan_index] = next.size();
      next.push_back(std::move(inserted));
    }
  };
  for (std::size_t index = 0; index < count && !failed; ++index) {
    append_inserts(index);
    if (failed) {
      break;
    }
    if (std::find(removed_indices.begin(), removed_indices.end(), index) !=
        removed_indices.end()) {
      continue;
    }
    next.push_back(std::move(next_tags[index]));
  }
  if (!failed) {
    append_inserts(count);
  }
  if (failed) {
    return false;
  }

  file.tags = std::move(next);
  out.inserted_tag_indices = std::move(inserted_tag_indices);
  out.changed = true;
  return true;
}
}

}  // namespace fo4::swf
