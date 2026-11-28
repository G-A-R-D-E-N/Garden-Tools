#include "fo4/plugin/text_export.hpp"

#include "fo4/io/path_text.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/text/encoding.hpp"
#include "text_export_internal.hpp"

#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace fo4 {
namespace {

using text_export::Json;

struct ImportState {
  std::filesystem::path dir;
  std::set<std::string> expected;
};

std::uint16_t optional_u16(const Json& object, const char* key) {
  if (!object.contains(key)) return 0;
  return static_cast<std::uint16_t>(text_export::unsigned_field(object, key, 0xFFFFu));
}

std::uint16_t required_u16(const Json& object, const char* key) {
  return static_cast<std::uint16_t>(text_export::unsigned_field(object, key, 0xFFFFu));
}

Signature signature_from_text(const std::string& text) {
  if (!text_export::is_plain_signature(text)) {
    throw std::invalid_argument("fo4txt: signature is not 4 plain characters: " + text);
  }
  return Signature::from_string(text);
}

Signature subrecord_signature_from_text(const std::string& text) {
  if (text.size() == 4 && text_export::is_printable_ascii(text)) return Signature::from_string(text);
  if (text.size() == 10 && text.starts_with("0x")) {
    return Signature::from_bytes(text_export::parse_u32_hex(text));
  }
  throw std::invalid_argument("fo4txt: subrecord signature is neither 4 printable characters nor 0x hex: " +
                              text);
}

Record record_from_json(const Json& document) {
  Record record;
  record.header.signature = signature_from_text(text_export::string_field(document, "signature"));
  record.header.form_id = RawFormId{text_export::parse_u32_hex(text_export::string_field(document, "form_id"))};
  const auto& header = text_export::field(document, "header");
  record.header.flags =
      RecordFlags{text_export::parse_u32_hex(text_export::string_field(header, "flags"))};
  record.header.form_version = optional_u16(header, "form_version");
  record.header.timestamp = optional_u16(header, "timestamp");
  record.header.version_control = optional_u16(header, "version_control");
  record.header.vc_version = optional_u16(header, "vc_version");
  record.compressed = text_export::bool_field(document, "compressed");
  if (record.compressed != record.header.flags.is_compressed()) {
    throw std::invalid_argument("fo4txt: record " + record.header.signature.to_string() +
                                " compressed flag disagrees with its header flags");
  }
  for (const auto& entry : text_export::array_field(document, "subrecords")) {
    Subrecord sub;
    sub.signature = subrecord_signature_from_text(text_export::string_field(entry, "sig"));
    sub.data = text_export::hex_decode(text_export::string_field(entry, "hex"));
    if (entry.contains("extended")) {
      sub.extended_size = text_export::bool_field(entry, "extended");
    }
    record.subrecords.push_back(std::move(sub));
  }
  return record;
}

void require_record_path(const std::string& relative) {
  const auto slash = relative.find('/');
  if (slash != 4 || !text_export::is_plain_signature(relative.substr(0, 4)) ||
      relative.find('/', slash + 1) != std::string::npos || relative.find('\\') != std::string::npos) {
    throw std::invalid_argument("fo4txt: manifest record path must be SIG/name.json: " + relative);
  }
  const auto name = relative.substr(slash + 1);
  if (!name.ends_with(".json") || !text_export::is_safe_name(name)) {
    throw std::invalid_argument("fo4txt: manifest record path has an unusable file name: " + relative);
  }
}

Record read_record_file(ImportState& state, const std::string& relative) {
  require_record_path(relative);
  if (!state.expected.insert(text::fold(relative)).second) {
    throw std::invalid_argument("fo4txt: manifests name record file twice: " + relative);
  }
  const auto path = state.dir / io::from_utf8(relative);
  if (!std::filesystem::is_regular_file(path)) {
    throw std::invalid_argument("fo4txt: manifest names a missing record file: " + relative);
  }
  return record_from_json(text_export::parse_document(text_export::read_file_text(path), path));
}

GroupHeader group_header_from_json(const Json& object) {
  GroupHeader header;
  header.group_type = text_export::int32_field(object, "group_type");
  const auto label = text_export::string_field(object, "label");
  if (label.size() == 4) {
    for (std::size_t index = 0; index < 4; ++index) {
      header.label[index] = std::byte{static_cast<std::uint8_t>(label[index])};
    }
  } else if (label.size() == 10) {
    header.set_label_as_u32(text_export::parse_u32_hex(label));
  } else {
    throw std::invalid_argument("fo4txt: group label is neither 4 characters nor a hex value: " + label);
  }
  header.timestamp = required_u16(object, "timestamp");
  header.version_control = required_u16(object, "version_control");
  header.form_version = required_u16(object, "form_version");
  header.unknown = required_u16(object, "unknown");
  return header;
}

Group read_group(ImportState& state, const Json& object) {
  Group group;
  group.header = group_header_from_json(object);
  for (const auto& child : text_export::array_field(object, "children")) {
    if (!child.is_object() || child.size() != 1) {
      throw std::invalid_argument("fo4txt: group child must hold exactly one record or group entry");
    }
    if (child.contains("record")) {
      group.children.emplace_back(read_record_file(state, text_export::string_field(child, "record")));
    } else if (child.contains("group")) {
      group.children.emplace_back(read_group(state, text_export::field(child, "group")));
    } else {
      throw std::invalid_argument("fo4txt: group child must hold exactly one record or group entry");
    }
  }
  return group;
}

std::string relative_key(const std::filesystem::path& root, const std::filesystem::path& file) {
  std::string key;
  for (const auto& part : std::filesystem::relative(file, root)) {
    if (!key.empty()) key += '/';
    key += io::utf8(part);
  }
  return key;
}

}  // namespace

