#include "fo4/anim/anim_text_data.hpp"

#include "fo4/error.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fo4::anim {
namespace {

inline constexpr std::size_t k_max_list_entries = 100000;
inline constexpr std::size_t k_max_name_length = 20;

struct Split {
  std::vector<std::string> lines;
  bool crlf{false};
};

bool is_control(unsigned char value) { return value < 0x20u || value == 0x7Fu; }

Split split_lines(std::string_view text, const std::string& what, std::size_t max_lines) {
  if (text.empty()) {
    throw fo4::parse_error(0, what + " is empty");
  }
  const bool crlf = text.find("\r\n") != std::string_view::npos;
  Split out;
  out.crlf = crlf;
  std::size_t start = 0;
  while (start < text.size()) {
    std::size_t stop = text.find('\n', start);
    if (stop == std::string_view::npos) {
      throw fo4::parse_error(start, what + " does not end with a line ending");
    }
    if (crlf) {
      if (stop == start || text[stop - 1] != '\r') {
        throw fo4::parse_error(stop, what + " mixes its line endings");
      }
      out.lines.emplace_back(text.substr(start, stop - 1 - start));
    } else {
      out.lines.emplace_back(text.substr(start, stop - start));
    }
    if (out.lines.size() > max_lines) {
      throw fo4::parse_error(start, what + " holds more lines than a list can hold");
    }
    start = stop + 1;
  }
  if (out.lines.empty()) {
    throw fo4::parse_error(0, what + " holds no lines");
  }
  return out;
}

void require_entry(const std::string& line, std::size_t index, const std::string& what) {
  for (const char value : line) {
    if (is_control(static_cast<unsigned char>(value))) {
      throw fo4::parse_error(index, what + " holds a control character");
    }
  }
}

void require_plain(const std::string& line, std::size_t index, const std::string& what) {
  if (line.empty()) {
    throw fo4::parse_error(index, what + " holds an empty line");
  }
  require_entry(line, index, what);
}

std::size_t parse_count(const std::string& line, std::size_t index, const std::string& what) {
  require_plain(line, index, what);
  std::size_t value = 0;
  for (const char digit : line) {
    if (digit < '0' || digit > '9') {
      throw fo4::parse_error(index, what + " count is not a decimal number");
    }
    value = value * 10 + static_cast<std::size_t>(digit - '0');
    if (value > k_max_list_entries) {
      throw fo4::parse_error(index, what + " count is larger than a list can hold");
    }
  }
  return value;
}

void require_numeric_id(const std::string& name, std::size_t index) {
  require_plain(name, index, "an animation file list");
  if (name.size() > k_max_name_length) {
    throw fo4::parse_error(index, "an animation file list id is longer than 20 digits");
  }
  std::uint64_t value = 0;
  for (const char digit : name) {
    if (digit < '0' || digit > '9') {
      throw fo4::parse_error(index, "a flavour 1 animation file list names a character with digits only");
    }
    const auto previous = value;
    value = value * 10 + static_cast<std::uint64_t>(digit - '0');
    if (value / 10 != previous) {
      throw fo4::parse_error(index, "an animation file list id does not fit 64 bits");
    }
  }
}

void require_unique(const std::vector<std::string>& lines, std::size_t index, const std::string& what) {
  std::unordered_set<std::string_view> seen;
  seen.reserve(lines.size());
  for (const auto& line : lines) {
    if (!seen.insert(line).second) {
      throw fo4::parse_error(index, what + " names \"" + line + "\" twice");
    }
  }
}

void append_line(std::string& out, std::string_view line, bool crlf) {
  out.append(line);
  out.append(crlf ? "\r\n" : "\n");
}

std::uint32_t parse_flavour(const std::string& line, std::size_t index) {
  if (line == "1") return 1;
  if (line == "0") return 0;
  throw fo4::parse_error(index, "an animation file list has an unknown flavour line");
}

template <typename Callable>
void require_writable(Callable check) {
  try {
    check();
  } catch (const fo4::parse_error& error) {
    throw std::invalid_argument(error.parse_message());
  }
}

}  // namespace

