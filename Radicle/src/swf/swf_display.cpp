#include "fo4/swf/swf_display.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::swf {

namespace {

struct Operation {
  std::size_t tag_index{0};
  bool remove{false};
  std::uint16_t depth{0};
  bool move{false};
  std::optional<std::uint16_t> character_id;
  std::optional<SwfTransform> transform;
  std::optional<std::string> name;
  std::optional<std::uint16_t> clip_depth;
  std::optional<SwfColorTransform> color_transform;
};

struct LocalInstance {
  std::uint16_t depth{0};
  std::uint16_t character_id{0};
  std::uint32_t created_frame{0};
  std::uint32_t serial{0};
  std::size_t create_tag_index{0};
  std::optional<std::size_t> transform_tag_index;
  std::string name;
  SwfTransform transform{};
  std::optional<std::uint16_t> clip_depth;
  std::optional<SwfColorTransform> color_transform;
};

using LocalState = std::map<std::uint16_t, LocalInstance>;

struct LocalTimeline {
  std::uint16_t character_id{0};
  bool is_sprite{false};
  std::size_t sprite_tag_index{0};
  std::vector<LocalState> frames;
};

bool collect_operations(const std::vector<SwfTag>& tags, std::vector<std::vector<Operation>>& frames,
                        std::string& message) {
  std::vector<Operation> current;
  for (std::size_t index = 0; index < tags.size(); ++index) {
    const SwfTag& tag = tags[index];
    if (tag.code == tag::kShowFrame) {
      frames.push_back(std::move(current));
      current.clear();
      continue;
    }
    if (tag.code == tag::kRemoveObject || tag.code == tag::kRemoveObject2) {
      if (!tag.removal) {
        message = "removal payload did not decode";
        return false;
      }
      Operation operation;
      operation.tag_index = index;
      operation.remove = true;
      operation.depth = tag.removal->depth;
      current.push_back(std::move(operation));
      continue;
    }
    if (tag.code != tag::kPlaceObject && tag.code != tag::kPlaceObject2 &&
        tag.code != tag::kPlaceObject3) {
      continue;
    }
    if (!tag.placement) {
      message = "placement payload did not decode";
      return false;
    }
    const SwfPlacement& placement = *tag.placement;
    if (placement.class_name && !placement.character_id) {
      continue;
    }
    Operation operation;
    operation.tag_index = index;
    operation.depth = placement.depth;
    operation.move = placement.move;
    operation.character_id = placement.character_id;
    if (placement.matrix) {
      operation.transform = transform_from_matrix(*placement.matrix);
    }
    if (placement.name) {
      operation.name = *placement.name;
    }
    operation.clip_depth = placement.clip_depth;
    operation.color_transform = placement.color_transform;
    current.push_back(std::move(operation));
  }
  if (!current.empty()) {
    message = "timeline ends with operations after its last frame";
    return false;
  }
  if (frames.empty()) {
    frames.emplace_back();
  }
  return true;
}

bool replay(const std::vector<std::vector<Operation>>& operations, std::uint16_t character_id,
            LocalTimeline& out, std::string& message) {
  LocalState active;
  std::map<std::uint16_t, std::uint32_t> serials;
  out.character_id = character_id;
  out.frames.reserve(operations.size());
  for (std::size_t frame = 0; frame < operations.size(); ++frame) {
    for (const Operation& operation : operations[frame]) {
      if (operation.remove) {
        active.erase(operation.depth);
        continue;
      }
      const auto existing = active.find(operation.depth);
      if (operation.move && existing == active.end()) {
        message = "move refers to a depth that is not live";
        return false;
      }
      if (!operation.character_id) {
        if (existing == active.end()) {
          message = "a placement without a character has no live depth";
          return false;
        }
        LocalInstance& target = existing->second;
        if (operation.transform) {
          target.transform = *operation.transform;
          target.transform_tag_index = operation.tag_index;
        }
        if (operation.name) {
          target.name = *operation.name;
        }
        if (operation.clip_depth) {
          target.clip_depth = operation.clip_depth;
        }
        if (operation.color_transform) {
          target.color_transform = operation.color_transform;
        }
        continue;
      }
      LocalInstance instance;
      instance.depth = operation.depth;
      instance.character_id = *operation.character_id;
      instance.created_frame = static_cast<std::uint32_t>(frame);
      instance.serial = serials[operation.depth]++;
      instance.create_tag_index = operation.tag_index;
      if (operation.move && existing != active.end()) {
        instance.name = existing->second.name;
        instance.transform = existing->second.transform;
        instance.clip_depth = existing->second.clip_depth;
        instance.color_transform = existing->second.color_transform;
        instance.transform_tag_index = existing->second.transform_tag_index;
      }
      if (operation.transform) {
        instance.transform = *operation.transform;
        instance.transform_tag_index = operation.tag_index;
      }
      if (operation.name) {
        instance.name = *operation.name;
      }
      if (operation.clip_depth) {
        instance.clip_depth = operation.clip_depth;
      }
      if (operation.color_transform) {
        instance.color_transform = operation.color_transform;
      }
      active[operation.depth] = std::move(instance);
    }
    out.frames.push_back(active);
  }
  return true;
}

constexpr std::int64_t kColorOne = 256;
constexpr std::int64_t kColorLimit = 65536;

std::int32_t clamp_color(std::int64_t value) {
  return static_cast<std::int32_t>(std::clamp(value, -kColorLimit, kColorLimit));
}

std::optional<SwfColorTransform> compose_color(const std::optional<SwfColorTransform>& parent,
                                               const std::optional<SwfColorTransform>& child) {
  if (!parent) {
    return child;
  }
  if (!child) {
    return parent;
  }
  SwfColorTransform out;
  out.has_alpha = parent->has_alpha || child->has_alpha;
  for (std::size_t channel = 0; channel < 4; ++channel) {
    const std::int64_t parent_multiply = parent->multiply[channel];
    out.multiply[channel] = clamp_color(parent_multiply * child->multiply[channel] / kColorOne);
    out.add[channel] = clamp_color(parent_multiply * child->add[channel] / kColorOne +
                                   parent->add[channel]);
  }
  return out;
}

class Expander {
public:
  Expander(const std::map<std::uint16_t, LocalTimeline>& sprites,
           const std::map<std::uint16_t, SwfRect>& shape_bounds, const SwfDisplayOptions& options,
           std::vector<SwfInstancePlacement>* placements)
      : sprites_(sprites), shape_bounds_(shape_bounds), options_(options),
        placements_(placements) {}

