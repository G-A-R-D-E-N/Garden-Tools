#include "fo4/c_api_swf.h"

#include "c_api_env_internal.hpp"
#include "c_api_internal.hpp"
#include "c_api_swf_internal.hpp"
#include "fo4/abc/opcodes.hpp"
#include "fo4/abc/summary.hpp"
#include "fo4/swf/swf.hpp"

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <new>
#include <span>
#include <string>
#include <utility>
#include <vector>

using radicle_c_detail::clear_error;
using radicle_c_detail::fail;
using radicle_c_detail::fail_exception;
using radicle_c_detail::utf8_path;
using radicle_c_env_detail::guarded;

using radicle_swf_detail::abc_view;
using radicle_swf_detail::tag_view;

namespace {

radicle_status map_swf_status(fo4::swf::SwfStatus status, const std::string& message) {
  switch (status) {
    case fo4::swf::SwfStatus::ok:
      return RADICLE_STATUS_OK;
    case fo4::swf::SwfStatus::empty_input:
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the movie bytes are empty");
    case fo4::swf::SwfStatus::out_of_memory:
      return fail(RADICLE_STATUS_OUT_OF_MEMORY, message.empty() ? "out of memory while reading the movie" : message.c_str());
    default:
      break;
  }
  return fail(RADICLE_STATUS_PARSE_ERROR, message.empty() ? "the movie did not parse" : message.c_str());
}

std::string null_terminated_string(std::span<const std::byte> payload) {
  std::string text;
  for (const auto byte : payload) {
    const auto value = static_cast<unsigned char>(byte);
    if (value == 0) break;
    text.push_back(static_cast<char>(value));
  }
  return text;
}

void flatten_tags(const fo4::swf::SwfTag& tag,
                  std::size_t parent,
                  std::uint16_t sprite_id,
                  bool inside_sprite,
                  radicle_swf_detail::swf_views& views) {
  const auto index = views.tags.size();
  // A DefineSprite tag must report the id its children carry, not only its parent's.
  const auto own_sprite = tag.sprite_id.has_value() ? *tag.sprite_id : sprite_id;
  views.tags.push_back(tag_view{&tag, parent, own_sprite, inside_sprite});
  const auto child_sprite = own_sprite;
  const auto child_inside = tag.sprite_id.has_value() ? true : inside_sprite;
  for (const auto& child : tag.children) {
    flatten_tags(child, index, child_sprite, child_inside, views);
  }
}

void collect_records(radicle_swf_detail::swf_views& views) {
  for (std::size_t index = 0; index < views.tags.size(); ++index) {
    const auto& view = views.tags[index];
    const auto& tag = *view.tag;
    if (tag.symbols) {
      views.symbols.insert(views.symbols.end(), tag.symbols->begin(), tag.symbols->end());
    }
    if (tag.exports) {
      views.exports.insert(views.exports.end(), tag.exports->begin(), tag.exports->end());
    }
    if (tag.code == fo4::swf::tag::kFrameLabel) {
      views.frame_labels.push_back(null_terminated_string(tag.payload));
      views.frame_label_tags.push_back(index);
      views.frame_label_sprites.push_back(view.sprite_id);
      views.frame_label_inside_sprite.push_back(view.inside_sprite ? 1 : 0);
    }
  }
}

void build_abc_views(const fo4::swf::SwfFile& file, radicle_swf_detail::swf_views& views) {
  // abc_blocks indexes root tags, so each block must report the flat index radicle_swf_tag_at takes.
  std::vector<std::size_t> root_flat_index;
  for (std::size_t index = 0; index < views.tags.size(); ++index) {
    if (views.tags[index].parent == RADICLE_SWF_NO_INDEX) root_flat_index.push_back(index);
  }
  for (const auto& block : fo4::swf::abc_blocks(file)) {
    const auto payload = block.payload;
    const auto flat_index =
        block.tag_index < root_flat_index.size() ? root_flat_index[block.tag_index] : block.tag_index;
    abc_view view;
    view.name = block.name;
    view.flags = block.flags;
    view.tag_index = flat_index;
    view.payload_size = payload.size();
    const auto status = fo4::abc::AbcFile::parse(
        reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size(), view.file);
    view.parsed = status == fo4::abc::AbcStatus::Ok;
    if (view.parsed) {
      // A block that fails to summarize must keep an empty summary; parsed stays 1.
      if (fo4::abc::summarize(view.file, view.summary) != fo4::abc::AbcStatus::Ok) view.summary = {};
      for (std::uint32_t index = 0; index < view.file.methods.size(); ++index) {
        view.method_names.push_back(view.file.method_name(index));
        view.method_return_types.push_back(view.file.multiname_at(view.file.methods[index].return_type));
      }
    }
    views.abcs.push_back(std::move(view));
  }
}

// Requires the caller to have validated swf and out; the movie keeps ownership of every pointer handed out.
radicle_status open_from_bytes(const std::uint8_t* bytes, std::size_t size, radicle_swf** out_swf) {
  auto result = fo4::swf::read_swf(std::span<const std::byte>(reinterpret_cast<const std::byte*>(bytes), size));
  if (!result.ok()) return map_swf_status(result.status, result.message);

  auto swf = std::make_unique<radicle_swf>();
  swf->file = std::move(result.file);
  swf->declared_length_matches = result.declared_length_matches;
  radicle_swf_detail::rebuild_views(*swf);
  *out_swf = swf.release();
  return RADICLE_STATUS_OK;
}

std::vector<std::byte> read_file(const char* path) {
  std::ifstream in(utf8_path(path), std::ios::binary);
  if (!in) return {};
  std::vector<std::byte> bytes;
  char buffer[8192];
  while (in.read(buffer, static_cast<std::streamsize>(sizeof(buffer))) || in.gcount() > 0) {
    const auto count = in.gcount();
    for (std::streamsize index = 0; index < count; ++index) {
      bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(buffer[index])));
    }
  }
  return bytes;
}

