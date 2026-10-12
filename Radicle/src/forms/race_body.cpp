#include "fo4/forms/race.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "../plugin/record_edit.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <cstring>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace fo4::forms {
namespace {

constexpr auto k_race = Signature::from_string("RACE");
constexpr auto k_full = Signature::from_string("FULL");
constexpr auto k_desc = Signature::from_string("DESC");
constexpr auto k_vtck = Signature::from_string("VTCK");
constexpr auto k_pnam = Signature::from_string("PNAM");
constexpr auto k_unam = Signature::from_string("UNAM");
constexpr auto k_nam1 = Signature::from_string("NAM1");
constexpr auto k_mnam = Signature::from_string("MNAM");
constexpr auto k_fnam = Signature::from_string("FNAM");
constexpr auto k_indx = Signature::from_string("INDX");
constexpr auto k_modl = Signature::from_string("MODL");
constexpr auto k_modt = Signature::from_string("MODT");
constexpr auto k_modc = Signature::from_string("MODC");
constexpr auto k_mods = Signature::from_string("MODS");
constexpr auto k_modf = Signature::from_string("MODF");

bool is_model_signature(Signature signature) {
  return signature == k_modl || signature == k_modt || signature == k_modc ||
         signature == k_mods || signature == k_modf;
}

bool is_sparse_creation_signature(Signature signature) {
  return signature == sig_edid || signature == k_full || signature == k_desc ||
         signature == sig_data || signature == k_vtck || signature == k_pnam ||
         signature == k_unam;
}

void require_empty_marker(const Subrecord& subrecord, std::string_view label) {
  if (!subrecord.data.empty()) {
    throw RaceError("RACE " + std::string(label) + " marker must be empty");
  }
}

void validate_part(const RaceBodyPart& part) {
  std::set<std::uint32_t> seen;
  for (const auto& subrecord : part.model_subrecords) {
    if (!is_model_signature(subrecord.signature)) {
      throw RaceError(
          "RACE BodyData model bundle contains unsupported subrecord " +
          subrecord.signature.to_string());
    }
    if (!seen.insert(subrecord.signature.packed()).second) {
      throw RaceError(
          "RACE BodyData model bundle contains duplicate " +
          subrecord.signature.to_string() + " subrecord");
    }
  }
}

RaceBodyPart parse_part(const Record& record, std::size_t& cursor) {
  if (cursor >= record.subrecords.size() ||
      record.subrecords[cursor].signature != k_indx) {
    throw RaceError("RACE BodyData part is missing its INDX marker");
  }
  const auto& marker = record.subrecords[cursor];
  if (marker.data.size() != 4) {
    throw RaceError("RACE BodyData INDX must be exactly four bytes");
  }
  RaceBodyPart part;
  std::memcpy(&part.index, marker.data.data(), 4);
  ++cursor;

  while (cursor < record.subrecords.size() &&
         is_model_signature(record.subrecords[cursor].signature)) {
    part.model_subrecords.push_back(record.subrecords[cursor]);
    ++cursor;
  }
  validate_part(part);
  return part;
}

struct BodyRange {
  std::size_t begin{0};
  std::size_t end{0};
  RaceBodyData data;
};

std::optional<BodyRange> find_body_range(const Record& record) {
  std::optional<std::size_t> marker;
  for (std::size_t index = 0; index < record.subrecords.size(); ++index) {
    if (record.subrecords[index].signature != k_nam1) continue;
    if (marker.has_value()) {
      throw RaceError("RACE contains more than one BodyData NAM1 marker");
    }
    marker = index;
  }
  if (!marker.has_value()) return std::nullopt;

  require_empty_marker(record.subrecords[*marker], "BodyData NAM1");
  std::size_t cursor = *marker + 1;
  if (cursor >= record.subrecords.size() ||
      record.subrecords[cursor].signature != k_mnam) {
    throw RaceError("RACE BodyData is missing the male MNAM marker");
  }
  require_empty_marker(record.subrecords[cursor], "BodyData male MNAM");
  ++cursor;

  BodyRange result;
  result.begin = *marker;
  while (cursor < record.subrecords.size() &&
         record.subrecords[cursor].signature != k_fnam) {
    if (record.subrecords[cursor].signature != k_indx) {
      throw RaceError(
          "RACE male BodyData contains a subrecord outside an INDX model part: " +
          record.subrecords[cursor].signature.to_string());
    }
    result.data.male.push_back(parse_part(record, cursor));
  }

  if (cursor >= record.subrecords.size() ||
      record.subrecords[cursor].signature != k_fnam) {
    throw RaceError("RACE BodyData is missing the female FNAM marker");
  }
  require_empty_marker(record.subrecords[cursor], "BodyData female FNAM");
  ++cursor;

  while (cursor < record.subrecords.size() &&
         record.subrecords[cursor].signature == k_indx) {
    result.data.female.push_back(parse_part(record, cursor));
  }
  if (cursor < record.subrecords.size() &&
      is_model_signature(record.subrecords[cursor].signature)) {
    throw RaceError(
        "RACE female BodyData model subrecord appears without an INDX marker: " +
        record.subrecords[cursor].signature.to_string());
  }

  result.end = cursor;
  return result;
}

Subrecord index_marker(Signature signature, std::uint32_t index) {
  Subrecord subrecord;
  subrecord.signature = signature;
  subrecord.data.resize(4);
  std::memcpy(subrecord.data.data(), &index, 4);
  return subrecord;
}

Subrecord empty_marker(Signature signature) {
  Subrecord subrecord;
  subrecord.signature = signature;
  return subrecord;
}

std::vector<Subrecord> encode_body_data(std::span<const RaceBodyPart> male,
                                        std::span<const RaceBodyPart> female) {
  std::vector<Subrecord> encoded;
  encoded.push_back(empty_marker(k_nam1));
  encoded.push_back(empty_marker(k_mnam));
  for (const auto& part : male) {
    validate_part(part);
    encoded.push_back(index_marker(k_indx, part.index));
    encoded.insert(encoded.end(), part.model_subrecords.begin(), part.model_subrecords.end());
  }
  encoded.push_back(empty_marker(k_fnam));
  for (const auto& part : female) {
    validate_part(part);
    encoded.push_back(index_marker(k_indx, part.index));
    encoded.insert(encoded.end(), part.model_subrecords.begin(), part.model_subrecords.end());
  }
  return encoded;
}

using fo4::detail::find_mutable_record;

void mark_modified(Plugin& plugin, Record& record) {
  record.original_range.clear();
  record.decompressed_payload.clear();
  record.header.data_size = 0;
  record.uncompressed_size = 0;
  plugin.original_range.clear();
}

}  // namespace

