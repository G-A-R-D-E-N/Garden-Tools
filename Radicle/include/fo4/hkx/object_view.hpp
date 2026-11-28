#pragma once

#include "fo4/hkx/decode.hpp"
#include "fo4/hkx/layout.hpp"
#include "fo4/hkx/packfile.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace fo4::hkx {

// Invariant: one field shows at most this many bytes, so a 4 KiB object cannot flood a reply.
inline constexpr std::uint32_t kObjectHexLimit = 16;

// Invariant: hex is always filled; value is empty when the member is not decoded, and reason then says why; a struct or array whose parts were only partly decoded keeps the decoded parts in value (an undecoded part prints as ?) and reason names the first part that was not.
struct HkxObjectField {
  std::string name;
  std::uint32_t offset{0};
  std::uint32_t size{0};
  std::string hex;
  std::string type;
  std::string value;
  std::string reason;
};

// Constraint: target_object is -1 whenever the destination is not the start of an object this reader indexes.
struct HkxObjectReference {
  std::uint32_t source{0};
  std::string member;
  std::uint32_t target_section{0};
  std::uint32_t target_offset{0};
  std::int32_t target_object{-1};
  std::string target_class;
};

// Invariant: an object whose class has no generated layout still reports its offset, signature and bytes.
struct HkxObjectDetail {
  std::size_t index{0};
  std::uint32_t offset{0};
  std::uint32_t signature{0};
  std::string class_name;
  std::uint32_t layout_size{0};
  std::size_t size{0};
  bool layout_known{false};
  std::vector<HkxObjectField> fields;
  std::vector<HkxObjectReference> references;
};

struct HkxObjectQuery {
  // Requires a prefix of the class name; an empty prefix keeps every class.
  std::string class_prefix;
  bool known_layout_only{false};
  // Requires an index into HkxPackfile::objects; when set, only that object is reported.
  std::optional<std::size_t> object_index;
  std::size_t limit{64};
  // Invariant: false leaves every field's value and reason empty, for callers that need only member labels and references.
  bool decode_values{true};
};

struct HkxObjectReport {
  std::size_t object_count{0};
  std::size_t matched{0};
  std::vector<std::string> classes;
  std::vector<HkxObjectDetail> objects;
  std::vector<std::string> diagnostics;
};

HkxStatus inspect_objects(const HkxPackfile& packfile, const HkxObjectQuery& query, HkxObjectReport& out);

// Requires the packfile to outlive the decoder and to hold a data section; decodes only the member types whose width the layout tables or commonlibf4-nomad state (structs through their class size), and leaves every other member to its hex.
class HkxFieldDecoder {
 public:
  explicit HkxFieldDecoder(const HkxPackfile& packfile);

  // Requires a data-section object; lists inherited members first and skips none, so a member that is not decoded still has its hex.
  std::vector<HkxObjectField> fields(const HkxObjectView& object, const HkxClassLayout& layout) const;

  // Invariant: the same rows as fields() with value and reason left empty; needs no packfile, so a caller that wants only labels pays no decode.
  static std::vector<HkxObjectField> labels(const HkxObjectView& object, const HkxClassLayout& layout);

  // Invariant: at most three non-empty string members (the node name, the clip animation), each capped, as `name="Idle" animationName="walk.hkx"`; empty when there is none.
  std::string summary(const HkxObjectView& object, const HkxClassLayout& layout) const;

 private:
  const HkxPackfile& packfile_;
  const HkxSection& data_;
  std::unordered_map<std::uint32_t, HkxRemoteFixup> global_;
};

}  // namespace fo4::hkx
