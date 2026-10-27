#include "fo4/c_api_pex.h"

#include "c_api_internal.hpp"
#include "c_api_pex_internal.hpp"
#include "fo4/pex/pex.hpp"
#include "fo4/pex_analysis/opcodes.hpp"
#include "fo4/pex_analysis/script_set.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using namespace radicle_c_detail;

radicle_status copy_bytes(const std::string& value, uint8_t** out_bytes, size_t* out_size) {
  if (value.empty()) {
    return RADICLE_STATUS_OK;
  }
  auto* buffer = static_cast<uint8_t*>(std::malloc(value.size()));
  if (!buffer) {
    return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate string bytes");
  }
  std::memcpy(buffer, value.data(), value.size());
  *out_bytes = buffer;
  *out_size = value.size();
  return RADICLE_STATUS_OK;
}

std::string bytes_text(const uint8_t* bytes, size_t size) {
  return bytes ? std::string(reinterpret_cast<const char*>(bytes), size) : std::string();
}

radicle_status entry_at(const std::vector<std::string>& table,
                        size_t index,
                        const char* what,
                        uint8_t** out_bytes,
                        size_t* out_size) {
  *out_bytes = nullptr;
  *out_size = 0;
  if (index >= table.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, what);
  }
  try {
    return copy_bytes(table[index], out_bytes, out_size);
  } catch (const std::bad_alloc& error) {
    return fail_exception(RADICLE_STATUS_OUT_OF_MEMORY, error);
  } catch (const std::exception& error) {
    return fail_exception(RADICLE_STATUS_INTERNAL_ERROR, error);
  } catch (...) {
    return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown exception while reading .pex data");
  }
}

class CodeBuilder {
 public:
  explicit CodeBuilder(const std::unordered_map<std::string, size_t>& strings) : strings_(strings) {}

  void object(const fo4::pex::PexObject& object) {
    autoState_ = str(object.autoStateName);
    for (const auto& state : object.states) {
      const auto first = functions_.size();
      for (const auto& function : state.functions) add(function, states_.size());
      states_.push_back({str(state.name), first, state.functions.size()});
    }
    for (const auto& property : object.properties) {
      radicle_pex_property row{};
      row.name = str(property.name);
      row.type = str(property.type);
      row.user_flags = property.userFlags;
      row.flags = property.flags;
      row.auto_var_name = str(property.autoVarName);
      row.read_function = property.readHandler ? add(*property.readHandler, RADICLE_PEX_NO_INDEX) : RADICLE_PEX_NO_INDEX;
      row.write_function = property.writeHandler ? add(*property.writeHandler, RADICLE_PEX_NO_INDEX) : RADICLE_PEX_NO_INDEX;
      properties_.push_back(row);
    }
    for (const auto& variable : object.variables) {
      variables_.push_back(variableRow(variable.name, variable.type, variable.userFlags, variable.isConst, variable.defaultValue));
    }
    for (const auto& structValue : object.structs) {
      structs_.push_back({str(structValue.name), members_.size(), structValue.members.size()});
      for (const auto& member : structValue.members) {
        members_.push_back(variableRow(member.name, member.type, member.userFlags, member.isConst, member.defaultValue));
      }
    }
  }

  void release(radicle_pex_object_code* out) const {
    radicle_pex_object_code code{};
    code.auto_state_name = autoState_;
    try {
      take(states_, &code.states, &code.state_count);
      take(functions_, &code.functions, &code.function_count);
      take(names_, &code.typed_names, &code.typed_name_count);
      take(instructions_, &code.instructions, &code.instruction_count);
      take(values_, &code.values, &code.value_count);
      take(variables_, &code.variables, &code.variable_count);
      take(properties_, &code.properties, &code.property_count);
      take(structs_, &code.structs, &code.struct_count);
      take(members_, &code.struct_members, &code.struct_member_count);
    } catch (...) {
      radicle_pex_free_object_code(&code);
      throw;
    }
    *out = code;
  }

