#pragma once

#include <fo4/abc/abc.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace avm2_test {

inline std::uint8_t push_byte_opcode() { return 0x24; }

class CodeBuilder {
 public:
  void byte(std::uint32_t value) { code_.push_back(static_cast<std::uint8_t>(value & 0xFFu)); }

  void u30(std::uint32_t value) {
    while (value >= 0x80u) {
      byte((value & 0x7Fu) | 0x80u);
      value >>= 7;
    }
    byte(value);
  }

  void op(std::uint8_t opcode) { byte(opcode); }

  void op_u30(std::uint8_t opcode, std::uint32_t value) {
    byte(opcode);
    u30(value);
  }

  void op_multiname(std::uint8_t opcode, std::uint32_t multiname) {
    byte(opcode);
    u30(multiname);
  }

  void label(std::string name) { labels_[std::move(name)] = code_.size(); }

  void branch(std::uint8_t opcode, std::string target) {
    byte(opcode);
    const std::size_t at = code_.size();
    code_.resize(code_.size() + 3, 0);
    patches_.push_back(Patch{at, std::move(target)});
  }

  std::size_t position() const { return code_.size(); }

  std::size_t offset_of(const std::string& label) const {
    const auto found = labels_.find(label);
    if (found == labels_.end()) {
      throw std::runtime_error("unknown label " + label);
    }
    return found->second;
  }

  void newcatch(std::uint32_t exception_index) { op_u30(0x5A, exception_index); }

  void pushbyte(std::int32_t value) { byte(push_byte_opcode()); byte(static_cast<std::uint32_t>(value) & 0xFFu); }
  void pushnull() { op(0x20); }
  void pushundefined() { op(0x21); }
  void pushtrue() { op(0x26); }
  void pushfalse() { op(0x27); }
  void pushstring(std::uint32_t string_index) { op_u30(0x2C, string_index); }
  void pushint(std::uint32_t int_index) { op_u30(0x2D, int_index); }
  void pushdouble(std::uint32_t double_index) { op_u30(0x2F, double_index); }
  void pop() { op(0x29); }
  void dup() { op(0x2A); }
  void swap() { op(0x2B); }
  void getlocal(std::uint32_t index) { op_u30(0x62, index); }
  void setlocal(std::uint32_t index) { op_u30(0x63, index); }
  void getlocal0() { op(0xD0); }
  void setlocal0() { op(0xD4); }
  void kill(std::uint32_t index) { op_u30(0x08, index); }
  void getglobalscope() { op(0x64); }
  void pushscope() { op(0x30); }
  void popscope() { op(0x1D); }
  void jump(std::string target) { branch(0x10, std::move(target)); }
  void iftrue(std::string target) { branch(0x11, std::move(target)); }
  void iffalse(std::string target) { branch(0x12, std::move(target)); }
  void ifeq(std::string target) { branch(0x13, std::move(target)); }
  void ifne(std::string target) { branch(0x14, std::move(target)); }
  void iflt(std::string target) { branch(0x15, std::move(target)); }
  void ifle(std::string target) { branch(0x16, std::move(target)); }
  void ifgt(std::string target) { branch(0x17, std::move(target)); }
  void ifge(std::string target) { branch(0x18, std::move(target)); }
  void add() { op(0xA0); }
  void subtract() { op(0xA1); }
  void multiply() { op(0xA2); }
  void divide() { op(0xA3); }
  void modulo() { op(0xA4); }
  void negate() { op(0x90); }
  void increment() { op(0x91); }
  void decrement() { op(0x93); }
  void bit_or() { op(0xA9); }
  void bit_and() { op(0xA8); }
  void bitxor() { op(0xAA); }
  void lshift() { op(0xA5); }
  void rshift() { op(0xA6); }
  void not_op() { op(0x96); }
  void typeof_op() { op(0x95); }
  void equals() { op(0xAB); }
  void strictequals() { op(0xAC); }
  void lessthan() { op(0xAD); }
  void greaterthan() { op(0xAF); }
  void convert_i() { op(0x73); }
  void convert_s() { op(0x70); }
  void convert_b() { op(0x76); }
  void coerce(std::uint32_t multiname) { op_multiname(0x80, multiname); }
  void getproperty(std::uint32_t multiname) { op_multiname(0x66, multiname); }
  void setproperty(std::uint32_t multiname) { op_multiname(0x61, multiname); }
  void initproperty(std::uint32_t multiname) { op_multiname(0x68, multiname); }
  void deleteproperty(std::uint32_t multiname) { op_multiname(0x6A, multiname); }
  void getlex(std::uint32_t multiname) { op_multiname(0x60, multiname); }
  void findpropstrict(std::uint32_t multiname) { op_multiname(0x5D, multiname); }
  void findproperty(std::uint32_t multiname) { op_multiname(0x5E, multiname); }
  void newobject(std::uint32_t count) { op_u30(0x55, count); }
  void newarray(std::uint32_t count) { op_u30(0x56, count); }
  void newfunction(std::uint32_t method) { op_u30(0x40, method); }
  void newclass(std::uint32_t instance) { op_u30(0x58, instance); }
  void newactivation() { op(0x57); }
  void call(std::uint32_t count) { op_u30(0x41, count); }
  void callproperty(std::uint32_t multiname, std::uint32_t count) {
    op_multiname(0x46, multiname);
    u30(count);
  }
  void callpropvoid(std::uint32_t multiname, std::uint32_t count) {
    op_multiname(0x4F, multiname);
    u30(count);
  }
  void construct(std::uint32_t count) { op_u30(0x42, count); }
  void constructprop(std::uint32_t multiname, std::uint32_t count) {
    op_multiname(0x4A, multiname);
    u30(count);
  }
  void constructsuper(std::uint32_t count) { op_u30(0x49, count); }
  void applytype(std::uint32_t count) { op_u30(0x53, count); }
  void getslot(std::uint32_t slot) { op_u30(0x6C, slot); }
  void setslot(std::uint32_t slot) { op_u30(0x6D, slot); }
  void returnvalue() { op(0x48); }
  void returnvoid() { op(0x47); }
  void throw_op() { op(0x03); }
  void callstatic(std::uint32_t method, std::uint32_t count) { op_u30(0x44, method); u30(count); }
  void getdescendants(std::uint32_t multiname) { op_multiname(0x59, multiname); }

