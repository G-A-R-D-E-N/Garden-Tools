#pragma once

#include "fo4/plugin/container.hpp"
#include "fo4/signature.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace fo4::schema {

struct LocalizedFieldEntry {
  Signature record;
  Signature subrecord;
};

#include "fo4/schema/generated_lstring_fields.inc"

// Whether this (record, subrecord) pair is a localized string field.
//
// Payload size cannot answer this: NPC_ CNAM is four bytes in all 3,015
// shipped instances but is a Class FormID, while QUST CNAM in a stage is a
// log-entry lstring. The set is declared in schema/fallout4/fields.json.
bool is_localized_field(Signature record, Signature subrecord) noexcept;

// A string field as it exists on disk.
//
// In a localized plugin the subrecord holds a four-byte string ID into the
// .STRINGS/.DLSTRINGS/.ILSTRINGS tables; otherwise it holds inline text.
// Decoding keeps whichever form the file used, so a write-back reproduces the
// original bytes and no language choice is baked into the SDK. Resolve an ID
// through fo4::localization when display text is wanted.
//
// A localized payload that is not four bytes has no ID and no text; its bytes
// are kept in raw_id and written back unchanged. Build a new value to change
// the ID; writing returns raw_id before id.
struct LocalizedString {
  std::optional<std::uint32_t> id;
  std::optional<std::string> text;
  std::optional<std::vector<std::byte>> raw_id;

  bool localized() const noexcept { return id.has_value(); }
  // Inline text when present; empty for an unresolved string ID.
  const std::string& inline_text() const noexcept;
};

// Reads a string subrecord according to the plugin's localized flag and the
// schema. Throws only if inline text is not a single NUL-terminated string.
LocalizedString read_localized_string(const Plugin& plugin,
                                      Signature record,
                                      Signature subrecord,
                                      const Subrecord& payload);

// Encodes back into the same form the value carries.
std::vector<std::byte> write_localized_string(const LocalizedString& value);

}  // namespace fo4::schema
