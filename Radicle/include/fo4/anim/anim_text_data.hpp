#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::anim {

inline constexpr std::uint32_t k_anim_file_list_version = 3;

struct AnimFileList {
  std::uint32_t flavour{1};
  std::string name;
  std::vector<std::string> files;
  bool crlf{false};

  friend bool operator==(const AnimFileList&, const AnimFileList&) = default;
};

AnimFileList parse_anim_file_list(std::string_view text);

std::string write_anim_file_list(const AnimFileList& list);

struct AnimFileDirList {
  std::vector<std::string> names;

  friend bool operator==(const AnimFileDirList&, const AnimFileDirList&) = default;
};

AnimFileDirList parse_anim_file_dirlist(std::string_view text);
std::string write_anim_file_dirlist(const AnimFileDirList& list);

inline constexpr std::string_view k_anim_idle_version = "Version 1";

struct AnimIdleData {
  std::vector<std::string> poses;
  bool crlf{false};

  friend bool operator==(const AnimIdleData&, const AnimIdleData&) = default;
};

AnimIdleData parse_anim_idle_data(std::string_view text);
std::string write_anim_idle_data(const AnimIdleData& data);

inline constexpr std::string_view k_anim_event_version = "V2";

struct AnimEventRecord {
  std::string event;
  std::uint32_t flag{0};
  std::vector<std::string> clips;

  friend bool operator==(const AnimEventRecord&, const AnimEventRecord&) = default;
};

struct AnimEventInfo {
  std::string behavior;
  std::string parent_behavior;
  std::vector<AnimEventRecord> records;
  bool crlf{false};

  friend bool operator==(const AnimEventInfo&, const AnimEventInfo&) = default;
};

AnimEventInfo parse_anim_event_info(std::string_view text);
std::string write_anim_event_info(const AnimEventInfo& info);

}  // namespace fo4::anim
