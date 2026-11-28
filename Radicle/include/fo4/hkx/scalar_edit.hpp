#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::hkx {

// Invariant: values are the C ABI's radicle_hkx_scalar_info.kind and are never renumbered, new kinds are appended.
enum class HkxScalarKind : std::uint8_t {
  Bool = 0,
  Int8,
  Uint8,
  Int16,
  Uint16,
  Int32,
  Uint32,
  Real,
};

enum class HkxScalarStatus : std::uint8_t {
  Ok = 0,
  BadPackfile,
  NoObject,
  NoLayout,
  NoMember,
  NotScalar,
  BadValue,
  OutOfRange,
  VerifyFailed,
};

// Invariant: this is the one spelling of a status, so a consumer's refusal text and the log cannot disagree.
const char* hkx_scalar_status_name(HkxScalarStatus status);

// Invariant: offset is relative to the object's start, as in HkxObjectField; value is plain text (true, false, an integer, or a real printed with %.9g) that set_scalar_member accepts back; enum_name is empty unless the member has an enum or flags table that names the value.
struct HkxScalarMember {
  std::string name;
  std::string type;
  std::string value;
  std::string enum_name;
  HkxScalarKind kind{HkxScalarKind::Bool};
  bool is_enum{false};
  bool is_flags{false};
  std::uint32_t offset{0};
  std::uint32_t size{0};
};

// Requires a packfile read_packfile accepts and an index into HkxPackfile::objects (the behaviour node index is HkxGraphNode::object). Constraint: lists the written members of fixed-size scalar type (bool, int8 to uint32, real, enum or flags of those) that no fixup covers, in offset order with inherited members first; a bool whose byte is not 0 or 1 is left out. `why` is set whenever the status is not Ok.
HkxScalarStatus list_scalar_members(std::span<const std::byte> source, std::size_t object_index,
                                    std::vector<HkxScalarMember>& out, std::string& why);

// Requires the grammar of list_scalar_members' value: bool takes true, false, 1 or 0; an integer or flags member a decimal integer; an enum a name of its table or a decimal integer that the table names or that is already stored (any in-range integer when the table is empty); a real finite decimal text.
// Constraint: a real that is NaN or infinite is listed but never accepted, so a file that stores one cannot be written back unchanged through this call; every other member's own listed value writes back byte-identical. The packfile is patched in place, so out has the source's size and differs from it only inside the member's bytes (checked before out is assigned); out is empty on any refusal. The result is not validated as a behaviour graph and has not been loaded by the game.
HkxScalarStatus set_scalar_member(std::span<const std::byte> source, std::size_t object_index,
                                  std::string_view member, std::string_view value, std::vector<std::byte>& out,
                                  std::string& why);

}  // namespace fo4::hkx
