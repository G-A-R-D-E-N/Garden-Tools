#include "fo4/forms/proj_expl_hazd.hpp"

#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/schema/preflight.hpp"
#include "../plugin/record_edit.hpp"

#include <algorithm>
#include <cstring>
#include <initializer_list>
#include <utility>

namespace fo4::forms {
namespace {

constexpr auto k_proj = Signature::from_string("PROJ");
constexpr auto k_expl = Signature::from_string("EXPL");
constexpr auto k_hazd = Signature::from_string("HAZD");
constexpr auto k_edid = Signature::from_string("EDID");
constexpr auto k_obnd = Signature::from_string("OBND");
constexpr auto k_full = Signature::from_string("FULL");
constexpr auto k_modl = Signature::from_string("MODL");
constexpr auto k_modt = Signature::from_string("MODT");
constexpr auto k_data = Signature::from_string("DATA");
constexpr auto k_dnam = Signature::from_string("DNAM");
constexpr auto k_nam1 = Signature::from_string("NAM1");
constexpr auto k_vnam = Signature::from_string("VNAM");

// ---------------------------------------------------------------------------
// Byte plumbing
// ---------------------------------------------------------------------------

class PayloadWriter {
public:
  explicit PayloadWriter(std::size_t reserve) { bytes_.reserve(reserve); }

  void u32(std::uint32_t value) {
    std::array<std::byte, 4> raw{};
    std::memcpy(raw.data(), &value, 4);
    bytes_.insert(bytes_.end(), raw.begin(), raw.end());
  }
  void f32(float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, 4);
    u32(bits);
  }
  void u8(std::uint8_t value) { bytes_.push_back(std::byte{value}); }
  void u16(std::uint16_t value) {
    std::array<std::byte, 2> raw{};
    std::memcpy(raw.data(), &value, 2);
    bytes_.insert(bytes_.end(), raw.begin(), raw.end());
  }
  void i16(std::int16_t value) {
    std::array<std::byte, 2> raw{};
    std::memcpy(raw.data(), &value, 2);
    bytes_.insert(bytes_.end(), raw.begin(), raw.end());
  }

