#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::swf {

enum class SwfCompression : std::uint8_t { none, zlib, lzma };

enum class SwfFormat : std::uint8_t { swf, gfx };

enum class SwfStatus : std::uint8_t {
  ok = 0,
  empty_input,
  bad_signature,
  truncated_header,
  unsupported_compression,
  decompress_failed,
  truncated_tag,
  malformed_tag,
};

std::string_view status_name(SwfStatus status) noexcept;

namespace tag {

inline constexpr std::uint16_t kEnd = 0;
inline constexpr std::uint16_t kShowFrame = 1;
inline constexpr std::uint16_t kDefineShape = 2;
inline constexpr std::uint16_t kPlaceObject = 4;
inline constexpr std::uint16_t kRemoveObject = 5;
inline constexpr std::uint16_t kDefineBits = 6;
inline constexpr std::uint16_t kDefineButton = 7;
inline constexpr std::uint16_t kJpegTables = 8;
inline constexpr std::uint16_t kSetBackgroundColor = 9;
inline constexpr std::uint16_t kDefineFont = 10;
inline constexpr std::uint16_t kDefineText = 11;
inline constexpr std::uint16_t kDoAction = 12;
inline constexpr std::uint16_t kDefineFontInfo = 13;
inline constexpr std::uint16_t kDefineSound = 14;
inline constexpr std::uint16_t kStartSound = 15;
inline constexpr std::uint16_t kDefineButtonSound = 17;
inline constexpr std::uint16_t kSoundStreamHead = 18;
inline constexpr std::uint16_t kSoundStreamBlock = 19;
inline constexpr std::uint16_t kDefineBitsLossless = 20;
inline constexpr std::uint16_t kDefineBitsJpeg2 = 21;
inline constexpr std::uint16_t kDefineShape2 = 22;
inline constexpr std::uint16_t kProtect = 24;
inline constexpr std::uint16_t kPlaceObject2 = 26;
inline constexpr std::uint16_t kRemoveObject2 = 28;
inline constexpr std::uint16_t kDefineShape3 = 32;
inline constexpr std::uint16_t kDefineText2 = 33;
inline constexpr std::uint16_t kDefineButton2 = 34;
inline constexpr std::uint16_t kDefineBitsJpeg3 = 35;
inline constexpr std::uint16_t kDefineBitsLossless2 = 36;
inline constexpr std::uint16_t kDefineEditText = 37;
inline constexpr std::uint16_t kDefineSprite = 39;
inline constexpr std::uint16_t kFrameLabel = 43;
inline constexpr std::uint16_t kSoundStreamHead2 = 45;
inline constexpr std::uint16_t kDefineMorphShape = 46;
inline constexpr std::uint16_t kDefineFont2 = 48;
inline constexpr std::uint16_t kExportAssets = 56;
inline constexpr std::uint16_t kImportAssets = 57;
inline constexpr std::uint16_t kEnableDebugger = 58;
inline constexpr std::uint16_t kDoInitAction = 59;
inline constexpr std::uint16_t kDefineVideoStream = 60;
inline constexpr std::uint16_t kVideoFrame = 61;
inline constexpr std::uint16_t kDefineFontInfo2 = 62;
inline constexpr std::uint16_t kEnableDebugger2 = 64;
inline constexpr std::uint16_t kScriptLimits = 65;
inline constexpr std::uint16_t kSetTabIndex = 66;
inline constexpr std::uint16_t kFileAttributes = 69;
inline constexpr std::uint16_t kPlaceObject3 = 70;
inline constexpr std::uint16_t kImportAssets2 = 71;
inline constexpr std::uint16_t kDoAbc2 = 82;
inline constexpr std::uint16_t kDefineFontAlignZones = 73;
inline constexpr std::uint16_t kCsmTextSettings = 74;
inline constexpr std::uint16_t kDefineFont3 = 75;
inline constexpr std::uint16_t kSymbolClass = 76;
inline constexpr std::uint16_t kMetadata = 77;
inline constexpr std::uint16_t kDefineScalingGrid = 78;
inline constexpr std::uint16_t kDoAbc = 72;
inline constexpr std::uint16_t kDefineShape4 = 83;
inline constexpr std::uint16_t kDefineMorphShape2 = 84;
inline constexpr std::uint16_t kDefineSceneAndFrameLabelData = 86;
inline constexpr std::uint16_t kDefineBinaryData = 87;
inline constexpr std::uint16_t kDefineFontName = 88;
inline constexpr std::uint16_t kStartSound2 = 89;
inline constexpr std::uint16_t kDefineBitsJpeg4 = 90;
inline constexpr std::uint16_t kDefineFont4 = 91;

inline constexpr std::uint16_t kExporterInfo = 1000;
inline constexpr std::uint16_t kDefineExternalImage = 1001;
inline constexpr std::uint16_t kFontTextureInfo = 1002;
// DefineExternalImage requires tag 1009 with a UI32 CharacterID in GFx 4.x (1001 unassigned).
inline constexpr std::uint16_t kDefineExternalImageV4 = 1009;

}  // namespace tag

