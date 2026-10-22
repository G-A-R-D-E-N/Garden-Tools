#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fo4::abc {

inline constexpr std::uint16_t kAbcMinorVersion = 16;
inline constexpr std::uint16_t kAbcMajorVersion = 46;

enum class AbcStatus : std::uint8_t {
  Ok = 0,
  Truncated,
  VarintTooLong,
  CountOutOfRange,
  IndexOutOfRange,
  BadNamespaceKind,
  BadMultinameKind,
  BadTraitKind,
  BadValueKind,
  BadOpcode,
  BranchOutOfRange,
  TrailingBytes,
  TypeNameTooLarge,
  InconsistentModel,
};

const char* status_name(AbcStatus status) noexcept;

class ByteReader {
public:
  ByteReader(const std::uint8_t* data, std::size_t size) noexcept;
  explicit ByteReader(const std::vector<std::uint8_t>& bytes) noexcept;

  AbcStatus status() const noexcept { return status_; }
  bool ok() const noexcept { return status_ == AbcStatus::Ok; }
  void fail(AbcStatus status) noexcept;
  std::size_t offset() const noexcept { return pos_; }
  std::size_t size() const noexcept { return size_; }
  std::size_t remaining() const noexcept { return pos_ <= size_ ? size_ - pos_ : 0; }
  bool has(std::size_t count) const noexcept { return remaining() >= count; }

  std::uint8_t u8();
  std::int8_t s8();
  std::uint16_t u16();
  std::uint32_t u30();
  std::int32_t s24();
  std::uint32_t u32();
  std::int32_t s32();
  double d64();
  std::string utf8();
  std::vector<std::uint8_t> block(std::size_t count);
  void skip(std::size_t count);

private:
  const std::uint8_t* data_;
  std::size_t size_;
  std::size_t pos_{0};
  AbcStatus status_{AbcStatus::Ok};
};

enum class AbcNamespaceKind : std::uint8_t {
  Private = 0x05,
  Namespace = 0x08,
  Package = 0x16,
  PackageInternal = 0x17,
  Protected = 0x18,
  Explicit = 0x19,
  StaticProtected = 0x1A,
};

enum class AbcMultinameKind : std::uint8_t {
  QName = 0x07,
  QNameA = 0x0D,
  RTQName = 0x0F,
  RTQNameA = 0x10,
  RTQNameL = 0x11,
  RTQNameLA = 0x12,
  Multiname = 0x09,
  MultinameA = 0x0E,
  MultinameL = 0x1B,
  MultinameLA = 0x1C,
  TypeName = 0x1D,
};

enum class AbcTraitKind : std::uint8_t {
  Slot = 0,
  Method = 1,
  Getter = 2,
  Setter = 3,
  Class = 4,
  Function = 5,
  Const = 6,
};

enum class AbcValueKind : std::uint8_t {
  Undefined = 0x00,
  Utf8 = 0x01,
  Int = 0x03,
  UInt = 0x04,
  PrivateNs = 0x05,
  Double = 0x06,
  Namespace = 0x08,
  False = 0x0A,
  True = 0x0B,
  Null = 0x0C,
  PackageNamespace = 0x16,
  PackageInternalNs = 0x17,
  ProtectedNamespace = 0x18,
  ExplicitNamespace = 0x19,
  StaticProtectedNs = 0x1A,
};

struct AbcNamespace {
  AbcNamespaceKind kind{AbcNamespaceKind::Namespace};
  std::uint32_t name{0};
};

struct AbcNsSet {
  std::vector<std::uint32_t> namespaces;
};

struct AbcMultiname {
  AbcMultinameKind kind{AbcMultinameKind::QName};
  std::uint32_t name{0};
  std::uint32_t ns{0};
  std::uint32_t ns_set{0};
  std::vector<std::uint32_t> params;
};

struct AbcConstantPool {
  std::vector<std::int32_t> ints;
  std::vector<std::uint32_t> uints;
  std::vector<double> doubles;
  std::vector<std::string> strings;
  std::vector<AbcNamespace> namespaces;
  std::vector<AbcNsSet> ns_sets;
  std::vector<AbcMultiname> multinames;
};

struct AbcOptionalValue {
  AbcValueKind kind{AbcValueKind::Undefined};
  std::uint32_t index{0};
};

