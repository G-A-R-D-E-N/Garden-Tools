#include "fo4/forms/npc_gameplay.hpp"

#include "gameplay_detail.hpp"
#include "npc_visual_detail.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fo4::forms {
namespace {

using env::Environment;
using env::RecordRef;
using detail::add_stub;
using gameplay_detail::acbs_of;
using gameplay_detail::first_subrecord;
using gameplay_detail::link_at;
using gameplay_detail::link_of;
using gameplay_detail::read_at;

constexpr std::uint32_t k_level_mult_flag = 0x80;
constexpr std::uint16_t k_template_stats = 0x2;
constexpr std::size_t k_acbs_template_flags = 14;
constexpr std::size_t k_tpta_stats_offset = 4;
constexpr std::size_t k_max_template_hops = 8;

std::optional<FormKey> template_target(const Environment& environment,
                                       const RecordRef& ref,
                                       std::size_t tpta_offset = k_tpta_stats_offset) {
  const auto* tpta = first_subrecord(*ref.record, sig_tpta);
  if (tpta != nullptr && tpta->data.size() >= tpta_offset + 4) {
    if (const auto key = link_at(environment, ref, *tpta, tpta_offset)) return key;
  }
  return link_of(environment, ref, sig_tplt);
}

void follow_template_chain(const Environment& environment, const RecordRef& npc, NpcGameplay& out) {
  const RecordRef* current = &npc;
  for (std::size_t hops = 0;; ++hops) {
    const auto flags = read_at<std::uint16_t>(acbs_of(*current->record), k_acbs_template_flags);
    if ((flags & k_template_stats) == 0) return;
    const auto target = template_target(environment, *current);
    if (!target) return;
    if (hops == k_max_template_hops) {
      add_stub(&out.stubs, "template chain from " + npc.record->form_key.to_string() +
                               " is deeper than 8; stopped at " + current->record->form_key.to_string());
      return;
    }
    out.template_chain.push_back(*target);
    const auto* next = environment.winner(*target);
    if (next == nullptr || next->record->header.flags.is_deleted()) {
      add_stub(&out.stubs, "template " + target->to_string() + " not found");
      return;
    }
    if (next->record->header.signature != sig_npc_) {
      add_stub(&out.stubs, "template " + target->to_string() + " is not an NPC_");
      return;
    }
    if (next->record->undecodable) {
      add_stub(&out.stubs, "template " + target->to_string() + " is undecodable");
      return;
    }
    current = next;
  }
}

constexpr Signature sig_dplt_local = Signature::from_string("DPLT");
constexpr Signature sig_pkid_local = Signature::from_string("PKID");
constexpr Signature sig_fltv_local = Signature::from_string("FLTV");
constexpr Signature sig_glob_local = Signature::from_string("GLOB");
constexpr std::uint32_t k_glob_constant_flag = 0x40;

struct TemplateHop {
  const RecordRef* next{nullptr};
  NpcTemplateReason reason{NpcTemplateReason::own};
};

TemplateHop step_to_template(const Environment& environment, const FormKey& target) {
  const auto* next = environment.winner(target);
  if (next == nullptr || next->record->header.flags.is_deleted()) return {nullptr, NpcTemplateReason::not_found};
  if (next->record->header.signature == sig_lvln) return {nullptr, NpcTemplateReason::leveled_list};
  if (next->record->header.signature != sig_npc_) return {nullptr, NpcTemplateReason::not_npc};
  if (next->record->undecodable) return {nullptr, NpcTemplateReason::undecodable};
  return {next, NpcTemplateReason::own};
}

const RecordRef& source_ref(const Environment& environment, const RecordRef& npc, const NpcTemplateSource& source) {
  return source.hops == 0 ? npc : *environment.winner(source.source);
}

void append_links(const Environment& environment, const RecordRef& ref, Signature signature, std::vector<FormKey>& out) {
  for (const auto& subrecord : ref.record->subrecords) {
    if (subrecord.signature != signature) continue;
    if (subrecord.data.size() != 4) {
      throw GameplayError(ref.record->form_key.to_string() + " " + signature.to_string() + " must be 4 bytes");
    }
    if (const auto key = link_at(environment, ref, subrecord, 0)) out.push_back(*key);
  }
}

bool follows_template(const NpcTemplateSource& source) {
  return source.reason == NpcTemplateReason::own || source.reason == NpcTemplateReason::template_npc;
}

}  // namespace

NpcTemplateSource resolve_npc_template_source(const Environment& environment,
                                              const RecordRef& npc,
                                              NpcTemplateSlot slot) {
  if (npc.record->header.signature != sig_npc_) {
    throw GameplayError(npc.record->form_key.to_string() + " is not an NPC_");
  }
  const auto bit = static_cast<unsigned>(slot);
  const RecordRef* current = &npc;
  NpcTemplateSource out;
  for (std::uint32_t hops = 0;; ++hops) {
    out.source = current->record->form_key;
    out.hops = hops;
    const auto flags = read_at<std::uint16_t>(acbs_of(*current->record), k_acbs_template_flags);
    if (((flags >> bit) & 1u) == 0) {
      out.reason = hops == 0 ? NpcTemplateReason::own : NpcTemplateReason::template_npc;
      return out;
    }
    const auto target = template_target(environment, *current, bit * 4);
    if (!target) {
      out.reason = NpcTemplateReason::no_link;
      return out;
    }
    out.blocker = target;
    if (hops == k_max_template_hops) {
      out.reason = NpcTemplateReason::too_deep;
      return out;
    }
    const auto hop = step_to_template(environment, *target);
    if (hop.next == nullptr) {
      out.reason = hop.reason;
      return out;
    }
    const auto* next = hop.next;
    out.blocker.reset();
    current = next;
  }
}

