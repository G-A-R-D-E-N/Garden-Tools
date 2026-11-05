#include "fo4/c_api_papyrus_analysis.h"

#include "c_api_internal.hpp"
#include "fo4/papyrus/analysis_service.hpp"

#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <new>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace radicle_c_detail;
using fo4::papyrus::AnalysisService;

radicle_status copy_bytes(const std::string& value, uint8_t** out_text, size_t* out_size) {
  if (value.empty()) {
    return RADICLE_STATUS_OK;
  }
  auto* buffer = static_cast<uint8_t*>(std::malloc(value.size()));
  if (!buffer) {
    return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate analysis text");
  }
  std::memcpy(buffer, value.data(), value.size());
  *out_text = buffer;
  *out_size = value.size();
  return RADICLE_STATUS_OK;
}

template <typename Body>
radicle_status guarded(Body&& body) {
  try {
    return body();
  } catch (const std::bad_alloc& error) {
    return fail_exception(RADICLE_STATUS_OUT_OF_MEMORY, error);
  } catch (const std::filesystem::filesystem_error& error) {
    return fail_exception(RADICLE_STATUS_IO_ERROR, error);
  } catch (const std::exception& error) {
    return fail_exception(RADICLE_STATUS_INTERNAL_ERROR, error);
  } catch (...) {
    return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown exception while analyzing Papyrus source");
  }
}

radicle_status base_roots(const char* const* base_imports,
                          size_t base_count,
                          std::vector<std::filesystem::path>& roots) {
  if (base_count != 0 && base_imports == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                "base_imports is required when base_count is not zero");
  }
  for (size_t i = 0; i < base_count; ++i) {
    if (base_imports[i] == nullptr) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "base_imports must not contain null entries");
    }
    roots.push_back(utf8_path(base_imports[i]));
  }
  return RADICLE_STATUS_OK;
}

}  // namespace

extern "C" {

radicle_status radicle_papyrus_line_column_to_offset(const uint8_t* text,
                                                     size_t size,
                                                     int32_t line,
                                                     int32_t column,
                                                     int32_t* out_offset) {
  clear_error();
  if (!out_offset) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_offset is required");
  }
  *out_offset = 0;
  if (!text) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "text is required");
  }
  *out_offset = AnalysisService::lineColumnToOffset(
      std::string_view(reinterpret_cast<const char*>(text), size), line, column);
  return RADICLE_STATUS_OK;
}

radicle_status radicle_papyrus_check(const char* source,
                                     int32_t semantic,
                                     const char* imports,
                                     const char* const* base_imports,
                                     size_t base_count,
                                     uint8_t** out_text,
                                     size_t* out_size) {
  clear_error();
  if (!out_text || !out_size) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_text and out_size are required");
  }
  *out_text = nullptr;
  *out_size = 0;
  if (!source) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "source is required");
  }

  return guarded([&] {
    std::vector<std::filesystem::path> roots;
    const radicle_status status = base_roots(base_imports, base_count, roots);
    if (status != RADICLE_STATUS_OK) {
      return status;
    }
    const std::string imports_text = imports ? imports : "";
    return copy_bytes(AnalysisService::check(source, true, semantic != 0,
                                             imports ? &imports_text : nullptr, roots),
                      out_text, out_size);
  });
}

radicle_status radicle_papyrus_outline(const char* source, uint8_t** out_text, size_t* out_size) {
  clear_error();
  if (!out_text || !out_size) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_text and out_size are required");
  }
  *out_text = nullptr;
  *out_size = 0;
  if (!source) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "source is required");
  }

  return guarded([&] {
    return copy_bytes(AnalysisService::outline(source), out_text, out_size);
  });
}

