#include <fo4/abc/abc_asm.hpp>

#include <fo4/abc/abc.hpp>
#include <fo4/abc/opcodes.hpp>

#include "abc_asm_internal.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::abc::detail::text {


namespace {

struct Assembler {
  AbcFile file;
  std::string error;
  std::size_t line{0};
  std::map<std::string, std::size_t> expected;
  std::vector<std::string> group;
  std::string group_kind;
  std::vector<std::string> body_lines;

  bool fail(std::string_view message) {
    if (error.empty()) {
      error = "line " + std::to_string(line) + ": " + std::string{message};
    }
    return false;
  }

  template <typename T>
  bool place(std::vector<T>& target, std::uint32_t index, T value) {
    if (index != target.size()) {
      return fail("pool or record index is not sequential");
    }
    target.push_back(std::move(value));
    return true;
  }

  Cursor cursor(const std::vector<std::string>& tokens) { return Cursor(tokens, error); }

  bool parse_version(const std::vector<std::string>& tokens) {
    if (tokens.size() != 2) {
      return fail("abc version line is malformed");
    }
    const std::string& version = tokens[1];
    const std::size_t dot = version.find('.');
    std::uint32_t major = 0;
    std::uint32_t minor = 0;
    if (dot == std::string::npos || !parse_u32(version.substr(0, dot), major) ||
        !parse_u32(version.substr(dot + 1), minor)) {
      return fail("abc version is malformed");
    }
    file.major_version = static_cast<std::uint16_t>(major);
    file.minor_version = static_cast<std::uint16_t>(minor);
    return true;
  }

  bool parse_string(const std::vector<std::string>& tokens) {
    std::uint32_t index = 0;
    std::string value;
    if (tokens.size() != 3 || !parse_u32(tokens[1], index) || !unquote(tokens[2], value)) {
      return fail("string line is malformed");
    }
    return place(file.pool.strings, index, std::move(value));
  }

  bool parse_int(const std::vector<std::string>& tokens) {
    std::uint32_t index = 0;
    std::int32_t value = 0;
    if (tokens.size() != 3 || !parse_u32(tokens[1], index) || !parse_i32(tokens[2], value)) {
      return fail("int line is malformed");
    }
    return place(file.pool.ints, index, value);
  }

  bool parse_uint(const std::vector<std::string>& tokens) {
    std::uint32_t index = 0;
    std::uint32_t value = 0;
    if (tokens.size() != 3 || !parse_u32(tokens[1], index) || !parse_u32(tokens[2], value)) {
      return fail("uint line is malformed");
    }
    return place(file.pool.uints, index, value);
  }

  bool parse_double(const std::vector<std::string>& tokens) {
    std::uint32_t index = 0;
    double value = 0.0;
    if (tokens.size() != 3 || !parse_u32(tokens[1], index) ||
        !parse_double_bits(tokens[2], value)) {
      return fail("double line is malformed");
    }
    return place(file.pool.doubles, index, value);
  }

  bool parse_namespace(const std::vector<std::string>& tokens) {
    std::uint32_t index = 0;
    std::uint32_t name = 0;
    AbcNamespaceKind kind{};
    if (tokens.size() != 4 || !parse_u32(tokens[1], index) ||
        !namespace_kind_from(tokens[2], kind) || !parse_ref(tokens[3], name)) {
      return fail("namespace line is malformed");
    }
    AbcNamespace name_space;
    name_space.kind = kind;
    name_space.name = name;
    return place(file.pool.namespaces, index, name_space);
  }

  bool parse_ns_set(const std::vector<std::string>& tokens) {
    std::uint32_t index = 0;
    if (tokens.size() < 2 || !parse_u32(tokens[1], index)) {
      return fail("ns_set line is malformed");
    }
    AbcNsSet set;
    for (std::size_t token = 2; token < tokens.size(); ++token) {
      std::uint32_t member = 0;
      if (!parse_ref(tokens[token], member)) {
        return fail("ns_set member is malformed");
      }
      set.namespaces.push_back(member);
    }
    return place(file.pool.ns_sets, index, std::move(set));
  }

