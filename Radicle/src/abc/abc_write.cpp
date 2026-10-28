#include <fo4/abc/abc.hpp>

#include "abc_internal.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace fo4::abc {

namespace {

using Bytes = std::vector<std::uint8_t>;

void put_u16(Bytes& out, std::uint16_t value) {
  out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
  out.push_back(static_cast<std::uint8_t>(value >> 8));
}

void put_u32(Bytes& out, std::uint32_t value) {
  while (value >= 0x80u) {
    out.push_back(static_cast<std::uint8_t>((value & 0x7Fu) | 0x80u));
    value >>= 7;
  }
  out.push_back(static_cast<std::uint8_t>(value));
}

void put_size(Bytes& out, std::size_t count) { put_u32(out, static_cast<std::uint32_t>(count)); }

void put_d64(Bytes& out, double value) {
  std::uint64_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  for (std::size_t i = 0; i < 8; ++i) {
    out.push_back(static_cast<std::uint8_t>((bits >> (8 * i)) & 0xFFu));
  }
}

void put_string(Bytes& out, const std::string& value) {
  put_size(out, value.size());
  for (const char ch : value) {
    out.push_back(static_cast<std::uint8_t>(ch));
  }
}

void put_constant_pool(Bytes& out, const AbcConstantPool& pool) {
  put_size(out, pool.ints.size());
  for (std::size_t i = 1; i < pool.ints.size(); ++i) {
    put_u32(out, static_cast<std::uint32_t>(pool.ints[i]));
  }
  put_size(out, pool.uints.size());
  for (std::size_t i = 1; i < pool.uints.size(); ++i) {
    put_u32(out, pool.uints[i]);
  }
  put_size(out, pool.doubles.size());
  for (std::size_t i = 1; i < pool.doubles.size(); ++i) {
    put_d64(out, pool.doubles[i]);
  }
  put_size(out, pool.strings.size());
  for (std::size_t i = 1; i < pool.strings.size(); ++i) {
    put_string(out, pool.strings[i]);
  }
  put_size(out, pool.namespaces.size());
  for (std::size_t i = 1; i < pool.namespaces.size(); ++i) {
    out.push_back(static_cast<std::uint8_t>(pool.namespaces[i].kind));
    put_u32(out, pool.namespaces[i].name);
  }
  put_size(out, pool.ns_sets.size());
  for (std::size_t i = 1; i < pool.ns_sets.size(); ++i) {
    put_size(out, pool.ns_sets[i].namespaces.size());
    for (const std::uint32_t ns : pool.ns_sets[i].namespaces) {
      put_u32(out, ns);
    }
  }
  put_size(out, pool.multinames.size());
  for (std::size_t i = 1; i < pool.multinames.size(); ++i) {
    const AbcMultiname& multiname = pool.multinames[i];
    out.push_back(static_cast<std::uint8_t>(multiname.kind));
    switch (multiname.kind) {
      case AbcMultinameKind::QName:
      case AbcMultinameKind::QNameA:
        put_u32(out, multiname.ns);
        put_u32(out, multiname.name);
        break;
      case AbcMultinameKind::RTQName:
      case AbcMultinameKind::RTQNameA:
        put_u32(out, multiname.name);
        break;
      case AbcMultinameKind::RTQNameL:
      case AbcMultinameKind::RTQNameLA:
        break;
      case AbcMultinameKind::Multiname:
      case AbcMultinameKind::MultinameA:
        put_u32(out, multiname.name);
        put_u32(out, multiname.ns_set);
        break;
      case AbcMultinameKind::MultinameL:
      case AbcMultinameKind::MultinameLA:
        put_u32(out, multiname.ns_set);
        break;
      case AbcMultinameKind::TypeName:
        put_u32(out, multiname.name);
        put_size(out, multiname.params.size());
        for (const std::uint32_t param : multiname.params) {
          put_u32(out, param);
        }
        break;
    }
  }
}

void put_method_info(Bytes& out, const AbcMethodInfo& method) {
  put_u32(out, method.param_count);
  put_u32(out, method.return_type);
  for (const std::uint32_t param : method.param_types) {
    put_u32(out, param);
  }
  put_u32(out, method.name);
  out.push_back(method.flags);
  if (method.has_optional()) {
    put_size(out, method.options.size());
    for (const AbcOptionalValue& option : method.options) {
      put_u32(out, option.index);
      out.push_back(static_cast<std::uint8_t>(option.kind));
    }
  }
  if (method.has_param_names()) {
    for (const std::uint32_t param_name : method.param_names) {
      put_u32(out, param_name);
    }
  }
}

void put_metadata(Bytes& out, const AbcMetadata& metadata) {
  put_u32(out, metadata.name);
  put_size(out, metadata.items.size());
  for (const AbcMetadataItem& item : metadata.items) {
    put_u32(out, item.key);
  }
  for (const AbcMetadataItem& item : metadata.items) {
    put_u32(out, item.value);
  }
}

void put_trait(Bytes& out, const AbcTrait& trait);

void put_traits(Bytes& out, const std::vector<AbcTrait>& traits) {
  put_size(out, traits.size());
  for (const AbcTrait& trait : traits) {
    put_trait(out, trait);
  }
}

void put_trait(Bytes& out, const AbcTrait& trait) {
  put_u32(out, trait.name);
  const std::uint8_t tag =
      static_cast<std::uint8_t>(static_cast<std::uint8_t>(trait.kind) | (trait.attrs << 4));
  out.push_back(tag);
  switch (trait.kind) {
    case AbcTraitKind::Slot:
    case AbcTraitKind::Const:
      put_u32(out, trait.slot_id);
      put_u32(out, trait.type_name);
      put_u32(out, trait.value_index);
      if (trait.value_index != 0) {
        out.push_back(static_cast<std::uint8_t>(trait.value_kind));
      }
      break;
    case AbcTraitKind::Class:
    case AbcTraitKind::Function:
      put_u32(out, trait.slot_id);
      put_u32(out, trait.class_index);
      break;
    case AbcTraitKind::Method:
    case AbcTraitKind::Getter:
    case AbcTraitKind::Setter:
      put_u32(out, trait.disp_id);
      put_u32(out, trait.method_index);
      break;
  }
  if (trait.has_metadata()) {
    put_size(out, trait.metadata.size());
    for (const std::uint32_t metadata_index : trait.metadata) {
      put_u32(out, metadata_index);
    }
  }
}

void put_instance(Bytes& out, const AbcInstanceInfo& instance) {
  put_u32(out, instance.name);
  put_u32(out, instance.super_index);
  out.push_back(instance.flags);
  if (instance.has_protected_ns()) {
    put_u32(out, instance.protected_ns);
  }
  put_size(out, instance.interfaces.size());
  for (const std::uint32_t interface_index : instance.interfaces) {
    put_u32(out, interface_index);
  }
  put_u32(out, instance.iinit);
  put_traits(out, instance.traits);
}

void put_body(Bytes& out, const AbcMethodBody& body) {
  put_u32(out, body.method);
  put_u32(out, body.max_stack);
  put_u32(out, body.local_count);
  put_u32(out, body.init_scope_depth);
  put_u32(out, body.max_scope_depth);
  put_size(out, body.code.size());
  out.insert(out.end(), body.code.begin(), body.code.end());
  put_size(out, body.exceptions.size());
  for (const AbcException& exception : body.exceptions) {
    put_u32(out, exception.from);
    put_u32(out, exception.to);
    put_u32(out, exception.target);
    put_u32(out, exception.exc_type);
    put_u32(out, exception.var_name);
  }
  put_traits(out, body.traits);
}

bool fits_u30(std::uint32_t value) { return value <= 0x3FFFFFFFu; }

bool past_end(std::uint32_t index, std::size_t limit) {
  return static_cast<std::size_t>(index) >= limit;
}

bool namespace_kind_known(AbcNamespaceKind kind) {
  switch (kind) {
    case AbcNamespaceKind::Private:
    case AbcNamespaceKind::Namespace:
    case AbcNamespaceKind::Package:
    case AbcNamespaceKind::PackageInternal:
    case AbcNamespaceKind::Protected:
    case AbcNamespaceKind::Explicit:
    case AbcNamespaceKind::StaticProtected:
      return true;
  }
  return false;
}

bool multiname_kind_known(AbcMultinameKind kind) {
  switch (kind) {
    case AbcMultinameKind::QName:
    case AbcMultinameKind::QNameA:
    case AbcMultinameKind::RTQName:
    case AbcMultinameKind::RTQNameA:
    case AbcMultinameKind::RTQNameL:
    case AbcMultinameKind::RTQNameLA:
    case AbcMultinameKind::Multiname:
    case AbcMultinameKind::MultinameA:
    case AbcMultinameKind::MultinameL:
    case AbcMultinameKind::MultinameLA:
    case AbcMultinameKind::TypeName:
      return true;
  }
  return false;
}

bool value_kind_known(AbcValueKind kind) {
  switch (kind) {
    case AbcValueKind::Undefined:
    case AbcValueKind::Utf8:
    case AbcValueKind::Int:
    case AbcValueKind::UInt:
    case AbcValueKind::PrivateNs:
    case AbcValueKind::Double:
    case AbcValueKind::Namespace:
    case AbcValueKind::False:
    case AbcValueKind::True:
    case AbcValueKind::Null:
    case AbcValueKind::PackageNamespace:
    case AbcValueKind::PackageInternalNs:
    case AbcValueKind::ProtectedNamespace:
    case AbcValueKind::ExplicitNamespace:
    case AbcValueKind::StaticProtectedNs:
      return true;
  }
  return false;
}

bool methods_encodable(const std::vector<AbcMethodInfo>& methods) {
  for (const AbcMethodInfo& method : methods) {
    if (!fits_u30(method.param_count) || method.param_types.size() != method.param_count) {
      return false;
    }
    if (method.has_optional() ? method.options.size() > method.param_count
                              : !method.options.empty()) {
      return false;
    }
    if (method.has_param_names() ? method.param_names.size() != method.param_count
                                 : !method.param_names.empty()) {
      return false;
    }
  }
  return true;
}

bool traits_encodable(const std::vector<AbcTrait>& traits) {
  for (const AbcTrait& trait : traits) {
    if (static_cast<std::uint8_t>(trait.kind) > static_cast<std::uint8_t>(AbcTraitKind::Const) ||
        trait.attrs > 0x0F || !fits_u30(trait.slot_id) || !fits_u30(trait.disp_id)) {
      return false;
    }
    if (!trait.has_metadata() && !trait.metadata.empty()) {
      return false;
    }
  }
  return true;
}

bool model_encodable(const AbcFile& file) {
  if (file.classes.size() != file.instances.size() || !methods_encodable(file.methods)) {
    return false;
  }
  for (const AbcNamespace& name_space : file.pool.namespaces) {
    if (!fits_u30(name_space.name)) {
      return false;
    }
  }
  for (const AbcInstanceInfo& instance : file.instances) {
    if (!instance.has_protected_ns() && instance.protected_ns != 0) {
      return false;
    }
    if (!traits_encodable(instance.traits)) {
      return false;
    }
  }
  for (const AbcClassInfo& klass : file.classes) {
    if (!traits_encodable(klass.traits)) {
      return false;
    }
  }
  for (const AbcScriptInfo& script : file.scripts) {
    if (!traits_encodable(script.traits)) {
      return false;
    }
  }
  for (const AbcMethodBody& body : file.bodies) {
    if (!traits_encodable(body.traits) || !fits_u30(body.max_stack) ||
        !fits_u30(body.local_count) || !fits_u30(body.init_scope_depth) ||
        !fits_u30(body.max_scope_depth)) {
      return false;
    }
    for (const AbcException& exception : body.exceptions) {
      if (!fits_u30(exception.from) || !fits_u30(exception.to) || !fits_u30(exception.target)) {
        return false;
      }
    }
  }
  return true;
}

bool multiname_refs_past_end(const AbcConstantPool& pool, std::size_t index,
                             const AbcMultiname& multiname) {
  const bool name_past_end =
      multiname.name != 0 && past_end(multiname.name, pool.strings.size());
  switch (multiname.kind) {
    case AbcMultinameKind::QName:
    case AbcMultinameKind::QNameA:
      return past_end(multiname.ns, pool.namespaces.size()) || name_past_end;
    case AbcMultinameKind::RTQName:
    case AbcMultinameKind::RTQNameA:
      return name_past_end;
    case AbcMultinameKind::RTQNameL:
    case AbcMultinameKind::RTQNameLA:
      return false;
    case AbcMultinameKind::Multiname:
    case AbcMultinameKind::MultinameA:
      return name_past_end || past_end(multiname.ns_set, pool.ns_sets.size());
    case AbcMultinameKind::MultinameL:
    case AbcMultinameKind::MultinameLA:
      return past_end(multiname.ns_set, pool.ns_sets.size());
    case AbcMultinameKind::TypeName:
      if (past_end(multiname.name, index)) {
        return true;
      }
      for (const std::uint32_t param : multiname.params) {
        if (past_end(param, index)) {
          return true;
        }
      }
      return false;
  }
  return false;
}

bool traits_values_known(const std::vector<AbcTrait>& traits) {
  for (const AbcTrait& trait : traits) {
    const bool has_value = trait.kind == AbcTraitKind::Slot || trait.kind == AbcTraitKind::Const;
    if (has_value && trait.value_index != 0 && !value_kind_known(trait.value_kind)) {
      return false;
    }
  }
  return true;
}

AbcStatus pool_encodable(const AbcFile& file) {
  const AbcConstantPool& pool = file.pool;
  for (std::size_t i = 1; i < pool.namespaces.size(); ++i) {
    if (!namespace_kind_known(pool.namespaces[i].kind)) {
      return AbcStatus::BadNamespaceKind;
    }
  }
  for (std::size_t i = 1; i < pool.ns_sets.size(); ++i) {
    for (const std::uint32_t ns : pool.ns_sets[i].namespaces) {
      if (past_end(ns, pool.namespaces.size())) {
        return AbcStatus::IndexOutOfRange;
      }
    }
  }
  for (std::size_t i = 1; i < pool.multinames.size(); ++i) {
    const AbcMultiname& multiname = pool.multinames[i];
    if (!multiname_kind_known(multiname.kind)) {
      return AbcStatus::BadMultinameKind;
    }
    if (multiname_refs_past_end(pool, i, multiname)) {
      return AbcStatus::IndexOutOfRange;
    }
  }
  for (const AbcMethodInfo& method : file.methods) {
    for (const AbcOptionalValue& option : method.options) {
      if (!value_kind_known(option.kind)) {
        return AbcStatus::BadValueKind;
      }
    }
  }
  for (const AbcInstanceInfo& instance : file.instances) {
    if (!traits_values_known(instance.traits)) {
      return AbcStatus::BadValueKind;
    }
  }
  for (const AbcClassInfo& klass : file.classes) {
    if (!traits_values_known(klass.traits)) {
      return AbcStatus::BadValueKind;
    }
  }
  for (const AbcScriptInfo& script : file.scripts) {
    if (!traits_values_known(script.traits)) {
      return AbcStatus::BadValueKind;
    }
  }
  for (const AbcMethodBody& body : file.bodies) {
    if (!traits_values_known(body.traits)) {
      return AbcStatus::BadValueKind;
    }
  }
  return AbcStatus::Ok;
}

}

