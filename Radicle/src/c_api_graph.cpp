#include "fo4/c_api_graph.h"

#include "c_api_internal.hpp"
#include "fo4/graph/graph_document.hpp"
#include "fo4/graph/graph_script_loader.hpp"
#include "fo4/graph/graph_tool_service.hpp"

#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <new>
#include <string>
#include <vector>

namespace {

using namespace radicle_c_detail;

radicle_status copy_bytes(const std::string& value, uint8_t** out_text, size_t* out_size) {
  if (value.empty()) {
    return RADICLE_STATUS_OK;
  }
  auto* buffer = static_cast<uint8_t*>(std::malloc(value.size()));
  if (!buffer) {
    return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate graph text");
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
    return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown exception while running the graph tool");
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

radicle_status output_folder(const char* output_path, std::filesystem::path& folder) {
  if (output_path == nullptr || output_path[0] == '\0') {
    return RADICLE_STATUS_OK;
  }
  folder = utf8_path(output_path);
  for (const auto& part : folder) {
    if (part == std::filesystem::path("..")) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "output_path must not contain '..' segments");
    }
  }
  folder = folder.lexically_normal();
  return RADICLE_STATUS_OK;
}

std::string load_failure(const fo4::graph::GraphScriptLoadResult& loaded) {
  if (loaded.failure) {
    return *loaded.failure;
  }
  std::string message;
  for (const auto& diagnostic : loaded.diagnostics) {
    if (diagnostic.severity != fo4::graph::GraphSeverity::Error) {
      continue;
    }
    if (!message.empty()) {
      message += "; ";
    }
    message += diagnostic.toString();
  }
  return message.empty() ? "the script could not be lifted into a graph" : message;
}

}  // namespace

extern "C" {

radicle_status radicle_graph_validate(const char* graph_path,
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
  if (!graph_path) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "graph_path is required");
  }

  return guarded([&] {
    std::vector<std::filesystem::path> roots;
    const radicle_status status = base_roots(base_imports, base_count, roots);
    if (status != RADICLE_STATUS_OK) {
      return status;
    }
    const std::string imports_text = imports ? imports : "";
    return copy_bytes(fo4::graph::GraphToolService::validate(utf8_path(graph_path),
                                                             imports ? &imports_text : nullptr,
                                                             roots),
                      out_text, out_size);
  });
}

radicle_status radicle_graph_compile(const char* graph_path,
                                     const char* output_path,
                                     const char* imports,
                                     int source_only,
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
  if (!graph_path) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "graph_path is required");
  }

  return guarded([&] {
    std::vector<std::filesystem::path> roots;
    radicle_status status = base_roots(base_imports, base_count, roots);
    if (status != RADICLE_STATUS_OK) {
      return status;
    }
    std::filesystem::path folder;
    status = output_folder(output_path, folder);
    if (status != RADICLE_STATUS_OK) {
      return status;
    }
    const std::string imports_text = imports ? imports : "";
    return copy_bytes(fo4::graph::GraphToolService::compile(utf8_path(graph_path),
                                                            folder.empty() ? nullptr : &folder,
                                                            imports ? &imports_text : nullptr,
                                                            source_only != 0,
                                                            roots),
                      out_text, out_size);
  });
}

radicle_status radicle_graph_palette_search(const char* query,
                                            const char* imports,
                                            int limit,
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
  if (!query) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "query is required");
  }

  return guarded([&] {
    std::vector<std::filesystem::path> roots;
    const radicle_status status = base_roots(base_imports, base_count, roots);
    if (status != RADICLE_STATUS_OK) {
      return status;
    }
    const std::string imports_text = imports ? imports : "";
    return copy_bytes(fo4::graph::GraphToolService::searchPalette(
                          query, imports ? &imports_text : nullptr, limit, roots),
                      out_text, out_size);
  });
}

radicle_status radicle_graph_describe_node(const char* node_type,
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
  if (!node_type) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "node_type is required");
  }

  return guarded([&] {
    std::vector<std::filesystem::path> roots;
    const radicle_status status = base_roots(base_imports, base_count, roots);
    if (status != RADICLE_STATUS_OK) {
      return status;
    }
    const std::string imports_text = imports ? imports : "";
    return copy_bytes(fo4::graph::GraphToolService::describeNode(
                          node_type, imports ? &imports_text : nullptr, roots),
                      out_text, out_size);
  });
}

radicle_status radicle_graph_load_script(const char* script_path,
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
  if (!script_path) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "script_path is required");
  }

  return guarded([&] {
    std::vector<std::filesystem::path> roots;
    const radicle_status status = base_roots(base_imports, base_count, roots);
    if (status != RADICLE_STATUS_OK) {
      return status;
    }
    const auto path = utf8_path(script_path);
    const std::string imports_text = imports ? imports : "";
    const auto index = fo4::graph::graphIndexFor(&path, imports ? &imports_text : nullptr, roots);
    const auto loaded = fo4::graph::GraphScriptLoader::load(path, index.roots());
    if (!loaded.success()) {
      return fail(RADICLE_STATUS_PARSE_ERROR, load_failure(loaded).c_str());
    }
    return copy_bytes(fo4::graph::documentJson::serialize(*loaded.document), out_text, out_size);
  });
}

}  // extern "C"
