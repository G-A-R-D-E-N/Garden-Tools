#include "fo4/forms/npc_visual.hpp"

#include "npc_visual_detail.hpp"

#include "fo4/binary/checked.hpp"
#include "fo4/forms/leveled_list.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/schema/scalar.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace fo4::forms {
namespace {

using env::Environment;
using env::RecordRef;

using detail::add_stub;
using detail::ascii_lower;
using detail::k_max_leveled_nodes;
using detail::pick_leveled_impls;
using detail::winner_of;

constexpr std::size_t k_max_object_template_depth = 8;

// Constraint: the archive holds Actors\Robot\Animations\Idle.hkx and no race record names a robot idle.
constexpr std::string_view k_robot_skeleton = "meshes/actors/robot/characterassets/skeleton.hkx";
constexpr std::string_view k_robot_idle = "meshes/actors/robot/animations/idle.hkx";

constexpr std::size_t k_acbs_template_flags_end = 16;
constexpr std::size_t k_acbs_template_flags_offset = 14;
constexpr std::uint32_t k_acbs_female = 0x1;
constexpr std::uint16_t k_template_traits = 0x1;
constexpr std::uint16_t k_template_inventory = 0x100;
constexpr std::size_t k_tpta_traits_offset = 0;
constexpr std::size_t k_tpta_inventory_offset = 32;
constexpr std::size_t k_max_template_hops = 8;
constexpr std::size_t k_race_flags_offset = 32;
constexpr std::size_t k_max_armor_race_hops = 8;
constexpr std::uint32_t k_race_facegen_head = 0x2;
constexpr std::uint32_t k_head_slots = 0x7;
constexpr std::size_t k_qnam_size = 16;

constexpr std::string_view k_human_skeleton = "meshes/actors/character/characterassets/skeleton.hkx";
constexpr std::string_view k_human_idle = "meshes/actors/character/animations/h2h/idle.hkx";

struct Context {
  const Environment& env;
  NpcVisual& out;
  std::uint16_t level{1};
  std::uint64_t seed{0};

  void stub(std::string text) { add_stub(&out.stubs, std::move(text)); }
};

std::string describe(const Record& record, Signature signature) {
  return record.header.signature.to_string() + " " + record.form_key.to_string() + " " +
         signature.to_string();
}

const Subrecord* first_subrecord(const Record& record, Signature signature) {
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature == signature) return &subrecord;
  }
  return nullptr;
}

std::uint32_t read_u32(const Subrecord& subrecord, std::size_t offset) {
  return fo4::binary::checked::read_u32(subrecord.data, offset);
}

std::uint16_t read_u16(const Subrecord& subrecord, std::size_t offset) {
  return fo4::binary::checked::read_u16(subrecord.data, offset);
}

std::string zstring(const Subrecord& subrecord) {
  if (subrecord.data.empty()) return {};
  const auto* begin = reinterpret_cast<const char*>(subrecord.data.data());
  std::size_t size = 0;
  while (size < subrecord.data.size() && begin[size] != '\0') ++size;
  return std::string(begin, size);
}

std::optional<std::string> model_of(const Record& record, Signature signature) {
  const auto* subrecord = first_subrecord(record, signature);
  if (subrecord == nullptr) return std::nullopt;
  auto path = zstring(*subrecord);
  if (path.empty()) return std::nullopt;
  return path;
}

std::optional<FormKey> link_in(const Environment& env,
                               const RecordRef& ref,
                               const Subrecord& subrecord,
                               std::size_t offset) {
  const auto key = env.resolve_link(ref, RawFormId{read_u32(subrecord, offset)});
  if (key.owner().empty()) return std::nullopt;
  return key;
}

std::optional<FormKey> link_of(const Environment& env, const RecordRef& ref, Signature signature) {
  const auto* subrecord = first_subrecord(*ref.record, signature);
  if (subrecord == nullptr) return std::nullopt;
  if (subrecord->data.size() != 4) {
    throw NpcVisualError(describe(*ref.record, signature) + " must be 4 bytes");
  }
  return link_in(env, ref, *subrecord, 0);
}

