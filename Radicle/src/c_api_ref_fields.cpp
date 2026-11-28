#include "c_api_env_internal.hpp"
#include "fo4/forms/placed_reference.hpp"
#include "fo4/plugin/writer.hpp"

#include <algorithm>
#include <initializer_list>
#include <optional>
#include <string_view>
#include <vector>

namespace {

using namespace radicle_c_detail;
using namespace radicle_c_env_detail;

// A C key with plugin == NULL is "clear"; anything else must be a valid FormKey.
bool optional_key(radicle_form_key key, std::optional<fo4::FormKey>& out) {
  out.reset();
  if (key.plugin == nullptr) return true;
  out = to_form_key(key);
  return out.has_value();
}

fo4::Plugin with_masters(const fo4::Plugin& plugin,
                         const radicle_env& env,
                         const std::vector<std::optional<fo4::FormKey>>& keys) {
  std::vector<fo4::MasterDescriptor> masters;
  for (const auto& master : plugin.header.masters) masters.push_back({master.key, master.kind});
  bool added = false;
  const auto self = env.value.find_plugin(plugin.key.filename());
  for (const auto& key : keys) {
    if (!key || key->owner().empty() || key->owner() == plugin.key) continue;
    const auto& owner = key->owner();
    if (std::any_of(masters.begin(), masters.end(), [&](const auto& m) { return m.key == owner; })) continue;
    const auto index = env.value.find_plugin(owner.filename());
    if (!index) {
      throw fo4::forms::PlacedReferenceError(std::string(owner.filename()) + " is not loaded in the environment");
    }
    if (self && *index > *self) {
      throw fo4::forms::PlacedReferenceError(std::string(owner.filename()) + " loads after " +
                                             std::string(plugin.key.filename()));
    }
    masters.push_back({env.value.plugins()[*index].key, env.value.plugins()[*index].kind});
    added = true;
  }
  if (!added) return plugin;
  std::vector<fo4::PluginKey> order;
  for (const auto& info : env.value.plugins()) order.push_back(info.key);
  return fo4::sort_plugin_masters(fo4::remap_plugin_masters(plugin, std::move(masters)), order);
}

template <class Edit>
radicle_status run(radicle_plugin* plugin, const char* what, Edit&& edit) {
  return guarded(what, [&] {
    try {
      auto next = plugin->value;
      const auto status = edit(next);
      if (status == RADICLE_STATUS_OK) plugin->value = std::move(next);
      return status;
    } catch (const fo4::forms::PlacedReferenceError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    } catch (const fo4::FormIdError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
}

radicle_status found(bool present) {
  return present ? RADICLE_STATUS_OK : fail(RADICLE_STATUS_NOT_FOUND, "plugin has no such record");
}

radicle_status present(fo4::forms::PresentFieldEdit result) {
  switch (result) {
    case fo4::forms::PresentFieldEdit::updated:
      return RADICLE_STATUS_OK;
    case fo4::forms::PresentFieldEdit::field_absent:
      return fail(RADICLE_STATUS_NOT_FOUND, "the record does not have this field");
    case fo4::forms::PresentFieldEdit::record_missing:
      break;
  }
  return fail(RADICLE_STATUS_NOT_FOUND, "plugin has no such record");
}

}  // namespace

extern "C" {

radicle_status radicle_plugin_set_placed_editor_id(radicle_plugin* plugin,
                                                   radicle_form_key reference,
                                                   const char* editor_id) {
  clear_error();
  return guarded("unknown exception while setting EditorID", [&] {
    const auto key = to_form_key(reference);
    if (plugin == nullptr || !key) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin and a valid reference are required");
    return run(plugin, "unknown exception while setting EditorID", [&](fo4::Plugin& next) {
      const auto stored = fo4::text::utf8_to_windows1252(editor_id == nullptr ? "" : editor_id);
      if (!stored) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "EditorID is not valid UTF-8 text representable in Windows-1252");
      return found(fo4::forms::set_placed_reference_editor_id(next, *key, *stored));
    });
  });
}

radicle_status radicle_plugin_set_placed_record_flags(radicle_plugin* plugin, radicle_form_key reference, uint32_t flags) {
  clear_error();
  return guarded("unknown exception while setting record flags", [&] {
    const auto key = to_form_key(reference);
    if (plugin == nullptr || !key) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin and a valid reference are required");
    return run(plugin, "unknown exception while setting record flags", [&](fo4::Plugin& next) {
      return found(fo4::forms::set_placed_reference_record_flags(next, *key, flags));
    });
  });
}

radicle_status radicle_plugin_set_placed_comments(radicle_plugin* plugin, radicle_form_key reference, const char* comments) {
  clear_error();
  return guarded("unknown exception while setting comments", [&] {
    const auto key = to_form_key(reference);
    if (plugin == nullptr || !key) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin and a valid reference are required");
    return run(plugin, "unknown exception while setting comments", [&](fo4::Plugin& next) {
      const auto stored = comments == nullptr ? std::nullopt : fo4::text::utf8_to_windows1252(comments);
      if (comments != nullptr && !stored) {
        return fail(RADICLE_STATUS_INVALID_ARGUMENT, "comments are not valid UTF-8 text representable in Windows-1252");
      }
      const auto value = stored ? std::optional<std::string_view>{*stored} : std::nullopt;
      return found(fo4::forms::set_placed_reference_comments(next, *key, value));
    });
  });
}

radicle_status radicle_plugin_set_placed_location_reference(radicle_plugin* plugin,
                                                            const radicle_env* env,
                                                            radicle_form_key reference,
                                                            radicle_form_key location) {
  clear_error();
  return guarded("unknown exception while setting location reference", [&] {
    const auto key = to_form_key(reference);
    std::optional<fo4::FormKey> target;
    if (plugin == nullptr || env == nullptr || !key || !optional_key(location, target)) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, env, a valid reference and a valid or cleared location are required");
    }
    return run(plugin, "unknown exception while setting location reference", [&](fo4::Plugin& next) {
      next = with_masters(next, *env, {target});
      return found(fo4::forms::set_placed_reference_location_reference(next, *key, target));
    });
  });
}

radicle_status radicle_plugin_set_placed_ownership(radicle_plugin* plugin,
                                                   const radicle_env* env,
                                                   radicle_form_key reference,
                                                   radicle_form_key owner,
                                                   int32_t no_crime,
                                                   int32_t has_rank,
                                                   int32_t faction_rank) {
  clear_error();
  return guarded("unknown exception while setting ownership", [&] {
    const auto key = to_form_key(reference);
    std::optional<fo4::FormKey> target;
    if (plugin == nullptr || env == nullptr || !key || !optional_key(owner, target)) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, env, a valid reference and a valid or cleared owner are required");
    }
    return run(plugin, "unknown exception while setting ownership", [&](fo4::Plugin& next) {
      next = with_masters(next, *env, {target});
      const auto rank = has_rank != 0 ? std::optional<std::int32_t>{faction_rank} : std::nullopt;
      return found(fo4::forms::set_placed_reference_ownership(next, *key, target, no_crime != 0, rank));
    });
  });
}

radicle_status radicle_plugin_set_placed_enable_parent(radicle_plugin* plugin,
                                                       const radicle_env* env,
                                                       radicle_form_key reference,
                                                       radicle_form_key parent,
                                                       uint8_t flags) {
  clear_error();
  return guarded("unknown exception while setting enable parent", [&] {
    const auto key = to_form_key(reference);
    std::optional<fo4::FormKey> target;
    if (plugin == nullptr || env == nullptr || !key || !optional_key(parent, target)) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, env, a valid reference and a valid or cleared parent are required");
    }
    return run(plugin, "unknown exception while setting enable parent", [&](fo4::Plugin& next) {
      next = with_masters(next, *env, {target});
      return found(fo4::forms::set_placed_reference_enable_parent(next, *key, target, flags));
    });
  });
}

