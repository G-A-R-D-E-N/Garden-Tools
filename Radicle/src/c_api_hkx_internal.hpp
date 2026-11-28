#pragma once

#include "c_api_internal.hpp"
#include "fo4/c_api_hkx.h"
#include "fo4/hkx/animation.hpp"
#include "fo4/hkx/behavior_graph.hpp"
#include "fo4/hkx/clip_roles.hpp"
#include "fo4/hkx/packfile.hpp"
#include "fo4/hkx/skeleton.hpp"

#include <cstdio>
#include <vector>

namespace radicle_hkx_detail {

template <typename T>
struct Part {
  T value{};
  fo4::hkx::HkxStatus status{fo4::hkx::HkxStatus::MissingObject};
};

}  // namespace radicle_hkx_detail

// Invariant: radicle_hkx_read fills every part once and nothing mutates them, so they must stay read-only.
struct radicle_hkx {
  radicle_hkx_detail::Part<fo4::hkx::HkxSkeleton> skeleton;
  radicle_hkx_detail::Part<fo4::hkx::HkxAnimation> animation;
  radicle_hkx_detail::Part<fo4::hkx::HkxAnimationBinding> binding;
  radicle_hkx_detail::Part<fo4::hkx::HkxExtractedMotion> motion;
  radicle_hkx_detail::Part<std::vector<fo4::hkx::HkxBehaviorClip>> clips;
  radicle_hkx_detail::Part<fo4::hkx::HkxGraphModel> graph;
};

namespace radicle_hkx_detail {

inline radicle_status part_status(fo4::hkx::HkxStatus status, const char* what) {
  using radicle_c_detail::fail;
  if (status == fo4::hkx::HkxStatus::Ok) return RADICLE_STATUS_OK;
  char message[128];
  if (status == fo4::hkx::HkxStatus::MissingObject) {
    std::snprintf(message, sizeof message, "the packfile has no %s", what);
    return fail(RADICLE_STATUS_NOT_FOUND, message);
  }
  std::snprintf(message, sizeof message, "the %s could not be decoded, HkxStatus %d", what, static_cast<int>(status));
  return fail(RADICLE_STATUS_PARSE_ERROR, message);
}

template <typename T>
const T* need(const radicle_hkx* hkx, Part<T> radicle_hkx::*member, const char* what, radicle_status& status) {
  if (!hkx) {
    status = radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "hkx is required");
    return nullptr;
  }
  const Part<T>& part = hkx->*member;
  status = part_status(part.status, what);
  return status == RADICLE_STATUS_OK ? &part.value : nullptr;
}

}  // namespace radicle_hkx_detail
