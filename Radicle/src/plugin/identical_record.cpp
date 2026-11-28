#include "identical_record.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/schema/form_link_mixed.hpp"
#include "fo4/schema/form_links.hpp"

#include <exception>
#include <string_view>
#include <vector>

namespace fo4::identical {
namespace {

struct ItmIgnoredSubrecord {
  std::string_view record;
  std::string_view subrecord;
};

constexpr ItmIgnoredSubrecord k_itm_ignored_subrecords[] = {
    {"TACT", "PNAM"}, {"TACT", "FNAM"}, {"CSTY", "CSMD"}, {"DIAL", "TIFC"}, {"PROJ", "DATA"}, {"PERK", "PRKF"},
    {"MESG", "INAM"}, {"MATO", "DNAM"}, {"REFR", "XMBP"}, {"WRLD", "NNAM"},
};

bool is_itm_ignored(const Record& record, const Subrecord& subrecord) {
  const auto record_signature = record.header.signature.view();
  const auto subrecord_signature = subrecord.signature.view();
  for (const auto& ignored : k_itm_ignored_subrecords) {
    if (ignored.record == record_signature && ignored.subrecord == subrecord_signature) return true;
  }
  return false;
}

std::vector<const Subrecord*> comparable_subrecords(const Record& record, bool exact) {
  std::vector<const Subrecord*> out;
  for (const auto& subrecord : record.subrecords) {
    if (exact || !is_itm_ignored(record, subrecord)) out.push_back(&subrecord);
  }
  return out;
}

}

bool same_record(const Record& left, const Record& right, bool exact) {
  if (left.header.signature != right.header.signature || left.header.flags != right.header.flags) return false;
  const auto left_subrecords = comparable_subrecords(left, exact);
  const auto right_subrecords = comparable_subrecords(right, exact);
  if (left_subrecords.size() != right_subrecords.size()) return false;
  for (std::size_t i = 0; i < left_subrecords.size(); ++i) {
    if (left_subrecords[i]->signature != right_subrecords[i]->signature ||
        left_subrecords[i]->data != right_subrecords[i]->data) {
      return false;
    }
  }
  return true;
}

std::optional<Record> remapped_into(const Record& record,
                                    const Plugin& source,
                                    const PluginFormIdContext& target) {
  auto normalized = record;
  try {
    remap_record_form_links(normalized, plugin_form_id_context(source), target,
                            plugin_type_lookup(source));
  } catch (const std::exception&) {
    return std::nullopt;
  }
  return normalized;
}

}
