#include <fo4/avm2/swf_runtime.hpp>

#include <fo4/avm2/display.hpp>
#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/name.hpp>
#include <fo4/avm2/object.hpp>
#include <fo4/avm2/runtime.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
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
  if (Class* shape = const_cast<Class*>(runtime.find_class("flash.display::Shape"))) {
    return shape;
  }
  return const_cast<Class*>(runtime.find_class("flash.display::MovieClip"));
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
  ExecutionResult apply_frame_state(Interpreter& interpreter) override {
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
        const auto found = objects_.find(row.depth);
        if (found != objects_.end()) {
          remove_child(found->second);
          sprite_clips_.erase(row.depth);
          objects_.erase(found);
        }
        continue;
      }
      Class* klass = resolve_character_class(runtime, *data_, row.character_id);
      if (klass == nullptr) {
        continue;
      }
      std::shared_ptr<DisplayObject> object;
      const auto found = objects_.find(row.depth);
      if (found != objects_.end() && found->second->display_class() == klass) {
        object = found->second;
      } else {
        if (found != objects_.end()) {
          remove_child(found->second);
          sprite_clips_.erase(row.depth);
          objects_.erase(found);
        }
        const auto sprite = data_->sprites.find(row.character_id);
        if (sprite != data_->sprites.end()) {
          auto clip = std::make_shared<SwfTimelineClip>(klass, data_, &sprite->second,
                                                        row.character_id);
          object = clip;
          sprite_clips_[row.depth] = clip.get();
        } else {
          object = make_display_object(runtime, *klass);
        }
        if (object == nullptr) {
          continue;
        }
        add_child(object);
        objects_[row.depth] = object;
        just_created.push_back(row.depth);
      }
      object->set_x(row.translate_x);
      object->set_y(row.translate_y);
      object->set_scale_x(row.scale_x);
      object->set_scale_y(row.scale_y);
      if (!row.name.empty()) {
        object->set_name(row.name);
      }
    }
    order_children_by_depth();
    for (const auto& [depth, clip] : sprite_clips_) {
      if (clip == nullptr) {
        continue;
      }
      const bool created = std::find(just_created.begin(), just_created.end(), depth) !=
                           just_created.end();
      ExecutionResult result =
          created ? clip->goto_frame(interpreter, 1, true) : clip->advance_frame(interpreter);
      if (!result.ok()) {
        return result;
      }
    }
    return ExecutionResult{};
  }

 private:
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
                                            const std::shared_ptr<SwfMovieData>& data) {
  if (data == nullptr) {
    return nullptr;
  }
  Class* root_class = const_cast<Class*>(runtime.find_class("flash.display::MovieClip"));
  if (!data->root_class_name.empty()) {
    Class* document_class = const_cast<Class*>(runtime.find_class(data->root_class_name));
    Class* movie_clip_class = const_cast<Class*>(runtime.find_class("flash.display::MovieClip"));
    if (document_class != nullptr && movie_clip_class != nullptr &&
        document_class->is_subclass_of(movie_clip_class)) {
      root_class = document_class;
    }
  }
  if (root_class == nullptr) {
    return nullptr;
  }
  auto root = std::make_shared<SwfTimelineClip>(root_class, data, &data->root, 0);
  root->set_playing(true);
  return root;
}

std::shared_ptr<SwfRuntime> SwfRuntime::create(Runtime& runtime, Interpreter& interpreter,
                                               const swf::SwfFile& file,
                                               const SwfRuntimeOptions& options) {
  auto data = read_swf_movie_data(file, options);
  if (data == nullptr) {
    return nullptr;
  }
  Class* stage_class = const_cast<Class*>(runtime.find_class("flash.display::Stage"));
  if (stage_class == nullptr) {
    return nullptr;
  }
  auto stage_object = make_display_object(runtime, *stage_class);
  auto stage = std::dynamic_pointer_cast<DisplayObjectContainer>(stage_object);
  if (stage == nullptr) {
    return nullptr;
  }
  stage->set_stage_width(data->stage_width);
  stage->set_stage_height(data->stage_height);
  stage->set_frame_rate(data->frame_rate);

  auto runtime_movie = std::shared_ptr<SwfRuntime>(new SwfRuntime());
  runtime_movie->data_ = std::move(data);
  runtime_movie->stage_ = stage;
  runtime_movie->root_ = build_movie_root(runtime, runtime_movie->data_);
  if (runtime_movie->root_ == nullptr) {
    return nullptr;
  }
  runtime_movie->playing_ = options.start_playing;
  runtime_movie->root_->set_playing(runtime_movie->playing_);
  stage->add_child(runtime_movie->root_);
  const ExecutionResult first_frame =
      runtime_movie->root_->goto_frame(interpreter, 1, runtime_movie->playing_);
  if (!first_frame.ok()) {
    return nullptr;
  }
  runtime_movie->started_ = true;
  return runtime_movie;
}

void SwfRuntime::advance(Interpreter& interpreter, double dt_seconds) {
  if (!playing_ || root_ == nullptr || !started_) {
    return;
  }
  const double fps = data_ != nullptr && data_->frame_rate > 0.0 ? data_->frame_rate : 24.0;
  accumulator_ += dt_seconds * fps;
  while (accumulator_ >= 1.0) {
    const ExecutionResult result = root_->advance_frame(interpreter);
    if (!result.ok()) {
      playing_ = false;
      break;
    }
    accumulator_ -= 1.0;
  }
}

void SwfRuntime::set_playing(bool playing) {
  playing_ = playing;
  if (root_ != nullptr) {
    root_->set_playing(playing);
  }
}

bool SwfRuntime::playing() const { return playing_; }

}  // namespace fo4::avm2
