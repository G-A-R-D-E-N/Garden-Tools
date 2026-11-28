#include "fo4/hkx/convert_packfile.hpp"

#include "fo4/hkx/packfile.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <set>
#include <utility>

namespace fo4::hkx {
namespace {

constexpr std::size_t kProbeWindow = 96;

bool version_byte(std::byte value) {
  const auto c = static_cast<unsigned char>(value);
  return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == '.' || c == '-';
}

std::vector<HkxConversionMap> build_maps() {
  HkxConversionMap same;
  same.source_version = "hk_2014.1.0-r1";
  same.target_version = "hk_2014.1.0-r1";
  same.evidence =
      "same-version identity: decode_document and emit_document return the committed vanilla fixtures byte for byte "
      "(HkxDecodeReal); no field differs, so nothing is converted";
  for (const HkxDecodeClass& entry : decode_classes()) same.identity.insert(std::string(entry.name));
  std::vector<HkxConversionMap> maps;
  maps.push_back(std::move(same));
  return maps;
}

std::string show(const HkxValue& value) {
  switch (value.kind) {
    case HkxValue::Kind::Null:
      return "null";
    case HkxValue::Kind::Bool:
      return value.boolean ? "true" : "false";
    case HkxValue::Kind::Integer:
      return std::to_string(value.integer);
    case HkxValue::Kind::Real: {
      char text[40];
      std::snprintf(text, sizeof text, "%.9g", value.real);
      return text;
    }
    case HkxValue::Kind::String:
      return "\"" + value.text + "\"";
    case HkxValue::Kind::Reference:
      return value.target ? "object " + std::to_string(*value.target) : std::string("null");
    case HkxValue::Kind::Array:
      return "array of " + std::to_string(value.items.size());
    case HkxValue::Kind::Struct:
      return "struct of " + std::to_string(value.items.size());
  }
  return {};
}

bool same_scalar(const HkxValue& left, const HkxValue& right) {
  switch (left.kind) {
    case HkxValue::Kind::Bool:
      return left.boolean == right.boolean;
    case HkxValue::Kind::Integer:
      return left.integer == right.integer;
    case HkxValue::Kind::Real:
      return std::bit_cast<std::uint64_t>(left.real) == std::bit_cast<std::uint64_t>(right.real);
    case HkxValue::Kind::String:
      return left.text == right.text;
    case HkxValue::Kind::Reference:
      return left.target == right.target;
    default:
      return true;
  }
}

struct Sink {
  HkxPackfileConversion& out;
  std::uint64_t object;
  const std::string& class_name;

