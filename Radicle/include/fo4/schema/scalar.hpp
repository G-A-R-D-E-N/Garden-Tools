#pragma once

#include "fo4/signature.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::schema {

enum class ScalarStorage {
  u8,
  u16,
  u32,
  i32,
  f32,
};

enum class ScalarPresentation {
  identity,
  percent_0_100,
};

struct ScalarDescriptor {
  Signature subrecord{};
  std::string_view field_name;
  ScalarStorage storage{ScalarStorage::u32};
  ScalarPresentation presentation{ScalarPresentation::identity};
  double minimum{0.0};
  double maximum{0.0};
};

struct ScalarFieldDescriptor {
  Signature record{};
  std::string_view path;
  ScalarDescriptor scalar;
  bool can_insert{false};
};

class ScalarError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

std::optional<ScalarDescriptor> scalar_descriptor(Signature subrecord) noexcept;
std::optional<ScalarFieldDescriptor> scalar_field_descriptor(
    Signature record, std::string_view path) noexcept;
std::span<const ScalarFieldDescriptor> scalar_fields(Signature record) noexcept;
std::size_t scalar_storage_size(ScalarStorage storage) noexcept;

std::string decode_scalar(std::span<const std::byte> data,
                          const ScalarDescriptor& descriptor);
std::vector<std::byte> encode_scalar(std::string_view text,
                                     const ScalarDescriptor& descriptor);

}  // namespace fo4::schema
