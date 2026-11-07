#include "fo4/schema/form_links.hpp"

#include "non_formid_slots.hpp"
#include "fo4/schema/form_link_mixed.hpp"
#include "fo4/schema/form_link_schema.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/schema/vmad.hpp"
#include "fo4/signature.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <map>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace fo4 {
std::vector<std::size_t> ctda_formid_offsets(const Subrecord& sub);
namespace {

constexpr std::uint32_t k_formid_subrecords[] = {
#include "fo4/schema/formid_subrecords.inc"
};

struct StructuredFormIdSlot {
  std::uint32_t signature;
  std::uint16_t offset;
  std::uint16_t min_size;
};

constexpr StructuredFormIdSlot k_structured_formids[] = {
#include "fo4/schema/structured_formids.inc"
};

#include "form_link_signatures.inc"

std::uint32_t read_u32(std::span<const std::byte> data, std::size_t offset) {
  std::uint32_t value = 0;
  std::memcpy(&value, data.data() + offset, 4);
  return value;
}

bool info_known_non_formid_signature(Signature signature) {
  return signature == k_edid || signature == k_enam || signature == k_iovr ||
         signature == k_nam0 ||
         signature == k_nam1 || signature == k_nam2 || signature == k_nam3 ||
         signature == k_nam4 || signature == k_nam9 || signature == k_tnam ||
         signature == k_inam || signature == k_rnam ||
         signature == k_intv || signature == k_alfa || signature == k_gree ||
         signature == k_tiqs || signature == k_incc || signature == k_wzmd ||
         signature == k_cis1 || signature == k_cis2;
}

bool info_known_link_signature(Signature signature) {
  return signature == k_tpic || signature == k_pnam || signature == k_dnam ||
         signature == k_gnam || signature == k_snam || signature == k_lnam ||
         signature == k_sraf || signature == k_anam || signature == k_tsce ||
         signature == k_onam || signature == k_modq;
}

void visit_info_link_offsets(const Subrecord& sub, bool strict_remap, auto&& fn) {
  if (info_known_non_formid_signature(sub.signature)) return;

  if (info_known_link_signature(sub.signature)) {
    if (sub.data.size() != 4) {
      if (strict_remap) {
        throw FormIdError(
            "cannot safely remap malformed INFO FormLink subrecord " +
            sub.signature.to_string());
      }
      return;
    }
    fn(0);
    return;
  }

  if (sub.signature == k_trda) {
    if (sub.data.size() != 20) {
      if (strict_remap) {
        throw FormIdError("cannot safely remap malformed INFO TRDA payload");
      }
      return;
    }
    if (read_u32(sub.data, 0) != 0xFFFFFFFFu) fn(0);
    fn(5);
    return;
  }

  if (sub.signature == sig_vmad || sub.signature == sig_ctda) return;
  if (strict_remap) {
    throw FormIdError(
        "cannot safely remap unknown INFO subrecord " + sub.signature.to_string());
  }
}

bool same_form_id_context(const PluginFormIdContext& left,
                          const PluginFormIdContext& right) {
  if (left.current_plugin != right.current_plugin ||
      left.current_kind != right.current_kind ||
      left.masters.size() != right.masters.size()) {
    return false;
  }
  for (std::size_t index = 0; index < left.masters.size(); ++index) {
    if (left.masters[index].key != right.masters[index].key ||
        left.masters[index].kind != right.masters[index].kind) {
      return false;
    }
  }
  return true;
}

void write_u32(std::span<std::byte> data, std::size_t offset, std::uint32_t value) {
  std::memcpy(data.data() + offset, &value, 4);
}

bool is_listed_formid_signature(Signature signature) {
  const auto packed = signature.packed();
  const auto begin = std::begin(k_formid_subrecords);
  const auto end = std::end(k_formid_subrecords);
  return std::find(begin, end, packed) != end;
}

bool allowed_unclaimed_payload(Signature record, Signature subrecord) {
  struct Pair {
    const char* record;
    const char* subrecord;
  };
  static constexpr Pair k_allowed[] = {
#include "fo4/schema/unclaimed_link_allowlist.inc"
  };
  return std::any_of(std::begin(k_allowed), std::end(k_allowed), [&](const Pair& pair) {
    return record == Signature::from_string(pair.record) &&
           subrecord == Signature::from_string(pair.subrecord);
  });
}

bool is_formid_array_signature(Signature signature) {
  return signature == sig_kwda || signature == Signature::from_string("SPLO") ||
         signature == sig_xlkr || signature == Signature::from_string("APPR");
}

bool record_specific_formid_array(const Record& record, const Subrecord& sub) {
  // xEdit FO4 NAVM: ONAM is Base Objects, an array of FormIDs. PNAM and NNAM
  // beside it are arrays of u16 vertex indexes and must not be scanned as IDs.
  return record.header.signature == k_navm && sub.signature == sig_onam;
}

bool navm_vertex_index_signature(const Record& record, const Subrecord& sub) {
  return record.header.signature == k_navm &&
         (sub.signature == k_pnam || sub.signature == k_nnam);
}

bool visit_tagged_formid_slot(const Subrecord& sub, auto&& fn) {
  if (sub.data.size() < 8) {
    return false;
  }

  const auto type = read_u32(sub.data, 0);
  if (sub.signature == Signature::from_string("PTDT")) {
    if (type == 0 || type == 1) {
      fn(4);
    }
    return true;
  }

  if (sub.signature == Signature::from_string("PDTO")) {
    if (type == 0) {
      fn(4);
    }
    return true;
  }

  return false;
}

bool visit_structured_slots(Signature signature, std::size_t size, auto&& fn) {
  const auto packed = signature.packed();
  bool matched = false;
  for (const auto& slot : k_structured_formids) {
    if (slot.signature == packed && size >= slot.min_size) {
      fn(static_cast<std::size_t>(slot.offset));
      matched = true;
    }
  }
  return matched;
}


bool visit_coed_formids(const Subrecord& sub,
                        bool strict_remap,
                        const FormLinkTypeLookup& type_lookup,
                        const PluginFormIdContext* context,
                        auto&& fn) {
  if (sub.data.size() != 12) {
    if (strict_remap) {
      throw FormIdError("cannot safely remap malformed COED payload");
    }
    return true;
  }
  const auto owner_raw = RawFormId{read_u32(sub.data, 0)};
  const auto union_raw = read_u32(sub.data, 4);
  bool remap_global = false;
  if (!owner_raw.is_null() && union_raw != 0) {
    if (!type_lookup || context == nullptr) {
      if (strict_remap) {
        throw FormIdError("cannot safely remap COED union without owner type");
      }
    } else {
      FormKey owner_key;
      try {
        owner_key = resolve_plugin_link(owner_raw, *context);
      } catch (const FormIdError&) {
        if (strict_remap) {
          throw FormIdError("cannot safely remap COED union: owner FormID does not resolve");
        }
        owner_key = FormKey{};
      }
      if (!owner_key.owner().empty() || owner_key.local().value() != 0) {
        const auto owner_type = type_lookup(owner_key);
        if (!owner_type.has_value()) {
          if (strict_remap) {
            throw FormIdError("cannot safely remap COED union: owner type unknown");
          }
        } else if (*owner_type == k_npc_) {
          remap_global = true;
        } else if (*owner_type != k_fact) {
          if (strict_remap) {
            throw FormIdError("cannot safely remap COED union: owner is not NPC_ or FACT");
          }
        }
      }
    }
  }
  fn(0);
  if (remap_global) {
    fn(4);
  }
  return true;
}

bool visit_link_offsets(const Record& record, const Subrecord& sub,
                        bool strict_remap,
                        const FormLinkTypeLookup& type_lookup,
                        const PluginFormIdContext* context,
                        auto&& fn) {
  if (sub.signature == sig_xxxx) {
    return true;
  }
  if (record.header.signature == k_info) {
    if (sub.signature != sig_vmad && sub.signature != sig_ctda &&
        !info_known_non_formid_signature(sub.signature) &&
        !info_known_link_signature(sub.signature) && sub.signature != k_trda) {
      visit_info_link_offsets(sub, strict_remap, fn);
      return true;
    }
    if (sub.signature == k_trda || info_known_link_signature(sub.signature) ||
        info_known_non_formid_signature(sub.signature)) {
      visit_info_link_offsets(sub, strict_remap, fn);
      return true;
    }
  }
  if (record.header.signature == k_ipds && sub.signature == k_pnam) {
    if (sub.data.size() != 8) {
      if (strict_remap) {
        throw FormIdError("cannot safely remap malformed IPDS PNAM payload");
      }
      return true;
    }
    fn(0);
    fn(4);
    return true;
  }
  if (record.header.signature == k_dobj && sub.signature == k_dnam) {
    if (sub.data.size() % 8 != 0 ||
        std::count_if(record.subrecords.begin(), record.subrecords.end(),
                      [](const Subrecord& item) { return item.signature == k_dnam; }) != 1) {
      if (strict_remap) {
        throw FormIdError("cannot safely remap malformed DOBJ DNAM payload or cardinality");
      }
      return true;
    }
    for (std::size_t offset = 4; offset < sub.data.size(); offset += 8) {
      fn(offset);
    }
    return true;
  }
  if (const auto assoc = mgef_assoc_item_formid_offsets(record, sub)) {
    for (const auto offset : *assoc) fn(offset);
  }
  if (const auto mixed = mixed_formid_offsets(record, sub, strict_remap)) {
    for (const auto offset : *mixed) fn(offset);
    return true;
  }
  if (sub.data.empty()) {
    return true;
  }
  // xEdit FO4 uses SRAF in two incompatible ways: INFO response Camera Path
  // is a FormID, while RACE subgraph SRAF is {u16 role, u16 perspective} and
  // terminates one subgraph. Never interpret the RACE flags as a FormID.
  if (record.header.signature == k_race && sub.signature == k_sraf) {
    return true;
  }
  // QUST has the same signature-reuse problem on a larger scale. Classify the
  // proven QUST-only meanings before consulting the global signature table.
  // ALFR Specific Reference and ALCO Create Reference Object are FormLinks but
  // are not present in the generated wbFormIDCk signature list. The listed
  // signatures below are scalar/string data in QUST even though they are
  // FormIDs elsewhere.
  if (record.header.signature == k_qust) {
    if (sub.signature == k_alfr || sub.signature == k_alco) {
      if (sub.data.size() == 4) fn(0);
      return true;
    }
    if (sub.signature == k_alla) {
      if (sub.data.size() % 8 != 0) {
        if (strict_remap) {
          throw FormIdError("cannot safely remap malformed QUST ALLA payload");
        }
        return true;
      }
      for (std::size_t offset = 0; offset < sub.data.size(); offset += 8) {
        fn(offset);
      }
      return true;
    }
    if (schema::qust_non_formid_signature(sub.signature)) {
      return true;
    }
  }
  if (record.header.signature == k_wthr) {
    const auto sig = sub.signature;
    const auto size = sub.data.size();
    auto fail_malformed = [&](const char* message) {
      if (strict_remap) {
        throw FormIdError(message);
      }
    };
    if (sig == k_mnam || sig == k_nnam || sig == k_gnam || sig == k_tnam) {
      if (size != 4) {
        fail_malformed("cannot safely remap malformed WTHR FormLink payload");
        return true;
      }
      fn(0);
      return true;
    }
    if (sig == k_imsp) {
      if (size != 16 && size != 32) {
        fail_malformed("cannot safely remap malformed WTHR IMSP payload");
        return true;
      }
      for (std::size_t offset = 0; offset < size; offset += 4) fn(offset);
      return true;
    }
    if (sig == k_wgdr) {
      if (size != 32) {
        fail_malformed("cannot safely remap malformed WTHR WGDR payload");
        return true;
      }
      for (std::size_t offset = 0; offset < size; offset += 4) fn(offset);
      return true;
    }
    if (sig == k_snam) {
      if (size != 8) {
        fail_malformed("cannot safely remap malformed WTHR SNAM payload");
        return true;
      }
      fn(0);
      return true;
    }
    if (sig == k_unam) {
      if (size != 12 && size != 24) {
        fail_malformed("cannot safely remap malformed WTHR UNAM payload");
        return true;
      }
      fn(0);
      fn(8);
      return true;
    }
    if (sig == k_vnam || sig == k_wnam || sig == k_lnam || sig == k_nam1 ||
        sig == k_pnam || sig == k_jnam || sig == k_nam0 || sig == k_nam4 ||
        sig == k_fnam || sig == k_rnam || sig == k_qnam || sig == k_dalc ||
        sig == k_data || sig == k_edid) {
      return true;
    }
    if (is_listed_formid_signature(sig) && size == 4) {
      fail_malformed(
          "cannot safely remap unknown WTHR FormID-listed subrecord");
      return true;
    }
    return true;
  }
  if (record.header.signature == k_clmt && sub.signature == k_wlst) {
    if (sub.data.size() % 12 != 0) {
      if (strict_remap) {
        throw FormIdError("cannot safely remap malformed CLMT WLST payload");
      }
      return true;
    }
    for (std::size_t offset = 0; offset < sub.data.size(); offset += 12) {
      fn(offset);
      fn(offset + 8);
    }
    return true;
  }
  // OBTS is the BGSMod::Template body: a variable-length header whose keyword
  // and include FormIDs sit at offsets derived from two counts, so it cannot
  // be expressed as a fixed structured slot.
  //
  //   0x00 u32 include count I      0x04 u32 property count P
  //   0x0F u8  keyword count K      0x10 K*4 keyword FormIDs
  //   0x10+4K  u8 min level, u8 alt levels
  //   0x12+4K  I*7  include rows { u32 OMOD FormID, u8, u8, u8 }
  //            P*24 property rows
  //
  // Layout validated on all 3,297 shipped OBTS bodies (ARMO, WEAP, NPC_ and
  // FURN): the computed size matches the payload exactly, zero mismatches,
  // and all 7,342 Fallout4.esm include FormIDs resolve to OMOD records.
  if (const auto offsets = struct_payload_formid_offsets(record, sub, strict_remap)) {
    for (const auto offset : *offsets) fn(offset);
    return true;
  }
  if (sub.signature == k_obts) {
    const auto offsets = obts_formid_offsets(sub);
    if (!offsets.has_value()) {
      if (strict_remap) {
        throw FormIdError("cannot safely remap malformed OBTS template payload");
      }
      return true;
    }
    for (const auto offset : *offsets) fn(offset);
    return true;
  }
  if (is_navmesh_mixed_payload(record, sub)) {
    const auto offsets = navmesh_mixed_formid_offsets(record, sub);
    if (!offsets.has_value()) {
      if (strict_remap) {
        throw FormIdError(
            "cannot safely remap malformed NAVM/NAVI mixed FormID payload " +
            sub.signature.to_string());
      }
      return true;
    }
    for (const auto offset : *offsets) fn(offset);
    return true;
  }
  if (record.header.signature == sig_tes4 && sub.signature == sig_onam &&
      sub.data.size() % 4 == 0) {
    for (std::size_t i = 0; i < sub.data.size(); i += 4) {
      fn(i);
    }
    return true;
  }
  if (navm_vertex_index_signature(record, sub)) {
    return true;
  }
  if (record_specific_formid_array(record, sub) && sub.data.size() % 4 == 0) {
    for (std::size_t i = 0; i < sub.data.size(); i += 4) {
      fn(i);
    }
    return true;
  }
  if (schema::scene_action_topic_data(record, sub)) {
    if (sub.data.size() != 4) {
      if (strict_remap) {
        throw FormIdError("cannot safely remap malformed SCEN action Topic DATA payload");
      }
      return true;
    }
    fn(0);
    return true;
  }
  if (is_formid_array_signature(sub.signature) && sub.data.size() % 4 == 0) {
    for (std::size_t i = 0; i < sub.data.size(); i += 4) {
      fn(i);
    }
    return true;
  }
  if (is_listed_formid_signature(sub.signature) && sub.data.size() == 4 &&
      !schema::corpus_non_formid_slot(record, sub)) {
    fn(0);
    return true;
  }
  if (visit_tagged_formid_slot(sub, fn)) {
    return true;
  }
  if (sub.signature == sig_coed) {
    visit_coed_formids(sub, strict_remap, type_lookup, context, fn);
    return true;
  }
  if (visit_structured_slots(sub.signature, sub.data.size(), fn)) {
    return true;
  }
  if (sub.signature == sig_vmad) {
    if (strict_remap) {
      auto offsets = vmad_formid_offsets_for_remap(sub.data, record.header.signature);
      if (!offsets.has_value()) {
        throw FormIdError(
            "cannot safely remap incomplete VMAD: unsupported property types or "
            "an unparsed remainder would leave stale FormIDs behind");
      }
      for (const auto offset : *offsets) {
        fn(offset);
      }
    } else {
      for (const auto offset : vmad_formid_offsets(sub.data, record.header.signature)) {
        fn(offset);
      }
    }
    return true;
  }
  for (const auto offset : ctda_formid_offsets(sub)) {
    fn(offset);
  }
  return sub.signature == sig_ctda;
}

bool visit_formlink_offsets(const Record& record, const Subrecord& sub,
                            const schema::SubrecordFormLinkOffsets& schema_offsets,
                            bool strict_remap, const FormLinkTypeLookup& type_lookup,
                            const PluginFormIdContext* context, auto&& fn) {
  const auto index = static_cast<std::size_t>(&sub - record.subrecords.data());
  if (schema_offsets.empty() || schema_offsets[index].empty()) {
    return visit_link_offsets(record, sub, strict_remap, type_lookup, context, fn);
  }
  std::vector<std::size_t> offsets;
  visit_link_offsets(record, sub, strict_remap, type_lookup, context,
                     [&](std::size_t offset) { offsets.push_back(offset); });
  const auto& extra = schema_offsets[index];
  offsets.insert(offsets.end(), extra.begin(), extra.end());
  std::sort(offsets.begin(), offsets.end());
  offsets.erase(std::unique(offsets.begin(), offsets.end()), offsets.end());
  for (const auto offset : offsets) fn(offset);
  return true;
}

void remap_raw(std::span<std::byte> data, std::size_t offset,
               const PluginFormIdContext& old_context,
               const PluginFormIdContext& new_context) {
  if (offset + 4 > data.size()) {
    return;
  }
  const auto raw = RawFormId{read_u32(data, offset)};
  if (raw.is_null() || raw.is_none()) {
    return;
  }
  const auto key = resolve_plugin_link(raw, old_context);
  const auto encoded = encode_plugin_form_id(key, new_context);
  if (encoded != raw) {
    write_u32(data, offset, encoded.value());
  }
}
}  // namespace

