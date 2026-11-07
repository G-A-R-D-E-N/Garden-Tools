#include "fo4/environment/scene_layout.hpp"

#include <algorithm>
#include <array>

namespace fo4::env::detail {
namespace {

constexpr Signature sig(const char (&s)[5]) { return Signature{s[0], s[1], s[2], s[3]}; }

constexpr std::array k_action_header{sig("NAM0"), sig("ALID"), sig("INAM"), sig("FNAM"), sig("SNAM"), sig("ENAM")};

void empty(const Subrecord& sub) {
  if (!sub.data.empty()) throw SceneLayoutError(sub.signature, "marker must be empty");
}

std::size_t action_header_end(std::span<const Subrecord> subs, std::size_t open, std::size_t close) {
  std::size_t next = 0, at = open + 1;
  for (; at < close; ++at) {
    const auto found = std::find(k_action_header.begin() + static_cast<std::ptrdiff_t>(next),
                                 k_action_header.end(), subs[at].signature);
    if (found == k_action_header.end()) break;
    next = static_cast<std::size_t>(found - k_action_header.begin()) + 1;
  }
  return at;
}

}

SceneLayout scene_layout(std::span<const Subrecord> subs) {
  SceneLayout out;
  out.first_group = subs.size();
  for (std::size_t i = 0; i < subs.size(); ++i) {
    const auto tag = subs[i].signature;
    if (tag == sig("HNAM")) {
      out.first_group = std::min(out.first_group, i);
      empty(subs[i]);
      std::size_t close = i + 1;
      std::vector<std::size_t> next;
      for (; close < subs.size() && subs[close].signature != sig("HNAM"); ++close) {
        if (subs[close].signature == sig("ANAM") || subs[close].signature == sig("ALID")) {
          throw SceneLayoutError(tag, "phase crosses a later group");
        }
        if (subs[close].signature == sig("NEXT")) {
          empty(subs[close]);
          next.push_back(close);
        }
      }
      if (close == subs.size() || next.size() != 2) {
        throw SceneLayoutError(tag, "phase requires two NEXT separators and HNAM close");
      }
      empty(subs[close]);
      out.phases.push_back({i, next[0], next[1], close});
      out.tail = close + 1;
      i = close;
    } else if (tag == sig("ALID")) {
      out.first_group = std::min(out.first_group, i);
      if (i + 2 >= subs.size() || subs[i].data.size() != 4 || subs[i + 1].signature != sig("LNAM") ||
          subs[i + 1].data.size() != 4 || subs[i + 2].signature != sig("DNAM") || subs[i + 2].data.size() != 4) {
        throw SceneLayoutError(tag, "actor requires ALID/LNAM/DNAM four-byte triplet");
      }
      out.actors.push_back(i);
      i += 2;
      out.tail = i + 1;
    } else if (tag == sig("ANAM")) {
      out.first_group = std::min(out.first_group, i);
      if (subs[i].data.size() != 2) throw SceneLayoutError(tag, "action opener must be two bytes");
      std::size_t close = i + 1;
      while (close < subs.size() && subs[close].signature != sig("ANAM")) ++close;
      if (close == subs.size()) throw SceneLayoutError(tag, "action requires an empty ANAM close");
      empty(subs[close]);
      out.actions.push_back({i, action_header_end(subs, i, close), close});
      out.tail = close + 1;
      i = close;
    } else if (tag == sig("NEXT")) {
      throw SceneLayoutError(tag, "NEXT outside a phase");
    }
  }
  return out;
}

}
