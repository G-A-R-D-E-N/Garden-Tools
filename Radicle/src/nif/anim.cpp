#include "fo4/nif/anim.hpp"

#include "fo4/binary/cursor.hpp"
#include "fo4/constants.hpp"
#include "fo4/error.hpp"
#include "nif_internal.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fo4::nif {
namespace {

// Must stay FO4-only: 20.2.0.7 user 12 bs 130, the writer and ref scanner assume it.
void require_fo4(const NifFile& nif) {
  if (nif.version() != k_nif_version || nif.user_version() != k_nif_user_version || nif.bs_version() != k_nif_bs_version) {
    throw parse_error(0, "unsupported animation reference layout version");
  }
}

// Requires block of the expected type; the name is the block's own type for diagnostics.
void require_type(const NifFile& nif, std::uint32_t block, const char* type) {
  if (block >= nif.blocks().size()) throw parse_error(0, "animation block is out of range");
  if (nif.blocks()[block].type != type) {
    throw parse_error(0, std::string("block is ") + nif.blocks()[block].type + ", not " + type);
  }
}

// Must skip NiTimeController base: Next ref, Flags u16, Freq/Phase/Start/Stop f32, Target ptr.
void skip_time_controller(binary::Cursor& cursor) {
  cursor.read_i32();
  cursor.read_u16();
  cursor.read_f32();
  cursor.read_f32();
  cursor.read_f32();
  cursor.read_f32();
  cursor.read_i32();
}

AnimSequence decode_sequence(const NifFile& nif, std::uint32_t block) {
  require_type(nif, block, "NiControllerSequence");
  require_fo4(nif);
  auto cursor = detail::block_cursor(nif, block);
  AnimSequence seq;
  seq.block = block;
  seq.name = detail::read_string_ref(cursor, nif);
  const auto controlled = cursor.read_u32();
  cursor.read_u32();
  if (controlled > cursor.remaining() / 29) throw parse_error(0, "controlled block list exceeds its block");
  for (std::uint32_t i = 0; i < controlled; ++i) {
    AnimControlledBlock entry;
    entry.interpolator_ref = cursor.read_i32();
    entry.controller_ref = cursor.read_i32();
    entry.priority = cursor.read_u8();
    entry.node_name = detail::read_string_ref(cursor, nif);
    entry.property_type = detail::read_string_ref(cursor, nif);
    entry.controller_type = detail::read_string_ref(cursor, nif);
    entry.controller_id = detail::read_string_ref(cursor, nif);
    entry.interpolator_id = detail::read_string_ref(cursor, nif);
    seq.controlled.push_back(std::move(entry));
  }
  seq.weight = cursor.read_f32();
  seq.text_keys_ref = cursor.read_i32();
  seq.cycle = static_cast<AnimCycleType>(cursor.read_u32());
  seq.frequency = cursor.read_f32();
  seq.start_time = cursor.read_f32();
  seq.stop_time = cursor.read_f32();
  seq.manager_ref = cursor.read_i32();
  seq.accum_root_name = detail::read_string_ref(cursor, nif);
  const auto notes = cursor.read_u16();
  if (notes > cursor.remaining() / 4) throw parse_error(0, "anim note array list exceeds its block");
  for (std::uint16_t i = 0; i < notes; ++i) seq.anim_note_refs.push_back(cursor.read_i32());
  if (!cursor.at_end()) throw parse_error(0, "NiControllerSequence does not match its reference layout");
  return seq;
}

}  // namespace

std::vector<AnimSequence> read_anim_sequences(const NifFile& nif) {
  std::vector<AnimSequence> out;
  for (std::uint32_t i = 0; i < nif.blocks().size(); ++i) {
    if (nif.blocks()[i].type == "NiControllerSequence") out.push_back(decode_sequence(nif, i));
  }
  return out;
}

std::vector<AnimManager> read_anim_managers(const NifFile& nif) {
  require_fo4(nif);
  std::vector<AnimManager> out;
  for (std::uint32_t i = 0; i < nif.blocks().size(); ++i) {
    if (nif.blocks()[i].type != "NiControllerManager") continue;
    auto cursor = detail::block_cursor(nif, i);
    skip_time_controller(cursor);
    AnimManager manager;
    manager.block = i;
    manager.cumulative = cursor.read_u8() != 0;
    const auto count = cursor.read_u32();
    if (count > cursor.remaining() / 4) throw parse_error(0, "controller sequence list exceeds its block");
    for (std::uint32_t k = 0; k < count; ++k) {
      const auto ref = cursor.read_i32();
      if (ref < 0 || static_cast<std::size_t>(ref) >= nif.blocks().size()) {
        throw parse_error(0, "controller sequence reference is out of range");
      }
      manager.sequences.push_back(static_cast<std::uint32_t>(ref));
    }
    manager.object_palette_ref = cursor.read_i32();
    if (!cursor.at_end()) throw parse_error(0, "NiControllerManager does not match its reference layout");
    out.push_back(std::move(manager));
  }
  return out;
}

std::vector<AnimTextKey> read_text_keys(const NifFile& nif, std::uint32_t block) {
  require_type(nif, block, "NiTextKeyExtraData");
  require_fo4(nif);
  auto cursor = detail::block_cursor(nif, block);
  // Must skip NiExtraData name prefix: string ref only, no extra list on this path.
  detail::read_string_ref(cursor, nif);
  const auto count = cursor.read_u32();
  std::vector<AnimTextKey> out;
  out.reserve(count);
  for (std::uint32_t i = 0; i < count; ++i) {
    AnimTextKey key;
    key.time = cursor.read_f32();
    key.text = detail::read_string_ref(cursor, nif);
    out.push_back(std::move(key));
  }
  if (!cursor.at_end()) throw parse_error(0, "NiTextKeyExtraData does not match its reference layout");
  return out;
}

AnimBehaviourGraph read_behaviour_graph(const NifFile& nif, std::uint32_t block) {
  require_type(nif, block, "BSBehaviorGraphExtraData");
  require_fo4(nif);
  auto cursor = detail::block_cursor(nif, block);
  detail::read_string_ref(cursor, nif);
  AnimBehaviourGraph graph;
  graph.block = block;
  graph.graph_file = detail::read_string_ref(cursor, nif);
  graph.controls_base_skeleton = cursor.read_u8() != 0;
  if (!cursor.at_end()) throw parse_error(0, "BSBehaviorGraphExtraData does not match its reference layout");
  return graph;
}

}  // namespace fo4::nif