AbcStatus AbcFile::serialize(std::vector<std::uint8_t>& out) const {
  out.clear();
  if (!model_encodable(*this)) {
    return AbcStatus::InconsistentModel;
  }
  const AbcStatus index_status = detail::check_pool_indices(*this);
  if (index_status != AbcStatus::Ok) {
    return index_status;
  }
  const AbcStatus pool_status = pool_encodable(*this);
  if (pool_status != AbcStatus::Ok) {
    return pool_status;
  }
  put_u16(out, minor_version);
  put_u16(out, major_version);
  put_constant_pool(out, pool);
  put_size(out, methods.size());
  for (const AbcMethodInfo& method : methods) {
    put_method_info(out, method);
  }
  put_size(out, metadata.size());
  for (const AbcMetadata& entry : metadata) {
    put_metadata(out, entry);
  }
  put_size(out, instances.size());
  for (const AbcInstanceInfo& instance : instances) {
    put_instance(out, instance);
  }
  for (const AbcClassInfo& klass : classes) {
    put_u32(out, klass.cinit);
    put_traits(out, klass.traits);
  }
  put_size(out, scripts.size());
  for (const AbcScriptInfo& script : scripts) {
    put_u32(out, script.init);
    put_traits(out, script.traits);
  }
  put_size(out, bodies.size());
  for (const AbcMethodBody& body : bodies) {
    put_body(out, body);
  }
  return AbcStatus::Ok;
}

}
