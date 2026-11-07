#include "fo4/plugin/writer.hpp"

#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/text/encoding.hpp"

#include <algorithm>
#include <cstdint>
#include <format>
#include <stdexcept>
#include <string>

namespace fo4 {
namespace {

constexpr Signature sig_ofst = Signature::from_string("OFST");
constexpr Signature sig_dele = Signature::from_string("DELE");

std::string stored_tes4_text(std::string_view utf8, std::string_view field) {
  if (utf8.find('\0') != std::string_view::npos) {
    throw std::invalid_argument(std::string(field) + " contains an embedded NUL");
  }
  const auto stored = text::utf8_to_windows1252(utf8);
  if (!stored) {
    throw std::invalid_argument(std::string(field) +
                                " is not valid UTF-8 text representable in Windows-1252");
  }
  return *stored;
}

void clear_tes4_original_bytes(Plugin& plugin) {
  plugin.original_range.clear();
  plugin.tes4.original_range.clear();
}

Subrecord zstring_subrecord(Signature signature, std::string_view stored) {
  Subrecord sub;
  sub.signature = signature;
  sub.data.reserve(stored.size() + 1);
  for (const char ch : stored) {
    sub.data.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  }
  sub.data.push_back(std::byte{0});
  return sub;
}

bool precedes_tes4_text(const Subrecord& sub, Signature signature) {
  if (sub.signature == sig_hedr || sub.signature == sig_ofst || sub.signature == sig_dele) {
    return true;
  }
  return signature == sig_snam && sub.signature == sig_cnam;
}

void set_tes4_zstring(Record& tes4, Signature signature, std::string_view stored) {
  auto& subrecords = tes4.subrecords;
  const auto existing = std::find_if(subrecords.begin(), subrecords.end(),
                                     [&](const Subrecord& sub) { return sub.signature == signature; });
  if (existing != subrecords.end()) {
    existing->data = zstring_subrecord(signature, stored).data;
    return;
  }
  const auto position = std::find_if_not(subrecords.begin(), subrecords.end(), [&](const Subrecord& sub) {
    return precedes_tes4_text(sub, signature);
  });
  subrecords.insert(position, zstring_subrecord(signature, stored));
}

}  // namespace

Plugin set_plugin_light_flag(Plugin plugin, bool light) {
  assign_record_form_keys(plugin);
  if (light) {
    const auto lowest = allows_hardcoded_range(plugin) ? k_first_light_local_id : k_first_full_local_id;
    for (const auto* record : collect_records(plugin)) {
      if (record->header.signature == sig_tes4 || record->form_key.owner() != plugin.key) {
        continue;
      }
      const auto local = record->form_key.local().value();
      if (local < lowest || local > k_light_local_mask) {
        throw FormIdError(std::format(
            "plugin cannot be flagged light while owned FormIDs fall outside 0x{:03X}-0xFFF",
            lowest));
      }
    }
    if (const auto lowered = lowered_light_next_form_id(plugin)) {
      set_hedr_next_form_id(plugin, *lowered);
    }
  }

  const auto current = plugin.tes4.header.flags.value();
  const auto next = light
      ? (current | RecordFlags::small_file)
      : (current & ~RecordFlags::small_file);

  plugin.tes4.header.flags = RecordFlags{next};
  plugin.header.tes4_flags = plugin.tes4.header.flags;

  plugin.original_range.clear();
  plugin.tes4.original_range.clear();
  return plugin;
}

Plugin set_plugin_master_flag(Plugin plugin, bool master) {
  const auto current = plugin.tes4.header.flags.value();
  const auto next = master
      ? (current | RecordFlags::master)
      : (current & ~RecordFlags::master);

  plugin.tes4.header.flags = RecordFlags{next};
  plugin.header.tes4_flags = plugin.tes4.header.flags;
  clear_tes4_original_bytes(plugin);
  return plugin;
}

Plugin set_plugin_localized_flag(Plugin plugin, bool localized) {
  const auto current = plugin.tes4.header.flags.value();
  const auto next = localized
      ? (current | RecordFlags::localized)
      : (current & ~RecordFlags::localized);

  plugin.tes4.header.flags = RecordFlags{next};
  plugin.header.tes4_flags = plugin.tes4.header.flags;
  clear_tes4_original_bytes(plugin);
  return plugin;
}

Plugin set_plugin_author(Plugin plugin, std::string_view author) {
  const auto stored = stored_tes4_text(author, "author");
  set_tes4_zstring(plugin.tes4, sig_cnam, stored);
  plugin.header.author = stored;
  clear_tes4_original_bytes(plugin);
  return plugin;
}

Plugin set_plugin_description(Plugin plugin, std::string_view description) {
  const auto stored = stored_tes4_text(description, "description");
  if (stored.empty()) {
    std::erase_if(plugin.tes4.subrecords, [](const Subrecord& sub) { return sub.signature == sig_snam; });
  } else {
    set_tes4_zstring(plugin.tes4, sig_snam, stored);
  }
  plugin.header.description = stored;
  clear_tes4_original_bytes(plugin);
  return plugin;
}

}  // namespace fo4
