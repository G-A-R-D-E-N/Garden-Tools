#include "fo4/plugin/sound_shapes.hpp"

#include <cstddef>
#include <cstring>
#include <string>

namespace fo4 {
namespace {

[[noreturn]] void fail(const Record& record, Signature signature, const std::string& message) {
  throw RecordShapeError(record.header.signature.to_string() + " " + record.form_key.to_string() + " " +
                         signature.to_string() + " " + message);
}

const Subrecord* unique(const Record& record, Signature signature) {
  const Subrecord* found = nullptr;
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature != signature) continue;
    if (found != nullptr) fail(record, signature, "appears more than once");
    found = &subrecord;
  }
  return found;
}

const Subrecord* sized(const Record& record, Signature signature, std::size_t size) {
  const auto* subrecord = unique(record, signature);
  if (subrecord != nullptr && subrecord->data.size() != size) {
    fail(record, signature, "must be " + std::to_string(size) + " bytes");
  }
  return subrecord;
}

std::uint32_t u32(const Subrecord& subrecord, std::size_t offset) {
  std::uint32_t value = 0;
  std::memcpy(&value, subrecord.data.data() + offset, 4);
  return value;
}

float f32(const Subrecord& subrecord, std::size_t offset) {
  float value = 0.0f;
  std::memcpy(&value, subrecord.data.data() + offset, 4);
  return value;
}

}

std::vector<SoundRateFile> parse_sound_rate_files(const Record& record) {
  const auto* count = sized(record, sig_itmc, 4);
  std::vector<SoundRateFile> out;
  int state = 0;
  SoundRateFile rate;
  for (const auto& sub : record.subrecords) {
    if (sub.signature == sig_itms) {
      if (state != 0 || !sub.data.empty()) fail(record, sub.signature, "invalid rate start");
      state = 1;
    } else if (sub.signature == sig_intv) {
      if (state != 1 || sub.data.size() != 4) fail(record, sub.signature, "invalid rate RPM");
      rate.rpm = u32(sub, 0);
      state = 2;
    } else if (sub.signature == sig_fnam) {
      if (state != 2) fail(record, sub.signature, "rate filename is out of order");
      const auto file = zstring_view(sub);
      if (!file) fail(record, sub.signature, "must be a single NUL-terminated string");
      rate.file = *file;
      state = 3;
    } else if (sub.signature == sig_itme) {
      if (state != 3 || !sub.data.empty()) fail(record, sub.signature, "invalid rate end");
      out.push_back(rate);
      state = 0;
    }
  }
  if (state != 0 || (count ? u32(*count, 0) != out.size() : !out.empty())) {
    fail(record, sig_itmc, "rate group count does not match complete blocks");
  }
  return out;
}

std::optional<SoundRepeat> parse_sound_repeat(const Record& record) {
  if (const auto* sub = unique(record, sig_rept)) {
    if (sub->data.size() != 8 && sub->data.size() != 9) fail(record, sub->signature, "must be eight or nine bytes");
    SoundRepeat repeat{f32(*sub, 0), f32(*sub, 4), std::nullopt};
    if (sub->data.size() == 9) repeat.stackable = std::to_integer<std::uint8_t>(sub->data[8]);
    return repeat;
  }
  return std::nullopt;
}

std::array<std::uint32_t, 5> parse_footstep_counts(const Record& record) {
  std::array<std::uint32_t, 5> out{};
  const auto* count = sized(record, sig_xcnt, 20);
  const auto* data = unique(record, sig_data);
  if (!count && !data) return out;
  if (!count || !data) fail(record, sig_data, "requires both counts and links");
  std::uint64_t total = 0;
  for (std::size_t i = 0; i < out.size(); ++i) {
    out[i] = u32(*count, i * 4);
    total += out[i];
  }
  if (total * 4 != data->data.size()) fail(record, sig_data, "does not match the five category counts");
  return out;
}

}