std::string_view tag_code_name(std::uint16_t code) noexcept;

struct SwfRect {
  std::int32_t x_min{0};
  std::int32_t x_max{0};
  std::int32_t y_min{0};
  std::int32_t y_max{0};
  std::uint8_t nbits{0};
  std::uint8_t padding{0};

  bool operator==(const SwfRect&) const = default;
};

struct SwfHeader {
  SwfFormat format{SwfFormat::swf};
  SwfCompression compression{SwfCompression::none};
  std::uint8_t version{0};
  std::uint32_t file_length{0};
  SwfRect frame_size{};
  std::uint16_t frame_rate{0};
  std::uint16_t frame_count{0};

  double frame_rate_fps() const noexcept { return static_cast<double>(frame_rate) / 256.0; }
};

struct SwfFileAttributes {
  std::uint32_t raw{0};
  bool use_direct_blit{false};
  bool use_gpu{false};
  bool has_metadata{false};
  bool action_script3{false};
  bool use_network{false};
};

struct SwfScriptLimits {
  std::uint16_t max_recursion_depth{0};
  std::uint16_t script_timeout_seconds{0};
};

struct SwfSymbol {
  std::uint16_t tag_id{0};
  std::string name;
};

struct SwfAbc {
  std::uint32_t flags{0};
  std::string name;
  std::size_t payload_offset{0};
  std::size_t payload_size{0};
};

struct SwfAction {
  std::uint16_t sprite_id{0};
  std::size_t offset{0};
  std::size_t size{0};
};

struct SwfBinaryData {
  std::uint16_t tag_id{0};
  std::uint32_t reserved{0};
  std::size_t offset{0};
  std::size_t size{0};
};

struct SwfShape {
  std::uint16_t shape_id{0};
  SwfRect bounds{};
  bool has_edge_bounds{false};
  SwfRect edge_bounds{};
  std::uint8_t flags{0};
};

struct SwfBitmap {
  std::uint16_t character_id{0};
  std::uint8_t format{0};
  std::uint16_t width{0};
  std::uint16_t height{0};
  std::optional<std::uint8_t> color_table_size;
};

struct SwfFont {
  std::uint16_t font_id{0};
  std::uint32_t glyph_count{0};
  std::uint8_t flags{0};
  std::uint8_t language{0};
  std::string name;
  bool wide_offsets{false};
  bool wide_codes{false};
};

struct SwfText {
  std::uint16_t character_id{0};
  SwfRect bounds{};
  std::size_t record_count{0};
  std::size_t glyph_count{0};
};

struct SwfSound {
  std::uint16_t sound_id{0};
  std::uint8_t format{0};
  std::uint8_t rate{0};
  bool sample_16bit{false};
  bool stereo{false};
  std::uint32_t sample_count{0};
  std::size_t data_size{0};
};

struct SwfExporterInfo {
  std::uint16_t version{0};
  std::uint32_t flags{0};
  std::uint16_t bitmap_format{0};
  std::string prefix;
  std::string swf_name;
  std::size_t code_offset_count{0};
};