 private:
  template <typename T>
  static void take(const std::vector<T>& rows, T** out_rows, size_t* out_count) {
    if (rows.empty()) return;
    auto* buffer = static_cast<T*>(std::malloc(rows.size() * sizeof(T)));
    if (!buffer) throw std::bad_alloc();
    std::memcpy(buffer, rows.data(), rows.size() * sizeof(T));
    *out_rows = buffer;
    *out_count = rows.size();
  }

  size_t str(const std::string& value) const {
    const auto found = strings_.find(value);
    if (found != strings_.end()) return found->second;
    if (value.empty()) return RADICLE_PEX_NO_STRING;
    throw std::runtime_error("a .pex name is missing from its own string table");
  }

  radicle_pex_value value(const fo4::pex::PexValue& source) const {
    radicle_pex_value out{};
    out.type = static_cast<uint8_t>(source.type);
    out.string = RADICLE_PEX_NO_STRING;
    switch (source.type) {
      case fo4::pex::PexValueType::Identifier:
      case fo4::pex::PexValueType::String:
        out.string = str(source.str);
        break;
      case fo4::pex::PexValueType::Integer:
        out.integer = source.int_;
        break;
      case fo4::pex::PexValueType::Float:
        out.real = source.float_;
        break;
      case fo4::pex::PexValueType::Bool:
        out.boolean = source.bool_ ? 1 : 0;
        break;
      case fo4::pex::PexValueType::None:
        break;
    }
    return out;
  }

  radicle_pex_variable variableRow(const std::string& name, const std::string& type, uint32_t userFlags, bool isConst,
                                   const std::optional<fo4::pex::PexValue>& defaultValue) const {
    radicle_pex_variable out{};
    out.name = str(name);
    out.type = str(type);
    out.user_flags = userFlags;
    out.is_const = isConst ? 1 : 0;
    out.has_default = defaultValue ? 1 : 0;
    out.default_value = defaultValue ? value(*defaultValue) : value(fo4::pex::PexValue{});
    return out;
  }

  size_t add(const fo4::pex::PexFunction& function, size_t state) {
    radicle_pex_function out{};
    out.state = state;
    out.name = str(function.name);
    out.return_type = str(function.returnType);
    out.user_flags = function.userFlags;
    out.is_global = function.isGlobal ? 1 : 0;
    out.is_native = function.isNative ? 1 : 0;
    out.first_parameter = names_.size();
    out.parameter_count = function.params.size();
    for (const auto& name : function.params) names_.push_back({str(name.name), str(name.type)});
    out.first_local = names_.size();
    out.local_count = function.locals.size();
    for (const auto& name : function.locals) names_.push_back({str(name.name), str(name.type)});
    out.first_instruction = instructions_.size();
    out.instruction_count = function.instructions.size();
    for (const auto& instruction : function.instructions) {
      radicle_pex_instruction row{};
      row.opcode = instruction.opCode;
      row.line = instruction.line;
      row.first_argument = values_.size();
      row.argument_count = instruction.args.size();
      for (const auto& argument : instruction.args) values_.push_back(value(argument));
      instructions_.push_back(row);
    }
    functions_.push_back(out);
    return functions_.size() - 1;
  }

  const std::unordered_map<std::string, size_t>& strings_;
  size_t autoState_ = RADICLE_PEX_NO_STRING;
  std::vector<radicle_pex_state> states_;
  std::vector<radicle_pex_function> functions_;
  std::vector<radicle_pex_typed_name> names_;
  std::vector<radicle_pex_instruction> instructions_;
  std::vector<radicle_pex_value> values_;
  std::vector<radicle_pex_variable> variables_;
  std::vector<radicle_pex_property> properties_;
  std::vector<radicle_pex_struct> structs_;
  std::vector<radicle_pex_variable> members_;
};

}

