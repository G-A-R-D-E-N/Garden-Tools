#include "fo4/environment/placed.hpp"

#include "fo4/schema/form_link_mixed.hpp"
#include "record_reader.hpp"

#include "fo4/error.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>

namespace fo4::env {
namespace {

using detail::Reader;

constexpr Signature sig(const char (&text)[5]) {
  return Signature{text[0], text[1], text[2], text[3]};
}

enum class Section { none, map_marker, room, patrol };

std::optional<std::string_view> cstring(const Subrecord& subrecord) noexcept {
  const auto& data = subrecord.data;
  const auto nul = std::find(data.begin(), data.end(), std::byte{0});
  if (nul == data.end()) return std::nullopt;
  const auto* begin = reinterpret_cast<const char*>(data.data());
  return std::string_view{begin, static_cast<std::size_t>(nul - data.begin())};
}

class PlacedDecoder {
public:
  PlacedDecoder(const Environment& env, const RecordRef& ref) : env_(env), ref_(ref), reader_(env, ref) {}

  PlacedReferenceData run() {
    data_.signature = ref_.record->header.signature;
    for (const auto& subrecord : ref_.record->subrecords) visit(subrecord);
    return std::move(data_);
  }

private:
  void require_size(const Subrecord& sub, std::size_t size) const {
    if (sub.data.size() != size) reader_.fail(sub.signature, "must be " + std::to_string(size) + " bytes");
  }

  void require_multiple(const Subrecord& sub, std::size_t unit) const {
    if (sub.data.size() % unit != 0) {
      reader_.fail(sub.signature, "must be a multiple of " + std::to_string(unit) + " bytes");
    }
  }

  void once(bool present, const Subrecord& sub) const {
    if (present) reader_.fail(sub.signature, "appears more than once");
  }

  FormKey link(const Subrecord& sub, std::size_t offset = 0) const {
    return reader_.link_at(sub, offset);
  }

  std::optional<FormKey> single_link(const Subrecord& sub, const std::optional<FormKey>& current) const {
    once(current.has_value(), sub);
    require_size(sub, 4);
    return link(sub);
  }

  void in_section(const Subrecord& sub, Section expected) const {
    if (section_ != expected) reader_.fail(sub.signature, "appears outside its section");
  }

  RecordName marker_name(const Subrecord& sub) const {
    const auto& owner = env_.plugin(ref_.plugin);
    if (owner.header.tes4_flags.is_localized()) {
      if (sub.data.size() != 4) return {NameKind::malformed, {}, 0};
      return {NameKind::string_id, {}, Reader::u32(sub, 0)};
    }
    const auto text = zstring_view(sub);
    if (!text) return {NameKind::malformed, {}, 0};
    return {NameKind::inline_text, *text, 0};
  }

