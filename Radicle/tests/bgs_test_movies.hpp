#pragma once

#include "avm2_test_support.hpp"
#include "fo4/abc/abc.hpp"
#include "fo4/swf/swf.hpp"
#include "swf_movie_builder.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace bgs_test {

namespace abc = fo4::abc;

inline abc::AbcTrait method_trait(std::uint32_t name, std::uint32_t method) {
  abc::AbcTrait trait;
  trait.name = name;
  trait.kind = abc::AbcTraitKind::Method;
  trait.method_index = method;
  return trait;
}

inline abc::AbcFile document_class_file(bool sets_code_object) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder constructor;
  if (sets_code_object) {
    constructor.getlocal0();
    constructor.pushscope();
    constructor.getlocal0();
    constructor.newobject(0);
    constructor.initproperty(script.multiname("BGSCodeObj"));
  }
  constructor.returnvoid();
  const std::uint32_t document = script.multiname("DocumentClass");
  const std::uint32_t movie_clip = script.multiname("MovieClip", "flash.display");
  const std::uint32_t constructor_method =
      script.method("DocumentClass", constructor.bytes(), 1);
  const std::uint32_t class_init = script.method("cinit", {0x47}, 1);
  const std::uint32_t script_init = script.method("init", {0x47}, 1);
  abc::AbcFile file = script.finish(script_init);

  abc::AbcInstanceInfo instance;
  instance.name = document;
  instance.super_index = movie_clip;
  instance.iinit = constructor_method;
  file.instances.push_back(instance);
  abc::AbcClassInfo klass;
  klass.cinit = class_init;
  file.classes.push_back(klass);
  abc::AbcTrait class_trait;
  class_trait.name = document;
  class_trait.kind = abc::AbcTraitKind::Class;
  class_trait.class_index = 0;
  file.scripts.front().traits.push_back(class_trait);
  return file;
}

inline std::vector<std::byte> movie_of(abc::AbcFile file, std::uint16_t frames = 1) {
  std::vector<std::uint8_t> abc_bytes;
  EXPECT_EQ(file.serialize(abc_bytes), abc::AbcStatus::Ok);
  swf_test::SwfBuilder abc_payload;
  abc_payload.u32(0);
  abc_payload.text("document");
  abc_payload.bytes(std::span<const std::byte>(
      reinterpret_cast<const std::byte*>(abc_bytes.data()), abc_bytes.size()));

  swf_test::SwfBuilder symbols;
  symbols.u16(1);
  symbols.u16(0);
  symbols.text("DocumentClass");

  swf_test::SwfBuilder builder;
  builder.rect(0, 400, 0, 200, 9);
  builder.u16(24u * 256u);
  builder.u16(frames);
  builder.tag(fo4::swf::tag::kDoAbc2, abc_payload.body, true);
  builder.tag(fo4::swf::tag::kSymbolClass, symbols.body, false);
  for (std::uint16_t frame = 0; frame < frames; ++frame) {
    builder.tag(fo4::swf::tag::kShowFrame);
  }
  builder.tag(fo4::swf::tag::kEnd);
  return swf_test::wrap_body(builder.body, 10, false, false);
}

inline std::vector<std::byte> document_movie(bool sets_code_object) {
  return movie_of(document_class_file(sets_code_object));
}

