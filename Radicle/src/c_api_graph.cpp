#include "fo4/c_api_graph.h"

#include "c_api_internal.hpp"
#include "fo4/graph/graph_document.hpp"
#include "fo4/graph/graph_json_service.hpp"
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

constexpr CopyMessages k_copy_messages{"out_bytes and out_size are required", "failed to allocate graph text"};

template <typename Body>
radicle_status guarded(Body&& body) {
  return guard_io_exceptions("unknown exception while running the graph tool", body);
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

radicle_status status_of(fo4::graph::GraphJsonStatus status) {
  switch (status) {
    case fo4::graph::GraphJsonStatus::Ok: return RADICLE_STATUS_OK;
    case fo4::graph::GraphJsonStatus::Invalid: return RADICLE_STATUS_INVALID_ARGUMENT;
    case fo4::graph::GraphJsonStatus::NotFound: return RADICLE_STATUS_NOT_FOUND;
    case fo4::graph::GraphJsonStatus::Parse: return RADICLE_STATUS_PARSE_ERROR;
    case fo4::graph::GraphJsonStatus::Io: return RADICLE_STATUS_IO_ERROR;
  }
  return RADICLE_STATUS_INTERNAL_ERROR;
}

const std::string* text_or_null(const char* value, std::string& storage) {
  if (value == nullptr) {
    return nullptr;
  }
  storage = value;
  return &storage;
}

radicle_status deliver(const fo4::graph::GraphJsonResult& result, uint8_t** out_text, size_t* out_size) {
  if (!result.ok()) {
    return fail(status_of(result.status), result.text.c_str());
  }
  return copy_out(result.text, out_text, out_size, k_copy_messages);
}

// Invariant: clears the error and both outputs before any check, so every refusal leaves them empty.
template <typename Run>
radicle_status json_call(bool present,
                         const char* missing,
                         const char* const* base_imports,
                         size_t base_count,
                         uint8_t** out_text,
                         size_t* out_size,
                         Run&& run) {
  clear_error();
  if (!out_text || !out_size) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_text and out_size are required");
  }
  *out_text = nullptr;
  *out_size = 0;
  if (!present) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, missing);
  }
  return guarded([&] {
    std::vector<std::filesystem::path> roots;
    const radicle_status status = base_roots(base_imports, base_count, roots);
    if (status != RADICLE_STATUS_OK) {
      return status;
    }
    return deliver(run(roots), out_text, out_size);
  });
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
    return copy_out(fo4::graph::GraphToolService::validate(utf8_path(graph_path),
                                                           imports ? &imports_text : nullptr,
                                                           roots),
                    out_text, out_size, k_copy_messages);
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
    return copy_out(fo4::graph::GraphToolService::compile(utf8_path(graph_path),
                                                          folder.empty() ? nullptr : &folder,
                                                          imports ? &imports_text : nullptr,
                                                          source_only != 0,
                                                          roots),
                    out_text, out_size, k_copy_messages);
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
    return copy_out(fo4::graph::GraphToolService::searchPalette(
                        query, imports ? &imports_text : nullptr, limit, roots),
                    out_text, out_size, k_copy_messages);
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
    return copy_out(fo4::graph::GraphToolService::describeNode(
                        node_type, imports ? &imports_text : nullptr, roots),
                    out_text, out_size, k_copy_messages);
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
    return copy_out(fo4::graph::documentJson::serialize(*loaded.document), out_text, out_size, k_copy_messages);
  });
}

radicle_status radicle_graph_validate_json(const char* document_json,
                                           const char* imports,
                                           const char* const* base_imports,
                                           size_t base_count,
                                           uint8_t** out_text,
                                           size_t* out_size) {
  return json_call(document_json != nullptr, "document_json is required", base_imports, base_count,
                   out_text, out_size, [&](const auto& roots) {
                     std::string storage;
                     return fo4::graph::GraphJsonService::validate(
                         document_json, text_or_null(imports, storage), roots);
                   });
}

