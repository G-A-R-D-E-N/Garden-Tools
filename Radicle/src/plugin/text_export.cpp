#include "fo4/plugin/text_export.hpp"

#include "fo4/formid.hpp"
#include "fo4/io/path_text.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/text/encoding.hpp"
#include "text_export_internal.hpp"

#include <algorithm>
#include <exception>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fo4 {
namespace {

using text_export::Json;

struct PendingFile {
  std::string relative;
  std::string text;
};

struct ExportState {
  std::vector<PendingFile> files;
  std::set<std::string> taken;
  bool schema_labels{false};
  bool localized{false};
};

std::string record_label(const Record& record) {
  return record.header.signature.to_string() + " " + text_export::u32_hex(record.header.form_id.value());
}

std::string signature_text(Signature signature, const char* what) {
  auto text = signature.to_string();
  if (!text_export::is_plain_signature(text)) {
    throw std::invalid_argument(std::string(what) + " signature is not 4 plain characters");
  }
  return text;
}

std::string subrecord_signature_text(Signature signature) {
  const auto text = signature.to_string();
  return text_export::is_printable_ascii(text) ? text : text_export::u32_hex(signature.packed());
}

std::string hex_upper(std::uint32_t value, std::size_t width) {
  constexpr char digits[] = "0123456789ABCDEF";
  std::string out(width, '0');
  for (std::size_t digit = width; digit-- > 0;) {
    out[digit] = digits[value & 15u];
    value >>= 4;
  }
  return out;
}

std::string record_stem(const Record& record) {
  const auto& key = record.form_key;
  if (key.owner().empty()) return "RAW_" + hex_upper(record.header.form_id.value(), 8);
  const auto owner = std::string(key.owner().filename());
  if (!text_export::is_safe_name(owner)) {
    throw std::invalid_argument("record owner cannot be used in a file name: " + owner);
  }
  return hex_upper(key.local().value() & k_full_local_mask, 6) + "@" + owner;
}

std::string reserve_record_path(ExportState& state, const Record& record) {
  const auto folder = signature_text(record.header.signature, "record");
  const auto stem = record_stem(record);
  std::string name = stem;
  for (std::size_t suffix = 2; state.taken.contains(text::fold(folder + "/" + name + ".json")); ++suffix) {
    name = stem + "~" + std::to_string(suffix);
  }
  auto relative = folder + "/" + name + ".json";
  state.taken.insert(text::fold(relative));
  return relative;
}

Json header_json(const RecordHeader& header) {
  Json out = Json::object();
  out["flags"] = text_export::u32_hex(header.flags.value());
  if (header.form_version != 0) out["form_version"] = header.form_version;
  if (header.timestamp != 0) out["timestamp"] = header.timestamp;
  if (header.version_control != 0) out["version_control"] = header.version_control;
  if (header.vc_version != 0) out["vc_version"] = header.vc_version;
  return out;
}

Json record_json(const Record& record, bool schema_labels, bool localized) {
  if (record.undecodable) {
    throw std::invalid_argument("record " + record_label(record) + " is undecodable and cannot be exported");
  }
  if (record.compressed != record.header.flags.is_compressed()) {
    throw std::invalid_argument("record " + record_label(record) +
                                " compressed flag disagrees with its header flags");
  }
  Json out = Json::object();
  out["signature"] = signature_text(record.header.signature, "record");
  out["form_id"] = text_export::u32_hex(record.header.form_id.value());
  out["header"] = header_json(record.header);
  out["compressed"] = record.compressed;
  Json subrecords = Json::array();
  for (const auto& sub : record.subrecords) {
    Json entry = Json::object();
    entry["sig"] = subrecord_signature_text(sub.signature);
    entry["hex"] = text_export::hex_encode(sub.data);
    if (sub.extended_size) entry["extended"] = true;
    subrecords.push_back(std::move(entry));
  }
  out["subrecords"] = std::move(subrecords);
  if (schema_labels) out["labels"] = text_export::schema_labels_json(record, localized);
  return out;
}

bool label_is_printable(const GroupHeader& header) {
  for (const auto byte : header.label) {
    const auto code = std::to_integer<unsigned>(byte);
    if (code < 0x20 || code > 0x7E) return false;
  }
  return true;
}

Json group_fields_json(const GroupHeader& header) {
  Json out = Json::object();
  if (!group_label_is_form_id(header.group_type) && label_is_printable(header)) {
    std::string text;
    for (const auto byte : header.label) text += static_cast<char>(std::to_integer<std::uint8_t>(byte));
    out["label"] = text;
  } else {
    out["label"] = text_export::u32_hex(header.label_as_u32());
  }
  out["group_type"] = header.group_type;
  out["timestamp"] = header.timestamp;
  out["version_control"] = header.version_control;
  out["form_version"] = header.form_version;
  out["unknown"] = header.unknown;
  return out;
}

std::string top_label(const GroupHeader& header) {
  std::string label;
  for (const auto byte : header.label) label += static_cast<char>(std::to_integer<std::uint8_t>(byte));
  if (group_label_is_form_id(header.group_type) || !text_export::is_plain_signature(label)) {
    throw std::invalid_argument("top-level group label is not a record signature");
  }
  return label;
}

Json children_json(ExportState& state, const Group& group) {
  Json children = Json::array();
  for (const auto& child : group.children) {
    Json entry = Json::object();
    if (const auto* record = std::get_if<Record>(&child)) {
      std::string relative = reserve_record_path(state, *record);
      state.files.push_back(PendingFile{relative, record_json(*record, state.schema_labels, state.localized).dump(2) + "\n"});
      entry["record"] = relative;
    } else {
      const auto& nested = std::get<Group>(child);
      Json fields = group_fields_json(nested.header);
      fields["children"] = children_json(state, nested);
      entry["group"] = std::move(fields);
    }
    children.push_back(std::move(entry));
  }
  return children;
}

void zero_hedr_record_count(Record& tes4) {
  for (auto& sub : tes4.subrecords) {
    if (sub.signature == sig_hedr && sub.data.size() >= 8) {
      std::fill(sub.data.begin() + 4, sub.data.begin() + 8, std::byte{0});
    }
  }
}

bool is_fo4txt_export(const std::filesystem::path& dir) {
  const auto header = dir / std::string(text_export::k_header_file);
  if (!std::filesystem::is_regular_file(header)) return false;
  try {
    const auto document = Json::parse(text_export::read_file_text(header));
    return document.is_object() && document.contains("format") && document.at("format").is_string() &&
           document.at("format").get<std::string>() == text_export::k_format;
  } catch (const std::exception&) {
    return false;
  }
}

bool is_link_or_reparse_point(const std::filesystem::path& path) {
  if (std::filesystem::is_symlink(std::filesystem::symlink_status(path))) return true;
#ifdef _WIN32
  const auto attributes = GetFileAttributesW(path.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES) throw std::runtime_error("cannot inspect export entry: " + io::utf8(path));
  return (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
  return false;
#endif
}

void prepare_target(const std::filesystem::path& target) {
  if (!std::filesystem::exists(target)) {
    std::filesystem::create_directories(target);
    return;
  }
  if (is_link_or_reparse_point(target)) {
    throw std::runtime_error("export target is a link or reparse point: " + io::utf8(target));
  }
  if (!std::filesystem::is_directory(target)) {
    throw std::runtime_error("export target exists and is not a folder: " + io::utf8(target));
  }
  if (std::filesystem::is_empty(target)) return;
  if (!is_fo4txt_export(target)) {
    throw std::runtime_error("export target exists, is not empty and is not an fo4txt export: " +
                             io::utf8(target));
  }
  for (const auto& entry : std::filesystem::recursive_directory_iterator(target)) {
    if (is_link_or_reparse_point(entry.path())) {
      throw std::runtime_error("export target contains a link or reparse point: " + io::utf8(entry.path()));
    }
  }
  try {
    static_cast<void>(import_plugin_text(target));
  } catch (const std::exception& error) {
    throw std::runtime_error("export target cannot be safely replaced: " + std::string(error.what()));
  }
  std::vector<std::filesystem::path> owned;
  std::vector<std::filesystem::path> folders;
  for (const auto& entry : std::filesystem::directory_iterator(target)) {
    const auto name = io::utf8(entry.path().filename());
    if (name == text_export::k_header_file && entry.is_regular_file()) {
      owned.push_back(entry.path());
      continue;
    }
    if (!entry.is_directory() || !text_export::is_group_folder_name(name)) {
      throw std::runtime_error("export target holds an entry this export does not own: " +
                               io::utf8(entry.path()));
    }
    folders.push_back(entry.path());
    for (const auto& child : std::filesystem::directory_iterator(entry.path())) {
      if (!child.is_regular_file() || child.path().extension() != ".json") {
        throw std::runtime_error("export target folder holds a file this export does not own: " +
                                 io::utf8(child.path()));
      }
      owned.push_back(child.path());
    }
  }
  for (const auto& path : owned) {
    std::filesystem::remove(path);
  }
  for (const auto& folder : folders) {
    if (std::filesystem::is_empty(folder)) std::filesystem::remove(folder);
  }
}

}  // namespace

std::filesystem::path export_plugin_text(const Plugin& plugin,
                                         const std::filesystem::path& parent_dir,
                                         const TextExportOptions& options) {
  const auto filename = std::string(plugin.key.filename());
  if (!text_export::is_safe_name(filename)) {
    throw std::invalid_argument("plugin filename is empty or cannot be used as a file name: " + filename);
  }
  text_export::require_windows1252(filename, "plugin filename");
  for (const auto& master : plugin.header.masters) {
    text_export::require_windows1252(master.key.filename(), "master name");
  }
  if (plugin.tes4.header.signature != sig_tes4) {
    throw std::invalid_argument("plugin TES4 record is not a TES4 record");
  }

  Record tes4 = plugin.tes4;
  zero_hedr_record_count(tes4);

  ExportState state;
  state.schema_labels = options.schema_labels;
  state.localized = plugin.header.tes4_flags.is_localized();
  Json groups = Json::array();
  for (const auto& group : plugin.groups) {
    const auto label = top_label(group.header);
    std::string folder = label;
    for (std::size_t suffix = 2;
         state.taken.contains(text::fold(folder + "/" + std::string(text_export::k_group_file))); ++suffix) {
      folder = label + "~" + std::to_string(suffix);
    }
    const auto relative = folder + "/" + std::string(text_export::k_group_file);
    state.taken.insert(text::fold(relative));
    Json fields = group_fields_json(group.header);
    groups.push_back(fields);
    fields["children"] = children_json(state, group);
    state.files.push_back(PendingFile{relative, fields.dump(2) + "\n"});
  }

  Json header = Json::object();
  header["format"] = std::string(text_export::k_format);
  header["filename"] = filename;
  header["tes4"] = record_json(tes4, options.schema_labels, state.localized);
  header["groups"] = std::move(groups);
  state.files.push_back(PendingFile{std::string(text_export::k_header_file), header.dump(2) + "\n"});

  const auto target = parent_dir / io::from_utf8(filename + std::string(".fo4txt"));
  prepare_target(target);
  for (const auto& file : state.files) {
    const auto path = target / io::from_utf8(file.relative);
    std::filesystem::create_directories(path.parent_path());
    text_export::write_file_text(path, file.text);
  }
  return target;
}

}  // namespace fo4
