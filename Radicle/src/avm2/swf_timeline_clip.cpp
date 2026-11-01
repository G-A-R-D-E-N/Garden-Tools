#include <fo4/avm2/swf_runtime.hpp>

#include <fo4/avm2/display.hpp>
#include <fo4/avm2/events.hpp>
#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/name.hpp>
#include <fo4/avm2/object.hpp>
#include <fo4/avm2/runtime.hpp>
#include <fo4/avm2/text.hpp>

#include "native_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace fo4::avm2 {

namespace {

std::shared_ptr<TimelineDefinition> timeline_from_rows(const SwfTimelineRows& rows) {
  auto timeline = std::make_shared<TimelineDefinition>();
  timeline->frame_count = std::max<std::uint32_t>(1, rows.frame_count);
  timeline->labels = rows.labels;
  return timeline;
}

Class* resolve_character_class(Runtime& runtime, const SwfMovieData& data,
                               std::uint16_t character_id) {
  const auto symbol = data.symbol_classes.find(character_id);
  if (symbol != data.symbol_classes.end() && !symbol->second.empty()) {
    if (Class* klass = const_cast<Class*>(runtime.find_class(symbol->second))) {
      return klass;
    }
  }
  if (data.sprites.find(character_id) != data.sprites.end()) {
    return const_cast<Class*>(runtime.find_class("flash.display::MovieClip"));
  }
  if (data.text_fields.find(character_id) != data.text_fields.end()) {
    return const_cast<Class*>(runtime.find_class("flash.text::TextField"));
  }
  if (data.static_texts.find(character_id) != data.static_texts.end()) {
    return const_cast<Class*>(runtime.find_class("flash.text::StaticText"));
  }
  if (Class* shape = const_cast<Class*>(runtime.find_class("flash.display::Shape"))) {
    return shape;
  }
  return const_cast<Class*>(runtime.find_class("flash.display::MovieClip"));
}

ExecutionResult initialize_placed(Interpreter& interpreter,
                                  const std::shared_ptr<DisplayObject>& object, Class& klass,
                                  std::size_t depth) {
  ExecutionResult prepared = interpreter.prepare_class(klass, depth);
  if (!prepared.ok()) {
    return prepared;
  }
  klass.apply_slot_defaults(*object);
  return ExecutionResult{};
}

ExecutionResult run_placed_constructor(Interpreter& interpreter,
                                       const std::shared_ptr<DisplayObject>& object, Class& klass,
                                       std::size_t depth) {
  if (klass.iinit == kNoMethod || klass.file == nullptr) {
    return ExecutionResult{};
  }
  auto initializer = std::make_shared<FunctionObject>(klass.file->method_name(klass.iinit),
                                                      klass.file, klass.iinit);
  initializer->set_owner_class(&klass);
  return interpreter.call_function(*initializer, Value::object(object), {}, depth);
}

void apply_character_definition(DisplayObject& object, const SwfMovieData& data,
                                std::uint16_t character_id) {
  const auto bounds = data.character_bounds.find(character_id);
  if (bounds != data.character_bounds.end()) {
    object.set_origin(bounds->second.origin_x, bounds->second.origin_y);
    object.set_width(bounds->second.width);
    object.set_height(bounds->second.height);
  }
  // Geometry must attach at creation, so a script's later size writes cannot clear it.
  const auto shape = data.character_shapes.find(character_id);
  if (shape != data.character_shapes.end()) {
    object.set_shape_geometry(std::make_shared<ShapeGeometry>(shape->second));
  }
  const auto static_text = data.static_texts.find(character_id);
  if (static_text != data.static_texts.end() && object.is_static_text()) {
    auto& static_object = static_cast<StaticTextObject&>(object);
    static_object.set_font_owner(data.font_owner);
    static_object.set_records(static_text->second);
  }
  const auto text = data.text_fields.find(character_id);
  if (text == data.text_fields.end() || !object.is_text_field()) {
    return;
  }
  auto& field = static_cast<TextFieldObject&>(object);
  field.set_font_owner(data.font_owner);
  field.set_format(text->second.format);
  field.set_word_wrap(text->second.word_wrap);
  field.set_multiline(text->second.multiline);
  field.set_auto_size(text->second.auto_size);
  if (text->second.html) {
    field.set_html_text(text->second.text);
  } else {
    field.set_text(text->second.text);
  }
}

void apply_matrix(DisplayObject& object, const SwfPlacementRow& row) {
  object.set_x(row.translate_x);
  object.set_y(row.translate_y);
  if (row.rotate_skew0 == 0.0 && row.rotate_skew1 == 0.0) {
    object.set_rotation(0.0);
    object.set_scale_x(row.scale_x);
    object.set_scale_y(row.scale_y);
    return;
  }
  const double determinant = row.scale_x * row.scale_y - row.rotate_skew0 * row.rotate_skew1;
  object.set_rotation(std::atan2(row.rotate_skew0, row.scale_x) * 180.0 / std::numbers::pi);
  object.set_scale_x(std::hypot(row.scale_x, row.rotate_skew0));
  object.set_scale_y(std::copysign(std::hypot(row.rotate_skew1, row.scale_y), determinant));
}

void apply_placement_state(DisplayObject& object, const SwfPlacementRow& row) {
  if (row.has_ratio || !row.move) {
    object.set_ratio(row.has_ratio ? std::optional<std::uint16_t>{row.ratio} : std::nullopt);
  }
  if (row.has_blend_mode || !row.move) {
    object.set_blend_mode(row.has_blend_mode ? row.blend_mode : std::uint8_t{0});
  }
  if (row.has_visible || !row.move) {
    object.set_visible(row.has_visible ? row.visible : true);
  }
  if (row.has_bitmap_cache || !row.move) {
    object.set_cache_as_bitmap(row.has_bitmap_cache && row.bitmap_cache);
  }
  if (row.has_filters || !row.move) {
    object.set_placed_filters(row.filters);
  }
}

class SwfTimelineClip : public MovieClip {
 public:
  SwfTimelineClip(Class* display_class, std::shared_ptr<SwfMovieData> data,
                  const SwfTimelineRows* rows, std::uint16_t character_id)
      : MovieClip(display_class),
        data_(std::move(data)),
        rows_(rows),
        character_id_(character_id) {
    if (display_class != nullptr && display_class->prototype_object != nullptr) {
      set_prototype(display_class->prototype_object);
    }
    if (rows_ != nullptr) {
      set_timeline(timeline_from_rows(*rows_));
    }
    set_dynamic(true);
  }