  std::vector<std::byte> take() { return std::move(bytes_); }

private:
  std::vector<std::byte> bytes_;
};

std::uint32_t read_u32(std::span<const std::byte> data, std::size_t offset) {
  std::uint32_t value = 0;
  std::memcpy(&value, data.data() + offset, 4);
  return value;
}

std::uint16_t read_u16(std::span<const std::byte> data, std::size_t offset) {
  std::uint16_t value = 0;
  std::memcpy(&value, data.data() + offset, 2);
  return value;
}

float read_f32(std::span<const std::byte> data, std::size_t offset) {
  const auto bits = read_u32(data, offset);
  float value = 0.0F;
  std::memcpy(&value, &bits, 4);
  return value;
}

std::vector<std::byte> zstring(std::string_view value) {
  std::vector<std::byte> bytes;
  bytes.reserve(value.size() + 1);
  for (const char ch : value) bytes.push_back(static_cast<std::byte>(ch));
  bytes.push_back(std::byte{0});
  return bytes;
}

std::string read_zstring(std::span<const std::byte> data) {
  std::string value;
  value.reserve(data.size());
  for (const auto byte : data) {
    const auto ch = std::to_integer<unsigned char>(byte);
    if (ch == 0) break;
    value.push_back(static_cast<char>(ch));
  }
  return value;
}

std::vector<std::byte> encode_bounds(const ObjectBounds& bounds) {
  PayloadWriter out{12};
  for (const auto value : bounds) out.i16(value);
  return out.take();
}

ObjectBounds decode_bounds(std::span<const std::byte> data) {
  ObjectBounds bounds{};
  if (data.size() < 12) return bounds;
  for (std::size_t i = 0; i < bounds.size(); ++i) {
    std::int16_t value = 0;
    std::memcpy(&value, data.data() + i * 2, 2);
    bounds[i] = value;
  }
  return bounds;
}

// ---------------------------------------------------------------------------
// Record plumbing
// ---------------------------------------------------------------------------

using fo4::detail::find_mutable_record;

const Subrecord* find_subrecord(const Record& record, Signature signature) {
  for (const auto& sub : record.subrecords) {
    if (sub.signature == signature) return &sub;
  }
  return nullptr;
}

const Record& require_record(const Plugin& plugin, const FormKey& key, Signature expected) {
  const auto* record = find_record(plugin, key);
  if (record == nullptr) {
    throw ProjExplHazdError("no such record: " + key.to_string());
  }
  if (record->header.signature != expected) {
    throw ProjExplHazdError(key.to_string() + " is a " +
                            record->header.signature.to_string() + ", not a " +
                            expected.to_string());
  }
  return *record;
}

std::span<const std::byte> require_payload(const Record& record, Signature signature,
                                           std::initializer_list<std::size_t> accepted) {
  const auto* sub = find_subrecord(record, signature);
  if (sub == nullptr) {
    throw ProjExplHazdError(record.header.signature.to_string() + " " + signature.to_string() +
                            " is missing");
  }
  if (std::find(accepted.begin(), accepted.end(), sub->data.size()) == accepted.end()) {
    throw ProjExplHazdError(record.header.signature.to_string() + " " + signature.to_string() +
                            " is " + std::to_string(sub->data.size()) +
                            " bytes, which is not a shape this decoder knows");
  }
  return sub->data;
}

void require_clean_editor_id(std::string_view editor_id) {
  if (editor_id.empty()) {
    throw ProjExplHazdError("editor ID is empty; PROJ, EXPL and HAZD require one");
  }
  if (editor_id.find('\0') != std::string_view::npos) {
    throw ProjExplHazdError("editor ID may not contain a null byte");
  }
}

// A slot may name any of several record types -- EXPL's placed object takes any
// placeable base, HAZD's spell takes SPEL or ENCH. An empty list means the slot
// accepts anything that exists.
struct TypedSlot {
  const FormKey* key;
  std::vector<const char*> accepted;
};

// Local keys must resolve to a record of the type the slot holds. Foreign keys
// name a master this plugin cannot see into, so they pass through -- the master
// list check happens when the FormID is encoded.
void validate_slots(const Plugin& plugin, std::initializer_list<TypedSlot> slots) {
  for (const auto& slot : slots) {
    if (slot.key->owner().empty()) continue;
    if (slot.key->owner() != plugin.key) continue;

    const auto* target = find_record(plugin, *slot.key);
    if (target == nullptr) {
      throw ProjExplHazdError("local FormKey does not resolve to a record in this plugin: " +
                              slot.key->to_string());
    }
    if (slot.accepted.empty()) continue;

    const auto signature = target->header.signature.to_string();
    const bool ok = std::any_of(slot.accepted.begin(), slot.accepted.end(),
                                [&](const char* name) { return signature == name; });
    if (!ok) {
      std::string expected;
      for (const char* name : slot.accepted) {
        if (!expected.empty()) expected += " or ";
        expected += name;
      }
      throw ProjExplHazdError(slot.key->to_string() + " is a " + signature + ", but this slot " +
                              "holds a " + expected);
    }
  }
}

RawFormId encode_slot(const FormKey& key, const PluginFormIdContext& context) {
  if (key.owner().empty()) return RawFormId{0};
  return encode_plugin_form_id(key, context);
}

FormKey decode_slot(std::span<const std::byte> data, std::size_t offset,
                    const PluginFormIdContext& context) {
  const auto raw = RawFormId{read_u32(data, offset)};
  if (raw.is_null()) return FormKey{};
  return resolve_plugin_form_id(raw, context);
}

void run_authoring_preflight(const Plugin& plugin, const FormKey& key, std::string_view family) {
  for (const auto& issue : schema::preflight_authoring_record(plugin, key)) {
    if (issue.severity == schema::PreflightSeverity::error) {
      throw ProjExplHazdError(std::string{family} + " authoring preflight failed: " +
                              issue.message);
    }
  }
}

}  // namespace

std::vector<std::byte> empty_model_texture_data() {
  // Version 4, zero texture entries: the shape ten shipped HAZD records use.
  PayloadWriter out{20};
  out.u32(4);
  out.u32(0);
  out.u32(0);
  out.u32(0);
  out.u32(0);
  return out.take();
}

// ---------------------------------------------------------------------------
// Payload encoders -- field order is the measured file order, not the order
// CommonLibF4 holds these structs in memory. See docs/FORMAT_NOTES.md.
// ---------------------------------------------------------------------------