struct AbcMethodInfo {
  std::uint32_t param_count{0};
  std::uint32_t return_type{0};
  std::vector<std::uint32_t> param_types;
  std::uint32_t name{0};
  std::uint8_t flags{0};
  std::vector<AbcOptionalValue> options;
  std::vector<std::uint32_t> param_names;

  bool needs_arguments() const noexcept { return (flags & 0x01u) != 0; }
  bool needs_activation() const noexcept { return (flags & 0x02u) != 0; }
  bool needs_rest() const noexcept { return (flags & 0x04u) != 0; }
  bool has_optional() const noexcept { return (flags & 0x08u) != 0; }
  bool ignores_rest() const noexcept { return (flags & 0x10u) != 0; }
  bool is_native() const noexcept { return (flags & 0x20u) != 0; }
  bool sets_dxns() const noexcept { return (flags & 0x40u) != 0; }
  bool has_param_names() const noexcept { return (flags & 0x80u) != 0; }
};

struct AbcMetadataItem {
  std::uint32_t key{0};
  std::uint32_t value{0};
};

struct AbcMetadata {
  std::uint32_t name{0};
  std::vector<AbcMetadataItem> items;
};

struct AbcTrait {
  std::uint32_t name{0};
  AbcTraitKind kind{AbcTraitKind::Slot};
  std::uint8_t attrs{0};
  std::uint32_t slot_id{0};
  std::uint32_t type_name{0};
  std::uint32_t value_index{0};
  AbcValueKind value_kind{AbcValueKind::Undefined};
  std::uint32_t class_index{0};
  std::uint32_t disp_id{0};
  std::uint32_t method_index{0};
  std::vector<std::uint32_t> metadata;

  bool is_final() const noexcept { return (attrs & 0x01u) != 0; }
  bool is_override() const noexcept { return (attrs & 0x02u) != 0; }
  bool has_metadata() const noexcept { return (attrs & 0x04u) != 0; }
};

struct AbcInstanceInfo {
  std::uint32_t name{0};
  std::uint32_t super_index{0};
  std::uint8_t flags{0};
  std::uint32_t protected_ns{0};
  std::vector<std::uint32_t> interfaces;
  std::uint32_t iinit{0};
  std::vector<AbcTrait> traits;

  bool is_sealed() const noexcept { return (flags & 0x01u) != 0; }
  bool is_final() const noexcept { return (flags & 0x02u) != 0; }
  bool is_interface() const noexcept { return (flags & 0x04u) != 0; }
  bool has_protected_ns() const noexcept { return (flags & 0x08u) != 0; }
};

struct AbcClassInfo {
  std::uint32_t cinit{0};
  std::vector<AbcTrait> traits;
};

struct AbcScriptInfo {
  std::uint32_t init{0};
  std::vector<AbcTrait> traits;
};

struct AbcException {
  std::uint32_t from{0};
  std::uint32_t to{0};
  std::uint32_t target{0};
  std::uint32_t exc_type{0};
  std::uint32_t var_name{0};
};

struct AbcMethodBody {
  std::uint32_t method{0};
  std::uint32_t max_stack{0};
  std::uint32_t local_count{0};
  std::uint32_t init_scope_depth{0};
  std::uint32_t max_scope_depth{0};
  std::vector<std::uint8_t> code;
  std::vector<AbcException> exceptions;
  std::vector<AbcTrait> traits;
};

struct AbcFile {
  std::uint16_t minor_version{0};
  std::uint16_t major_version{0};
  AbcConstantPool pool;
  std::vector<AbcMethodInfo> methods;
  std::vector<AbcMetadata> metadata;
  std::vector<AbcInstanceInfo> instances;
  std::vector<AbcClassInfo> classes;
  std::vector<AbcScriptInfo> scripts;
  std::vector<AbcMethodBody> bodies;

  static AbcStatus parse(const std::uint8_t* data, std::size_t size, AbcFile& out);
  static AbcStatus parse(const std::vector<std::uint8_t>& bytes, AbcFile& out);

  AbcStatus serialize(std::vector<std::uint8_t>& out) const;

  std::string string_at(std::uint32_t index) const;
  std::string namespace_at(std::uint32_t index) const;
  std::string multiname_at(std::uint32_t index) const;
  std::string method_name(std::uint32_t index) const;
  std::string class_name(std::uint32_t index) const;
  std::string optional_value_at(const AbcOptionalValue& value) const;
  const AbcMethodBody* body_for(std::uint32_t method_index) const;
};

}
