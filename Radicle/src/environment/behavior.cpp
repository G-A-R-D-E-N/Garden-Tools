#include "fo4/environment/behavior.hpp"

#include "record_reader.hpp"

namespace fo4::env {
namespace {

using detail::Reader;
constexpr Signature sig(const char (&s)[5]) { return Signature{s[0], s[1], s[2], s[3]}; }

const Subrecord* one(const Reader& r, std::span<const Subrecord> subs, Signature tag, std::size_t size) {
  const Subrecord* found = nullptr;
  for (const auto& sub : subs) {
    if (sub.signature != tag) continue;
    if (found || sub.data.size() != size) r.fail(tag, "duplicate or malformed scoped field");
    found = &sub;
  }
  return found;
}

std::optional<FormKey> link(const Reader& r, std::span<const Subrecord> subs, Signature tag) {
  const auto* sub = one(r, subs, tag, 4);
  return sub ? std::optional{r.link_at(*sub, 0)} : std::nullopt;
}

std::vector<CtdaCondition> conditions(const Reader& r, std::size_t begin, std::size_t end) {
  try { return read_ctda_conditions_in(*r.ref().record, begin, end); }
  catch (const std::exception& error) { r.fail(sig("CTDA"), error.what()); }
}

std::optional<std::string_view> text(const Reader& r, Signature tag) {
  const auto* sub = r.unique(tag); if (!sub) return std::nullopt;
  const auto value = zstring_view(*sub); if (!value) r.fail(tag, "invalid stored string"); return value;
}

template <std::size_t N>
std::optional<std::array<float, N>> floats(const Reader& r, Signature tag) {
  const auto* sub = r.sized(tag, N * 4); if (!sub) return std::nullopt;
  std::array<float, N> out{}; for (std::size_t i = 0; i < N; ++i) out[i] = Reader::f32(*sub, i * 4); return out;
}

std::vector<float> variable_floats(const Reader& r, Signature tag, std::size_t minimum, std::size_t maximum) {
  const auto* sub = r.unique(tag); if (!sub) return {};
  if (sub->data.size() < minimum * 4 || sub->data.size() > maximum * 4 || sub->data.size() % 4 != 0)
    r.fail(tag, "unsupported stored array size");
  std::vector<float> out; for (std::size_t i = 0; i < sub->data.size(); i += 4) out.push_back(Reader::f32(*sub, i)); return out;
}

}

Package decode_package(const Environment& env, const RecordRef& ref) {
  const Reader r(env, ref, sig("PACK")); Package out; const auto subs = std::span{ref.record->subrecords};
  if (const auto* sub = r.sized(sig("PKDT"), 12)) out.data = PackageData{Reader::u32(*sub, 0),
      std::to_integer<std::uint8_t>(sub->data[4]), std::to_integer<std::uint8_t>(sub->data[5]),
      std::to_integer<std::uint8_t>(sub->data[6]), std::to_integer<std::uint8_t>(sub->data[7]), Reader::u16(*sub, 8), Reader::u16(*sub, 10)};
  if (const auto* sub = r.sized(sig("PSDT"), 12)) out.schedule = PackageSchedule{
      static_cast<std::int8_t>(std::to_integer<std::uint8_t>(sub->data[0])),
      static_cast<std::int8_t>(std::to_integer<std::uint8_t>(sub->data[1])),
      static_cast<std::int8_t>(std::to_integer<std::uint8_t>(sub->data[2])),
      static_cast<std::int8_t>(std::to_integer<std::uint8_t>(sub->data[3])),
      static_cast<std::int8_t>(std::to_integer<std::uint8_t>(sub->data[4])), Reader::u32(*sub, 8)};
  std::size_t boundary = subs.size();
  if (const auto* sub = r.sized(sig("PKCU"), 12)) {
    boundary = static_cast<std::size_t>(sub - subs.data());
    out.counter = PackageCounter{Reader::u32(*sub, 0), Reader::u32(*sub, 8), r.link_at(*sub, 4)};
    out.opaque = subs.subspan(boundary + 1);
  }
  out.combat_style = link(r, subs.first(boundary), sig("CNAM")); out.owner_quest = link(r, subs.first(boundary), sig("QNAM"));
  out.conditions = conditions(r, 0, boundary);
  if (const auto* sub = r.unique(sig("IDLA"))) {
    if (sub->data.size() % 4 != 0) r.fail(sig("IDLA"), "must be a whole number of form links");
    for (std::size_t i = 0; i < sub->data.size(); i += 4) out.idle_animations.push_back(r.link_at(*sub, i));
  }
  for (std::size_t i = 0; i < subs.size(); ++i) {
    const auto& sub = subs[i];
    if (sub.signature == sig("PLDT") && (sub.data.size() == 8 || sub.data.size() == 12 || sub.data.size() == 16)) {
      const auto type = Reader::i32(sub, 0);
      if (type < 0 || type > 14) continue;
      PackageLocation location{i, type, Reader::u32(sub, 4), {}, {}, {}};
      if (sub.data.size() >= 12) location.radius = Reader::i32(sub, 8);
      if (sub.data.size() == 16) location.item_index = Reader::u32(sub, 12);
      if (type == 0 || type == 1 || type == 4 || type == 6) location.form = r.link_at(sub, 4);
      out.locations.push_back(std::move(location));
    } else if (sub.signature == sig("PTDA") && sub.data.size() == 12) {
      const auto type = Reader::i32(sub, 0);
      if (type < 0 || type > 8) continue;
      PackageTarget target{i, type, Reader::u32(sub, 4), Reader::i32(sub, 8), {}};
      if (type == 0 || type == 1 || type == 3 || type == 7) target.form = r.link_at(sub, 4);
      out.targets.push_back(std::move(target));
    }
  }
  const Signature tags[] = {sig("POBA"), sig("POEA"), sig("POCA")};
  std::optional<FormKey>* outputs[] = {&out.begin_idle, &out.end_idle, &out.change_idle};
  for (std::size_t t = 0; t < 3; ++t) {
    const auto* marker = one(r, subs, tags[t], 0); if (!marker) continue;
    const auto at = static_cast<std::size_t>(marker - subs.data());
    std::size_t end = at + 1;
    while (end < subs.size() && subs[end].signature != sig("POBA") && subs[end].signature != sig("POEA") &&
           subs[end].signature != sig("POCA")) ++end;
    *outputs[t] = link(r, subs.subspan(at + 1, end - at - 1), sig("INAM"));
  }
  return out;
}

CombatStyle decode_combat_style(const Environment& env, const RecordRef& ref) {
  const Reader r(env, ref, sig("CSTY")); CombatStyle out;
  out.general = floats<12>(r, sig("CSGD")); out.melee = variable_floats(r, sig("CSME"), 9, 10);
  out.long_range = variable_floats(r, sig("CSLR"), 3, 5); out.flight = floats<8>(r, sig("CSFL"));
  out.ranged_accuracy = r.f32(sig("CSRA")); out.cover_search = r.f32(sig("CSCV"));
  if (const auto* sub = r.sized(sig("CSCR"), 44)) {
    CombatCloseRange close; for (std::size_t i = 0; i < close.values.size(); ++i) close.values[i] = Reader::f32(*sub, i * 4);
    close.throw_max_targets = Reader::u32(*sub, 36); close.flank_variance = Reader::f32(*sub, 40); out.close_range = close;
  }
  if (const auto* sub = r.sized(sig("DATA"), 4)) out.flags = Reader::u32(*sub, 0);
  return out;
}

Idle decode_idle(const Environment& env, const RecordRef& ref) {
  const Reader r(env, ref, sig("IDLE")); Idle out;
  if (const auto* sub = r.sized(sig("ANAM"), 8)) { out.parent = r.link_at(*sub, 0); out.previous = r.link_at(*sub, 4); }
  if (const auto* sub = r.sized(sig("DATA"), 6)) out.data = IdleData{
      std::to_integer<std::uint8_t>(sub->data[0]), std::to_integer<std::uint8_t>(sub->data[1]),
      std::to_integer<std::uint8_t>(sub->data[2]), std::to_integer<std::uint8_t>(sub->data[3]), Reader::u16(*sub, 4)};
  out.behavior_graph = text(r, sig("DNAM")); out.animation_event = text(r, sig("ENAM")); out.animation_file = text(r, sig("GNAM"));
  out.conditions = conditions(r, 0, ref.record->subrecords.size()); return out;
}

}
