#include "fo4/plugin/scene_writer.hpp"

#include "scene_writer_internal.hpp"

#include "fo4/plugin/reader.hpp"

namespace fo4 {

std::optional<std::vector<FormKey>> read_scene_action_packages(const Plugin& plugin, const FormKey& scene,
                                                               std::size_t action) {
  const auto* record = find_record(plugin, scene);
  if (!record || record->header.signature != Signature::from_string("SCEN")) return std::nullopt;
  require_decodable(*record);
  const auto layout = detail::checked_scene_layout(*record);
  if (action >= layout.actions.size()) return std::nullopt;
  const auto& group = layout.actions[action];
  const auto& type = record->subrecords[group.open].data;
  if (type.size() != 2 || type[0] != std::byte{1} || type[1] != std::byte{0}) return std::nullopt;
  std::vector<FormKey> packages;
  bool ended = false;
  for (auto i = group.header_end; i < group.close; ++i) {
    const auto& sub = record->subrecords[i];
    if (sub.signature != Signature::from_string("PNAM")) {
      if (!packages.empty()) ended = true;
      continue;
    }
    if (ended || sub.data.size() != 4) throw schema::FormLinkFieldError("SCEN action package list is malformed");
    std::uint32_t raw = 0;
    for (unsigned j = 0; j < 4; ++j) raw |= std::to_integer<std::uint32_t>(sub.data[j]) << (j * 8);
    if (raw == 0) throw schema::FormLinkFieldError("SCEN action package link is null");
    packages.push_back(resolve_plugin_form_id(RawFormId{raw}, plugin_form_id_context(plugin)));
  }
  return packages;
}

}
