#include "fo4/c_api_papyrus_compiler.h"

#include "c_api_internal.hpp"
#include "c_api_pex_internal.hpp"
#include "fo4/c_api_papyrus_source.h"
#include "fo4/papyrus/compile_script.hpp"
#include "fo4/papyrus/decompiler.hpp"
#include "fo4/papyrus/parser.hpp"
#include "fo4/papyrus/script_index.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace radicle_c_detail;
using fo4::papyrus::Diagnostic;
using fo4::papyrus::PapyrusCompileOptions;
using fo4::papyrus::ScriptIndex;
using fo4::papyrus::Severity;

radicle_status copy_bytes(const std::string& value, uint8_t** out_bytes, size_t* out_size) {
  if (value.empty()) {
    return RADICLE_STATUS_OK;
  }
  auto* buffer = static_cast<uint8_t*>(std::malloc(value.size()));
  if (!buffer) {
    return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate listing bytes");
  }
  std::memcpy(buffer, value.data(), value.size());
  *out_bytes = buffer;
  *out_size = value.size();
  return RADICLE_STATUS_OK;
}

std::string row_text(const std::string& value) {
  std::string out = value;
  for (char& character : out) {
    if (character == '\t' || character == '\r' || character == '\n' || character == '\0') {
      character = ' ';
    }
  }
  return out;
}

std::string diagnostic_row(const Diagnostic& diagnostic) {
  return std::string(diagnostic.severity == Severity::Error ? "error" : "warning") + "\t" +
         std::string(fo4::papyrus::codeString(diagnostic.code)) + "\t" +
         std::to_string(diagnostic.span.line) + "\t" + std::to_string(diagnostic.span.column) + "\t" +
         row_text(diagnostic.message);
}

void add_import_roots(ScriptIndex& index, const char* imports) {
  if (!imports) {
    return;
  }
  std::istringstream stream(imports);
  std::string root;
  while (std::getline(stream, root, ';')) {
    if (!root.empty()) {
      index.addRoot(utf8_path(root));
    }
  }
}

}  // namespace

extern "C" {

radicle_status radicle_papyrus_compile_source(const uint8_t* source,
                                              size_t size,
                                              const char* file_name,
                                              radicle_papyrus_compile_options options,
                                              radicle_pex** out_pex,
                                              char*** diag_rows,
                                              size_t* diag_count) {
  clear_error();
  if (!out_pex || !diag_rows || !diag_count) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_pex, diag_rows and diag_count are required");
  }
  *out_pex = nullptr;
  *diag_rows = nullptr;
  *diag_count = 0;
  if (!source) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "source is required");
  }
  if (!file_name) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "file_name is required");
  }
  if (size == 0) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "source must not be empty");
  }
  if (size > RADICLE_PAPYRUS_SOURCE_MAX_BYTES) {
    char message[96];
    std::snprintf(message, sizeof(message), "source size %zu is above the %u-byte cap", size,
                  static_cast<unsigned>(RADICLE_PAPYRUS_SOURCE_MAX_BYTES));
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, message);
  }

  try {
    const std::string name(file_name);
    const std::string text(reinterpret_cast<const char*>(source), size);
    ScriptIndex index;
    add_import_roots(index, options.imports);
    const auto script = fo4::papyrus::Parser::parse(text, name);
    PapyrusCompileOptions compile_options;
    compile_options.emitDebugInfo = options.no_debug_info == 0;
    compile_options.emitDebugOnlyCode = options.release == 0;
    compile_options.emitBetaOnlyCode = options.release == 0;
    auto result = fo4::papyrus::compileScript(index, &script, name, compile_options);

    std::vector<std::string> rows;
    bool failed = false;
    for (const Diagnostic& diagnostic : result.diagnostics) {
      rows.push_back(diagnostic_row(diagnostic));
      failed = failed || diagnostic.severity == Severity::Error;
    }
    std::unique_ptr<radicle_pex> handle;
    if (!failed && result.pex) {
      handle = make_pex_handle(std::move(*result.pex));
    }
    const radicle_status status = copy_strings(rows, diag_rows, diag_count);
    if (status != RADICLE_STATUS_OK) {
      return status;
    }
    *out_pex = handle.release();
    return RADICLE_STATUS_OK;
  } catch (const std::bad_alloc& error) {
    return fail_exception(RADICLE_STATUS_OUT_OF_MEMORY, error);
  } catch (const std::exception& error) {
    return fail_exception(RADICLE_STATUS_INTERNAL_ERROR, error);
  } catch (...) {
    return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown exception while compiling Papyrus source");
  }
}

radicle_status radicle_papyrus_decompile(const radicle_pex* pex,
                                         int32_t assembly,
                                         uint8_t** out_text,
                                         size_t* out_size) {
  clear_error();
  if (out_text) *out_text = nullptr;
  if (out_size) *out_size = 0;
  if (!pex || !out_text || !out_size) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "pex, out_text and out_size are required");
  }
  try {
    const std::string text = assembly != 0 ? fo4::papyrus::Decompiler::disassemble(pex->value)
                                           : fo4::papyrus::Decompiler::toSource(pex->value);
    return copy_bytes(text, out_text, out_size);
  } catch (const std::bad_alloc& error) {
    return fail_exception(RADICLE_STATUS_OUT_OF_MEMORY, error);
  } catch (const std::exception& error) {
    return fail_exception(RADICLE_STATUS_PARSE_ERROR, error);
  } catch (...) {
    return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown exception while decompiling .pex data");
  }
}

}  // extern "C"
