#pragma once

#include "fo4/environment/environment.hpp"
#include "fo4/schema/form_link_mixed.hpp"
#include "fo4/environment/world.hpp"
#include "fo4/schema/vmad.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace fo4::env {

using Vec3 = std::array<float, 3>;

struct EnableParent {
  FormKey reference;
  std::optional<std::uint8_t> flags;
};

struct Ownership {
  FormKey owner;
  std::optional<bool> no_crime;
};

struct LinkedReference {
  FormKey keyword;
  FormKey reference;
};

struct ActivateParent {
  FormKey reference;
  float delay{0.0f};
};

struct Portal {
  FormKey origin;
  FormKey destination;
};

// PDTO type 0 carries a DIAL link, type 1 a four-character subtype.
struct TopicReference {
  std::uint32_t type{0};
  FormKey topic;
  std::uint32_t subtype{0};
};

struct Patrol {
  float idle_time{0.0f};
  std::optional<FormKey> idle;
  std::vector<TopicReference> topics;
  std::optional<FormKey> topic;
};

struct MapMarker {
  std::optional<std::uint8_t> flags;
  RecordName name{};
  std::optional<std::uint8_t> type;
};

struct Teleport {
  FormKey door;
  Vec3 position{};
  Vec3 rotation{};
  std::uint32_t flags{0};
  FormKey transition_interior;
};

// XPRM as stored: Mutagen doubles `bounds` on read, xEdit displays them doubled.
struct Primitive {
  Vec3 bounds{};
  std::array<float, 4> color{};
  std::uint32_t type{0};
};

struct Spline {
  float slack{0.0f};
  float thickness{0.0f};
  Vec3 half_extents{};
  std::optional<bool> wind_detached_end;
};

// XPTL/XOCP: width, height, position, then a rotation quaternion.
struct SizePositionRotation {
  float width{0.0f};
  float height{0.0f};
  Vec3 position{};
  std::array<float, 4> rotation{};
};

struct RoomBounds {
  std::uint8_t linked_room_count{0};
  std::uint8_t flags{0};
  std::optional<FormKey> lighting_template;
  std::optional<FormKey> image_space;
  std::vector<FormKey> linked_rooms;
};

struct ScriptProperty {
  std::string name;
  std::uint8_t type{0};
  std::uint8_t flags{0};
  std::optional<FormKey> object;
  std::optional<std::int16_t> alias;
  std::optional<std::string> text;
  std::optional<std::int32_t> integer;
  std::optional<float> real;
  std::optional<bool> boolean;
  std::optional<std::uint32_t> element_count;
  std::optional<std::vector<FormKey>> object_elements;
  std::optional<std::vector<std::vector<std::optional<FormKey>>>> struct_member_objects;
};

struct Script {
  std::string name;
  std::uint8_t flags{0};
  std::vector<ScriptProperty> properties;
};

struct Scripts {
  VmadScripts raw;
  std::vector<Script> scripts;
};

// XLIG offset 16 holds the near clip and offset 20 the volumetric intensity.
struct PlacedLightData {
  float fov{0.0f};
  float fade{0.0f};
  float end_distance_cap{0.0f};
  float shadow_depth_bias{0.0f};
  std::optional<float> near_clip;
  std::optional<float> volumetric_intensity;
};

struct PlacedReferenceData {
  Signature signature{};
  std::optional<FormKey> base;
  std::optional<Vec3> position;
  std::optional<Vec3> rotation;
  std::optional<float> scale;
  std::optional<FormKey> layer;
  std::optional<FormKey> encounter_zone;
  std::optional<FormKey> emittance;
  std::optional<float> radius;
  std::optional<PlacedLightData> light_data;
  std::optional<FormKey> location_reference;
  std::vector<FormKey> location_ref_types;
  std::optional<EnableParent> enable_parent;
  std::optional<Ownership> ownership;
  std::optional<std::int32_t> faction_rank;
  std::vector<LinkedReference> linked_references;
  std::optional<std::uint8_t> activate_parent_flags;
  std::vector<ActivateParent> activate_parents;
  std::optional<std::string_view> comments;
  bool ignored_by_sandbox{false};
  bool open_by_default{false};
  std::optional<MapMarker> map_marker;
  std::optional<Teleport> teleport;
  std::optional<Primitive> primitive;
  std::optional<Spline> spline;
  std::vector<Portal> portals;
  std::optional<SizePositionRotation> room_portal;
  std::optional<RoomBounds> room;
  std::vector<Patrol> patrols;
  std::optional<FormKey> attach_ref;
  std::optional<std::int32_t> count;
  std::optional<std::int32_t> level_modifier;
  std::optional<Scripts> scripts;
};

using fo4::is_placed_reference_signature;

// REFR, ACHR and the placed trap/projectile/hazard family; throws WorldDecodeError when malformed.
PlacedReferenceData decode_placed_reference(const Environment& env, const RecordRef& ref);

std::optional<std::string_view> primary_model_path(const RecordRef& ref) noexcept;

}  // namespace fo4::env
