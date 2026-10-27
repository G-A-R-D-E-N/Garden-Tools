#pragma once

#include <fo4/avm2/class.hpp>
#include <fo4/avm2/display.hpp>
#include <fo4/avm2/object.hpp>
#include <fo4/avm2/sound.hpp>
#include <fo4/avm2/value.hpp>
#include <fo4/swf/swf.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace fo4::avm2 {

/* A Loader must own its movie's root clip so a snapshot shows it under the Loader entry. */
class SwfRuntime;
class ApplicationDomainObject;

class LoaderObject : public DisplayObjectContainer {
 public:
  explicit LoaderObject(Class* display_class);
  std::shared_ptr<MovieClip> content;
  std::shared_ptr<SwfRuntime> child_runtime;
  std::shared_ptr<ApplicationDomainObject> domain;
  std::shared_ptr<swf::SwfFile> movie;
  std::vector<std::uint8_t> bytes;
  std::string data;
  bool loaded{false};
};

/* A Sound instance must keep the registered sound id, so play() can queue a snapshot event. */
class SoundObject : public Object {
 public:
  explicit SoundObject(Class* sound_class);
  std::uint32_t sound_id{0};
  bool loaded{false};
};

class SoundChannelObject : public Object {
 public:
  explicit SoundChannelObject(Class* channel_class);
  std::uint32_t sound_id{0};
  double volume{1.0};
  double pan{0.0};
};

/* A SharedObject must keep its name and last decoded data so flush can write the same body back. */
class SharedObjectObject : public Object {
 public:
  explicit SharedObjectObject(Class* shared_class);
  std::string name;
  Value data;
  bool dirty{false};
};

}  // namespace fo4::avm2
