#include "fo4/forms/message_buttons.hpp"
#include "authoring_position.hpp"
#include "fo4/binary/raw.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/record_edit.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace fo4::forms {
namespace {

using fo4::binary::raw::read_u32;
using fo4::binary::raw::write_u32;
using fo4::forms::detail::authoring_position;

constexpr std::size_t k_max_buttons = 65535;
constexpr std::uint32_t k_message_box = 1;

bool is_button_member(Signature signature) {
  return signature == sig_itxt || signature == sig_ctda || signature == sig_cis1 || signature == sig_cis2;
}

}  // namespace

MessageButtonsWrite set_message_buttons(Plugin& plugin,
                                        const FormKey& record_key,
                                        std::span<const schema::LocalizedString> texts) {
  auto* record = fo4::detail::locate_mutable_record(plugin, record_key);
  if (record == nullptr) return MessageButtonsWrite::record_missing;
  if (record->header.signature != Signature::from_string("MESG")) return MessageButtonsWrite::unsupported_record;
  if (texts.size() > k_max_buttons) return MessageButtonsWrite::too_many_buttons;

  const bool localized = plugin.header.tes4_flags.is_localized();
  std::vector<Subrecord> buttons;
  buttons.reserve(texts.size());
  for (const auto& text : texts) {
    if (text.localized() != localized) return MessageButtonsWrite::localization_mismatch;
    if (!localized && (!text.text.has_value() || text.text->empty() ||
                       text.text->find('\0') != std::string::npos)) {
      return MessageButtonsWrite::invalid_text;
    }
    buttons.push_back(Subrecord{sig_itxt, schema::write_localized_string(text), false});
  }

  const auto& source = record->subrecords;
  const auto first_button = std::find_if(source.begin(), source.end(),
                                         [](const Subrecord& sub) { return sub.signature == sig_itxt; });
  if (!std::all_of(first_button, source.end(), [](const Subrecord& sub) { return is_button_member(sub.signature); })) {
    return MessageButtonsWrite::unexpected_layout;
  }

  std::optional<std::size_t> dnam;
  for (std::size_t index = 0; index < source.size(); ++index) {
    if (source[index].signature != sig_dnam) continue;
    if (dnam.has_value() || source[index].data.size() != 4) return MessageButtonsWrite::unexpected_layout;
    dnam = index;
  }

  Record edited = *record;
  auto& subrecords = edited.subrecords;
  subrecords.erase(subrecords.begin() + (first_button - source.begin()), subrecords.end());

  if (dnam.has_value()) {
    write_u32(subrecords[*dnam].data, 0, read_u32(subrecords[*dnam].data, 0) | k_message_box);
  } else {
    const auto position = authoring_position(edited, sig_dnam);
    if (!position.has_value()) return MessageButtonsWrite::no_insertion_point;
    std::vector<std::byte> payload(4, std::byte{0});
    write_u32(payload, 0, k_message_box);
    subrecords.insert(subrecords.begin() + static_cast<std::ptrdiff_t>(*position),
                      Subrecord{sig_dnam, std::move(payload), false});
  }
  for (auto& button : buttons) subrecords.push_back(std::move(button));

  *record = std::move(edited);
  fo4::detail::mark_record_modified(plugin, *record);
  return MessageButtonsWrite::written;
}

}  // namespace fo4::forms