void remap_record_form_links(Record& record,
                             const PluginFormIdContext& old_context,
                             const PluginFormIdContext& new_context,
                             const FormLinkTypeLookup& type_lookup) {
  require_decodable(record);
  if (record.header.signature == k_info &&
      same_form_id_context(old_context, new_context)) {
    return;
  }
  const auto schema_offsets = schema::schema_formlink_offsets(record);
  for (auto& sub : record.subrecords) {
    auto bytes = std::span<std::byte>(sub.data);
    visit_formlink_offsets(record, sub, schema_offsets, true, type_lookup, &old_context,
                           [&](std::size_t offset) {
      remap_raw(bytes, offset, old_context, new_context);
    });
  }
}

FormLinkTypeLookup plugin_type_lookup(const Plugin& plugin) {
  return [&plugin](const FormKey& key) -> std::optional<Signature> {
    const auto* record = find_record(plugin, key);
    if (record == nullptr) return std::nullopt;
    return record->header.signature;
  };
}

void remap_form_links(Plugin& plugin, const PluginFormIdContext& old_context,
                      const PluginFormIdContext& new_context,
                      FormLinkTypeLookup type_lookup) {
  if (!type_lookup) type_lookup = plugin_type_lookup(plugin);
  visit_records(plugin, [](const Record& record) { require_decodable(record); });
  visit_records(plugin, [&](Record& record) {
    remap_record_form_links(record, old_context, new_context, type_lookup);
  });
}

