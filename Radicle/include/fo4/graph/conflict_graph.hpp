#pragma once

#include "fo4/formid.hpp"
#include "fo4/plugin/container.hpp"
#include "fo4/signature.hpp"

#include <cstddef>
#include <map>
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
  const std::vector<ReferencedByHit>& referenced_by(const FormKey& key) const noexcept;
  const std::vector<OutgoingReferenceHit>& references_from(const FormKey& key) const noexcept;
  std::vector<FormKey> conflicts() const;
  std::vector<FormKey> all_keys() const;

private:
  std::map<FormKey, std::vector<RecordVersion>> records_;
  std::map<FormKey, std::vector<ReferencedByHit>> inbound_;
  std::map<FormKey, std::vector<OutgoingReferenceHit>> outbound_;
};

}  // namespace fo4