const Subrecord& acbs_of(const Record& record) {
  const auto* acbs = first_subrecord(record, sig_acbs);
  if (acbs == nullptr || acbs->data.size() < k_acbs_template_flags_end) {
    throw NpcVisualError(describe(record, sig_acbs) + " must be at least 16 bytes");
  }
  return *acbs;
}

std::optional<FormKey> template_target(const Environment& env,
                                       const RecordRef& ref,
                                       std::size_t tpta_offset) {
  const auto* tpta = first_subrecord(*ref.record, sig_tpta);
  if (tpta != nullptr && tpta->data.size() >= tpta_offset + 4) {
    if (const auto key = link_in(env, ref, *tpta, tpta_offset)) return key;
  }
  return link_of(env, ref, sig_tplt);
}

const RecordRef* resolve_template(Context& ctx,
                                  const RecordRef& start,
                                  std::uint16_t flag,
                                  std::size_t tpta_offset) {
  const RecordRef* current = &start;
  for (std::size_t hops = 0;; ++hops) {
    const auto flags = read_u16(acbs_of(*current->record), k_acbs_template_flags_offset);
    if ((flags & flag) == 0) return current;
    const auto target = template_target(ctx.env, *current, tpta_offset);
    if (!target) return current;
    if (hops == k_max_template_hops) {
      ctx.stub("template chain from " + start.record->form_key.to_string() +
               " is deeper than 8; stopped at " + current->record->form_key.to_string());
      return current;
    }
    const auto* next = winner_of(ctx.env, *target);
    if (next == nullptr) {
      ctx.stub("template " + target->to_string() + " not found");
      return current;
    }
    const auto signature = next->record->header.signature;
    if (signature == sig_lvln) {
      std::size_t budget = k_max_leveled_nodes;
      const auto picked = pick_leveled_impls(ctx.env, *target, ctx.level, ctx.seed, 0,
                                             &ctx.out.stubs, budget, &ctx.out.seed_dependent);
      if (picked.empty()) return current;
      const auto* chosen = winner_of(ctx.env, picked.front());
      if (chosen == nullptr) {
        ctx.stub("leveled template " + target->to_string() + " picks " +
                 picked.front().to_string() + " which is not a live record");
        return current;
      }
      if (chosen->record->header.signature != sig_npc_) {
        ctx.stub("leveled template " + target->to_string() + " picks " +
                 picked.front().to_string() + " which is a " +
                 chosen->record->header.signature.to_string() + " record, not an NPC_");
        return current;
      }
      current = chosen;
      continue;
    }
    if (signature != sig_npc_) {
      ctx.stub("template " + target->to_string() + " is a " + signature.to_string() +
               " record, not an NPC_; not resolved");
      return current;
    }
    current = next;
  }
}

std::optional<std::string> race_skeleton_model(const Record& race, bool female) {
  const auto marker = female ? sig_fnam : sig_mnam;
  const auto& subrecords = race.subrecords;
  for (std::size_t i = 0; i + 1 < subrecords.size(); ++i) {
    if (subrecords[i].signature != marker || subrecords[i + 1].signature != sig_anam) continue;
    auto path = zstring(subrecords[i + 1]);
    if (!path.empty()) return path;
  }
  return std::nullopt;
}

std::uint32_t biped_slots(const Record& record) {
  const auto* bod2 = first_subrecord(record, sig_bod2);
  return bod2 != nullptr && bod2->data.size() >= 4 ? read_u32(*bod2, 0) : 0u;
}

std::optional<std::string> addon_model(const Record& addon, bool female) {
  if (female) {
    if (auto path = model_of(addon, sig_mod3)) return path;
  }
  return model_of(addon, sig_mod2);
}

using RaceChain = std::vector<FormKey>;