  void reset_budget() noexcept { budget_ = 0; }

  void collect_placements(std::vector<SwfInstancePlacement>* placements) noexcept {
    placements_ = placements;
  }

  const std::string& message() const noexcept { return message_; }

  bool expand(const LocalTimeline& timeline, std::uint32_t age, const SwfTransform& parent,
              const std::optional<SwfColorTransform>& parent_color, std::string_view parent_path,
              std::string_view parent_key, std::vector<std::uint16_t>& stack, SwfDisplayFrame& out) {
    const std::size_t count = timeline.frames.size();
    const std::uint32_t index = count <= 1 ? 0 : age % static_cast<std::uint32_t>(count);
    const std::uint32_t cycle_start = count <= 1 ? 0 : age - index;
    for (const auto& entry : timeline.frames[index]) {
      if (budget_ >= options_.max_instances) {
        message_ = "the display list exceeds its instance budget";
        return false;
      }
      ++budget_;
      const LocalInstance& local = entry.second;
      const SwfTransform world = compose_transform(parent, local.transform);
      const std::optional<SwfColorTransform> world_color =
          compose_color(parent_color, local.color_transform);
      const std::string segment =
          local.name.empty() ? "#" + std::to_string(local.character_id) : local.name;
      const std::string path = std::string(parent_path) + segment;
      const std::string key = std::string(parent_key) + "/d" + std::to_string(local.depth) + "c" +
                              std::to_string(local.serial);
      SwfFrameInstance instance;
      instance.instance_id = id_for(key);
      instance.depth = local.depth;
      instance.character_id = local.character_id;
      instance.timeline_id = timeline.character_id;
      instance.timeline_frame = index;
      instance.name = local.name;
      instance.path = path;
      instance.transform = local.transform;
      instance.world_transform = world;
      instance.clip_depth = local.clip_depth;
      instance.world_color_transform = world_color;
      instance.create_tag_index = local.create_tag_index;
      instance.transform_tag_index = local.transform_tag_index;
      instance.is_sprite = sprites_.find(local.character_id) != sprites_.end();
      const auto bounds = shape_bounds_.find(local.character_id);
      if (bounds != shape_bounds_.end()) {
        instance.has_bounds = true;
        instance.bounds = transformed_bounds(world, bounds->second);
      }
      out.instances.push_back(std::move(instance));
      if (placements_ != nullptr) {
        SwfInstancePlacement placement;
        placement.instance_id = out.instances.back().instance_id;
        placement.frame = out.frame;
        placement.path = path;
        placement.inside_sprite = timeline.is_sprite;
        placement.sprite_id = timeline.character_id;
        placement.sprite_tag_index = timeline.sprite_tag_index;
        placement.has_authored_matrix = local.transform_tag_index.has_value();
        placement.transform_tag_index = local.transform_tag_index.value_or(0);
        placement.create_tag_index = local.create_tag_index;
        placements_->push_back(std::move(placement));
      }
      const auto child = sprites_.find(local.character_id);
      if (child == sprites_.end()) {
        continue;
      }
      if (stack.size() >= options_.max_depth) {
        continue;
      }
      if (std::find(stack.begin(), stack.end(), local.character_id) != stack.end()) {
        continue;
      }
      const std::uint32_t born = cycle_start + local.created_frame;
      const std::uint32_t child_age = age > born ? age - born : 0;
      stack.push_back(local.character_id);
      const bool ok =
          expand(child->second, child_age, world, world_color, path + "/", key, stack, out);
      stack.pop_back();
      if (!ok) {
        return false;
      }
    }
    return true;
  }

private:
  std::uint32_t id_for(const std::string& key) {
    const auto found = ids_.find(key);
    if (found != ids_.end()) {
      return found->second;
    }
    const std::uint32_t id = next_id_++;
    ids_.emplace(key, id);
    return id;
  }

