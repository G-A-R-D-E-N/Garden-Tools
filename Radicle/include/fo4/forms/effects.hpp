#pragma once

#include "fo4/plugin/container.hpp"
#include "fo4/schema/ctda.hpp"

#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace fo4::forms {

class MagicEffectError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

struct MagicEffectEntry {
  FormKey effect{};
  float magnitude{0.0f};
  std::uint32_t area{0};
  std::uint32_t duration{0};
  std::vector<CtdaCondition> conditions;
};

enum class EffectListWrite {
  written,
  record_missing,
  unsupported_record,
  unexpected_layout,
};

std::vector<MagicEffectEntry> read_magic_effects(const Plugin& plugin, const Record& record);

EffectListWrite set_magic_effects(Plugin& plugin,
                                  const FormKey& key,
                                  std::span<const MagicEffectEntry> entries);

}  // namespace fo4::forms