// Constraint: the engine's TESObjectARMA::UseModel follows RACE RNAM without a cycle guard; this stops at 8.
RaceChain armor_races(Context& ctx, const FormKey& race) {
  RaceChain chain{race};
  while (chain.size() <= k_max_armor_race_hops) {
    const auto* ref = winner_of(ctx.env, chain.back());
    if (ref == nullptr || ref->record->header.signature != sig_race) break;
    std::optional<FormKey> next;
    try {
      next = link_of(ctx.env, *ref, sig_rnam);
    } catch (const NpcVisualError& error) {
      ctx.stub(error.what());
    }
    if (!next || std::find(chain.begin(), chain.end(), *next) != chain.end()) break;
    const auto* parent = winner_of(ctx.env, *next);
    if (parent == nullptr || parent->record->header.signature != sig_race) break;
    chain.push_back(*next);
  }
  return chain;
}

std::optional<std::size_t> addon_race_rank(const Environment& env,
                                           const RecordRef& addon,
                                           const RaceChain& races) {
  std::optional<std::size_t> rank;
  for (const auto& subrecord : addon.record->subrecords) {
    if (subrecord.signature != sig_rnam && subrecord.signature != sig_modl) continue;
    if (subrecord.data.size() != 4) continue;
    const auto key = link_in(env, addon, subrecord, 0);
    if (!key) continue;
    const auto found = std::find(races.begin(), races.end(), *key);
    if (found == races.end()) continue;
    const auto index = static_cast<std::size_t>(found - races.begin());
    if (!rank || index < *rank) rank = index;
  }
  return rank;
}

struct MatchedAddon {
  std::size_t rank;
  std::uint32_t slots;
  std::optional<NpcVisualPart> part;
};

// Constraint: the engine attaches every addon that passes UseModel; which one wins a shared slot is unverified, so the earliest chain race wins.
void collect_addons(Context& ctx,
                    const RecordRef& armor,
                    const RaceChain& races,
                    bool female,
                    NpcVisualSource source,
                    std::vector<NpcVisualPart>& parts,
                    std::uint32_t* matched_slots = nullptr) {
  std::vector<MatchedAddon> matched;
  for (const auto& subrecord : armor.record->subrecords) {
    if (subrecord.signature != sig_modl) continue;
    if (subrecord.data.size() != 4) {
      ctx.stub(describe(*armor.record, sig_modl) + " must be 4 bytes");
      continue;
    }
    const auto key = link_in(ctx.env, armor, subrecord, 0);
    if (!key) continue;
    const auto* addon = winner_of(ctx.env, *key);
    if (addon == nullptr || addon->record->header.signature != sig_arma) {
      ctx.stub("armor addon " + key->to_string() + " is not a live ARMA record");
      continue;
    }
    const auto rank = addon_race_rank(ctx.env, *addon, races);
    if (!rank) continue;
    const auto slots = biped_slots(*addon->record);
    if (matched_slots != nullptr) *matched_slots |= slots;
    const auto model = addon_model(*addon->record, female);
    if (!model) {
      ctx.stub("armor addon " + key->to_string() + " has no " + (female ? "female" : "male") +
               " model");
      matched.push_back(MatchedAddon{*rank, slots, std::nullopt});
      continue;
    }
    matched.push_back(
        MatchedAddon{*rank, slots, NpcVisualPart{npc_visual_mesh_entry(*model), slots, source}});
  }
  for (auto& candidate : matched) {
    if (!candidate.part) continue;
    const auto dominated = std::any_of(matched.begin(), matched.end(), [&](const MatchedAddon& other) {
      return other.part && other.rank < candidate.rank && (other.slots & candidate.slots) != 0;
    });
    if (!dominated) parts.push_back(std::move(*candidate.part));
  }
}

void collect_outfit_armor(Context& ctx,
                          const RecordRef& armor,
                          const RaceChain& races,
                          bool female,
                          std::uint32_t& mask,
                          std::vector<NpcVisualPart>& parts) {
  collect_addons(ctx, armor, races, female, NpcVisualSource::outfit, parts, &mask);
}