namespace {

std::vector<std::byte> encode_projectile_dnam_with(const ProjectileCreation& c,
                                                   const PluginFormIdContext& context) {
  PayloadWriter out{k_proj_dnam_size};
  out.u16(c.flags);                                  // +0
  out.u16(c.type);                                   // +2
  out.f32(c.gravity);                                // +4
  out.f32(c.speed);                                  // +8
  out.f32(c.range);                                  // +12
  out.u32(encode_slot(c.light, context).value());    // +16
  out.u32(encode_slot(c.muzzle_flash_light, context).value());  // +20
  out.f32(c.explosion_proximity);                    // +24
  out.f32(c.explosion_timer);                        // +28
  out.u32(encode_slot(c.explosion_type, context).value());      // +32
  out.u32(encode_slot(c.active_sound_loop, context).value());   // +36
  out.f32(c.muzzle_flash_duration);                  // +40
  out.f32(c.fade_out_time);                          // +44
  out.f32(c.force);                                  // +48
  out.u32(encode_slot(c.countdown_sound, context).value());     // +52
  out.u32(encode_slot(c.deactivate_sound, context).value());    // +56
  out.u32(encode_slot(c.default_weapon_source, context).value());  // +60
  out.f32(c.cone_spread);                            // +64
  out.f32(c.collision_radius);                       // +68
  out.f32(c.lifetime);                               // +72
  out.f32(c.relaunch_interval);                      // +76
  out.u32(encode_slot(c.decal_data, context).value());          // +80
  out.u32(encode_slot(c.collision_layer, context).value());     // +84
  // The tail is the reverse of the in-memory struct: the byte first, then an
  // unaligned FormID as the final four bytes.
  out.u8(c.tracer_frequency);                        // +88
  out.u32(encode_slot(c.vats_projectile, context).value());     // +89, unaligned
  auto bytes = out.take();
  if (bytes.size() != k_proj_dnam_size) {
    throw ProjExplHazdError("encoded PROJ DNAM is not 93 bytes");
  }
  return bytes;
}

std::vector<std::byte> encode_explosion_data_with(const ExplosionCreation& c,
                                                  const PluginFormIdContext& context) {
  PayloadWriter out{k_expl_data_size};
  out.u32(encode_slot(c.light, context).value());                 // +0
  out.u32(encode_slot(c.sound1, context).value());                // +4
  out.u32(encode_slot(c.sound2, context).value());                // +8
  out.u32(encode_slot(c.impact_data_set, context).value());       // +12
  out.u32(encode_slot(c.impact_placed_object, context).value());  // +16
  out.u32(encode_slot(c.spawn_projectile, context).value());      // +20
  out.f32(c.force);                                  // +24
  out.f32(c.damage);                                 // +28
  out.f32(c.inner_radius);                           // +32
  out.f32(c.outer_radius);                           // +36
  out.f32(c.image_space_radius);                     // +40
  out.f32(c.vertical_offset_mult);                   // +44
  out.u32(c.flags);                                  // +48
  out.u32(c.sound_level);                            // +52
  out.f32(c.placed_object_fade_delay);               // +56
  out.u32(c.stagger_magnitude);                      // +60
  out.f32(c.spawn_projectile_vector[0]);             // +64
  out.f32(c.spawn_projectile_vector[1]);             // +68
  out.f32(c.spawn_projectile_vector[2]);             // +72
  out.f32(c.projectile_spread);                      // +76
  out.u32(c.projectile_count);                       // +80
  auto bytes = out.take();
  if (bytes.size() != k_expl_data_size) {
    throw ProjExplHazdError("encoded EXPL DATA is not 84 bytes");
  }
  return bytes;
}

std::vector<std::byte> encode_hazard_dnam_with(const HazardCreation& c,
                                               const PluginFormIdContext& context) {
  PayloadWriter out{k_hazd_dnam_size};
  out.u32(c.limit);                                  // +0
  out.f32(c.radius);                                 // +4
  out.f32(c.lifetime);                               // +8
  out.f32(c.image_space_radius);                     // +12
  out.f32(c.target_interval);                        // +16
  out.u32(c.flags);                                  // +20
  out.u32(encode_slot(c.spell, context).value());              // +24
  out.u32(encode_slot(c.light, context).value());              // +28
  out.u32(encode_slot(c.impact_data_set, context).value());    // +32
  out.u32(encode_slot(c.sound, context).value());              // +36
  out.f32(c.full_effect_radius);                     // +40
  out.f32(c.taper_weight);                           // +44
  out.f32(c.taper_curve);                            // +48
  auto bytes = out.take();
  if (bytes.size() != k_hazd_dnam_size) {
    throw ProjExplHazdError("encoded HAZD DNAM is not 52 bytes");
  }
  return bytes;
}

}  // namespace

