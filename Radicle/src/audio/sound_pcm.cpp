#include "fo4/audio/sound_pcm.hpp"

#include "fo4/audio/xwma_decoder.hpp"
#include "fo4/error.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace fo4::audio {
namespace {

constexpr std::size_t k_riff_header = 12;
constexpr std::size_t k_chunk_header = 8;
constexpr std::size_t k_fmt_min = 16;
constexpr std::size_t k_fmt_extensible = 26;
constexpr std::uint16_t k_format_pcm = 1;
constexpr std::uint16_t k_format_extensible = 0xFFFE;

std::uint16_t u16_at(std::span<const std::byte> b, std::size_t at) {
  return static_cast<std::uint16_t>(std::to_integer<unsigned>(b[at]) | std::to_integer<unsigned>(b[at + 1]) << 8);
}

std::uint32_t u32_at(std::span<const std::byte> b, std::size_t at) {
  return static_cast<std::uint32_t>(u16_at(b, at)) | static_cast<std::uint32_t>(u16_at(b, at + 2)) << 16;
}

bool tag_at(std::span<const std::byte> b, std::size_t at, const char (&tag)[5]) {
  return b.size() >= at + 4 && std::memcmp(b.data() + at, tag, 4) == 0;
}

std::string lower_ascii(std::string_view text) {
  std::string out(text);
  for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

}  // namespace

SoundPcm decode_wav(std::span<const std::byte> bytes) {
  if (bytes.size() < k_riff_header || !tag_at(bytes, 0, "RIFF") || !tag_at(bytes, 8, "WAVE")) {
    throw parse_error(0, "sound payload is not a RIFF/WAVE stream");
  }
  std::uint16_t format = 0, channels = 0, bits = 0;
  std::uint32_t rate = 0;
  bool have_fmt = false;
  for (std::size_t pos = k_riff_header; pos + k_chunk_header <= bytes.size();) {
    const std::size_t size = u32_at(bytes, pos + 4);
    const std::size_t body = pos + k_chunk_header;
    if (tag_at(bytes, pos, "fmt ") && size >= k_fmt_min && body + k_fmt_min <= bytes.size()) {
      format = u16_at(bytes, body);
      channels = u16_at(bytes, body + 2);
      rate = u32_at(bytes, body + 4);
      bits = u16_at(bytes, body + 14);
      if (format == k_format_extensible && size >= k_fmt_extensible && body + k_fmt_extensible <= bytes.size()) {
        format = u16_at(bytes, body + 24);
      }
      have_fmt = true;
    } else if (tag_at(bytes, pos, "data")) {
      if (!have_fmt) throw parse_error(pos, "WAV data chunk comes before its fmt chunk");
      if (size > bytes.size() - body) throw parse_error(pos + 4, "WAV data chunk runs past the end of the buffer");
      if (format != k_format_pcm) throw parse_error(pos, "WAV is not integer PCM");
      if (channels == 0 || rate == 0) throw parse_error(pos, "WAV has no channels or no sample rate");
      if (bits != 8 && bits != 16 && bits != 24 && bits != 32) throw parse_error(pos, "WAV sample width is not 8, 16, 24 or 32 bits");
      const std::size_t stored = bits / 8u;
      const std::size_t frames = size / (stored * channels);
      SoundPcm out;
      out.container = SoundContainerKind::wav;
      out.sample_rate = rate;
      out.channels = channels;
      const auto* source = bytes.data() + body;
      if (bits <= 16) {
        out.bits_per_sample = bits;
        out.pcm.assign(source, source + frames * stored * channels);
      } else {
        out.bits_per_sample = 16;
        out.pcm.resize(frames * channels * 2);
        for (std::size_t i = 0; i < frames * channels; ++i) {
          out.pcm[i * 2] = source[i * stored + stored - 2];
          out.pcm[i * 2 + 1] = source[i * stored + stored - 1];
        }
      }
      return out;
    }
    pos = body + size + (size & 1u);
  }
  throw parse_error(bytes.size(), "WAV has no data chunk");
}

SoundPcm decode_sound(std::span<const std::byte> bytes) {
  SoundContainerKind kind = SoundContainerKind::xwma;
  if (tag_at(bytes, 0, "FUZE")) {
    kind = SoundContainerKind::fuz;
  } else if (tag_at(bytes, 0, "RIFF") && tag_at(bytes, 8, "WAVE")) {
    return decode_wav(bytes);
  } else if (tag_at(bytes, 0, "RIFF") && tag_at(bytes, 8, "XWMA")) {
    kind = SoundContainerKind::xwma;
  } else {
    throw parse_error(0, "sound payload is not a WAV, xWMA or FUZ stream");
  }
  auto decoded = decode_xwma(bytes);
  SoundPcm out;
  out.container = kind;
  out.sample_rate = decoded.sample_rate;
  out.channels = decoded.channels;
  out.bits_per_sample = 16;
  out.bad_packets = decoded.bad_packets;
  out.pcm.resize(decoded.samples.size() * sizeof(std::int16_t));
  if (!out.pcm.empty()) std::memcpy(out.pcm.data(), decoded.samples.data(), out.pcm.size());
  return out;
}

std::vector<std::string> sound_path_candidates(std::string_view name) {
  std::string text(name);
  std::replace(text.begin(), text.end(), '/', '\\');
  text.erase(0, std::min(text.find_first_not_of('\\'), text.size()));
  if (lower_ascii(text).starts_with("data\\")) text.erase(0, 5);
  const bool has_sound_root = lower_ascii(text).starts_with("sound\\");
  const auto dot = text.find_last_of('.');
  const bool has_ext = dot != std::string::npos && text.find_first_of('\\', dot) == std::string::npos;
  const std::string stem = has_ext ? text.substr(0, dot) : text;
  std::vector<std::string> extensions;
  if (has_ext) extensions.push_back(text.substr(dot));
  for (const char* ext : {".wav", ".xwm", ".fuz"}) {
    if (std::find_if(extensions.begin(), extensions.end(), [&](const std::string& e) { return lower_ascii(e) == ext; }) ==
        extensions.end()) {
      extensions.emplace_back(ext);
    }
  }
  if (!has_ext) extensions.emplace(extensions.begin());
  std::vector<std::string> out;
  for (const char* prefix : {"", "sound\\", "sound\\fx\\"}) {
    if (has_sound_root && *prefix != '\0') continue;
    for (const auto& ext : extensions) out.push_back(prefix + stem + ext);
  }
  return out;
}

std::optional<SoundAsset> read_sound(const archive::AssetResolver& assets, std::string_view name) {
  for (auto& candidate : sound_path_candidates(name)) {
    const auto location = assets.locate(candidate);
    if (!location) continue;
    const auto bytes = assets.read(*location);
    return SoundAsset{std::move(candidate), decode_sound(bytes)};
  }
  return std::nullopt;
}

}  // namespace fo4::audio
