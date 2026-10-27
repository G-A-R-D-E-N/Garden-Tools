#include "fo4/c_api_swf.h"

#include "c_api_env_internal.hpp"
#include "c_api_internal.hpp"
#include "c_api_swf_internal.hpp"
#include "fo4/abc/abc.hpp"
#include "fo4/abc/abc_asm.hpp"
#include "fo4/abc/abc_text.hpp"
#include "fo4/abc/edit.hpp"
#include "fo4/swf/swf.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using radicle_c_detail::clear_error;
using radicle_c_detail::fail;
using radicle_c_env_detail::guarded;

namespace {

using InstructionEdit = fo4::abc::AbcEditStatus (*)(fo4::abc::AbcFile&, std::uint32_t, std::size_t,
                                                    std::span<const std::uint8_t>, std::string&);

const radicle_swf_detail::abc_view* abc_at(const radicle_swf* swf, std::size_t index) {
  return swf == nullptr || index >= swf->abcs.size() ? nullptr : &swf->abcs[index];
}

/* Invariant: the buffer must carry a trailing NUL that *out_size does not count. */
std::uint8_t* copy_text(const std::string& text, std::size_t* out_size) {
  auto* buffer = static_cast<std::uint8_t*>(std::malloc(text.size() + 1));
  if (buffer == nullptr) return nullptr;
  if (!text.empty()) std::memcpy(buffer, text.data(), text.size());
  buffer[text.size()] = 0;
  out_size[0] = text.size();
  return buffer;
}

/* A byte buffer must carry the same trailing-NUL invariant as copy_text. */
std::uint8_t* copy_bytes(const std::vector<std::uint8_t>& bytes, std::size_t* out_size) {
  auto* buffer = static_cast<std::uint8_t*>(std::malloc(bytes.size() + 1));
  if (buffer == nullptr) return nullptr;
  if (!bytes.empty()) std::memcpy(buffer, bytes.data(), bytes.size());
  buffer[bytes.size()] = 0;
  out_size[0] = bytes.size();
  return buffer;
}

radicle_status check_block(const radicle_swf* swf, std::size_t abc, const std::uint8_t* text,
                           std::size_t size) {
  if (swf == nullptr) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "swf is required");
  if (text == nullptr || size == 0) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "text and size are required");
  }
  const auto* view = abc_at(swf, abc);
  if (view == nullptr) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the ABC index is out of range");
  if (!view->parsed) return fail(RADICLE_STATUS_PARSE_ERROR, "the ABC block did not parse");
  if (view->tag_index >= swf->tags.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the ABC tag index is out of range");
  }
  const std::uint16_t code = swf->tags[view->tag_index].tag->code;
  if (code != fo4::swf::tag::kDoAbc2 && code != fo4::swf::tag::kDoAbc) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the ABC block is not a DoABC or DoABC2 tag");
  }
  return RADICLE_STATUS_OK;
}

radicle_status apply_file(radicle_swf* swf, std::size_t abc, const fo4::abc::AbcFile& file) {
  std::vector<std::uint8_t> abc_bytes;
  if (file.serialize(abc_bytes) != fo4::abc::AbcStatus::Ok) {
    return fail(RADICLE_STATUS_PARSE_ERROR, "the assembled model does not serialize");
  }
  const auto* view = abc_at(swf, abc);
  const std::size_t tag_index = view->tag_index;
  const bool has_header = swf->tags[tag_index].tag->code == fo4::swf::tag::kDoAbc2;
  const std::uint32_t flags = view->flags;
  const std::string name = view->name;
  const std::size_t header_size = has_header ? 4 + name.size() + 1 : 0;
  std::vector<std::byte> payload(header_size + abc_bytes.size());
  std::byte* cursor = payload.data();
  if (has_header) {
    for (int index = 0; index < 4; ++index) {
      *cursor++ = static_cast<std::byte>((flags >> (8 * index)) & 0xFFu);
    }
    if (!name.empty()) std::memcpy(cursor, name.data(), name.size());
    cursor += name.size();
    *cursor++ = std::byte{0};
  }
  if (!abc_bytes.empty()) std::memcpy(cursor, abc_bytes.data(), abc_bytes.size());
  const radicle_swf_tag_replacement replacement{
      tag_index, reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size()};
  const radicle_swf_tag_plan plan{&replacement, 1, nullptr, 0, nullptr, 0};
  radicle_swf_plan_result result{};
  return radicle_swf_apply_plan(swf, &plan, &result);
}

// Requires before to be the block the handle holds; only bodies whose code bytes changed are measured.
radicle_status refresh_changed_bodies(const fo4::abc::AbcFile& before, fo4::abc::AbcFile& file) {
  for (std::uint32_t index = 0; index < file.bodies.size(); ++index) {
    const bool changed =
        index >= before.bodies.size() || before.bodies[index].code != file.bodies[index].code;
    if (!changed) continue;
    std::string message;
    if (fo4::abc::refresh_body_limits(file, index, message) != fo4::abc::AbcEditStatus::Ok) {
      return fail(RADICLE_STATUS_PARSE_ERROR, message.c_str());
    }
  }
  return RADICLE_STATUS_OK;
}