void rekey_form_links(Plugin& plugin,
                      const FormKey& old_key,
                      const FormKey& new_key) {
  auto next = plugin;
  const auto context = plugin_form_id_context(next);
  const auto type_lookup = plugin_type_lookup(plugin);
  bool any_changed = false;

  visit_records(next, [&](Record& record) {
    require_decodable(record);
    bool changed = false;
    const auto schema_offsets = schema::schema_formlink_offsets(record);
    for (auto& sub : record.subrecords) {
      auto bytes = std::span<std::byte>(sub.data);
      visit_formlink_offsets(record, sub, schema_offsets, true, type_lookup, &context,
                             [&](std::size_t offset) {
        if (offset + 4 > bytes.size()) return;
        const auto raw = RawFormId{read_u32(bytes, offset)};
        if (raw.is_null() || raw.is_none()) return;
        if (resolve_plugin_link(raw, context) != old_key) return;
        const auto replacement = encode_plugin_form_id(new_key, context);
        if (replacement == raw) return;
        write_u32(bytes, offset, replacement.value());
        changed = true;
      });
    }
    if (changed) {
      record.original_range.clear();
      record.decompressed_payload.clear();
      record.header.data_size = 0;
      record.uncompressed_size = 0;
      any_changed = true;
    }
  });

  if (any_changed) {
    next.original_range.clear();
  }
  plugin = std::move(next);
}