  std::vector<std::uint8_t> bytes() {
    for (const Patch& patch : patches_) {
      const auto found = labels_.find(patch.target);
      if (found == labels_.end()) {
        throw std::runtime_error("branch to missing label " + patch.target);
      }
      const auto delta = static_cast<std::int32_t>(found->second) -
                         static_cast<std::int32_t>(patch.at + 3);
      const auto raw = static_cast<std::uint32_t>(delta) & 0xFFFFFFu;
      code_[patch.at] = static_cast<std::uint8_t>(raw & 0xFFu);
      code_[patch.at + 1] = static_cast<std::uint8_t>((raw >> 8) & 0xFFu);
      code_[patch.at + 2] = static_cast<std::uint8_t>((raw >> 16) & 0xFFu);
    }
    return code_;
  }

 private:
  struct Patch {
    std::size_t at{0};
    std::string target;
  };

  std::vector<std::uint8_t> code_;
  std::map<std::string, std::size_t> labels_;
  std::vector<Patch> patches_;
};

class ScriptBuilder {
 public:
  std::uint32_t string_of(const std::string& value) {
    for (std::size_t index = 0; index < strings_.size(); ++index) {
      if (strings_[index] == value) {
        return static_cast<std::uint32_t>(index + 1);
      }
    }
    strings_.push_back(value);
    return static_cast<std::uint32_t>(strings_.size());
  }

  std::uint32_t namespace_of(const std::string& name) {
    string_of(name);
    for (std::size_t index = 0; index < package_names_.size(); ++index) {
      if (package_names_[index] == name) {
        return static_cast<std::uint32_t>(index + 1);
      }
    }
    package_names_.push_back(name);
    return static_cast<std::uint32_t>(package_names_.size());
  }

  std::uint32_t multiname(const std::string& name, const std::string& package = {}) {
    MultinameEntry entry;
    entry.name = string_of(name);
    entry.name_space = namespace_of(package);
    multinames_.push_back(entry);
    return static_cast<std::uint32_t>(multinames_.size());
  }

  std::uint32_t multiname_l(std::uint32_t ns_set = 1u) {
    MultinameEntry entry;
    entry.kind = fo4::abc::AbcMultinameKind::MultinameL;
    entry.ns_set = ns_set;
    multinames_.push_back(entry);
    return static_cast<std::uint32_t>(multinames_.size());
  }

  std::uint32_t rtqname_l() {
    MultinameEntry entry;
    entry.kind = fo4::abc::AbcMultinameKind::RTQNameL;
    multinames_.push_back(entry);
    return static_cast<std::uint32_t>(multinames_.size());
  }

  std::uint32_t namespace_set(const std::vector<std::uint32_t>& namespaces) {
    ns_sets_.push_back(namespaces);
    return static_cast<std::uint32_t>(ns_sets_.size());
  }

  std::uint32_t rtqname(const std::string& name) {
    MultinameEntry entry;
    entry.kind = fo4::abc::AbcMultinameKind::RTQName;
    entry.name = string_of(name);
    multinames_.push_back(entry);
    return static_cast<std::uint32_t>(multinames_.size());
  }