const abc_view* abc_at(const radicle_swf* swf, std::size_t index) {
  return index < swf->abcs.size() ? &swf->abcs[index] : nullptr;
}

const fo4::abc::AbcMethodSummary* summary_method(const abc_view& view,
                                                 std::size_t owner,
                                                 std::size_t owner_index,
                                                 std::size_t method_index) {
  switch (owner) {
    case RADICLE_SWF_OWNER_CLASS: {
      if (owner_index >= view.summary.classes.size()) return nullptr;
      const auto& methods = view.summary.classes[owner_index].methods;
      return method_index < methods.size() ? &methods[method_index] : nullptr;
    }
    case RADICLE_SWF_OWNER_SCRIPT: {
      if (owner_index >= view.summary.scripts.size()) return nullptr;
      return method_index == 0 ? &view.summary.scripts[owner_index] : nullptr;
    }
    case RADICLE_SWF_OWNER_FUNCTION: {
      if (owner_index >= view.summary.functions.size()) return nullptr;
      return method_index == 0 ? &view.summary.functions[owner_index] : nullptr;
    }
    default:
      return nullptr;
  }
}

void fill_method(const fo4::abc::AbcMethodSummary& summary, radicle_swf_method& out) {
  out = {};
  out.name = summary.name.c_str();
  out.return_type = summary.return_type.c_str();
  out.flags = summary.flags;
  out.instruction_count = summary.instruction_count;
  out.call_count = summary.calls.size();
  out.multiname_count = summary.multinames.size();
}

void fill_call(const fo4::abc::AbcCallSite& site, radicle_swf_call& out) {
  out = {};
  out.offset = site.offset;
  out.opcode = site.opcode.c_str();
  out.target = site.target.c_str();
  out.receiver = site.receiver.c_str();
  out.argument_count = site.arg_count;
  out.event_name = site.event_name.c_str();
  out.handler = site.handler.c_str();
  out.is_external_interface = fo4::abc::is_external_interface_call(site) ? 1 : 0;
  out.is_event_registration = fo4::abc::is_event_registration(site) ? 1 : 0;
}

}  // namespace

