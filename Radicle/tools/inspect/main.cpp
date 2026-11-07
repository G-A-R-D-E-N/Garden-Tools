#include "fo4/fo4.hpp"
#include "fo4/schema/form_link_mixed.hpp"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::string hex_u32(std::uint32_t value) {
  char buf[11];
  std::snprintf(buf, sizeof(buf), "0x%08X", value);
  return buf;
}

std::string json_escape(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (const unsigned char ch : text) {
    switch (ch) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (ch < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", ch);
          out += buf;
        } else {
          out += static_cast<char>(ch);
        }
        break;
    }
  }
  return out;
}

std::string json_string(std::string_view text) {
  return "\"" + json_escape(text) + "\"";
}

std::string label_text(const fo4::GroupHeader& header) {
  if (header.group_type == 0) {
    return header.top_label().to_string();
  }
  return hex_u32(header.label_as_u32());
}

fo4::RawFormId parse_raw_form_id(std::string_view text) {
  auto hex = text;
  if (hex.size() >= 2 && hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) {
    hex.remove_prefix(2);
  }
  std::uint32_t value = 0;
  for (const char ch : hex) {
    value <<= 4;
    if (ch >= '0' && ch <= '9') {
      value |= static_cast<std::uint32_t>(ch - '0');
    } else if (ch >= 'a' && ch <= 'f') {
      value |= static_cast<std::uint32_t>(ch - 'a' + 10);
    } else if (ch >= 'A' && ch <= 'F') {
      value |= static_cast<std::uint32_t>(ch - 'A' + 10);
    } else {
      throw std::runtime_error("invalid FormID: " + std::string{text});
    }
  }
  return fo4::RawFormId{value};
}

std::string editor_id(const fo4::Record& record) {
  for (const auto& sub : record.subrecords) {
    if (sub.signature == fo4::sig_edid && !sub.data.empty()) {
      std::string value(reinterpret_cast<const char*>(sub.data.data()), sub.data.size());
      if (!value.empty() && value.back() == '\0') {
        value.pop_back();
      }
      return value;
    }
  }
  return {};
}

void print_header(const fo4::Plugin& plugin) {
  std::cout << "{\n";
  std::cout << "  \"path\": " << json_string(plugin.path.string()) << ",\n";
  std::cout << "  \"plugin\": " << json_string(plugin.key.filename()) << ",\n";
  std::cout << "  \"signature\": " << json_string(plugin.tes4.header.signature.to_string())
            << ",\n";
  std::cout << "  \"flags\": " << json_string(hex_u32(plugin.header.tes4_flags.value())) << ",\n";
  std::cout << "  \"master\": " << (plugin.header.tes4_flags.is_master() ? "true" : "false")
            << ",\n";
  std::cout << "  \"small_file\": "
            << (plugin.header.tes4_flags.is_small_file() ? "true" : "false") << ",\n";
  std::cout << "  \"localized\": " << (plugin.header.tes4_flags.is_localized() ? "true" : "false")
            << ",\n";
  std::cout << "  \"form_version\": " << plugin.header.form_version << ",\n";
  std::cout << "  \"hedr_version\": " << plugin.header.version << ",\n";
  std::cout << "  \"record_count\": " << plugin.header.record_count << ",\n";
  std::cout << "  \"next_form_id\": " << json_string(hex_u32(plugin.header.next_form_id)) << ",\n";
  std::cout << "  \"author\": " << json_string(plugin.header.author) << ",\n";
  std::cout << "  \"description\": " << json_string(plugin.header.description) << ",\n";
  std::cout << "  \"file_size\": " << fo4::plugin_original_bytes(plugin).size() << "\n";
  std::cout << "}\n";
}

void print_masters(const fo4::Plugin& plugin) {
  std::cout << "{\n  \"masters\": [\n";
  for (std::size_t i = 0; i < plugin.header.masters.size(); ++i) {
    const auto& master = plugin.header.masters[i];
    std::cout << "    {\"index\": " << i << ", \"name\": "
              << json_string(master.key.filename()) << ", \"data_size\": " << master.data_size
              << ", \"kind\": "
              << json_string(master.kind == fo4::PluginKind::light ? "light" : "full") << "}";
    if (i + 1 != plugin.header.masters.size()) {
      std::cout << ",";
    }
    std::cout << "\n";
  }
  std::cout << "  ]\n}\n";
}

