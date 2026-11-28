#pragma once

#include "fo4/archive/assets.hpp"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace fo4::hkx {

class AnimationSourceError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

struct AnimationSourceRequest {
  std::filesystem::path data_dir;
  std::filesystem::path mo2_instance;
  std::string profile;
};

struct AnimationSources {
  archive::AssetResolver resolver;
  std::string manager{"none"};
  std::string profile;
  std::filesystem::path data_dir;
  std::vector<std::string> notes;
};

// Requires a game Data folder or an MO2 instance whose ini names one, and throws AnimationSourceError instead of returning an empty default.
// Invariant: the resolver's roots are highest priority first and its archives lowest first, so loose files beat archives and a higher mod beats a lower one.
AnimationSources open_animation_sources(const AnimationSourceRequest& request);

}  // namespace fo4::hkx