namespace radicle_swf_detail {

std::uint8_t* copy_text(std::string_view text, std::size_t* out_size) {
  auto* buffer = static_cast<std::uint8_t*>(std::malloc(text.size() + 1));
  if (buffer == nullptr) return nullptr;
  if (!text.empty()) std::memcpy(buffer, text.data(), text.size());
  buffer[text.size()] = 0;
  *out_size = text.size();
  return buffer;
}

// Invariant: every cached view must be built here before any getter runs, and a throw must leave the handle intact.
void build_views(const fo4::swf::SwfFile& file, swf_views& out) {
  out = {};
  out.summary = fo4::swf::summarize(file);
  for (const auto& tag : file.tags) {
    flatten_tags(tag, RADICLE_SWF_NO_INDEX, 0, false, out);
  }
  collect_records(out);
  build_abc_views(file, out);
}

// Invariant: the install must move, never copy, so every tag_view keeps pointing at the same tags.
void install_views(radicle_swf& swf, swf_views&& views) {
  swf.summary = std::move(views.summary);
  swf.tags = std::move(views.tags);
  swf.symbols = std::move(views.symbols);
  swf.exports = std::move(views.exports);
  swf.frame_labels = std::move(views.frame_labels);
  swf.frame_label_tags = std::move(views.frame_label_tags);
  swf.frame_label_sprites = std::move(views.frame_label_sprites);
  swf.frame_label_inside_sprite = std::move(views.frame_label_inside_sprite);
  swf.abcs = std::move(views.abcs);
  swf.morph_frames.clear();
  swf.characters = {};
}

void rebuild_views(radicle_swf& swf) {
  swf_views views;
  build_views(swf.file, views);
  install_views(swf, std::move(views));
}

}  // namespace radicle_swf_detail

extern "C" {

radicle_status radicle_swf_open_bytes(const uint8_t* bytes, size_t size, radicle_swf** out_swf) {
  clear_error();
  if (out_swf) *out_swf = nullptr;
  if (bytes == nullptr || size == 0 || out_swf == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "bytes, size and out_swf are required");
  }
  return guarded("unknown exception while reading a movie",
                 [&] { return open_from_bytes(bytes, size, out_swf); });
}

radicle_status radicle_swf_open_path(const char* path, radicle_swf** out_swf) {
  clear_error();
  if (out_swf) *out_swf = nullptr;
  if (path == nullptr || *path == '\0' || out_swf == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "path and out_swf are required");
  }
  return guarded("unknown exception while reading a movie file", [&] {
    const auto bytes = read_file(path);
    if (bytes.empty()) return fail(RADICLE_STATUS_IO_ERROR, "the movie file is empty or missing");
    return open_from_bytes(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size(), out_swf);
  });
}

radicle_status radicle_swf_open_ba2(const radicle_ba2* ba2, const char* path, radicle_swf** out_swf) {
  clear_error();
  if (out_swf) *out_swf = nullptr;
  if (ba2 == nullptr || path == nullptr || *path == '\0' || out_swf == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "ba2, path and out_swf are required");
  }
  return guarded("unknown exception while reading a movie from an archive", [&] {
    std::size_t index = 0;
    const auto found = radicle_ba2_find(ba2, path, &index);
    if (found != RADICLE_STATUS_OK) return found;
    uint8_t* bytes = nullptr;
    std::size_t size = 0;
    const auto extracted = radicle_ba2_extract(ba2, index, &bytes, &size);
    if (extracted != RADICLE_STATUS_OK) return extracted;
    const auto status = open_from_bytes(bytes, size, out_swf);
    // Requires std::free here: radicle_free_bytes would clear the refusal message.
    std::free(bytes);
    return status;
  });
}

