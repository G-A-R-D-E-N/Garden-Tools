#include "fo4/plugin/sound_writer.hpp"

#include "record_edit.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/schema/scalar.hpp"

#include <cstdint>
#include <cstring>
#include <optional>
#include <string_view>
#include <vector>

namespace fo4 {
namespace {

using detail::locate_mutable_record;
using detail::mark_record_modified;

constexpr Signature k_sndr = Signature::from_string("SNDR");
constexpr Signature k_cnam = Signature::from_string("CNAM");
constexpr Signature k_bnam = Signature::from_string("BNAM");
constexpr Signature k_anam = Signature::from_string("ANAM");
constexpr std::uint32_t k_auto_weapon = 0xED157AE3u;

bool after_file_names(Signature signature) {
  constexpr Signature tags[] = {Signature::from_string("ONAM"), Signature::from_string("CTDA"),
                                Signature::from_string("LNAM"), Signature::from_string("BNAM"),
                                Signature::from_string("DNAM"), Signature::from_string("ITMC"),
                                Signature::from_string("ITMS"), Signature::from_string("INTV"),
                                Signature::from_string("FNAM"), Signature::from_string("ITME")};
  for (const auto tag : tags) {
    if (signature == tag) return true;
  }
  return false;
}

bool after_values(Signature signature) {
  constexpr Signature tags[] = {Signature::from_string("DNAM"), Signature::from_string("ITMC"),
                                Signature::from_string("ITMS"), Signature::from_string("INTV"),
                                Signature::from_string("FNAM"), Signature::from_string("ITME")};
  for (const auto tag : tags) {
    if (signature == tag) return true;
  }
  return false;
}

std::optional<std::size_t> unique_subrecord(const Record& record, Signature signature) {
  std::optional<std::size_t> found;
  for (std::size_t at = 0; at < record.subrecords.size(); ++at) {
    if (record.subrecords[at].signature != signature) continue;
    if (found) return std::nullopt;
    found = at;
  }
  return found;
}

std::optional<std::uint32_t> subrecord_u32(const Record& record, std::size_t at) {
  const auto& data = record.subrecords[at].data;
  if (data.size() != 4) return std::nullopt;
  std::uint32_t value = 0;
  std::memcpy(&value, data.data(), sizeof(value));
  return value;
}

std::size_t file_name_position(const Record& record) {
  std::size_t after_last_file = record.subrecords.size();
  for (std::size_t at = 0; at < record.subrecords.size(); ++at) {
    if (record.subrecords[at].signature == k_anam) after_last_file = at + 1;
  }
  if (after_last_file != record.subrecords.size()) return after_last_file;
  for (std::size_t at = 0; at < record.subrecords.size(); ++at) {
    if (after_file_names(record.subrecords[at].signature)) return at;
  }
  return record.subrecords.size();
}

std::size_t base_link_position(const Record& record) {
  for (std::size_t at = 0; at < record.subrecords.size(); ++at) {
    if (after_values(record.subrecords[at].signature)) return at;
  }
  return record.subrecords.size();
}

std::vector<std::byte> string_payload(std::string_view text) {
  std::vector<std::byte> out;
  out.reserve(text.size() + 1);
  for (const char ch : text) out.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  out.push_back(std::byte{0});
  return out;
}

bool discriminator_is_base(const Record& record) {
  const auto cnam = unique_subrecord(record, k_cnam);
  if (!cnam) return false;
  const auto kind = subrecord_u32(record, *cnam);
  return kind.has_value() && *kind == k_auto_weapon;
}

}  // namespace

SoundDescriptorMutationStatus add_sound_descriptor_file(Plugin& plugin,
                                                        const FormKey& descriptor,
                                                        std::string_view file) {
  auto* record = locate_mutable_record(plugin, descriptor);
  if (record == nullptr) return SoundDescriptorMutationStatus::record_missing;
  if (record->header.signature != k_sndr) return SoundDescriptorMutationStatus::record_mismatch;
  if (file.find('\0') != std::string_view::npos) {
    throw schema::ScalarError("SNDR file name cannot contain an embedded NUL");
  }
  const auto position = file_name_position(*record);
  record->subrecords.insert(record->subrecords.begin() + static_cast<std::ptrdiff_t>(position),
                            Subrecord{k_anam, string_payload(file), false});
  mark_record_modified(plugin, *record);
  return SoundDescriptorMutationStatus::updated;
}

SoundDescriptorMutationStatus remove_sound_descriptor_file(Plugin& plugin,
                                                           const FormKey& descriptor,
                                                           std::size_t index) {
  auto* record = locate_mutable_record(plugin, descriptor);
  if (record == nullptr) return SoundDescriptorMutationStatus::record_missing;
  if (record->header.signature != k_sndr) return SoundDescriptorMutationStatus::record_mismatch;
  std::vector<std::size_t> positions;
  for (std::size_t at = 0; at < record->subrecords.size(); ++at) {
    if (record->subrecords[at].signature == k_anam) positions.push_back(at);
  }
  if (positions.empty()) return SoundDescriptorMutationStatus::subrecord_missing;
  if (index >= positions.size()) return SoundDescriptorMutationStatus::index_out_of_range;
  record->subrecords.erase(record->subrecords.begin() +
                           static_cast<std::ptrdiff_t>(positions[index]));
  mark_record_modified(plugin, *record);
  return SoundDescriptorMutationStatus::updated;
}

SoundDescriptorMutationStatus add_sound_descriptor_base(Plugin& plugin,
                                                        const FormKey& descriptor,
                                                        const FormKey& target) {
  auto* record = locate_mutable_record(plugin, descriptor);
  if (record == nullptr) return SoundDescriptorMutationStatus::record_missing;
  if (record->header.signature != k_sndr) return SoundDescriptorMutationStatus::record_mismatch;
  if (!discriminator_is_base(*record)) return SoundDescriptorMutationStatus::discriminator_unproven;
  std::size_t bnam_count = 0;
  for (const auto& sub : record->subrecords) {
    if (sub.signature == k_bnam) ++bnam_count;
  }
  if (bnam_count != 0) return SoundDescriptorMutationStatus::already_present;
  if (target.owner().empty()) {
    throw schema::ScalarError("SNDR Base Descriptor target is required");
  }
  if (target.owner() == plugin.key) {
    const auto* local = find_record(plugin, target);
    if (local == nullptr || local->header.signature != k_sndr) {
      throw schema::FormLinkFieldError("SNDR Base Descriptor target type is not allowed");
    }
  }
  const auto encoded = encode_plugin_form_id(target, plugin_form_id_context(plugin));
  if (!encoded.has_value()) throw schema::FormLinkFieldError("SNDR Base Descriptor target is not encodable");
  std::vector<std::byte> payload(4, std::byte{0});
  const auto raw = encoded->value();
  for (std::size_t byte = 0; byte < 4; ++byte) {
    payload[byte] = std::byte{static_cast<std::uint8_t>((raw >> (8 * byte)) & 0xFFu)};
  }
  const auto position = base_link_position(*record);
  record->subrecords.insert(record->subrecords.begin() + static_cast<std::ptrdiff_t>(position),
                            Subrecord{k_bnam, std::move(payload), false});
  mark_record_modified(plugin, *record);
  return SoundDescriptorMutationStatus::updated;
}

SoundDescriptorMutationStatus remove_sound_descriptor_base(Plugin& plugin,
                                                           const FormKey& descriptor) {
  auto* record = locate_mutable_record(plugin, descriptor);
  if (record == nullptr) return SoundDescriptorMutationStatus::record_missing;
  if (record->header.signature != k_sndr) return SoundDescriptorMutationStatus::record_mismatch;
  if (!discriminator_is_base(*record)) return SoundDescriptorMutationStatus::discriminator_unproven;
  std::optional<std::size_t> bnam;
  for (std::size_t at = 0; at < record->subrecords.size(); ++at) {
    if (record->subrecords[at].signature != k_bnam) continue;
    if (bnam) return SoundDescriptorMutationStatus::payload_unproven;
    bnam = at;
  }
  if (!bnam) return SoundDescriptorMutationStatus::subrecord_missing;
  if (record->subrecords[*bnam].data.size() != 4) {
    return SoundDescriptorMutationStatus::payload_unproven;
  }
  record->subrecords.erase(record->subrecords.begin() + static_cast<std::ptrdiff_t>(*bnam));
  mark_record_modified(plugin, *record);
  return SoundDescriptorMutationStatus::updated;
}

}  // namespace fo4
