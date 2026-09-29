#pragma once

#include "fo4/c_api.h"

#include <exception>
#include <filesystem>
#include <string>
#include <string_view>

namespace radicle_c_detail {

extern thread_local std::string g_last_error;

void clear_error();
radicle_status fail(radicle_status status, const char* message);
radicle_status fail_exception(radicle_status status, const std::exception& error);
std::filesystem::path utf8_path(std::string_view value);

}  // namespace radicle_c_detail
