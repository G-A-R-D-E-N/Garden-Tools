#include "fo4/environment/material_swap.hpp"

#include "fo4/environment/placed.hpp"
#include "record_reader.hpp"

namespace fo4::env {
namespace {

using detail::Reader;

constexpr Signature sig(const char (&text)[5]) {
  return Signature{text[0], text[1], text[2], text[3]};
}

}  // namespace

std::vector<MaterialSubstitution> decode_material_swap(const Environment& env, const RecordRef& ref) {
  const Reader reader(env, ref, sig("MSWP"));
  std::vector<MaterialSubstitution> out;
  for (const auto& sub : ref.record->subrecords) {
    const auto s = sub.signature;
    if (s == sig("BNAM")) {
      const auto path = zstring_view(sub);
      if (!path) reader.fail(s, "is not a single NUL-terminated string");
      out.push_back({*path, std::nullopt, std::nullopt});
    } else if (s == sig("SNAM") || s == sig("CNAM")) {
      if (out.empty()) reader.fail(s, "does not follow a BNAM");
      auto& substitution = out.back();
      if (s == sig("SNAM")) {
        if (substitution.replacement_material) reader.fail(s, "appears more than once in a substitution");
        const auto path = zstring_view(sub);
        if (!path) reader.fail(s, "is not a single NUL-terminated string");
        substitution.replacement_material = path;
      } else {
        if (substitution.color_remapping_index) reader.fail(s, "appears more than once in a substitution");
        if (sub.data.size() != 4) reader.fail(s, "must be 4 bytes");
        substitution.color_remapping_index = Reader::f32(sub, 0);
      }
    }
  }
  return out;
}

std::optional<FormKey> placed_material_swap(const Environment& env, const RecordRef& ref) {
  if (ref.record == nullptr || !is_placed_reference_signature(ref.record->header.signature)) {
    throw WorldDecodeError("expected a placed reference record");
  }
  return Reader(env, ref).link(sig("XMSP"));
}

std::optional<FormKey> primary_model_material_swap(const Environment& env, const RecordRef& ref) {
  const auto path = primary_model_path(ref);
  if (!path) return std::nullopt;
  auto swap = sig("MODS");
  if (ref.record->header.signature == sig("ARMO")) {
    swap = sig("MO4S");
    for (const auto& sub : ref.record->subrecords) {
      if (sub.signature != sig("MOD2")) continue;
      if (path->data() == reinterpret_cast<const char*>(sub.data.data())) swap = sig("MO2S");
      break;
    }
  }
  return Reader(env, ref).link(swap);
}

}  // namespace fo4::env
