#include "fo4/c_api_swf_as3.h"

#include "c_api_env_internal.hpp"
#include "c_api_internal.hpp"
#include "c_api_swf_internal.hpp"
#include "fo4/abc/decompile.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>

using radicle_c_detail::clear_error;
using radicle_c_detail::fail;
using radicle_c_env_detail::guarded;

struct radicle_swf_as3 {
  fo4::abc::AbcAs3Output output;
  std::size_t class_count{0};
};

extern "C" {

radicle_status radicle_swf_abc_decompile_ex(const radicle_swf* swf, size_t abc,
                                            radicle_swf_as3_language language,
                                            radicle_swf_as3_scope scope, uint32_t index,
                                            radicle_swf_as3** out_as3) {
  clear_error();
  if (out_as3) *out_as3 = nullptr;
  if (swf == nullptr || out_as3 == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "swf and out_as3 are required");
  }
  if (abc >= swf->abcs.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the ABC index is out of range");
  }
  if (language != RADICLE_SWF_AS3_LANGUAGE_AS3 && language != RADICLE_SWF_AS3_LANGUAGE_CSHARP) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the language is not a known value");
  }
  if (scope != RADICLE_SWF_AS3_SCOPE_FILE && scope != RADICLE_SWF_AS3_SCOPE_CLASS &&
      scope != RADICLE_SWF_AS3_SCOPE_METHOD) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the scope is not a known value");
  }
  const auto& view = swf->abcs[abc];
  if (!view.parsed) return fail(RADICLE_STATUS_PARSE_ERROR, "the ABC block did not parse");
  if (scope == RADICLE_SWF_AS3_SCOPE_CLASS && index >= view.file.instances.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the class index is out of range");
  }
  if (scope == RADICLE_SWF_AS3_SCOPE_METHOD && index >= view.file.methods.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the method index is out of range");
  }
  return guarded("unknown exception while decompiling ABC", [&] {
    std::unique_ptr<radicle_swf_as3> built(new radicle_swf_as3());
    const bool cs = language == RADICLE_SWF_AS3_LANGUAGE_CSHARP;
    if (scope == RADICLE_SWF_AS3_SCOPE_FILE) {
      built->output = cs ? fo4::abc::write_cs(view.file) : fo4::abc::write_as3(view.file);
    } else if (scope == RADICLE_SWF_AS3_SCOPE_CLASS) {
      built->output = cs ? fo4::abc::write_cs_class(view.file, index) : fo4::abc::write_as3_class(view.file, index);
    } else {
      built->output =
          cs ? fo4::abc::write_cs_method(view.file, index) : fo4::abc::write_as3_method(view.file, index);
    }
    built->class_count = view.file.instances.size();
    *out_as3 = built.release();
    return RADICLE_STATUS_OK;
  });
}

void radicle_swf_as3_destroy(radicle_swf_as3* as3) { delete as3; }

radicle_status radicle_swf_as3_get_info(const radicle_swf_as3* as3, radicle_swf_as3_info* out_info) {
  clear_error();
  if (out_info) *out_info = {};
  if (as3 == nullptr || out_info == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "as3 and out_info are required");
  }
  out_info->hash = as3->output.hash;
  out_info->text_size = as3->output.text.size();
  out_info->class_count = as3->class_count;
  out_info->function_count = as3->output.functions;
  out_info->refused_count = as3->output.refused.size();
  return RADICLE_STATUS_OK;
}

radicle_status radicle_swf_as3_text(const radicle_swf_as3* as3, const char** out_text,
                                    size_t* out_size) {
  clear_error();
  if (out_text) *out_text = nullptr;
  if (out_size) *out_size = 0;
  if (as3 == nullptr || out_text == nullptr || out_size == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "as3, out_text and out_size are required");
  }
  *out_text = as3->output.text.c_str();
  *out_size = as3->output.text.size();
  return RADICLE_STATUS_OK;
}

radicle_status radicle_swf_as3_issue_at(const radicle_swf_as3* as3, size_t index,
                                        radicle_swf_as3_issue* out_issue) {
  clear_error();
  if (out_issue) *out_issue = {};
  if (as3 == nullptr || out_issue == nullptr || index >= as3->output.refused.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                "as3, an in-range issue index and out_issue are required");
  }
  const auto& issue = as3->output.refused[index];
  out_issue->status = static_cast<std::uint8_t>(issue.status);
  out_issue->method = issue.method;
  out_issue->body = issue.body;
  out_issue->offset = issue.offset;
  out_issue->status_name = fo4::abc::decompile_status_name(issue.status);
  out_issue->message = issue.message.c_str();
  return RADICLE_STATUS_OK;
}

}  // extern "C"
