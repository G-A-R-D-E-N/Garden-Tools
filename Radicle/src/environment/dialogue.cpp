#include "fo4/environment/dialogue.hpp"
#include "fo4/plugin/info_responses.hpp"

#include "record_reader.hpp"
#include "fo4/environment/scene_layout.hpp"

#include <algorithm>

namespace fo4::env {
namespace {

using detail::Reader;
constexpr Signature sig(const char (&s)[5]) { return Signature{s[0], s[1], s[2], s[3]}; }

struct Scope {
  const Reader& reader;
  std::span<const Subrecord> subs;
  const Subrecord* one(Signature tag) const {
    const Subrecord* found = nullptr;
    for (const auto& sub : subs) {
      if (sub.signature != tag) continue;
      if (found) reader.fail(tag, "duplicate scoped field");
      found = &sub;
    }
    return found;
  }
  const Subrecord* sized(Signature tag, std::size_t size) const {
    const auto* sub = one(tag);
    if (sub && sub->data.size() != size) reader.fail(tag, "invalid scoped field size");
    return sub;
  }
  std::optional<std::string_view> text(Signature tag) const {
    const auto* sub = one(tag);
    if (!sub) return std::nullopt;
    const auto value = zstring_view(*sub);
    if (!value) reader.fail(tag, "invalid stored string");
    return value;
  }
  std::optional<FormKey> link(Signature tag) const {
    const auto* sub = sized(tag, 4);
    return sub ? std::optional{reader.link_at(*sub, 0)} : std::nullopt;
  }
  std::optional<std::uint32_t> u32(Signature tag) const {
    const auto* sub = sized(tag, 4);
    return sub ? std::optional{Reader::u32(*sub, 0)} : std::nullopt;
  }
  std::optional<std::int32_t> i32(Signature tag) const {
    const auto* sub = sized(tag, 4);
    return sub ? std::optional{Reader::i32(*sub, 0)} : std::nullopt;
  }
};

RecordName localized(const Environment& env, const RecordRef& ref, const Reader& r, const Subrecord* sub) {
  if (!sub) return {};
  if (env.plugins().at(ref.plugin).localized) {
    if (sub->data.size() != 4) r.fail(sub->signature, "string ID must be four bytes");
    return {NameKind::string_id, {}, Reader::u32(*sub, 0)};
  }
  const auto value = zstring_view(*sub);
  if (!value) r.fail(sub->signature, "invalid inline text");
  return {NameKind::inline_text, *value, 0};
}

std::vector<CtdaCondition> conditions(const Reader& r, std::size_t begin, std::size_t end) {
  try { return read_ctda_conditions_in(*r.ref().record, begin, end); }
  catch (const std::exception& error) { r.fail(sig("CTDA"), error.what()); }
}

constexpr std::array<std::string_view, 7> scene_action_type_names{
    "dialogue", "package", "timer", "player_dialogue", "start_scene", "npc_response_dialogue", "radio"};

}

std::optional<double> DialogueInfo::reset_hours() const {
  if (!data) return std::nullopt;
  return (*data)[1] * 24.0 / 65535.0;
}

std::optional<SceneActionType> scene_action_type(std::uint16_t type) {
  if (type >= scene_action_type_names.size()) return std::nullopt;
  return static_cast<SceneActionType>(type);
}

std::string_view scene_action_type_name(std::uint16_t type) {
  return scene_action_type(type) ? scene_action_type_names[type] : std::string_view{""};
}

DialogueTopic decode_dialogue_topic(const Environment& env, const RecordRef& ref) {
  const Reader r(env, ref, sig("DIAL")); DialogueTopic out;
  if (const auto* sub = r.sized(sig("DATA"), 4))
    out.data = DialogueTopic::Data{std::to_integer<std::uint8_t>(sub->data[0]),
                                  std::to_integer<std::uint8_t>(sub->data[1]), Reader::u16(*sub, 2)};
  out.priority = r.f32(sig("PNAM"));
  if (const auto* sub = r.sized(sig("TIFC"), 4)) out.info_count = Reader::u32(*sub, 0);
  if (const auto* sub = r.sized(sig("SNAM"), 4)) {
    std::array<std::uint8_t, 4> name{};
    for (std::size_t i = 0; i < name.size(); ++i) name[i] = std::to_integer<std::uint8_t>(sub->data[i]);
    out.subtype_name = name;
  }
  out.branch = r.link(sig("BNAM")); out.quest = r.link(sig("QNAM")); out.keyword = r.link(sig("KNAM"));
  return out;
}

DialogueInfo decode_dialogue_info(const Environment& env, const RecordRef& ref) {
  const Reader r(env, ref, sig("INFO")); DialogueInfo out;
  if (const auto* sub = r.sized(sig("ENAM"), 4)) out.data = std::array{Reader::u16(*sub, 0), Reader::u16(*sub, 2)};
  out.previous_topic = r.link(sig("TPIC")); out.previous_info = r.link(sig("PNAM"));
  out.shared_info = r.link(sig("DNAM")); out.info_group = r.link(sig("GNAM"));
  out.speaker = r.link(sig("ANAM")); out.start_scene = r.link(sig("TSCE"));
  out.prompt = localized(env, ref, r, r.unique(sig("RNAM")));
  const auto subs = std::span{ref.record->subrecords};
  for (const auto& at : read_info_responses(*ref.record)) {
    if (at.malformed) r.fail(sig("TRDA"), "response data must be twenty bytes");
    const Scope response{r, subs.subspan(at.first, at.last - at.first)};
    const auto* text = response.one(sig("NAM1"));
    if (!text) r.fail(sig("TRDA"), "response has no NAM1 text");
    DialogueResponse value;
    value.emotion = r.link_at(subs[at.trda], 0); value.sound = r.link_at(subs[at.trda], 5);
    value.number = at.number;
    value.unknown = at.unknown;
    value.interrupt_percent = at.interrupt_percent;
    value.camera_target_alias = at.camera_target_alias;
    value.camera_location_alias = at.camera_location_alias;
    value.text = localized(env, ref, r, text);
    value.speaker_idle = response.link(sig("SNAM")); value.listener_idle = response.link(sig("LNAM"));
    out.responses.push_back(std::move(value));
  }
  out.conditions = conditions(r, 0, subs.size());
  return out;
}

Scene decode_scene(const Environment& env, const RecordRef& ref) {
  const Reader r(env, ref, sig("SCEN")); Scene out;
  const auto subs = std::span{ref.record->subrecords};
  detail::SceneLayout layout;
  try { layout = detail::scene_layout(subs); }
  catch (const detail::SceneLayoutError& error) { r.fail(error.tag, error.what()); }
  for (const auto& span : layout.phases) {
    const Scope phase{r, subs.subspan(span.open + 1, span.close - span.open - 1)};
    ScenePhase p; p.name = phase.text(sig("NAM0")); p.editor_width = phase.u32(sig("WNAM"));
    if (const auto* value = phase.sized(sig("FNAM"), 2)) p.flags = Reader::u16(*value, 0);
    if (const auto* value = phase.sized(sig("SCQS"), 4))
      p.quest_stages = std::array{static_cast<std::int16_t>(Reader::u16(*value, 0)),
                                  static_cast<std::int16_t>(Reader::u16(*value, 2))};
    p.start_conditions = conditions(r, span.open + 1, span.first_next);
    p.completion_conditions = conditions(r, span.first_next + 1, span.second_next);
    out.phases.push_back(std::move(p));
  }
  for (const auto at : layout.actors)
    out.actors.push_back({Reader::i32(subs[at], 0), Reader::u32(subs[at + 1], 0), Reader::u32(subs[at + 2], 0)});
  for (const auto& span : layout.actions) {
    const Scope header{r, subs.subspan(span.open + 1, span.header_end - span.open - 1)};
    SceneAction a; a.type = Reader::u16(subs[span.open], 0); a.name = header.text(sig("NAM0"));
    a.actor_alias = header.i32(sig("ALID")); a.index = header.u32(sig("INAM")); a.flags = header.u32(sig("FNAM"));
    a.start_phase = header.u32(sig("SNAM")); a.end_phase = header.u32(sig("ENAM"));
    a.payload = subs.subspan(span.header_end, span.close - span.header_end);
    out.actions.push_back(a);
  }
  out.flags = Scope{r, subs.first(layout.first_group)}.u32(sig("FNAM"));
  out.parent_quest = Scope{r, subs.subspan(layout.tail)}.link(sig("PNAM"));
  out.template_scene = Scope{r, subs.subspan(layout.tail)}.link(sig("TNAM"));
  out.index = Scope{r, subs.subspan(layout.tail)}.u32(sig("XNAM"));
  out.conditions = conditions(r, layout.tail, subs.size());
  return out;
}
}