void collect_outfit(Context& ctx,
                    const RecordRef& inventory,
                    const RaceChain& races,
                    bool female,
                    std::uint32_t& mask,
                    std::vector<NpcVisualPart>& parts) {
  const auto outfit_key = link_of(ctx.env, inventory, sig_doft);
  if (!outfit_key) return;
  const auto* outfit = winner_of(ctx.env, *outfit_key);
  if (outfit == nullptr || outfit->record->header.signature != sig_otft) {
    ctx.stub("outfit " + outfit_key->to_string() + " is not a live OTFT record");
    return;
  }
  for (const auto& list : outfit->record->subrecords) {
    if (list.signature != sig_inam) continue;
    if (list.data.size() % 4 != 0) {
      ctx.stub(describe(*outfit->record, sig_inam) + " is not a whole number of FormIDs");
      continue;
    }
    for (std::size_t offset = 0; offset + 4 <= list.data.size(); offset += 4) {
      const auto item_key = link_in(ctx.env, *outfit, list, offset);
      if (!item_key) continue;
      const auto* item = winner_of(ctx.env, *item_key);
      if (item == nullptr) {
        ctx.stub("outfit item " + item_key->to_string() + " not found");
        continue;
      }
      const auto signature = item->record->header.signature;
      if (signature == sig_lvli) {
        std::size_t budget = k_max_leveled_nodes;
        for (const auto& picked :
             pick_leveled_impls(ctx.env, *item_key, ctx.level, ctx.seed, 0, &ctx.out.stubs,
                                budget, &ctx.out.seed_dependent)) {
          const auto* armor = winner_of(ctx.env, picked);
          if (armor == nullptr) {
            ctx.stub("leveled item " + item_key->to_string() + " picks " + picked.to_string() +
                     " which is not a live record");
            continue;
          }
          if (armor->record->header.signature != sig_armo) {
            ctx.stub("leveled item " + item_key->to_string() + " picks " + picked.to_string() +
                     " which is a " + armor->record->header.signature.to_string() +
                     " record; not resolved");
            continue;
          }
          collect_outfit_armor(ctx, *armor, races, female, mask, parts);
        }
        continue;
      }
      if (signature != sig_armo) {
        ctx.stub("outfit item " + item_key->to_string() + " is a " + signature.to_string() +
                 " record; not resolved");
        continue;
      }
      collect_outfit_armor(ctx, *item, races, female, mask, parts);
    }
  }
}

void collect_skin(Context& ctx,
                  const FormKey& skin_key,
                  const RaceChain& races,
                  bool female,
                  std::uint32_t outfit_mask,
                  std::vector<NpcVisualPart>& parts) {
  const auto* skin = winner_of(ctx.env, skin_key);
  if (skin == nullptr || skin->record->header.signature != sig_armo) {
    ctx.stub("skin " + skin_key.to_string() + " is not a live ARMO record");
    return;
  }
  std::vector<NpcVisualPart> addons;
  collect_addons(ctx, *skin, races, female, NpcVisualSource::skin, addons);
  for (auto& part : addons) {
    if ((part.biped_slots & outfit_mask) == 0) parts.push_back(std::move(part));
  }
}

// Requires: the count call must precede the word reads, because the include rows fail a miscounted DATA.
std::vector<FormKey> omod_includes(const Plugin& plugin, const Record& record) {
  if (first_subrecord(record, sig_data) == nullptr) {
    throw schema::ScalarError("OMOD record has no data subrecord");
  }
  const auto field = schema::scalar_field_descriptor("OMOD\\IncludeMod");
  if (!field) throw schema::ScalarError("missing OMOD include row");
  const auto context = plugin_form_id_context(plugin);
  const auto count = record_field_count(record, *field);
  std::vector<FormKey> includes;
  for (std::size_t index = 0; index < count; ++index) {
    const auto word = record_scalar_text(plugin, record, *field, index);
    if (!word) throw schema::ScalarError("OMOD include word is missing");
    includes.push_back(resolve_plugin_form_id(
        RawFormId{static_cast<std::uint32_t>(std::stoull(*word))}, context));
  }
  return includes;
}