void print_groups(const fo4::Plugin& plugin) {
  const auto groups = fo4::collect_groups(plugin);
  std::cout << "{\n  \"groups\": [\n";
  for (std::size_t i = 0; i < groups.size(); ++i) {
    const auto& group = *groups[i];
    std::cout << "    {\"label\": " << json_string(label_text(group.header))
              << ", \"type\": " << group.header.group_type
              << ", \"size\": " << group.header.group_size
              << ", \"children\": " << group.children.size() << "}";
    if (i + 1 != groups.size()) {
      std::cout << ",";
    }
    std::cout << "\n";
  }
  std::cout << "  ]\n}\n";
}

void print_one_record(const fo4::Record& record, bool with_subrecords) {
  std::cout << "    {\"signature\": " << json_string(record.header.signature.to_string())
            << ", \"form_id\": " << json_string(hex_u32(record.header.form_id.value()))
            << ", \"form_key\": " << json_string(record.form_key.to_string())
            << ", \"flags\": " << json_string(hex_u32(record.header.flags.value()))
            << ", \"form_version\": " << record.header.form_version
            << ", \"data_size\": " << record.header.data_size
            << ", \"compressed\": " << (record.compressed ? "true" : "false")
            << ", \"editor_id\": " << json_string(editor_id(record))
            << ", \"subrecord_count\": " << record.subrecords.size();
  if (with_subrecords) {
    std::cout << ", \"subrecords\": [\n";
    for (std::size_t i = 0; i < record.subrecords.size(); ++i) {
      const auto& sub = record.subrecords[i];
      std::cout << "      {\"signature\": " << json_string(sub.signature.to_string())
                << ", \"size\": " << sub.data.size()
                << ", \"extended_size\": " << (sub.extended_size ? "true" : "false") << "}";
      if (i + 1 != record.subrecords.size()) {
        std::cout << ",";
      }
      std::cout << "\n";
    }
    std::cout << "    ]";
  }
  std::cout << "}";
}

void print_records(const fo4::Plugin& plugin) {
  const auto records = fo4::collect_records(plugin);
  std::cout << "{\n  \"records\": [\n";
  for (std::size_t i = 0; i < records.size(); ++i) {
    print_one_record(*records[i], false);
    if (i + 1 != records.size()) {
      std::cout << ",";
    }
    std::cout << "\n";
  }
  std::cout << "  ]\n}\n";
}

void print_record(const fo4::Plugin& plugin, fo4::RawFormId form_id) {
  const auto* record = fo4::find_record(plugin, form_id);
  if (!record) {
    throw std::runtime_error("record not found: " + hex_u32(form_id.value()));
  }
  std::cout << "{\n  \"record\":\n";
  print_one_record(*record, true);
  std::cout << "\n}\n";
}

void print_raw(const fo4::Plugin& plugin, fo4::RawFormId form_id) {
  const auto* record = fo4::find_record(plugin, form_id);
  if (!record) {
    throw std::runtime_error("record not found: " + hex_u32(form_id.value()));
  }
  const auto original = fo4::record_original_bytes(plugin, *record);
  std::cout << "{\n  \"form_id\": " << json_string(hex_u32(form_id.value()))
            << ",\n  \"byte_count\": " << original.size() << ",\n  \"bytes\": \"";
  for (const auto b : original) {
    char buf[3];
    std::snprintf(buf, sizeof(buf), "%02X", std::to_integer<unsigned>(b));
    std::cout << buf;
  }
  std::cout << "\"\n}\n";
}

void print_summary(const fo4::Plugin& plugin) {
  const auto records = fo4::collect_records(plugin);
  const auto groups = fo4::collect_groups(plugin);
  std::cout << "{\n";
  std::cout << "  \"path\": " << json_string(plugin.path.string()) << ",\n";
  std::cout << "  \"plugin\": " << json_string(plugin.key.filename()) << ",\n";
  std::cout << "  \"flags\": " << json_string(hex_u32(plugin.header.tes4_flags.value())) << ",\n";
  std::cout << "  \"form_version\": " << plugin.header.form_version << ",\n";
  std::cout << "  \"hedr_record_count\": " << plugin.header.record_count << ",\n";
  std::cout << "  \"parsed_record_count\": " << records.size() << ",\n";
  std::cout << "  \"group_count\": " << groups.size() << ",\n";
  std::cout << "  \"master_count\": " << plugin.header.masters.size() << ",\n";
  std::cout << "  \"masters\": [";
  for (std::size_t i = 0; i < plugin.header.masters.size(); ++i) {
    if (i != 0) std::cout << ", ";
    std::cout << json_string(plugin.header.masters[i].key.filename());
  }
  std::cout << "]\n}\n";
}