void radicle_swf_destroy(radicle_swf* swf) {
  clear_error();
  delete swf;
}

radicle_status radicle_swf_get_info(const radicle_swf* swf, radicle_swf_info* out_info) {
  clear_error();
  if (out_info) *out_info = {};
  if (swf == nullptr || out_info == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "swf and out_info are required");
  }
  out_info->format = swf->file.header.format == fo4::swf::SwfFormat::gfx ? 1 : 0;
  out_info->compression = static_cast<uint8_t>(swf->file.header.compression);
  out_info->version = swf->file.header.version;
  out_info->action_script3 = swf->summary.action_script3 ? 1 : 0;
  out_info->has_metadata = swf->summary.has_metadata ? 1 : 0;
  out_info->use_network = swf->summary.use_network ? 1 : 0;
  out_info->use_gpu = 0;
  out_info->use_direct_blit = 0;
  for (const auto& tag : swf->file.tags) {
    if (!tag.attributes) continue;
    out_info->use_gpu = tag.attributes->use_gpu ? 1 : 0;
    out_info->use_direct_blit = tag.attributes->use_direct_blit ? 1 : 0;
    break;
  }
  out_info->declared_length_matches = swf->declared_length_matches ? 1 : 0;
  out_info->frame_count = swf->file.header.frame_count;
  out_info->file_length = swf->file.header.file_length;
  out_info->frame_rate = static_cast<float>(swf->file.header.frame_rate_fps());
  out_info->tag_count = swf->tags.size();
  out_info->shape_tags = swf->summary.shape_tags;
  out_info->bitmap_tags = swf->summary.bitmap_tags;
  out_info->font_tags = swf->summary.font_tags;
  out_info->text_tags = swf->summary.text_tags;
  out_info->sound_tags = swf->summary.sound_tags;
  out_info->exporter_info_tags = swf->summary.exporter_info_tags;
  out_info->external_image_tags = swf->summary.external_image_tags;
  out_info->font_texture_tags = swf->summary.font_texture_tags;
  out_info->symbol_count = swf->symbols.size();
  out_info->export_count = swf->exports.size();
  out_info->frame_label_count = swf->frame_labels.size();
  out_info->abc_count = swf->abcs.size();
  return RADICLE_STATUS_OK;
}

size_t radicle_swf_tag_count(const radicle_swf* swf) {
  clear_error();
  return swf == nullptr ? 0 : swf->tags.size();
}

radicle_status radicle_swf_tag_at(const radicle_swf* swf, size_t index, radicle_swf_tag* out_tag) {
  clear_error();
  if (out_tag) *out_tag = {};
  if (swf == nullptr || out_tag == nullptr || index >= swf->tags.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "swf, out_tag and a tag index are required");
  }
  const auto& view = swf->tags[index];
  out_tag->code = view.tag->code;
  out_tag->length = view.tag->length;
  out_tag->offset = view.tag->offset;
  out_tag->parent = view.parent;
  out_tag->sprite_id = view.sprite_id;
  out_tag->inside_sprite = view.inside_sprite ? 1 : 0;
  out_tag->long_header = view.tag->long_header ? 1 : 0;
  out_tag->payload_size = view.tag->payload.size();
  out_tag->name = fo4::swf::tag_code_name(view.tag->code).data();
  return RADICLE_STATUS_OK;
}

radicle_status radicle_swf_tag_payload(const radicle_swf* swf, size_t index,
                                       uint8_t** out_bytes, size_t* out_size) {
  clear_error();
  if (out_bytes) *out_bytes = nullptr;
  if (out_size) *out_size = 0;
  if (swf == nullptr || out_bytes == nullptr || out_size == nullptr || index >= swf->tags.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "swf, out_bytes, out_size and a tag index are required");
  }
  const auto& payload = swf->tags[index].tag->payload;
  if (payload.empty()) return RADICLE_STATUS_OK;
  auto* buffer = static_cast<uint8_t*>(std::malloc(payload.size()));
  if (buffer == nullptr) return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate the tag payload");
  std::memcpy(buffer, payload.data(), payload.size());
  *out_bytes = buffer;
  *out_size = payload.size();
  return RADICLE_STATUS_OK;
}