void collect_omod(Context& ctx,
                  const FormKey& key,
                  std::size_t depth,
                  std::vector<FormKey>& seen,
                  std::vector<NpcVisualPart>& parts) {
  if (depth >= k_max_object_template_depth) {
    ctx.stub("object template " + key.to_string() + " nests deeper than 8; stopped");
    return;
  }
  if (std::find(seen.begin(), seen.end(), key) != seen.end()) return;
  seen.push_back(key);
  const auto* ref = winner_of(ctx.env, key);
  if (ref == nullptr) {
    ctx.stub("object template item " + key.to_string() + " not found");
    return;
  }
  if (ref->record->header.signature != sig_omod) {
    ctx.stub("object template item " + key.to_string() + " is a " +
             ref->record->header.signature.to_string() + " record; not resolved");
    return;
  }
  if (const auto model = model_of(*ref->record, sig_modl)) {
    parts.push_back(
        NpcVisualPart{npc_visual_mesh_entry(*model), 0, NpcVisualSource::object_template});
  }
  std::vector<FormKey> includes;
  try {
    includes = omod_includes(ctx.env.plugin(ref->plugin), *ref->record);
  } catch (const schema::ScalarError& error) {
    ctx.stub("object template item " + key.to_string() + " did not decode: " + error.what());
    return;
  } catch (const FormIdError& error) {
    ctx.stub("object template item " + key.to_string() + " has a bad form ID: " + error.what());
    return;
  }
  for (const auto& include : includes) {
    if (include.owner().empty()) continue;
    collect_omod(ctx, include, depth + 1, seen, parts);
  }
}

// Constraint: a mid-chain NPC_'s OBTS must still count, so every template hop is checked for one.
const RecordRef* obts_source_in_chain(Context& ctx,
                                      const RecordRef& start,
                                      std::uint16_t flag,
                                      std::size_t tpta_offset) {
  const RecordRef* current = &start;
  for (std::size_t hops = 0; hops <= k_max_template_hops; ++hops) {
    if (first_subrecord(*current->record, sig_obts) != nullptr) return current;
    if ((read_u16(acbs_of(*current->record), k_acbs_template_flags_offset) & flag) == 0) {
      return nullptr;
    }
    const auto target = template_target(ctx.env, *current, tpta_offset);
    if (!target) return nullptr;
    const auto* next = winner_of(ctx.env, *target);
    if (next == nullptr) return nullptr;
    const auto signature = next->record->header.signature;
    if (signature == sig_npc_) {
      current = next;
      continue;
    }
    if (signature != sig_lvln) return nullptr;
    std::size_t budget = k_max_leveled_nodes;
    const auto picked = pick_leveled_impls(ctx.env, *target, ctx.level, ctx.seed, 0, &ctx.out.stubs,
                                           budget, &ctx.out.seed_dependent);
    if (picked.empty()) return nullptr;
    const auto* chosen = winner_of(ctx.env, picked.front());
    if (chosen == nullptr || chosen->record->header.signature != sig_npc_) return nullptr;
    current = chosen;
  }
  return nullptr;
}

// Constraint: the NPC_ OBTS rows index across every OBTS of the record, so a combination's rows start after the counts of the combinations before it.
std::optional<std::string> obts_row_text(const Plugin& plugin,
                                         const Record& record,
                                         std::string_view leaf,
                                         std::size_t index) {
  const auto field = schema::scalar_field_descriptor("NPC_\\ObjectTemplate" + std::string(leaf));
  if (!field) throw schema::ScalarError("missing NPC_ object template row " + std::string(leaf));
  return record_scalar_text(plugin, record, *field, index);
}

std::uint32_t obts_row_word(const Plugin& plugin,
                            const Record& record,
                            std::string_view leaf,
                            std::size_t index) {
  const auto value = obts_row_text(plugin, record, leaf, index);
  if (!value) throw schema::ScalarError("object template row " + std::string(leaf) + " is missing");
  return static_cast<std::uint32_t>(std::stoull(*value));
}

// Constraint: one NPC_ can carry several OBTS combinations, so the default (parentless) one wins.
std::size_t object_template_index(const Plugin& plugin, const Record& record) {
  for (std::size_t index = 0;; ++index) {
    try {
      const auto parent = obts_row_text(plugin, record, "ParentCombinationIndex", index);
      const auto is_default = obts_row_text(plugin, record, "Default", index);
      if (!parent || !is_default) return 0;
      if (*is_default == "1" && *parent == "-1") return index;
    } catch (const schema::ScalarError&) {
      return 0;
    }
  }
}