  bool parse_multiname(const std::vector<std::string>& tokens) {
    std::uint32_t index = 0;
    AbcMultinameKind kind{};
    if (tokens.size() < 3 || !parse_u32(tokens[1], index) || !multiname_kind_from(tokens[2], kind)) {
      return fail("multiname line is malformed");
    }
    AbcMultiname multiname;
    multiname.kind = kind;
    std::size_t token = 3;
    const auto take_ref = [&](std::uint32_t& out) {
      if (token >= tokens.size() || !parse_ref(tokens[token], out)) {
        return false;
      }
      ++token;
      return true;
    };
    switch (kind) {
      case AbcMultinameKind::QName:
      case AbcMultinameKind::QNameA:
        if (token + 3 > tokens.size() || tokens[token] != "name") {
          return fail("qname multiname is malformed");
        }
        ++token;
        if (!take_ref(multiname.name) || token >= tokens.size() || tokens[token] != "ns") {
          return fail("qname multiname is malformed");
        }
        ++token;
        if (!take_ref(multiname.ns)) {
          return fail("qname multiname is malformed");
        }
        break;
      case AbcMultinameKind::RTQName:
      case AbcMultinameKind::RTQNameA:
        if (token + 1 > tokens.size() || tokens[token] != "name") {
          return fail("rtqname multiname is malformed");
        }
        ++token;
        if (!take_ref(multiname.name)) {
          return fail("rtqname multiname is malformed");
        }
        break;
      case AbcMultinameKind::RTQNameL:
      case AbcMultinameKind::RTQNameLA:
        break;
      case AbcMultinameKind::Multiname:
      case AbcMultinameKind::MultinameA:
        if (token + 3 > tokens.size() || tokens[token] != "name") {
          return fail("multiname is malformed");
        }
        ++token;
        if (!take_ref(multiname.name) || token >= tokens.size() || tokens[token] != "ns_set") {
          return fail("multiname is malformed");
        }
        ++token;
        if (!take_ref(multiname.ns_set)) {
          return fail("multiname is malformed");
        }
        break;
      case AbcMultinameKind::MultinameL:
      case AbcMultinameKind::MultinameLA:
        if (token + 1 > tokens.size() || tokens[token] != "ns_set") {
          return fail("multinamel is malformed");
        }
        ++token;
        if (!take_ref(multiname.ns_set)) {
          return fail("multinamel is malformed");
        }
        break;
      case AbcMultinameKind::TypeName: {
        if (token + 1 > tokens.size() || tokens[token] != "base") {
          return fail("typename multiname is malformed");
        }
        ++token;
        if (!take_ref(multiname.name)) {
          return fail("typename multiname is malformed");
        }
        while (token < tokens.size()) {
          if (tokens[token] != "param") {
            return fail("typename parameter is malformed");
          }
          ++token;
          std::uint32_t param = 0;
          if (!take_ref(param)) {
            return fail("typename parameter is malformed");
          }
          multiname.params.push_back(param);
        }
        break;
      }
    }
    return place(file.pool.multinames, index, std::move(multiname));
  }

  bool parse_method(const std::vector<std::string>& tokens) {
    std::uint32_t index = 0;
    if (tokens.size() < 2 || tokens[0] != "method" || !parse_u32(tokens[1], index)) {
      return fail("method line is malformed");
    }
    AbcMethodInfo method;
    std::size_t token = 2;
    const auto take_keyword_ref = [&](std::string_view keyword, std::uint32_t& out) {
      if (token + 1 >= tokens.size() || tokens[token] != keyword ||
          !parse_ref(tokens[token + 1], out)) {
        return false;
      }
      token += 2;
      return true;
    };
    const auto take_keyword_u32 = [&](std::string_view keyword, std::uint32_t& out) {
      if (token + 1 >= tokens.size() || tokens[token] != keyword ||
          !parse_u32(tokens[token + 1], out)) {
        return false;
      }
      token += 2;
      return true;
    };
    if (!take_keyword_ref("name", method.name) ||
        !take_keyword_u32("params", method.param_count) ||
        !take_keyword_ref("returns", method.return_type)) {
      return fail("method header is malformed");
    }
    while (token < tokens.size() && tokens[token] == "param_type") {
      std::uint32_t param_type = 0;
      if (token + 1 >= tokens.size() || !parse_ref(tokens[token + 1], param_type)) {
        return fail("method parameter type is malformed");
      }
      method.param_types.push_back(param_type);
      token += 2;
    }
    std::uint32_t flags = 0;
    if (!take_keyword_u32("flags", flags)) {
      return fail("method flags are malformed");
    }
    method.flags = static_cast<std::uint8_t>(flags);
    std::uint32_t options = 0;
    if (!take_keyword_u32("options", options)) {
      return fail("method options count is malformed");
    }
    for (std::uint32_t option_index = 0; option_index < options; ++option_index) {
      AbcOptionalValue option;
      std::uint32_t stored_index = 0;
      if (token + 2 >= tokens.size() || tokens[token] != "option" ||
          !value_kind_from(tokens[token + 1], option.kind) ||
          !parse_ref(tokens[token + 2], stored_index)) {
        return fail("method optional value is malformed");
      }
      option.index = stored_index;
      method.options.push_back(option);
      token += 3;
    }
    if (token < tokens.size() && tokens[token] == "param_names") {
      ++token;
      while (token < tokens.size()) {
        std::uint32_t param_name = 0;
        if (!parse_ref(tokens[token], param_name)) {
          return fail("method parameter name is malformed");
        }
        method.param_names.push_back(param_name);
        ++token;
      }
    }
    return place(file.methods, index, std::move(method));
  }

