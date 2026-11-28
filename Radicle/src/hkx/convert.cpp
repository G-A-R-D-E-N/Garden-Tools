#include "fo4/hkx/convert.hpp"

#include "fo4/hkx/layout.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::hkx {
namespace {

const HkxTypeRule kIdentityRule{};

struct Resolved {
  const HkxTypeRule* rule{nullptr};
  std::string target_type;
};

struct Pass {
  const std::set<std::uint64_t>& supported;
  HkxConversionReport& report;
  std::vector<HkxConversionDiagnostic>& diagnostics;
};

void note(Pass& pass, HkxDiagnosticLevel level, std::uint64_t object, std::string member, std::string message) {
  pass.diagnostics.push_back({level, object, std::move(member), std::move(message)});
}

std::optional<Resolved> resolve(const HkxConversionMap& map, const std::string& type) {
  if (const auto found = map.rules.find(type); found != map.rules.end()) {
    return Resolved{&found->second, found->second.target_type};
  }
  if (map.identity.contains(type)) return Resolved{&kIdentityRule, type};
  return std::nullopt;
}

std::optional<std::string> invalid_map_reason(const HkxConversionMap& map) {
  for (const auto& [source_type, rule] : map.rules) {
    if (source_type.empty() || rule.target_type.empty()) return "a type rule has an empty source or target type";
    std::set<std::string> targets;
    for (const auto& [from, to] : rule.renamed) {
      if (from.empty() || to.empty()) return source_type + " renames an empty member name";
      if (!targets.insert(to).second) return source_type + " renames two members onto " + to;
      if (rule.dropped.contains(from)) return source_type + " both renames and drops " + from;
    }
  }
  return std::nullopt;
}

HkxValue null_reference() {
  HkxValue value;
  value.kind = HkxValue::Kind::Reference;
  return value;
}

HkxValue clone_value(const HkxValue& value, std::uint64_t owner, const std::string& member, Pass& pass) {
  HkxValue copy = value;
  switch (value.kind) {
    case HkxValue::Kind::Reference:
      if (value.target && !pass.supported.contains(*value.target)) {
        ++pass.report.dropped_references;
        note(pass, HkxDiagnosticLevel::Error, owner, member,
             "reference to unsupported object " + std::to_string(*value.target) + " was replaced with null");
        return null_reference();
      }
      return copy;
    case HkxValue::Kind::Array:
      for (std::size_t at = 0; at < value.items.size(); ++at) copy.items[at] = clone_value(value.items[at], owner, member, pass);
      return copy;
    case HkxValue::Kind::Struct:
      for (std::size_t at = 0; at < value.items.size(); ++at) {
        const std::string name = at < value.names.size() ? value.names[at] : std::string{};
        copy.items[at] = clone_value(value.items[at], owner, member + "." + name, pass);
      }
      return copy;
    default:
      return copy;
  }
}

HkxValue convert_enum(const HkxValue& source, const std::map<std::int64_t, std::int64_t>& mapping, std::uint64_t owner,
                      const std::string& member, Pass& pass, bool& patched) {
  patched = true;
  if (source.kind != HkxValue::Kind::Integer) {
    ++pass.report.unsupported_enum_values;
    note(pass, HkxDiagnosticLevel::Error, owner, member, "enum mapping expected an integer value");
    return HkxValue{};
  }
  const auto found = mapping.find(source.integer);
  if (found == mapping.end()) {
    ++pass.report.unsupported_enum_values;
    note(pass, HkxDiagnosticLevel::Error, owner, member,
         "enum value " + std::to_string(source.integer) + " has no target mapping");
    return HkxValue{};
  }
  ++pass.report.enum_mapped_fields;
  patched = found->second != source.integer;
  HkxValue converted;
  converted.kind = HkxValue::Kind::Integer;
  converted.integer = found->second;
  return converted;
}

}  // namespace

