#pragma once

#include "fo4/schema/field_address.hpp"
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
  i8,
  i16,
  zstring,
};

enum class ScalarPresentation {
  identity,
  percent_0_100,
  enumeration,
  flags,
};

// Invariant: value is the stored integer for an enumeration row and the single-bit mask (1 << bit) for a flags row.
struct ScalarName {
  std::int64_t value{0};
  std::string_view name;
};

struct ScalarDescriptor {
  Signature subrecord{};
  std::string_view field_name;
  ScalarStorage storage{ScalarStorage::u32};
  ScalarPresentation presentation{ScalarPresentation::identity};
  double minimum{0.0};
  double maximum{0.0};
  // Byte offset of the field within the subrecord payload. Meaningful only
  // when `embedded` is set.
  std::size_t offset{0};
  // False: the field *is* the whole payload, which must be exactly the storage
  // width -- the original shape, and still what REFR XHLT/XSCL are.
  // True: the field lives at `offset` inside a larger struct payload, as every
  // PROJ DNAM / EXPL DATA / HAZD DNAM field does. Such a payload carries many
  // fields, and a short payload simply does not expose the ones past its end
  // (EXPL DATA ships truncated at 52/56/60/64 bytes as well as 84).
  bool embedded{false};
  std::size_t payload_size{0};
  FieldAddress address{};
  std::size_t minimum_payload_size{0};
  std::span<const ScalarName> names{};
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

// Resolves only a signature that exactly one whole-payload row names; else use record plus path.
std::optional<ScalarDescriptor> scalar_descriptor(Signature subrecord) noexcept;
std::optional<ScalarFieldDescriptor> scalar_field_descriptor(
    std::string_view path) noexcept;
std::optional<ScalarFieldDescriptor> scalar_field_descriptor(
    Signature record, std::string_view path) noexcept;
std::span<const ScalarFieldDescriptor> scalar_fields(Signature record) noexcept;
std::size_t scalar_storage_size(ScalarStorage storage) noexcept;

// The bytes of one field within a subrecord payload. Empty when an embedded
// field lies past the end of a legitimately shorter payload -- absent, not
// malformed. Throws for a whole-payload field of the wrong width.
std::span<const std::byte> scalar_field_bytes(std::span<const std::byte> payload,
                                              const ScalarDescriptor& descriptor);

FieldShape scalar_field_shape(const ScalarDescriptor& descriptor) noexcept;

std::span<const std::byte> scalar_slot_bytes(std::span<const std::byte> payload,
                                             const FieldSlot& slot,
                                             const ScalarDescriptor& descriptor);

// Writes an encoded field back into a payload at the descriptor's offset,
// leaving every other byte untouched. A whole-payload field replaces it.
void patch_scalar_field(std::span<std::byte> payload,
                        const ScalarDescriptor& descriptor,
                        std::span<const std::byte> encoded);

std::optional<std::string_view> scalar_value_name(const ScalarDescriptor& descriptor,
                                                  std::int64_t value) noexcept;
std::optional<std::int64_t> scalar_name_value(const ScalarDescriptor& descriptor,
                                              std::string_view name) noexcept;

std::string decode_scalar(std::span<const std::byte> data,
                          const ScalarDescriptor& descriptor);
std::vector<std::byte> encode_scalar(std::string_view text,
                                     const ScalarDescriptor& descriptor);

}  // namespace fo4::schema