AnimFileList parse_anim_file_list(std::string_view text) {
  const Split split = split_lines(text, "an animation file list", k_max_list_entries + 5);
  const auto& lines = split.lines;
  if (lines.size() < 4) {
    throw fo4::parse_error(0, "an animation file list has fewer than four header lines");
  }
  if (lines[0] != std::to_string(k_anim_file_list_version)) {
    throw fo4::parse_error(0, "an animation file list has an unknown version line");
  }
  AnimFileList list;
  list.flavour = parse_flavour(lines[1], 1);
  list.crlf = split.crlf;
  if (list.flavour == 1) {
    require_numeric_id(lines[2], 2);
  } else {
    require_plain(lines[2], 2, "an animation file list");
  }
  list.name = lines[2];
  const std::size_t count = parse_count(lines[3], 3, "an animation file list");
  const std::size_t expected = 4 + count + (list.flavour == 0 ? 1 : 0);
  if (lines.size() != expected) {
    throw fo4::parse_error(4, "an animation file list count does not match its lines");
  }
  list.files.assign(lines.begin() + 4, lines.begin() + 4 + static_cast<std::ptrdiff_t>(count));
  for (std::size_t index = 0; index < list.files.size(); ++index) {
    require_entry(list.files[index], 4 + index, "an animation file list");
  }
  require_unique(list.files, 4, "an animation file list");
  if (list.flavour == 0 && lines.back() != "0") {
    throw fo4::parse_error(lines.size() - 1, "a flavour 0 animation file list must end with a zero line");
  }
  return list;
}

std::string write_anim_file_list(const AnimFileList& list) {
  if (list.flavour > 1) {
    throw std::invalid_argument("an animation file list flavour is 0 or 1");
  }
  if (list.name.empty()) {
    throw std::invalid_argument("an animation file list needs a name");
  }
  if (list.files.size() > k_max_list_entries) {
    throw std::invalid_argument("an animation file list holds at most 100000 entries");
  }
  require_writable([&list] {
    if (list.flavour == 1) {
      require_numeric_id(list.name, 0);
    } else {
      require_plain(list.name, 0, "an animation file list");
    }
    for (const auto& file : list.files) {
      require_entry(file, 0, "an animation file list");
    }
    require_unique(list.files, 0, "an animation file list");
  });
  std::string out;
  append_line(out, std::to_string(k_anim_file_list_version), list.crlf);
  append_line(out, list.flavour == 0 ? "0" : "1", list.crlf);
  append_line(out, list.name, list.crlf);
  append_line(out, std::to_string(list.files.size()), list.crlf);
  for (const auto& file : list.files) {
    append_line(out, file, list.crlf);
  }
  if (list.flavour == 0) {
    append_line(out, "0", list.crlf);
  }
  return out;
}

AnimFileDirList parse_anim_file_dirlist(std::string_view text) {
  const Split split = split_lines(text, "an animation file dirlist", k_max_list_entries);
  AnimFileDirList list;
  list.names = split.lines;
  for (std::size_t index = 0; index < list.names.size(); ++index) {
    require_plain(list.names[index], index, "an animation file dirlist");
    const auto& name = list.names[index];
    if (name.size() < 5 || name.compare(name.size() - 4, 4, ".txt") != 0) {
      throw fo4::parse_error(index, "an animation file dirlist names a file that is not a .txt");
    }
  }
  require_unique(list.names, 0, "an animation file dirlist");
  return list;
}

std::string write_anim_file_dirlist(const AnimFileDirList& list) {
  if (list.names.empty()) {
    throw std::invalid_argument("an animation file dirlist needs at least one name");
  }
  require_writable([&list] {
    for (const auto& name : list.names) {
      require_plain(name, 0, "an animation file dirlist");
      if (name.size() < 5 || name.compare(name.size() - 4, 4, ".txt") != 0) {
        throw fo4::parse_error(0, "an animation file dirlist names a file that is not a .txt");
      }
    }
    require_unique(list.names, 0, "an animation file dirlist");
  });
  std::string out;
  for (const auto& name : list.names) {
    append_line(out, name, true);
  }
  return out;
}

AnimIdleData parse_anim_idle_data(std::string_view text) {
  const Split split = split_lines(text, "an animation idle list", k_max_list_entries + 1);
  const auto& lines = split.lines;
  if (lines.empty() || lines[0] != k_anim_idle_version) {
    throw fo4::parse_error(0, "an animation idle list has an unknown version line");
  }
  AnimIdleData data;
  data.crlf = split.crlf;
  data.poses.assign(lines.begin() + 1, lines.end());
  for (std::size_t index = 0; index < data.poses.size(); ++index) {
    require_plain(data.poses[index], 1 + index, "an animation idle list");
  }
  require_unique(data.poses, 1, "an animation idle list");
  return data;
}

