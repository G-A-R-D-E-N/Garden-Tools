#include "non_formid_slots.hpp"

#include <algorithm>
#include <iterator>
#include <utility>

namespace fo4::schema {
namespace {

constexpr std::pair<Signature, Signature> k_non_formid_slots[] = {
#include "fo4/schema/non_formid_slots.inc"
};

constexpr Signature k_sig_pack = Signature::from_string("PACK");
constexpr Signature k_sig_cnam = Signature::from_string("CNAM");
constexpr Signature k_sig_anam = Signature::from_string("ANAM");

// source:wbDefinitionsFO4.pas PACK Data Input Values; a CNAM after ANAM must stay scalar.
bool pack_data_input_value(const Record& record, const Subrecord& subrecord) noexcept {
  if (record.header.signature != k_sig_pack || subrecord.signature != k_sig_cnam) return false;
  const auto* first = record.subrecords.data();
  const auto index = static_cast<std::size_t>(&subrecord - first);
  return index > 0 && index < record.subrecords.size() &&
         record.subrecords[index - 1].signature == k_sig_anam;
}

}  // namespace

bool corpus_non_formid_slot(const Record& record, const Subrecord& subrecord) noexcept {
  if (pack_data_input_value(record, subrecord)) return true;
  return std::find(std::begin(k_non_formid_slots), std::end(k_non_formid_slots),
                   std::pair{record.header.signature, subrecord.signature}) !=
         std::end(k_non_formid_slots);
}

}  // namespace fo4::schema