  const std::map<std::uint16_t, LocalTimeline>& sprites_;
  const std::map<std::uint16_t, SwfRect>& shape_bounds_;
  const SwfDisplayOptions& options_;
  std::vector<SwfInstancePlacement>* placements_{nullptr};
  std::map<std::string, std::uint32_t> ids_;
  std::uint32_t next_id_{1};
  std::size_t budget_{0};
  std::string message_;
};

}  // namespace

SwfDisplayList build_display_list(const SwfFile& file, const SwfDisplayOptions& options) {
  SwfDisplayList out;
  std::map<std::uint16_t, SwfRect> shape_bounds;
  std::map<std::uint16_t, std::vector<std::vector<Operation>>> sprite_operations;
  std::map<std::uint16_t, std::size_t> sprite_tag_indices;
  for (std::size_t tag_index = 0; tag_index < file.tags.size(); ++tag_index) {
    const SwfTag& tag = file.tags[tag_index];
    if (tag.shape) {
      shape_bounds.emplace(tag.shape->shape_id, tag.shape->bounds);
    }
    if (tag.code != tag::kDefineSprite || !tag.sprite_id) {
      continue;
    }
    std::vector<std::vector<Operation>> frames;
    if (!collect_operations(tag.children, frames, out.message)) {
      return out;
    }
    if (!sprite_operations.emplace(*tag.sprite_id, std::move(frames)).second) {
      out.message = "duplicate sprite definition";
      return out;
    }
    sprite_tag_indices.emplace(*tag.sprite_id, tag_index);
  }

  std::vector<std::vector<Operation>> root_operations;
  if (!collect_operations(file.tags, root_operations, out.message)) {
    return out;
  }
  LocalTimeline root;
  if (!replay(root_operations, 0, root, out.message)) {
    return out;
  }
  std::map<std::uint16_t, LocalTimeline> sprites;
  for (const auto& entry : sprite_operations) {
    LocalTimeline timeline;
    if (!replay(entry.second, entry.first, timeline, out.message)) {
      return out;
    }
    timeline.is_sprite = true;
    timeline.sprite_tag_index = sprite_tag_indices.at(entry.first);
    sprites.emplace(entry.first, std::move(timeline));
  }

  Expander expander(sprites, shape_bounds, options,
                    options.record_placements ? &out.placements : nullptr);
  SwfDisplayTimeline root_display;
  root_display.character_id = 0;
  for (std::uint32_t frame = 0; frame < root.frames.size(); ++frame) {
    expander.reset_budget();
    SwfDisplayFrame state;
    state.frame = frame;
    std::vector<std::uint16_t> stack;
    if (!expander.expand(root, frame, SwfTransform{}, std::nullopt, std::string{}, "r", stack,
                         state)) {
      out.message = expander.message();
      return out;
    }
    root_display.frames.push_back(std::move(state));
  }
  out.timelines.push_back(std::move(root_display));

  // Invariant: only the root traversal records placements.
  expander.collect_placements(nullptr);
  for (const auto& entry : sprites) {
    SwfDisplayTimeline display;
    display.character_id = entry.first;
    const std::string parent_key = "s" + std::to_string(entry.first);
    for (std::uint32_t frame = 0; frame < entry.second.frames.size(); ++frame) {
      expander.reset_budget();
      SwfDisplayFrame state;
      state.frame = frame;
      std::vector<std::uint16_t> stack{entry.first};
      if (!expander.expand(entry.second, frame, SwfTransform{}, std::nullopt, std::string{},
                           parent_key, stack, state)) {
        out.message = expander.message();
        return out;
      }
      display.frames.push_back(std::move(state));
    }
    out.timelines.push_back(std::move(display));
  }
  out.status = SwfTimelineStatus::ok;
  return out;
}

}  // namespace fo4::swf