void print_formlinks(const fo4::Plugin& plugin) {
  const auto hits = fo4::collect_form_links(plugin);
  std::cout << "{\n  \"form_links\": [\n";
  for (std::size_t i = 0; i < hits.size(); ++i) {
    const auto& hit = hits[i];
    std::cout << "    {\"record\": " << json_string(hit.record.to_string())
              << ", \"subrecord\": " << json_string(hit.subrecord.to_string())
              << ", \"offset\": " << hit.offset
              << ", \"raw\": " << json_string(hex_u32(hit.raw.value()))
              << ", \"form_key\": " << json_string(hit.key.to_string()) << "}";
    if (i + 1 != hits.size()) {
      std::cout << ",";
    }
    std::cout << "\n";
  }
  std::cout << "  ]\n}\n";
}

void print_check(const std::vector<std::string>& paths) {
  std::vector<fo4::Plugin> plugins;
  plugins.reserve(paths.size());
  for (const auto& path : paths) plugins.push_back(fo4::read_plugin_file(path));
  std::vector<const fo4::Plugin*> load_order;
  for (const auto& plugin : plugins) load_order.push_back(&plugin);
  fo4::CheckOptions options;
  options.plugins.push_back(plugins.back().key);
  const auto findings = fo4::check_load_order(load_order, options);
  std::cout << "{\n  \"findings\": [\n";
  for (std::size_t i = 0; i < findings.size(); ++i) {
    const auto& finding = findings[i];
    std::cout << "    {\"code\": " << json_string(fo4::check_code_name(finding.code))
              << ", \"severity\": " << json_string(fo4::check_severity_name(finding.severity))
              << ", \"plugin\": " << json_string(finding.plugin.filename())
              << ", \"record\": " << json_string(finding.record.to_string())
              << ", \"subrecord\": "
              << json_string(finding.subrecord == fo4::Signature{} ? std::string{}
                                                                   : finding.subrecord.to_string())
              << ", \"offset\": " << finding.offset
              << ", \"target\": " << json_string(finding.target.to_string())
              << ", \"detail\": " << json_string(finding.detail)
              << ", \"message\": " << json_string(finding.message) << "}";
    if (i + 1 != findings.size()) {
      std::cout << ",";
    }
    std::cout << "\n";
  }
  std::cout << "  ]\n}\n";
}

void print_unreachable(const std::vector<std::string>& paths) {
  std::vector<fo4::Plugin> plugins;
  plugins.reserve(paths.size());
  for (const auto& path : paths) plugins.push_back(fo4::read_plugin_file(path));
  std::vector<const fo4::Plugin*> load_order;
  for (const auto& plugin : plugins) load_order.push_back(&plugin);
  const auto rows = fo4::find_unreachable(load_order, load_order.size() - 1);
  std::cout << "{\n  \"unreachable\": [\n";
  for (std::size_t i = 0; i < rows.size(); ++i) {
    std::cout << "    {\"record\": " << json_string(rows[i].key.to_string())
              << ", \"signature\": " << json_string(rows[i].signature.to_string()) << "}"
              << (i + 1 != rows.size() ? ",\n" : "\n");
  }
  std::cout << "  ]\n}\n";
}

void print_itm(const std::vector<std::string>& paths) {
  std::vector<fo4::Plugin> plugins;
  plugins.reserve(paths.size());
  for (const auto& path : paths) plugins.push_back(fo4::read_plugin_file(path));
  std::vector<const fo4::Plugin*> load_order;
  for (const auto& plugin : plugins) load_order.push_back(&plugin);
  const auto itm = fo4::find_identical_to_master(load_order, load_order.size() - 1);
  const auto deleted = fo4::count_deleted_records(plugins.back());
  std::cout << "{\n  \"deleted_records\": " << deleted.records
            << ",\n  \"deleted_references\": " << deleted.references
            << ",\n  \"deleted_navmeshes\": " << deleted.navmeshes << ",\n  \"itm\": [\n";
  for (std::size_t i = 0; i < itm.size(); ++i) {
    std::cout << "    {\"record\": " << json_string(itm[i].key.to_string())
              << ", \"signature\": " << json_string(itm[i].signature.to_string())
              << ", \"previous\": " << json_string(itm[i].previous.filename()) << "}";
    if (i + 1 != itm.size()) {
      std::cout << ",";
    }
    std::cout << "\n";
  }
  std::cout << "  ]\n}\n";
}

void print_udr(const fo4::Plugin& plugin) {
  const auto counts = fo4::count_deleted_records(plugin);
  std::cout << "{\n  \"deleted_records\": " << counts.records
            << ",\n  \"deleted_navmeshes\": " << counts.navmeshes << ",\n  \"deleted_references\": [\n";
  bool first = true;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    if (!record.header.flags.is_deleted() || !fo4::is_placed_reference_signature(record.header.signature)) return;
    std::cout << (first ? "" : ",\n") << "    {\"record\": " << json_string(record.form_key.to_string())
              << ", \"signature\": " << json_string(record.header.signature.to_string()) << "}";
    first = false;
  });
  std::cout << (first ? "" : "\n") << "  ]\n}\n";
}