  bool parse_trait_lines(const std::vector<std::string>& block, std::vector<AbcTrait>& out) {
    for (const std::string& line : block) {
      const std::vector<std::string> tokens = split_tokens(line);
      Cursor trait_cursor = cursor(tokens);
      AbcTrait trait;
      if (parse_trait(trait_cursor, trait) != AbcStatus::Ok) {
        return fail(error.empty() ? "trait is malformed" : error);
      }
      out.push_back(std::move(trait));
    }
    return true;
  }

  bool flush_metadata() {
    if (group.empty()) {
      return true;
    }
    Cursor header_cursor = cursor(split_tokens(group.front()));
    std::uint32_t index = 0;
    std::uint32_t name = 0;
    std::uint32_t items = 0;
    if (!header_cursor.skip("metadata") || !header_cursor.u32(index) ||
        !header_cursor.text("name", name) || !header_cursor.take("items") ||
        !header_cursor.u32(items)) {
      return fail("metadata header is malformed");
    }
    if (items + 1 != group.size()) {
      return fail("metadata item count does not match the listed items");
    }
    AbcMetadata metadata;
    metadata.name = name;
    for (std::size_t line_index = 1; line_index < group.size(); ++line_index) {
      const std::vector<std::string> item = split_tokens(group[line_index]);
      AbcMetadataItem entry;
      if (item.size() != 3 || item[0] != "item" || !parse_ref(item[1], entry.key) ||
          !parse_ref(item[2], entry.value)) {
        return fail("metadata item is malformed");
      }
      metadata.items.push_back(entry);
    }
    return place(file.metadata, index, std::move(metadata));
  }

  bool flush_instance() {
    if (group.empty()) {
      return true;
    }
    Cursor header_cursor = cursor(split_tokens(group.front()));
    std::uint32_t index = 0;
    AbcInstanceInfo instance;
    if (!header_cursor.skip("instance") || !header_cursor.u32(index) ||
        !header_cursor.text("name", instance.name) ||
        !header_cursor.text("super", instance.super_index)) {
      return fail("instance header is malformed");
    }
    std::uint32_t flags = 0;
    if (!header_cursor.text("flags", flags)) {
      return fail("instance flags are malformed");
    }
    instance.flags = static_cast<std::uint8_t>(flags);
    if (header_cursor.skip("protected")) {
      std::uint32_t protected_ns = 0;
      if (!header_cursor.ref(protected_ns)) {
        return fail("instance protected namespace is malformed");
      }
      instance.protected_ns = protected_ns;
    }
    while (header_cursor.skip("interface")) {
      std::uint32_t interface_index = 0;
      if (!header_cursor.ref(interface_index)) {
        return fail("instance interface is malformed");
      }
      instance.interfaces.push_back(interface_index);
    }
    std::uint32_t iinit = 0;
    if (!header_cursor.text("iinit", iinit)) {
      return fail("instance iinit is malformed");
    }
    instance.iinit = iinit;
    std::vector<std::string> traits;
    for (std::size_t line_index = 1; line_index < group.size(); ++line_index) {
      traits.push_back(group[line_index]);
    }
    if (!parse_trait_lines(traits, instance.traits)) {
      return false;
    }
    return place(file.instances, index, std::move(instance));
  }

  bool flush_class() {
    if (group.empty()) {
      return true;
    }
    Cursor header_cursor = cursor(split_tokens(group.front()));
    std::uint32_t index = 0;
    AbcClassInfo klass;
    if (!header_cursor.skip("class") || !header_cursor.u32(index) ||
        !header_cursor.text("cinit", klass.cinit)) {
      return fail("class header is malformed");
    }
    std::vector<std::string> traits;
    for (std::size_t line_index = 1; line_index < group.size(); ++line_index) {
      traits.push_back(group[line_index]);
    }
    if (!parse_trait_lines(traits, klass.traits)) {
      return false;
    }
    return place(file.classes, index, std::move(klass));
  }