size_t radicle_swf_symbol_count(const radicle_swf* swf) {
  clear_error();
  return swf == nullptr ? 0 : swf->symbols.size();
}

radicle_status radicle_swf_symbol_at(const radicle_swf* swf, size_t index, radicle_swf_symbol* out_symbol) {
  clear_error();
  if (out_symbol) *out_symbol = {};
  if (swf == nullptr || out_symbol == nullptr || index >= swf->symbols.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "swf, out_symbol and a symbol index are required");
  }
  out_symbol->tag_id = swf->symbols[index].tag_id;
  out_symbol->name = swf->symbols[index].name.c_str();
  return RADICLE_STATUS_OK;
}

size_t radicle_swf_export_count(const radicle_swf* swf) {
  clear_error();
  return swf == nullptr ? 0 : swf->exports.size();
}

radicle_status radicle_swf_export_at(const radicle_swf* swf, size_t index, radicle_swf_symbol* out_export) {
  clear_error();
  if (out_export) *out_export = {};
  if (swf == nullptr || out_export == nullptr || index >= swf->exports.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "swf, out_export and an export index are required");
  }
  out_export->tag_id = swf->exports[index].tag_id;
  out_export->name = swf->exports[index].name.c_str();
  return RADICLE_STATUS_OK;
}

size_t radicle_swf_frame_label_count(const radicle_swf* swf) {
  clear_error();
  return swf == nullptr ? 0 : swf->frame_labels.size();
}

radicle_status radicle_swf_frame_label_at(const radicle_swf* swf, size_t index,
                                          radicle_swf_frame_label* out_label) {
  clear_error();
  if (out_label) *out_label = {};
  if (swf == nullptr || out_label == nullptr || index >= swf->frame_labels.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "swf, out_label and a label index are required");
  }
  out_label->name = swf->frame_labels[index].c_str();
  out_label->tag_index = swf->frame_label_tags[index];
  out_label->sprite_id = swf->frame_label_sprites[index];
  out_label->inside_sprite = swf->frame_label_inside_sprite[index];
  return RADICLE_STATUS_OK;
}

size_t radicle_swf_abc_count(const radicle_swf* swf) {
  clear_error();
  return swf == nullptr ? 0 : swf->abcs.size();
}

radicle_status radicle_swf_abc_info_at(const radicle_swf* swf, size_t abc, radicle_swf_abc_info* out_info) {
  clear_error();
  if (out_info) *out_info = {};
  const auto* view = swf == nullptr ? nullptr : abc_at(swf, abc);
  if (view == nullptr || out_info == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "swf, out_info and an ABC index are required");
  }
  out_info->name = view->name.c_str();
  out_info->flags = view->flags;
  out_info->tag_index = view->tag_index;
  out_info->payload_size = view->payload_size;
  out_info->parsed = view->parsed ? 1 : 0;
  return RADICLE_STATUS_OK;
}

size_t radicle_swf_abc_class_count(const radicle_swf* swf, size_t abc) {
  clear_error();
  const auto* view = swf == nullptr ? nullptr : abc_at(swf, abc);
  return view == nullptr ? 0 : view->summary.classes.size();
}

radicle_status radicle_swf_abc_class_at(const radicle_swf* swf, size_t abc, size_t index,
                                        radicle_swf_class* out_class) {
  clear_error();
  if (out_class) *out_class = {};
  const auto* view = swf == nullptr ? nullptr : abc_at(swf, abc);
  if (view == nullptr || out_class == nullptr || index >= view->summary.classes.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "swf, out_class and a class index are required");
  }
  const auto& entry = view->summary.classes[index];
  out_class->name = entry.name.c_str();
  out_class->super_name = entry.super_name.c_str();
  out_class->interface_count = entry.interfaces.size();
  out_class->method_count = entry.methods.size();
  return RADICLE_STATUS_OK;
}

