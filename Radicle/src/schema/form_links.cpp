#include "fo4/schema/form_links.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/schema/vmad.hpp"
#include "fo4/signature.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <span>

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

void visit_link_offsets(const Record& record, const Subrecord& sub,
                        bool strict_vmad, auto&& fn) {
  if (sub.signature == sig_xxxx || sub.data.empty()) {
    return;
  }
  if (record.header.signature == sig_tes4 && sub.signature == sig_onam &&
      sub.data.size() % 4 == 0) {
    for (std::size_t i = 0; i < sub.data.size(); i += 4) {
      fn(i);
    }
    return;
  }
  if (is_formid_array_signature(sub.signature) && sub.data.size() % 4 == 0) {
    for (std::size_t i = 0; i < sub.data.size(); i += 4) {
      fn(i);
    }
    return;
  }
  if (is_listed_formid_signature(sub.signature) && sub.data.size() == 4) {
    fn(0);
    return;
  }
  if (visit_tagged_formid_slot(sub, fn)) {
    return;
  }
  if (visit_structured_slots(sub.signature, sub.data.size(), fn)) {
    return;
  }
  if (sub.signature == sig_vmad) {
    if (strict_vmad) {
      auto offsets = vmad_formid_offsets_for_remap(sub.data, record.header.signature);
      if (!offsets.has_value()) {
        throw FormIdError(
            "cannot safely remap VMAD containing unsupported Variable/Variable Array property types");
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
  const auto key = resolve_plugin_form_id(raw, old_context);
  const auto encoded = encode_plugin_form_id(key, new_context);
  if (encoded != raw) {
    write_u32(data, offset, encoded.value());
  }
}

}  // namespace

void remap_record_form_links(Record& record,
                             const PluginFormIdContext& old_context,
                             const PluginFormIdContext& new_context) {
  for (auto& sub : record.subrecords) {
    auto bytes = std::span<std::byte>(sub.data);
    visit_link_offsets(record, sub, true, [&](std::size_t offset) {
      remap_raw(bytes, offset, old_context, new_context);
    });
  }
}

void remap_form_links(Plugin& plugin, const PluginFormIdContext& old_context,
                      const PluginFormIdContext& new_context) {
  visit_records(plugin, [&](Record& record) {
    remap_record_form_links(record, old_context, new_context);
  });
}

std::vector<FormLinkHit> collect_form_links(const Plugin& plugin) {
  std::vector<FormLinkHit> hits;
  const auto context = plugin_form_id_context(plugin);
  visit_records(plugin, [&](const Record& record) {
    for (const auto& sub : record.subrecords) {
      visit_link_offsets(record, sub, false, [&](std::size_t offset) {
        if (offset + 4 > sub.data.size()) {
          return;
        }
        FormLinkHit hit;
        hit.record = record.form_key;
        hit.subrecord = sub.signature;
        hit.offset = offset;
        hit.raw = RawFormId{read_u32(sub.data, offset)};
        if (!hit.raw.is_null()) {
          try {
            hit.key = resolve_plugin_form_id(hit.raw, context);
          } catch (const FormIdError&) {
          }
        }
        hits.push_back(std::move(hit));
      });
    }
  });
  return hits;
}

}  // namespace fo4
