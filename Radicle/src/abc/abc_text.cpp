#include <fo4/abc/abc_text.hpp>

#include <fo4/abc/abc.hpp>
#include <fo4/abc/opcodes.hpp>

#include "abc_text_internal.hpp"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace fo4::abc::detail::text {

namespace {

std::string double_bits(double value) {
  std::uint64_t bits = 0;
  static_assert(sizeof(bits) == sizeof(value));
  std::memcpy(&bits, &value, sizeof(bits));
  char buffer[24] = {};
  const auto written = std::to_chars(buffer, buffer + sizeof(buffer), bits, 16);
  return std::string{"0x"} + std::string{buffer, written.ptr};
}

std::string value_kind_or_none(AbcValueKind kind) {
  return kind == AbcValueKind::Undefined ? std::string{"none"} : kind_token(kind);
}

}  // namespace

std::string quote(std::string_view value) {
  std::string out;
  out.reserve(value.size() + 2);
  out.push_back('"');
  for (const char raw : value) {
    const auto byte = static_cast<unsigned char>(raw);
    switch (raw) {
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
        if (byte < 0x20u || byte >= 0x7Fu) {
          char buffer[8] = {};
          const auto written = std::to_chars(buffer, buffer + sizeof(buffer), byte, 16);
          out += "\\x";
          if (byte < 0x10u) {
            out += '0';
          }
          out += std::string{buffer, written.ptr};
        } else {
          out.push_back(raw);
        }
        break;
    }
  }
  out.push_back('"');
  return out;
}

std::string kind_token(AbcNamespaceKind kind) {
  switch (kind) {
    case AbcNamespaceKind::Private:
      return "private";
    case AbcNamespaceKind::Namespace:
      return "namespace";
    case AbcNamespaceKind::Package:
      return "package";
    case AbcNamespaceKind::PackageInternal:
      return "package_internal";
    case AbcNamespaceKind::Protected:
      return "protected";
    case AbcNamespaceKind::Explicit:
      return "explicit";
    case AbcNamespaceKind::StaticProtected:
      return "static_protected";
  }
  return "unknown";
}

std::string kind_token(AbcMultinameKind kind) {
  switch (kind) {
    case AbcMultinameKind::QName:
      return "qname";
    case AbcMultinameKind::QNameA:
      return "qname_a";
    case AbcMultinameKind::RTQName:
      return "rtqname";
    case AbcMultinameKind::RTQNameA:
      return "rtqname_a";
    case AbcMultinameKind::RTQNameL:
      return "rtqname_l";
    case AbcMultinameKind::RTQNameLA:
      return "rtqname_la";
    case AbcMultinameKind::Multiname:
      return "multiname";
    case AbcMultinameKind::MultinameA:
      return "multiname_a";
    case AbcMultinameKind::MultinameL:
      return "multinamel";
    case AbcMultinameKind::MultinameLA:
      return "multinamela";
    case AbcMultinameKind::TypeName:
      return "typename";
  }
  return "unknown";
}

std::string kind_token(AbcTraitKind kind) {
  switch (kind) {
    case AbcTraitKind::Slot:
      return "slot";
    case AbcTraitKind::Method:
      return "method";
    case AbcTraitKind::Getter:
      return "getter";
    case AbcTraitKind::Setter:
      return "setter";
    case AbcTraitKind::Class:
      return "class";
    case AbcTraitKind::Function:
      return "function";
    case AbcTraitKind::Const:
      return "const";
  }
  return "unknown";
}

std::string kind_token(AbcValueKind kind) {
  switch (kind) {
    case AbcValueKind::Undefined:
      return "undefined";
    case AbcValueKind::Utf8:
      return "utf8";
    case AbcValueKind::Int:
      return "int";
    case AbcValueKind::UInt:
      return "uint";
    case AbcValueKind::PrivateNs:
      return "private_ns";
    case AbcValueKind::Double:
      return "double";
    case AbcValueKind::Namespace:
      return "namespace";
    case AbcValueKind::False:
      return "false";
    case AbcValueKind::True:
      return "true";
    case AbcValueKind::Null:
      return "null";
    case AbcValueKind::PackageNamespace:
      return "package_namespace";
    case AbcValueKind::PackageInternalNs:
      return "package_internal_ns";
    case AbcValueKind::ProtectedNamespace:
      return "protected_namespace";
    case AbcValueKind::ExplicitNamespace:
      return "explicit_namespace";
    case AbcValueKind::StaticProtectedNs:
      return "static_protected_ns";
  }
  return "unknown";
}

std::string string_ref(const AbcFile& file, std::uint32_t index) {
  (void)file;
  return "@" + std::to_string(index);
}

std::string namespace_ref(const AbcFile& file, std::uint32_t index) {
  (void)file;
  return "@" + std::to_string(index);
}