radicle_status radicle_swf_abc_class_interface(const radicle_swf* swf, size_t abc, size_t index,
                                               size_t interface_index, const char** out_name) {
  clear_error();
  if (out_name) *out_name = nullptr;
  const auto* view = swf == nullptr ? nullptr : abc_at(swf, abc);
  if (view == nullptr || out_name == nullptr || index >= view->summary.classes.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "swf, out_name and a class index are required");
  }
  const auto& interfaces = view->summary.classes[index].interfaces;
  if (interface_index >= interfaces.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the interface index is out of range");
  }
  *out_name = interfaces[interface_index].c_str();
  return RADICLE_STATUS_OK;
}

radicle_status radicle_swf_abc_class_method(const radicle_swf* swf, size_t abc, size_t index,
                                            size_t method_index, radicle_swf_method* out_method) {
  clear_error();
  if (out_method) *out_method = {};
  const auto* view = swf == nullptr ? nullptr : abc_at(swf, abc);
  if (view == nullptr || out_method == nullptr || index >= view->summary.classes.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "swf, out_method and a class index are required");
  }
  const auto& methods = view->summary.classes[index].methods;
  if (method_index >= methods.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the method index is out of range");
  }
  fill_method(methods[method_index], *out_method);
  return RADICLE_STATUS_OK;
}

size_t radicle_swf_abc_script_count(const radicle_swf* swf, size_t abc) {
  clear_error();
  const auto* view = swf == nullptr ? nullptr : abc_at(swf, abc);
  return view == nullptr ? 0 : view->summary.scripts.size();
}

radicle_status radicle_swf_abc_script_at(const radicle_swf* swf, size_t abc, size_t index,
                                         radicle_swf_method* out_method) {
  clear_error();
  if (out_method) *out_method = {};
  const auto* view = swf == nullptr ? nullptr : abc_at(swf, abc);
  if (view == nullptr || out_method == nullptr || index >= view->summary.scripts.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "swf, out_method and a script index are required");
  }
  fill_method(view->summary.scripts[index], *out_method);
  return RADICLE_STATUS_OK;
}

size_t radicle_swf_abc_function_count(const radicle_swf* swf, size_t abc) {
  clear_error();
  const auto* view = swf == nullptr ? nullptr : abc_at(swf, abc);
  return view == nullptr ? 0 : view->summary.functions.size();
}

radicle_status radicle_swf_abc_function_at(const radicle_swf* swf, size_t abc, size_t index,
                                           radicle_swf_method* out_method) {
  clear_error();
  if (out_method) *out_method = {};
  const auto* view = swf == nullptr ? nullptr : abc_at(swf, abc);
  if (view == nullptr || out_method == nullptr || index >= view->summary.functions.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "swf, out_method and a function index are required");
  }
  fill_method(view->summary.functions[index], *out_method);
  return RADICLE_STATUS_OK;
}

radicle_status radicle_swf_abc_call_at(const radicle_swf* swf, size_t abc,
                                       size_t owner, size_t owner_index, size_t method_index,
                                       size_t call_index, radicle_swf_call* out_call) {
  clear_error();
  if (out_call) *out_call = {};
  const auto* view = swf == nullptr ? nullptr : abc_at(swf, abc);
  if (view == nullptr || out_call == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "swf, out_call and an ABC index are required");
  }
  const auto* method = summary_method(*view, owner, owner_index, method_index);
  if (method == nullptr) return fail(RADICLE_STATUS_NOT_FOUND, "the owner and method indexes name no summary method");
  if (call_index >= method->calls.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the call index is out of range");
  }
  fill_call(method->calls[call_index], *out_call);
  return RADICLE_STATUS_OK;
}

