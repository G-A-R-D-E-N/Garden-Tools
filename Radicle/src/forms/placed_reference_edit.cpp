#include "fo4/forms/placed_reference.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/schema/form_link_mixed.hpp"
#include "fo4/signature.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <string>
#include <utility>

namespace fo4::forms {
namespace {

constexpr Signature sig(const char (&text)[5]) {
  return Signature{text[0], text[1], text[2], text[3]};
}

constexpr std::initializer_list<const char*> k_refr_order = {
    "EDID", "VMAD", "NAME", "XMBO", "XPRM", "XPOD", "XPTL", "XORD", "XOCP", "XRMR", "LNAM", "INAM", "XLRM",
    "XMBP", "XRDS", "XEMI", "XLIG", "XLTW", "XPWR", "XALP", "XTEL", "XTNM", "XMBR", "XWCN", "XWCU", "XASP",
    "XATP", "XAMC", "XLKT", "XLYR", "XMSP", "XRFG", "XRDO", "XBSD", "XPDD", "XSPC", "XAPD", "XAPR", "XLIB",
    "XLCM", "XLCN", "XTRI", "XLOC", "XEZN", "XNDP", "XLRL", "XLRT", "XIS2", "XOWN", "XRNK", "XCNT", "XHLT",
    "XESP", "XLKR", "XPRD", "XPPA", "XHTW", "XFVC", "ONAM", "XMRK", "FNAM", "TNAM", "XATR", "XPLK", "XWPG",
    "XWPN", "XCVL", "XCVR", "XCZR", "XCZC", "XSCL", "XLOD", "DATA", "MNAM"};
constexpr std::initializer_list<const char*> k_achr_order = {
    "EDID", "VMAD", "NAME", "XEZN", "XLOC", "XLTW", "XPWR", "XPRD", "XPPA", "INAM", "TNAM", "XLCM", "XCNT",
    "XRDS", "XHLT", "XLKR", "XAPD", "XAPR", "XATP", "XLKT", "XRFG", "XLYR", "XMSP", "XATR", "XLCN", "XLRL",
    "XLRT", "XWCN", "XWCU", "XIS2", "XPLK", "XHTW", "XFVC", "XESP", "XOWN", "XRNK", "XEMI", "XMBR", "XIBS",
    "XSCL", "ONAM", "DATA", "MNAM"};
constexpr std::initializer_list<const char*> k_projectile_order = {
    "EDID", "VMAD", "NAME", "XEZN", "XHTW", "XFVC", "XPWR", "XLKR", "XAPD", "XAPR", "XASP", "XATP", "XAMC",
    "XLKT", "XLYR", "XMSP", "XRFG", "XCVR", "XESP", "XOWN", "XRNK", "XRGD", "XHLT", "XEMI", "XMBR", "XIS2",
    "XLRT", "XLRL", "XSCL", "XLOD", "DATA", "MNAM"};

std::optional<std::size_t> field_rank(Signature record, Signature field) {
  const auto& order = record == sig("REFR") ? k_refr_order : record == sig("ACHR") ? k_achr_order : k_projectile_order;
  std::size_t rank = 0;
  for (const auto* text : order) {
    if (field == Signature::from_string(text)) return rank;
    ++rank;
  }
  return std::nullopt;
}

std::vector<Subrecord>::iterator insertion_point(Record& record, Signature field) {
  const auto target = field_rank(record.header.signature, field);
  if (!target) throw PlacedReferenceError("no xEdit field position for " + field.to_string());
  return std::find_if(record.subrecords.begin(), record.subrecords.end(), [&](const Subrecord& sub) {
    const auto rank = field_rank(record.header.signature, sub.signature);
    return rank && *rank > *target;
  });
}

void write_u32(std::vector<std::byte>& data, std::size_t offset, std::uint32_t value) {
  for (std::size_t i = 0; i < 4; ++i) data[offset + i] = std::byte{static_cast<std::uint8_t>(value >> (8 * i))};
}

void write_f32(std::vector<std::byte>& data, std::size_t offset, float value) {
  std::uint32_t bits = 0;
  std::memcpy(&bits, &value, 4);
  write_u32(data, offset, bits);
}

void require_finite(std::initializer_list<float> values) {
  for (const auto value : values) {
    if (!std::isfinite(value)) throw PlacedReferenceError("placed-reference values must be finite");
  }
}

std::size_t count_of(const Record& record, Signature field) {
  return static_cast<std::size_t>(std::count_if(record.subrecords.begin(), record.subrecords.end(),
                                                 [&](const Subrecord& sub) { return sub.signature == field; }));
}

Subrecord* single(Record& record, Signature field) {
  if (count_of(record, field) > 1) throw PlacedReferenceError("placed reference has duplicate " + field.to_string());
  const auto it = std::find_if(record.subrecords.begin(), record.subrecords.end(),
                               [&](const Subrecord& sub) { return sub.signature == field; });
  return it == record.subrecords.end() ? nullptr : &*it;
}

void erase_all(Record& record, Signature field) {
  std::erase_if(record.subrecords, [&](const Subrecord& sub) { return sub.signature == field; });
}

Subrecord& put(Record& record, Signature field, std::vector<std::byte> data) {
  if (auto* existing = single(record, field)) {
    existing->data = std::move(data);
    return *existing;
  }
  Subrecord sub;
  sub.signature = field;
  sub.data = std::move(data);
  return *record.subrecords.insert(insertion_point(record, field), std::move(sub));
}

std::uint32_t encode(const Plugin& plugin, const std::optional<FormKey>& key) {
  if (!key || (key->owner().empty() && key->local().value() == 0)) return 0;
  const auto& owner = key->owner();
  if (owner.empty()) throw PlacedReferenceError("form key " + key->to_string() + " has no plugin name");
  const bool known = owner == plugin.key ||
                     std::any_of(plugin.header.masters.begin(), plugin.header.masters.end(),
                                 [&](const MasterReference& master) { return master.key == owner; });
  if (!known) {
    throw PlacedReferenceError(std::string(owner.filename()) + " is not a master of " +
                               std::string(plugin.key.filename()));
  }
  const auto context = plugin_form_id_context(plugin);
  const auto raw = encode_plugin_form_id(*key, context);
  if (resolve_plugin_form_id(raw, context) != *key) {
    throw PlacedReferenceError("form key " + key->to_string() + " does not fit its plugin's form ID range");
  }
  return raw.value();
}

std::vector<std::byte> u32_bytes(std::uint32_t value) {
  std::vector<std::byte> out(4);
  write_u32(out, 0, value);
  return out;
}

void invalidate(Record& record) {
  record.original_range.clear();
  record.decompressed_payload.clear();
  record.header.data_size = 0;
  record.uncompressed_size = 0;
}

template <class Edit>
std::optional<bool> edit_placed(Plugin& plugin, const FormKey& reference, Edit&& edit) {
  auto next = plugin;
  assign_record_form_keys(next);
  Record* target = nullptr;
  visit_records(next, [&](Record& record) {
    if (target == nullptr && record.header.signature != sig_tes4 && record.form_key == reference) target = &record;
  });
  if (target == nullptr) return std::nullopt;
  if (!is_placed_reference_signature(target->header.signature)) {
    throw PlacedReferenceError("expected a placed-reference record");
  }
  require_decodable(*target);
  if (!edit(next, *target)) return false;
  invalidate(*target);
  next.original_range.clear();
  plugin = std::move(next);
  return true;
}

bool simple(Plugin& plugin, const FormKey& reference, auto&& edit) {
  return edit_placed(plugin, reference, [&](Plugin& next, Record& record) {
           edit(next, record);
           return true;
         }).has_value();
}

PresentFieldEdit present(Plugin& plugin, const FormKey& reference, auto&& edit) {
  const auto result = edit_placed(plugin, reference, edit);
  if (!result) return PresentFieldEdit::record_missing;
  return *result ? PresentFieldEdit::updated : PresentFieldEdit::field_absent;
}

}  // namespace

bool set_placed_reference_scale(Plugin& plugin, const FormKey& reference, std::optional<float> scale) {
  if (scale) require_finite({*scale});
  return simple(plugin, reference, [&](Plugin&, Record& record) {
    if (const auto* existing = single(record, sig("XSCL")); existing != nullptr && existing->data.size() != 4) {
      throw PlacedReferenceError("XSCL must be exactly 4 bytes");
    }
    if (!scale) {
      erase_all(record, sig("XSCL"));
      return;
    }
    std::vector<std::byte> data(4);
    write_f32(data, 0, *scale);
    put(record, sig("XSCL"), std::move(data));
  });
}

bool set_placed_reference_editor_id(Plugin& plugin, const FormKey& reference, std::string_view editor_id) {
  if (editor_id.find('\0') != std::string_view::npos) throw PlacedReferenceError("EditorID must not contain NUL");
  return simple(plugin, reference, [&](Plugin&, Record& record) {
    single(record, sig("EDID"));
    if (editor_id.empty()) {
      erase_all(record, sig("EDID"));
      return;
    }
    std::vector<std::byte> data;
    for (const char ch : editor_id) data.push_back(std::byte{static_cast<std::uint8_t>(ch)});
    data.push_back(std::byte{0});
    put(record, sig("EDID"), std::move(data));
  });
}

bool set_placed_reference_record_flags(Plugin& plugin, const FormKey& reference, std::uint32_t flags) {
  return simple(plugin, reference, [&](Plugin&, Record& record) {
    constexpr auto kept = RecordFlags::compressed | RecordFlags::persistent | RecordFlags::deleted;
    record.header.flags = RecordFlags{(flags & ~kept) | (record.header.flags.value() & kept)};
  });
}

bool set_placed_reference_comments(Plugin& plugin,
                                   const FormKey& reference,
                                   std::optional<std::string_view> comments) {
  if (comments && comments->find('\0') != std::string_view::npos) {
    throw PlacedReferenceError("comments must not contain NUL");
  }
  return simple(plugin, reference, [&](Plugin&, Record& record) {
    single(record, sig("MNAM"));
    if (!comments || comments->empty()) {
      erase_all(record, sig("MNAM"));
      return;
    }
    std::vector<std::byte> data;
    for (const char ch : *comments) data.push_back(std::byte{static_cast<std::uint8_t>(ch)});
    data.push_back(std::byte{0});
    put(record, sig("MNAM"), std::move(data));
  });
}

bool set_placed_reference_location_reference(Plugin& plugin,
                                             const FormKey& reference,
                                             const std::optional<FormKey>& location) {
  return simple(plugin, reference, [&](Plugin& next, Record& record) {
    single(record, sig("XLRL"));
    if (!location) {
      erase_all(record, sig("XLRL"));
      return;
    }
    put(record, sig("XLRL"), u32_bytes(encode(next, location)));
  });
}

bool set_placed_reference_ownership(Plugin& plugin,
                                    const FormKey& reference,
                                    const std::optional<FormKey>& owner,
                                    bool no_crime,
                                    std::optional<std::int32_t> faction_rank) {
  return simple(plugin, reference, [&](Plugin& next, Record& record) {
    auto* existing = single(record, sig("XOWN"));
    single(record, sig("XRNK"));
    if (!owner) {
      erase_all(record, sig("XOWN"));
      erase_all(record, sig("XRNK"));
      return;
    }
    std::vector<std::byte> data(12, std::byte{0});
    if (existing != nullptr) {
      if (existing->data.size() != 4 && existing->data.size() != 12) {
        throw PlacedReferenceError("XOWN must be 4 or 12 bytes");
      }
      std::copy(existing->data.begin(), existing->data.end(), data.begin());
    }
    write_u32(data, 0, encode(next, owner));
    data[8] = std::byte{static_cast<std::uint8_t>(no_crime ? 1 : 0)};
    put(record, sig("XOWN"), std::move(data));
    if (faction_rank) {
      put(record, sig("XRNK"), u32_bytes(static_cast<std::uint32_t>(*faction_rank)));
    } else {
      erase_all(record, sig("XRNK"));
    }
  });
}

bool set_placed_reference_enable_parent(Plugin& plugin,
                                        const FormKey& reference,
                                        const std::optional<FormKey>& parent,
                                        std::uint8_t flags) {
  return simple(plugin, reference, [&](Plugin& next, Record& record) {
    auto* existing = single(record, sig("XESP"));
    if (!parent) {
      erase_all(record, sig("XESP"));
      return;
    }
    std::vector<std::byte> data(8, std::byte{0});
    if (existing != nullptr) {
      if (existing->data.size() != 4 && existing->data.size() != 8) {
        throw PlacedReferenceError("XESP must be 4 or 8 bytes");
      }
      std::copy(existing->data.begin(), existing->data.end(), data.begin());
    }
    write_u32(data, 0, encode(next, parent));
    data[4] = std::byte{flags};
    put(record, sig("XESP"), std::move(data));
  });
}

bool set_placed_reference_linked_references(Plugin& plugin,
                                            const FormKey& reference,
                                            std::span<const LinkedReferenceEdit> links) {
  return simple(plugin, reference, [&](Plugin& next, Record& record) {
    std::vector<Subrecord> fresh;
    for (const auto& link : links) {
      Subrecord sub;
      sub.signature = sig("XLKR");
      sub.data.resize(8);
      write_u32(sub.data, 0, encode(next, link.keyword));
      write_u32(sub.data, 4, encode(next, link.reference));
      fresh.push_back(std::move(sub));
    }
    auto first = std::find_if(record.subrecords.begin(), record.subrecords.end(),
                              [](const Subrecord& sub) { return sub.signature == sig("XLKR"); });
    std::size_t at = static_cast<std::size_t>(first - record.subrecords.begin());
    if (first == record.subrecords.end()) {
      at = static_cast<std::size_t>(insertion_point(record, sig("XLKR")) - record.subrecords.begin());
    }
    erase_all(record, sig("XLKR"));
    record.subrecords.insert(record.subrecords.begin() + static_cast<std::ptrdiff_t>(at),
                             std::make_move_iterator(fresh.begin()), std::make_move_iterator(fresh.end()));
  });
}

PresentFieldEdit set_placed_reference_teleport(Plugin& plugin,
                                               const FormKey& reference,
                                               const FormKey& door,
                                               const PositionRotation& destination) {
  require_finite({destination.x, destination.y, destination.z, destination.rx, destination.ry, destination.rz});
  return present(plugin, reference, [&](Plugin& next, Record& record) {
    auto* xtel = single(record, sig("XTEL"));
    if (xtel == nullptr) return false;
    if (xtel->data.size() != 32 && xtel->data.size() != 36) throw PlacedReferenceError("XTEL must be 32 or 36 bytes");
    write_u32(xtel->data, 0, encode(next, door));
    const std::array<float, 6> values{destination.x, destination.y, destination.z,
                                      destination.rx, destination.ry, destination.rz};
    for (std::size_t i = 0; i < values.size(); ++i) write_f32(xtel->data, 4 + i * 4, values[i]);
    return true;
  });
}

PresentFieldEdit set_placed_reference_primitive(Plugin& plugin,
                                                const FormKey& reference,
                                                const std::array<float, 3>& bounds,
                                                std::uint32_t type) {
  require_finite({bounds[0], bounds[1], bounds[2]});
  return present(plugin, reference, [&](Plugin&, Record& record) {
    auto* xprm = single(record, sig("XPRM"));
    if (xprm == nullptr) return false;
    if (xprm->data.size() != 32) throw PlacedReferenceError("XPRM must be 32 bytes");
    for (std::size_t i = 0; i < 3; ++i) write_f32(xprm->data, i * 4, bounds[i]);
    write_u32(xprm->data, 28, type);
    return true;
  });
}

PresentFieldEdit set_placed_reference_activate_parent_delay(Plugin& plugin,
                                                            const FormKey& reference,
                                                            std::size_t index,
                                                            float delay) {
  require_finite({delay});
  return present(plugin, reference, [&](Plugin&, Record& record) {
    std::size_t seen = 0;
    for (auto& sub : record.subrecords) {
      if (sub.signature != sig("XAPR")) continue;
      if (seen++ != index) continue;
      if (sub.data.size() != 8) throw PlacedReferenceError("XAPR must be 8 bytes");
      write_f32(sub.data, 4, delay);
      return true;
    }
    return false;
  });
}

}  // namespace fo4::forms