std::string ns_set_ref(const AbcFile& file, std::uint32_t index) {
  (void)file;
  return "@" + std::to_string(index);
}

std::string multiname_ref(const AbcFile& file, std::uint32_t index) {
  (void)file;
  return "@" + std::to_string(index);
}

std::string method_ref(const AbcFile& file, std::uint32_t index) {
  (void)file;
  return "@" + std::to_string(index);
}

std::string class_ref(const AbcFile& file, std::uint32_t index) {
  (void)file;
  return "@" + std::to_string(index);
}

std::string int_ref(const AbcFile& file, std::uint32_t index) {
  (void)file;
  return "@" + std::to_string(index);
}

std::string uint_ref(const AbcFile& file, std::uint32_t index) {
  (void)file;
  return "@" + std::to_string(index);
}

std::string double_ref(const AbcFile& file, std::uint32_t index) {
  (void)file;
  return "@" + std::to_string(index);
}

std::string value_ref(const AbcFile& file, AbcValueKind kind, std::uint32_t index) {
  (void)file;
  return kind_token(kind) + " @" + std::to_string(index);
}

std::string render_traits(const AbcFile& file, const std::vector<AbcTrait>& traits) {
  std::string out;
  for (const AbcTrait& trait : traits) {
    out += "  trait " + kind_token(trait.kind);
    out += " name " + multiname_ref(file, trait.name);
    out += " attrs " + std::to_string(trait.attrs);
    switch (trait.kind) {
      case AbcTraitKind::Slot:
      case AbcTraitKind::Const:
        out += " slot " + std::to_string(trait.slot_id);
        out += " type " + multiname_ref(file, trait.type_name);
        out += " value " + value_kind_or_none(trait.value_kind) + " @" +
               std::to_string(trait.value_index);
        break;
      case AbcTraitKind::Class:
      case AbcTraitKind::Function:
        out += " slot " + std::to_string(trait.slot_id);
        out += " class " + class_ref(file, trait.class_index);
        break;
      case AbcTraitKind::Method:
      case AbcTraitKind::Getter:
      case AbcTraitKind::Setter:
        out += " disp " + std::to_string(trait.disp_id);
        out += " method " + method_ref(file, trait.method_index);
        break;
    }
    if (trait.has_metadata()) {
      out += " metadata";
      for (const std::uint32_t metadata : trait.metadata) {
        out += " @" + std::to_string(metadata);
      }
    }
    out += "\n";
  }
  return out;
}

}  // namespace fo4::abc::detail::text

