#include "fo4/previs/stamp.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/signature.hpp"

#include <cstring>
#include <span>
#include <string>

namespace fo4::previs {
namespace {

constexpr auto k_cell = Signature::from_string("CELL");
constexpr auto k_xcri = Signature::from_string("XCRI");
constexpr auto k_rvis = Signature::from_string("RVIS");

bool set_whole_subrecord(Plugin& plugin,
                         const FormKey& record_key,
                         Signature signature,
                         std::span<const std::byte> data) {
  bool found = false;
  bool changed = false;
  visit_records(plugin, [&](Record& record) {
    if (found || record.header.signature != k_cell || record.form_key != record_key) return;
    found = true;
    require_decodable(record);
    for (auto& sub : record.subrecords) {
      if (sub.signature != signature) continue;
      const std::span<const std::byte> next{sub.data};
      if (next.size() == data.size() &&
          (data.empty() || std::memcmp(next.data(), data.data(), data.size()) == 0)) {
        return;
      }
      sub.data.assign(data.begin(), data.end());
      record.original_range.clear();
      plugin.original_range.clear();
      changed = true;
      return;
    }
    Subrecord sub;
    sub.signature = signature;
    sub.data.assign(data.begin(), data.end());
    record.subrecords.push_back(std::move(sub));
    record.original_range.clear();
    plugin.original_range.clear();
    changed = true;
  });
  return found;
}

std::vector<std::byte> encode_u16_stamp(std::uint16_t value) {
  return {static_cast<std::byte>(value & 0xFFu),
          static_cast<std::byte>((value >> 8) & 0xFFu)};
}

std::vector<std::byte> encode_rvis(std::uint16_t local_cell) {
  std::vector<std::byte> out(2);
  out[0] = static_cast<std::byte>(local_cell & 0xFFu);
  out[1] = static_cast<std::byte>((local_cell >> 8) & 0xFFu);
  return out;
}

}  // namespace

StampStatus stamp_cell_previs(Plugin& plugin, const FormKey& cell_key, const StampRequest& req) {
  assign_record_form_keys(plugin);
  const auto k_pcmb = Signature::from_string("PCMB");
  const auto k_visi = Signature::from_string("VISI");
  if (!set_whole_subrecord(plugin, cell_key, k_pcmb, encode_u16_stamp(req.pcmb_stamp))) {
    return StampStatus::record_missing;
  }
  if (!set_whole_subrecord(plugin, cell_key, k_visi, encode_u16_stamp(req.visi_stamp))) {
    return StampStatus::record_missing;
  }

  const auto xcri = write_xcri_bytes(req.xcri);
  if (!set_whole_subrecord(plugin, cell_key, k_xcri, xcri)) return StampStatus::record_missing;

  if (req.rvis_local_cell) {
    const auto rvis = encode_rvis(*req.rvis_local_cell);
    if (!set_whole_subrecord(plugin, cell_key, k_rvis, rvis)) return StampStatus::subrecord_error;
  }

  return StampStatus::ok;
}

}  // namespace fo4::previs
