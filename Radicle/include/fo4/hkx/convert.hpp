#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::hkx {

// Invariant: names is parallel to items for a Struct and empty for every other kind.
struct HkxValue {
  enum class Kind : std::uint8_t { Null, Bool, Integer, Real, String, Reference, Array, Struct };
  Kind kind{Kind::Null};
  bool boolean{false};
  std::int64_t integer{0};
  double real{0.0};
  std::string text;
  // Invariant: an empty target is a null pointer.
  std::optional<std::uint64_t> target;
  std::vector<std::string> names;
  std::vector<HkxValue> items;
};

struct HkxSemanticObject {
  std::uint64_t id{0};
  std::string type_name;
  std::map<std::string, HkxValue> members;
};

struct HkxSemanticDocument {
  std::optional<std::uint64_t> root;
  std::map<std::uint64_t, HkxSemanticObject> objects;
};

struct HkxTypeRule {
  std::string target_type;
  std::map<std::string, std::string> renamed;
  std::set<std::string> dropped;
  std::map<std::string, HkxValue> defaults;
  std::map<std::string, std::map<std::int64_t, std::int64_t>> enums;
};

// Invariant: a map without evidence is never applied; evidence names the fixture or record the mappings were checked against.
struct HkxConversionMap {
  std::string source_version;
  std::string target_version;
  std::string evidence;
  std::map<std::string, HkxTypeRule> rules;
  std::set<std::string> identity;
};

enum class HkxDiagnosticLevel : std::uint8_t { Info, Warning, Error };

struct HkxConversionDiagnostic {
  HkxDiagnosticLevel level{HkxDiagnosticLevel::Info};
  std::uint64_t object_id{0};
  std::string member;
  std::string message;
};

struct HkxConversionReport {
  std::size_t exact_objects{0};
  std::size_t patched_objects{0};
  std::size_t defaulted_fields{0};
  std::size_t dropped_fields{0};
  std::size_t dropped_references{0};
  std::size_t enum_mapped_fields{0};
  std::size_t unsupported_enum_values{0};
  std::size_t unsupported_objects{0};
};

// Invariant: document is empty unless the status is Ok; diagnostics and report are filled for every status.
struct HkxConversionResult {
  HkxSemanticDocument document;
  HkxConversionReport report;
  std::vector<HkxConversionDiagnostic> diagnostics;
};

enum class HkxConvertStatus : std::uint8_t { Ok, UnverifiedMapping, InvalidMap, Refused };

// Constraint: any Error diagnostic refuses the whole conversion, so a partial document is never returned.
// Requires target types that have a verified layout in the generated table.
HkxConvertStatus convert_document(const HkxSemanticDocument& source, const HkxConversionMap& map,
                                  HkxConversionResult& out);

std::string_view convert_status_name(HkxConvertStatus status);

// Invariant: the text depends only on the map versions, the status, the report and the diagnostics, in that order.
std::string render_conversion_report(const HkxConversionMap& map, HkxConvertStatus status,
                                     const HkxConversionResult& result);

}  // namespace fo4::hkx