radicle_status radicle_plugin_set_placed_linked_refs(radicle_plugin* plugin,
                                                     const radicle_env* env,
                                                     radicle_form_key reference,
                                                     const radicle_linked_ref* links,
                                                     size_t link_count) {
  clear_error();
  return guarded("unknown exception while setting linked references", [&] {
    const auto key = to_form_key(reference);
    if (plugin == nullptr || env == nullptr || !key || (links == nullptr && link_count != 0)) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, env, a valid reference and links are required");
    }
    std::vector<fo4::forms::LinkedReferenceEdit> edits;
    std::vector<std::optional<fo4::FormKey>> owners;
    for (size_t i = 0; i < link_count; ++i) {
      std::optional<fo4::FormKey> keyword;
      const auto target = to_form_key(links[i].reference);
      if (!optional_key(links[i].keyword, keyword) || !target) {
        return fail(RADICLE_STATUS_INVALID_ARGUMENT, "each link needs a valid reference and a valid or cleared keyword");
      }
      edits.push_back({keyword, *target});
      owners.push_back(keyword);
      owners.push_back(target);
    }
    return run(plugin, "unknown exception while setting linked references", [&](fo4::Plugin& next) {
      next = with_masters(next, *env, owners);
      return found(fo4::forms::set_placed_reference_linked_references(next, *key, edits));
    });
  });
}

radicle_status radicle_plugin_set_placed_teleport(radicle_plugin* plugin,
                                                  const radicle_env* env,
                                                  radicle_form_key reference,
                                                  radicle_form_key door,
                                                  const float position[3],
                                                  const float rotation[3]) {
  clear_error();
  return guarded("unknown exception while setting teleport", [&] {
    const auto key = to_form_key(reference);
    const auto target = to_form_key(door);
    if (plugin == nullptr || env == nullptr || !key || !target || position == nullptr || rotation == nullptr) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, env, reference, door, position and rotation are required");
    }
    return run(plugin, "unknown exception while setting teleport", [&](fo4::Plugin& next) {
      next = with_masters(next, *env, {target});
      const fo4::forms::PositionRotation destination{position[0], position[1], position[2],
                                                     rotation[0], rotation[1], rotation[2]};
      return present(fo4::forms::set_placed_reference_teleport(next, *key, *target, destination));
    });
  });
}

radicle_status radicle_plugin_set_placed_primitive(radicle_plugin* plugin,
                                                   radicle_form_key reference,
                                                   const float bounds[3],
                                                   uint32_t type) {
  clear_error();
  return guarded("unknown exception while setting primitive", [&] {
    const auto key = to_form_key(reference);
    if (plugin == nullptr || !key || bounds == nullptr) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, a valid reference and bounds are required");
    }
    return run(plugin, "unknown exception while setting primitive", [&](fo4::Plugin& next) {
      return present(fo4::forms::set_placed_reference_primitive(next, *key, {bounds[0], bounds[1], bounds[2]}, type));
    });
  });
}

radicle_status radicle_plugin_set_placed_activate_parent_delay(radicle_plugin* plugin,
                                                               radicle_form_key reference,
                                                               size_t index,
                                                               float delay) {
  clear_error();
  return guarded("unknown exception while setting activate parent delay", [&] {
    const auto key = to_form_key(reference);
    if (plugin == nullptr || !key) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin and a valid reference are required");
    return run(plugin, "unknown exception while setting activate parent delay", [&](fo4::Plugin& next) {
      return present(fo4::forms::set_placed_reference_activate_parent_delay(next, *key, index, delay));
    });
  });
}

}  // extern "C"