radicle_status radicle_papyrus_definition(const char* source,
                                          int32_t line,
                                          int32_t column,
                                          const char* imports,
                                          const char* const* base_imports,
                                          size_t base_count,
                                          uint8_t** out_text,
                                          size_t* out_size) {
  clear_error();
  if (!out_text || !out_size) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_text and out_size are required");
  }
  *out_text = nullptr;
  *out_size = 0;
  if (!source) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "source is required");
  }

  return guarded([&] {
    std::vector<std::filesystem::path> roots;
    const radicle_status status = base_roots(base_imports, base_count, roots);
    if (status != RADICLE_STATUS_OK) {
      return status;
    }
    const std::string imports_text = imports ? imports : "";
    return copy_bytes(AnalysisService::definition(source, line, column,
                                                  imports ? &imports_text : nullptr, roots),
                      out_text, out_size);
  });
}

radicle_status radicle_papyrus_analyze_json(const uint8_t* text,
                                            size_t size,
                                            const char* file_path,
                                            uint8_t** out_text,
                                            size_t* out_size) {
  clear_error();
  if (!out_text || !out_size) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_text and out_size are required");
  }
  *out_text = nullptr;
  *out_size = 0;
  if (!text) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "text is required");
  }

  return guarded([&] {
    const std::string buffer(reinterpret_cast<const char*>(text), size);
    const std::string path_text = file_path ? file_path : "";
    return copy_bytes(
        AnalysisService::analyzeJson(buffer, file_path ? &path_text : nullptr), out_text, out_size);
  });
}

radicle_status radicle_papyrus_symbol_at_json(const uint8_t* text,
                                              size_t size,
                                              const char* file_path,
                                              int32_t offset,
                                              const char* imports,
                                              const char* const* base_imports,
                                              size_t base_count,
                                              uint8_t** out_text,
                                              size_t* out_size) {
  clear_error();
  if (!out_text || !out_size) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_text and out_size are required");
  }
  *out_text = nullptr;
  *out_size = 0;
  if (!text) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "text is required");
  }

  return guarded([&] {
    std::vector<std::filesystem::path> roots;
    const radicle_status status = base_roots(base_imports, base_count, roots);
    if (status != RADICLE_STATUS_OK) {
      return status;
    }
    const std::string buffer(reinterpret_cast<const char*>(text), size);
    const std::string path_text = file_path ? file_path : "";
    const std::string imports_text = imports ? imports : "";
    return copy_bytes(AnalysisService::symbolAtJson(buffer, file_path ? &path_text : nullptr,
                                                    offset, imports ? &imports_text : nullptr,
                                                    roots),
                      out_text, out_size);
  });
}

radicle_status radicle_papyrus_completion_json(const uint8_t* text,
                                               size_t size,
                                               const char* file_path,
                                               int32_t offset,
                                               const char* imports,
                                               const char* const* base_imports,
                                               size_t base_count,
                                               uint8_t** out_text,
                                               size_t* out_size) {
  clear_error();
  if (!out_text || !out_size) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_text and out_size are required");
  }
  *out_text = nullptr;
  *out_size = 0;
  if (!text) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "text is required");
  }
  if (offset < 0 || static_cast<size_t>(offset) > size) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "offset is outside the text");
  }

  return guarded([&] {
    std::vector<std::filesystem::path> roots;
    const radicle_status status = base_roots(base_imports, base_count, roots);
    if (status != RADICLE_STATUS_OK) {
      return status;
    }
    const std::string buffer(reinterpret_cast<const char*>(text), size);
    const std::string path_text = file_path ? file_path : "";
    const std::string imports_text = imports ? imports : "";
    return copy_bytes(AnalysisService::completionJson(buffer, file_path ? &path_text : nullptr,
                                                      offset, imports ? &imports_text : nullptr,
                                                      roots),
                      out_text, out_size);
  });
}

