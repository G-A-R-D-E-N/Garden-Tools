#include "non_formid_slots.hpp"

#include <algorithm>
#include <iterator>
#include <utility>

namespace fo4::schema {
namespace {

constexpr std::pair<Signature, Signature> k_non_formid_slots[] = {
#include "fo4/schema/non_formid_slots.inc"
#include "fo4/schema/non_formid_slots_manual.inc"
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

constexpr Signature k_sig_scen = Signature::from_string("SCEN");
constexpr Signature k_sig_tnam = Signature::from_string("TNAM");
constexpr Signature k_sig_htid = Signature::from_string("HTID");
constexpr int k_scene_action_dialogue = 0;
constexpr int k_scene_action_radio = 6;

int scene_action_type(const Record& record, const Subrecord& subrecord) noexcept {
  int action = -1;
  for (const auto& sub : record.subrecords) {
    if (&sub == &subrecord) break;
    if (sub.signature != k_sig_anam) continue;
    action = sub.data.size() == 2 ? static_cast<int>(sub.data[0]) | static_cast<int>(sub.data[1]) << 8 : -1;
  }
  return action;
}

// source:wbDefinitionsFO4.pas SCEN Actions; the action type must come from the open 2-byte ANAM.
bool scene_action_scalar(const Record& record, const Subrecord& subrecord) noexcept {
  if (record.header.signature != k_sig_scen ||
      (subrecord.signature != k_sig_tnam && subrecord.signature != k_sig_htid)) {
    return false;
  }
  const auto action = scene_action_type(record, subrecord);
  if (action < 0) return false;
  return subrecord.signature == k_sig_tnam || action != k_scene_action_radio;
}

}  // namespace

bool corpus_non_formid_slot(const Record& record, const Subrecord& subrecord) noexcept {
  if (pack_data_input_value(record, subrecord) || scene_action_scalar(record, subrecord)) return true;
  return std::find(std::begin(k_non_formid_slots), std::end(k_non_formid_slots),
                   std::pair{record.header.signature, subrecord.signature}) !=
         std::end(k_non_formid_slots);
}

bool scene_action_topic_data(const Record& record, const Subrecord& subrecord) noexcept {
  if (record.header.signature != k_sig_scen || subrecord.signature != sig_data) return false;
  const auto action = scene_action_type(record, subrecord);
  return action == k_scene_action_dialogue || action == k_scene_action_radio;
}

}  // namespace fo4::schema
