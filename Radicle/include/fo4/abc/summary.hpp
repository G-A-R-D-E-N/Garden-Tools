#pragma once

#include <fo4/abc/abc.hpp>
#include <fo4/abc/opcodes.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fo4::abc {

enum class AbcArgumentKind : std::uint8_t {
  Unknown = 0,
  Literal,
  Named,
};

struct AbcCallArgument {
  AbcArgumentKind kind{AbcArgumentKind::Unknown};
  std::string value;
};

struct AbcCallSite {
  std::size_t offset{0};
  std::string opcode;
  std::string target;
  std::string receiver;
  std::uint32_t arg_count{0};
  std::vector<std::string> literals;
  bool literal_unambiguous{false};
  std::vector<AbcCallArgument> arguments;
  std::string event_name;
  std::string handler;
};

struct AbcMethodSummary {
  std::string name;
  std::string return_type;
  std::uint8_t flags{0};
  std::size_t instruction_count{0};
  std::vector<std::string> multinames;
  std::vector<AbcCallSite> calls;
};

struct AbcClassSummary {
  std::string name;
  std::string super_name;
  std::vector<std::string> interfaces;
  std::vector<AbcMethodSummary> methods;
};

struct AbcSummary {
  std::vector<AbcClassSummary> classes;
  std::vector<AbcMethodSummary> scripts;
  std::vector<AbcMethodSummary> functions;
};

bool is_event_registration(const AbcCallSite& site);
bool is_external_interface_call(const AbcCallSite& site);

AbcStatus summarize(const AbcFile& file, AbcSummary& out);

}
