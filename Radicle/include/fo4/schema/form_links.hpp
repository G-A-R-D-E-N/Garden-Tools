#pragma once

#include "fo4/formid.hpp"
#include "fo4/plugin/container.hpp"
#include "fo4/signature.hpp"

#include <cstddef>
#include <functional>
#include <optional>
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

using FormLinkTypeLookup = std::function<std::optional<Signature>(const FormKey&)>;

FormLinkTypeLookup plugin_type_lookup(const Plugin& plugin);

void remap_record_form_links(Record& record,
                             const PluginFormIdContext& old_context,
                             const PluginFormIdContext& new_context,
                             const FormLinkTypeLookup& type_lookup = {});

void remap_form_links(Plugin& plugin, const PluginFormIdContext& old_context,
                      const PluginFormIdContext& new_context,
                      FormLinkTypeLookup type_lookup = {});

// Replaces every schema-proven in-plugin reference to old_key with new_key.
// The operation is transactional and uses strict link parsing: unsupported
// VMAD or malformed mixed NAVM/NAVI payloads throw rather than risking a
// partial rekey with hidden stale references.
void rekey_form_links(Plugin& plugin,
                      const FormKey& old_key,
                      const FormKey& new_key);

struct UnclaimedLinkPayload {
  FormKey record;
  Signature subrecord{};
  std::size_t subrecord_ordinal{0};
  std::size_t size{0};
};

struct FormLinkCollection {
  std::vector<FormLinkHit> links;
  std::size_t records_with_incomplete_vmad{0};
  std::vector<UnclaimedLinkPayload> unclaimed_link_payloads;
};

std::vector<FormLinkHit> collect_form_links(const Plugin& plugin);
FormLinkCollection collect_form_links_detailed(const Plugin& plugin);

}  // namespace fo4