std::vector<std::byte> encode_projectile_dnam(const Plugin& plugin,
                                              const ProjectileCreation& creation) {
  return encode_projectile_dnam_with(creation, plugin_form_id_context(plugin));
}
std::vector<std::byte> encode_explosion_data(const Plugin& plugin,
                                             const ExplosionCreation& creation) {
  return encode_explosion_data_with(creation, plugin_form_id_context(plugin));
}
std::vector<std::byte> encode_hazard_dnam(const Plugin& plugin,
                                          const HazardCreation& creation) {
  return encode_hazard_dnam_with(creation, plugin_form_id_context(plugin));
}

// ---------------------------------------------------------------------------
// Creation
// ---------------------------------------------------------------------------

FormKey create_projectile(Plugin& plugin, const ProjectileCreation& creation) {
  require_clean_editor_id(creation.editor_id);
  if (creation.muzzle_flash_model.find('\0') != std::string::npos) {
    throw ProjExplHazdError("PROJ muzzle flash model path may not contain a null byte");
  }

  auto next = plugin;
  assign_record_form_keys(next);
  validate_slots(next, {{&creation.light, {"LIGH"}},
                        {&creation.muzzle_flash_light, {"LIGH"}},
                        {&creation.explosion_type, {"EXPL"}},
                        {&creation.active_sound_loop, {"SNDR"}},
                        {&creation.countdown_sound, {"SNDR"}},
                        {&creation.deactivate_sound, {"SNDR"}},
                        {&creation.default_weapon_source, {"WEAP"}},
                        {&creation.decal_data, {"TXST"}},
                        {&creation.collision_layer, {"COLL"}},
                        {&creation.vats_projectile, {"PROJ"}}});

  const auto context = plugin_form_id_context(next);
  auto dnam = encode_projectile_dnam_with(creation, context);

  const auto key = create_local_record(next, k_proj);
  auto* record = find_mutable_record(next, key);
  if (record == nullptr) {
    throw ProjExplHazdError("newly allocated PROJ record could not be resolved");
  }

  // Measured shipped order: EDID OBND [FULL] ... DATA DNAM NAM1 VNAM.
  if (!creation.editor_id.empty()) {
    record->subrecords.push_back(Subrecord{k_edid, zstring(stored_editor_id(creation.editor_id)), false});
  }
  record->subrecords.push_back(Subrecord{k_obnd, encode_bounds(creation.bounds), false});
  if (creation.name.has_value() && !creation.name->empty()) {
    record->subrecords.push_back(Subrecord{k_full, zstring(stored_inline_text(*creation.name, "full name")), false});
  }
  // DATA is a zero-length marker in all 239 shipped records.
  record->subrecords.push_back(Subrecord{k_data, {}, false});
  record->subrecords.push_back(Subrecord{k_dnam, std::move(dnam), false});
  record->subrecords.push_back(
      Subrecord{k_nam1, zstring(stored_inline_text(creation.muzzle_flash_model, "muzzle flash model path")), false});
  PayloadWriter vnam{4};
  vnam.u32(creation.sound_level);
  record->subrecords.push_back(Subrecord{k_vnam, vnam.take(), false});

  run_authoring_preflight(next, key, "PROJ");
  plugin = std::move(next);
  return key;
}

FormKey create_explosion(Plugin& plugin, const ExplosionCreation& creation) {
  require_clean_editor_id(creation.editor_id);

  auto next = plugin;
  assign_record_form_keys(next);
  validate_slots(next, {{&creation.light, {"LIGH"}},
                        {&creation.sound1, {"SNDR"}},
                        {&creation.sound2, {"SNDR"}},
                        {&creation.impact_data_set, {"IPDS"}},
                        // Placed object takes any placeable base -- NPC_, LVLI,
                        // LIGH, MSTT, ACTI and more all appear in shipped data.
                        {&creation.impact_placed_object, {}},
                        {&creation.spawn_projectile, {"PROJ"}}});

  const auto context = plugin_form_id_context(next);
  auto data = encode_explosion_data_with(creation, context);

  const auto key = create_local_record(next, k_expl);
  auto* record = find_mutable_record(next, key);
  if (record == nullptr) {
    throw ProjExplHazdError("newly allocated EXPL record could not be resolved");
  }

  if (!creation.editor_id.empty()) {
    record->subrecords.push_back(Subrecord{k_edid, zstring(stored_editor_id(creation.editor_id)), false});
  }
  record->subrecords.push_back(Subrecord{k_obnd, encode_bounds(creation.bounds), false});
  if (creation.name.has_value() && !creation.name->empty()) {
    record->subrecords.push_back(Subrecord{k_full, zstring(stored_inline_text(*creation.name, "full name")), false});
  }
  record->subrecords.push_back(Subrecord{k_data, std::move(data), false});

  run_authoring_preflight(next, key, "EXPL");
  plugin = std::move(next);
  return key;
}

