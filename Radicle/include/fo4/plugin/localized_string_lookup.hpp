#pragma once

#include "fo4/signature.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace fo4 {

class LocalizedStringLookup {
public:
  virtual ~LocalizedStringLookup() = default;

  virtual std::optional<std::string_view> lookup_field(std::size_t plugin,
                                                       Signature record,
                                                       Signature subrecord,
                                                       std::uint32_t id) const noexcept = 0;
};

}