  void visit(const Subrecord& sub) {
    const auto s = sub.signature;
    // Section members first: TNAM/INAM/FNAM/FULL/LNAM change meaning with the preceding anchor.
    if (section_ == Section::map_marker && (s == sig("FNAM") || s == sig("FULL") || s == sig("TNAM"))) {
      auto& marker = *data_.map_marker;
      if (s == sig("FNAM")) {
        once(marker.flags.has_value(), sub);
        require_size(sub, 1);
        marker.flags = std::to_integer<std::uint8_t>(sub.data[0]);
      } else if (s == sig("FULL")) {
        once(marker.name.kind != NameKind::absent, sub);
        marker.name = marker_name(sub);
      } else {
        once(marker.type.has_value(), sub);
        require_size(sub, 2);
        marker.type = std::to_integer<std::uint8_t>(sub.data[0]);
      }
      return;
    }
    if (section_ == Section::room && (s == sig("LNAM") || s == sig("INAM") || s == sig("XLRM"))) {
      auto& room = *data_.room;
      if (s == sig("LNAM")) {
        room.lighting_template = single_link(sub, room.lighting_template);
      } else if (s == sig("INAM")) {
        room.image_space = single_link(sub, room.image_space);
      } else {
        require_size(sub, 4);
        room.linked_rooms.push_back(link(sub));
      }
      return;
    }
    if (section_ == Section::patrol &&
        (s == sig("XPPA") || s == sig("INAM") || s == sig("PDTO") || s == sig("TNAM"))) {
      auto& patrol = data_.patrols.back();
      if (s == sig("XPPA")) {
        require_size(sub, 0);
      } else if (s == sig("INAM")) {
        patrol.idle = single_link(sub, patrol.idle);
      } else if (s == sig("TNAM")) {
        patrol.topic = single_link(sub, patrol.topic);
      } else {
        require_size(sub, 8);
        TopicReference topic;
        topic.type = Reader::u32(sub, 0);
        if (topic.type == 0) {
          topic.topic = link(sub, 4);
        } else if (topic.type == 1) {
          topic.subtype = Reader::u32(sub, 4);
        } else {
          reader_.fail(s, "has an unknown topic type");
        }
        patrol.topics.push_back(topic);
      }
      return;
    }
    section_ = Section::none;
    if (s == sig("FNAM") || s == sig("FULL") || s == sig("TNAM") || s == sig("LNAM") ||
        s == sig("INAM") || s == sig("XLRM") || s == sig("XPPA") || s == sig("PDTO")) {
      reader_.fail(s, "appears outside its section");
    }

    if (s == sig("NAME")) {
      data_.base = single_link(sub, data_.base);
    } else if (s == sig("DATA")) {
      once(data_.position.has_value(), sub);
      require_size(sub, 24);
      data_.position = reader_.vec3(sub, 0);
      data_.rotation = reader_.vec3(sub, 12);
    } else if (s == sig("XSCL")) {
      once(data_.scale.has_value(), sub);
      require_size(sub, 4);
      data_.scale = Reader::f32(sub, 0);
    } else if (s == sig("XLYR")) {
      data_.layer = single_link(sub, data_.layer);
    } else if (s == sig("XEZN")) {
      data_.encounter_zone = single_link(sub, data_.encounter_zone);
    } else if (s == sig("XEMI")) {
      data_.emittance = single_link(sub, data_.emittance);
    } else if (s == sig("XRDS")) {
      once(data_.radius.has_value(), sub);
      require_size(sub, 4);
      data_.radius = Reader::f32(sub, 0);
    } else if (s == sig("XLIG")) {
      once(data_.light_data.has_value(), sub);
      if (sub.data.size() != 16 && sub.data.size() != 20 && sub.data.size() != 24) {
        reader_.fail(s, "must be 16, 20 or 24 bytes");
      }
      PlacedLightData light;
      light.fov = Reader::f32(sub, 0);
      light.fade = Reader::f32(sub, 4);
      light.end_distance_cap = Reader::f32(sub, 8);
      light.shadow_depth_bias = Reader::f32(sub, 12);
      if (sub.data.size() >= 20) light.near_clip = Reader::f32(sub, 16);
      if (sub.data.size() >= 24) light.volumetric_intensity = Reader::f32(sub, 20);
      data_.light_data = light;
    } else if (s == sig("XLRL")) {
      data_.location_reference = single_link(sub, data_.location_reference);
    } else if (s == sig("XATR")) {
      data_.attach_ref = single_link(sub, data_.attach_ref);
    } else if (s == sig("XLRT")) {
      once(!data_.location_ref_types.empty(), sub);
      require_multiple(sub, 4);
      for (std::size_t offset = 0; offset < sub.data.size(); offset += 4) {
        data_.location_ref_types.push_back(link(sub, offset));
      }
    } else if (s == sig("XESP")) {
      once(data_.enable_parent.has_value(), sub);
      if (sub.data.size() != 4 && sub.data.size() != 8) reader_.fail(s, "must be 4 or 8 bytes");
      EnableParent parent{link(sub), std::nullopt};
      if (sub.data.size() == 8) parent.flags = std::to_integer<std::uint8_t>(sub.data[4]);
      data_.enable_parent = parent;
    } else if (s == sig("XOWN")) {
      once(data_.ownership.has_value(), sub);
      if (sub.data.size() != 4 && sub.data.size() != 12) reader_.fail(s, "must be 4 or 12 bytes");
      Ownership ownership{link(sub), std::nullopt};
      if (sub.data.size() == 12) ownership.no_crime = sub.data[8] != std::byte{0};
      data_.ownership = ownership;
    } else if (s == sig("XRNK")) {
      once(data_.faction_rank.has_value(), sub);
      require_size(sub, 4);
      data_.faction_rank = Reader::i32(sub, 0);
    } else if (s == sig("XLKR")) {
      if (sub.data.size() == 4) {
        data_.linked_references.push_back({link(sub), FormKey{}});
      } else if (sub.data.size() == 8) {
        data_.linked_references.push_back({link(sub), link(sub, 4)});
      } else {
        reader_.fail(s, "must be 4 or 8 bytes");
      }
    } else if (s == sig("XAPD")) {
      once(data_.activate_parent_flags.has_value(), sub);
      require_size(sub, 1);
      data_.activate_parent_flags = std::to_integer<std::uint8_t>(sub.data[0]);
    } else if (s == sig("XAPR")) {
      require_size(sub, 8);
      data_.activate_parents.push_back({link(sub), Reader::f32(sub, 4)});
    } else if (s == sig("MNAM")) {
      once(data_.comments.has_value(), sub);
      const auto text = cstring(sub);
      if (!text) reader_.fail(s, "is not NUL-terminated");
      data_.comments = text;
    } else if (s == sig("XIS2")) {
      require_size(sub, 0);
      data_.ignored_by_sandbox = true;
    } else if (s == sig("ONAM")) {
      require_size(sub, 0);
      data_.open_by_default = true;
    } else if (s == sig("XMRK")) {
      once(data_.map_marker.has_value(), sub);
      require_size(sub, 0);
      data_.map_marker.emplace();
      section_ = Section::map_marker;
    } else if (s == sig("XTEL")) {
      once(data_.teleport.has_value(), sub);
      if (sub.data.size() != 32 && sub.data.size() != 36) reader_.fail(s, "must be 32 or 36 bytes");
      data_.teleport = Teleport{link(sub), reader_.vec3(sub, 4), reader_.vec3(sub, 16),
                                Reader::u32(sub, 28), FormKey{}};
      if (sub.data.size() == 36) data_.teleport->transition_interior = link(sub, 32);
    } else if (s == sig("XPRM")) {
      once(data_.primitive.has_value(), sub);
      require_size(sub, 32);
      data_.primitive = Primitive{reader_.vec3(sub, 0),
                                  {Reader::f32(sub, 12), Reader::f32(sub, 16), Reader::f32(sub, 20),
                                   Reader::f32(sub, 24)},
                                  Reader::u32(sub, 28)};
    } else if (s == sig("XBSD")) {
      once(data_.spline.has_value(), sub);
      if (sub.data.size() < 20) reader_.fail(s, "is shorter than 20 bytes");
      Spline spline{Reader::f32(sub, 0), Reader::f32(sub, 4), reader_.vec3(sub, 8), std::nullopt};
      if (sub.data.size() > 20) spline.wind_detached_end = sub.data[20] != std::byte{0};
      data_.spline = spline;
    } else if (s == sig("XPOD")) {
      once(!data_.portals.empty(), sub);
      require_multiple(sub, 8);
      for (std::size_t offset = 0; offset < sub.data.size(); offset += 8) {
        data_.portals.push_back({link(sub, offset), link(sub, offset + 4)});
      }
    } else if (s == sig("XPTL")) {
      once(data_.room_portal.has_value(), sub);
      require_size(sub, 36);
      data_.room_portal = SizePositionRotation{
          Reader::f32(sub, 0), Reader::f32(sub, 4), reader_.vec3(sub, 8),
          {Reader::f32(sub, 20), Reader::f32(sub, 24), Reader::f32(sub, 28), Reader::f32(sub, 32)}};
    } else if (s == sig("XRMR")) {
      once(data_.room.has_value(), sub);
      require_size(sub, 4);
      data_.room = RoomBounds{std::to_integer<std::uint8_t>(sub.data[0]),
                              std::to_integer<std::uint8_t>(sub.data[1]), std::nullopt,
                              std::nullopt, {}};
      section_ = Section::room;
    } else if (s == sig("XPRD")) {
      require_size(sub, 4);
      data_.patrols.push_back(Patrol{Reader::f32(sub, 0), std::nullopt, {}, std::nullopt});
      section_ = Section::patrol;
    } else if (s == sig("XCNT")) {
      once(data_.count.has_value(), sub);
      require_size(sub, 4);
      data_.count = Reader::i32(sub, 0);
    } else if (s == sig("XLCM")) {
      once(data_.level_modifier.has_value(), sub);
      require_size(sub, 4);
      data_.level_modifier = Reader::i32(sub, 0);
    } else if (s == sig("VMAD")) {
      once(data_.scripts.has_value(), sub);
      decode_scripts(sub);
    }
  }

