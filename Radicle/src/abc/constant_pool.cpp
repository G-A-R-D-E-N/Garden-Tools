#include <fo4/abc/abc.hpp>

#include "abc_internal.hpp"

namespace fo4::abc {
namespace detail {

namespace {

AbcStatus read_count(ByteReader& reader, std::uint32_t& count, std::size_t entry_size) {
  count = reader.u30();
  if (!reader.ok()) {
    return reader.status();
  }
  if (static_cast<std::size_t>(count) > reader.remaining() / entry_size + 1) {
    return AbcStatus::CountOutOfRange;
  }
  return AbcStatus::Ok;
}

bool is_namespace_kind(std::uint32_t raw) {
  switch (raw) {
    case 0x05:
    case 0x08:
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

bool is_multiname_kind(std::uint32_t raw) {
  switch (raw) {
    case 0x07:
    case 0x09:
    case 0x0D:
    case 0x0E:
    case 0x0F:
    case 0x10:
    case 0x11:
    case 0x12:
    case 0x1B:
    case 0x1C:
    case 0x1D:
      return true;
    default:
      return false;
  }
}

AbcStatus parse_int_pool(ByteReader& reader, AbcConstantPool& pool) {
  std::uint32_t count = 0;
  const AbcStatus status = read_count(reader, count, 1);
  if (status != AbcStatus::Ok) {
    return status;
  }
  pool.ints.assign(static_cast<std::size_t>(count), 0);
  for (std::size_t i = 1; i < pool.ints.size(); ++i) {
    pool.ints[i] = reader.s32();
    if (!reader.ok()) {
      return reader.status();
    }
  }
  return AbcStatus::Ok;
}

AbcStatus parse_uint_pool(ByteReader& reader, AbcConstantPool& pool) {
  std::uint32_t count = 0;
  const AbcStatus status = read_count(reader, count, 1);
  if (status != AbcStatus::Ok) {
    return status;
  }
  pool.uints.assign(static_cast<std::size_t>(count), 0);
  for (std::size_t i = 1; i < pool.uints.size(); ++i) {
    pool.uints[i] = reader.u32();
    if (!reader.ok()) {
      return reader.status();
    }
  }
  return AbcStatus::Ok;
}

AbcStatus parse_double_pool(ByteReader& reader, AbcConstantPool& pool) {
  std::uint32_t count = 0;
  const AbcStatus status = read_count(reader, count, 8);
  if (status != AbcStatus::Ok) {
    return status;
  }
  pool.doubles.assign(static_cast<std::size_t>(count), 0.0);
  for (std::size_t i = 1; i < pool.doubles.size(); ++i) {
    pool.doubles[i] = reader.d64();
    if (!reader.ok()) {
      return reader.status();
    }
  }
  return AbcStatus::Ok;
}

AbcStatus parse_string_pool(ByteReader& reader, AbcConstantPool& pool) {
  std::uint32_t count = 0;
  const AbcStatus status = read_count(reader, count, 1);
  if (status != AbcStatus::Ok) {
    return status;
  }
  pool.strings.assign(static_cast<std::size_t>(count), std::string{});
  for (std::size_t i = 1; i < pool.strings.size(); ++i) {
    pool.strings[i] = reader.utf8();
    if (!reader.ok()) {
      return reader.status();
    }
  }
  return AbcStatus::Ok;
}

AbcStatus parse_namespace_pool(ByteReader& reader, AbcConstantPool& pool) {
  std::uint32_t count = 0;
  const AbcStatus status = read_count(reader, count, 2);
  if (status != AbcStatus::Ok) {
    return status;
  }
  pool.namespaces.assign(static_cast<std::size_t>(count), AbcNamespace{});
  for (std::size_t i = 1; i < pool.namespaces.size(); ++i) {
    const std::uint32_t kind = reader.u8();
    const std::uint32_t name = reader.u30();
    if (!reader.ok()) {
      return reader.status();
    }
    if (!is_namespace_kind(kind)) {
      return AbcStatus::BadNamespaceKind;
    }
    pool.namespaces[i].kind = static_cast<AbcNamespaceKind>(kind);
    pool.namespaces[i].name = name;
  }
  return AbcStatus::Ok;
}

AbcStatus parse_ns_set_pool(ByteReader& reader, AbcConstantPool& pool) {
  std::uint32_t count = 0;
  const AbcStatus status = read_count(reader, count, 1);
  if (status != AbcStatus::Ok) {
    return status;
  }
  pool.ns_sets.assign(static_cast<std::size_t>(count), AbcNsSet{});
  for (std::size_t i = 1; i < pool.ns_sets.size(); ++i) {
    const std::uint32_t members = reader.u30();
    if (!reader.ok()) {
      return reader.status();
    }
    if (static_cast<std::size_t>(members) > reader.remaining()) {
      return AbcStatus::CountOutOfRange;
    }
    pool.ns_sets[i].namespaces.resize(members);
    for (std::size_t j = 0; j < pool.ns_sets[i].namespaces.size(); ++j) {
      const std::uint32_t index = reader.u30();
      if (!reader.ok()) {
        return reader.status();
      }
      if (static_cast<std::size_t>(index) >= pool.namespaces.size()) {
        return AbcStatus::IndexOutOfRange;
      }
      pool.ns_sets[i].namespaces[j] = index;
    }
  }
  return AbcStatus::Ok;
}

AbcStatus parse_multiname_pool(ByteReader& reader, AbcConstantPool& pool) {
  std::uint32_t count = 0;
  const AbcStatus status = read_count(reader, count, 1);
  if (status != AbcStatus::Ok) {
    return status;
  }
  pool.multinames.assign(static_cast<std::size_t>(count), AbcMultiname{});
  std::vector<std::size_t> expansion(pool.multinames.size(), 1);
  for (std::size_t i = 1; i < pool.multinames.size(); ++i) {
    const std::uint32_t kind = reader.u8();
    if (!reader.ok()) {
      return reader.status();
    }
    if (!is_multiname_kind(kind)) {
      return AbcStatus::BadMultinameKind;
    }
    AbcMultiname& multiname = pool.multinames[i];
    multiname.kind = static_cast<AbcMultinameKind>(kind);
    switch (kind) {
      case 0x07:
      case 0x0D: {
        multiname.ns = reader.u30();
        multiname.name = reader.u30();
        if (!reader.ok()) {
          return reader.status();
        }
        if (static_cast<std::size_t>(multiname.ns) >= pool.namespaces.size()) {
          return AbcStatus::IndexOutOfRange;
        }
        break;
      }
      case 0x0F:
      case 0x10: {
        multiname.name = reader.u30();
        if (!reader.ok()) {
          return reader.status();
        }
        break;
      }
      case 0x11:
      case 0x12:
        break;
      case 0x09:
      case 0x0E: {
        multiname.name = reader.u30();
        multiname.ns_set = reader.u30();
        if (!reader.ok()) {
          return reader.status();
        }
        if (static_cast<std::size_t>(multiname.ns_set) >= pool.ns_sets.size()) {
          return AbcStatus::IndexOutOfRange;
        }
        break;
      }
      case 0x1B:
      case 0x1C: {
        multiname.ns_set = reader.u30();
        if (!reader.ok()) {
          return reader.status();
        }
        if (static_cast<std::size_t>(multiname.ns_set) >= pool.ns_sets.size()) {
          return AbcStatus::IndexOutOfRange;
        }
        break;
      }
      case 0x1D: {
        multiname.name = reader.u30();
        const std::uint32_t params = reader.u30();
        if (!reader.ok()) {
          return reader.status();
        }
        if (static_cast<std::size_t>(params) > reader.remaining()) {
          return AbcStatus::CountOutOfRange;
        }
        if (static_cast<std::size_t>(multiname.name) >= i) {
          return AbcStatus::IndexOutOfRange;
        }
        multiname.params.resize(params);
        std::size_t nodes = 1 + expansion[multiname.name];
        if (nodes > kMaxTypeNameExpansion) {
          return AbcStatus::TypeNameTooLarge;
        }
        for (std::size_t j = 0; j < multiname.params.size(); ++j) {
          const std::uint32_t index = reader.u30();
          if (!reader.ok()) {
            return reader.status();
          }
          if (static_cast<std::size_t>(index) >= i) {
            return AbcStatus::IndexOutOfRange;
          }
          multiname.params[j] = index;
          nodes += expansion[index];
          if (nodes > kMaxTypeNameExpansion) {
            return AbcStatus::TypeNameTooLarge;
          }
        }
        expansion[i] = nodes;
        break;
      }
      default:
        return AbcStatus::BadMultinameKind;
    }
    if (kind != 0x1Du && multiname.name != 0 &&
        static_cast<std::size_t>(multiname.name) >= pool.strings.size()) {
      return AbcStatus::IndexOutOfRange;
    }
  }
  return AbcStatus::Ok;
}

}

AbcStatus parse_constant_pool(ByteReader& reader, AbcConstantPool& out) {
  AbcStatus status = parse_int_pool(reader, out);
  if (status != AbcStatus::Ok) {
    return status;
  }
  status = parse_uint_pool(reader, out);
  if (status != AbcStatus::Ok) {
    return status;
  }
  status = parse_double_pool(reader, out);
  if (status != AbcStatus::Ok) {
    return status;
  }
  status = parse_string_pool(reader, out);
  if (status != AbcStatus::Ok) {
    return status;
  }
  status = parse_namespace_pool(reader, out);
  if (status != AbcStatus::Ok) {
    return status;
  }
  status = parse_ns_set_pool(reader, out);
  if (status != AbcStatus::Ok) {
    return status;
  }
  return parse_multiname_pool(reader, out);
}

}
}
