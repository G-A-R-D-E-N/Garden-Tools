#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::avm2 {

enum class NamespaceKind : std::uint8_t {
  Package = 0x16,
  PackageInternal = 0x17,
  Protected = 0x18,
  Explicit = 0x19,
  StaticProtected = 0x1A,
  Private = 0x05,
  Namespace = 0x08,
};

const char* namespace_kind_name(NamespaceKind kind) noexcept;
bool is_public_namespace(NamespaceKind kind) noexcept;

struct Namespace {
  NamespaceKind kind{NamespaceKind::Package};
  std::string name;

  bool operator==(const Namespace& other) const = default;
};

std::string render_namespace(const Namespace& name_space);

enum class MultinameKind : std::uint8_t {
  QName = 0,
  QNameSet,
  RuntimeNamespace,
  RuntimeName,
  Dynamic,
  TypeName,
};

struct Multiname {
  MultinameKind kind{MultinameKind::QName};
  std::string name;
  std::vector<Namespace> namespaces;

  std::vector<std::string> type_parameters;
  bool attribute{false};

  bool has_fixed_name() const noexcept {
    return kind != MultinameKind::RuntimeName && kind != MultinameKind::Dynamic;
  }
  bool has_fixed_namespace() const noexcept {
    return kind != MultinameKind::RuntimeNamespace && kind != MultinameKind::Dynamic;
  }
};

std::string render_multiname(const Multiname& multiname);
std::string rendered_class_name(std::string_view name);

struct Key {
  Namespace name_space;
  std::string name;

  bool operator==(const Key& other) const = default;
};

struct KeyHash {
  std::size_t operator()(const Key& key) const noexcept;
};

Key primary_key(const Multiname& multiname);

bool key_matches(const Multiname& multiname, const Key& key);

}  // namespace fo4::avm2