  void add(const std::string& member, std::string before, std::string after) const {
    ++out.changed_total;
    if (out.changes.size() < HkxPackfileConversion::kMaxChanges) {
      out.changes.push_back({object, class_name, member, std::move(before), std::move(after)});
    }
  }
};

void diff_value(const Sink& sink, const std::string& member, const HkxValue* before, const HkxValue* after) {
  if (before == nullptr || after == nullptr) {
    sink.add(member, before ? show(*before) : std::string("absent"), after ? show(*after) : std::string("absent"));
    return;
  }
  const bool composite = before->kind == HkxValue::Kind::Array || before->kind == HkxValue::Kind::Struct;
  if (before->kind != after->kind || (composite && (before->items.size() != after->items.size() || before->names != after->names))) {
    sink.add(member, show(*before), show(*after));
    return;
  }
  if (!composite) {
    if (!same_scalar(*before, *after)) sink.add(member, show(*before), show(*after));
    return;
  }
  for (std::size_t at = 0; at < before->items.size(); ++at) {
    const std::string child = before->kind == HkxValue::Kind::Struct ? member + "." + before->names[at]
                                                                      : member + "[" + std::to_string(at) + "]";
    diff_value(sink, child, &before->items[at], &after->items[at]);
  }
}

void diff_documents(const HkxSemanticDocument& source, const HkxSemanticDocument& target, HkxPackfileConversion& out) {
  for (const auto& [id, before] : source.objects) {
    const Sink sink{out, id, before.type_name};
    const auto found = target.objects.find(id);
    if (found == target.objects.end()) {
      sink.add("", "object", "absent");
      continue;
    }
    const HkxSemanticObject& after = found->second;
    if (before.type_name != after.type_name) sink.add("", before.type_name, after.type_name);
    for (const auto& [name, value] : before.members) {
      const auto match = after.members.find(name);
      diff_value(sink, name, &value, match == after.members.end() ? nullptr : &match->second);
    }
    for (const auto& [name, value] : after.members) {
      if (!before.members.contains(name)) diff_value(sink, name, nullptr, &value);
    }
  }
}

void collect_targets(const HkxValue& value, const HkxSemanticDocument& document, std::set<std::uint64_t>& out) {
  if (value.kind == HkxValue::Kind::Reference && value.target && !document.objects.contains(*value.target)) {
    out.insert(*value.target);
  }
  for (const HkxValue& item : value.items) collect_targets(item, document, out);
}

std::string opaque_targets(const HkxSemanticDocument& document, const HkxPackfile& packfile) {
  std::set<std::uint64_t> targets;
  for (const auto& object : document.objects) {
    for (const auto& member : object.second.members) collect_targets(member.second, document, targets);
  }
  std::set<std::string> names;
  for (const HkxObjectView& view : packfile.objects) {
    if (targets.contains(view.offset)) names.insert(std::string(view.class_name));
  }
  std::string text;
  for (const std::string& name : names) text += (text.empty() ? "" : ", ") + name;
  return text;
}

std::string block_text(const HkxBlock& block) {
  std::string text = block.class_name;
  if (!block.member.empty()) text += (text.empty() ? "" : " ") + block.member;
  if (!text.empty()) text += ": ";
  return text + block.reason;
}

}  // namespace

std::optional<HkxHeaderVersion> probe_havok_version(std::span<const std::byte> bytes) {
  const std::size_t limit = std::min(bytes.size(), kProbeWindow);
  for (std::size_t at = 0; at + 3 < limit; ++at) {
    if (static_cast<unsigned char>(bytes[at]) != 'h' || static_cast<unsigned char>(bytes[at + 1]) != 'k' ||
        static_cast<unsigned char>(bytes[at + 2]) != '_') {
      continue;
    }
    std::size_t end = at + 3;
    bool digit = false;
    while (end < limit && version_byte(bytes[end])) {
      const auto c = static_cast<unsigned char>(bytes[end]);
      if (c >= '0' && c <= '9') digit = true;
      ++end;
    }
    if (!digit || end <= at + 3) continue;
    if (end == limit && limit < bytes.size() && version_byte(bytes[limit])) return std::nullopt;
    HkxHeaderVersion found;
    found.offset = at;
    for (std::size_t i = at; i < end; ++i) found.version.push_back(static_cast<char>(bytes[i]));
    return found;
  }
  return std::nullopt;
}

std::string_view packfile_convert_status_name(HkxPackfileConvertStatus status) {
  switch (status) {
    case HkxPackfileConvertStatus::Ok:
      return "ok";
    case HkxPackfileConvertStatus::Unreadable:
      return "unreadable";
    case HkxPackfileConvertStatus::UnknownVersion:
      return "unknown-version";
    case HkxPackfileConvertStatus::UnsupportedPair:
      return "unsupported-pair";
    case HkxPackfileConvertStatus::DecodeRefused:
      return "decode-refused";
    case HkxPackfileConvertStatus::ConversionRefused:
      return "conversion-refused";
    case HkxPackfileConvertStatus::EmitRefused:
      return "emit-refused";
  }
  return "unknown";
}

const std::vector<HkxConversionMap>& verified_conversion_maps() {
  static const std::vector<HkxConversionMap> maps = build_maps();
  return maps;
}

HkxPackfileConversion convert_packfile(std::span<const std::byte> bytes, std::string_view target_version,
                                       std::span<const HkxConversionMap> maps) {
  HkxPackfileConversion out;
  out.target_version = std::string(target_version);

  HkxPackfile packfile;
  if (read_packfile(bytes, packfile) != HkxStatus::Ok) {
    out.status = HkxPackfileConvertStatus::Unreadable;
    out.reason = describe_packfile_refusal(bytes);
    if (out.reason.empty()) out.reason = "the packfile structure is not valid";
    return out;
  }
  const auto version = probe_havok_version(bytes);
  if (!version) {
    out.status = HkxPackfileConvertStatus::UnknownVersion;
    out.reason = "the header names no hk_ version within its first 96 bytes";
    return out;
  }
  out.source_version = version->version;

  const auto chosen = std::find_if(maps.begin(), maps.end(), [&](const HkxConversionMap& map) {
    return map.source_version == out.source_version && map.target_version == target_version;
  });
  if (chosen == maps.end()) {
    out.status = HkxPackfileConvertStatus::UnsupportedPair;
    out.reason = "no verified conversion from " + out.source_version + " to " + out.target_version + "; supported:";
    for (const HkxConversionMap& map : maps) out.reason += " " + map.source_version + " -> " + map.target_version + ";";
    return out;
  }
  out.evidence = chosen->evidence;
  out.same_version = chosen->source_version == chosen->target_version;

  HkxDecodeOptions options;
  options.opaque_unlisted = true;
  HkxDecodeResult decoded = decode_document(bytes, options);
  if (decoded.status != HkxDecodeStatus::Ok) {
    out.status = decoded.status == HkxDecodeStatus::Unreadable ? HkxPackfileConvertStatus::Unreadable
                                                               : HkxPackfileConvertStatus::DecodeRefused;
    out.block = decoded.block;
    out.reason = block_text(decoded.block);
    return out;
  }
  out.opaque_classes = std::move(decoded.opaque_classes);
  out.object_count = decoded.document.objects.size();

  HkxConversionResult converted;
  const HkxConvertStatus converted_status = convert_document(decoded.document, *chosen, converted);
  out.report = converted.report;
  out.diagnostics = converted.diagnostics;
  if (converted_status != HkxConvertStatus::Ok) {
    out.status = HkxPackfileConvertStatus::ConversionRefused;
    out.reason = std::string(convert_status_name(converted_status));
    const auto error = std::find_if(out.diagnostics.begin(), out.diagnostics.end(), [](const HkxConversionDiagnostic& item) {
      return item.level == HkxDiagnosticLevel::Error;
    });
    if (error != out.diagnostics.end()) out.reason += ": " + error->message;
    if (out.report.dropped_references > 0) {
      const std::string names = opaque_targets(decoded.document, packfile);
      if (!names.empty()) out.reason += "; the unreachable objects are of classes this build leaves opaque: " + names;
    }
    return out;
  }
  diff_documents(decoded.document, converted.document, out);

  if (!out.same_version) {
    out.status = HkxPackfileConvertStatus::EmitRefused;
    out.reason = "the writer rewrites scalar values only and cannot restamp or re-lay a packfile for " + out.target_version;
    return out;
  }
  HkxEmitResult emitted = emit_document(bytes, converted.document, options);
  if (emitted.status != HkxDecodeStatus::Ok) {
    out.status = HkxPackfileConvertStatus::EmitRefused;
    out.block = emitted.block;
    out.reason = block_text(emitted.block);
    return out;
  }
  out.bytes = std::move(emitted.bytes);
  return out;
}

HkxPackfileConversion convert_packfile(std::span<const std::byte> bytes, std::string_view target_version) {
  return convert_packfile(bytes, target_version, verified_conversion_maps());
}

}  // namespace fo4::hkx