radicle_status radicle_graph_compile_json(const char* document_json,
                                          const char* output_path,
                                          const char* imports,
                                          int source_only,
                                          const char* const* base_imports,
                                          size_t base_count,
                                          uint8_t** out_text,
                                          size_t* out_size) {
  return json_call(document_json != nullptr, "document_json is required", base_imports, base_count,
                   out_text, out_size, [&](const auto& roots) {
                     std::filesystem::path folder;
                     if (output_folder(output_path, folder) != RADICLE_STATUS_OK) {
                       return fo4::graph::GraphJsonResult{fo4::graph::GraphJsonStatus::Invalid,
                                                          radicle_last_error()};
                     }
                     std::string storage;
                     return fo4::graph::GraphJsonService::compile(
                         document_json, folder.empty() ? nullptr : &folder,
                         text_or_null(imports, storage), source_only != 0, roots);
                   });
}

radicle_status radicle_graph_linearize_json(const char* document_json,
                                            const char* imports,
                                            const char* const* base_imports,
                                            size_t base_count,
                                            uint8_t** out_text,
                                            size_t* out_size) {
  return json_call(document_json != nullptr, "document_json is required", base_imports, base_count,
                   out_text, out_size, [&](const auto& roots) {
                     std::string storage;
                     return fo4::graph::GraphJsonService::linearize(
                         document_json, text_or_null(imports, storage), roots);
                   });
}

radicle_status radicle_graph_core_palette_json(const char* imports,
                                               const char* const* base_imports,
                                               size_t base_count,
                                               uint8_t** out_text,
                                               size_t* out_size) {
  return json_call(true, "", base_imports, base_count, out_text, out_size, [&](const auto& roots) {
    std::string storage;
    return fo4::graph::GraphJsonService::corePalette(text_or_null(imports, storage), roots);
  });
}

radicle_status radicle_graph_palette_search_json(const char* kind,
                                                 const char* query,
                                                 const char* script_filter,
                                                 int limit,
                                                 const char* imports,
                                                 const char* const* base_imports,
                                                 size_t base_count,
                                                 uint8_t** out_text,
                                                 size_t* out_size) {
  return json_call(true, "", base_imports, base_count, out_text, out_size, [&](const auto& roots) {
    std::string storage;
    return fo4::graph::GraphJsonService::searchPalette(
        kind ? kind : "", query ? query : "", script_filter ? script_filter : "", limit,
        text_or_null(imports, storage), roots);
  });
}

radicle_status radicle_graph_describe_node_json(const char* node_type,
                                                const char* imports,
                                                const char* const* base_imports,
                                                size_t base_count,
                                                uint8_t** out_text,
                                                size_t* out_size) {
  return json_call(node_type != nullptr, "node_type is required", base_imports, base_count,
                   out_text, out_size, [&](const auto& roots) {
                     std::string storage;
                     return fo4::graph::GraphJsonService::describeNode(
                         node_type, text_or_null(imports, storage), roots);
                   });
}

radicle_status radicle_graph_load_script_json(const char* script_path,
                                              const char* imports,
                                              const char* const* base_imports,
                                              size_t base_count,
                                              uint8_t** out_text,
                                              size_t* out_size) {
  return json_call(script_path != nullptr, "script_path is required", base_imports, base_count,
                   out_text, out_size, [&](const auto& roots) {
                     std::string storage;
                     return fo4::graph::GraphJsonService::loadScript(
                         utf8_path(script_path), text_or_null(imports, storage), roots);
                   });
}

radicle_status radicle_graph_document_normalize(const char* document_json,
                                                uint8_t** out_text,
                                                size_t* out_size) {
  return json_call(document_json != nullptr, "document_json is required", nullptr, 0, out_text,
                   out_size, [&](const auto&) {
                     return fo4::graph::GraphJsonService::normalize(document_json);
                   });
}

radicle_status radicle_graph_document_load(const char* path,
                                           uint8_t** out_text,
                                           size_t* out_size) {
  return json_call(path != nullptr, "path is required", nullptr, 0, out_text, out_size,
                   [&](const auto&) {
                     return fo4::graph::GraphJsonService::loadDocument(utf8_path(path));
                   });
}

radicle_status radicle_graph_document_save(const char* document_json, const char* path) {
  clear_error();
  if (!document_json || !path || path[0] == '\0') {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "document_json and path are required");
  }
  return guarded([&] {
    const auto saved = fo4::graph::GraphJsonService::save(document_json, utf8_path(path));
    return saved.ok() ? RADICLE_STATUS_OK : fail(status_of(saved.status), saved.text.c_str());
  });
}

}  // extern "C"