// Constraint: OBTS is not one of the TPLT categories, so the NPC's own template wins over any the templates carry.
void collect_object_template(Context& ctx,
                             const RecordRef& npc,
                             bool allowed,
                             std::vector<NpcVisualPart>& parts) {
  const RecordRef* source =
      obts_source_in_chain(ctx, npc, k_template_traits, k_tpta_traits_offset);
  if (source == nullptr) {
    source = obts_source_in_chain(ctx, npc, k_template_inventory, k_tpta_inventory_offset);
  }
  if (source == nullptr) return;
  if (!allowed) {
    ctx.stub("note: object template on " + source->record->form_key.to_string() +
             " not used: the NPC already has a FaceGen head, skin or outfit");
    return;
  }
  const auto& plugin = ctx.env.plugin(source->plugin);
  const auto& record = *source->record;
  std::vector<FormKey> includes;
  try {
    const auto context = plugin_form_id_context(plugin);
    // Requires: the include run rows fail the whole record when any OBTS payload disagrees with its counts, so the count call must precede the reads.
    static_cast<void>(record_field_count(plugin, record.form_key, "NPC_\\ObjectTemplateIncludeMod"));
    const auto combination = object_template_index(plugin, record);
    std::size_t first = 0;
    for (std::size_t before = 0; before < combination; ++before) {
      first += obts_row_word(plugin, record, "IncludeCount", before);
    }
    const auto count = obts_row_word(plugin, record, "IncludeCount", combination);
    for (std::size_t include = 0; include < count; ++include) {
      includes.push_back(resolve_plugin_form_id(
          RawFormId{obts_row_word(plugin, record, "IncludeMod", first + include)}, context));
    }
  } catch (const schema::ScalarError& error) {
    ctx.stub("object template on " + source->record->form_key.to_string() + " did not decode: " +
             error.what());
    return;
  } catch (const FormIdError& error) {
    ctx.stub("object template on " + source->record->form_key.to_string() +
             " has a bad form ID: " + error.what());
    return;
  }
  std::vector<FormKey> seen;
  for (const auto& include : includes) {
    if (include.owner().empty()) continue;
    collect_omod(ctx, include, 0, seen, parts);
  }
}

std::string facegen_entry(const FormKey& key) {
  char id[9];
  std::snprintf(id, sizeof(id), "%08X", static_cast<unsigned int>(key.local().value()));
  return "meshes/actors/character/facegendata/facegeom/" +
         ascii_lower(std::string(key.owner().filename())) + "/" + id + ".nif";
}

}  // namespace

std::string npc_visual_mesh_entry(std::string_view model_path) {
  std::string entry(model_path);
  std::replace(entry.begin(), entry.end(), '\\', '/');
  entry = ascii_lower(std::move(entry));
  entry.erase(0, entry.find_first_not_of('/'));
  if (!entry.starts_with("meshes/")) entry.insert(0, "meshes/");
  return entry;
}

std::string npc_visual_hkx_entry(std::string_view model_path) {
  auto entry = npc_visual_mesh_entry(model_path);
  const auto dot = entry.rfind('.');
  if (dot != std::string::npos && dot > entry.rfind('/')) entry.resize(dot);
  entry += ".hkx";
  return entry;
}

std::vector<FormKey> pick_leveled_entries(const env::Environment& environment,
                                          const FormKey& list,
                                          std::uint16_t level,
                                          std::uint64_t seed,
                                          std::vector<std::string>* stubs) {
  std::size_t budget = k_max_leveled_nodes;
  return pick_leveled_impls(environment, list, level, seed, 0, stubs, budget);
}

LeveledPick pick_leveled(const env::Environment& environment,
                         const FormKey& list,
                         std::uint16_t level,
                         std::uint64_t seed,
                         std::vector<std::string>* stubs,
                         std::uint8_t flags_override) {
  std::size_t budget = k_max_leveled_nodes;
  detail::LeveledTrace trace;
  LeveledPick result;
  result.entries = pick_leveled_impls(environment, list, level, seed, 0, stubs, budget, nullptr, &trace, flags_override);
  result.reason = trace.reason.value_or(LeveledPickReason::nothing_live);
  if (!result.entries.empty() && !detail::is_limit_reason(result.reason)) {
    result.reason = LeveledPickReason::picked;
  }
  result.depth_used = trace.deepest;
  return result;
}