std::string write_anim_idle_data(const AnimIdleData& data) {
  require_writable([&data] {
    for (const auto& pose : data.poses) {
      require_plain(pose, 0, "an animation idle list");
    }
    require_unique(data.poses, 0, "an animation idle list");
  });
  std::string out;
  append_line(out, k_anim_idle_version, data.crlf);
  for (const auto& pose : data.poses) {
    append_line(out, pose, data.crlf);
  }
  return out;
}

AnimEventInfo parse_anim_event_info(std::string_view text) {
  const Split split = split_lines(text, "an animation event info", std::string::npos);
  const auto& lines = split.lines;
  if (lines.size() < 4 || lines[0] != k_anim_event_version) {
    throw fo4::parse_error(0, "an animation event info has an unknown version line");
  }
  AnimEventInfo info;
  info.crlf = split.crlf;
  info.behavior = lines[1];
  require_plain(info.behavior, 1, "an animation event info");
  if (!lines[2].empty()) {
    info.parent_behavior = lines[2];
    require_plain(info.parent_behavior, 2, "an animation event info");
  }
  const std::size_t declared = parse_count(lines[3], 3, "an animation event info");
  std::size_t pos = 4;
  while (pos < lines.size()) {
    if (pos + 3 > lines.size()) {
      throw fo4::parse_error(pos, "an animation event record is shorter than its three header lines");
    }
    AnimEventRecord record;
    record.event = lines[pos];
    require_plain(record.event, pos, "an animation event record");
    const std::string& flag = lines[pos + 1];
    if (flag.size() != 1 || (flag[0] != '0' && flag[0] != '1')) {
      throw fo4::parse_error(pos + 1, "an animation event record flag is not 0 or 1");
    }
    record.flag = static_cast<std::uint32_t>(flag[0] - '0');
    const std::size_t clips = parse_count(lines[pos + 2], pos + 2, "an animation event record");
    if (clips == 0 || pos + 3 + clips > lines.size()) {
      throw fo4::parse_error(pos + 2, "an animation event record clip count runs past the body");
    }
    record.clips.assign(lines.begin() + static_cast<std::ptrdiff_t>(pos + 3),
                        lines.begin() + static_cast<std::ptrdiff_t>(pos + 3 + clips));
    for (std::size_t index = 0; index < record.clips.size(); ++index) {
      require_plain(record.clips[index], pos + 3 + index, "an animation event record");
    }
    info.records.push_back(std::move(record));
    pos += 3 + clips;
  }
  if (info.records.size() != declared) {
    throw fo4::parse_error(3, "an animation event info count does not match its records");
  }
  return info;
}

std::string write_anim_event_info(const AnimEventInfo& info) {
  if (info.behavior.empty()) {
    throw std::invalid_argument("an animation event info needs a behavior path");
  }
  require_writable([&info] {
    require_plain(info.behavior, 0, "an animation event info");
    if (!info.parent_behavior.empty()) {
      require_plain(info.parent_behavior, 0, "an animation event info");
    }
    for (const auto& record : info.records) {
      require_plain(record.event, 0, "an animation event record");
      if (record.flag > 1) {
        throw fo4::parse_error(0, "an animation event record flag is not 0 or 1");
      }
      if (record.clips.empty()) {
        throw fo4::parse_error(0, "an animation event record needs at least one clip");
      }
      for (const auto& clip : record.clips) {
        require_plain(clip, 0, "an animation event record");
      }
    }
  });
  std::string out;
  append_line(out, k_anim_event_version, info.crlf);
  append_line(out, info.behavior, info.crlf);
  append_line(out, info.parent_behavior, info.crlf);
  append_line(out, std::to_string(info.records.size()), info.crlf);
  for (const auto& record : info.records) {
    append_line(out, record.event, info.crlf);
    append_line(out, record.flag == 0 ? "0" : "1", info.crlf);
    append_line(out, std::to_string(record.clips.size()), info.crlf);
    for (const auto& clip : record.clips) {
      append_line(out, clip, info.crlf);
    }
  }
  return out;
}

}  // namespace fo4::anim