extern "C" {

radicle_status radicle_pex_parse(const uint8_t* bytes, size_t size, radicle_pex** out_pex) {
  clear_error();
  if ((!bytes && size != 0) || !out_pex) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "bytes and out_pex are required");
  }

  *out_pex = nullptr;
  try {
    std::vector<std::uint8_t> input;
    if (size != 0) {
      input.assign(bytes, bytes + size);
    }
    auto handle = std::make_unique<radicle_pex>();
    handle->value = fo4::pex::PexFile::fromBytes(input);
    for (size_t index = 0; index < handle->value.stringTable.size(); ++index) {
      handle->strings.emplace(handle->value.stringTable[index], index);
    }
    *out_pex = handle.release();
    return RADICLE_STATUS_OK;
  } catch (const std::bad_alloc& error) {
    return fail_exception(RADICLE_STATUS_OUT_OF_MEMORY, error);
  } catch (const std::exception& error) {
    return fail_exception(RADICLE_STATUS_PARSE_ERROR, error);
  } catch (...) {
    return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown exception while parsing .pex data");
  }
}

radicle_status radicle_pex_read_file(const char* path, radicle_pex** out_pex) {
  clear_error();
  if (!path || !out_pex) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "path and out_pex are required");
  }

  *out_pex = nullptr;
  try {
    std::ifstream stream(utf8_path(path), std::ios::binary);
    if (!stream) {
      return fail(RADICLE_STATUS_IO_ERROR, "failed to open the .pex file");
    }

    const std::vector<char> raw((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    if (stream.bad()) {
      return fail(RADICLE_STATUS_IO_ERROR, "failed to read the .pex file");
    }
    const std::vector<std::uint8_t> bytes(raw.begin(), raw.end());
    return radicle_pex_parse(bytes.data(), bytes.size(), out_pex);
  } catch (const std::bad_alloc& error) {
    return fail_exception(RADICLE_STATUS_OUT_OF_MEMORY, error);
  } catch (const std::exception& error) {
    return fail_exception(RADICLE_STATUS_IO_ERROR, error);
  } catch (...) {
    return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown exception while reading .pex data");
  }
}

void radicle_pex_destroy(radicle_pex* pex) {
  clear_error();
  delete pex;
}

radicle_status radicle_pex_write_bytes(const radicle_pex* pex, uint8_t** out_bytes, size_t* out_size) {
  clear_error();
  if (!pex || !out_bytes || !out_size) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "pex, out_bytes and out_size are required");
  }

  *out_bytes = nullptr;
  *out_size = 0;
  try {
    const auto bytes = pex->value.toBytes();
    if (bytes.empty()) {
      return RADICLE_STATUS_OK;
    }
    auto* buffer = static_cast<uint8_t*>(std::malloc(bytes.size()));
    if (!buffer) {
      return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate serialized bytes");
    }
    std::memcpy(buffer, bytes.data(), bytes.size());
    *out_bytes = buffer;
    *out_size = bytes.size();
    return RADICLE_STATUS_OK;
  } catch (const std::bad_alloc& error) {
    return fail_exception(RADICLE_STATUS_OUT_OF_MEMORY, error);
  } catch (const std::exception& error) {
    return fail_exception(RADICLE_STATUS_INTERNAL_ERROR, error);
  } catch (...) {
    return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown exception while serializing .pex data");
  }
}