  bool flush_script() {
    if (group.empty()) {
      return true;
    }
    Cursor header_cursor = cursor(split_tokens(group.front()));
    std::uint32_t index = 0;
    AbcScriptInfo script;
    if (!header_cursor.skip("script") || !header_cursor.u32(index) ||
        !header_cursor.text("init", script.init)) {
      return fail("script header is malformed");
    }
    std::vector<std::string> traits;
    for (std::size_t line_index = 1; line_index < group.size(); ++line_index) {
      traits.push_back(group[line_index]);
    }
    if (!parse_trait_lines(traits, script.traits)) {
      return false;
    }
    return place(file.scripts, index, std::move(script));
  }

  bool flush_body() {
    if (body_lines.empty()) {
      return true;
    }
    const std::vector<std::string> header = split_tokens(body_lines.front());
    std::uint32_t index = 0;
    if (header.size() < 2 || header[0] != "body" || !parse_u32(header[1], index)) {
      return fail("body header is malformed");
    }
    const AbcStatus status = parse_body(file, index, body_lines, error);
    body_lines.clear();
    if (status != AbcStatus::Ok) {
      if (error.empty()) {
        return fail("body is malformed");
      }
      return false;
    }
    return true;
  }

  bool flush_group() {
    if (group_kind == "metadata") {
      const bool ok = flush_metadata();
      group.clear();
      return ok;
    }
    if (group_kind == "instances") {
      const bool ok = flush_instance();
      group.clear();
      return ok;
    }
    if (group_kind == "classes") {
      const bool ok = flush_class();
      group.clear();
      return ok;
    }
    if (group_kind == "scripts") {
      const bool ok = flush_script();
      group.clear();
      return ok;
    }
    group.clear();
    return true;
  }

  bool flush_all() { return flush_group() && flush_body(); }

  bool check_counts() {
    const auto expect = [this](const char* name, std::size_t actual) {
      const auto found = expected.find(name);
      if (found != expected.end() && found->second != actual) {
        return fail(std::string{name} + " count does not match the listed entries");
      }
      return true;
    };
    if (!expect("strings", file.pool.strings.size()) || !expect("ints", file.pool.ints.size()) ||
        !expect("uints", file.pool.uints.size()) ||
        !expect("doubles", file.pool.doubles.size()) ||
        !expect("namespaces", file.pool.namespaces.size()) ||
        !expect("ns_sets", file.pool.ns_sets.size()) ||
        !expect("multinames", file.pool.multinames.size()) ||
        !expect("metadata", file.metadata.size()) || !expect("methods", file.methods.size()) ||
        !expect("instances", file.instances.size()) ||
        !expect("classes", file.classes.size()) || !expect("scripts", file.scripts.size()) ||
        !expect("bodies", file.bodies.size())) {
      return false;
    }
    return true;
  }
};

}  // namespace

}  // namespace fo4::abc::detail::text