inline abc::AbcFile capture_root_file(bool references_random) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t root = script.multiname("DocumentClass");
  const std::uint32_t movie_clip = script.multiname("MovieClip", "flash.display");
  const std::uint32_t code_object = script.multiname("BGSCodeObj");
  const std::uint32_t register_sound = script.multiname("registerSound");
  const std::uint32_t play_sound = script.multiname("playSound");
  const std::uint32_t set_highscore = script.multiname("setHighscore");
  const std::uint32_t text_id = script.multiname("getTextReplaceID");
  const std::uint32_t notify = script.multiname("notifyScripts");
  const std::uint32_t play_left = script.multiname("playLeftAnim");
  const std::uint32_t init_name = script.multiname("InitProgram");
  const std::uint32_t highscore_name = script.multiname("SetHighscore");
  const std::uint32_t lookup_name = script.multiname("Lookup");
  const std::uint32_t pause_name = script.multiname("Pause");
  const std::uint32_t int_type = script.multiname("int");
  const std::uint32_t string_type = script.multiname("String");
  const std::uint32_t boolean_type = script.multiname("Boolean");
  if (references_random) {
    script.multiname("random");
  }
  const std::uint32_t intro = script.string_of("intro");

  avm2_test::CodeBuilder constructor;
  constructor.getlocal0();
  constructor.pushscope();
  constructor.getlocal0();
  constructor.newobject(0);
  constructor.initproperty(code_object);
  constructor.returnvoid();
  const std::uint32_t constructor_method = script.method("DocumentClass", constructor.bytes(), 1);

  avm2_test::CodeBuilder init;
  init.getlocal0();
  init.getproperty(code_object);
  init.pushstring(intro);
  init.callpropvoid(register_sound, 1);
  init.getlocal0();
  init.getproperty(code_object);
  init.pushstring(intro);
  init.callpropvoid(play_sound, 1);
  init.returnvoid();
  const std::uint32_t init_method = script.method("InitProgram", init.bytes(), 1);

  avm2_test::CodeBuilder highscore;
  highscore.getlocal0();
  highscore.getproperty(code_object);
  highscore.getlocal(1);
  highscore.callpropvoid(set_highscore, 1);
  highscore.returnvoid();
  const std::uint32_t highscore_method = script.method("SetHighscore", highscore.bytes(), 2);

  avm2_test::CodeBuilder lookup;
  lookup.getlocal0();
  lookup.getproperty(code_object);
  lookup.getlocal0();
  lookup.getproperty(code_object);
  lookup.getlocal(1);
  lookup.callproperty(text_id, 1);
  lookup.callpropvoid(notify, 1);
  lookup.returnvoid();
  const std::uint32_t lookup_method = script.method("Lookup", lookup.bytes(), 2);

  avm2_test::CodeBuilder pause;
  pause.getlocal0();
  pause.getproperty(code_object);
  pause.getlocal(1);
  pause.callpropvoid(play_left, 1);
  pause.returnvoid();
  const std::uint32_t pause_method = script.method("Pause", pause.bytes(), 2);
  const std::uint32_t class_init = script.method("cinit", {0x47}, 1);
  const std::uint32_t script_init = script.method("init", {0x47}, 1);
  abc::AbcFile file = script.finish(script_init);
  file.methods[highscore_method].param_count = 1;
  file.methods[highscore_method].param_types = {int_type};
  file.methods[lookup_method].param_count = 1;
  file.methods[lookup_method].param_types = {string_type};
  file.methods[pause_method].param_count = 1;
  file.methods[pause_method].param_types = {boolean_type};

  abc::AbcInstanceInfo instance;
  instance.name = root;
  instance.super_index = movie_clip;
  instance.iinit = constructor_method;
  instance.traits = {method_trait(init_name, init_method),
                     method_trait(highscore_name, highscore_method),
                     method_trait(lookup_name, lookup_method),
                     method_trait(pause_name, pause_method)};
  file.instances.push_back(instance);
  abc::AbcClassInfo klass;
  klass.cinit = class_init;
  file.classes.push_back(klass);
  abc::AbcTrait class_trait;
  class_trait.name = root;
  class_trait.kind = abc::AbcTraitKind::Class;
  class_trait.class_index = 0;
  file.scripts.front().traits.push_back(class_trait);
  return file;
}