radicle_status radicle_pex_get_header(const radicle_pex* pex, radicle_pex_header* out_header) {
  clear_error();
  if (!pex || !out_header) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "pex and out_header are required");
  }

  const auto& file = pex->value;
  radicle_pex_header header{};
  header.major_version = file.majorVersion;
  header.minor_version = file.minorVersion;
  header.game_id = file.gameId;
  header.compilation_time = file.compilationTime;
  header.string_count = file.stringTable.size();
  header.object_count = file.objects.size();
  *out_header = header;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_pex_object_get_counts(const radicle_pex* pex,
                                             size_t index,
                                             radicle_pex_object_counts* out_counts) {
  clear_error();
  if (!pex || !out_counts) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "pex and out_counts are required");
  }

  *out_counts = radicle_pex_object_counts{};
  if (index >= pex->value.objects.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "object index is out of range");
  }

  const auto& object = pex->value.objects[index];
  radicle_pex_object_counts counts{};
  counts.struct_count = object.structs.size();
  counts.variable_count = object.variables.size();
  counts.property_count = object.properties.size();
  counts.state_count = object.states.size();
  for (const auto& state : object.states) {
    counts.function_count += state.functions.size();
  }
  *out_counts = counts;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_pex_object_name(const radicle_pex* pex,
                                       size_t index,
                                       uint8_t** out_bytes,
                                       size_t* out_size) {
  clear_error();
  if (!pex || !out_bytes || !out_size) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "pex, out_bytes and out_size are required");
  }
  *out_bytes = nullptr;
  *out_size = 0;
  if (index >= pex->value.objects.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "object index is out of range");
  }
  try {
    return copy_bytes(pex->value.objects[index].name, out_bytes, out_size);
  } catch (const std::bad_alloc& error) {
    return fail_exception(RADICLE_STATUS_OUT_OF_MEMORY, error);
  } catch (const std::exception& error) {
    return fail_exception(RADICLE_STATUS_INTERNAL_ERROR, error);
  } catch (...) {
    return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown exception while reading a .pex object name");
  }
}

radicle_status radicle_pex_object_parent_name(const radicle_pex* pex,
                                              size_t index,
                                              uint8_t** out_bytes,
                                              size_t* out_size) {
  clear_error();
  if (!pex || !out_bytes || !out_size) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "pex, out_bytes and out_size are required");
  }
  *out_bytes = nullptr;
  *out_size = 0;
  if (index >= pex->value.objects.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "object index is out of range");
  }
  try {
    return copy_bytes(pex->value.objects[index].parentClassName, out_bytes, out_size);
  } catch (const std::bad_alloc& error) {
    return fail_exception(RADICLE_STATUS_OUT_OF_MEMORY, error);
  } catch (const std::exception& error) {
    return fail_exception(RADICLE_STATUS_INTERNAL_ERROR, error);
  } catch (...) {
    return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown exception while reading a parent class name");
  }
}

radicle_status radicle_pex_string_table_entry(const radicle_pex* pex,
                                              size_t index,
                                              uint8_t** out_bytes,
                                              size_t* out_size) {
  clear_error();
  if (!pex || !out_bytes || !out_size) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "pex, out_bytes and out_size are required");
  }
  return entry_at(pex->value.stringTable, index, "string table index is out of range", out_bytes, out_size);
}

radicle_status radicle_pex_object_code_get(const radicle_pex* pex,
                                          size_t index,
                                          radicle_pex_object_code* out_code) {
  clear_error();
  if (!pex || !out_code) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "pex and out_code are required");
  }
  *out_code = radicle_pex_object_code{};
  if (index >= pex->value.objects.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "object index is out of range");
  }
  try {
    CodeBuilder builder(pex->strings);
    builder.object(pex->value.objects[index]);
    builder.release(out_code);
    return RADICLE_STATUS_OK;
  } catch (const std::bad_alloc& error) {
    return fail_exception(RADICLE_STATUS_OUT_OF_MEMORY, error);
  } catch (const std::exception& error) {
    return fail_exception(RADICLE_STATUS_INTERNAL_ERROR, error);
  } catch (...) {
    return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown exception while reading .pex code");
  }
}

void radicle_pex_free_object_code(radicle_pex_object_code* code) {
  clear_error();
  if (!code) return;
  std::free(code->states);
  std::free(code->functions);
  std::free(code->typed_names);
  std::free(code->instructions);
  std::free(code->values);
  std::free(code->variables);
  std::free(code->properties);
  std::free(code->structs);
  std::free(code->struct_members);
  *code = radicle_pex_object_code{};
}

