#pragma once

#include "abc_test_support.hpp"

#include <fo4/abc/abc.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace fo4::test {

inline constexpr std::uint8_t kOpNop = 0x02;
inline constexpr std::uint8_t kOpThrow = 0x03;
inline constexpr std::uint8_t kOpJump = 0x10;
inline constexpr std::uint8_t kOpIfTrue = 0x11;
inline constexpr std::uint8_t kOpIfFalse = 0x12;
inline constexpr std::uint8_t kOpPop = 0x29;
inline constexpr std::uint8_t kOpDup = 0x2A;
inline constexpr std::uint8_t kOpSwap = 0x2B;
inline constexpr std::uint8_t kOpPushScope = 0x30;
inline constexpr std::uint8_t kOpPopScope = 0x1D;
inline constexpr std::uint8_t kOpReturnVoid = 0x47;
inline constexpr std::uint8_t kOpReturnValue = 0x48;
inline constexpr std::uint8_t kOpNewCatch = 0x5A;
inline constexpr std::uint8_t kOpGetLocal = 0x62;
inline constexpr std::uint8_t kOpSetLocal = 0x63;
inline constexpr std::uint8_t kOpSetSlot = 0x6D;
inline constexpr std::uint8_t kOpKill = 0x08;
inline constexpr std::uint8_t kOpLabel = 0x09;
inline constexpr std::uint8_t kOpGetLex = 0x60;
inline constexpr std::uint8_t kOpGetLocal0 = 0xD0;
inline constexpr std::uint8_t kOpGetLocal1 = 0xD1;
inline constexpr std::uint8_t kOpGetLocal2 = 0xD2;
inline constexpr std::uint8_t kOpGetLocal3 = 0xD3;
inline constexpr std::uint8_t kOpSetLocal3 = 0xD7;

/* Invariant: labels resolve in bytes(), so a fixture never hand-counts a jump offset. */
class CodeBuilder {
public:
  CodeBuilder& op(std::uint8_t opcode) {
    put_u8(code_, opcode);
    return *this;
  }

  CodeBuilder& op_u30(std::uint8_t opcode, std::uint32_t value) {
    put_u8(code_, opcode);
    put_u30(code_, value);
    return *this;
  }

  CodeBuilder& op_u30_u30(std::uint8_t opcode, std::uint32_t first, std::uint32_t second) {
    put_u8(code_, opcode);
    put_u30(code_, first);
    put_u30(code_, second);
    return *this;
  }

  CodeBuilder& pushbyte(std::int32_t value) {
    put_u8(code_, 0x24);
    put_u8(code_, static_cast<std::uint32_t>(value) & 0xFFu);
    return *this;
  }

  CodeBuilder& stmt(std::int32_t value) { return pushbyte(value).op(kOpPop); }

  CodeBuilder& branch(std::uint8_t opcode, const std::string& target) {
    const std::size_t at = code_.size();
    put_u8(code_, opcode);
    fixups_.push_back({code_.size(), at + 4, target});
    put_s24(code_, 0);
    return *this;
  }

  CodeBuilder& label(const std::string& name) {
    labels_[name] = code_.size();
    return *this;
  }

  CodeBuilder& lookupswitch(const std::string& fallback, const std::vector<std::string>& cases) {
    const std::size_t at = code_.size();
    put_u8(code_, 0x1B);
    fixups_.push_back({code_.size(), at, fallback});
    put_s24(code_, 0);
    put_u30(code_, static_cast<std::uint32_t>(cases.size() - 1));
    for (const std::string& name : cases) {
      fixups_.push_back({code_.size(), at, name});
      put_s24(code_, 0);
    }
    return *this;
  }

  std::size_t offset_of(const std::string& name) const {
    const auto found = labels_.find(name);
    if (found == labels_.end()) {
      ADD_FAILURE() << "unknown label " << name;
      return 0;
    }
    return found->second;
  }

  std::vector<std::uint8_t> bytes() const {
    std::vector<std::uint8_t> out = code_;
    for (const Fixup& fixup : fixups_) {
      const std::int64_t relative = static_cast<std::int64_t>(offset_of(fixup.label)) -
                                    static_cast<std::int64_t>(fixup.base);
      const std::uint32_t raw = static_cast<std::uint32_t>(relative) & 0xFFFFFFu;
      out[fixup.at] = static_cast<std::uint8_t>(raw & 0xFFu);
      out[fixup.at + 1] = static_cast<std::uint8_t>((raw >> 8) & 0xFFu);
      out[fixup.at + 2] = static_cast<std::uint8_t>((raw >> 16) & 0xFFu);
    }
    return out;
  }

private:
  struct Fixup {
    std::size_t at;
    std::size_t base;
    std::string label;
  };

  std::vector<std::uint8_t> code_;
  std::vector<Fixup> fixups_;
  std::map<std::string, std::size_t> labels_;
};

inline fo4::abc::AbcFile decompile_fixture_file(const std::vector<std::uint8_t>& code,
                                                const std::vector<fo4::abc::AbcException>& exceptions = {}) {
  fo4::abc::AbcFile file;
  const std::vector<std::uint8_t> bytes = build_abc();
  EXPECT_EQ(fo4::abc::AbcFile::parse(bytes, file), fo4::abc::AbcStatus::Ok);
  file.bodies[0].code = code;
  file.bodies[0].max_stack = 16;
  file.bodies[0].local_count = 8;
  file.bodies[0].init_scope_depth = 0;
  file.bodies[0].max_scope_depth = 4;
  file.bodies[0].exceptions = exceptions;
  return file;
}

inline fo4::abc::AbcException make_exception(std::size_t from, std::size_t to, std::size_t target,
                                             std::uint32_t variable = kMultinameHelper) {
  fo4::abc::AbcException exception;
  exception.from = static_cast<std::uint32_t>(from);
  exception.to = static_cast<std::uint32_t>(to);
  exception.target = static_cast<std::uint32_t>(target);
  exception.exc_type = 0;
  exception.var_name = variable;
  return exception;
}

}  // namespace fo4::test