NpcPackageList npc_package_list(const Environment& environment, const RecordRef& npc) {
  NpcPackageList out;
  out.source = resolve_npc_template_source(environment, npc, NpcTemplateSlot::ai_packages);
  if (!follows_template(out.source)) return out;
  const auto defaults = resolve_npc_template_source(environment, npc, NpcTemplateSlot::def_pack_list);
  if (!follows_template(defaults)) {
    out.source = defaults;
    return out;
  }
  append_links(environment, source_ref(environment, npc, out.source), sig_pkid_local, out.keys);
  const auto& defaults_ref = source_ref(environment, npc, defaults);
  if (const auto list = link_of(environment, defaults_ref, sig_dplt_local)) {
    const auto* found = environment.winner(*list);
    if (found != nullptr && found->record->header.signature == sig_flst) {
      if (found->record->undecodable) throw GameplayError(list->to_string() + " is undecodable");
      append_links(environment, *found, sig_lnam, out.keys);
    }
  }
  return out;
}

NpcVoiceType resolve_npc_voice_type(const Environment& environment, const RecordRef& npc) {
  if (npc.record->header.signature != sig_npc_) {
    throw GameplayError(npc.record->form_key.to_string() + " is not an NPC_");
  }
  NpcVoiceType out;
  const RecordRef* current = &npc;
  for (std::uint32_t hops = 0;; ++hops) {
    out.origin.source = current->record->form_key;
    out.origin.hops = hops;
    if (const auto voice = link_of(environment, *current, sig_vtck)) {
      out.voice = voice;
      out.origin.reason = hops == 0 ? NpcTemplateReason::own : NpcTemplateReason::template_npc;
      return out;
    }
    const auto target = link_of(environment, *current, sig_tplt);
    if (!target) {
      out.origin.reason = NpcTemplateReason::no_link;
      return out;
    }
    out.origin.blocker = target;
    if (hops == k_max_template_hops) {
      out.origin.reason = NpcTemplateReason::too_deep;
      return out;
    }
    const auto hop = step_to_template(environment, *target);
    if (hop.next == nullptr) {
      out.origin.reason = hop.reason;
      return out;
    }
    out.origin.blocker.reset();
    current = hop.next;
  }
}

GlobalValue read_global_value(const RecordRef& glob, bool constant_only) {
  const auto& record = *glob.record;
  if (record.header.signature != sig_glob_local) return {GlobalValueReason::not_glob, 0.0};
  if (constant_only && (record.header.flags.value() & k_glob_constant_flag) == 0) {
    return {GlobalValueReason::not_constant, 0.0};
  }
  const auto* fltv = first_subrecord(record, sig_fltv_local);
  if (fltv == nullptr) return {GlobalValueReason::no_value, 0.0};
  if (fltv->data.size() != 4) throw GameplayError(record.form_key.to_string() + " FLTV must be 4 bytes");
  return {GlobalValueReason::ok, static_cast<double>(read_at<float>(*fltv, 0))};
}

NpcGameplay decode_npc_gameplay(const Environment& environment, const RecordRef& npc) {
  const auto& record = *npc.record;
  if (record.header.signature != sig_npc_) {
    throw GameplayError(record.form_key.to_string() + " is not an NPC_");
  }
  const auto& acbs = acbs_of(record);

  NpcGameplay out;
  out.npc = record.form_key;
  out.configuration_flags = read_at<std::uint32_t>(acbs, 0);
  out.level_raw = read_at<std::uint16_t>(acbs, 6);
  out.calc_min_level = read_at<std::uint16_t>(acbs, 8);
  out.calc_max_level = read_at<std::uint16_t>(acbs, 10);
  out.template_flags = read_at<std::uint16_t>(acbs, k_acbs_template_flags);
  out.level_is_multiplier = (out.configuration_flags & k_level_mult_flag) != 0;

  out.race = link_of(environment, npc, sig_rnam);
  out.class_form = link_of(environment, npc, sig_cnam);
  out.combat_style = link_of(environment, npc, sig_znam);
  out.crime_faction = link_of(environment, npc, sig_crif);
  out.template_base = template_target(environment, npc);
  follow_template_chain(environment, npc, out);

  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature != sig_splo) continue;
    if (subrecord.data.size() != 4) {
      throw GameplayError(record.form_key.to_string() + " SPLO must be 4 bytes");
    }
    if (const auto key = link_at(environment, npc, subrecord, 0)) out.spells.push_back(*key);
  }

  out.factions = decode_npc_factions(environment.plugin(npc.plugin), record.form_key);
  out.perks = decode_npc_perks(environment.plugin(npc.plugin), record.form_key);

  if (const auto* aidt = first_subrecord(record, sig_aidt)) {
    out.ai = NpcAiData{
        read_at<std::uint8_t>(*aidt, 0), read_at<std::uint8_t>(*aidt, 1),
        read_at<std::uint8_t>(*aidt, 2), read_at<std::uint8_t>(*aidt, 3),
        read_at<std::uint8_t>(*aidt, 4), read_at<std::uint8_t>(*aidt, 5),
    };
  }
  if (const auto* dnam = first_subrecord(record, sig_dnam)) {
    out.calculated = NpcCalculatedStats{
        read_at<std::uint16_t>(*dnam, 0),
        read_at<std::uint16_t>(*dnam, 2),
    };
  }
  return out;
}

}  // namespace fo4::forms
