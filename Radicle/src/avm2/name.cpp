#include <fo4/avm2/name.hpp>

#include <functional>
#include <utility>

namespace fo4::avm2 {

const char* namespace_kind_name(NamespaceKind kind) noexcept {
  switch (kind) {
    case NamespaceKind::Package:
      return "package";
    case NamespaceKind::PackageInternal:
      return "package-internal";
    case NamespaceKind::Protected:
      return "protected";
    case NamespaceKind::Explicit:
      return "explicit";
    case NamespaceKind::StaticProtected:
      return "static-protected";
    case NamespaceKind::Private:
      return "private";
    case NamespaceKind::Namespace:
      return "namespace";
  }
  return "unknown";
}

bool is_public_namespace(NamespaceKind kind) noexcept { return kind == NamespaceKind::Package; }

std::string render_namespace(const Namespace& name_space) {
  if (name_space.name.empty()) {
    return {};
  }
  return std::string{namespace_kind_name(name_space.kind)} + ":" + name_space.name;
}

std::string render_multiname(const Multiname& multiname) {
  if (multiname.kind == MultinameKind::TypeName) {
    std::string text = multiname.name;
    if (!multiname.type_parameters.empty()) {
      text += "<";
      for (std::size_t index = 0; index < multiname.type_parameters.size(); ++index) {
        if (index != 0) {
          text += ",";
        }
        text += multiname.type_parameters[index];
      }
      text += ">";
    }
    return text;
  }
  if (!multiname.has_fixed_name()) {
    return "*";
  }
  if (multiname.namespaces.empty()) {
    return multiname.name;
  }
  const Namespace& name_space = multiname.namespaces.front();
  if (is_public_namespace(name_space.kind)) {
    return name_space.name.empty() ? multiname.name : name_space.name + "::" + multiname.name;
  }
  return render_namespace(name_space) + "::" + multiname.name;
}

std::size_t KeyHash::operator()(const Key& key) const noexcept {
  std::size_t hash = std::hash<std::string>{}(key.name);
  hash ^= std::hash<std::size_t>{}(static_cast<std::size_t>(key.name_space.kind)) + 0x9E3779B9u +
          (hash << 6) + (hash >> 2);
  hash ^= std::hash<std::string>{}(key.name_space.name) + 0x9E3779B9u + (hash << 6) + (hash >> 2);
  return hash;
}

Key primary_key(const Multiname& multiname) {
  Key key;
  key.name = multiname.name;
  if (!multiname.namespaces.empty()) {
    key.name_space = multiname.namespaces.front();
  }
  return key;
}

bool key_matches(const Multiname& multiname, const Key& key) {
  if (multiname.kind == MultinameKind::TypeName) {
    return false;
  }
  if (multiname.has_fixed_name() && multiname.name != key.name) {
    return false;
  }
  if (!multiname.has_fixed_namespace()) {
    return true;
  }
  for (const Namespace& candidate : multiname.namespaces) {
    if (candidate == key.name_space) {
      return true;
    }
  }
  return false;
}

std::string rendered_class_name(std::string_view name) {
  const std::size_t dot = name.find_last_of('.');
  if (dot == std::string_view::npos || name.find("::") != std::string_view::npos) {
    return std::string{name};
  }
  std::string rendered{name};
  rendered.replace(dot, 1, "::");
  return rendered;
}

}  // namespace fo4::avm2
