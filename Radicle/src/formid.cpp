#include "fo4/formid.hpp"

#include <charconv>
#include <iomanip>
#include <sstream>

namespace fo4 {
namespace {

std::uint32_t parse_hex(std::string_view text) {
  std::uint32_t value = 0;
  const auto* begin = text.data();
  const auto* end = text.data() + text.size();
  const auto [ptr, ec] = std::from_chars(begin, end, value, 16);
  if (ec != std::errc{} || ptr != end) {
    throw FormIdError("invalid hex FormID: " + std::string{text});
  }
  return value;
}

const MasterDescriptor* find_master(const std::vector<MasterDescriptor>& masters,
                                    const PluginKey& key) {
  for (const auto& master : masters) {
    if (master.key == key) {
      return &master;
    }
  }
  return nullptr;
}

}  // namespace

std::string FormKey::to_string() const {
  if (owner_.empty() && local_.value() == 0) {
    return {};
  }
  std::ostringstream out;
  out << std::uppercase << std::hex << std::setw(6) << std::setfill('0')
      << (local_.value() & 0x00FFFFFFu) << ':' << owner_.filename();
  return out.str();
}

FormKey FormKey::parse(std::string_view text) {
  const auto colon = text.find(':');
  if (colon == std::string_view::npos || colon == 0 || colon + 1 >= text.size()) {
    throw FormIdError("FormKey must look like 00009A:Plugin.esp");
  }
  auto hex = text.substr(0, colon);
  if (hex.size() >= 2 && hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) {
    hex.remove_prefix(2);
  }
  const auto local = LocalFormId{parse_hex(hex)};
  const auto owner = PluginKey::from_filename(text.substr(colon + 1));
  return FormKey{owner, local};
}

bool operator<(const FormKey& a, const FormKey& b) noexcept {
  if (a.owner() < b.owner()) return true;
  if (b.owner() < a.owner()) return false;
  return a.local() < b.local();
}

FormKey resolve_plugin_form_id(RawFormId raw, const PluginFormIdContext& context) {
  if (raw.is_null()) {
    return FormKey{};
  }
  if (raw.is_created()) {
    throw FormIdError("created/save FormIDs (index 0xFF) are not plugin identities");
  }

  if (raw.is_runtime_light()) {
    if (context.current_kind != PluginKind::light) {
      throw FormIdError(
          "on-disk FormID uses FE encoding but the current plugin is not light");
    }
    return FormKey{context.current_plugin, LocalFormId{raw.light_local()}};
  }

  const auto index = raw.index_byte();
  const auto local = LocalFormId{raw.local_24()};
  if (index < context.masters.size()) {
    return FormKey{context.masters[index].key, local};
  }
  if (index == context.masters.size()) {
    return FormKey{context.current_plugin, local};
  }
  throw FormIdError("master index " + std::to_string(index) +
                    " is outside this plugin's master table");
}

RawFormId encode_plugin_form_id(const FormKey& key, const PluginFormIdContext& dest) {
  if (key.owner().empty() && key.local().value() == 0) {
    return RawFormId{0};
  }

  const auto mask_for = [](PluginKind kind) {
    return kind == PluginKind::light ? k_light_local_mask : k_full_local_mask;
  };

  if (key.owner() == dest.current_plugin) {
    const auto index = static_cast<std::uint32_t>(dest.masters.size());
    if (index > 0xFEu) {
      throw FormIdError("too many masters to encode a local FormID");
    }
    return RawFormId{(index << 24) | (key.local().value() & mask_for(dest.current_kind))};
  }

  for (std::size_t i = 0; i < dest.masters.size(); ++i) {
    if (dest.masters[i].key == key.owner()) {
      const auto local = key.local().value() & mask_for(dest.masters[i].kind);
      return RawFormId{(static_cast<std::uint32_t>(i) << 24) | local};
    }
  }

  throw FormIdError("owner " + std::string{key.owner().filename()} +
                    " is not in the destination master table");
}

FormKey resolve_runtime_form_id(RawFormId raw, const RuntimeFormIdContext& context) {
  if (raw.is_null()) {
    return FormKey{};
  }
  if (raw.is_created()) {
    throw FormIdError("created/save FormIDs (index 0xFF) are not plugin identities");
  }

  if (raw.is_runtime_light()) {
    const auto slot = raw.light_slot();
    if (slot >= context.light_plugins.size()) {
      throw FormIdError("light slot 0x" + std::to_string(slot) +
                        " is outside the runtime light table");
    }
    return FormKey{context.light_plugins[slot].key, LocalFormId{raw.light_local()}};
  }

  const auto index = raw.index_byte();
  if (index >= context.full_plugins.size()) {
    throw FormIdError("compile index " + std::to_string(index) +
                      " is outside the runtime full-plugin table");
  }
  const auto* full = find_master(context.full_plugins, context.full_plugins[index].key);
  (void)full;
  return FormKey{context.full_plugins[index].key, LocalFormId{raw.local_24()}};
}

}  // namespace fo4