void print_master_usage(const fo4::Plugin& plugin) {
  const auto usage = fo4::scan_master_usage(plugin);
  std::cout << "{\n  \"complete\": " << (usage.complete() ? "true" : "false")
            << ",\n  \"records_with_incomplete_vmad\": " << usage.records_with_incomplete_vmad
            << ",\n  \"masters\": [\n";
  for (std::size_t i = 0; i < usage.references.size(); ++i) {
    std::cout << "    {\"name\": " << json_string(plugin.header.masters[i].key.filename())
              << ", \"references\": " << usage.references[i] << "}";
    if (i + 1 != usage.references.size()) {
      std::cout << ",";
    }
    std::cout << "\n";
  }
  std::cout << "  ]\n}\n";
}

void print_diff(const std::string& before_path, const std::string& after_path) {
  const auto before = fo4::read_plugin_file(before_path);
  const auto after = fo4::read_plugin_file(after_path);
  const auto diffs = fo4::diff_plugins(before, after);
  std::cout << "{\n  \"records\": [\n";
  for (std::size_t i = 0; i < diffs.size(); ++i) {
    const auto& diff = diffs[i];
    const char* change = diff.change == fo4::RecordChange::added     ? "added"
                         : diff.change == fo4::RecordChange::removed ? "removed"
                                                                     : "changed";
    std::cout << "    {\"record\": " << json_string(diff.key.to_string())
              << ", \"signature\": " << json_string(diff.signature.to_string())
              << ", \"change\": " << json_string(change)
              << ", \"header_changed\": " << (diff.header_changed ? "true" : "false")
              << ", \"subrecords\": [";
    for (std::size_t s = 0; s < diff.changed_subrecords.size(); ++s) {
      if (s != 0) std::cout << ", ";
      std::cout << json_string(diff.changed_subrecords[s].to_string());
    }
    std::cout << "]}";
    if (i + 1 != diffs.size()) {
      std::cout << ",";
    }
    std::cout << "\n";
  }
  std::cout << "  ]\n}\n";
}

void usage() {
  std::cerr << "usage: fo4inspect [header|groups|records|record|masters|raw|summary|write|"
               "formlinks] plugin.esp [formid|out.esp]\n";
  std::cerr << "       fo4inspect [udr|unused-masters] plugin.esp\n"
               "       fo4inspect [check|itm|unreachable] master.esm ... plugin.esp\n"
               "       fo4inspect diff before.esp after.esp\n";
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc < 2) {
      usage();
      return 2;
    }

    std::string command = "summary";
    std::string path;
    std::string form_id_text;
    if (argc == 2) {
      path = argv[1];
    } else {
      command = argv[1];
      path = argv[2];
      if (argc >= 4) {
        form_id_text = argv[3];
      }
    }

    if (command == "diff" && argc == 4) {
      print_diff(argv[2], argv[3]);
      return 0;
    }
    if (command == "unreachable") {
      print_unreachable(std::vector<std::string>(argv + 2, argv + argc));
      return 0;
    }
    if (command == "itm") {
      print_itm(std::vector<std::string>(argv + 2, argv + argc));
      return 0;
    }
    if (command == "check") {
      print_check(std::vector<std::string>(argv + 2, argv + argc));
      return 0;
    }

    const auto plugin = fo4::read_plugin_file(path);
    if (command == "header") {
      print_header(plugin);
    } else if (command == "masters") {
      print_masters(plugin);
    } else if (command == "groups") {
      print_groups(plugin);
    } else if (command == "records") {
      print_records(plugin);
    } else if (command == "record") {
      if (form_id_text.empty()) {
        usage();
        return 2;
      }
      print_record(plugin, parse_raw_form_id(form_id_text));
    } else if (command == "raw") {
      if (form_id_text.empty()) {
        usage();
        return 2;
      }
      print_raw(plugin, parse_raw_form_id(form_id_text));
    } else if (command == "summary") {
      print_summary(plugin);
    } else if (command == "formlinks") {
      print_formlinks(plugin);
    } else if (command == "udr") {
      print_udr(plugin);
    } else if (command == "unused-masters") {
      print_master_usage(plugin);
    } else if (command == "write") {
      if (form_id_text.empty()) {
        usage();
        return 2;
      }
      fo4::write_plugin_file(plugin, form_id_text);
      std::cout << "{\"wrote\": " << json_string(form_id_text)
                << ", \"bytes\": " << std::filesystem::file_size(form_id_text) << "}\n";
    } else {
      usage();
      return 2;
    }
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }
}