struct SwfExternalImage {
  std::uint32_t character_id{0};
  std::uint16_t bitmap_format{0};
  std::uint16_t target_width{0};
  std::uint16_t target_height{0};
  std::string export_name;
  std::string file_name;
};

struct SwfFontTextureInfo {
  std::uint32_t texture_id{0};
  std::uint16_t texture_format{0};
  std::string file_name;
  std::uint16_t texture_width{0};
  std::uint16_t texture_height{0};
  std::uint8_t pad_pixels{0};
  std::uint16_t nominal_glyph_size{0};
  std::uint32_t texture_glyph_count{0};
  std::uint32_t font_count{0};
};

struct SwfTag {
  std::uint16_t code{0};
  std::uint32_t length{0};
  std::size_t offset{0};
  bool long_header{false};
  std::vector<std::byte> payload;
  std::vector<SwfTag> children;

  std::optional<SwfFileAttributes> attributes;
  std::optional<std::string> metadata;
  std::optional<SwfScriptLimits> script_limits;
  std::optional<std::uint32_t> background_color;
  std::optional<std::uint16_t> sprite_id;
  std::optional<std::uint16_t> sprite_frame_count;
  std::optional<std::vector<SwfSymbol>> symbols;
  std::optional<std::vector<SwfSymbol>> exports;
  std::optional<SwfAbc> abc;
  std::optional<SwfAction> action;
  std::optional<SwfBinaryData> binary_data;
  std::optional<SwfShape> shape;
  std::optional<SwfBitmap> bitmap;
  std::optional<SwfFont> font;
  std::optional<SwfText> text;
  std::optional<SwfSound> sound;
  std::optional<SwfExporterInfo> exporter_info;
  std::optional<SwfExternalImage> external_image;
  std::optional<SwfFontTextureInfo> font_texture;

  void clear_decoded();
};

struct SwfFile {
  SwfHeader header{};
  std::vector<SwfTag> tags;
  std::vector<std::byte> trailing_bytes;
  std::vector<std::byte> source_body;
  std::vector<std::byte> source_compressed_body;
  bool source_compressed_body_known{false};
};

struct SwfReadResult {
  SwfFile file;
  SwfStatus status{SwfStatus::ok};
  bool declared_length_matches{true};
  std::string message;

  bool ok() const noexcept { return status == SwfStatus::ok; }
};

struct SwfAbcInfo {
  std::string name;
  std::uint32_t size{0};
};

struct SwfSummary {
  std::uint8_t version{0};
  bool compressed{false};
  bool is_gfx{false};
  bool action_script3{false};
  bool has_metadata{false};
  bool use_network{false};
  std::uint16_t frame_count{0};
  std::size_t tag_count{0};
  std::size_t shape_tags{0};
  std::size_t bitmap_tags{0};
  std::size_t font_tags{0};
  std::size_t text_tags{0};
  std::size_t sound_tags{0};
  std::size_t exporter_info_tags{0};
  std::size_t external_image_tags{0};
  std::size_t font_texture_tags{0};
  std::vector<SwfSymbol> symbol_classes;
  std::vector<SwfAbcInfo> abc_blocks;
  std::vector<std::string> export_names;
};

struct SwfAbcBlock {
  std::size_t tag_index{0};
  std::uint32_t flags{0};
  std::string name;
  std::span<const std::byte> payload;
};

SwfReadResult read_swf(std::span<const std::byte> bytes);

std::vector<std::byte> write_swf(const SwfFile& file);
std::vector<std::byte> write_swf(const SwfFile& file, SwfCompression compression);

bool replace_tag_payload(SwfFile& file, std::size_t tag_index, std::span<const std::byte> payload);

std::span<const std::byte> abc_payload(const SwfTag& tag) noexcept;
std::vector<SwfAbcBlock> abc_blocks(const SwfFile& file);

SwfSummary summarize(const SwfFile& file);

}  // namespace fo4::swf
