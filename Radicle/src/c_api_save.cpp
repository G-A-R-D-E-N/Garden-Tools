#include "fo4/c_api_save.h"

#include "c_api_env_internal.hpp"
#include "fo4/save/save_header.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

struct radicle_save_header {
  fo4::save::SaveHeader value;
  std::vector<std::byte> screenshot;
};

namespace {

using namespace radicle_c_detail;
using radicle_c_env_detail::guarded;

}  // namespace

extern "C" {

radicle_status radicle_save_header_parse(const uint8_t* bytes, size_t size, radicle_save_header** out) {
  clear_error();
  if (out != nullptr) *out = nullptr;
  if ((bytes == nullptr && size != 0) || out == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "bytes (unless size is 0) and out are required");
  }
  return guarded("unknown exception while parsing a save header", [&] {
    fo4::save::SaveHeader parsed;
    std::string error;
    const auto* first = reinterpret_cast<const std::byte*>(bytes);
    if (!fo4::save::parse_save_header(std::span<const std::byte>(first, size), parsed, error)) {
      return fail(RADICLE_STATUS_PARSE_ERROR, error.c_str());
    }
    auto handle = std::make_unique<radicle_save_header>();
    handle->screenshot.assign(parsed.screenshot.begin(), parsed.screenshot.end());
    parsed.screenshot = {};
    handle->value = std::move(parsed);
    *out = handle.release();
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_save_header_get_info(const radicle_save_header* header, radicle_save_header_info* out_info) {
  clear_error();
  if (out_info != nullptr) *out_info = {};
  if (header == nullptr || out_info == nullptr) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "header and out_info are required");
  const auto& value = header->value;
  radicle_save_header_info info{};
  info.version = value.version;
  info.save_number = value.save_number;
  info.player_name = value.player_name.c_str();
  info.level = value.level;
  info.location = value.location.c_str();
  info.playtime = value.playtime.c_str();
  info.race = value.race.c_str();
  info.sex = value.sex;
  info.current_xp = value.current_xp;
  info.required_xp = value.required_xp;
  info.filetime = value.filetime;
  info.screenshot_width = value.screenshot_width;
  info.screenshot_height = value.screenshot_height;
  info.screenshot = header->screenshot.empty() ? nullptr : reinterpret_cast<const uint8_t*>(header->screenshot.data());
  info.screenshot_size = header->screenshot.size();
  info.form_version = value.form_version;
  info.game_version = value.game_version.c_str();
  info.plugin_count = value.plugins.size();
  info.light_plugin_count = value.light_plugins.size();
  *out_info = info;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_save_header_plugin(const radicle_save_header* header, radicle_plugin_kind kind, size_t index,
                                          const char** out_name) {
  clear_error();
  if (out_name != nullptr) *out_name = nullptr;
  if (header == nullptr || out_name == nullptr) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "header and out_name are required");
  if (kind != RADICLE_PLUGIN_FULL && kind != RADICLE_PLUGIN_LIGHT) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "kind is not a plugin kind");
  }
  const auto& names = kind == RADICLE_PLUGIN_LIGHT ? header->value.light_plugins : header->value.plugins;
  if (index >= names.size()) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin index is out of range");
  *out_name = names[index].c_str();
  return RADICLE_STATUS_OK;
}

void radicle_save_header_free(radicle_save_header* header) {
  clear_error();
  delete header;
}

}
