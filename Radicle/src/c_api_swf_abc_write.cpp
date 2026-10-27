#include "fo4/c_api_swf.h"

#include "c_api_env_internal.hpp"
#include "c_api_internal.hpp"
#include "c_api_swf_internal.hpp"
#include "fo4/abc/abc.hpp"
#include "fo4/abc/abc_asm.hpp"
#include "fo4/abc/abc_text.hpp"
#include "fo4/swf/swf.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

using radicle_c_detail::clear_error;
using radicle_c_detail::fail;
using radicle_c_env_detail::guarded;

namespace {

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
  if (swf == nullptr) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "swf is required");
  if (text == nullptr || size == 0) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "text and size are required");
  }
  const auto* view = abc_at(swf, abc);
  if (view == nullptr) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the ABC index is out of range");
  if (!view->parsed) return fail(RADICLE_STATUS_PARSE_ERROR, "the ABC block did not parse");
  const std::size_t tag_index = view->tag_index;
  const std::uint32_t flags = view->flags;
  const std::string name = view->name;
  if (tag_index >= swf->file.tags.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the ABC tag index is out of range");
  }
  const std::uint16_t code = swf->file.tags[tag_index].code;
  if (code != fo4::swf::tag::kDoAbc2 && code != fo4::swf::tag::kDoAbc) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the ABC block is not a DoABC or DoABC2 tag");
  }
  return guarded("unknown exception while applying ABC text", [&] {
    fo4::abc::AbcFile file;
    fo4::abc::AbcTextError error;
    const std::string_view source(reinterpret_cast<const char*>(text), size);
    if (fo4::abc::assemble_abc_text(source, file, &error) != fo4::abc::AbcStatus::Ok) {
      return fail(RADICLE_STATUS_PARSE_ERROR, error.message.c_str());
    }
    std::vector<std::uint8_t> abc_bytes;
    if (file.serialize(abc_bytes) != fo4::abc::AbcStatus::Ok) {
      return fail(RADICLE_STATUS_PARSE_ERROR, "the assembled model does not serialize");
    }
    std::vector<std::byte> payload(4 + name.size() + 1 + abc_bytes.size());
    std::byte* cursor = payload.data();
    for (int index = 0; index < 4; ++index) {
      *cursor++ = static_cast<std::byte>((flags >> (8 * index)) & 0xFFu);
    }
    if (!name.empty()) std::memcpy(cursor, name.data(), name.size());
    cursor += name.size();
    *cursor++ = std::byte{0};
    if (!abc_bytes.empty()) std::memcpy(cursor, abc_bytes.data(), abc_bytes.size());
    const radicle_swf_tag_replacement replacement{
        tag_index, reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size()};
    const radicle_swf_tag_plan plan{&replacement, 1, nullptr, 0, nullptr, 0};
    radicle_swf_plan_result result{};
    return radicle_swf_apply_plan(swf, &plan, &result);
  });
}

}  // extern "C"
