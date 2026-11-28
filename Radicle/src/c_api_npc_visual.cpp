#include "c_api_forms_internal.hpp"
#include "fo4/c_api_npc_visual.h"
#include "fo4/forms/npc_visual.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace {

using namespace radicle_c_detail;
using namespace radicle_c_env_detail;
using namespace radicle_c_forms_detail;

struct NpcVisualHolder {
  radicle_npc_visual view{};
  TextStore text;
  std::vector<radicle_npc_visual_part> parts;
  std::vector<const char*> stubs;
};

struct ArmorAddonsHolder {
  radicle_armor_addons view{};
  TextStore text;
  std::vector<radicle_npc_visual_part> parts;
  std::vector<const char*> stubs;
};

}  // namespace

extern "C" {

radicle_status radicle_npc_visual_decode_ex(const radicle_env* env,
                                           const radicle_record* npc,
                                           const radicle_npc_visual_options* options,
                                           radicle_npc_visual** out_visual) {
  clear_error();
  if (out_visual != nullptr) *out_visual = nullptr;
  if (const auto status = require_env_record(env, npc, out_visual); status != RADICLE_STATUS_OK) return status;
  const auto& ref = *to_ref(npc);
  if (ref.record->header.signature != fo4::Signature::from_string("NPC_")) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record is not an NPC_");
  }
  return guarded("unknown exception while decoding the NPC_ visual plan", [&] {
    try {
      fo4::forms::NpcVisualOptions settings;
      if (options != nullptr) {
        // Invariant: the caller must zero the reserved bytes, so stale memory cannot change the plan.
        if (options->has_level > 1) {
          return fail(RADICLE_STATUS_INVALID_ARGUMENT, "has_level must be 0 or 1");
        }
        if (options->reserved[0] != 0 || options->reserved[1] != 0 || options->reserved[2] != 0) {
          return fail(RADICLE_STATUS_INVALID_ARGUMENT, "reserved bytes must be zero");
        }
        if (options->has_level != 0) {
          if (options->level == 0 || options->level > 0xFFFFu) {
            return fail(RADICLE_STATUS_INVALID_ARGUMENT, "level must be between 1 and 65535");
          }
          settings.level = static_cast<std::uint16_t>(options->level);
        }
        settings.seed = options->seed;
      }
      const auto data = fo4::forms::decode_npc_visual(env->value, ref, settings);
      auto holder = std::make_unique<NpcVisualHolder>();
      auto& view = holder->view;
      view.npc = to_c_key(*env, data.npc);
      view.race = to_c_key(*env, data.race);
      view.has_race = static_cast<uint8_t>(data.race ? 1 : 0);
      view.race_editor_id = holder->text.keep(data.race_editor_id);
      view.female = static_cast<uint8_t>(data.female ? 1 : 0);
      view.seed_dependent = static_cast<uint8_t>(data.seed_dependent ? 1 : 0);
      if (data.skin_tint) {
        view.has_skin_tint = 1;
        std::copy(data.skin_tint->begin(), data.skin_tint->end(), view.skin_tint);
      }
      view.skeleton_hkx_entry = holder->text.keep(data.skeleton_hkx_entry);
      view.idle_clip_entry = holder->text.keep(data.idle_clip_entry);
      for (const auto& part : data.parts) {
        radicle_npc_visual_part item{};
        item.mesh_entry = holder->text.keep(std::string_view(part.mesh_entry));
        item.biped_slots = part.biped_slots;
        item.source = static_cast<uint8_t>(part.source);
        holder->parts.push_back(item);
      }
      for (const auto& stub : data.stubs) holder->stubs.push_back(holder->text.keep(std::string_view(stub)));
      view.parts = holder->parts.empty() ? nullptr : holder->parts.data();
      view.part_count = holder->parts.size();
      view.stubs = holder->stubs.empty() ? nullptr : holder->stubs.data();
      view.stub_count = holder->stubs.size();
      view.internal = holder.get();
      *out_visual = &holder.release()->view;
      return RADICLE_STATUS_OK;
    } catch (const fo4::forms::NpcVisualError& error) {
      return fail_exception(RADICLE_STATUS_PARSE_ERROR, error);
    }
  });
}

void radicle_npc_visual_free(radicle_npc_visual* visual) {
  clear_error();
  if (visual == nullptr) return;
  delete static_cast<NpcVisualHolder*>(visual->internal);
}

radicle_status radicle_armor_addons_decode(const radicle_env* env,
                                           const radicle_record* armor,
                                           radicle_form_key race,
                                           uint8_t female,
                                           uint8_t first_person,
                                           radicle_armor_addons** out_addons) {
  clear_error();
  if (out_addons != nullptr) *out_addons = nullptr;
  if (const auto status = require_env_record(env, armor, out_addons); status != RADICLE_STATUS_OK) return status;
  const auto& ref = *to_ref(armor);
  if (ref.record->header.signature != fo4::Signature::from_string("ARMO")) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record is not an ARMO");
  }
  const auto race_key = to_form_key(race);
  if (!race_key) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "a valid race key is required");
  if (female > 1 || first_person > 1) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "female and first_person must be 0 or 1");
  }
  return guarded("unknown exception while decoding the armor addons", [&] {
    try {
      const auto data =
          fo4::forms::decode_armor_addons(env->value, ref, *race_key, female != 0, first_person != 0);
      auto holder = std::make_unique<ArmorAddonsHolder>();
      for (const auto& part : data.parts) {
        radicle_npc_visual_part item{};
        item.mesh_entry = holder->text.keep(std::string_view(part.mesh_entry));
        item.biped_slots = part.biped_slots;
        item.source = static_cast<uint8_t>(part.source);
        holder->parts.push_back(item);
      }
      for (const auto& stub : data.stubs) holder->stubs.push_back(holder->text.keep(std::string_view(stub)));
      auto& view = holder->view;
      view.parts = holder->parts.empty() ? nullptr : holder->parts.data();
      view.part_count = holder->parts.size();
      view.matched_slots = data.matched_slots;
      view.stubs = holder->stubs.empty() ? nullptr : holder->stubs.data();
      view.stub_count = holder->stubs.size();
      view.internal = holder.get();
      *out_addons = &holder.release()->view;
      return RADICLE_STATUS_OK;
    } catch (const fo4::forms::NpcVisualError& error) {
      return fail_exception(RADICLE_STATUS_PARSE_ERROR, error);
    }
  });
}

void radicle_armor_addons_free(radicle_armor_addons* addons) {
  clear_error();
  if (addons == nullptr) return;
  delete static_cast<ArmorAddonsHolder*>(addons->internal);
}

}  // extern "C"
