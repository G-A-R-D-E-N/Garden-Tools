#pragma once

#include <fo4/abc/abc.hpp>
#include <fo4/abc/opcodes.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::abc::detail::text {

std::vector<std::string> split_tokens(std::string_view line);
std::string trim(std::string_view line);
bool unquote(std::string_view token, std::string& out);
bool parse_u32(std::string_view token, std::uint32_t& out);
bool parse_i32(std::string_view token, std::int32_t& out);
bool parse_double_bits(std::string_view token, double& out);
bool parse_ref(std::string_view token, std::uint32_t& out);

bool namespace_kind_from(std::string_view token, AbcNamespaceKind& out);
bool multiname_kind_from(std::string_view token, AbcMultinameKind& out);
bool trait_kind_from(std::string_view token, AbcTraitKind& out);
bool value_kind_from(std::string_view token, AbcValueKind& out);
std::uint8_t opcode_for(std::string_view mnemonic);

/* Grammar helpers must be shared by the record and body parsers. */
class Cursor {
 public:
  Cursor(std::vector<std::string> tokens, std::string& error) noexcept
      : tokens_(std::move(tokens)), error_(error) {}

  bool empty() const noexcept { return position_ >= tokens_.size(); }
  const std::string& peek() const noexcept { return tokens_[position_]; }
  bool take(std::string_view expected);
  bool skip(std::string_view keyword) noexcept;
  bool u32(std::uint32_t& out);
  bool i32(std::int32_t& out);
  bool ref(std::uint32_t& out);
  bool quoted(std::string& out);
  bool text(std::string_view keyword, std::uint32_t& out);
  bool invalid(std::string_view message);

 private:
  std::vector<std::string> tokens_;
  std::string& error_;
  std::size_t position_{0};
};

AbcStatus parse_trait(Cursor& cursor, AbcTrait& out);

/* The body parser must parse one body block with its code/exception/trait lines and its first line. */
AbcStatus parse_body(AbcFile& file, std::size_t body_index,
                     const std::vector<std::string>& block, std::size_t first_line,
                     std::string& error);

AbcStatus parse_instruction_text(std::string_view text, std::vector<std::uint8_t>& out,
                                 std::string& error);

}  // namespace fo4::abc::detail::text