HkxConvertStatus convert_document(const HkxSemanticDocument& source, const HkxConversionMap& map,
                                  HkxConversionResult& out) {
  out = HkxConversionResult{};
  if (map.evidence.empty()) {
    out.diagnostics.push_back({HkxDiagnosticLevel::Error, 0, "",
                               "the mapping names no evidence it was verified against, so it is not applied"});
    return HkxConvertStatus::UnverifiedMapping;
  }
  if (const auto reason = invalid_map_reason(map)) {
    out.diagnostics.push_back({HkxDiagnosticLevel::Error, 0, "", "invalid mapping: " + *reason});
    return HkxConvertStatus::InvalidMap;
  }

  HkxConversionReport& report = out.report;
  std::vector<HkxConversionDiagnostic>& diagnostics = out.diagnostics;
  std::map<std::uint64_t, Resolved> resolved;
  std::set<std::uint64_t> supported;
  for (const auto& [id, object] : source.objects) {
    auto rule = resolve(map, object.type_name);
    if (!rule) {
      ++report.unsupported_objects;
      diagnostics.push_back({HkxDiagnosticLevel::Error, id, "", "no conversion rule exists for " + object.type_name});
      continue;
    }
    supported.insert(id);
    resolved.emplace(id, std::move(*rule));
  }

  HkxSemanticDocument target;
  Pass pass{supported, report, diagnostics};
  for (const auto& [id, object] : source.objects) {
    const auto found = resolved.find(id);
    if (found == resolved.end()) continue;
    const HkxTypeRule& rule = *found->second.rule;
    HkxSemanticObject& converted = target.objects[id];
    converted.id = id;
    converted.type_name = found->second.target_type;
    bool patched = object.type_name != converted.type_name;
    const std::size_t references_before = report.dropped_references;

    for (const auto& [source_member, source_value] : object.members) {
      if (rule.dropped.contains(source_member)) {
        ++report.dropped_fields;
        patched = true;
        note(pass, HkxDiagnosticLevel::Warning, id, source_member, "field was explicitly dropped by the conversion rule");
        continue;
      }
      std::string target_member = source_member;
      if (const auto renamed = rule.renamed.find(source_member); renamed != rule.renamed.end()) {
        target_member = renamed->second;
        patched = true;
      }
      HkxValue value = clone_value(source_value, id, source_member, pass);
      if (const auto mapping = rule.enums.find(source_member); mapping != rule.enums.end()) {
        bool enum_patched = false;
        value = convert_enum(source_value, mapping->second, id, source_member, pass, enum_patched);
        patched = patched || enum_patched;
      }
      if (converted.members.contains(target_member)) {
        note(pass, HkxDiagnosticLevel::Error, id, target_member,
             source_member + " maps onto " + target_member + ", which this object already wrote; the later value was discarded");
        patched = true;
        continue;
      }
      converted.members.emplace(std::move(target_member), std::move(value));
    }

    for (const auto& [member, value] : rule.defaults) {
      if (converted.members.contains(member)) continue;
      converted.members.emplace(member, clone_value(value, id, member, pass));
      ++report.defaulted_fields;
      patched = true;
    }
    if (report.dropped_references != references_before) patched = true;
    if (patched) {
      ++report.patched_objects;
    } else {
      ++report.exact_objects;
    }
  }

  for (const auto& [id, converted] : target.objects) {
    if (find_layout(converted.type_name) == nullptr) {
      note(pass, HkxDiagnosticLevel::Error, id, "", "target type " + converted.type_name + " has no verified layout");
    }
  }

  if (source.root) {
    if (target.objects.contains(*source.root)) {
      target.root = source.root;
    } else {
      note(pass, HkxDiagnosticLevel::Error, *source.root, "",
           "the source root object is unsupported, so the converted document has no root");
    }
  }

  const bool refused = std::any_of(diagnostics.begin(), diagnostics.end(), [](const HkxConversionDiagnostic& diagnostic) {
    return diagnostic.level == HkxDiagnosticLevel::Error;
  });
  if (refused) return HkxConvertStatus::Refused;
  out.document = std::move(target);
  return HkxConvertStatus::Ok;
}

std::string_view convert_status_name(HkxConvertStatus status) {
  switch (status) {
    case HkxConvertStatus::Ok:
      return "ok";
    case HkxConvertStatus::UnverifiedMapping:
      return "unverified-mapping";
    case HkxConvertStatus::InvalidMap:
      return "invalid-map";
    case HkxConvertStatus::Refused:
      return "refused";
  }
  return "unknown";
}

std::string render_conversion_report(const HkxConversionMap& map, HkxConvertStatus status,
                                     const HkxConversionResult& result) {
  const HkxConversionReport& report = result.report;
  std::string text = "conversion " + map.source_version + " -> " + map.target_version + " status " +
                     std::string(convert_status_name(status)) + "\n";
  text += "objects exact " + std::to_string(report.exact_objects) + " patched " + std::to_string(report.patched_objects) +
          " unsupported " + std::to_string(report.unsupported_objects) + "\n";
  text += "fields defaulted " + std::to_string(report.defaulted_fields) + " dropped " +
          std::to_string(report.dropped_fields) + " enum_mapped " + std::to_string(report.enum_mapped_fields) +
          " enum_unsupported " + std::to_string(report.unsupported_enum_values) + "\n";
  text += "references dropped " + std::to_string(report.dropped_references) + "\n";
  for (const HkxConversionDiagnostic& diagnostic : result.diagnostics) {
    const char* level = diagnostic.level == HkxDiagnosticLevel::Error     ? "error"
                        : diagnostic.level == HkxDiagnosticLevel::Warning ? "warning"
                                                                          : "info";
    text += std::string(level) + " object " + std::to_string(diagnostic.object_id) + " member " +
            (diagnostic.member.empty() ? std::string{"-"} : diagnostic.member) + ": " + diagnostic.message + "\n";
  }
  return text;
}

}  // namespace fo4::hkx