FormKey create_hazard(Plugin& plugin, const HazardCreation& creation) {
  require_clean_editor_id(creation.editor_id);
  // All 78 shipped HAZD records carry a model, and there is no defensible
  // default path, so this fails rather than inventing one.
  if (creation.model.empty()) {
    throw ProjExplHazdError("HAZD requires a model path");
  }
  if (creation.model.find('\0') != std::string::npos) {
    throw ProjExplHazdError("HAZD model path may not contain a null byte");
  }

  auto next = plugin;
  assign_record_form_keys(next);
  validate_slots(next, {{&creation.spell, {"SPEL", "ENCH"}},
                        {&creation.light, {"LIGH"}},
                        {&creation.impact_data_set, {"IPDS"}},
                        {&creation.sound, {"SNDR"}}});

  const auto context = plugin_form_id_context(next);
  auto dnam = encode_hazard_dnam_with(creation, context);

  const auto key = create_local_record(next, k_hazd);
  auto* record = find_mutable_record(next, key);
  if (record == nullptr) {
    throw ProjExplHazdError("newly allocated HAZD record could not be resolved");
  }

  if (!creation.editor_id.empty()) {
    record->subrecords.push_back(Subrecord{k_edid, zstring(stored_editor_id(creation.editor_id)), false});
  }
  record->subrecords.push_back(Subrecord{k_obnd, encode_bounds(creation.bounds), false});
  if (creation.name.has_value() && !creation.name->empty()) {
    record->subrecords.push_back(Subrecord{k_full, zstring(stored_inline_text(*creation.name, "full name")), false});
  }
  record->subrecords.push_back(Subrecord{k_modl, zstring(stored_inline_text(creation.model, "model path")), false});
  record->subrecords.push_back(
      Subrecord{k_modt,
                creation.model_texture_data.value_or(empty_model_texture_data()),
                false});
  record->subrecords.push_back(Subrecord{k_dnam, std::move(dnam), false});

  run_authoring_preflight(next, key, "HAZD");
  plugin = std::move(next);
  return key;
}

// ---------------------------------------------------------------------------
// Decoding
// ---------------------------------------------------------------------------

ProjectileCreation decode_projectile(const Plugin& plugin, const FormKey& key) {
  const auto& record = require_record(plugin, key, k_proj);
  const auto dnam = require_payload(record, k_dnam, {k_proj_dnam_size});
  const auto context = plugin_form_id_context(plugin);

  ProjectileCreation out;
  if (const auto* edid = find_subrecord(record, k_edid)) out.editor_id = read_zstring(edid->data);
  if (const auto* obnd = find_subrecord(record, k_obnd)) out.bounds = decode_bounds(obnd->data);
  if (const auto* full = find_subrecord(record, k_full)) out.name = read_zstring(full->data);
  if (const auto* nam1 = find_subrecord(record, k_nam1)) {
    out.muzzle_flash_model = read_zstring(nam1->data);
  }
  if (const auto* vnam = find_subrecord(record, k_vnam); vnam != nullptr && vnam->data.size() >= 4) {
    out.sound_level = read_u32(vnam->data, 0);
  }

  out.flags = read_u16(dnam, 0);
  out.type = read_u16(dnam, 2);
  out.gravity = read_f32(dnam, 4);
  out.speed = read_f32(dnam, 8);
  out.range = read_f32(dnam, 12);
  out.light = decode_slot(dnam, 16, context);
  out.muzzle_flash_light = decode_slot(dnam, 20, context);
  out.explosion_proximity = read_f32(dnam, 24);
  out.explosion_timer = read_f32(dnam, 28);
  out.explosion_type = decode_slot(dnam, 32, context);
  out.active_sound_loop = decode_slot(dnam, 36, context);
  out.muzzle_flash_duration = read_f32(dnam, 40);
  out.fade_out_time = read_f32(dnam, 44);
  out.force = read_f32(dnam, 48);
  out.countdown_sound = decode_slot(dnam, 52, context);
  out.deactivate_sound = decode_slot(dnam, 56, context);
  out.default_weapon_source = decode_slot(dnam, 60, context);
  out.cone_spread = read_f32(dnam, 64);
  out.collision_radius = read_f32(dnam, 68);
  out.lifetime = read_f32(dnam, 72);
  out.relaunch_interval = read_f32(dnam, 76);
  out.decal_data = decode_slot(dnam, 80, context);
  out.collision_layer = decode_slot(dnam, 84, context);
  out.tracer_frequency = std::to_integer<std::uint8_t>(dnam[88]);
  out.vats_projectile = decode_slot(dnam, 89, context);
  return out;
}