LeveledCountedPick pick_leveled_counted(const env::Environment& environment,
                                        const FormKey& list,
                                        std::uint16_t level,
                                        std::uint64_t seed,
                                        std::uint32_t count,
                                        std::vector<std::string>* stubs,
                                        std::uint8_t flags_override) {
  detail::LeveledTrace trace;
  LeveledCountedPick result;
  std::size_t budget = k_max_leveled_nodes;
  result.entries = pick_leveled_impls(environment, list, level, seed, 0, stubs, budget, nullptr, &trace, flags_override);
  result.rolls = 1;
  const bool each = (flags_override & detail::k_leveled_calc_each) != 0 ||
                    (trace.top_flags && (*trace.top_flags & detail::k_leveled_calc_each) != 0);
  result.multiplier = each ? 1u : count;
  std::size_t spent = k_max_leveled_nodes - budget;
  for (std::uint32_t roll = 1; each && roll < count; ++roll) {
    if (spent >= detail::k_max_leveled_total_nodes) {
      detail::add_stub(stubs, "leveled list " + list.to_string() + " exceeded the " +
                                  std::to_string(detail::k_max_leveled_total_nodes) +
                                  "-node budget across rolls; stopped");
      detail::note_reason(&trace, LeveledPickReason::budget_exceeded);
      break;
    }
    if (result.entries.size() >= detail::k_max_leveled_total_entries) {
      detail::add_stub(stubs, "leveled list " + list.to_string() + " exceeded the " +
                                  std::to_string(detail::k_max_leveled_total_entries) +
                                  "-entry cap across rolls; stopped");
      detail::note_reason(&trace, LeveledPickReason::budget_exceeded);
      break;
    }
    budget = k_max_leveled_nodes;
    auto drawn = pick_leveled_impls(environment, list, level, detail::roll_seed(seed, roll), 0, stubs, budget, nullptr,
                                    &trace, flags_override);
    result.entries.insert(result.entries.end(), std::make_move_iterator(drawn.begin()),
                          std::make_move_iterator(drawn.end()));
    spent += k_max_leveled_nodes - budget;
    ++result.rolls;
  }
  result.counts = std::move(trace.counts);
  result.reason = trace.reason.value_or(LeveledPickReason::nothing_live);
  if (!result.entries.empty() && !detail::is_limit_reason(result.reason)) {
    result.reason = LeveledPickReason::picked;
  }
  result.depth_used = trace.deepest;
  return result;
}

std::optional<FormKey> pick_leveled_entry(const env::Environment& environment,
                                          const FormKey& list,
                                          std::uint16_t level,
                                          std::uint64_t seed,
                                          std::vector<std::string>* stubs) {
  const auto picked = pick_leveled_entries(environment, list, level, seed, stubs);
  if (picked.empty()) return std::nullopt;
  return picked.front();
}