RaceBodyData decode_race_body_data(const Plugin& plugin, const FormKey& key) {
  const auto* record = find_record(plugin, key);
  if (record == nullptr) throw RaceError("RACE record not found: " + key.to_string());
  if (record->header.signature != k_race) {
    throw RaceError("record is not RACE: " + key.to_string());
  }

  const auto body = find_body_range(*record);
  if (!body.has_value()) {
    throw RaceError("RACE is missing required BodyData NAM1/MNAM/FNAM structure");
  }
  return body->data;
}

bool set_race_body_data(Plugin& plugin,
                        const FormKey& key,
                        std::span<const RaceBodyPart> male,
                        std::span<const RaceBodyPart> female) {
  const auto encoded = encode_body_data(male, female);
  auto next = plugin;
  auto* record = find_mutable_record(next, key);
  if (record == nullptr) return false;
  if (record->header.signature != k_race) {
    throw RaceError("record is not RACE: " + key.to_string());
  }

  const auto existing = find_body_range(*record);
  if (existing.has_value()) {
    record->subrecords.erase(
        record->subrecords.begin() + static_cast<std::ptrdiff_t>(existing->begin),
        record->subrecords.begin() + static_cast<std::ptrdiff_t>(existing->end));
    record->subrecords.insert(
        record->subrecords.begin() + static_cast<std::ptrdiff_t>(existing->begin),
        encoded.begin(), encoded.end());
  } else {
    // The complete RACE ordering contains many optional structures before
    // BodyData. Without a prior BodyData marker, only the sparse #65 creation
    // shape has a proven insertion point: after its required VTCK/PNAM/UNAM.
    for (const auto& subrecord : record->subrecords) {
      if (!is_sparse_creation_signature(subrecord.signature)) {
        throw RaceError(
            "cannot safely insert missing RACE BodyData around unmodeled legacy subrecords");
      }
    }
    record->subrecords.insert(record->subrecords.end(), encoded.begin(), encoded.end());
  }

  mark_modified(next, *record);
  plugin = std::move(next);
  return true;
}

}  // namespace fo4::forms