  void decode_scripts(const Subrecord& sub) {
    Scripts scripts;
    try {
      scripts.raw = read_vmad_scripts(sub.data);
    } catch (const parse_error& error) {
      reader_.fail(sub.signature, std::string("is malformed: ") + error.what());
    }
    for (const auto& raw : scripts.raw.scripts) {
      Script script{raw.name, raw.flags, {}};
      for (const auto& property : raw.properties) {
        ScriptProperty out;
        out.name = property.name;
        out.type = property.type;
        out.flags = property.flags;
        if (property.object) out.object = env_.resolve_link(ref_, *property.object);
        out.alias = property.alias;
        out.text = property.text;
        out.integer = property.integer;
        out.real = property.real;
        out.boolean = property.boolean;
        out.element_count = property.element_count;
        if (property.object_elements) {
          auto& keys = out.object_elements.emplace();
          for (const auto& element : *property.object_elements) keys.push_back(env_.resolve_link(ref_, element.object));
        }
        if (property.struct_elements) {
          auto& structs = out.struct_member_objects.emplace();
          for (const auto& value : *property.struct_elements) {
            auto& members = structs.emplace_back();
            for (const auto& member : value.members) {
              auto& object = members.emplace_back();
              if (member.object) object = env_.resolve_link(ref_, member.object->object);
            }
          }
        }
        script.properties.push_back(std::move(out));
      }
      scripts.scripts.push_back(std::move(script));
    }
    data_.scripts = std::move(scripts);
  }