NpcVisual decode_npc_visual(const Environment& environment,
                            const RecordRef& npc_ref,
                            NpcVisualOptions options) {
  if (npc_ref.record == nullptr || npc_ref.record->header.signature != sig_npc_) {
    throw NpcVisualError("record is not an NPC_");
  }
  const auto* npc = winner_of(environment, npc_ref.record->form_key);
  if (npc == nullptr) {
    throw NpcVisualError("NPC_ " + npc_ref.record->form_key.to_string() +
                         " has no live winning record");
  }

  NpcVisual out;
  out.npc = npc->record->form_key;
  Context ctx{environment, out};
  ctx.level = options.level.value_or(1);
  ctx.seed = options.seed;

  const auto* traits = resolve_template(ctx, *npc, k_template_traits, k_tpta_traits_offset);
  const auto* inventory = resolve_template(ctx, *npc, k_template_inventory, k_tpta_inventory_offset);
  const auto traits_label = traits->record->form_key.to_string();
  out.female = (read_u32(acbs_of(*traits->record), 0) & k_acbs_female) != 0;

  if (const auto* qnam = first_subrecord(*traits->record, sig_qnam)) {
    if (qnam->data.size() == k_qnam_size) {
      std::array<float, 4> tint{};
      std::memcpy(tint.data(), qnam->data.data(), k_qnam_size);
      out.skin_tint = tint;
    } else {
      ctx.stub("QNAM on " + traits_label + " is " + std::to_string(qnam->data.size()) +
               " bytes, expected 16; skin tint skipped");
    }
  }

  const auto race_key = link_of(environment, *traits, sig_rnam);
  if (!race_key) {
    ctx.stub("NPC_ " + traits_label + " has no race");
    return out;
  }
  const auto* race = winner_of(environment, *race_key);
  if (race == nullptr || race->record->header.signature != sig_race) {
    ctx.stub("race " + race_key->to_string() + " is not a live RACE record");
    return out;
  }
  out.race = *race_key;
  if (const auto* edid = first_subrecord(*race->record, sig_edid)) {
    auto text = zstring(*edid);
    if (!text.empty()) out.race_editor_id = std::move(text);
  }
  const auto race_label = out.race_editor_id ? *out.race_editor_id : race_key->to_string();
  const char* const sex = out.female ? "female" : "male";

  bool facegen = false;
  const auto* race_data = first_subrecord(*race->record, sig_data);
  if (race_data != nullptr && race_data->data.size() >= k_race_flags_offset + 4) {
    facegen = (read_u32(*race_data, k_race_flags_offset) & k_race_facegen_head) != 0;
  } else {
    ctx.stub("race " + race_label + " DATA is too short to hold the flags; no FaceGen part");
  }

  if (const auto model = race_skeleton_model(*race->record, out.female)) {
    out.skeleton_hkx_entry = npc_visual_hkx_entry(*model);
  } else {
    ctx.stub("race " + race_label + " has no " + sex + " skeleton model");
  }

  const auto races = armor_races(ctx, *race_key);
  std::uint32_t outfit_mask = 0;
  std::vector<NpcVisualPart> outfit_parts;
  try {
    collect_outfit(ctx, *inventory, races, out.female, outfit_mask, outfit_parts);
  } catch (const NpcVisualError& error) {
    ctx.stub(error.what());
  }

  std::vector<NpcVisualPart> skin_parts;
  try {
    auto skin_key = link_of(environment, *traits, sig_wnam);
    if (!skin_key) skin_key = link_of(environment, *race, sig_wnam);
    if (skin_key) collect_skin(ctx, *skin_key, races, out.female, outfit_mask, skin_parts);
  } catch (const NpcVisualError& error) {
    ctx.stub(error.what());
  }

  std::vector<NpcVisualPart> template_parts;
  const auto has_other_visual = facegen || !skin_parts.empty() || !outfit_parts.empty();
  collect_object_template(ctx, *npc, !has_other_visual, template_parts);

  std::set<std::string> seen;
  const auto add = [&](NpcVisualPart part) {
    if (facegen && part.source != NpcVisualSource::facegen && (part.biped_slots & k_head_slots) != 0) {
      return;
    }
    if (!seen.insert(part.mesh_entry).second) return;
    out.parts.push_back(std::move(part));
  };
  if (facegen) {
    add(NpcVisualPart{facegen_entry(traits->record->form_key), 0, NpcVisualSource::facegen});
  }
  for (auto& part : skin_parts) add(std::move(part));
  for (auto& part : outfit_parts) add(std::move(part));
  for (auto& part : template_parts) add(std::move(part));

  if (out.skeleton_hkx_entry && *out.skeleton_hkx_entry == k_human_skeleton) {
    out.idle_clip_entry = std::string(k_human_idle);
  } else if (out.skeleton_hkx_entry && *out.skeleton_hkx_entry == k_robot_skeleton) {
    out.idle_clip_entry = std::string(k_robot_idle);
  } else {
    ctx.stub("no idle mapped for race " + race_label);
  }
  return out;
}

}  // namespace fo4::forms
