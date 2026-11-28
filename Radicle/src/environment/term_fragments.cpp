#include "fo4/environment/term_fragments.hpp"

#include "record_reader.hpp"
#include "fo4/error.hpp"
#include "fo4/text/encoding.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <utility>

namespace fo4::env {
namespace {

void fill_fragments(const Record& record, TermFragments& out) {
  const Subrecord* vmad = nullptr;
  for (const auto& sub : record.subrecords) {
    if (sub.signature == sig_vmad) { vmad = &sub; break; }
  }
  if (!vmad) return;
  VmadScripts scripts;
  try {
    scripts = read_vmad_scripts(vmad->data);
  } catch (const parse_error& error) {
    out.state = TermFragmentState::unreadable;
    out.failure = error.what();
    return;
  }
  // Invariant: an unsupported property ends the script list early and leaves trailing_bytes 0, so it must be checked first.
  if (scripts.unsupported) {
    out.state = TermFragmentState::unreadable;
    out.failure = scripts.stopped
        ? "VMAD property '" + text::windows1252_to_utf8(scripts.stopped->property) + "' (type " + std::to_string(scripts.stopped->property_type) +
              ") in the leading script list is unsupported"
        : "a VMAD property in the leading script list is unsupported";
    return;
  }
  if (scripts.trailing_bytes == 0 || scripts.trailing_bytes > vmad->data.size()) {
    out.state = TermFragmentState::no_section;
    return;
  }
  auto extras = read_term_vmad_extras(std::span<const std::byte>(vmad->data).subspan(vmad->data.size() - scripts.trailing_bytes),
                                      scripts.object_format);
  if (extras.unsupported || !extras.fully_consumed) {
    out.state = TermFragmentState::unreadable;
    out.failure = extras.unsupported ? extras.unsupported_reason : "the fragment section has trailing bytes";
    return;
  }
  out.state = TermFragmentState::present;
  out.version = extras.version;
  out.script = std::move(extras.script);
  for (auto& row : extras.fragments) out.rows.push_back({row.index, row.unknown, std::move(row.script), std::move(row.function)});
}

}  // namespace

TermFragments decode_term_fragments(const Environment& env, const RecordRef& ref) {
  [[maybe_unused]] const detail::Reader record_check(env, ref, sig_term);
  TermFragments out;
  fill_fragments(*ref.record, out);
  return out;
}

}  // namespace fo4::env
