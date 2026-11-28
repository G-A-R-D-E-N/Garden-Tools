#include "fo4/environment/scene_targets.hpp"

#include "record_reader.hpp"
#include "fo4/plugin/scene_layout.hpp"

#include <algorithm>
#include <exception>
#include <span>

namespace fo4::env {
namespace {

using detail::Reader;

struct LinkRule {
  Signature tag;
  SceneLinkSlot slot;
};

constexpr std::array k_dialogue_links{LinkRule{sig("DATA"), SceneLinkSlot::topic},
                                      LinkRule{sig("VENC"), SceneLinkSlot::dialogue_subtype},
                                      LinkRule{sig("ONAM"), SceneLinkSlot::audio_output_override},
                                      LinkRule{sig("PNAM"), SceneLinkSlot::anim_arch_type}};

constexpr std::array k_start_links{LinkRule{sig("STSC"), SceneLinkSlot::topic}};

constexpr std::array k_radio_links{LinkRule{sig("DATA"), SceneLinkSlot::topic},
                                   LinkRule{sig("HTID"), SceneLinkSlot::sound},
                                   LinkRule{sig("VENC"), SceneLinkSlot::dialogue_subtype}};

constexpr std::array k_response_links{
    LinkRule{sig("PTOP"), SceneLinkSlot::player_positive_response},
    LinkRule{sig("NTOP"), SceneLinkSlot::player_negative_response},
    LinkRule{sig("NETO"), SceneLinkSlot::player_neutral_response},
    LinkRule{sig("QTOP"), SceneLinkSlot::player_question_response},
    LinkRule{sig("VENC"), SceneLinkSlot::player_positive_subtype},
    LinkRule{sig("PLVD"), SceneLinkSlot::player_negative_subtype},
    LinkRule{sig("JOUT"), SceneLinkSlot::player_neutral_subtype},
    LinkRule{sig("DALC"), SceneLinkSlot::player_question_subtype},
    LinkRule{sig("ONAM"), SceneLinkSlot::audio_output_override}};

constexpr std::array k_player_links{
    LinkRule{sig("NPOT"), SceneLinkSlot::npc_positive_response},
    LinkRule{sig("NNGT"), SceneLinkSlot::npc_negative_response},
    LinkRule{sig("NNUT"), SceneLinkSlot::npc_neutral_response},
    LinkRule{sig("NQUT"), SceneLinkSlot::npc_question_response},
    LinkRule{sig("NPOS"), SceneLinkSlot::npc_positive_subtype},
    LinkRule{sig("NNGS"), SceneLinkSlot::npc_negative_subtype},
    LinkRule{sig("NNUS"), SceneLinkSlot::npc_neutral_subtype},
    LinkRule{sig("NQUS"), SceneLinkSlot::npc_question_subtype}};

const LinkRule* find_link(std::uint16_t type, Signature tag) {
  const auto find = [&](const auto& rules) -> const LinkRule* {
    const auto it = std::find_if(rules.begin(), rules.end(), [&](const LinkRule& rule) { return rule.tag == tag; });
    return it == rules.end() ? nullptr : &*it;
  };
  switch (type) {
    case 0: return find(k_dialogue_links);
    case 3:
      if (const auto* rule = find(k_response_links)) return rule;
      return find(k_player_links);
    case 4: return find(k_start_links);
    case 5: return find(k_response_links);
    case 6: return find(k_radio_links);
    default: return nullptr;
  }
}

std::vector<std::int32_t> alias_array(const Subrecord& sub) {
  std::vector<std::int32_t> out;
  for (std::size_t at = 0; at < sub.data.size(); at += 4) out.push_back(Reader::i32(sub, at));
  return out;
}

SceneActionTarget decode_action(const Reader& r, std::uint16_t type, std::span<const Subrecord> payload,
                                std::size_t base) {
  SceneActionTarget t;
  t.type = type;
  if (type > 6) return t;
  std::vector<Signature> seen;
  std::optional<std::size_t> open;
  const auto close_entry = [&](std::size_t end) {
    if (!open) return;
    try {
      t.start_entries.back().conditions = read_ctda_conditions_in(*r.ref().record, *open + 1, end);
    } catch (const std::exception&) {
      t.malformed = true;
    }
    open.reset();
  };
  const auto first = [&](Signature tag) {
    if (std::find(seen.begin(), seen.end(), tag) != seen.end()) return false;
    seen.push_back(tag);
    return true;
  };
  for (std::size_t i = 0; i < payload.size(); ++i) {
    const auto& sub = payload[i];
    const auto tag = sub.signature;
    const auto size = sub.data.size();
    const auto fixed = [&](std::size_t want, auto&& assign) {
      if (size == want && first(tag)) assign();
      else t.malformed = true;
    };
    if (type == 4 && tag == sig("LCEP")) {
      close_entry(base + i);
      if (size != 4) { t.malformed = true; continue; }
      t.start_entries.push_back({r.link_at(sub, 0), 0, std::nullopt, {}});
      open = base + i;
    } else if (type == 4 && open && tag == sig("INTT")) {
      if (size == 2) t.start_entries.back().phase_index = Reader::u16(sub, 0);
      else t.malformed = true;
    } else if (type == 4 && open && tag == sig("SSPN")) {
      const auto name = zstring_view(sub);
      if (name) t.start_entries.back().phase_name = *name;
      else t.malformed = true;
    } else if (type == 4 && open && (tag == sig("CITC") || tag == sig("CTDA") || tag == sig("CIS1") || tag == sig("CIS2"))) {
      continue;
    } else if (type == 4 && tag == sig("HTID")) {
      close_entry(base + i);
      if (size == 0) t.end_scene_greeting = true;
      else t.malformed = true;
    } else if (type == 1 && tag == sig("PNAM")) {
      if (size == 4) t.packages.push_back(r.link_at(sub, 0));
      else t.malformed = true;
    } else if (const auto* rule = find_link(type, tag)) {
      fixed(4, [&] { t.links.emplace_back(rule->slot, r.link_at(sub, 0)); });
    } else if ((type == 0 || type == 6) && tag == sig("DMAX")) {
      fixed(4, [&] { t.max_seconds = Reader::f32(sub, 0); });
    } else if (type == 0 && tag == sig("DMIN")) {
      fixed(4, [&] { t.min_seconds = Reader::f32(sub, 0); });
    } else if (type == 0 && tag == sig("CRIS")) {
      fixed(8, [&] { t.camera = std::array{Reader::f32(sub, 0), Reader::f32(sub, 4)}; });
    } else if (type == 0 && tag == sig("DEMO")) {
      fixed(4, [&] { t.emotion_type = Reader::u32(sub, 0); });
    } else if (type == 0 && tag == sig("DEVA")) {
      fixed(4, [&] { t.emotion_value = Reader::u32(sub, 0); });
    } else if ((type == 0 && tag == sig("HTID")) || ((type == 3 || type == 5) && tag == sig("DTID"))) {
      if (size % 4 == 0 && first(tag)) t.head_tracking = alias_array(sub);
      else t.malformed = true;
    } else if (type == 2 && tag == sig("SNAM")) {
      fixed(4, [&] { t.max_seconds = Reader::f32(sub, 0); });
    } else if (type == 2 && tag == sig("TNAM")) {
      fixed(4, [&] { t.min_seconds = Reader::f32(sub, 0); });
    } else if (type == 2 && tag == sig("SCQS")) {
      fixed(2, [&] { t.quest_stage = static_cast<std::int16_t>(Reader::u16(sub, 0)); });
    } else if (type == 3 && tag == sig("DTGT")) {
      fixed(4, [&] { t.dialogue_target_actor = Reader::i32(sub, 0); });
    } else {
      t.malformed = true;
    }
  }
  close_entry(base + payload.size());
  return t;
}

}  // namespace

SceneTargets decode_scene_targets(const Environment& env, const RecordRef& ref) {
  const Reader r(env, ref, sig("SCEN"));
  const auto subs = std::span{ref.record->subrecords};
  detail::SceneLayout layout;
  try { layout = detail::scene_layout(subs); }
  catch (const detail::SceneLayoutError& error) { r.fail(error.tag, error.what()); }
  SceneTargets out;
  const Subrecord* stage = nullptr;
  for (std::size_t i = layout.tail; i < subs.size(); ++i) {
    if (subs[i].signature != sig("SCQS")) continue;
    if (stage) r.fail(sig("SCQS"), "duplicate scoped field");
    stage = &subs[i];
  }
  if (stage) {
    if (stage->data.size() != 4) r.fail(sig("SCQS"), "invalid scoped field size");
    out.quest_stages = std::array{static_cast<std::int16_t>(Reader::u16(*stage, 0)),
                                  static_cast<std::int16_t>(Reader::u16(*stage, 2))};
  }
  for (const auto& span : layout.actions) {
    out.actions.push_back(decode_action(r, Reader::u16(subs[span.open], 0),
                                        subs.subspan(span.header_end, span.close - span.header_end), span.header_end));
  }
  return out;
}

}  // namespace fo4::env