namespace fo4::abc {

using namespace detail::text;

std::string render_abc_text(const AbcFile& file) {
  std::string out;
  out += "; Radicle ABC text form (phase 4)\n";
  out += "; Every pool entry is written by value; references name the pool index as @N so an unedited\n";
  out += "; round trip can restore the exact index while edits are re-interned by the assembler.\n";
  out += "abc " + std::to_string(file.major_version) + "." + std::to_string(file.minor_version) + "\n";

  out += "strings " + std::to_string(file.pool.strings.size()) + "\n";
  for (std::size_t index = 0; index < file.pool.strings.size(); ++index) {
    out += "  string " + std::to_string(index) + " " + quote(file.pool.strings[index]) + "\n";
  }
  out += "ints " + std::to_string(file.pool.ints.size()) + "\n";
  for (std::size_t index = 0; index < file.pool.ints.size(); ++index) {
    out += "  int " + std::to_string(index) + " " + std::to_string(file.pool.ints[index]) + "\n";
  }
  out += "uints " + std::to_string(file.pool.uints.size()) + "\n";
  for (std::size_t index = 0; index < file.pool.uints.size(); ++index) {
    out += "  uint " + std::to_string(index) + " " + std::to_string(file.pool.uints[index]) + "\n";
  }
  out += "doubles " + std::to_string(file.pool.doubles.size()) + "\n";
  for (std::size_t index = 0; index < file.pool.doubles.size(); ++index) {
    out += "  double " + std::to_string(index) + " " + double_bits(file.pool.doubles[index]) + "\n";
  }
  out += "namespaces " + std::to_string(file.pool.namespaces.size()) + "\n";
  for (std::size_t index = 0; index < file.pool.namespaces.size(); ++index) {
    const AbcNamespace& name_space = file.pool.namespaces[index];
    out += "  namespace " + std::to_string(index) + " " + kind_token(name_space.kind) + " " +
           string_ref(file, name_space.name) + "\n";
  }
  out += "ns_sets " + std::to_string(file.pool.ns_sets.size()) + "\n";
  for (std::size_t index = 0; index < file.pool.ns_sets.size(); ++index) {
    out += "  ns_set " + std::to_string(index);
    for (const std::uint32_t member : file.pool.ns_sets[index].namespaces) {
      out += " " + namespace_ref(file, member);
    }
    out += "\n";
  }
  out += "multinames " + std::to_string(file.pool.multinames.size()) + "\n";
  for (std::size_t index = 0; index < file.pool.multinames.size(); ++index) {
    const AbcMultiname& multiname = file.pool.multinames[index];
    out += "  multiname " + std::to_string(index) + " " + kind_token(multiname.kind);
    switch (multiname.kind) {
      case AbcMultinameKind::QName:
      case AbcMultinameKind::QNameA:
        out += " name " + string_ref(file, multiname.name) + " ns " +
               namespace_ref(file, multiname.ns);
        break;
      case AbcMultinameKind::RTQName:
      case AbcMultinameKind::RTQNameA:
        out += " name " + string_ref(file, multiname.name);
        break;
      case AbcMultinameKind::RTQNameL:
      case AbcMultinameKind::RTQNameLA:
        break;
      case AbcMultinameKind::Multiname:
      case AbcMultinameKind::MultinameA:
        out += " name " + string_ref(file, multiname.name) + " ns_set " +
               ns_set_ref(file, multiname.ns_set);
        break;
      case AbcMultinameKind::MultinameL:
      case AbcMultinameKind::MultinameLA:
        out += " ns_set " + ns_set_ref(file, multiname.ns_set);
        break;
      case AbcMultinameKind::TypeName:
        out += " base " + multiname_ref(file, multiname.name);
        for (const std::uint32_t param : multiname.params) {
          out += " param " + multiname_ref(file, param);
        }
        break;
    }
    out += "\n";
  }

  out += "metadata " + std::to_string(file.metadata.size()) + "\n";
  for (std::size_t index = 0; index < file.metadata.size(); ++index) {
    const AbcMetadata& metadata = file.metadata[index];
    out += "  metadata " + std::to_string(index) + " name " + string_ref(file, metadata.name) +
           " items " + std::to_string(metadata.items.size()) + "\n";
    for (const AbcMetadataItem& item : metadata.items) {
      out += "    item " + string_ref(file, item.key) + " " + string_ref(file, item.value) + "\n";
    }
  }

  out += "methods " + std::to_string(file.methods.size()) + "\n";
  for (std::size_t index = 0; index < file.methods.size(); ++index) {
    const AbcMethodInfo& method = file.methods[index];
    out += "  method " + std::to_string(index) + " name " + string_ref(file, method.name);
    out += " params " + std::to_string(method.param_count);
    out += " returns " + multiname_ref(file, method.return_type);
    for (const std::uint32_t param : method.param_types) {
      out += " param_type " + multiname_ref(file, param);
    }
    out += " flags " + std::to_string(method.flags);
    out += " options " + std::to_string(method.options.size());
    for (const AbcOptionalValue& option : method.options) {
      out += " option " + value_kind_or_none(option.kind) + " @" + std::to_string(option.index);
    }
    if (method.has_param_names()) {
      out += " param_names";
      for (const std::uint32_t param_name : method.param_names) {
        out += " " + string_ref(file, param_name);
      }
    }
    out += "\n";
  }

  out += "instances " + std::to_string(file.instances.size()) + "\n";
  for (std::size_t index = 0; index < file.instances.size(); ++index) {
    const AbcInstanceInfo& instance = file.instances[index];
    out += "  instance " + std::to_string(index) + " name " + multiname_ref(file, instance.name) +
           " super " + multiname_ref(file, instance.super_index);
    out += " flags " + std::to_string(instance.flags);
    if (instance.has_protected_ns()) {
      out += " protected " + namespace_ref(file, instance.protected_ns);
    }
    for (const std::uint32_t interface_index : instance.interfaces) {
      out += " interface " + multiname_ref(file, interface_index);
    }
    out += " iinit " + method_ref(file, instance.iinit);
    out += " traits " + std::to_string(instance.traits.size()) + "\n";
    out += render_traits(file, instance.traits);
  }

  out += "classes " + std::to_string(file.classes.size()) + "\n";
  for (std::size_t index = 0; index < file.classes.size(); ++index) {
    const AbcClassInfo& klass = file.classes[index];
    out += "  class " + std::to_string(index) + " cinit " + method_ref(file, klass.cinit) +
           " traits " + std::to_string(klass.traits.size()) + "\n";
    out += render_traits(file, klass.traits);
  }

  out += "scripts " + std::to_string(file.scripts.size()) + "\n";
  for (std::size_t index = 0; index < file.scripts.size(); ++index) {
    const AbcScriptInfo& script = file.scripts[index];
    out += "  script " + std::to_string(index) + " init " + method_ref(file, script.init) +
           " traits " + std::to_string(script.traits.size()) + "\n";
    out += render_traits(file, script.traits);
  }

  out += "bodies " + std::to_string(file.bodies.size()) + "\n";
  for (std::size_t index = 0; index < file.bodies.size(); ++index) {
    out += render_body(file, file.bodies[index], index);
  }
  out += "end\n";
  return out;
}

}  // namespace fo4::abc