ExplosionCreation decode_explosion(const Plugin& plugin, const FormKey& key) {
  const auto& record = require_record(plugin, key, k_expl);
  const auto data = require_payload(record, k_data, {52, 56, 60, 64, k_expl_data_size});
  const auto context = plugin_form_id_context(plugin);
  const auto size = data.size();

  ExplosionCreation out;
  if (const auto* edid = find_subrecord(record, k_edid)) out.editor_id = read_zstring(edid->data);
  if (const auto* obnd = find_subrecord(record, k_obnd)) out.bounds = decode_bounds(obnd->data);
  if (const auto* full = find_subrecord(record, k_full)) out.name = read_zstring(full->data);

  out.light = decode_slot(data, 0, context);
  out.sound1 = decode_slot(data, 4, context);
  out.sound2 = decode_slot(data, 8, context);
  out.impact_data_set = decode_slot(data, 12, context);
  out.impact_placed_object = decode_slot(data, 16, context);
  out.spawn_projectile = decode_slot(data, 20, context);
  out.force = read_f32(data, 24);
  out.damage = read_f32(data, 28);
  out.inner_radius = read_f32(data, 32);
  out.outer_radius = read_f32(data, 36);
  out.image_space_radius = read_f32(data, 40);
  out.vertical_offset_mult = read_f32(data, 44);
  out.flags = read_u32(data, 48);
  // Truncated legacy shapes are a clean prefix: every field past the end keeps
  // its default rather than being read from beyond the payload.
  if (size >= 56) out.sound_level = read_u32(data, 52);
  if (size >= 60) out.placed_object_fade_delay = read_f32(data, 56);
  if (size >= 64) out.stagger_magnitude = read_u32(data, 60);
  if (size >= k_expl_data_size) {
    out.spawn_projectile_vector = {read_f32(data, 64), read_f32(data, 68), read_f32(data, 72)};
    out.projectile_spread = read_f32(data, 76);
    out.projectile_count = read_u32(data, 80);
  }
  return out;
}

HazardCreation decode_hazard(const Plugin& plugin, const FormKey& key) {
  const auto& record = require_record(plugin, key, k_hazd);
  const auto dnam = require_payload(record, k_dnam, {k_hazd_dnam_size});
  const auto context = plugin_form_id_context(plugin);

  HazardCreation out;
  if (const auto* edid = find_subrecord(record, k_edid)) out.editor_id = read_zstring(edid->data);
  if (const auto* obnd = find_subrecord(record, k_obnd)) out.bounds = decode_bounds(obnd->data);
  if (const auto* full = find_subrecord(record, k_full)) out.name = read_zstring(full->data);
  if (const auto* modl = find_subrecord(record, k_modl)) out.model = read_zstring(modl->data);
  if (const auto* modt = find_subrecord(record, k_modt)) {
    out.model_texture_data = std::vector<std::byte>{modt->data.begin(), modt->data.end()};
  }

  out.limit = read_u32(dnam, 0);
  out.radius = read_f32(dnam, 4);
  out.lifetime = read_f32(dnam, 8);
  out.image_space_radius = read_f32(dnam, 12);
  out.target_interval = read_f32(dnam, 16);
  out.flags = read_u32(dnam, 20);
  out.spell = decode_slot(dnam, 24, context);
  out.light = decode_slot(dnam, 28, context);
  out.impact_data_set = decode_slot(dnam, 32, context);
  out.sound = decode_slot(dnam, 36, context);
  out.full_effect_radius = read_f32(dnam, 40);
  out.taper_weight = read_f32(dnam, 44);
  out.taper_curve = read_f32(dnam, 48);
  return out;
}

}  // namespace fo4::forms