  std::uint32_t add_double(double value) {
    doubles_.push_back(value);
    return static_cast<std::uint32_t>(doubles_.size());
  }

  std::uint32_t add_int(std::int32_t value) {
    ints_.push_back(value);
    return static_cast<std::uint32_t>(ints_.size());
  }

  std::uint32_t method(const std::string& name, std::vector<std::uint8_t> code,
                       std::uint32_t local_count) {
    return method_with_exceptions(name, std::move(code), local_count, {});
  }

  std::uint32_t method_with_exceptions(const std::string& name, std::vector<std::uint8_t> code,
                                       std::uint32_t local_count,
                                       std::vector<fo4::abc::AbcException> exceptions) {
    fo4::abc::AbcMethodInfo info;
    info.param_count = 0;
    info.return_type = 0;
    info.name = string_of(name);
    info.flags = 0;
    methods_.push_back(std::move(info));
    fo4::abc::AbcMethodBody body;
    body.method = static_cast<std::uint32_t>(methods_.size() - 1);
    body.max_stack = 16;
    body.local_count = local_count;
    body.init_scope_depth = 0;
    body.max_scope_depth = 2;
    body.code = std::move(code);
    body.exceptions = std::move(exceptions);
    bodies_.push_back(std::move(body));
    return static_cast<std::uint32_t>(methods_.size() - 1);
  }

  /* A body-less method must report as an unbound native. */
  std::uint32_t method_native(const std::string& name) {
    fo4::abc::AbcMethodInfo info;
    info.param_count = 0;
    info.return_type = 0;
    info.name = string_of(name);
    info.flags = 0;
    methods_.push_back(std::move(info));
    return static_cast<std::uint32_t>(methods_.size() - 1);
  }

  fo4::abc::AbcFile finish(std::uint32_t script_init_method) {
    fo4::abc::AbcFile file;
    file.minor_version = fo4::abc::kAbcMinorVersion;
    file.major_version = fo4::abc::kAbcMajorVersion;
    file.pool.strings.push_back("");
    for (const std::string& value : strings_) {
      file.pool.strings.push_back(value);
    }
    file.pool.namespaces.push_back(fo4::abc::AbcNamespace{fo4::abc::AbcNamespaceKind::Namespace, 0});
    for (const std::string& package : package_names_) {
      fo4::abc::AbcNamespace name_space;
      name_space.kind = fo4::abc::AbcNamespaceKind::Package;
      /* Pool string 0 must be the empty string that names the default package. */
      name_space.name = package.empty() ? 0u : string_of(package);
      file.pool.namespaces.push_back(name_space);
    }
    file.pool.ns_sets.push_back(fo4::abc::AbcNsSet{});
    /* Pool ns_set 1 must stay the public set that multiname_l names. */
    for (const std::vector<std::uint32_t>& namespaces : ns_sets_) {
      file.pool.ns_sets.push_back(fo4::abc::AbcNsSet{namespaces});
    }
    file.pool.multinames.push_back(fo4::abc::AbcMultiname{});
    for (const MultinameEntry& entry : multinames_) {
      fo4::abc::AbcMultiname multiname;
      multiname.kind = entry.kind;
      multiname.name = entry.name;
      multiname.ns = entry.name_space;
      multiname.ns_set = entry.ns_set;
      file.pool.multinames.push_back(multiname);
    }
    file.pool.ints.push_back(0);
    for (const std::int32_t value : ints_) {
      file.pool.ints.push_back(value);
    }
    file.pool.uints.push_back(0);
    file.pool.doubles.push_back(0.0);
    for (const double value : doubles_) {
      file.pool.doubles.push_back(value);
    }
    file.methods = methods_;
    file.bodies = bodies_;
    fo4::abc::AbcScriptInfo script;
    script.init = script_init_method;
    file.scripts.push_back(script);
    return file;
  }

 private:
  struct MultinameEntry {
    fo4::abc::AbcMultinameKind kind{fo4::abc::AbcMultinameKind::QName};
    std::uint32_t name{0};
    std::uint32_t name_space{1};
    std::uint32_t ns_set{0};
  };

  std::vector<std::string> strings_;
  std::vector<std::string> package_names_{std::string{}};
  std::vector<std::vector<std::uint32_t>> ns_sets_{{1u}};
  std::vector<MultinameEntry> multinames_;
  std::vector<std::int32_t> ints_;
  std::vector<double> doubles_;
  std::vector<fo4::abc::AbcMethodInfo> methods_;
  std::vector<fo4::abc::AbcMethodBody> bodies_;
};

}  // namespace avm2_test
