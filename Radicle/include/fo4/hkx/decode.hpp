#pragma once

#include "fo4/hkx/convert.hpp"
#include "fo4/hkx/packfile.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::hkx {

enum class HkxMemberKind : std::uint8_t {
  None,
  Bool,
  Int8,
  Int16,
  Int32,
  Int64,
  Uint8,
  Uint16,
  Uint32,
  Uint64,
  Real,
  Vector4,
  Quaternion,
  QsTransform,
  Transform,
  StringPtr,
  Pointer,
  Array,
  Struct
};

// Invariant: element is None unless kind is Array; struct_class names the class of a Struct or of an Array of Struct.
struct HkxMemberType {
  std::string_view name;
  std::uint32_t offset;
  HkxMemberKind kind;
  HkxMemberKind element;
  std::string_view struct_class;
};

// Invariant: source is never empty; members lists parent members too, in offset order, and only members a file stores.
struct HkxDecodeClass {
  std::string_view name;
  std::uint32_t signature;
  std::uint32_t size;
  std::span<const HkxMemberType> members;
  std::string_view source;
};

std::span<const HkxDecodeClass> decode_classes();
const HkxDecodeClass* find_decode_class(std::string_view class_name);

// Invariant: zero for None and Struct, whose size is the class size.
std::uint32_t kind_size(HkxMemberKind kind);

// Requires a Struct member to name a class in the decode table; nullopt otherwise.
std::optional<std::uint32_t> member_size(const HkxMemberType& member);

struct HkxDecodeOptions {
  // Constraint: an unlisted class stays opaque and untouched instead of refusing the file.
  bool opaque_unlisted{false};
};

struct HkxBlock {
  std::uint64_t object{0};
  std::string class_name;
  std::string member;
  std::string reason;
};

enum class HkxDecodeStatus : std::uint8_t { Ok, Unreadable, Refused };

// Invariant: document is empty unless the status is Ok; block names the object, class and member that refused.
struct HkxDecodeResult {
  HkxDecodeStatus status{HkxDecodeStatus::Ok};
  HkxStatus packfile_status{HkxStatus::Ok};
  HkxSemanticDocument document;
  std::vector<std::string> opaque_classes;
  HkxBlock block;
};

// Requires a little-endian 8-byte-pointer packfile; object ids are data-section offsets.
HkxDecodeResult decode_document(std::span<const std::byte> bytes, const HkxDecodeOptions& options = {});

// Invariant: bytes is empty unless the status is Ok; every byte not owned by a changed scalar equals the source.
struct HkxEmitResult {
  HkxDecodeStatus status{HkxDecodeStatus::Ok};
  HkxStatus packfile_status{HkxStatus::Ok};
  std::vector<std::byte> bytes;
  HkxBlock block;
};

// Constraint: only scalar leaves change; array length, string text, reference targets, the object set and type names must equal the source.
HkxEmitResult emit_document(std::span<const std::byte> source, const HkxSemanticDocument& edited,
                            const HkxDecodeOptions& options = {});

}  // namespace fo4::hkx
