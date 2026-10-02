#include "fo4/schema/form_links.hpp"

#include "non_formid_slots.hpp"
#include "fo4/schema/form_link_mixed.hpp"

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
namespace {

struct CtdaFunction {
  std::uint16_t index;
  std::uint8_t flags;
};

constexpr CtdaFunction k_ctda_functions[] = {
#include "fo4/schema/ctda_functions.inc"
};

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

constexpr std::uint8_t k_param1_formid = 1;
constexpr std::uint8_t k_param2_formid = 2;
constexpr std::uint8_t k_ctda_use_global = 0x04;
constexpr std::size_t k_ctda_type = 0;
constexpr std::size_t k_ctda_compare = 4;
constexpr std::size_t k_ctda_function = 8;
constexpr std::size_t k_ctda_param1 = 12;
constexpr std::size_t k_ctda_param2 = 16;
constexpr std::size_t k_ctda_run_on = 20;
constexpr std::size_t k_ctda_reference = 24;
constexpr std::uint32_t k_run_on_reference = 2;

constexpr auto k_navm = Signature::from_string("NAVM");
constexpr auto k_mnam = Signature::from_string("MNAM");
constexpr auto k_qust = Signature::from_string("QUST");
constexpr auto k_race = Signature::from_string("RACE");
constexpr auto k_info = Signature::from_string("INFO");
constexpr auto k_edid = Signature::from_string("EDID");
constexpr auto k_obts = Signature::from_string("OBTS");
constexpr auto k_sraf = Signature::from_string("SRAF");
constexpr auto k_alco = Signature::from_string("ALCO");
constexpr auto k_alfr = Signature::from_string("ALFR");
constexpr auto k_alla = Signature::from_string("ALLA");
constexpr auto k_anam = Signature::from_string("ANAM");
constexpr auto k_cnam = Signature::from_string("CNAM");
constexpr auto k_dnam = Signature::from_string("DNAM");
constexpr auto k_fnam = Signature::from_string("FNAM");
constexpr auto k_nam2 = Signature::from_string("NAM2");
constexpr auto k_nnam = Signature::from_string("NNAM");
constexpr auto k_snam = Signature::from_string("SNAM");
constexpr auto k_npc_ = Signature::from_string("NPC_");
constexpr auto k_fact = Signature::from_string("FACT");
constexpr auto k_tpic = Signature::from_string("TPIC");
constexpr auto k_pnam = Signature::from_string("PNAM");
constexpr auto k_gnam = Signature::from_string("GNAM");
constexpr auto k_trda = Signature::from_string("TRDA");
constexpr auto k_lnam = Signature::from_string("LNAM");
constexpr auto k_tsce = Signature::from_string("TSCE");
constexpr auto k_onam = Signature::from_string("ONAM");
constexpr auto k_modq = Signature::from_string("MODQ");
constexpr auto k_enam = Signature::from_string("ENAM");
constexpr auto k_iovr = Signature::from_string("IOVR");
constexpr auto k_nam0 = Signature::from_string("NAM0");
constexpr auto k_nam1 = Signature::from_string("NAM1");
constexpr auto k_nam3 = Signature::from_string("NAM3");
constexpr auto k_nam4 = Signature::from_string("NAM4");
constexpr auto k_nam9 = Signature::from_string("NAM9");
constexpr auto k_tnam = Signature::from_string("TNAM");
constexpr auto k_inam = Signature::from_string("INAM");
constexpr auto k_rnam = Signature::from_string("RNAM");
constexpr auto k_intv = Signature::from_string("INTV");
constexpr auto k_alfa = Signature::from_string("ALFA");
constexpr auto k_gree = Signature::from_string("GREE");
constexpr auto k_tiqs = Signature::from_string("TIQS");
constexpr auto k_incc = Signature::from_string("INCC");
constexpr auto k_wzmd = Signature::from_string("WZMD");
constexpr auto k_cis1 = Signature::from_string("CIS1");
constexpr auto k_cis2 = Signature::from_string("CIS2");
constexpr auto k_wthr = Signature::from_string("WTHR");
constexpr auto k_imsp = Signature::from_string("IMSP");
constexpr auto k_wgdr = Signature::from_string("WGDR");
constexpr auto k_unam = Signature::from_string("UNAM");
constexpr auto k_vnam = Signature::from_string("VNAM");
constexpr auto k_wnam = Signature::from_string("WNAM");
constexpr auto k_dalc = Signature::from_string("DALC");
constexpr auto k_jnam = Signature::from_string("JNAM");
constexpr auto k_qnam = Signature::from_string("QNAM");
constexpr auto k_data = Signature::from_string("DATA");
constexpr auto k_clmt = Signature::from_string("CLMT");
constexpr auto k_wlst = Signature::from_string("WLST");
constexpr auto k_ipds = Signature::from_string("IPDS");
constexpr auto k_dobj = Signature::from_string("DOBJ");

std::uint16_t read_u16(std::span<const std::byte> data, std::size_t offset) {
  std::uint16_t value = 0;
  std::memcpy(&value, data.data() + offset, 2);
  return value;
}

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

const CtdaFunction* find_ctda_function(std::uint16_t index) {
  const auto begin = std::begin(k_ctda_functions);
  const auto end = std::end(k_ctda_functions);
  const auto it = std::lower_bound(begin, end, index,
                                   [](const CtdaFunction& row, std::uint16_t value) {
                                     return row.index < value;
                                   });
  if (it == end || it->index != index) {
    return nullptr;
  }
  return &*it;
}

bool is_listed_formid_signature(Signature signature) {
  const auto packed = signature.packed();
  const auto begin = std::begin(k_formid_subrecords);
  const auto end = std::end(k_formid_subrecords);
  return std::find(begin, end, packed) != end;
}

bool is_formid_array_signature(Signature signature) {
  return signature == sig_kwda || signature == Signature::from_string("SPLO") ||
         signature == sig_xlkr;
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

bool qust_non_formid_signature(Signature signature) {
  // xEdit FO4 QUST reuses several signatures that are FormIDs in other record
  // types. Within QUST they are scalar/string data and must never be sent
  // through the global signature-only FormID table:
  //   DNAM General quest data
  //   NAM2 stage note
  //   CNAM stage log-entry text
  //   FNAM objective/alias flags
  //   ANAM next alias ID
  //   NNAM objective text / quest description
  //   SNAM SWF filename
  return signature == k_dnam || signature == k_nam2 || signature == k_cnam ||
         signature == k_fnam || signature == k_anam || signature == k_nnam ||
         signature == k_snam;
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
                        const Plugin* type_source,
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
    if (type_source == nullptr || context == nullptr) {
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
        const auto* owner = find_record(*type_source, owner_key);
        if (owner == nullptr) {
          if (strict_remap) {
            throw FormIdError("cannot safely remap COED union: owner type unknown");
          }
        } else if (owner->header.signature == k_npc_) {
          remap_global = true;
        } else if (owner->header.signature != k_fact) {
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

void visit_link_offsets(const Record& record, const Subrecord& sub,
                        bool strict_remap,
                        const Plugin* type_source,
                        const PluginFormIdContext* context,
                        auto&& fn) {
  if (sub.signature == sig_xxxx) {
    return;
  }
  if (record.header.signature == k_info) {
    if (sub.signature != sig_vmad && sub.signature != sig_ctda &&
        !info_known_non_formid_signature(sub.signature) &&
        !info_known_link_signature(sub.signature) && sub.signature != k_trda) {
      visit_info_link_offsets(sub, strict_remap, fn);
      return;
    }
    if (sub.signature == k_trda || info_known_link_signature(sub.signature) ||
        info_known_non_formid_signature(sub.signature)) {
      visit_info_link_offsets(sub, strict_remap, fn);
      return;
    }
  }
  if (record.header.signature == k_ipds && sub.signature == k_pnam) {
    if (sub.data.size() != 8) {
      if (strict_remap) {
        throw FormIdError("cannot safely remap malformed IPDS PNAM payload");
      }
      return;
    }
    fn(0);
    fn(4);
    return;
  }
  if (record.header.signature == k_dobj && sub.signature == k_dnam) {
    if (sub.data.size() % 8 != 0 ||
        std::count_if(record.subrecords.begin(), record.subrecords.end(),
                      [](const Subrecord& item) { return item.signature == k_dnam; }) != 1) {
      if (strict_remap) {
        throw FormIdError("cannot safely remap malformed DOBJ DNAM payload or cardinality");
      }
      return;
    }
    for (std::size_t offset = 4; offset < sub.data.size(); offset += 8) {
      fn(offset);
    }
    return;
  }
  if (const auto mixed = mixed_formid_offsets(record, sub, strict_remap)) {
    for (const auto offset : *mixed) fn(offset);
    return;
  }
  if (sub.data.empty()) {
    return;
  }
  // xEdit FO4 uses SRAF in two incompatible ways: INFO response Camera Path
  // is a FormID, while RACE subgraph SRAF is {u16 role, u16 perspective} and
  // terminates one subgraph. Never interpret the RACE flags as a FormID.
  if (record.header.signature == k_race && sub.signature == k_sraf) {
    return;
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
      return;
    }
    if (sub.signature == k_alla) {
      if (sub.data.size() % 8 != 0) {
        if (strict_remap) {
          throw FormIdError("cannot safely remap malformed QUST ALLA payload");
        }
        return;
      }
      for (std::size_t offset = 0; offset < sub.data.size(); offset += 8) {
        fn(offset);
      }
      return;
    }
    if (qust_non_formid_signature(sub.signature)) {
      return;
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
        return;
      }
      fn(0);
      return;
    }
    if (sig == k_imsp) {
      if (size != 16 && size != 32) {
        fail_malformed("cannot safely remap malformed WTHR IMSP payload");
        return;
      }
      for (std::size_t offset = 0; offset < size; offset += 4) fn(offset);
      return;
    }
    if (sig == k_wgdr) {
      if (size != 32) {
        fail_malformed("cannot safely remap malformed WTHR WGDR payload");
        return;
      }
      for (std::size_t offset = 0; offset < size; offset += 4) fn(offset);
      return;
    }
    if (sig == k_snam) {
      if (size != 8) {
        fail_malformed("cannot safely remap malformed WTHR SNAM payload");
        return;
      }
      fn(0);
      return;
    }
    if (sig == k_unam) {
      if (size != 12 && size != 24) {
        fail_malformed("cannot safely remap malformed WTHR UNAM payload");
        return;
      }
      fn(0);
      fn(8);
      return;
    }
    if (sig == k_vnam || sig == k_wnam || sig == k_lnam || sig == k_nam1 ||
        sig == k_pnam || sig == k_jnam || sig == k_nam0 || sig == k_nam4 ||
        sig == k_fnam || sig == k_rnam || sig == k_qnam || sig == k_dalc ||
        sig == k_data || sig == k_edid) {
      return;
    }
    if (is_listed_formid_signature(sig) && size == 4) {
      fail_malformed(
          "cannot safely remap unknown WTHR FormID-listed subrecord");
      return;
    }
    return;
  }
  if (record.header.signature == k_clmt && sub.signature == k_wlst) {
    if (sub.data.size() % 12 != 0) {
      if (strict_remap) {
        throw FormIdError("cannot safely remap malformed CLMT WLST payload");
      }
      return;
    }
    for (std::size_t offset = 0; offset < sub.data.size(); offset += 12) {
      fn(offset);
      fn(offset + 8);
    }
    return;
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
    return;
  }
  if (sub.signature == k_obts) {
    const auto offsets = obts_formid_offsets(sub);
    if (!offsets.has_value()) {
      if (strict_remap) {
        throw FormIdError("cannot safely remap malformed OBTS template payload");
      }
      return;
    }
    for (const auto offset : *offsets) fn(offset);
    return;
  }
  if (is_navmesh_mixed_payload(record, sub)) {
    const auto offsets = navmesh_mixed_formid_offsets(record, sub);
    if (!offsets.has_value()) {
      if (strict_remap) {
        throw FormIdError(
            "cannot safely remap malformed NAVM/NAVI mixed FormID payload " +
            sub.signature.to_string());
      }
      return;
    }
    for (const auto offset : *offsets) fn(offset);
    return;
  }
  if (record.header.signature == sig_tes4 && sub.signature == sig_onam &&
      sub.data.size() % 4 == 0) {
    for (std::size_t i = 0; i < sub.data.size(); i += 4) {
      fn(i);
    }
    return;
  }
  if (navm_vertex_index_signature(record, sub)) {
    return;
  }
  if (record_specific_formid_array(record, sub) && sub.data.size() % 4 == 0) {
    for (std::size_t i = 0; i < sub.data.size(); i += 4) {
      fn(i);
    }
    return;
  }
  if (perk_epfd_is_formid(record, sub)) {
    fn(0);
    return;
  }
  if (is_formid_array_signature(sub.signature) && sub.data.size() % 4 == 0) {
    for (std::size_t i = 0; i < sub.data.size(); i += 4) {
      fn(i);
    }
    return;
  }
  if (is_listed_formid_signature(sub.signature) && sub.data.size() == 4 &&
      !schema::corpus_non_formid_slot(record, sub)) {
    fn(0);
    return;
  }
  if (visit_tagged_formid_slot(sub, fn)) {
    return;
  }
  if (sub.signature == sig_coed) {
    visit_coed_formids(sub, strict_remap, type_source, context, fn);
    return;
  }
  if (visit_structured_slots(sub.signature, sub.data.size(), fn)) {
    return;
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
    return;
  }
  if (sub.signature != sig_ctda || sub.data.size() < 16) {
    return;
  }
  if ((std::to_integer<std::uint8_t>(sub.data[k_ctda_type]) & k_ctda_use_global) != 0) {
    fn(k_ctda_compare);
  }
  const auto function = read_u16(sub.data, k_ctda_function);
  const auto* desc = find_ctda_function(function);
  if (desc != nullptr) {
    if ((desc->flags & k_param1_formid) != 0) {
      fn(k_ctda_param1);
    }
    if ((desc->flags & k_param2_formid) != 0 && sub.data.size() >= 20) {
      fn(k_ctda_param2);
    }
  }
  if (sub.data.size() >= 28 && read_u32(sub.data, k_ctda_run_on) == k_run_on_reference) {
    fn(k_ctda_reference);
  }
}

void remap_raw(std::span<std::byte> data, std::size_t offset,
               const PluginFormIdContext& old_context,
               const PluginFormIdContext& new_context) {
  if (offset + 4 > data.size()) {
    return;
  }
  const auto raw = RawFormId{read_u32(data, offset)};
  if (raw.is_null()) {
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
                             const Plugin* type_source) {
  if (record.header.signature == k_info &&
      same_form_id_context(old_context, new_context)) {
    return;
  }
  for (auto& sub : record.subrecords) {
    auto bytes = std::span<std::byte>(sub.data);
    visit_link_offsets(record, sub, true, type_source, &old_context,
                       [&](std::size_t offset) {
      remap_raw(bytes, offset, old_context, new_context);
    });
  }
}

void remap_form_links(Plugin& plugin, const PluginFormIdContext& old_context,
                      const PluginFormIdContext& new_context) {
  visit_records(plugin, [&](Record& record) {
    remap_record_form_links(record, old_context, new_context, &plugin);
  });
}

void rekey_form_links(Plugin& plugin,
                      const FormKey& old_key,
                      const FormKey& new_key) {
  auto next = plugin;
  const auto context = plugin_form_id_context(next);
  bool any_changed = false;

  visit_records(next, [&](Record& record) {
    bool changed = false;
    for (auto& sub : record.subrecords) {
      auto bytes = std::span<std::byte>(sub.data);
      visit_link_offsets(record, sub, true, &plugin, &context, [&](std::size_t offset) {
        if (offset + 4 > bytes.size()) return;
        const auto raw = RawFormId{read_u32(bytes, offset)};
        if (raw.is_null()) return;
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

FormLinkCollection collect_form_links_detailed(const Plugin& plugin) {
  FormLinkCollection result;
  auto& hits = result.links;
  bool current_record_incomplete = false;
  const auto context = plugin_form_id_context(plugin);
  visit_records(plugin, [&](const Record& record) {
    current_record_incomplete = false;
    std::map<std::uint32_t, std::size_t> ordinals;
    for (const auto& sub : record.subrecords) {
      if (sub.signature == sig_vmad &&
          !vmad_coverage(sub.data, record.header.signature).complete()) {
        current_record_incomplete = true;
      }
      const auto ordinal = ordinals[sub.signature.packed()]++;
      visit_link_offsets(record, sub, false, &plugin, &context, [&](std::size_t offset) {
        if (offset + 4 > sub.data.size()) {
          return;
        }
        FormLinkHit hit;
        hit.record = record.form_key;
        hit.subrecord = sub.signature;
        hit.subrecord_ordinal = ordinal;
        hit.offset = offset;
        hit.raw = RawFormId{read_u32(sub.data, offset)};
        if (!hit.raw.is_null()) {
          try {
            hit.key = resolve_plugin_link(hit.raw, context);
          } catch (const FormIdError&) {
          }
        }
        hits.push_back(std::move(hit));
      });
    }
    if (current_record_incomplete) {
      ++result.records_with_incomplete_vmad;
    }
  });
  return result;
}

}  // namespace fo4