  std::uint16_t character_id() const noexcept { return character_id_; }

 protected:
  ExecutionResult apply_frame_state(Interpreter& interpreter, std::size_t depth,
                                    bool advance_children) override {
    if (advance_children && !playing()) {
      return advance_child_clips(interpreter, depth, {}, true);
    }
    if (rows_ == nullptr) {
      return ExecutionResult{};
    }
    const std::uint32_t frame = current_frame();
    if (frame == 0 || frame > rows_->frames.size()) {
      return ExecutionResult{};
    }
    Runtime& runtime = interpreter.runtime();
    std::vector<std::uint16_t> just_created;
    for (const SwfPlacementRow& row : rows_->frames[frame - 1]) {
      if (row.kind == SwfPlacementRow::Kind::Remove) {
        detach(interpreter, row.depth, depth);
        continue;
      }
      Class* klass = nullptr;
      if (row.has_character) {
        klass = resolve_character_class(runtime, *data_, row.character_id);
        if (klass == nullptr) {
          continue;
        }
      }
      std::shared_ptr<DisplayObject> object;
      std::shared_ptr<SwfTimelineClip> clip;
      bool created = false;
      const auto found = objects_.find(row.depth);
      if (!row.has_character) {
        if (found == objects_.end()) {
          continue;
        }
        object = found->second;
      } else if (found != objects_.end() && found->second->display_class() == klass &&
                 found->second->character_id() == row.character_id) {
        object = found->second;
      } else {
        detach(interpreter, row.depth, depth);
        const auto sprite = data_->sprites.find(row.character_id);
        if (sprite != data_->sprites.end()) {
          clip = std::make_shared<SwfTimelineClip>(klass, data_, &sprite->second,
                                                   row.character_id);
          object = clip;
        } else {
          object = make_display_object(runtime, *klass);
        }
        if (object == nullptr) {
          continue;
        }
        apply_character_definition(*object, *data_, row.character_id);
        ExecutionResult initialized = initialize_placed(interpreter, object, *klass, depth + 1);
        if (!initialized.ok()) {
          return initialized;
        }
        created = true;
      }
      if (row.has_matrix || !row.move) {
        apply_matrix(*object, row);
      }
      if (row.has_character) {
        object->set_character_id(row.character_id);
      }
      if (row.has_color_transform) {
        object->set_color_multiply(row.color_multiply);
        object->set_color_add(row.color_add);
      }
      if (row.has_clip_depth || !row.move) {
        object->set_clip_depth(row.has_clip_depth ? static_cast<std::int32_t>(row.clip_depth) : 0);
      }
      apply_placement_state(*object, row);
      if (!row.name.empty()) {
        object->set_name(row.name);
      }
      if (created) {
        if (clip != nullptr) {
          ExecutionResult placed = clip->place_frame(interpreter, 1, true, depth);
          if (!placed.ok()) {
            return placed;
          }
        }
        ExecutionResult constructed =
            run_placed_constructor(interpreter, object, *klass, depth + 1);
        if (!constructed.ok()) {
          return constructed;
        }
        if (clip != nullptr) {
          sprite_clips_[row.depth] = clip.get();
        }
        add_child(object);
        objects_[row.depth] = object;
        just_created.push_back(row.depth);
      }
      if (!row.name.empty()) {
        store_named_child(interpreter, object, row.name);
      }
    }
    order_children_by_depth();
    for (const std::uint16_t layer : just_created) {
      const auto added_object = objects_.find(layer);
      if (added_object == objects_.end()) {
        continue;
      }
      const std::shared_ptr<DisplayObject> object = added_object->second;
      dispatch_added(interpreter, object, depth + 1);
    }
    return advance_child_clips(interpreter, depth, just_created, advance_children);
  }