std::vector<FormLinkHit> collect_form_links(const Plugin& plugin) {
  return collect_form_links_detailed(plugin).links;
}

namespace {

void collect_record_links(const Record& record, const PluginFormIdContext& context,
                          const FormLinkTypeLookup& type_lookup, FormLinkCollection& result) {
  auto& hits = result.links;
  bool current_record_incomplete = false;
  bool current_record_unsupported = false;
  std::map<std::uint32_t, std::size_t> ordinals;
  const auto schema_offsets = schema::schema_formlink_offsets(record);
  for (const auto& sub : record.subrecords) {
    if (sub.signature == sig_vmad) {
      const auto coverage = vmad_coverage(sub.data, record.header.signature);
      if (!coverage.complete()) current_record_incomplete = true;
      if (coverage.unsupported) current_record_unsupported = true;
    }
    const auto ordinal = ordinals[sub.signature.packed()]++;
    const bool claimed = visit_formlink_offsets(
        record, sub, schema_offsets, false, type_lookup, &context, [&](std::size_t position) {
      if (position + 4 > sub.data.size()) {
        return;
      }
      FormLinkHit hit;
      hit.record = record.form_key;
      hit.subrecord = sub.signature;
      hit.subrecord_ordinal = ordinal;
      hit.offset = position;
      hit.raw = RawFormId{read_u32(sub.data, position)};
      if (!hit.raw.is_null()) {
        try {
          hit.key = resolve_plugin_link(hit.raw, context);
        } catch (const FormIdError&) {
        }
      }
      hits.push_back(std::move(hit));
    });
    if (!claimed && sub.data.size() != 4 && is_listed_formid_signature(sub.signature) &&
        !allowed_unclaimed_payload(record.header.signature, sub.signature)) {
      result.unclaimed_link_payloads.push_back(
          UnclaimedLinkPayload{record.form_key, sub.signature, ordinal, sub.data.size()});
    }
  }
  if (current_record_incomplete) {
    ++result.records_with_incomplete_vmad;
  }
  if (current_record_unsupported) {
    result.unsupported_vmad_records.push_back(record.form_key);
  }
}

}  // namespace

FormLinkCollection collect_record_form_links(const Record& record,
                                             const PluginFormIdContext& context,
                                             const FormLinkTypeLookup& type_lookup) {
  FormLinkCollection result;
  collect_record_links(record, context, type_lookup, result);
  return result;
}

FormLinkCollection collect_form_links_detailed(const Plugin& plugin) {
  FormLinkCollection result;
  const auto context = plugin_form_id_context(plugin);
  const auto type_lookup = plugin_type_lookup(plugin);
  visit_records(plugin, [&](const Record& record) {
    collect_record_links(record, context, type_lookup, result);
  });
  return result;
}

}  // namespace fo4
