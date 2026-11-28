#pragma once

#include "fo4/formid.hpp"
#include "fo4/plugin/container.hpp"
#include "fo4/signature.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <vector>

namespace fo4 {

struct RecordVersion {
  FormKey key;
  PluginKey plugin;
  Signature signature{};
  std::size_t load_order{0};
  bool partial_form{false};
};

struct ReferencedByHit {
  FormKey source;
  PluginKey plugin;
  Signature subrecord{};
  std::size_t offset{0};
  std::size_t load_order{0};
};

struct OutgoingReferenceHit {
  FormKey target;
  PluginKey plugin;
  Signature subrecord{};
  std::size_t offset{0};
  std::size_t load_order{0};
};

class ConflictGraph {
public:
  static ConflictGraph build(std::span<const Plugin* const> load_order);

  const std::vector<RecordVersion>& versions(const FormKey& key) const noexcept;
  const RecordVersion* winner(const FormKey& key) const noexcept;
  std::vector<ReferencedByHit> referenced_by(const FormKey& key) const;
  std::vector<OutgoingReferenceHit> references_from(const FormKey& key) const;
  bool has_references(const FormKey& key) const;
  void release_references() const;
  bool references_built() const;
  std::vector<FormKey> conflicts() const;
  std::vector<FormKey> all_keys() const;

private:
  struct Link {
    std::uint32_t source{0};
    std::uint32_t target{0};
    std::uint32_t subrecord{0};
    std::uint32_t offset{0};
  };

  struct ReferenceIndex {
    std::vector<FormKey> keys;
    std::vector<Link> links;
    std::vector<std::uint32_t> plugin_begin;
    std::vector<std::uint32_t> inbound_offsets;
    std::vector<std::uint32_t> inbound_links;
    std::vector<std::uint32_t> outbound_offsets;
    std::vector<std::uint32_t> outbound_links;
  };

  static ReferenceIndex build_reference_index(std::span<const Plugin* const> plugins);
  // Constraint: the caller must hold referencesMutex_.
  void ensure_references() const;

  std::map<FormKey, std::vector<RecordVersion>> records_;
  // Constraint: plugins must outlive any reference query, because ensure_references reads them lazily.
  std::vector<const Plugin*> plugins_;
  mutable ReferenceIndex references_;
  mutable std::unique_ptr<std::mutex> referencesMutex_ = std::make_unique<std::mutex>();
  mutable bool referencesBuilt_{false};
};

}  // namespace fo4
