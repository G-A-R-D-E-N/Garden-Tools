#include "fo4/environment/sound.hpp"

#include "record_reader.hpp"

namespace fo4::env {
namespace {

using detail::Reader;
constexpr Signature sig(const char (&s)[5]) { return Signature{s[0], s[1], s[2], s[3]}; }

std::string_view string(const Reader& reader, const Subrecord& sub) {
  const auto value = zstring_view(sub);
  if (!value) reader.fail(sub.signature, "must be a single NUL-terminated string");
  return *value;
}

std::optional<std::string_view> text(const Reader& reader, Signature tag) {
  const auto* sub = reader.unique(tag);
  return sub ? std::optional{string(reader, *sub)} : std::nullopt;
}

void links(const Reader& reader, const Subrecord& sub, std::vector<FormKey>& out) {
  if (sub.data.size() % 4 != 0) reader.fail(sub.signature, "must contain complete four-byte links");
  for (std::size_t i = 0; i < sub.data.size(); i += 4) out.push_back(reader.link_at(sub, i));
}

}

std::vector<SoundRateFile> decode_sound_rate_files(const Record& record) {
  const auto* count = Reader::sized(record, sig("ITMC"), 4);
  std::vector<SoundRateFile> out;
  int state = 0;
  SoundRateFile rate;
  for (const auto& sub : record.subrecords) {
    if (sub.signature == sig("ITMS")) {
      if (state != 0 || !sub.data.empty()) Reader::fail(record, sub.signature, "invalid rate start");
      state = 1;
    } else if (sub.signature == sig("INTV")) {
      if (state != 1 || sub.data.size() != 4) Reader::fail(record, sub.signature, "invalid rate RPM");
      rate.rpm = Reader::u32(sub, 0);
      state = 2;
    } else if (sub.signature == sig("FNAM")) {
      if (state != 2) Reader::fail(record, sub.signature, "rate filename is out of order");
      const auto file = zstring_view(sub);
      if (!file) Reader::fail(record, sub.signature, "must be a single NUL-terminated string");
      rate.file = *file;
      state = 3;
    } else if (sub.signature == sig("ITME")) {
      if (state != 3 || !sub.data.empty()) Reader::fail(record, sub.signature, "invalid rate end");
      out.push_back(rate);
      state = 0;
    }
  }
  if (state != 0 || (count ? Reader::u32(*count, 0) != out.size() : !out.empty())) {
    Reader::fail(record, sig("ITMC"), "rate group count does not match complete blocks");
  }
  return out;
}

SoundDescriptorData decode_sound_descriptor(const Environment& env, const RecordRef& ref) {
  const Reader reader(env, ref, sig("SNDR"));
  SoundDescriptorData out;
  if (const auto* kind = reader.sized(sig("CNAM"), 4)) {
    out.kind = Reader::u32(*kind, 0);
    if (out.kind != 0x1EEF540Au && out.kind != 0x54651A43u && out.kind != 0xED157AE3u) {
      reader.fail(sig("CNAM"), "unsupported descriptor kind");
    }
  }
  out.category = reader.link(sig("GNAM"));
  out.alternate = reader.link(sig("SNAM"));
  out.output_model = reader.link(sig("ONAM"));
  if (out.kind == 0xED157AE3u) {
    out.base_descriptor = reader.link(sig("BNAM"));
  } else if (const auto* sub = reader.sized(sig("BNAM"), 6)) {
    out.playback = SoundPlayback{static_cast<std::int8_t>(std::to_integer<std::uint8_t>(sub->data[0])),
                                static_cast<std::int8_t>(std::to_integer<std::uint8_t>(sub->data[1])),
                                std::to_integer<std::uint8_t>(sub->data[2]),
                                std::to_integer<std::uint8_t>(sub->data[3]), Reader::u16(*sub, 4)};
  }
  out.rate_files = decode_sound_rate_files(*ref.record);
  for (const auto& sub : ref.record->subrecords) {
    if (sub.signature == sig("ANAM")) out.files.push_back(string(reader, sub));
    else if (sub.signature == sig("LNAM")) {
      if (sub.data.size() != 4) reader.fail(sub.signature, "controls must be four bytes");
      std::array<std::uint8_t, 4> bytes{};
      for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = std::to_integer<std::uint8_t>(sub.data[i]);
      out.controls = bytes;
    } else if (sub.signature == sig("DNAM")) {
      if (sub.data.size() != 4) reader.fail(sub.signature, "must be one descriptor link");
      out.descriptors.push_back(reader.link_at(sub, 0));
    }
  }
  return out;
}

std::optional<SoundRepeat> decode_sound_repeat(const Record& record) {
  if (const auto* sub = Reader::unique(record, sig("REPT"))) {
    if (sub->data.size() != 8 && sub->data.size() != 9) Reader::fail(record, sub->signature, "must be eight or nine bytes");
    SoundRepeat repeat{Reader::f32(*sub, 0), Reader::f32(*sub, 4), std::nullopt};
    if (sub->data.size() == 9) repeat.stackable = std::to_integer<std::uint8_t>(sub->data[8]);
    return repeat;
  }
  return std::nullopt;
}

SoundMarkerData decode_sound_marker(const Environment& env, const RecordRef& ref) {
  const Reader reader(env, ref, sig("SOUN"));
  return {reader.link(sig("SDSC")), decode_sound_repeat(*ref.record)};
}

MusicTypeData decode_music_type(const Environment& env, const RecordRef& ref) {
  const Reader reader(env, ref, sig("MUSC"));
  MusicTypeData out;
  if (const auto* sub = reader.sized(sig("FNAM"), 4)) out.flags = Reader::u32(*sub, 0);
  if (const auto* sub = reader.sized(sig("PNAM"), 4)) {
    out.priority = Reader::u16(*sub, 0);
    out.ducking_hundredths = Reader::u16(*sub, 2);
  }
  out.fade_duration = reader.f32(sig("WNAM"));
  if (const auto* sub = reader.unique(sig("TNAM"))) links(reader, *sub, out.tracks);
  return out;
}

FootstepData decode_footstep(const Environment& env, const RecordRef& ref) {
  const Reader reader(env, ref, sig("FSTP"));
  return {reader.link(sig("DATA")), text(reader, sig("ANAM"))};
}

std::array<std::uint32_t, 5> decode_footstep_counts(const Record& record) {
  std::array<std::uint32_t, 5> out{};
  const auto* count = Reader::sized(record, sig("XCNT"), 20);
  const auto* data = Reader::unique(record, sig("DATA"));
  if (!count && !data) return out;
  if (!count || !data) Reader::fail(record, sig("DATA"), "requires both counts and links");
  std::uint64_t total = 0;
  for (std::size_t i = 0; i < out.size(); ++i) {
    out[i] = Reader::u32(*count, i * 4);
    total += out[i];
  }
  if (total * 4 != data->data.size()) Reader::fail(record, sig("DATA"), "does not match the five category counts");
  return out;
}

FootstepSetData decode_footstep_set(const Environment& env, const RecordRef& ref) {
  const Reader reader(env, ref, sig("FSTS"));
  FootstepSetData out;
  const auto counts = decode_footstep_counts(*ref.record);
  const auto* data = reader.unique(sig("DATA"));
  if (!data) return out;
  std::size_t offset = 0;
  for (std::size_t category = 5; category-- > 0;) {
    for (std::uint32_t i = 0; i < counts[category]; ++i, offset += 4) {
      out.categories[category].push_back(reader.link_at(*data, offset));
    }
  }
  return out;
}

}
