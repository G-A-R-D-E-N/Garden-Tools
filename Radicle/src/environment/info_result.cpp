#include "fo4/environment/info_result.hpp"

#include "record_reader.hpp"
#include "fo4/error.hpp"
#include "fo4/text/encoding.hpp"

#include <span>
#include <string>

namespace fo4::env {
namespace {

using detail::Reader;

void fill_fragments(const Record& record, InfoResult& out) {
  const Subrecord* vmad = nullptr;
  for (const auto& sub : record.subrecords) {
    if (sub.signature == sig_vmad) { vmad = &sub; break; }
  }
  if (!vmad) return;
  VmadScripts scripts;
  try {
    scripts = read_vmad_scripts(vmad->data);
  } catch (const parse_error& error) {
    out.fragment_state = InfoFragmentState::unreadable;
    out.fragment_failure = error.what();
    return;
  }
  // Invariant: an unsupported property ends the script list early and leaves trailing_bytes 0, so it must be checked first.
  if (scripts.unsupported) {
    out.fragment_state = InfoFragmentState::unreadable;
    out.fragment_failure = scripts.stopped
        ? "VMAD property '" + text::windows1252_to_utf8(scripts.stopped->property) + "' (type " + std::to_string(scripts.stopped->property_type) +
              ") in the leading script list is unsupported"
        : "a VMAD property in the leading script list is unsupported";
    return;
  }
  if (scripts.trailing_bytes == 0 || scripts.trailing_bytes > vmad->data.size()) {
    out.fragment_state = InfoFragmentState::no_section;
    return;
  }
  auto extras = read_info_vmad_extras(std::span<const std::byte>(vmad->data).subspan(vmad->data.size() - scripts.trailing_bytes),
                                      scripts.object_format);
  if (extras.unsupported || !extras.fully_consumed) {
    out.fragment_state = InfoFragmentState::unreadable;
    out.fragment_failure = extras.unsupported ? extras.unsupported_reason : "the fragment section has trailing bytes";
    return;
  }
  out.fragment_state = InfoFragmentState::present;
  out.fragment_version = extras.version;
  out.fragment_flags = extras.flags;
  out.fragment_script = std::move(extras.script);
  std::size_t next = 0;
  for (const auto kind : {InfoFragmentKind::on_begin, InfoFragmentKind::on_end}) {
    if ((extras.flags & static_cast<std::uint8_t>(kind)) == 0) continue;
    auto& row = extras.fragments[next++];
    out.rows.push_back({kind, row.unknown, std::move(row.script), std::move(row.function)});
  }
}

}  // namespace

InfoResult decode_info_result(const Environment& env, const RecordRef& ref) {
  const Reader r(env, ref, sig("INFO"));
  InfoResult out;
  if (const auto* sub = r.sized(sig("TIQS"), 4)) out.stages = std::array{Reader::u16(*sub, 0), Reader::u16(*sub, 2)};
  if (const auto* sub = r.unique(sig("NAM0"))) {
    const auto text = zstring_view(*sub);
    if (!text) r.fail(sig("NAM0"), "invalid stored string");
    out.start_scene_phase = text;
  }
  fill_fragments(*ref.record, out);
  return out;
}

}  // namespace fo4::env