namespace fo4::abc {

using namespace detail::text;

AbcStatus assemble_abc_text(std::string_view text, AbcFile& out, AbcTextError* error) {
  detail::text::Assembler assembler;
  enum class Section { none, strings, ints, uints, doubles, namespaces, ns_sets, multinames,
                       metadata, methods, instances, classes, scripts, bodies, end };
  Section section = Section::none;
  std::size_t line_number = 0;
  std::size_t begin = 0;
  while (begin <= text.size()) {
    std::size_t end = text.find('\n', begin);
    if (end == std::string_view::npos) {
      end = text.size();
    }
    const std::string_view raw = text.substr(begin, end - begin);
    begin = end + 1;
    ++line_number;
    assembler.line = line_number;
    const std::string line = trim(raw);
    if (line.empty()) {
      continue;
    }
    const bool top_level = !raw.empty() && raw[0] != ' ' && raw[0] != '\t';
    if (top_level) {
      if (line[0] == ';') {
        continue;
      }
      if (!assembler.flush_all()) {
        break;
      }
      const std::vector<std::string> tokens = split_tokens(line);
      const std::string& directive = tokens.front();
      const auto declare = [&](Section next, const char* name) {
        section = next;
        if (tokens.size() > 1) {
          std::uint32_t count = 0;
          if (!parse_u32(tokens[1], count)) {
            assembler.fail("section count is malformed");
          }
          assembler.expected[name] = count;
        }
      };
      if (directive == "abc") {
        if (!assembler.parse_version(tokens)) {
          break;
        }
        section = Section::none;
        continue;
      }
      if (directive == "end") {
        section = Section::end;
        continue;
      }
      if (directive == "strings") {
        declare(Section::strings, "strings");
        continue;
      }
      if (directive == "ints") {
        declare(Section::ints, "ints");
        continue;
      }
      if (directive == "uints") {
        declare(Section::uints, "uints");
        continue;
      }
      if (directive == "doubles") {
        declare(Section::doubles, "doubles");
        continue;
      }
      if (directive == "namespaces") {
        declare(Section::namespaces, "namespaces");
        continue;
      }
      if (directive == "ns_sets") {
        declare(Section::ns_sets, "ns_sets");
        continue;
      }
      if (directive == "multinames") {
        declare(Section::multinames, "multinames");
        continue;
      }
      if (directive == "metadata") {
        declare(Section::metadata, "metadata");
        assembler.group_kind = "metadata";
        continue;
      }
      if (directive == "methods") {
        declare(Section::methods, "methods");
        continue;
      }
      if (directive == "instances") {
        declare(Section::instances, "instances");
        assembler.group_kind = "instances";
        continue;
      }
      if (directive == "classes") {
        declare(Section::classes, "classes");
        assembler.group_kind = "classes";
        continue;
      }
      if (directive == "scripts") {
        declare(Section::scripts, "scripts");
        assembler.group_kind = "scripts";
        continue;
      }
      if (directive == "bodies") {
        declare(Section::bodies, "bodies");
        assembler.group_kind = "bodies";
        continue;
      }
      if (directive == "body") {
        section = Section::bodies;
        if (!assembler.body_lines.empty()) {
          if (!assembler.flush_body()) {
            break;
          }
        }
        assembler.body_lines.push_back(line);
        continue;
      }
      assembler.fail("unknown top-level directive");
      break;
    }

    const std::vector<std::string> tokens = split_tokens(line);
    if (tokens.empty()) {
      continue;
    }
    bool ok = true;
    switch (section) {
      case Section::strings:
        ok = tokens[0] == "string" && assembler.parse_string(tokens);
        break;
      case Section::ints:
        ok = tokens[0] == "int" && assembler.parse_int(tokens);
        break;
      case Section::uints:
        ok = tokens[0] == "uint" && assembler.parse_uint(tokens);
        break;
      case Section::doubles:
        ok = tokens[0] == "double" && assembler.parse_double(tokens);
        break;
      case Section::namespaces:
        ok = tokens[0] == "namespace" && assembler.parse_namespace(tokens);
        break;
      case Section::ns_sets:
        ok = tokens[0] == "ns_set" && assembler.parse_ns_set(tokens);
        break;
      case Section::multinames:
        ok = tokens[0] == "multiname" && assembler.parse_multiname(tokens);
        break;
      case Section::metadata:
        if (tokens[0] == "metadata") {
          if (!assembler.group.empty()) {
            ok = assembler.flush_group();
          }
          if (ok) {
            assembler.group.clear();
            assembler.group.push_back(line);
          }
        } else if (tokens[0] == "item") {
          assembler.group.push_back(line);
        } else {
          ok = false;
        }
        break;
      case Section::methods:
        ok = assembler.parse_method(tokens);
        break;
      case Section::instances:
      case Section::classes:
      case Section::scripts: {
        const char* keyword = section == Section::instances ? "instance"
                              : section == Section::classes ? "class"
                                                            : "script";
        if (tokens[0] == keyword) {
          if (!assembler.group.empty()) {
            ok = assembler.flush_group();
          }
          assembler.group.push_back(line);
        } else if (tokens[0] == "trait") {
          assembler.group.push_back(line);
        } else {
          ok = false;
        }
        break;
      }
      case Section::bodies:
        if (tokens[0] == "body") {
          if (!assembler.body_lines.empty()) {
            ok = assembler.flush_body();
          }
          assembler.body_lines.push_back(line);
        } else {
          assembler.body_lines.push_back(line);
        }
        break;
      case Section::end:
      case Section::none:
        ok = false;
        break;
    }
    if (!ok) {
      if (assembler.error.empty()) {
        assembler.fail("line does not belong to the current section");
      }
      break;
    }
  }
  if (assembler.error.empty()) {
    if (!assembler.flush_all()) {
      // The failing section must already have recorded its message.
    }
  }
  if (!assembler.error.empty()) {
    if (error != nullptr) {
      error->line = assembler.line;
      error->message = assembler.error;
    }
    return AbcStatus::InconsistentModel;
  }
  if (assembler.file.major_version == 0) {
    if (error != nullptr) {
      error->line = 1;
      error->message = "the text has no abc version line";
    }
    return AbcStatus::InconsistentModel;
  }
  if (!assembler.check_counts()) {
    if (error != nullptr) {
      error->line = assembler.line;
      error->message = assembler.error;
    }
    return AbcStatus::InconsistentModel;
  }
  out = std::move(assembler.file);
  return AbcStatus::Ok;
}

}  // namespace fo4::abc
