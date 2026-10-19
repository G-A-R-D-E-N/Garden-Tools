#include "fo4/schema/behavior_layout.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <string_view>

namespace fo4 {
namespace {

Signature sig(std::string_view text) { return Signature::from_string(text); }
std::int32_t type(const Subrecord& sub) {
  std::int32_t value = 0;
  std::memcpy(&value, sub.data.data(), sizeof(value));
  return value;
}
bool listed(std::string_view value, std::initializer_list<std::string_view> names) {
  return std::find(names.begin(), names.end(), value) != names.end();
}

std::vector<std::size_t> before_marker(const std::vector<Subrecord>& subs, Signature marker,
                                       Signature subrecord) {
  std::size_t boundary = subs.size();
  for (std::size_t i = 0; i < subs.size(); ++i) {
    if (subs[i].signature == marker) {
      boundary = i;
      break;
    }
  }
  std::vector<std::size_t> out;
  for (std::size_t i = 0; i < boundary; ++i) {
    if (subs[i].signature == subrecord) out.push_back(i);
  }
  return out;
}

std::vector<std::size_t> after_marker(const std::vector<Subrecord>& subs, Signature marker,
                                      Signature subrecord) {
  std::size_t start = subs.size();
  for (std::size_t i = 0; i < subs.size(); ++i) {
    if (subs[i].signature == marker) {
      start = i + 1;
      break;
    }
  }
  std::vector<std::size_t> out;
  for (std::size_t i = start; i < subs.size(); ++i) {
    if (subs[i].signature == subrecord) out.push_back(i);
  }
  return out;
}

}

std::optional<std::vector<std::size_t>> behavior_field_candidates(const Record& record, Signature subrecord,
                                                                 bool repeated) {
  if (record.header.signature == sig("LENS") && subrecord == sig("DNAM")) {
    // Constraint: DNAM is the sprite group's required non-repeatable leader, so every DNAM after LFSP heads one run.
    if (repeated) return after_marker(record.subrecords, sig("LFSP"), subrecord);
    return before_marker(record.subrecords, sig("LFSP"), subrecord);
  }
  if (record.header.signature == sig("SCCO") && subrecord == sig("XNAM")) {
    return before_marker(record.subrecords, sig("VNAM"), subrecord);
  }
  if (record.header.signature == sig("PERK") && subrecord == sig("DATA")) {
    for (std::size_t i = 0; i < record.subrecords.size(); ++i) {
      if (record.subrecords[i].signature == sig("PRKE")) break;
      if (record.subrecords[i].signature == subrecord) return std::vector<std::size_t>{i};
    }
    return std::vector<std::size_t>{};
  }
  if (record.header.signature != sig("PACK")) return std::nullopt;
  const auto& subs = record.subrecords;
  std::vector<std::size_t> out;
  if (subrecord == sig("CNAM") || subrecord == sig("QNAM")) {
    std::size_t boundary = subs.size();
    bool counter_seen = false;
    for (std::size_t i = 0; i < subs.size(); ++i) {
      if (subs[i].signature == sig("ANAM")) boundary = std::min(boundary, i);
      if (subs[i].signature != sig("PKCU")) continue;
      if (counter_seen || subs[i].data.size() != 12) return out;
      counter_seen = true;
      boundary = std::min(boundary, i);
    }
    for (std::size_t i = 0; i < boundary; ++i) {
      if (subs[i].signature == subrecord) out.push_back(i);
    }
    return out;
  }
  if (subrecord != sig("INAM")) return std::nullopt;
  std::array<std::size_t, 3> markers{subs.size(), subs.size(), subs.size()};
  const std::array tags{sig("POBA"), sig("POEA"), sig("POCA")};
  for (std::size_t i = 0; i < subs.size(); ++i) {
    for (std::size_t m = 0; m < tags.size(); ++m) {
      if (subs[i].signature != tags[m]) continue;
      if (markers[m] != subs.size() || !subs[i].data.empty()) return out;
      markers[m] = i;
    }
  }
  if (!(markers[0] < markers[1] && markers[1] < markers[2] && markers[2] < subs.size())) return out;
  for (std::size_t m = 0; m < markers.size(); ++m) {
    const auto end = m + 1 < markers.size() ? markers[m + 1] : subs.size();
    std::optional<std::size_t> idle;
    for (std::size_t i = markers[m] + 1; i < end; ++i) {
      if (subs[i].signature != subrecord) continue;
      if (idle || subs[i].data.size() != 4) return std::vector<std::size_t>{};
      idle = i;
    }
    if (!idle) return std::vector<std::size_t>{};
    out.push_back(*idle);
  }
  return out;
}

bool behavior_field_payload_valid(const Record& record, const Subrecord& sub) {
  const auto tag = sub.signature.to_string();
  const auto size = sub.data.size();
  if (record.header.signature == sig("PACK")) {
    if (tag == "PLDT") return (size == 8 || size == 12 || size == 16) && type(sub) >= 0 && type(sub) <= 14;
    if (tag == "PTDA") return size == 12 && type(sub) >= 0 && type(sub) <= 8;
    if (listed(tag, {"PKDT", "PSDT", "PKCU"})) return size == 12;
    if (listed(tag, {"CNAM", "QNAM", "INAM"})) return size == 4;
  }
  if (record.header.signature == sig("CSTY")) {
    if (tag == "CSME") return size == 36 || size == 40;
    if (tag == "CSLR") return size == 12 || size == 16 || size == 20;
  }
  if (record.header.signature == sig("IDLE") && tag == "ANAM") return size == 8;
  return true;
}

std::optional<std::vector<std::size_t>> behavior_formid_offsets(const Record& record,
                                                              const Subrecord& sub,
                                                              bool strict_remap) {
  const auto tag = sub.signature.to_string();
  const auto record_type = record.header.signature.to_string();
  const auto checked = [&](std::vector<std::size_t> offsets, bool valid) {
    if (!valid) {
      if (strict_remap) throw FormIdError("cannot safely remap malformed or unknown " + record_type + " " + tag);
      offsets.clear();
    }
    return offsets;
  };
  if (record_type == "IDLE" && tag == "ANAM") return checked({0, 4}, sub.data.size() == 8);
  if (record_type != "PACK") return std::nullopt;
  if (tag == "IDLA") {
    std::vector<std::size_t> offsets;
    for (std::size_t offset = 0; offset < sub.data.size(); offset += 4) offsets.push_back(offset);
    return checked(offsets, sub.data.size() % 4 == 0);
  }
  if (tag == "CNAM" || tag == "QNAM" || tag == "PKCU") {
    std::size_t counters = 0;
    for (const auto& item : record.subrecords) {
      if (item.signature == sig("PKCU") && (++counters > 1 || item.data.size() != 12)) {
        return checked({}, false);
      }
    }
  }
  if (tag == "PLDT" || tag == "PTDA") {
    if (!behavior_field_payload_valid(record, sub)) return checked({}, false);
    const auto kind = type(sub);
    std::uint32_t target = 0;
    std::memcpy(&target, sub.data.data() + 4, sizeof(target));
    const bool unknown = (tag == "PLDT" ? kind == 12 || kind == 13 : kind == 6 || kind == 8) && target != 0;
    if (unknown) return checked({}, false);
    const bool link = tag == "PLDT" ? kind == 0 || kind == 1 || kind == 4 || kind == 6
                                     : kind == 0 || kind == 1 || kind == 3 || kind == 7;
    return link ? std::vector<std::size_t>{4} : std::vector<std::size_t>{};
  }
  if (tag == "PKCU") return checked({4}, sub.data.size() == 12);
  if (tag == "CNAM" || tag == "QNAM" || tag == "INAM") {
    const auto candidates = *behavior_field_candidates(record, sub.signature);
    const auto index = static_cast<std::size_t>(&sub - record.subrecords.data());
    const bool owned = std::find(candidates.begin(), candidates.end(), index) != candidates.end();
    if (!owned) {
      if (tag == "INAM") return checked({}, false);
      return std::vector<std::size_t>{};
    }
    return checked({0}, sub.data.size() == 4);
  }
  if (listed(tag, {"PKDT", "PSDT", "ANAM", "BNAM", "PNAM", "FNAM", "PRCB", "PKC2", "PFO2", "PFOR",
                   "UNAM", "XNAM", "POBA", "POEA", "POCA", "IDLF", "IDLC", "IDLT", "IDLB"})) {
    return std::vector<std::size_t>{};
  }
  return std::nullopt;
}

}
