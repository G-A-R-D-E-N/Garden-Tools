#include "fo4/archive/assets.hpp"
#include "fo4/archive/ba2.hpp"
#include "fo4/c_api.h"
#include "fo4/c_api_pex.h"
#include "fo4/pex/pex.hpp"
#include "real_data_build.hpp"
#include "real_data_plugins.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace {

#ifdef _WIN32
constexpr char kRootSeparator = ';';
#else
constexpr char kRootSeparator = ':';
#endif

std::vector<std::string> splitRoots(const std::string& value) {
  std::vector<std::string> roots;
  std::string current;
  for (const char ch : value) {
    if (ch == kRootSeparator) {
      if (!current.empty()) roots.push_back(current);
      current.clear();
      continue;
    }
    current.push_back(ch);
  }
  if (!current.empty()) roots.push_back(current);
  return roots;
}

std::string lowerExtension(const std::filesystem::path& path) {
  std::string extension = path.extension().string();
  for (char& ch : extension) {
    if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
  }
  return extension;
}

/* Collapse the variable parts of a message so distinct causes group together,
   the same way the plugin corpus harness does. */
std::string normalize(std::string message) {
  std::string out;
  bool in_number = false;
  for (const char ch : message) {
    const bool digit = ch >= '0' && ch <= '9';
    const bool hex_body = in_number && ((ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F') || ch == 'x');
    if (digit || hex_body) {
      if (!in_number) {
        out += '#';
        in_number = true;
      }
      continue;
    }
    in_number = false;
    out += ch;
  }
  return out;
}

struct Tally {
  std::size_t found{0};
  std::size_t read{0};
  std::size_t identical{0};
  std::size_t unreadable{0};
  std::size_t mismatched{0};
  std::size_t codeMismatched{0};
  std::size_t objectsChecked{0};
  std::size_t functionsChecked{0};
  std::size_t instructionsChecked{0};
  std::size_t operandsChecked{0};
  std::map<std::string, std::size_t> causes;
  std::map<std::string, std::string> firstExample;

  void note(const std::string& message, const std::string& where) {
    const auto key = normalize(message);
    if (!causes.count(key)) firstExample[key] = where + ": " + message;
    causes[key]++;
  }
};

std::vector<std::filesystem::path> collectLoosePex(const std::vector<std::string>& roots) {
  std::vector<std::filesystem::path> found;
  for (const auto& root : roots) {
    std::error_code error;
    const std::filesystem::path base(root);
    if (!std::filesystem::is_directory(base, error)) continue;
    for (std::filesystem::recursive_directory_iterator it(base, error), end; it != end; it.increment(error)) {
      if (error) break;
      if (!it->is_regular_file(error)) continue;
      if (lowerExtension(it->path()) == ".pex") found.push_back(it->path());
    }
  }
  std::sort(found.begin(), found.end());
  return found;
}

std::vector<std::uint8_t> readBytes(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) throw std::runtime_error("cannot open " + path.string());
  const std::vector<char> raw((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
  return std::vector<std::uint8_t>(raw.begin(), raw.end());
}

std::vector<std::uint8_t> unpack(const std::vector<std::byte>& raw) {
  std::vector<std::uint8_t> bytes;
  bytes.reserve(raw.size());
  for (const std::byte byte : raw) bytes.push_back(std::to_integer<std::uint8_t>(byte));
  return bytes;
}

void compareCode(const fo4::pex::PexObject& object, const std::vector<std::string>& strings,
                 const radicle_pex_object_code& code) {
  std::string context = "object " + object.name;
  const auto check = [&](bool equal, const std::string& field) {
    if (!equal) throw std::runtime_error(context + ": " + field + " differs from the model");
  };
  std::unordered_map<std::string, size_t> indexes;
  for (size_t i = 0; i < strings.size(); ++i) indexes.emplace(strings[i], i);
  const auto text = [&](size_t actual, const std::string& expected, const std::string& field) {
    const auto found = indexes.find(expected);
    check(found != indexes.end() || expected.empty(), field + " absent from string table");
    check(actual == (found == indexes.end() ? RADICLE_PEX_NO_STRING : found->second), field);
  };
  const auto value = [&](const radicle_pex_value& actual, const fo4::pex::PexValue& expected) {
    using fo4::pex::PexValueType;
    check(actual.type == static_cast<uint8_t>(expected.type), "value.type");
    check(actual.integer == (expected.type == PexValueType::Integer ? expected.int_ : 0), "value.integer");
    check(std::bit_cast<uint32_t>(actual.real) == std::bit_cast<uint32_t>(
              expected.type == PexValueType::Float ? expected.float_ : 0.0F), "value.real bits");
    check(actual.boolean == (expected.type == PexValueType::Bool && expected.bool_ ? 1 : 0), "value.boolean");
    if (expected.type == PexValueType::Identifier || expected.type == PexValueType::String) {
      text(actual.string, expected.str, "value.string");
    } else {
      check(actual.string == RADICLE_PEX_NO_STRING, "value.string sentinel");
    }
  };
  const auto variable = [&](const radicle_pex_variable& actual, const auto& expected) {
    text(actual.name, expected.name, "variable.name");
    text(actual.type, expected.type, "variable.type");
    check(actual.user_flags == expected.userFlags, "variable.user_flags");
    check(actual.is_const == (expected.isConst ? 1 : 0), "variable.is_const");
    check(actual.has_default == (expected.defaultValue ? 1 : 0), "variable.has_default");
    value(actual.default_value, expected.defaultValue.value_or(fo4::pex::PexValue{}));
  };
  size_t functions = 0, names = 0, instructions = 0, arguments = 0, members = 0;
  const auto countFunction = [&](const fo4::pex::PexFunction& function) {
    ++functions;
    names += function.params.size() + function.locals.size();
    instructions += function.instructions.size();
    for (const auto& instruction : function.instructions) arguments += instruction.args.size();
  };
  for (const auto& state : object.states) {
    for (const auto& function : state.functions) countFunction(function);
  }
  for (const auto& property : object.properties) {
    if (property.readHandler) countFunction(*property.readHandler);
    if (property.writeHandler) countFunction(*property.writeHandler);
  }
  for (const auto& row : object.structs) members += row.members.size();
  check(code.state_count == object.states.size() && (code.states || code.state_count == 0), "states array");
  check(code.function_count == functions && (code.functions || functions == 0), "functions array");
  check(code.typed_name_count == names && (code.typed_names || names == 0), "typed_names array");
  check(code.instruction_count == instructions && (code.instructions || instructions == 0), "instructions array");
  check(code.value_count == arguments && (code.values || arguments == 0), "values array");
  check(code.variable_count == object.variables.size() && (code.variables || code.variable_count == 0), "variables array");
  check(code.property_count == object.properties.size() && (code.properties || code.property_count == 0), "properties array");
  check(code.struct_count == object.structs.size() && (code.structs || code.struct_count == 0), "structs array");
  check(code.struct_member_count == members && (code.struct_members || members == 0), "struct_members array");
  text(code.auto_state_name, object.autoStateName, "auto_state_name");

  size_t functionAt = 0, nameAt = 0, instructionAt = 0, argumentAt = 0;
  const auto function = [&](const fo4::pex::PexFunction& expected, size_t state) {
    context = "object " + object.name + "/function " + std::to_string(functionAt) + " " + expected.name;
    const auto& actual = code.functions[functionAt++];
    check(actual.state == state, "function.state");
    text(actual.name, expected.name, "function.name");
    text(actual.return_type, expected.returnType, "function.return_type");
    check(actual.user_flags == expected.userFlags, "function.user_flags");
    check(actual.is_global == (expected.isGlobal ? 1 : 0), "function.is_global");
    check(actual.is_native == (expected.isNative ? 1 : 0), "function.is_native");
    const auto typedNames = [&](size_t first, size_t count, const auto& expectedNames, const char* field) {
      check(first == nameAt && count == expectedNames.size(), field);
      for (const auto& name : expectedNames) {
        const auto& row = code.typed_names[nameAt++];
        text(row.name, name.name, std::string(field) + ".name");
        text(row.type, name.type, std::string(field) + ".type");
      }
    };
    typedNames(actual.first_parameter, actual.parameter_count, expected.params, "parameters");
    typedNames(actual.first_local, actual.local_count, expected.locals, "locals");
    check(actual.first_instruction == instructionAt && actual.instruction_count == expected.instructions.size(), "function.instructions");
    const auto functionContext = context;
    for (const auto& instruction : expected.instructions) {
      context = functionContext + "/instruction " + std::to_string(instructionAt);
      const auto& row = code.instructions[instructionAt++];
      check(row.opcode == instruction.opCode, "instruction.opcode");
      check(row.line == instruction.line, "instruction.line");
      check(row.first_argument == argumentAt && row.argument_count == instruction.args.size(), "instruction.arguments");
      for (const auto& argument : instruction.args) {
        context = functionContext + "/instruction " + std::to_string(instructionAt - 1) +
                  "/operand " + std::to_string(argumentAt - row.first_argument);
        value(code.values[argumentAt++], argument);
      }
    }
  };
  for (size_t i = 0; i < object.states.size(); ++i) {
    context = "object " + object.name + "/state " + std::to_string(i);
    const auto& expected = object.states[i];
    const auto& actual = code.states[i];
    text(actual.name, expected.name, "state.name");
    check(actual.first_function == functionAt && actual.function_count == expected.functions.size(), "state.functions");
    for (const auto& row : expected.functions) function(row, i);
  }
  for (size_t i = 0; i < object.properties.size(); ++i) {
    const auto& expected = object.properties[i];
    const auto& actual = code.properties[i];
    context = "object " + object.name + "/property " + std::to_string(i) + " " + expected.name;
    text(actual.name, expected.name, "property.name");
    text(actual.type, expected.type, "property.type");
    check(actual.user_flags == expected.userFlags, "property.user_flags");
    check(actual.flags == expected.flags, "property.flags");
    text(actual.auto_var_name, expected.autoVarName, "property.auto_var_name");
    check(actual.read_function == (expected.readHandler ? functionAt : RADICLE_PEX_NO_INDEX), "property.read_function");
    if (expected.readHandler) function(*expected.readHandler, RADICLE_PEX_NO_INDEX);
    context = "object " + object.name + "/property " + std::to_string(i) + " " + expected.name;
    check(actual.write_function == (expected.writeHandler ? functionAt : RADICLE_PEX_NO_INDEX), "property.write_function");
    if (expected.writeHandler) function(*expected.writeHandler, RADICLE_PEX_NO_INDEX);
  }
  for (size_t i = 0; i < object.variables.size(); ++i) {
    context = "object " + object.name + "/variable " + std::to_string(i);
    variable(code.variables[i], object.variables[i]);
  }
  size_t memberAt = 0;
  for (size_t i = 0; i < object.structs.size(); ++i) {
    context = "object " + object.name + "/struct " + std::to_string(i);
    const auto& expected = object.structs[i];
    const auto& actual = code.structs[i];
    text(actual.name, expected.name, "struct.name");
    check(actual.first_member == memberAt && actual.member_count == expected.members.size(), "struct.members");
    for (const auto& member : expected.members) {
      context = "object " + object.name + "/struct " + std::to_string(i) + "/member " + std::to_string(memberAt);
      variable(code.struct_members[memberAt++], member);
    }
  }
}

void checkCode(const std::vector<std::uint8_t>& bytes, const std::string& where, Tally& tally) {
  const auto model = fo4::pex::PexFile::fromBytes(bytes);
  radicle_pex* pex = nullptr;
  if (radicle_pex_parse(bytes.data(), bytes.size(), &pex) != RADICLE_STATUS_OK) {
    tally.codeMismatched++;
    tally.note("the C ABI could not parse a file the model read", where);
    return;
  }
  const std::unique_ptr<radicle_pex, decltype(&radicle_pex_destroy)> owner(pex, radicle_pex_destroy);
  for (size_t index = 0; index < model.objects.size(); ++index) {
    radicle_pex_object_code code{};
    if (radicle_pex_object_code_get(pex, index, &code) != RADICLE_STATUS_OK) {
      tally.codeMismatched++;
      tally.note(std::string("object code failed: ") + radicle_last_error(), where);
      continue;
    }
    try {
      compareCode(model.objects[index], model.stringTable, code);
      ++tally.objectsChecked;
      tally.functionsChecked += code.function_count;
      tally.instructionsChecked += code.instruction_count;
      tally.operandsChecked += code.value_count;
    } catch (const std::exception& error) {
      tally.codeMismatched++;
      const auto location = where + "/object " + std::to_string(index);
      tally.note(error.what(), location);
      std::cerr << location << ": " << error.what() << std::endl;
    }
    radicle_pex_free_object_code(&code);
  }
}

void roundTrip(const std::vector<std::uint8_t>& bytes, const std::string& where, Tally& tally) {
  tally.found++;
  std::vector<std::uint8_t> rewritten;
  try {
    rewritten = fo4::pex::PexFile::fromBytes(bytes).toBytes();
  } catch (const std::exception& error) {
    tally.unreadable++;
    tally.note(error.what(), where);
    return;
  }
  tally.read++;
  checkCode(bytes, where, tally);
  if (rewritten == bytes) {
    tally.identical++;
    return;
  }
  tally.mismatched++;
  std::size_t offset = 0;
  while (offset < rewritten.size() && offset < bytes.size() && rewritten[offset] == bytes[offset]) ++offset;
  tally.note("round trip differs at offset " + std::to_string(offset) + " of " +
                 std::to_string(bytes.size()) + " bytes",
             where);
}

void report(const Tally& tally) {
  std::cout << "pex corpus: found=" << tally.found << " read=" << tally.read
            << " identical=" << tally.identical << " unreadable=" << tally.unreadable
            << " mismatched=" << tally.mismatched << " code-mismatched=" << tally.codeMismatched
            << " objects-checked=" << tally.objectsChecked << " functions-checked=" << tally.functionsChecked
            << " instructions-checked=" << tally.instructionsChecked << " operands-checked=" << tally.operandsChecked << std::endl;
  for (const auto& entry : tally.firstExample) {
    std::cout << "  class(" << tally.causes.at(entry.first) << "): " << entry.second << std::endl;
  }
}

void expectClean(const Tally& tally) {
  EXPECT_EQ(tally.read + tally.unreadable, tally.found) << "every found file must be read or counted";
  EXPECT_EQ(tally.unreadable, 0u) << "files that failed to read";
  EXPECT_EQ(tally.mismatched, 0u) << "files that did not round trip byte-identically";
  EXPECT_EQ(tally.codeMismatched, 0u) << "files whose C ABI code view disagrees with the model";
  EXPECT_EQ(tally.identical, tally.found) << "byte-identical round trips";
}

}

TEST(PexCorpus, FullCodeComparisonRejectsFieldChangesWithoutCountChanges) {
  using namespace fo4::pex;
  PexFile source;
  source.majorVersion = 3;
  source.minorVersion = 9;
  source.gameId = 2;
  PexObject object;
  object.name = "Comparison";
  PexFunction run;
  run.name = "Run";
  run.returnType = "Float";
  run.userFlags = 0x80;
  run.isGlobal = true;
  run.params.push_back({"arg", "Float"});
  run.locals.push_back({"local", "Int"});
  PexInstruction instruction;
  instruction.opCode = 25;
  for (const auto& name : {"Run", "Run", "local"}) {
    PexValue value;
    value.type = PexValueType::Identifier;
    value.str = name;
    instruction.args.push_back(value);
  }
  PexValue real;
  real.type = PexValueType::Float;
  real.float_ = -0.0F;
  instruction.args.push_back(real);
  PexValue integer;
  integer.type = PexValueType::Integer;
  integer.int_ = -7;
  instruction.args.push_back(integer);
  PexValue boolean;
  boolean.type = PexValueType::Bool;
  boolean.bool_ = true;
  instruction.args.push_back(boolean);
  PexValue string;
  string.type = PexValueType::String;
  string.str = "payload";
  instruction.args.push_back(string);
  instruction.args.push_back(PexValue{});
  real.float_ = std::bit_cast<float>(uint32_t{0x7FC00001});
  instruction.args.push_back(real);
  run.instructions.push_back(instruction);
  object.states.push_back({"", {run}});
  object.states.push_back({"Waiting", {}});
  PexProperty property;
  property.name = "P";
  property.type = "Float";
  property.userFlags = 4;
  property.flags = 3;
  property.readHandler = PexFunction{};
  property.writeHandler = PexFunction{};
  property.writeHandler->params.push_back({"arg", "Float"});
  object.properties.push_back(property);
  property.name = "Auto";
  property.flags = 7;
  property.autoVarName = "var";
  property.readHandler.reset();
  property.writeHandler.reset();
  object.properties.push_back(property);
  object.variables.push_back({"var", "Float", 5, real, true});
  real.float_ = -0.0F;
  object.structs.push_back({"S", {{"member", "Float", 6, real, true, ""}}});
  source.objects.push_back(object);
  source.hasDebugInfo = true;
  source.debugFunctions.push_back({object.name, "", run.name, 0, {17}});
  source.rebuildStringTable();
  const auto bytes = source.toBytes();
  const auto model = PexFile::fromBytes(bytes);
  radicle_pex* pex = nullptr;
  ASSERT_EQ(radicle_pex_parse(bytes.data(), bytes.size(), &pex), RADICLE_STATUS_OK);
  const std::unique_ptr<radicle_pex, decltype(&radicle_pex_destroy)> owner(pex, radicle_pex_destroy);
  radicle_pex_object_code code{};
  ASSERT_EQ(radicle_pex_object_code_get(pex, 0, &code), RADICLE_STATUS_OK);
  const std::unique_ptr<radicle_pex_object_code, decltype(&radicle_pex_free_object_code)> cleanup(&code, radicle_pex_free_object_code);
  ASSERT_NO_THROW(compareCode(model.objects[0], model.stringTable, code));
  ASSERT_EQ(code.instructions[0].line, 17);
  const auto reject = [&](auto& field, const auto changed, const char* diagnostic) {
    const auto saved = field;
    field = static_cast<std::remove_reference_t<decltype(field)>>(changed);
    try {
      compareCode(model.objects[0], model.stringTable, code);
      ADD_FAILURE() << "accepted changed " << diagnostic;
    } catch (const std::runtime_error& error) {
      EXPECT_NE(std::string(error.what()).find(diagnostic), std::string::npos) << error.what();
      EXPECT_NE(std::string(error.what()).find("object Comparison"), std::string::npos);
    }
    field = saved;
  };
  reject(code.auto_state_name, RADICLE_PEX_NO_STRING, "auto_state_name");
  reject(code.states[0].name, RADICLE_PEX_NO_STRING, "state.name");
  reject(code.states[0].first_function, 1, "state.functions");
  reject(code.states[0].function_count, 0, "state.functions");
  auto& function = code.functions[0];
  reject(function.state, RADICLE_PEX_NO_INDEX, "function.state");
  reject(function.name, RADICLE_PEX_NO_STRING, "function.name");
  reject(function.return_type, RADICLE_PEX_NO_STRING, "function.return_type");
  reject(function.user_flags, 0, "function.user_flags");
  reject(function.is_global, 0, "function.is_global");
  reject(function.is_native, 1, "function.is_native");
  reject(function.first_parameter, 1, "parameters");
  reject(function.parameter_count, 0, "parameters");
  reject(code.typed_names[0].name, RADICLE_PEX_NO_STRING, "parameters.name");
  reject(code.typed_names[0].type, RADICLE_PEX_NO_STRING, "parameters.type");
  reject(function.first_local, 0, "locals");
  reject(function.local_count, 0, "locals");
  reject(code.typed_names[1].name, RADICLE_PEX_NO_STRING, "locals.name");
  reject(code.typed_names[1].type, RADICLE_PEX_NO_STRING, "locals.type");
  reject(function.first_instruction, 1, "function.instructions");
  reject(function.instruction_count, 0, "function.instructions");
  reject(code.instructions[0].opcode, 0, "instruction.opcode");
  reject(code.instructions[0].line, 0, "instruction.line");
  reject(code.instructions[0].first_argument, 1, "instruction.arguments");
  reject(code.instructions[0].argument_count, 0, "instruction.arguments");
  reject(code.values[0].type, 0, "value.type");
  reject(code.values[0].string, RADICLE_PEX_NO_STRING, "value.string");
  reject(code.values[3].real, 0.0F, "value.real bits");
  reject(code.values[4].integer, -6, "value.integer");
  reject(code.values[5].boolean, 0, "value.boolean");
  reject(code.values[6].string, RADICLE_PEX_NO_STRING, "value.string");
  reject(code.values[7].string, 0, "value.string sentinel");
  reject(code.values[8].real, std::bit_cast<float>(uint32_t{0x7FC00002}), "value.real bits");
  auto& variable = code.variables[0];
  reject(variable.name, RADICLE_PEX_NO_STRING, "variable.name");
  reject(variable.type, RADICLE_PEX_NO_STRING, "variable.type");
  reject(variable.user_flags, 0, "variable.user_flags");
  reject(variable.is_const, 0, "variable.is_const");
  reject(variable.has_default, 0, "variable.has_default");
  reject(variable.default_value.real, 0.0F, "value.real bits");
  auto& handler = code.properties[0];
  reject(handler.name, RADICLE_PEX_NO_STRING, "property.name");
  reject(handler.type, RADICLE_PEX_NO_STRING, "property.type");
  reject(handler.user_flags, 0, "property.user_flags");
  reject(handler.flags, 0, "property.flags");
  reject(handler.read_function, RADICLE_PEX_NO_INDEX, "property.read_function");
  reject(handler.write_function, RADICLE_PEX_NO_INDEX, "property.write_function");
  reject(code.properties[1].auto_var_name, RADICLE_PEX_NO_STRING, "property.auto_var_name");
  reject(code.functions[1].state, 0, "function.state");
  reject(code.structs[0].name, RADICLE_PEX_NO_STRING, "struct.name");
  reject(code.structs[0].first_member, 1, "struct.members");
  reject(code.structs[0].member_count, 0, "struct.members");
  reject(code.struct_members[0].name, RADICLE_PEX_NO_STRING, "variable.name");
  reject(code.struct_members[0].default_value.real, 0.0F, "value.real bits");
  ASSERT_NO_THROW(compareCode(model.objects[0], model.stringTable, code));
}

TEST(PexCorpus, EveryLoosePexRoundTripsByteIdentically) {
  const char* roots = std::getenv("FO4_PEX_CORPUS");
  if (roots == nullptr) {
    GTEST_SKIP() << "set FO4_PEX_CORPUS to one or more directories of .pex files";
  }
  const auto files = collectLoosePex(splitRoots(roots));
  if (files.empty()) GTEST_SKIP() << "no .pex files under " << roots;

  Tally tally;
  for (const auto& file : files) {
    try {
      roundTrip(readBytes(file), file.string(), tally);
    } catch (const std::exception& error) {
      tally.found++;
      tally.unreadable++;
      tally.note(error.what(), file.string());
    }
  }
  report(tally);
  expectClean(tally);
}

TEST(PexCorpus, EveryArchivedPexRoundTripsByteIdentically) {
  const char* archives = std::getenv("FO4_PEX_ARCHIVES");
  if (archives == nullptr) {
    GTEST_SKIP() << "set FO4_PEX_ARCHIVES to .ba2 files holding Scripts/*.pex";
  }

  Tally tally;
  for (const auto& root : splitRoots(archives)) {
    const std::filesystem::path path(root);
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) continue;

    std::vector<fo4::archive::Ba2GeneralEntry> entries;
    try {
      entries = fo4::archive::read_ba2_general_file(path).entries;
    } catch (const std::exception& read_error) {
      tally.note(read_error.what(), path.filename().string());
      continue;
    }

    for (const auto& entry : entries) {
      if (lowerExtension(entry.path) != ".pex") continue;
      const std::string where = path.filename().string() + "!" + entry.path;
      try {
        roundTrip(unpack(fo4::archive::extract_ba2_general_entry(entry)), where, tally);
      } catch (const std::exception& error) {
        tally.found++;
        tally.unreadable++;
        tally.note(error.what(), where);
      }
    }
  }

  report(tally);
  ASSERT_GT(tally.found, 0u) << "no .pex entries found in " << archives;
  expectClean(tally);
}

TEST(PexCorpus, EveryDataDirArchivePexRoundTripsByteIdentically) {
  if (std::getenv("FO4_PEX_CORPUS") != nullptr || std::getenv("FO4_PEX_ARCHIVES") != nullptr) {
    GTEST_SKIP() << "FO4_PEX_CORPUS or FO4_PEX_ARCHIVES is set; the explicit inputs above run instead";
  }
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder to scan its archives for .pex";
  record_game_build(detect_game_build(dir));
  const auto archives = fo4::archive::archive_load_order(dir, {}, installed_master_and_cc_plugins(dir));
  if (archives.empty()) GTEST_SKIP() << "no Fallout4 - *.ba2 archives under " << dir;

  const auto assets = fo4::archive::AssetResolver::open({}, archives);
  Tally tally;
  for (const auto& reader : assets.archives()) {
    if (reader.kind() != fo4::archive::Ba2Kind::general) continue;
    const auto found_start = tally.found;
    const auto identical_start = tally.identical;
    for (std::size_t i = 0; i < reader.entries().size(); ++i) {
      const auto& entry = reader.entries()[i].path;
      if (lowerExtension(entry) != ".pex") continue;
      const std::string where = reader.path().filename().string() + "!" + entry;
      try {
        roundTrip(unpack(reader.extract(i)), where, tally);
      } catch (const std::exception& error) {
        tally.found++;
        tally.unreadable++;
        tally.note(error.what(), where);
      }
    }
    RecordProperty(reader.path().filename().string() + " pex_found", std::to_string(tally.found - found_start));
    RecordProperty(reader.path().filename().string() + " pex_identical",
                   std::to_string(tally.identical - identical_start));
  }

  report(tally);
  ASSERT_GT(tally.found, 0u) << "no .pex entries found under " << dir;
  expectClean(tally);
}