  const Environment& env_;
  const RecordRef& ref_;
  Reader reader_;
  PlacedReferenceData data_;
  Section section_{Section::none};
};

}  // namespace

PlacedReferenceData decode_placed_reference(const Environment& env, const RecordRef& ref) {
  if (ref.record == nullptr || !is_placed_reference_signature(ref.record->header.signature)) {
    throw WorldDecodeError("expected a placed reference record");
  }
  return PlacedDecoder(env, ref).run();
}

std::optional<std::string_view> primary_model_path(const RecordRef& ref) noexcept {
  if (ref.record == nullptr) return std::nullopt;
  const auto signature = ref.record->header.signature;
  if (signature == sig("SCOL")) return std::nullopt;
  const auto first_path = [&](Signature wanted) -> std::optional<std::string_view> {
    for (const auto& subrecord : ref.record->subrecords) {
      if (subrecord.signature != wanted) continue;
      const auto text = cstring(subrecord);
      if (text && !text->empty()) return text;
      return std::nullopt;
    }
    return std::nullopt;
  };
  if (signature == sig("ARMO")) {
    if (auto male = first_path(sig("MOD2"))) return male;
    return first_path(sig("MOD4"));
  }
  static constexpr std::array modeled{
      sig("ACTI"), sig("ADDN"), sig("ALCH"), sig("AMMO"), sig("ANIO"), sig("ARTO"), sig("BOOK"), sig("BPTD"),
      sig("CAMS"), sig("CLMT"), sig("CONT"), sig("DOOR"), sig("EFSH"), sig("EXPL"), sig("FLOR"), sig("FURN"),
      sig("GRAS"), sig("HAZD"), sig("HDPT"), sig("IDLM"), sig("INGR"), sig("IPCT"), sig("KEYM"), sig("LIGH"),
      sig("LVLN"), sig("MATO"), sig("MISC"), sig("MSTT"), sig("NOTE"), sig("OMOD"), sig("PROJ"), sig("STAT"),
      sig("TACT"), sig("TERM"), sig("TREE"), sig("WEAP")};
  if (std::ranges::find(modeled, signature) == modeled.end()) return std::nullopt;
  return first_path(sig("MODL"));
}

}  // namespace fo4::env