inline abc::AbcFile frame_failure_file() {
  avm2_test::ScriptBuilder script;
  const std::uint32_t root = script.multiname("DocumentClass");
  const std::uint32_t movie_clip = script.multiname("MovieClip", "flash.display");
  const std::uint32_t add_frame_script = script.multiname("addFrameScript");
  const std::uint32_t missing = script.multiname("noSuchFrameFunction");
  avm2_test::CodeBuilder failing;
  failing.getlocal0();
  failing.callpropvoid(missing, 0);
  failing.returnvoid();
  const std::uint32_t failing_method = script.method("failingFrame", failing.bytes(), 1);
  avm2_test::CodeBuilder constructor;
  constructor.getlocal0();
  constructor.pushscope();
  constructor.getlocal0();
  constructor.pushbyte(1);
  constructor.newfunction(failing_method);
  constructor.callpropvoid(add_frame_script, 2);
  constructor.returnvoid();
  const std::uint32_t constructor_method = script.method("DocumentClass", constructor.bytes(), 1);
  const std::uint32_t class_init = script.method("cinit", {0x47}, 1);
  const std::uint32_t script_init = script.method("init", {0x47}, 1);
  abc::AbcFile file = script.finish(script_init);

  abc::AbcInstanceInfo instance;
  instance.name = root;
  instance.super_index = movie_clip;
  instance.iinit = constructor_method;
  file.instances.push_back(instance);
  abc::AbcClassInfo klass;
  klass.cinit = class_init;
  file.classes.push_back(klass);
  abc::AbcTrait class_trait;
  class_trait.name = root;
  class_trait.kind = abc::AbcTraitKind::Class;
  class_trait.class_index = 0;
  file.scripts.front().traits.push_back(class_trait);
  return file;
}

inline abc::AbcFile score_root_file() {
  avm2_test::ScriptBuilder script;
  const std::uint32_t root = script.multiname("DocumentClass");
  const std::uint32_t movie_clip = script.multiname("MovieClip", "flash.display");
  const std::uint32_t code_object = script.multiname("BGSCodeObj");
  const std::uint32_t set_highscore = script.multiname("setHighscore");
  const std::uint32_t get_highscore = script.multiname("getHighscore");
  const std::uint32_t stored = script.multiname("lastHighscore");
  const std::uint32_t set_score_name = script.multiname("SetScore");
  const std::uint32_t request_name = script.multiname("RequestScore");
  const std::uint32_t callback_name = script.multiname("SetHighscore");
  const std::uint32_t int_type = script.multiname("int");
  const std::uint32_t key = script.string_of("GAME_HIGH_SCORE_KEY");

  avm2_test::CodeBuilder constructor;
  constructor.getlocal0();
  constructor.pushscope();
  constructor.getlocal0();
  constructor.newobject(0);
  constructor.initproperty(code_object);
  constructor.returnvoid();
  const std::uint32_t constructor_method = script.method("DocumentClass", constructor.bytes(), 1);

  avm2_test::CodeBuilder set_score;
  set_score.getlocal0();
  set_score.getproperty(code_object);
  set_score.pushstring(key);
  set_score.getlocal(1);
  set_score.callpropvoid(set_highscore, 2);
  set_score.returnvoid();
  const std::uint32_t set_score_method = script.method("SetScore", set_score.bytes(), 2);

  avm2_test::CodeBuilder request;
  request.getlocal0();
  request.getproperty(code_object);
  request.pushstring(key);
  request.callpropvoid(get_highscore, 1);
  request.returnvoid();
  const std::uint32_t request_method = script.method("RequestScore", request.bytes(), 1);

  avm2_test::CodeBuilder callback;
  callback.getlocal0();
  callback.getlocal(1);
  callback.setproperty(stored);
  callback.returnvoid();
  const std::uint32_t callback_method = script.method("SetHighscore", callback.bytes(), 2);
  const std::uint32_t class_init = script.method("cinit", {0x47}, 1);
  const std::uint32_t script_init = script.method("init", {0x47}, 1);
  abc::AbcFile file = script.finish(script_init);
  file.methods[set_score_method].param_count = 1;
  file.methods[set_score_method].param_types = {int_type};
  file.methods[callback_method].param_count = 1;
  file.methods[callback_method].param_types = {int_type};

  abc::AbcInstanceInfo instance;
  instance.name = root;
  instance.super_index = movie_clip;
  instance.iinit = constructor_method;
  instance.traits = {method_trait(set_score_name, set_score_method),
                     method_trait(request_name, request_method),
                     method_trait(callback_name, callback_method)};
  file.instances.push_back(instance);
  abc::AbcClassInfo klass;
  klass.cinit = class_init;
  file.classes.push_back(klass);
  abc::AbcTrait class_trait;
  class_trait.name = root;
  class_trait.kind = abc::AbcTraitKind::Class;
  class_trait.class_index = 0;
  file.scripts.front().traits.push_back(class_trait);
  return file;
}

}  // namespace bgs_test
