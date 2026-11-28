#pragma once

#include "fo4/c_api.h"
#include "fo4/error.hpp"
#include "fo4/localization/strings.hpp"
#include "fo4/plugin/container.hpp"

#include <array>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

struct radicle_plugin {
  std::shared_ptr<fo4::Plugin> owner;
  fo4::Plugin& value;
  fo4::localization::LocalizationTables tables{};
  bool tables_loaded{false};
  std::array<std::filesystem::path, 3> table_files{};
  std::string tables_language{};

  explicit radicle_plugin(fo4::Plugin plugin)
      : owner(std::make_shared<fo4::Plugin>(std::move(plugin))), value(*owner) {}
  explicit radicle_plugin(std::shared_ptr<fo4::Plugin> plugin)
      : owner(std::move(plugin)), value(*owner) {}
  radicle_plugin(radicle_plugin&& other) noexcept
      : owner(std::move(other.owner)),
        value(*owner),
        tables(std::move(other.tables)),
        tables_loaded(other.tables_loaded),
        table_files(std::move(other.table_files)),
        tables_language(std::move(other.tables_language)) {}
  radicle_plugin(const radicle_plugin&) = delete;
  radicle_plugin& operator=(const radicle_plugin&) = delete;
};

namespace radicle_c_detail {

extern thread_local std::string g_last_error;

void clear_error();
radicle_status fail(radicle_status status, const char* message);
radicle_status fail_exception(radicle_status status, const std::exception& error);
std::filesystem::path utf8_path(std::string_view value);
radicle_status copy_strings(const std::vector<std::string>& values, char*** out_strings, size_t* out_count);

struct CopyMessages {
  const char* args;
  const char* alloc;
};

template <typename Body>
radicle_status guard_exceptions(const char* what, Body body) {
  try {
    return body();
  } catch (const std::bad_alloc& error) {
    return fail_exception(RADICLE_STATUS_OUT_OF_MEMORY, error);
  } catch (const fo4::parse_error& error) {
    return fail_exception(RADICLE_STATUS_PARSE_ERROR, error);
  } catch (const std::invalid_argument& error) {
    return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
  } catch (const std::exception& error) {
    return fail_exception(RADICLE_STATUS_INTERNAL_ERROR, error);
  } catch (...) {
    return fail(RADICLE_STATUS_INTERNAL_ERROR, what);
  }
}

template <typename Body>
radicle_status guard_io_exceptions(const char* what, Body body) {
  return guard_exceptions(what, [&]() -> radicle_status {
    try {
      return body();
    } catch (const std::filesystem::filesystem_error& error) {
      return fail_exception(RADICLE_STATUS_IO_ERROR, error);
    }
  });
}

template <typename Source, typename T>
radicle_status copy_out(const Source& values, T** out, size_t* out_count, const CopyMessages& messages) {
  static_assert(sizeof(T) == sizeof(typename Source::value_type));
  if (!out || !out_count) return fail(RADICLE_STATUS_INVALID_ARGUMENT, messages.args);
  *out = nullptr;
  *out_count = 0;
  if (values.empty()) return RADICLE_STATUS_OK;
  auto* buffer = static_cast<T*>(std::malloc(values.size() * sizeof(T)));
  if (!buffer) return fail(RADICLE_STATUS_OUT_OF_MEMORY, messages.alloc);
  std::memcpy(buffer, values.data(), values.size() * sizeof(T));
  *out = buffer;
  *out_count = values.size();
  return RADICLE_STATUS_OK;
}

}  // namespace radicle_c_detail
