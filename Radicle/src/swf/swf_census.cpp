#include <fo4/swf/swf_census.hpp>

#include <fo4/abc/abc.hpp>
#include <fo4/abc/opcodes.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::swf {

namespace {

using fo4::abc::AbcFile;
using fo4::abc::AbcMultinameKind;
using fo4::abc::AbcNamespaceKind;
using fo4::abc::AbcOperandRole;
using fo4::abc::AbcTrait;

constexpr std::uint8_t k_package_namespace = static_cast<std::uint8_t>(AbcNamespaceKind::Package);
constexpr std::uint8_t k_utf8_value = static_cast<std::uint8_t>(fo4::abc::AbcValueKind::Utf8);

enum class Position : std::uint8_t { type, member, scope };

struct Description {
  std::string package;
  std::string name;
  std::string rendered;
  bool private_ns{false};
  bool valid{false};
};

bool starts_with(std::string_view value, std::string_view prefix) {
  return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

std::string to_lower(std::string value) {
  for (char& ch : value) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return value;
}

bool is_load(std::string_view value) {
  if (value.size() < 4) {
    return false;
  }
  const std::string suffix = to_lower(std::string{value.substr(value.size() - 4)});
  return suffix == ".swf" || suffix == ".xml";
}

std::pair<std::string, bool> package_from_namespace(const AbcFile& file, std::uint32_t index) {
  if (static_cast<std::size_t>(index) >= file.pool.namespaces.size()) {
    return {std::string{}, true};
  }
  const fo4::abc::AbcNamespace& entry = file.pool.namespaces[index];
  const auto kind = static_cast<std::uint8_t>(entry.kind);
  if (kind == k_package_namespace) {
    return {file.namespace_at(index), false};
  }
  return {std::string{}, kind != 0};
}

Description describe(const AbcFile& file, std::uint32_t index) {
  if (static_cast<std::size_t>(index) >= file.pool.multinames.size()) {
    return {};
  }
  const fo4::abc::AbcMultiname& multiname = file.pool.multinames[index];
  const std::string name = file.string_at(multiname.name);
  switch (multiname.kind) {
    case AbcMultinameKind::QName:
    case AbcMultinameKind::QNameA: {
      const auto [package, private_ns] = package_from_namespace(file, multiname.ns);
      Description out;
      out.package = package;
      out.name = name;
      out.rendered = package.empty() ? name : package + "::" + name;
      out.private_ns = private_ns;
      out.valid = true;
      return out;
    }
    case AbcMultinameKind::RTQName:
    case AbcMultinameKind::RTQNameA: {
      Description out;
      out.name = name;
      out.rendered = name;
      out.valid = true;
      return out;
    }
    case AbcMultinameKind::RTQNameL:
    case AbcMultinameKind::RTQNameLA:
      return {};
    case AbcMultinameKind::Multiname:
    case AbcMultinameKind::MultinameA:
    case AbcMultinameKind::MultinameL:
    case AbcMultinameKind::MultinameLA: {
      std::vector<std::string> packages;
      bool private_ns = false;
      if (static_cast<std::size_t>(multiname.ns_set) < file.pool.ns_sets.size()) {
        for (const std::uint32_t ns_index : file.pool.ns_sets[multiname.ns_set].namespaces) {
          const auto [package, is_private] = package_from_namespace(file, ns_index);
          if (!package.empty()) {
            packages.push_back(package);
          }
          private_ns = private_ns || is_private;
        }
      }
      const bool dynamic = multiname.kind == AbcMultinameKind::MultinameL ||
                           multiname.kind == AbcMultinameKind::MultinameLA;
      Description out;
      out.package = packages.empty() ? std::string{} : packages.front();
      out.name = name;
      if (dynamic) {
        out.rendered = packages.empty() ? std::string{"*"} : packages.front() + "::*";
      } else {
        out.rendered = packages.empty() ? name : packages.front() + "::" + name;
      }
      out.private_ns = private_ns;
      out.valid = true;
      return out;
    }
    case AbcMultinameKind::TypeName: {
      const Description base = describe(file, multiname.name);
      if (!base.valid) {
        return {};
      }
      Description out = base;
      if (!multiname.params.empty()) {
        out.rendered += "<";
        for (std::size_t param = 0; param < multiname.params.size(); ++param) {
          if (param != 0) {
            out.rendered += ",";
          }
          const Description parameter = describe(file, multiname.params[param]);
          out.rendered += parameter.valid ? parameter.rendered : std::string{"?"};
        }
        out.rendered += ">";
      }
      return out;
    }
  }
  return {};
}

bool is_member_mnemonic(std::string_view mnemonic) {
  static constexpr std::array<std::string_view, 13> k_members{
      "getsuper",     "setsuper",    "callsuper",     "callproperty", "constructprop",
      "callproplex",  "callsupervoid", "callpropvoid", "getdescendants", "setproperty",
      "getproperty",  "initproperty", "deleteproperty"};
  return std::find(k_members.begin(), k_members.end(), mnemonic) != k_members.end();
}

bool is_scope_mnemonic(std::string_view mnemonic) {
  static constexpr std::array<std::string_view, 3> k_scopes{"findpropstrict", "findproperty",
                                                            "finddef"};
  return std::find(k_scopes.begin(), k_scopes.end(), mnemonic) != k_scopes.end();
}

Position position_of(std::string_view mnemonic) {
  if (is_member_mnemonic(mnemonic)) {
    return Position::member;
  }
  if (is_scope_mnemonic(mnemonic)) {
    return Position::scope;
  }
  return Position::type;
}

std::string base_of(const std::string& rendered) {
  const std::size_t angle = rendered.find('<');
  return angle == std::string::npos ? rendered : rendered.substr(0, angle);
}

std::optional<CensusKind> classify(const Description& description,
                                   const std::set<std::string>& declared_classes,
                                   const std::set<std::string>& declared_names, Position position) {
  if (!description.valid || description.private_ns || description.rendered.empty()) {
    return std::nullopt;
  }
  if (description.rendered == "*") {
    return std::nullopt;
  }
  if (description.rendered.size() >= 3 &&
      description.rendered.compare(description.rendered.size() - 3, 3, "::*") == 0) {
    return std::nullopt;
  }
  const std::string base = base_of(description.rendered);
  if (declared_classes.contains(base)) {
    return std::nullopt;
  }
  const std::string& package = description.package;
  if (package == "flash" || starts_with(package, "flash.")) {
    return position == Position::member ? CensusKind::flash_member : CensusKind::flash_class;
  }
  if (package == "scaleform" || starts_with(package, "scaleform.")) {
    return position == Position::member ? CensusKind::scaleform_member
                                       : CensusKind::scaleform_class;
  }
  if (package == "__AS3__.vec") {
    return CensusKind::builtin;
  }
  if (!package.empty()) {
    return std::nullopt;
  }
  const std::size_t separator = base.rfind("::");
  const std::string bare = separator == std::string::npos ? base : base.substr(separator + 2);
  if (declared_names.contains(description.name) || declared_names.contains(bare)) {
    return std::nullopt;
  }
  if (position == Position::type) {
    return CensusKind::builtin;
  }
  return position == Position::member ? CensusKind::member : CensusKind::scope;
}

struct Counts {
  void add_reference(CensusKind kind, const std::string& name) {
    ++references[std::make_pair(kind, name)];
  }

  void add_string(const std::string& value) {
    if (is_load(value)) {
      ++loads[value];
    }
  }

  std::map<std::pair<CensusKind, std::string>, std::uint32_t> references;
  std::map<std::string, std::uint32_t> loads;
};

void collect_traits(const AbcFile& file, const std::vector<AbcTrait>& traits,
                    const std::set<std::string>& declared_classes,
                    const std::set<std::string>& declared_names, Counts& counts) {
  for (const AbcTrait& trait : traits) {
    if (trait.kind != fo4::abc::AbcTraitKind::Slot && trait.kind != fo4::abc::AbcTraitKind::Const) {
      continue;
    }
    const Description description = describe(file, trait.type_name);
    if (const auto kind = classify(description, declared_classes, declared_names, Position::type)) {
      counts.add_reference(*kind, description.rendered);
    }
    if (trait.value_index != 0 && static_cast<std::uint8_t>(trait.value_kind) == k_utf8_value) {
      counts.add_string(file.string_at(trait.value_index));
    }
  }
}

void collect_method_types(const AbcFile& file, const fo4::abc::AbcMethodInfo& method,
                          const std::set<std::string>& declared_classes,
                          const std::set<std::string>& declared_names, Counts& counts) {
  std::vector<std::uint32_t> types;
  types.reserve(method.param_types.size() + 1);
  types.push_back(method.return_type);
  types.insert(types.end(), method.param_types.begin(), method.param_types.end());
  for (const std::uint32_t index : types) {
    const Description description = describe(file, index);
    if (const auto kind = classify(description, declared_classes, declared_names, Position::type)) {
      counts.add_reference(*kind, description.rendered);
    }
  }
}

void collect_body(const AbcFile& file, const fo4::abc::AbcMethodBody& body,
                  const std::set<std::string>& declared_classes,
                  const std::set<std::string>& declared_names, Counts& counts) {
  std::vector<fo4::abc::AbcInstruction> instructions;
  if (fo4::abc::decode_code(body.code, instructions, body.exceptions) !=
      fo4::abc::AbcStatus::Ok) {
    return;
  }
  for (const fo4::abc::AbcInstruction& instruction : instructions) {
    const Position position = position_of(instruction.mnemonic);
    for (const fo4::abc::AbcDecodedOperand& operand : instruction.operands) {
      if (operand.role == AbcOperandRole::Multiname) {
        const Description description = describe(file, operand.value);
        if (const auto kind = classify(description, declared_classes, declared_names, position)) {
          counts.add_reference(*kind, description.rendered);
        }
      } else if (operand.role == AbcOperandRole::String) {
        counts.add_string(file.string_at(operand.value));
      }
    }
  }
}

std::set<std::string> declared_class_names(const std::vector<AbcFile>& files) {
  std::set<std::string> classes;
  for (const AbcFile& file : files) {
    for (const fo4::abc::AbcInstanceInfo& instance : file.instances) {
      const Description description = describe(file, instance.name);
      if (!description.valid || description.rendered.empty()) {
        continue;
      }
      classes.insert(description.rendered);
    }
  }
  return classes;
}

void add_trait_names(const AbcFile& file, const std::vector<AbcTrait>& traits,
                     std::set<std::string>& names) {
  for (const AbcTrait& trait : traits) {
    const Description description = describe(file, trait.name);
    if (!description.valid || description.rendered.empty()) {
      continue;
    }
    names.insert(description.rendered);
    names.insert(description.name);
  }
}

std::set<std::string> declared_trait_names(const std::vector<AbcFile>& files) {
  std::set<std::string> names;
  for (const AbcFile& file : files) {
    for (const fo4::abc::AbcInstanceInfo& instance : file.instances) {
      add_trait_names(file, instance.traits, names);
    }
    for (const fo4::abc::AbcClassInfo& klass : file.classes) {
      add_trait_names(file, klass.traits, names);
    }
    for (const fo4::abc::AbcScriptInfo& script : file.scripts) {
      add_trait_names(file, script.traits, names);
    }
    for (const fo4::abc::AbcMethodBody& body : file.bodies) {
      add_trait_names(file, body.traits, names);
    }
  }
  return names;
}

bool reference_less(const CensusReference& left, const CensusReference& right) {
  const std::string_view left_kind{census_kind_name(left.kind)};
  const std::string_view right_kind{census_kind_name(right.kind)};
  if (left_kind != right_kind) {
    return left_kind < right_kind;
  }
  return left.name < right.name;
}

}  // namespace

const char* census_kind_name(CensusKind kind) noexcept {
  switch (kind) {
    case CensusKind::flash_class:
      return "flash-class";
    case CensusKind::flash_member:
      return "flash-member";
    case CensusKind::scaleform_class:
      return "scaleform-class";
    case CensusKind::scaleform_member:
      return "scaleform-member";
    case CensusKind::builtin:
      return "builtin";
    case CensusKind::member:
      return "member";
    case CensusKind::scope:
      return "scope";
  }
  return "unknown";
}

MovieCensus census_movie(std::string_view movie, const SwfFile& file) {
  MovieCensus census;
  census.movie = std::string{movie};

  const std::vector<SwfAbcBlock> blocks = abc_blocks(file);
  census.abc_blocks = blocks.size();
  std::vector<AbcFile> parsed;
  parsed.reserve(blocks.size());
  for (const SwfAbcBlock& block : blocks) {
    AbcFile abc;
    if (AbcFile::parse(reinterpret_cast<const std::uint8_t*>(block.payload.data()),
                       block.payload.size(), abc) != fo4::abc::AbcStatus::Ok) {
      continue;
    }
    parsed.push_back(std::move(abc));
  }
  census.parsed_blocks = parsed.size();

  const std::set<std::string> declared_classes = declared_class_names(parsed);
  const std::set<std::string> declared_names = declared_trait_names(parsed);

  Counts counts;
  for (const AbcFile& abc : parsed) {
    for (const fo4::abc::AbcInstanceInfo& instance : abc.instances) {
      std::vector<std::uint32_t> types;
      types.reserve(instance.interfaces.size() + 1);
      types.push_back(instance.super_index);
      types.insert(types.end(), instance.interfaces.begin(), instance.interfaces.end());
      for (const std::uint32_t index : types) {
        const Description description = describe(abc, index);
        if (const auto kind =
                classify(description, declared_classes, declared_names, Position::type)) {
          counts.add_reference(*kind, description.rendered);
        }
      }
    }
    for (const fo4::abc::AbcMethodInfo& method : abc.methods) {
      collect_method_types(abc, method, declared_classes, declared_names, counts);
    }
    for (const fo4::abc::AbcInstanceInfo& instance : abc.instances) {
      collect_traits(abc, instance.traits, declared_classes, declared_names, counts);
    }
    for (const fo4::abc::AbcClassInfo& klass : abc.classes) {
      collect_traits(abc, klass.traits, declared_classes, declared_names, counts);
    }
    for (const fo4::abc::AbcScriptInfo& script : abc.scripts) {
      collect_traits(abc, script.traits, declared_classes, declared_names, counts);
    }
    for (const fo4::abc::AbcMethodBody& body : abc.bodies) {
      collect_traits(abc, body.traits, declared_classes, declared_names, counts);
      collect_body(abc, body, declared_classes, declared_names, counts);
    }
  }

  census.references.reserve(counts.references.size());
  for (const auto& [key, count] : counts.references) {
    census.references.push_back(CensusReference{key.first, key.second, count});
  }
  std::sort(census.references.begin(), census.references.end(), reference_less);
  census.loads.reserve(counts.loads.size());
  for (const auto& [url, count] : counts.loads) {
    census.loads.push_back(CensusLoad{url, count});
  }
  return census;
}

std::string census_text(std::span<const MovieCensus> movies, std::string_view game_build) {
  std::size_t total_blocks = 0;
  std::size_t total_parsed = 0;
  for (const MovieCensus& movie : movies) {
    total_blocks += movie.abc_blocks;
    total_parsed += movie.parsed_blocks;
  }
  std::string text;
  const auto line = [&text](std::string_view value) {
    text.append(value);
    text.push_back('\n');
  };
  line("# Radicle SWF ABC census: the engine-provided surface every Programs movie references.");
  line("# Generated from Fallout4 - Interface.ba2; names and counts only, no game data.");
  line("# game_build " + std::string{game_build});
  line("# movies " + std::to_string(movies.size()));
  line("# abc_blocks " + std::to_string(total_blocks));
  line("# abc_parsed " + std::to_string(total_parsed));
  for (const MovieCensus& movie : movies) {
    line("movie " + movie.movie);
    line("abc " + std::to_string(movie.abc_blocks));
    line("parsed " + std::to_string(movie.parsed_blocks));
    for (const CensusReference& reference : movie.references) {
      line(std::string{census_kind_name(reference.kind)} + " " + reference.name + " " +
           std::to_string(reference.count));
    }
    for (const CensusLoad& load : movie.loads) {
      line("load " + load.url + " " + std::to_string(load.count));
    }
  }
  return text;
}

}  // namespace fo4::swf
