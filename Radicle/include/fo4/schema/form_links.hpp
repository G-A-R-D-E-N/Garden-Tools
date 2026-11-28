#pragma once

#include "fo4/formid.hpp"
#include "fo4/plugin_core/container.hpp"
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

bool remap_record_form_links(Record& record,
                             const PluginFormIdContext& old_context,
                             const PluginFormIdContext& new_context,
                             const FormLinkTypeLookup& type_lookup = {});

// Constraint: a record that lives in a plugin must be remapped through this overload, or the writer returns the plugin's cached bytes.
bool remap_record_form_links(Plugin& owner, Record& record,
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

// Constraint: rekey_form_links without the plugin copy; a throw can leave plugin half rewritten, so call it only on a transactional copy.
void rekey_form_links_in_place(Plugin& plugin,
                               const FormKey& old_key,
                               const FormKey& new_key);

// Constraint: same strict parsing as rekey_form_links, limited to one record; returns how many links changed.
std::size_t rekey_record_form_links(Plugin& plugin,
                                    const FormKey& record_key,
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
  std::vector<FormKey> unsupported_vmad_records;
  std::vector<UnclaimedLinkPayload> unclaimed_link_payloads;
};

std::vector<FormLinkHit> collect_form_links(const Plugin& plugin);
FormLinkCollection collect_form_links_detailed(const Plugin& plugin);
FormLinkCollection collect_record_form_links(const Record& record,
                                             const PluginFormIdContext& context,
                                             const FormLinkTypeLookup& type_lookup);

}  // namespace fo4