 private:
  ExecutionResult advance_child_clips(Interpreter& interpreter, std::size_t depth,
                                      const std::vector<std::uint16_t>& just_created,
                                      bool advance_children) {
    std::vector<std::pair<std::uint16_t, std::shared_ptr<DisplayObject>>> sprites;
    for (const auto& [layer, object] : objects_) {
      if (sprite_clips_.count(layer) != 0) {
        sprites.emplace_back(layer, object);
      }
    }
    for (const auto& [layer, object] : sprites) {
      const auto current = objects_.find(layer);
      if (current == objects_.end() || current->second != object) {
        continue;
      }
      SwfTimelineClip& clip = static_cast<SwfTimelineClip&>(*object);
      const bool created = std::find(just_created.begin(), just_created.end(), layer) !=
                           just_created.end();
      if (!created && !advance_children) {
        continue;
      }
      ExecutionResult result = created ? clip.run_frame_actions(interpreter, depth)
                                       : clip.advance_timeline(interpreter, depth);
      if (!result.ok()) {
        return result;
      }
    }
    return ExecutionResult{};
  }

  void detach(Interpreter& interpreter, std::uint16_t layer, std::size_t depth) {
    const auto found = objects_.find(layer);
    if (found == objects_.end()) {
      return;
    }
    const std::shared_ptr<DisplayObject> object = found->second;
    dispatch_removed(interpreter, object, depth + 1);
    remove_child(object);
    sprite_clips_.erase(layer);
    objects_.erase(layer);
  }

  void store_named_child(Interpreter& interpreter, const std::shared_ptr<DisplayObject>& child,
                         const std::string& name) {
    Multiname multiname;
    multiname.kind = MultinameKind::QName;
    multiname.name = name;
    multiname.namespaces.push_back(Namespace{});
    const Class* klass = instance_class();
    const Trait* trait = klass == nullptr ? nullptr : klass->resolve_instance_trait(multiname);
    if (trait == nullptr || trait->kind != TraitKind::Slot) {
      return;
    }
    Value value = Value::object(child);
    if (!trait->type_name.empty() && trait->type_name != "*") {
      const Class* type = interpreter.runtime().find_class(trait->type_name);
      std::string message;
      if (type == nullptr || !interpreter.coerce_to_class(*type, value, message).ok()) {
        return;
      }
    }
    if (trait->slot_id >= slots().size()) {
      resize_slots(trait->slot_id + 1);
    }
    slots()[trait->slot_id] = value;
  }

  void order_children_by_depth() {
    std::vector<std::shared_ptr<DisplayObject>> expected;
    expected.reserve(objects_.size());
    for (const auto& entry : objects_) {
      expected.push_back(entry.second);
    }
    for (std::size_t index = 0; index < expected.size(); ++index) {
      if (index >= children().size() || children()[index] != expected[index]) {
        const auto found = std::find(children().begin(), children().end(), expected[index]);
        if (found != children().end()) {
          set_child_index(expected[index].get(), index);
        }
      }
    }
  }

  std::shared_ptr<SwfMovieData> data_;
  const SwfTimelineRows* rows_{nullptr};
  std::uint16_t character_id_{0};
  std::map<std::uint16_t, std::shared_ptr<DisplayObject>> objects_;
  std::map<std::uint16_t, SwfTimelineClip*> sprite_clips_;
};

}  // namespace

std::shared_ptr<MovieClip> build_movie_root(Runtime& runtime,
                                            const std::shared_ptr<SwfMovieData>& data,
                                            Class** out_root_class) {
  if (data == nullptr) {
    return nullptr;
  }
  Class* root_class = const_cast<Class*>(runtime.find_class("flash.display::MovieClip"));
  if (!data->root_class_name.empty()) {
    Class* document_class = const_cast<Class*>(runtime.find_class(data->root_class_name));
    Class* display_object_class =
        const_cast<Class*>(runtime.find_class("flash.display::DisplayObject"));
    if (document_class != nullptr && display_object_class != nullptr &&
        document_class->is_subclass_of(display_object_class)) {
      root_class = document_class;
    }
  }
  if (root_class == nullptr) {
    return nullptr;
  }
  if (out_root_class != nullptr) {
    out_root_class[0] = root_class;
  }
  auto root = std::make_shared<SwfTimelineClip>(root_class, data, &data->root, std::uint16_t{0});
  root->set_playing(true);
  return root;
}

}  // namespace fo4::avm2
