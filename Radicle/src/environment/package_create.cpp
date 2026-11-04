#include "fo4/environment/package_procedure.hpp"

#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"

#include <array>
#include <string>
#include <utility>
#include <vector>

namespace fo4::env {
namespace {

constexpr Signature sig(const char (&s)[5]) { return Signature{s[0], s[1], s[2], s[3]}; }

std::vector<std::byte> zstring(std::string_view text) {
  std::vector<std::byte> out;
  for (const char ch : text) out.push_back(static_cast<std::byte>(ch));
  out.push_back(std::byte{0});
  return out;
}

void put32(std::vector<std::byte>& out, std::uint32_t value) {
  for (int shift = 0; shift < 32; shift += 8) out.push_back(static_cast<std::byte>((value >> shift) & 0xFFu));
}

std::vector<std::byte> bytes(std::initializer_list<std::uint8_t> list) {
  std::vector<std::byte> out;
  for (const auto value : list) out.push_back(static_cast<std::byte>(value));
  return out;
}

struct BoolInput {
  std::uint8_t id;
  bool value;
  const char* label;
};

}  // namespace

const char* travel_package_status_message(TravelPackageStatus status) {
  switch (status) {
    case TravelPackageStatus::created: return "created";
    case TravelPackageStatus::invalid_editor_id: return "Editor ID is not valid for this record";
    case TravelPackageStatus::location_unsupported:
      return "location type must be reference, cell, near package start, near editor location or near self";
    case TravelPackageStatus::location_form_missing: return "this location type needs a form";
    case TravelPackageStatus::location_form_unexpected: return "this location type takes no form";
    case TravelPackageStatus::speed_invalid: return "speed must be walk, jog, run or fast_walk";
    case TravelPackageStatus::form_unresolvable: return "location form cannot be encoded in this plugin";
  }
  return "unknown status";
}

TravelPackageResult create_travel_package(Plugin& plugin,
                                          std::string_view editor_id,
                                          const TravelPackageOptions& options) {
  using Type = PackageLocationType;
  if (editor_id.empty() || editor_id.find('\0') != std::string_view::npos) {
    return {TravelPackageStatus::invalid_editor_id, {}};
  }
  const bool needs_form = options.location_type == Type::near_reference || options.location_type == Type::in_cell;
  const bool no_form = options.location_type == Type::near_package_start ||
                       options.location_type == Type::near_editor_location || options.location_type == Type::near_self;
  if (!needs_form && !no_form) return {TravelPackageStatus::location_unsupported, {}};
  if (needs_form && !options.location_form.has_value()) return {TravelPackageStatus::location_form_missing, {}};
  if (no_form && options.location_form.has_value()) return {TravelPackageStatus::location_form_unexpected, {}};
  if (options.preferred_speed > 3) return {TravelPackageStatus::speed_invalid, {}};

  std::uint32_t location_value = 0;
  if (needs_form) {
    try {
      location_value = encode_plugin_form_id(*options.location_form, plugin_form_id_context(plugin)).value();
    } catch (const FormIdError&) {
      return {TravelPackageStatus::form_unresolvable, {}};
    }
  }

  std::array<BoolInput, 3> flags{{{3, options.prefer_preferred_path, "Prefer Preferred Path?"},
                                  {5, options.use_soft_radius, "Use \"soft radius\"?"},
                                  {7, options.ignore_marker_orientation, "Ignore marker orientation?"}}};

  std::vector<Subrecord> subs;
  const auto add = [&subs](const char (&tag)[5], std::vector<std::byte> data) {
    subs.push_back(Subrecord{sig(tag), std::move(data), false});
  };

  add("EDID", zstring(editor_id));
  add("PKDT", bytes({0, 0, 0, 0, 0x12, 0, options.preferred_speed, 0, 0, 0, 0, 0}));
  add("PSDT", bytes({0, 0xFF, 0, 0xFF, 0xFF, 0, 0, 0, 0, 0, 0, 0}));
  std::vector<std::byte> counter;
  put32(counter, 4);
  put32(counter, 0);
  put32(counter, 1);
  add("PKCU", std::move(counter));

  add("ANAM", zstring("Location"));
  std::vector<std::byte> location;
  put32(location, static_cast<std::uint32_t>(options.location_type));
  put32(location, location_value);
  put32(location, static_cast<std::uint32_t>(options.radius));
  put32(location, 0);
  add("PLDT", std::move(location));
  for (const auto& flag : flags) {
    add("ANAM", zstring("Bool"));
    add("CNAM", bytes({static_cast<std::uint8_t>(flag.value ? 1 : 0)}));
  }
  add("UNAM", bytes({1}));
  for (const auto& flag : flags) add("UNAM", bytes({flag.id}));
  add("XNAM", bytes({8}));

  add("ANAM", zstring("Sequence"));
  add("CITC", bytes({0, 0, 0, 0}));
  add("PRCB", bytes({1, 0, 0, 0, 0, 0, 0, 0}));
  add("ANAM", zstring("Procedure"));
  add("CITC", bytes({0, 0, 0, 0}));
  add("PNAM", zstring("Travel"));
  add("FNAM", bytes({1, 0, 0, 0}));
  for (const std::uint8_t id : std::initializer_list<std::uint8_t>{1, 0xFF, 3, 5, 7, 0xFF, 0xFF, 0xFF}) add("PKC2", bytes({id}));

  const auto name = [&add](std::uint8_t id, std::string_view label) {
    add("UNAM", bytes({id}));
    add("BNAM", zstring(label));
    add("PNAM", bytes({1, 0, 0, 0}));
  };
  name(1, "Place to Travel");
  for (const auto& flag : flags) name(flag.id, flag.label);

  for (const char* marker : {"POBA", "POEA", "POCA"}) {
    subs.push_back(Subrecord{Signature{marker[0], marker[1], marker[2], marker[3]}, {}, false});
    add("INAM", bytes({0, 0, 0, 0}));
    add("PDTO", bytes({0, 0, 0, 0, 0, 0, 0, 0}));
  }

  const auto key = create_local_record(plugin, sig("PACK"), std::move(subs));
  return {TravelPackageStatus::created, key};
}

}  // namespace fo4::env
