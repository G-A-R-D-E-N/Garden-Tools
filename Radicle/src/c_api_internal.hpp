#pragma once

#include "fo4/c_api.h"
#include "fo4/plugin/container.hpp"

#include <exception>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

struct radicle_plugin {
  fo4::Plugin value;
};

namespace radicle_c_detail {

extern thread_local std::string g_last_error;

void clear_error();
radicle_status fail(radicle_status status, const char* message);
radicle_status fail_exception(radicle_status status, const std::exception& error);
std::filesystem::path utf8_path(std::string_view value);
radicle_status copy_strings(const std::vector<std::string>& values, char*** out_strings, size_t* out_count);

}  // namespace radicle_c_detail