Plugin import_plugin_text(const std::filesystem::path& fo4txt_dir) {
  if (!std::filesystem::is_directory(fo4txt_dir)) {
    throw std::invalid_argument("fo4txt: not a folder: " + io::utf8(fo4txt_dir));
  }
  const auto header_path = fo4txt_dir / std::string(text_export::k_header_file);
  if (!std::filesystem::is_regular_file(header_path)) {
    throw std::invalid_argument("fo4txt: folder has no header.json: " + io::utf8(fo4txt_dir));
  }
  const auto document = text_export::parse_document(text_export::read_file_text(header_path), header_path);
  if (text_export::string_field(document, "format") != text_export::k_format) {
    throw std::invalid_argument("fo4txt: unknown format marker in " + io::utf8(header_path));
  }
  const auto filename = text_export::string_field(document, "filename");
  if (!text_export::is_safe_name(filename)) {
    throw std::invalid_argument("fo4txt: plugin filename is not a plain file name: " + filename);
  }
  text_export::require_windows1252(filename, "plugin filename");

  Plugin plugin;
  plugin.key = PluginKey::from_filename(filename);
  if (plugin.key.filename() != filename) {
    throw std::invalid_argument("fo4txt: plugin filename is not a bare plugin name: " + filename);
  }
  plugin.tes4 = record_from_json(text_export::field(document, "tes4"));
  if (plugin.tes4.header.signature != sig_tes4) {
    throw std::invalid_argument("fo4txt: tes4 entry is not a TES4 record");
  }
  plugin.header = parse_tes4_header(plugin.tes4);
  for (const auto& master : plugin.header.masters) {
    text_export::require_windows1252(master.key.filename(), "master name");
  }

  ImportState state{fo4txt_dir, {text::fold(std::string(text_export::k_header_file))}};
  for (const auto& entry : text_export::array_field(document, "groups")) {
    const auto label = text_export::string_field(entry, "label");
    if (!text_export::is_plain_signature(label)) {
      throw std::invalid_argument("fo4txt: top-level group label is not a record signature: " + label);
    }
    std::string folder = label;
    for (std::size_t suffix = 2;
         state.expected.contains(text::fold(folder + "/" + std::string(text_export::k_group_file))); ++suffix) {
      folder = label + "~" + std::to_string(suffix);
    }
    const auto relative = folder + "/" + std::string(text_export::k_group_file);
    state.expected.insert(text::fold(relative));
    const auto path = fo4txt_dir / io::from_utf8(relative);
    if (!std::filesystem::is_regular_file(path)) {
      throw std::invalid_argument("fo4txt: missing group manifest: " + relative);
    }
    const auto manifest = text_export::parse_document(text_export::read_file_text(path), path);
    auto group = read_group(state, manifest);
    Json fields = manifest;
    fields.erase("children");
    if (fields != entry) {
      throw std::invalid_argument("fo4txt: group manifest disagrees with header.json: " + relative);
    }
    plugin.groups.push_back(std::move(group));
  }

  for (const auto& entry : std::filesystem::recursive_directory_iterator(fo4txt_dir)) {
    if (!entry.is_regular_file()) continue;
    const auto key = relative_key(fo4txt_dir, entry.path());
    if (!state.expected.contains(text::fold(key))) {
      throw std::invalid_argument("fo4txt: file is not named by any manifest: " + key);
    }
  }

  assign_record_form_keys(plugin);
  return plugin;
}

}  // namespace fo4
