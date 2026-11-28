#pragma once

#include "fo4/formid.hpp"
#include "fo4/plugin/container.hpp"
#include "fo4/signature.hpp"

#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace fo4::forms {

class ComponentError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

struct ComponentEntry {
  FormKey component{};
  std::uint32_t count{1};

  friend bool operator==(const ComponentEntry&, const ComponentEntry&) = default;
};

bool record_supports_components(Signature record) noexcept;

std::vector<ComponentEntry> decode_components(const Plugin& plugin,
                                              const FormKey& key);

std::size_t set_record_components(Plugin& plugin,
                                  const FormKey& key,
                                  std::span<const ComponentEntry> entries);

}  // namespace fo4::forms
