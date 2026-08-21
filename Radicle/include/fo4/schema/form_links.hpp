#pragma once

#include "fo4/formid.hpp"
#include "fo4/plugin/container.hpp"
#include "fo4/signature.hpp"

#include <cstddef>
#include <vector>

namespace fo4 {

struct FormLinkHit {
  FormKey record;
  Signature subrecord{};
  std::size_t subrecord_ordinal{0};
  std::size_t offset{0};
  RawFormId raw{};
  FormKey key;
};

void remap_record_form_links(Record& record,
                             const PluginFormIdContext& old_context,
                             const PluginFormIdContext& new_context);

void remap_form_links(Plugin& plugin, const PluginFormIdContext& old_context,
                      const PluginFormIdContext& new_context);

std::vector<FormLinkHit> collect_form_links(const Plugin& plugin);

}  // namespace fo4