radicle_status apply_instruction_edit(radicle_swf* swf, std::size_t abc, std::uint32_t body,
                                      std::size_t offset, const std::uint8_t* text, std::size_t size,
                                      InstructionEdit edit) {
  clear_error();
  if (const radicle_status status = check_block(swf, abc, text, size); status != RADICLE_STATUS_OK) {
    return status;
  }
  return guarded("unknown exception while editing an ABC instruction", [&] {
    std::vector<std::uint8_t> instruction;
    fo4::abc::AbcTextError error;
    const std::string_view source(reinterpret_cast<const char*>(text), size);
    if (fo4::abc::assemble_instruction_text(source, instruction, &error) != fo4::abc::AbcStatus::Ok) {
      return fail(RADICLE_STATUS_PARSE_ERROR, error.message.c_str());
    }
    fo4::abc::AbcFile file = swf->abcs[abc].file;
    std::string message;
    if (edit(file, body, offset, instruction, message) != fo4::abc::AbcEditStatus::Ok) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, message.c_str());
    }
    return apply_file(swf, abc, file);
  });
}

}  // namespace

extern "C" {

radicle_status radicle_swf_abc_text(const radicle_swf* swf, size_t abc, uint8_t** out_text,
                                    size_t* out_size) {
  clear_error();
  if (out_text) *out_text = nullptr;
  if (out_size) *out_size = 0;
  if (out_text == nullptr || out_size == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_text and out_size are required");
  }
  const auto* view = abc_at(swf, abc);
  if (view == nullptr) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the ABC index is out of range");
  if (!view->parsed) return fail(RADICLE_STATUS_PARSE_ERROR, "the ABC block did not parse");
  return guarded("unknown exception while rendering ABC text", [&] {
    const std::string text = fo4::abc::render_abc_text(view->file);
    auto* buffer = copy_text(text, out_size);
    if (buffer == nullptr) return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate the text");
    out_text[0] = buffer;
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_swf_abc_assemble(const radicle_swf* swf, size_t abc, const uint8_t* text,
                                        size_t size, uint8_t** out_bytes, size_t* out_size) {
  clear_error();
  if (out_bytes) *out_bytes = nullptr;
  if (out_size) *out_size = 0;
  if (out_bytes == nullptr || out_size == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_bytes and out_size are required");
  }
  if (text == nullptr || size == 0) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "text and size are required");
  }
  const auto* view = abc_at(swf, abc);
  if (view == nullptr) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the ABC index is out of range");
  if (!view->parsed) return fail(RADICLE_STATUS_PARSE_ERROR, "the ABC block did not parse");
  return guarded("unknown exception while assembling ABC text", [&] {
    fo4::abc::AbcFile file;
    fo4::abc::AbcTextError error;
    const std::string_view source(reinterpret_cast<const char*>(text), size);
    if (fo4::abc::assemble_abc_text(source, file, &error) != fo4::abc::AbcStatus::Ok) {
      return fail(RADICLE_STATUS_PARSE_ERROR, error.message.c_str());
    }
    std::vector<std::uint8_t> bytes;
    if (file.serialize(bytes) != fo4::abc::AbcStatus::Ok) {
      return fail(RADICLE_STATUS_PARSE_ERROR, "the assembled model does not serialize");
    }
    auto* buffer = copy_bytes(bytes, out_size);
    if (buffer == nullptr) return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate the ABC bytes");
    out_bytes[0] = buffer;
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_swf_abc_apply_text(radicle_swf* swf, size_t abc, const uint8_t* text,
                                          size_t size) {
  clear_error();
  if (const radicle_status status = check_block(swf, abc, text, size); status != RADICLE_STATUS_OK) {
    return status;
  }
  const fo4::abc::AbcFile& before = swf->abcs[abc].file;
  return guarded("unknown exception while applying ABC text", [&] {
    fo4::abc::AbcFile file;
    fo4::abc::AbcTextError error;
    const std::string_view source(reinterpret_cast<const char*>(text), size);
    if (fo4::abc::assemble_abc_text(source, file, &error) != fo4::abc::AbcStatus::Ok) {
      return fail(RADICLE_STATUS_PARSE_ERROR, error.message.c_str());
    }
    if (const radicle_status status = refresh_changed_bodies(before, file);
        status != RADICLE_STATUS_OK) {
      return status;
    }
    return apply_file(swf, abc, file);
  });
}

radicle_status radicle_swf_abc_insert_instruction(radicle_swf* swf, size_t abc, uint32_t body,
                                                  size_t offset, const uint8_t* text, size_t size) {
  return apply_instruction_edit(swf, abc, body, offset, text, size, fo4::abc::insert_instruction);
}

radicle_status radicle_swf_abc_replace_instruction(radicle_swf* swf, size_t abc, uint32_t body,
                                                   size_t offset, const uint8_t* text, size_t size) {
  return apply_instruction_edit(swf, abc, body, offset, text, size, fo4::abc::replace_instruction);
}

}  // extern "C"