radicle_status radicle_pex_opcode_name(uint8_t opcode, uint8_t** out_bytes, size_t* out_size) {
  clear_error();
  if (!out_bytes || !out_size) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_bytes and out_size are required");
  }
  *out_bytes = nullptr;
  *out_size = 0;
  if (opcode > static_cast<uint8_t>(fo4::pex_analysis::PexOp::ArrayClear)) {
    char message[64];
    std::snprintf(message, sizeof(message), "opcode %u is outside the 0..46 name table",
                  static_cast<unsigned>(opcode));
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, message);
  }
  try {
    return copy_bytes(fo4::pex_analysis::pex_op_name(opcode), out_bytes, out_size);
  } catch (const std::bad_alloc& error) {
    return fail_exception(RADICLE_STATUS_OUT_OF_MEMORY, error);
  } catch (const std::exception& error) {
    return fail_exception(RADICLE_STATUS_INTERNAL_ERROR, error);
  } catch (...) {
    return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown exception while reading an opcode name");
  }
}

radicle_status radicle_script_normalize_name(const uint8_t* bytes,
                                             size_t size,
                                             uint8_t** out_bytes,
                                             size_t* out_size) {
  clear_error();
  if (!out_bytes || !out_size || (!bytes && size != 0)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "bytes, out_bytes and out_size are required");
  }
  *out_bytes = nullptr;
  *out_size = 0;
  try {
    return copy_bytes(fo4::pex_analysis::normalize_script_name(bytes_text(bytes, size)), out_bytes, out_size);
  } catch (const fo4::archive::AssetPathError& error) {
    return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
  } catch (const std::bad_alloc& error) {
    return fail_exception(RADICLE_STATUS_OUT_OF_MEMORY, error);
  } catch (const std::exception& error) {
    return fail_exception(RADICLE_STATUS_INTERNAL_ERROR, error);
  } catch (...) {
    return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown exception while normalizing a script name");
  }
}

radicle_status radicle_script_resolve(const radicle_script_candidate* candidates,
                                      size_t count,
                                      radicle_script_winner** out_winners,
                                      size_t* out_count) {
  clear_error();
  if (!out_winners || !out_count || (!candidates && count != 0)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "candidates, out_winners and out_count are required");
  }
  *out_winners = nullptr;
  *out_count = 0;
  try {
    std::vector<fo4::pex_analysis::ScriptCandidate> input;
    input.reserve(count);
    for (size_t i = 0; i < count; ++i) {
      const auto& item = candidates[i];
      if (item.kind > RADICLE_PEX_SOURCE_LOOSE || (!item.name && item.name_size != 0) ||
          (!item.container && item.container_size != 0)) {
        return fail(RADICLE_STATUS_INVALID_ARGUMENT, "script candidate needs a known kind and non-null strings");
      }
      // Invariant: a caller-built candidate has an empty path and location.
      fo4::pex_analysis::ScriptCandidate candidate;
      candidate.name = bytes_text(item.name, item.name_size);
      candidate.kind = static_cast<fo4::pex_analysis::ScriptSourceKind>(item.kind);
      candidate.container = bytes_text(item.container, item.container_size);
      candidate.load_index = item.load_index;
      candidate.size = item.size;
      input.push_back(std::move(candidate));
    }
    const auto winners = fo4::pex_analysis::resolve_script_set(input);
    if (winners.empty()) {
      return RADICLE_STATUS_OK;
    }
    auto* buffer = static_cast<radicle_script_winner*>(std::malloc(winners.size() * sizeof(radicle_script_winner)));
    if (!buffer) {
      return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate script winners");
    }
    for (size_t i = 0; i < winners.size(); ++i) {
      buffer[i] = radicle_script_winner{winners[i].index, winners[i].replaced};
    }
    *out_winners = buffer;
    *out_count = winners.size();
    return RADICLE_STATUS_OK;
  } catch (const std::bad_alloc& error) {
    return fail_exception(RADICLE_STATUS_OUT_OF_MEMORY, error);
  } catch (const std::exception& error) {
    return fail_exception(RADICLE_STATUS_INTERNAL_ERROR, error);
  } catch (...) {
    return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown exception while resolving script winners");
  }
}

void radicle_script_free_winners(radicle_script_winner* winners) {
  std::free(winners);
}

}
