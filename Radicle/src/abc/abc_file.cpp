#include <fo4/abc/abc.hpp>

#include "abc_internal.hpp"

#include <cstring>

namespace fo4::abc {

namespace {

bool is_value_kind(std::uint32_t raw) {
  switch (raw) {
    case 0x00:
    case 0x01:
    case 0x03:
    case 0x04:
    case 0x05:
    case 0x06:
    case 0x08:
    case 0x0A:
    case 0x0B:
    case 0x0C:
    case 0x16:
    case 0x17:
    case 0x18:
    case 0x19:
    case 0x1A:
      return true;
    default:
      return false;
  }
}

std::string ns_set_name(const AbcFile& file, std::uint32_t index) {
  if (static_cast<std::size_t>(index) >= file.pool.ns_sets.size()) {
    return std::string{};
  }
  std::string out;
  const AbcNsSet& set = file.pool.ns_sets[index];
  for (const std::uint32_t ns : set.namespaces) {
    const std::string name = file.namespace_at(ns);
    if (name.empty()) {
      continue;
    }
    if (!out.empty()) {
      out += "|";
    }
    out += name;
  }
  return out;
}

bool count_fits(std::size_t count, std::size_t remaining, std::size_t entry_size) {
  return count <= remaining / entry_size + 1;
}

}

const char* status_name(AbcStatus status) noexcept {
  switch (status) {
    case AbcStatus::Ok:
      return "ok";
    case AbcStatus::Truncated:
      return "truncated";
    case AbcStatus::VarintTooLong:
      return "varint too long";
    case AbcStatus::CountOutOfRange:
      return "count out of range";
    case AbcStatus::IndexOutOfRange:
      return "index out of range";
    case AbcStatus::BadNamespaceKind:
      return "bad namespace kind";
    case AbcStatus::BadMultinameKind:
      return "bad multiname kind";
    case AbcStatus::BadTraitKind:
      return "bad trait kind";
    case AbcStatus::BadValueKind:
      return "bad value kind";
    case AbcStatus::BadOpcode:
      return "bad opcode";
    case AbcStatus::BranchOutOfRange:
      return "branch out of range";
    case AbcStatus::TrailingBytes:
      return "trailing bytes";
    case AbcStatus::TypeNameTooLarge:
      return "type name too large";
  }
  return "unknown";
}

ByteReader::ByteReader(const std::uint8_t* data, std::size_t size) noexcept
    : data_(data),
      size_(data == nullptr ? 0 : size),
      pos_(0),
      status_(data == nullptr ? AbcStatus::Truncated : AbcStatus::Ok) {}

ByteReader::ByteReader(const std::vector<std::uint8_t>& bytes) noexcept
    : data_(bytes.empty() ? nullptr : bytes.data()),
      size_(bytes.size()),
      pos_(0),
      status_(AbcStatus::Ok) {}

void ByteReader::fail(AbcStatus status) noexcept {
  if (status_ == AbcStatus::Ok) {
    status_ = status;
  }
}

std::uint8_t ByteReader::u8() {
  if (remaining() < 1) {
    fail(AbcStatus::Truncated);
    return 0;
  }
  return data_[pos_++];
}

std::int8_t ByteReader::s8() { return static_cast<std::int8_t>(u8()); }

std::uint16_t ByteReader::u16() {
  if (remaining() < 2) {
    fail(AbcStatus::Truncated);
    return 0;
  }
  const std::uint16_t value = static_cast<std::uint16_t>(
      static_cast<std::uint16_t>(data_[pos_]) |
      static_cast<std::uint16_t>(static_cast<std::uint16_t>(data_[pos_ + 1]) << 8));
  pos_ += 2;
  return value;
}

std::uint32_t ByteReader::u32() {
  if (!ok()) {
    return 0;
  }
  std::uint32_t value = 0;
  for (std::uint32_t shift = 0; shift < 35; shift += 7) {
    if (remaining() < 1) {
      fail(AbcStatus::Truncated);
      return 0;
    }
    const std::uint8_t byte = data_[pos_++];
    value |= static_cast<std::uint32_t>(byte & 0x7Fu) << shift;
    if ((byte & 0x80u) == 0u) {
      return value;
    }
  }
  fail(AbcStatus::VarintTooLong);
  return 0;
}

std::int32_t ByteReader::s24() {
  if (remaining() < 3) {
    fail(AbcStatus::Truncated);
    return 0;
  }
  const std::uint32_t raw = static_cast<std::uint32_t>(data_[pos_]) |
                            static_cast<std::uint32_t>(data_[pos_ + 1]) << 8 |
                            static_cast<std::uint32_t>(data_[pos_ + 2]) << 16;
  pos_ += 3;
  return (raw & 0x800000u) != 0u ? static_cast<std::int32_t>(raw) - 0x1000000
                                 : static_cast<std::int32_t>(raw);
}

std::uint32_t ByteReader::u30() { return u32() & 0x3FFFFFFFu; }

std::int32_t ByteReader::s32() { return static_cast<std::int32_t>(u32()); }

double ByteReader::d64() {
  if (remaining() < 8) {
    fail(AbcStatus::Truncated);
    return 0.0;
  }
  std::uint64_t bits = 0;
  for (std::size_t i = 0; i < 8; ++i) {
    bits |= static_cast<std::uint64_t>(data_[pos_ + i]) << (8 * i);
  }
  pos_ += 8;
  double value = 0.0;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

std::string ByteReader::utf8() {
  const std::uint32_t length = u30();
  if (!ok()) {
    return std::string{};
  }
  if (remaining() < length) {
    fail(AbcStatus::Truncated);
    return std::string{};
  }
  const std::string out(reinterpret_cast<const char*>(data_ + pos_), length);
  pos_ += length;
  return out;
}

std::vector<std::uint8_t> ByteReader::block(std::size_t count) {
  if (remaining() < count) {
    fail(AbcStatus::Truncated);
    return std::vector<std::uint8_t>{};
  }
  const std::vector<std::uint8_t> out(data_ + pos_, data_ + pos_ + count);
  pos_ += count;
  return out;
}

void ByteReader::skip(std::size_t count) {
  if (remaining() < count) {
    fail(AbcStatus::Truncated);
    return;
  }
  pos_ += count;
}

std::string AbcFile::string_at(std::uint32_t index) const {
  if (static_cast<std::size_t>(index) >= pool.strings.size()) {
    return std::string{};
  }
  return pool.strings[index];
}

std::string AbcFile::namespace_at(std::uint32_t index) const {
  if (static_cast<std::size_t>(index) >= pool.namespaces.size()) {
    return std::string{};
  }
  return string_at(pool.namespaces[index].name);
}

std::string AbcFile::multiname_at(std::uint32_t index) const {
  if (static_cast<std::size_t>(index) >= pool.multinames.size()) {
    return std::string{};
  }
  const AbcMultiname& multiname = pool.multinames[index];
  const std::string name = string_at(multiname.name);
  switch (multiname.kind) {
    case AbcMultinameKind::QName:
    case AbcMultinameKind::QNameA: {
      const std::string ns = namespace_at(multiname.ns);
      return ns.empty() ? name : ns + "::" + name;
    }
    case AbcMultinameKind::RTQName:
    case AbcMultinameKind::RTQNameA:
      return name;
    case AbcMultinameKind::RTQNameL:
    case AbcMultinameKind::RTQNameLA:
      return "*";
    case AbcMultinameKind::Multiname:
    case AbcMultinameKind::MultinameA: {
      const std::string ns = ns_set_name(*this, multiname.ns_set);
      return ns.empty() ? name : ns + "::" + name;
    }
    case AbcMultinameKind::MultinameL:
    case AbcMultinameKind::MultinameLA: {
      const std::string ns = ns_set_name(*this, multiname.ns_set);
      return ns.empty() ? std::string{"*"} : ns + "::*";
    }
    case AbcMultinameKind::TypeName: {
      std::string out = multiname_at(multiname.name);
      if (!multiname.params.empty()) {
        out += "<";
        for (std::size_t i = 0; i < multiname.params.size(); ++i) {
          if (i != 0) {
            out += ",";
          }
          out += multiname_at(multiname.params[i]);
        }
        out += ">";
      }
      return out;
    }
  }
  return name;
}

std::string AbcFile::method_name(std::uint32_t index) const {
  if (static_cast<std::size_t>(index) >= methods.size()) {
    return std::string{};
  }
  return string_at(methods[index].name);
}

std::string AbcFile::class_name(std::uint32_t index) const {
  if (static_cast<std::size_t>(index) >= instances.size()) {
    return std::string{};
  }
  return multiname_at(instances[index].name);
}

std::string AbcFile::optional_value_at(const AbcOptionalValue& value) const {
  switch (value.kind) {
    case AbcValueKind::Undefined:
      return "undefined";
    case AbcValueKind::Null:
      return "null";
    case AbcValueKind::True:
      return "true";
    case AbcValueKind::False:
      return "false";
    case AbcValueKind::Utf8:
      return string_at(value.index);
    case AbcValueKind::PrivateNs:
    case AbcValueKind::Namespace:
    case AbcValueKind::PackageNamespace:
    case AbcValueKind::PackageInternalNs:
    case AbcValueKind::ProtectedNamespace:
    case AbcValueKind::ExplicitNamespace:
    case AbcValueKind::StaticProtectedNs:
      return namespace_at(value.index);
    case AbcValueKind::Int: {
      if (static_cast<std::size_t>(value.index) >= pool.ints.size()) {
        return std::string{};
      }
      return std::to_string(pool.ints[value.index]);
    }
    case AbcValueKind::UInt: {
      if (static_cast<std::size_t>(value.index) >= pool.uints.size()) {
        return std::string{};
      }
      return std::to_string(pool.uints[value.index]);
    }
    case AbcValueKind::Double: {
      if (static_cast<std::size_t>(value.index) >= pool.doubles.size()) {
        return std::string{};
      }
      return std::to_string(pool.doubles[value.index]);
    }
  }
  return std::string{};
}

const AbcMethodBody* AbcFile::body_for(std::uint32_t method_index) const {
  for (const AbcMethodBody& body : bodies) {
    if (body.method == method_index) {
      return &body;
    }
  }
  return nullptr;
}

namespace detail {

AbcStatus parse_metadata(ByteReader& reader, std::vector<AbcMetadata>& out) {
  const std::uint32_t count = reader.u30();
  if (!reader.ok()) {
    return reader.status();
  }
  if (!count_fits(count, reader.remaining(), 2)) {
    return AbcStatus::CountOutOfRange;
  }
  out.resize(count);
  for (AbcMetadata& metadata : out) {
    metadata.name = reader.u30();
    const std::uint32_t items = reader.u30();
    if (!reader.ok()) {
      return reader.status();
    }
    if (!count_fits(items, reader.remaining(), 2)) {
      return AbcStatus::CountOutOfRange;
    }
    metadata.items.resize(items);
    for (AbcMetadataItem& item : metadata.items) {
      item.key = reader.u30();
      item.value = reader.u30();
      if (!reader.ok()) {
        return reader.status();
      }
    }
  }
  return AbcStatus::Ok;
}

AbcStatus parse_traits(ByteReader& reader, std::vector<AbcTrait>& out) {
  const std::uint32_t count = reader.u30();
  if (!reader.ok()) {
    return reader.status();
  }
  if (!count_fits(count, reader.remaining(), 2)) {
    return AbcStatus::CountOutOfRange;
  }
  out.resize(count);
  for (AbcTrait& trait : out) {
    trait.name = reader.u30();
    const std::uint32_t tag = reader.u8();
    if (!reader.ok()) {
      return reader.status();
    }
    const std::uint32_t kind = tag & 0x0Fu;
    trait.attrs = static_cast<std::uint8_t>(tag >> 4);
    if (kind > 6) {
      return AbcStatus::BadTraitKind;
    }
    trait.kind = static_cast<AbcTraitKind>(kind);
    if (kind == 0 || kind == 6) {
      trait.slot_id = reader.u30();
      trait.type_name = reader.u30();
      trait.value_index = reader.u30();
      if (!reader.ok()) {
        return reader.status();
      }
      if (trait.value_index != 0) {
        const std::uint32_t value_kind = reader.u8();
        if (!reader.ok()) {
          return reader.status();
        }
        if (!is_value_kind(value_kind)) {
          return AbcStatus::BadValueKind;
        }
        trait.value_kind = static_cast<AbcValueKind>(value_kind);
      }
    } else if (kind == 4 || kind == 5) {
      trait.slot_id = reader.u30();
      trait.class_index = reader.u30();
      if (!reader.ok()) {
        return reader.status();
      }
    } else {
      trait.disp_id = reader.u30();
      trait.method_index = reader.u30();
      if (!reader.ok()) {
        return reader.status();
      }
    }
    if (trait.has_metadata()) {
      const std::uint32_t metadata_count = reader.u30();
      if (!reader.ok()) {
        return reader.status();
      }
      if (!count_fits(metadata_count, reader.remaining(), 1)) {
        return AbcStatus::CountOutOfRange;
      }
      trait.metadata.resize(metadata_count);
      for (std::uint32_t& metadata_index : trait.metadata) {
        metadata_index = reader.u30();
        if (!reader.ok()) {
          return reader.status();
        }
      }
    }
  }
  return AbcStatus::Ok;
}

AbcStatus check_metadata_indices(const AbcFile& file) {
  const std::size_t pool_size = file.metadata.size();
  const auto has_bad_index = [pool_size](const std::vector<AbcTrait>& traits) {
    for (const AbcTrait& trait : traits) {
      for (const std::uint32_t metadata_index : trait.metadata) {
        if (static_cast<std::size_t>(metadata_index) >= pool_size) {
          return true;
        }
      }
    }
    return false;
  };
  for (const AbcInstanceInfo& instance : file.instances) {
    if (has_bad_index(instance.traits)) {
      return AbcStatus::IndexOutOfRange;
    }
  }
  for (const AbcClassInfo& klass : file.classes) {
    if (has_bad_index(klass.traits)) {
      return AbcStatus::IndexOutOfRange;
    }
  }
  for (const AbcScriptInfo& script : file.scripts) {
    if (has_bad_index(script.traits)) {
      return AbcStatus::IndexOutOfRange;
    }
  }
  for (const AbcMethodBody& body : file.bodies) {
    if (has_bad_index(body.traits)) {
      return AbcStatus::IndexOutOfRange;
    }
  }
  return AbcStatus::Ok;
}

}

namespace {

AbcStatus parse_methods(ByteReader& reader, AbcFile& out) {
  const std::uint32_t count = reader.u30();
  if (!reader.ok()) {
    return reader.status();
  }
  if (!count_fits(count, reader.remaining(), 4)) {
    return AbcStatus::CountOutOfRange;
  }
  out.methods.resize(count);
  for (AbcMethodInfo& method : out.methods) {
    method.param_count = reader.u30();
    method.return_type = reader.u30();
    if (!reader.ok()) {
      return reader.status();
    }
    if (!count_fits(method.param_count, reader.remaining(), 1)) {
      return AbcStatus::CountOutOfRange;
    }
    method.param_types.resize(method.param_count);
    for (std::uint32_t& param : method.param_types) {
      param = reader.u30();
      if (!reader.ok()) {
        return reader.status();
      }
    }
    method.name = reader.u30();
    method.flags = reader.u8();
    if (!reader.ok()) {
      return reader.status();
    }
    if (method.has_optional()) {
      const std::uint32_t optional_count = reader.u30();
      if (!reader.ok()) {
        return reader.status();
      }
      if (!count_fits(optional_count, reader.remaining(), 2)) {
        return AbcStatus::CountOutOfRange;
      }
      method.options.resize(optional_count);
      for (AbcOptionalValue& option : method.options) {
        option.index = reader.u30();
        const std::uint32_t value_kind = reader.u8();
        if (!reader.ok()) {
          return reader.status();
        }
        if (!is_value_kind(value_kind)) {
          return AbcStatus::BadValueKind;
        }
        option.kind = static_cast<AbcValueKind>(value_kind);
      }
    }
    if (method.has_param_names()) {
      method.param_names.resize(method.param_count);
      for (std::uint32_t& param_name : method.param_names) {
        param_name = reader.u30();
        if (!reader.ok()) {
          return reader.status();
        }
      }
    }
  }
  return AbcStatus::Ok;
}

AbcStatus parse_instances(ByteReader& reader, AbcFile& out) {
  const std::uint32_t count = reader.u30();
  if (!reader.ok()) {
    return reader.status();
  }
  if (!count_fits(count, reader.remaining(), 6)) {
    return AbcStatus::CountOutOfRange;
  }
  out.instances.resize(count);
  out.classes.resize(count);
  for (AbcInstanceInfo& instance : out.instances) {
    instance.name = reader.u30();
    instance.super_index = reader.u30();
    instance.flags = reader.u8();
    if (!reader.ok()) {
      return reader.status();
    }
    if (instance.has_protected_ns()) {
      instance.protected_ns = reader.u30();
      if (!reader.ok()) {
        return reader.status();
      }
    }
    const std::uint32_t interface_count = reader.u30();
    if (!reader.ok()) {
      return reader.status();
    }
    if (!count_fits(interface_count, reader.remaining(), 1)) {
      return AbcStatus::CountOutOfRange;
    }
    instance.interfaces.resize(interface_count);
    for (std::uint32_t& interface_index : instance.interfaces) {
      interface_index = reader.u30();
      if (!reader.ok()) {
        return reader.status();
      }
    }
    instance.iinit = reader.u30();
    if (!reader.ok()) {
      return reader.status();
    }
    const AbcStatus status = detail::parse_traits(reader, instance.traits);
    if (status != AbcStatus::Ok) {
      return status;
    }
  }
  for (AbcClassInfo& klass : out.classes) {
    klass.cinit = reader.u30();
    if (!reader.ok()) {
      return reader.status();
    }
    const AbcStatus status = detail::parse_traits(reader, klass.traits);
    if (status != AbcStatus::Ok) {
      return status;
    }
  }
  return AbcStatus::Ok;
}

AbcStatus parse_scripts(ByteReader& reader, AbcFile& out) {
  const std::uint32_t count = reader.u30();
  if (!reader.ok()) {
    return reader.status();
  }
  if (!count_fits(count, reader.remaining(), 2)) {
    return AbcStatus::CountOutOfRange;
  }
  out.scripts.resize(count);
  for (AbcScriptInfo& script : out.scripts) {
    script.init = reader.u30();
    if (!reader.ok()) {
      return reader.status();
    }
    const AbcStatus status = detail::parse_traits(reader, script.traits);
    if (status != AbcStatus::Ok) {
      return status;
    }
  }
  return AbcStatus::Ok;
}

AbcStatus parse_bodies(ByteReader& reader, AbcFile& out) {
  const std::uint32_t count = reader.u30();
  if (!reader.ok()) {
    return reader.status();
  }
  if (!count_fits(count, reader.remaining(), 8)) {
    return AbcStatus::CountOutOfRange;
  }
  out.bodies.resize(count);
  for (AbcMethodBody& body : out.bodies) {
    body.method = reader.u30();
    body.max_stack = reader.u30();
    body.local_count = reader.u30();
    body.init_scope_depth = reader.u30();
    body.max_scope_depth = reader.u30();
    const std::uint32_t code_length = reader.u30();
    if (!reader.ok()) {
      return reader.status();
    }
    body.code = reader.block(code_length);
    if (!reader.ok()) {
      return reader.status();
    }
    const std::uint32_t exception_count = reader.u30();
    if (!reader.ok()) {
      return reader.status();
    }
    if (!count_fits(exception_count, reader.remaining(), 5)) {
      return AbcStatus::CountOutOfRange;
    }
    body.exceptions.resize(exception_count);
    for (AbcException& exception : body.exceptions) {
      exception.from = reader.u30();
      exception.to = reader.u30();
      exception.target = reader.u30();
      exception.exc_type = reader.u30();
      exception.var_name = reader.u30();
      if (!reader.ok()) {
        return reader.status();
      }
    }
    const AbcStatus status = detail::parse_traits(reader, body.traits);
    if (status != AbcStatus::Ok) {
      return status;
    }
  }
  return AbcStatus::Ok;
}

}

AbcStatus AbcFile::parse(const std::uint8_t* data, std::size_t size, AbcFile& out) {
  out = AbcFile{};
  ByteReader reader(data, size);
  if (!reader.ok()) {
    return reader.status();
  }
  out.minor_version = reader.u16();
  out.major_version = reader.u16();
  if (!reader.ok()) {
    return reader.status();
  }
  AbcStatus status = detail::parse_constant_pool(reader, out.pool);
  if (status != AbcStatus::Ok) {
    return status;
  }
  status = parse_methods(reader, out);
  if (status != AbcStatus::Ok) {
    return status;
  }
  status = detail::parse_metadata(reader, out.metadata);
  if (status != AbcStatus::Ok) {
    return status;
  }
  status = parse_instances(reader, out);
  if (status != AbcStatus::Ok) {
    return status;
  }
  status = parse_scripts(reader, out);
  if (status != AbcStatus::Ok) {
    return status;
  }
  status = parse_bodies(reader, out);
  if (status != AbcStatus::Ok) {
    return status;
  }
  status = detail::check_metadata_indices(out);
  if (status != AbcStatus::Ok) {
    return status;
  }
  if (reader.remaining() != 0) {
    return AbcStatus::TrailingBytes;
  }
  return AbcStatus::Ok;
}

AbcStatus AbcFile::parse(const std::vector<std::uint8_t>& bytes, AbcFile& out) {
  return parse(bytes.empty() ? nullptr : bytes.data(), bytes.size(), out);
}

}