radicle_status radicle_swf_abc_multiname_at(const radicle_swf* swf, size_t abc,
                                            size_t owner, size_t owner_index, size_t method_index,
                                            size_t index, const char** out_name) {
  clear_error();
  if (out_name) *out_name = nullptr;
  const auto* view = swf == nullptr ? nullptr : abc_at(swf, abc);
  if (view == nullptr || out_name == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "swf, out_name and an ABC index are required");
  }
  const auto* method = summary_method(*view, owner, owner_index, method_index);
  if (method == nullptr) return fail(RADICLE_STATUS_NOT_FOUND, "the owner and method indexes name no summary method");
  if (index >= method->multinames.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the multiname index is out of range");
  }
  *out_name = method->multinames[index].c_str();
  return RADICLE_STATUS_OK;
}

size_t radicle_swf_abc_method_count(const radicle_swf* swf, size_t abc) {
  clear_error();
  const auto* view = swf == nullptr ? nullptr : abc_at(swf, abc);
  return view == nullptr || !view->parsed ? 0 : view->file.methods.size();
}

radicle_status radicle_swf_abc_method_at(const radicle_swf* swf, size_t abc, size_t index,
                                         radicle_swf_method_pool* out_method) {
  clear_error();
  if (out_method) *out_method = {};
  const auto* view = swf == nullptr ? nullptr : abc_at(swf, abc);
  if (view == nullptr || out_method == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "swf, out_method and an ABC index are required");
  }
  if (!view->parsed) return fail(RADICLE_STATUS_PARSE_ERROR, "the ABC block did not parse");
  if (index >= view->file.methods.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the method index is out of range");
  }
  const auto& method = view->file.methods[index];
  out_method->name = view->method_names[index].c_str();
  out_method->return_type = view->method_return_types[index].c_str();
  out_method->param_count = method.param_count;
  out_method->flags = method.flags;
  if (const auto* body = view->file.body_for(static_cast<std::uint32_t>(index)); body != nullptr) {
    out_method->has_body = 1;
    out_method->max_stack = body->max_stack;
    out_method->local_count = body->local_count;
    out_method->init_scope_depth = body->init_scope_depth;
    out_method->max_scope_depth = body->max_scope_depth;
    out_method->code_size = static_cast<uint32_t>(body->code.size());
    out_method->exception_count = body->exceptions.size();
  }
  return RADICLE_STATUS_OK;
}

radicle_status radicle_swf_abc_method_disassemble(const radicle_swf* swf, size_t abc, size_t index,
                                                  uint8_t** out_text, size_t* out_size) {
  clear_error();
  if (out_text) *out_text = nullptr;
  if (out_size) *out_size = 0;
  if (swf == nullptr || out_text == nullptr || out_size == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "swf, out_text, out_size and an ABC index are required");
  }
  const auto* view = abc_at(swf, abc);
  if (view == nullptr) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the ABC index is out of range");
  if (!view->parsed) return fail(RADICLE_STATUS_PARSE_ERROR, "the ABC block did not parse");
  if (index >= view->file.methods.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the method index is out of range");
  }
  const auto* body = view->file.body_for(static_cast<std::uint32_t>(index));
  if (body == nullptr) return fail(RADICLE_STATUS_NOT_FOUND, "the method has no body to disassemble");
  return guarded("unknown exception while disassembling a method", [&] {
    std::string text;
    const auto status = fo4::abc::disassemble(view->file, body->code, text, body->exceptions);
    if (status != fo4::abc::AbcStatus::Ok) return fail(RADICLE_STATUS_PARSE_ERROR, "the method body did not decode");
    auto* buffer = radicle_swf_detail::copy_text(text, out_size);
    if (buffer == nullptr) return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate the listing");
    *out_text = buffer;
    return RADICLE_STATUS_OK;
  });
}

}  // extern "C"