radicle_status radicle_papyrus_signature_help_json(const uint8_t* text,
                                                   size_t size,
                                                   const char* file_path,
                                                   int32_t offset,
                                                   const char* imports,
                                                   const char* const* base_imports,
                                                   size_t base_count,
                                                   uint8_t** out_text,
                                                   size_t* out_size) {
  clear_error();
  if (!out_text || !out_size) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_text and out_size are required");
  }
  *out_text = nullptr;
  *out_size = 0;
  if (!text) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "text is required");
  }
  if (offset < 0 || static_cast<size_t>(offset) > size) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "offset is outside the text");
  }

  return guarded([&] {
    std::vector<std::filesystem::path> roots;
    const radicle_status status = base_roots(base_imports, base_count, roots);
    if (status != RADICLE_STATUS_OK) {
      return status;
    }
    const std::string buffer(reinterpret_cast<const char*>(text), size);
    const std::string path_text = file_path ? file_path : "";
    const std::string imports_text = imports ? imports : "";
    return copy_bytes(
        AnalysisService::signatureHelpJson(buffer, file_path ? &path_text : nullptr, offset,
                                           imports ? &imports_text : nullptr, roots),
        out_text, out_size);
  });
}

radicle_status radicle_papyrus_check_text_json(const uint8_t* text,
                                               size_t size,
                                               const char* file_path,
                                               int32_t revision,
                                               const char* imports,
                                               const char* const* base_imports,
                                               size_t base_count,
                                               uint8_t** out_text,
                                               size_t* out_size) {
  clear_error();
  if (!out_text || !out_size) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_text and out_size are required");
  }
  *out_text = nullptr;
  *out_size = 0;
  if (!text) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "text is required");
  }

  return guarded([&] {
    std::vector<std::filesystem::path> roots;
    const radicle_status status = base_roots(base_imports, base_count, roots);
    if (status != RADICLE_STATUS_OK) {
      return status;
    }
    const std::string buffer(reinterpret_cast<const char*>(text), size);
    const std::string path_text = file_path ? file_path : "";
    const std::string imports_text = imports ? imports : "";
    return copy_bytes(AnalysisService::checkTextJson(buffer, file_path ? &path_text : nullptr,
                                                     revision, imports ? &imports_text : nullptr,
                                                     roots),
                      out_text, out_size);
  });
}

radicle_status radicle_papyrus_compile_path(const char* source,
                                            const char* output_path,
                                            const char* imports,
                                            int32_t release,
                                            int32_t no_debug_info,
                                            const char* flag_file,
                                            const char* const* base_imports,
                                            size_t base_count,
                                            uint8_t** out_text,
                                            size_t* out_size) {
  clear_error();
  if (!out_text || !out_size) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_text and out_size are required");
  }
  *out_text = nullptr;
  *out_size = 0;
  if (!source) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "source is required");
  }

  return guarded([&] {
    std::vector<std::filesystem::path> roots;
    const radicle_status status = base_roots(base_imports, base_count, roots);
    if (status != RADICLE_STATUS_OK) {
      return status;
    }
    const std::string output = output_path ? output_path : "";
    const std::string imports_text = imports ? imports : "";
    const std::string flag_text = flag_file ? flag_file : "";
    return copy_bytes(
        AnalysisService::compile(source, output_path ? &output : nullptr,
                                 imports ? &imports_text : nullptr, release != 0,
                                 no_debug_info == 0, flag_file ? &flag_text : nullptr, roots),
        out_text, out_size);
  });
}

radicle_status radicle_papyrus_natural_roots(const char* folder,
                                             char*** out_roots,
                                             size_t* out_count) {
  clear_error();
  if (!out_roots || !out_count) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_roots and out_count are required");
  }
  *out_roots = nullptr;
  *out_count = 0;
  if (!folder) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "folder is required");
  }

  return guarded([&] {
    std::vector<std::string> rows;
    for (const auto& root : AnalysisService::naturalRootsFor(utf8_path(folder))) {
      const auto utf8 = root.u8string();
      rows.emplace_back(utf8.begin(), utf8.end());
    }
    return copy_strings(rows, out_roots, out_count);
  });
}

}  // extern "C"
