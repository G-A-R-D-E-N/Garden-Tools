#include "fo4/hkx/symbol_search.hpp"

#include "fo4/hkx/behavior_graph.hpp"
#include "fo4/hkx/layout.hpp"
#include "fo4/hkx/skeleton.hpp"
#include "hkx_read_internal.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace fo4::hkx {
namespace {

std::string lowered(std::string_view text) {
  std::string out(text);
  for (char& ch : out) {
    if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
  }
  return out;
}

class HitSink {
public:
  HitSink(std::string_view query, std::size_t limit, HkxSearchResult& out)
      : query_(lowered(query)), limit_(limit), out_(out) {}

  bool matches(std::string_view text) const { return !text.empty() && lowered(text).find(query_) != std::string::npos; }

  bool truncated() const { return out_.truncated; }

  void offer(HkxHitKind kind, std::string_view text, std::int32_t object) {
    if (matches(text)) push(kind, text, object);
  }

  void push(HkxHitKind kind, std::string_view text, std::int32_t object) {
    std::string shown(text.substr(0, kSearchTextLimit));
    if (!seen_.insert({static_cast<std::uint8_t>(kind), shown, object}).second) return;
    if (out_.hits.size() >= limit_) {
      out_.truncated = true;
      return;
    }
    out_.hits.push_back({kind, std::move(shown), object});
  }

private:
  std::string query_;
  std::size_t limit_;
  HkxSearchResult& out_;
  std::set<std::tuple<std::uint8_t, std::string, std::int32_t>> seen_;
};

bool parse_object_query(std::string_view query, std::size_t& index) {
  if (query.size() < 2 || query.front() != '#' || query.size() > 10) return false;
  std::size_t value = 0;
  for (const char ch : query.substr(1)) {
    if (ch < '0' || ch > '9') return false;
    value = value * 10 + static_cast<std::size_t>(ch - '0');
  }
  index = value;
  return true;
}

constexpr std::size_t kWalkDepth = 8;
constexpr std::size_t kWalkArrayCap = 4096;
constexpr std::size_t kWalkLeafCap = 20000;

std::size_t kind_width(HkxFieldKind kind) {
  switch (kind) {
    case HkxFieldKind::Bool:
    case HkxFieldKind::Int8:
    case HkxFieldKind::Uint8: return 1;
    case HkxFieldKind::Int16:
    case HkxFieldKind::Uint16:
    case HkxFieldKind::Half: return 2;
    case HkxFieldKind::Int32:
    case HkxFieldKind::Uint32:
    case HkxFieldKind::Real: return 4;
    case HkxFieldKind::Int64:
    case HkxFieldKind::Uint64:
    case HkxFieldKind::String: return 8;
    case HkxFieldKind::Vector4:
    case HkxFieldKind::Quaternion: return 16;
    case HkxFieldKind::QsTransform:
    case HkxFieldKind::Matrix3: return 48;
    case HkxFieldKind::Transform: return 64;
    default: return 0;
  }
}

template <typename T>
std::string number_text(const HkxSection& section, std::size_t at) {
  const T value = detail::value_at<T>(section.data, at);
  if constexpr (std::is_floating_point_v<T>) {
    char buffer[32];
    const auto written = std::to_chars(buffer, buffer + sizeof buffer, value);
    return std::string(buffer, written.ptr);
  } else {
    return std::to_string(value);
  }
}

std::string floats_text(const HkxSection& section, std::size_t at, std::size_t count) {
  std::string text = "[";
  for (std::size_t i = 0; i < count; ++i) {
    if (i != 0) text += ", ";
    text += number_text<float>(section, at + 4 * i);
  }
  return text + "]";
}

// Requires at to hold the kind's width inside the section; only the enum-capable integer kinds give a value.
std::optional<std::int64_t> integer_value(const HkxSection& section, HkxFieldKind kind, std::size_t at) {
  switch (kind) {
    case HkxFieldKind::Int8: return detail::value_at<std::int8_t>(section.data, at);
    case HkxFieldKind::Uint8: return detail::value_at<std::uint8_t>(section.data, at);
    case HkxFieldKind::Int16: return detail::value_at<std::int16_t>(section.data, at);
    case HkxFieldKind::Uint16: return detail::value_at<std::uint16_t>(section.data, at);
    case HkxFieldKind::Int32: return detail::value_at<std::int32_t>(section.data, at);
    case HkxFieldKind::Uint32: return detail::value_at<std::uint32_t>(section.data, at);
    default: return std::nullopt;
  }
}

// Requires at to hold the kind's width inside the section; a null or unreadable string gives nullopt.
std::optional<std::string> leaf_text(const HkxSection& section, HkxFieldKind kind, std::size_t at) {
  switch (kind) {
    case HkxFieldKind::Bool: return std::string(detail::value_at<std::uint8_t>(section.data, at) != 0 ? "true" : "false");
    case HkxFieldKind::Int8: return number_text<std::int8_t>(section, at);
    case HkxFieldKind::Uint8: return number_text<std::uint8_t>(section, at);
    case HkxFieldKind::Int16: return number_text<std::int16_t>(section, at);
    case HkxFieldKind::Uint16: return number_text<std::uint16_t>(section, at);
    case HkxFieldKind::Int32: return number_text<std::int32_t>(section, at);
    case HkxFieldKind::Uint32: return number_text<std::uint32_t>(section, at);
    case HkxFieldKind::Int64: return number_text<std::int64_t>(section, at);
    case HkxFieldKind::Uint64: return number_text<std::uint64_t>(section, at);
    case HkxFieldKind::Real: return number_text<float>(section, at);
    case HkxFieldKind::Vector4:
    case HkxFieldKind::Quaternion:
    case HkxFieldKind::QsTransform:
    case HkxFieldKind::Transform:
    case HkxFieldKind::Matrix3: return floats_text(section, at, kind_width(kind) / 4);
    case HkxFieldKind::String: {
      const auto target = detail::local_target(section, at);
      if (!target) return std::nullopt;
      const auto text = cstring_at(section.data, *target);
      if (!text) return std::nullopt;
      return detail::printable_text(*text);
    }
    default: return std::nullopt;
  }
}

struct WalkSlot {
  const std::string& path;
  std::string_view declaring;
  std::string_view member;
  HkxFieldKind kind;
  std::size_t at;
  bool element;
  const HkxMemberLayout* layout;
};

// Constraint: walks only members the layout types (written scalars, strings, vectors and matrices, inline structs, arrays of them); 8-bit arrays, half, relative arrays, pointers and every Other member are not read, and the walk is bounded by depth, array length and leaves per object.
template <typename Visitor>
class Walker {
 public:
  Walker(const HkxSection& section, Visitor& visitor) : section_(section), visitor_(visitor) {}

  void object(const HkxObjectView& view) {
    leaves_ = 0;
    const HkxClassLayout* layout = find_layout(view.class_name, view.signature);
    if (layout == nullptr || view.data.size() < layout->size) return;
    members(*layout, view.offset, std::string{}, 0);
  }

 private:
  void members(const HkxClassLayout& top, std::size_t base, const std::string& prefix, std::size_t depth) {
    const HkxClassLayout* layout = &top;
    for (std::size_t hop = 0; layout != nullptr && hop < kWalkDepth; ++hop) {
      for (const HkxMemberLayout& member : layout->members) {
        if (leaves_ >= kWalkLeafCap) return;
        const std::string path = prefix.empty() ? std::string(member.name) : prefix + "." + std::string(member.name);
        const std::size_t at = base + member.offset;
        switch (member.kind) {
          case HkxFieldKind::Other:
          case HkxFieldKind::RelArray: break;
          case HkxFieldKind::Struct: structure(layout->name, member, find_layout(member.type_class), at, path, depth, false); break;
          case HkxFieldKind::Array: array(layout->name, member, at, path, depth); break;
          default: scalar(layout->name, member, member.kind, at, path, false); break;
        }
      }
      layout = layout->parent.empty() ? nullptr : find_layout(layout->parent);
    }
  }

  void scalar(std::string_view declaring, const HkxMemberLayout& member, HkxFieldKind kind, std::size_t at,
              const std::string& path, bool element) {
    if (!detail::in_section(section_, at, kind_width(kind))) return;
    ++leaves_;
    visitor_.leaf(WalkSlot{path, declaring, member.name, kind, at, element, &member});
  }

  void structure(std::string_view declaring, const HkxMemberLayout& member, const HkxClassLayout* layout, std::size_t at,
                 const std::string& path, std::size_t depth, bool element) {
    if (layout == nullptr || depth >= kWalkDepth || !detail::in_section(section_, at, layout->size)) return;
    ++leaves_;
    visitor_.structure(WalkSlot{path, declaring, member.name, HkxFieldKind::Struct, at, element, &member}, member.type_class);
    members(*layout, at, path, depth + 1);
  }

  void array(std::string_view declaring, const HkxMemberLayout& member, std::size_t at, const std::string& path,
             std::size_t depth) {
    std::size_t stride = kind_width(member.element);
    const HkxClassLayout* element_layout = nullptr;
    if (member.element == HkxFieldKind::Struct) {
      element_layout = find_layout(member.type_class);
      stride = element_layout == nullptr ? 0 : element_layout->size;
    }
    if (stride == 0 || member.element == HkxFieldKind::Int8 || member.element == HkxFieldKind::Uint8) return;
    detail::Elements elements;
    if (detail::elements_at(section_, at, 8, stride, elements) != HkxStatus::Ok) return;
    const std::size_t count = std::min(elements.count, kWalkArrayCap);
    for (std::size_t i = 0; i < count && leaves_ < kWalkLeafCap; ++i) {
      const std::string element_path = path + "[" + std::to_string(i) + "]";
      const std::size_t element_at = elements.first + i * stride;
      if (member.element == HkxFieldKind::Struct) {
        structure(declaring, member, element_layout, element_at, element_path, depth, true);
      } else {
        scalar(declaring, member, member.element, element_at, element_path, true);
      }
    }
  }

  const HkxSection& section_;
  Visitor& visitor_;
  std::size_t leaves_{0};
};

bool contains_text(const std::string& text, std::string_view part) { return text.find(part) != std::string::npos; }

HkxHitKind value_hit_kind(std::string_view path) {
  const std::string text = lowered(path);
  if (contains_text(text, "event")) return HkxHitKind::Event;
  if (contains_text(text, "variable")) return HkxHitKind::Variable;
  if (contains_text(text, "animation") || contains_text(text, "behaviorfilename") || contains_text(text, "rigname")) {
    return HkxHitKind::Asset;
  }
  if (text == "name" || (text.size() > 5 && text.compare(text.size() - 5, 5, ".name") == 0)) return HkxHitKind::Name;
  return HkxHitKind::Field;
}

class ValueVisitor {
 public:
  ValueVisitor(const HkxSection& section, HitSink& sink, std::string_view class_name, std::int32_t object)
      : section_(section), sink_(sink), class_name_(class_name), object_(object) {}

  void leaf(const WalkSlot& slot) {
    const auto text = leaf_text(section_, slot.kind, slot.at);
    if (!text) return;
    std::string shown = *text;
    bool matched = sink_.matches(*text);
    if (!slot.element && slot.layout->enumeration != nullptr) {
      const auto number = integer_value(section_, slot.kind, slot.at);
      const std::string name = number ? enum_value_name(*slot.layout->enumeration, slot.layout->type.starts_with("flags"), *number)
                                      : std::string{};
      if (!name.empty()) {
        matched = matched || sink_.matches(name);
        shown = name + " (" + *text + ")";
      }
    }
    if (!matched) return;
    sink_.push(value_hit_kind(slot.path), std::string(class_name_) + "." + slot.path + " = " + shown, object_);
  }

  void structure(const WalkSlot&, std::string_view) {}

 private:
  const HkxSection& section_;
  HitSink& sink_;
  std::string_view class_name_;
  std::int32_t object_;
};

struct EventMember {
  std::string_view declaring;
  std::string_view member;
};

// Constraint: each row is a plain int32 event id member that HavokClassTypes.json marks as written; struct slots typed hkbEventProperty or hkbEvent need no row.
constexpr EventMember k_event_id_members[] = {
    {"BSDirectAtCapturePoseModifier", "capturePoseEventId"},
    {"BSDirectAtModifier", "useCurrentSourceBonePoseEventId"},
    {"BSDirectAtModifier", "snapToTargetEventId"},
    {"BSLookAtCapturePoseModifier", "capturePoseEventId"},
    {"BSLookAtModifier", "restoreDefaultRefPosEventId"},
    {"BSLookAtModifier", "snapToTargetEventId"},
    {"hkbEventDrivenModifier", "activateEventId"},
    {"hkbEventDrivenModifier", "deactivateEventId"},
    {"hkbMoveBoneAttachmentModifier", "activateEventId"},
    {"hkbSequence", "enableEventId"},
    {"hkbSequence", "disableEventId"},
    {"hkbLayer", "onEventId"},
    {"hkbLayer", "offEventId"},
    {"hkbPoseMatchingGenerator", "startPlayingEventId"},
    {"hkbPoseMatchingGenerator", "startMatchingEventId"},
    {"hkbExpressionData", "assignmentEventIndex"},
};

struct EventSlot {
  std::int32_t id;
  std::string member;
};

class EventVisitor {
 public:
  EventVisitor(const HkxSection& section, std::vector<EventSlot>& found, std::optional<std::uint32_t> event_id_offset)
      : section_(section), found_(found), event_id_offset_(event_id_offset) {}

  void leaf(const WalkSlot& slot) {
    if (slot.kind != HkxFieldKind::Int32 || slot.element) return;
    for (const EventMember& row : k_event_id_members) {
      if (row.declaring == slot.declaring && row.member == slot.member) {
        found_.push_back({detail::value_at<std::int32_t>(section_.data, slot.at), slot.path});
        return;
      }
    }
  }

  void structure(const WalkSlot& slot, std::string_view type_class) {
    if (!event_id_offset_ || (type_class != "hkbEventProperty" && type_class != "hkbEvent")) return;
    const std::size_t at = slot.at + *event_id_offset_;
    if (!detail::in_section(section_, at, 4)) return;
    found_.push_back({detail::value_at<std::int32_t>(section_.data, at), slot.path});
  }

 private:
  const HkxSection& section_;
  std::vector<EventSlot>& found_;
  std::optional<std::uint32_t> event_id_offset_;
};

struct BindingOffsets {
  std::size_t variable_index;
  std::size_t binding_type;
};

// Constraint: only a binding whose bindingType is 0 (a graph variable) names a variable slot; a character property index is another table.
class SlotVisitor {
 public:
  SlotVisitor(const HkxSection& section, std::vector<EventSlot>& events, std::vector<EventSlot>* variables,
              std::optional<std::uint32_t> event_id_offset, std::optional<BindingOffsets> binding, bool skip_bindings)
      : section_(section),
        events_(section, events, event_id_offset),
        variables_(variables),
        binding_(binding),
        skip_bindings_(skip_bindings) {}

  void leaf(const WalkSlot& slot) {
    events_.leaf(slot);
    if (variables_ == nullptr || slot.kind != HkxFieldKind::Int32 || slot.element) return;
    if (slot.declaring == "hkbExpressionData" && slot.member == "assignmentVariableIndex") {
      variables_->push_back({detail::value_at<std::int32_t>(section_.data, slot.at), slot.path});
      return;
    }
    if (skip_bindings_ || !binding_ || slot.declaring != "hkbVariableBindingSetBinding" || slot.member != "variableIndex") {
      return;
    }
    const std::size_t type_at = slot.at - binding_->variable_index + binding_->binding_type;
    if (!detail::in_section(section_, type_at, 1) || detail::value_at<std::int8_t>(section_.data, type_at) != 0) return;
    variables_->push_back({detail::value_at<std::int32_t>(section_.data, slot.at), slot.path});
  }

  void structure(const WalkSlot& slot, std::string_view type_class) { events_.structure(slot, type_class); }

 private:
  const HkxSection& section_;
  EventVisitor events_;
  std::vector<EventSlot>* variables_;
  std::optional<BindingOffsets> binding_;
  bool skip_bindings_;
};

// Invariant: the label is the object's own name member when the class chain has one, else the class and object index.
std::string object_label(const HkxSection& section, const HkxObjectView& view, std::size_t index) {
  const HkxClassLayout* layout = find_layout(view.class_name, view.signature);
  for (std::size_t hop = 0; layout != nullptr && hop < kWalkDepth; ++hop) {
    for (const HkxMemberLayout& member : layout->members) {
      const std::size_t at = view.offset + member.offset;
      if (member.kind != HkxFieldKind::String || member.name != "name" || !detail::in_section(section, at, 8)) continue;
      const auto text = leaf_text(section, HkxFieldKind::String, at);
      if (text && !text->empty()) return *text;
    }
    layout = layout->parent.empty() ? nullptr : find_layout(layout->parent);
  }
  return std::string(view.class_name) + "#" + std::to_string(index);
}

}  // namespace

const char* hkx_hit_kind_name(HkxHitKind kind) {
  switch (kind) {
    case HkxHitKind::Object: return "object";
    case HkxHitKind::Class: return "class";
    case HkxHitKind::Event: return "event";
    case HkxHitKind::Variable: return "variable";
    case HkxHitKind::Asset: return "asset";
    case HkxHitKind::Name: return "name";
    case HkxHitKind::Field: return "field";
  }
  return "name";
}

const char* hkx_event_site_name(HkxEventSite site) {
  switch (site) {
    case HkxEventSite::ClipTrigger: return "clip_trigger";
    case HkxEventSite::StateMachineChange: return "state_machine_change";
    case HkxEventSite::StateMachineReturnToPrevious: return "state_machine_return_to_previous";
    case HkxEventSite::StateMachineRandomTransition: return "state_machine_random_transition";
    case HkxEventSite::StateMachineNextHigher: return "state_machine_next_higher";
    case HkxEventSite::StateMachineNextLower: return "state_machine_next_lower";
    case HkxEventSite::Transition: return "transition";
    case HkxEventSite::TriggerEnter: return "trigger_enter";
    case HkxEventSite::TriggerExit: return "trigger_exit";
    case HkxEventSite::InitiateEnter: return "initiate_enter";
    case HkxEventSite::InitiateExit: return "initiate_exit";
    case HkxEventSite::ObjectMember: return "object_member";
  }
  return "transition";
}

HkxStatus search_symbols(const HkxPackfile& packfile, std::string_view query, std::size_t limit, HkxSearchResult& out) {
  out = HkxSearchResult{};
  if (packfile.sections.empty() || packfile.data_section >= packfile.sections.size()) return HkxStatus::MissingSection;
  while (!query.empty() && (query.front() == ' ' || query.front() == '\t')) query.remove_prefix(1);
  while (!query.empty() && (query.back() == ' ' || query.back() == '\t')) query.remove_suffix(1);
  if (query.empty()) return HkxStatus::Ok;
  HitSink sink(query, limit, out);

  std::size_t wanted = 0;
  if (parse_object_query(query, wanted) && wanted < packfile.objects.size()) {
    sink.push(HkxHitKind::Object, "#" + std::to_string(wanted) + " " + std::string(packfile.objects[wanted].class_name),
              static_cast<std::int32_t>(wanted));
  }

  std::vector<std::size_t> first_of_class;
  std::set<std::pair<std::string, std::uint32_t>> seen_classes;
  for (std::size_t at = 0; at < packfile.objects.size(); ++at) {
    const HkxObjectView& object = packfile.objects[at];
    if (seen_classes.emplace(std::string(object.class_name), static_cast<std::uint32_t>(object.signature)).second) {
      first_of_class.push_back(at);
    }
  }
  for (const std::size_t at : first_of_class) {
    sink.offer(HkxHitKind::Class, packfile.objects[at].class_name, static_cast<std::int32_t>(at));
  }

  HkxGraphModel model;
  if (read_behavior_model(packfile, model) == HkxStatus::Ok) {
    for (const HkxGraphEvent& event : model.events) sink.offer(HkxHitKind::Event, event.name, -1);
    for (const HkxGraphVariable& variable : model.variables) sink.offer(HkxHitKind::Variable, variable.name, -1);
    for (const HkxGraphClip& clip : model.clips) {
      const auto object = clip.node < model.nodes.size() ? static_cast<std::int32_t>(model.nodes[clip.node].object) : -1;
      sink.offer(HkxHitKind::Asset, clip.animation, object);
      sink.offer(HkxHitKind::Asset, clip.animation_bundle, object);
    }
    for (const HkxGraphNode& node : model.nodes) {
      sink.offer(HkxHitKind::Name, node.name, static_cast<std::int32_t>(node.object));
    }
    for (const HkxGraphState& state : model.states) {
      sink.offer(HkxHitKind::Name, state.name, static_cast<std::int32_t>(state.object));
    }
  }

  HkxSkeleton skeleton;
  if (read_skeleton(packfile, skeleton) == HkxStatus::Ok) {
    for (const std::string& bone : skeleton.bone_names) sink.offer(HkxHitKind::Name, bone, -1);
  }

  const HkxSection& data = packfile.sections[packfile.data_section];
  for (std::size_t at = 0; at < packfile.objects.size() && !sink.truncated(); ++at) {
    ValueVisitor visitor(data, sink, packfile.objects[at].class_name, static_cast<std::int32_t>(at));
    Walker<ValueVisitor>(data, visitor).object(packfile.objects[at]);
  }

  const std::string needle = lowered(query);
  for (const std::size_t at : first_of_class) {
    const HkxObjectView& object = packfile.objects[at];
    const HkxClassLayout* layout = find_layout(object.class_name, object.signature);
    if (layout == nullptr) continue;
    for (const HkxMemberLayout& member : layout->members) {
      if (lowered(member.name).find(needle) == std::string::npos) continue;
      sink.push(HkxHitKind::Field, std::string(object.class_name) + "." + std::string(member.name),
                static_cast<std::int32_t>(at));
    }
  }
  return HkxStatus::Ok;
}

void collect_symbol_usage(const HkxGraphModel& model, HkxSymbolUsage& out) {
  out = HkxSymbolUsage{};
  std::map<std::int32_t, std::size_t> stray_events;
  std::map<std::int32_t, std::size_t> stray_variables;
  for (std::size_t at = 0; at < model.events.size(); ++at) {
    out.events.push_back({static_cast<std::int32_t>(at), model.events[at].name, true, {}});
  }
  for (std::size_t at = 0; at < model.variables.size(); ++at) {
    out.variables.push_back({static_cast<std::int32_t>(at), model.variables[at].name, true, {}});
  }

  const auto node_name = [&](std::int32_t node) -> std::string {
    return node >= 0 && static_cast<std::size_t>(node) < model.nodes.size() ? model.nodes[static_cast<std::size_t>(node)].name
                                                                          : std::string{};
  };
  const auto event_row = [&](std::int32_t id) -> HkxEventUsage& {
    if (id >= 0 && static_cast<std::size_t>(id) < model.events.size()) return out.events[static_cast<std::size_t>(id)];
    const auto found = stray_events.find(id);
    if (found != stray_events.end()) return out.events[found->second];
    stray_events[id] = out.events.size();
    out.events.push_back({id, {}, false, {}});
    return out.events.back();
  };
  const auto add_event = [&](std::int32_t id, HkxEventSite site, std::int32_t node, std::int32_t state, std::string owner) {
    if (id < 0) return;
    event_row(id).sites.push_back({site, node, state, std::move(owner), -1, {}});
  };
  const auto variable_row = [&](std::int32_t index) -> HkxVariableUsage& {
    if (index >= 0 && static_cast<std::size_t>(index) < model.variables.size()) {
      return out.variables[static_cast<std::size_t>(index)];
    }
    const auto found = stray_variables.find(index);
    if (found != stray_variables.end()) return out.variables[found->second];
    stray_variables[index] = out.variables.size();
    out.variables.push_back({index, {}, false, {}});
    return out.variables.back();
  };

  for (const HkxGraphClip& clip : model.clips) {
    for (const HkxGraphClipTrigger& trigger : clip.triggers) {
      add_event(trigger.event_id, HkxEventSite::ClipTrigger, static_cast<std::int32_t>(clip.node), -1,
                node_name(static_cast<std::int32_t>(clip.node)));
    }
  }
  for (const HkxGraphStateMachine& machine : model.state_machines) {
    const auto node = static_cast<std::int32_t>(machine.node);
    const std::string owner = node_name(node);
    add_event(machine.change_event_id, HkxEventSite::StateMachineChange, node, -1, owner);
    add_event(machine.return_to_previous_event_id, HkxEventSite::StateMachineReturnToPrevious, node, -1, owner);
    add_event(machine.random_transition_event_id, HkxEventSite::StateMachineRandomTransition, node, -1, owner);
    add_event(machine.next_higher_event_id, HkxEventSite::StateMachineNextHigher, node, -1, owner);
    add_event(machine.next_lower_event_id, HkxEventSite::StateMachineNextLower, node, -1, owner);
  }
  for (const HkxGraphTransition& transition : model.transitions) {
    const std::int32_t node = transition.state_machine < model.state_machines.size()
                                  ? static_cast<std::int32_t>(model.state_machines[transition.state_machine].node)
                                  : -1;
    const std::int32_t state = transition.state;
    const std::string owner = state >= 0 && static_cast<std::size_t>(state) < model.states.size()
                                  ? model.states[static_cast<std::size_t>(state)].name
                                  : node_name(node);
    add_event(transition.event_id, HkxEventSite::Transition, node, state, owner);
    add_event(transition.trigger_interval.enter_event_id, HkxEventSite::TriggerEnter, node, state, owner);
    add_event(transition.trigger_interval.exit_event_id, HkxEventSite::TriggerExit, node, state, owner);
    add_event(transition.initiate_interval.enter_event_id, HkxEventSite::InitiateEnter, node, state, owner);
    add_event(transition.initiate_interval.exit_event_id, HkxEventSite::InitiateExit, node, state, owner);
  }

  for (const HkxGraphBinding& binding : model.bindings) {
    if (binding.variable_index < 0) continue;
    variable_row(binding.variable_index).sites.push_back({binding.owner_kind, binding.owner_index, binding.member_path});
  }
  for (std::size_t at = 0; at < model.state_machines.size(); ++at) {
    const std::int32_t sync = model.state_machines[at].sync_variable_index;
    if (sync < 0) continue;
    variable_row(sync).sites.push_back({HkxGraphOwnerKind::StateMachine, at, "syncVariableIndex"});
  }

  for (std::size_t at = 0; at < model.states.size(); ++at) {
    const HkxGraphState& state = model.states[at];
    HkxStateUsage row;
    row.state = at;
    row.name = state.name;
    row.state_id = state.state_id;
    row.state_machine = state.state_machine;
    row.outgoing = state.transition_count;
    row.is_start = state.state_machine < model.state_machines.size() &&
                   model.state_machines[state.state_machine].start_state_id == state.state_id;
    out.states.push_back(std::move(row));
  }
  for (const HkxGraphTransition& transition : model.transitions) {
    if (transition.state_machine >= model.state_machines.size()) continue;
    const HkxGraphStateMachine& machine = model.state_machines[transition.state_machine];
    const std::size_t end = std::min(machine.first_state + machine.state_count, model.states.size());
    for (std::size_t at = machine.first_state; at < end; ++at) {
      if (model.states[at].state_id != transition.to_state_id) continue;
      ++out.states[at].incoming;
      break;
    }
  }
}

void collect_object_slots(const HkxPackfile& packfile, std::span<const std::size_t> decoded_binding_sets,
                          std::vector<HkxGraphObjectSlot>& events, std::vector<HkxGraphObjectSlot>* variables) {
  if (packfile.sections.empty() || packfile.data_section >= packfile.sections.size()) return;
  const HkxSection& data = packfile.sections[packfile.data_section];
  std::optional<std::uint32_t> event_id_offset;
  if (const HkxClassLayout* base = find_layout("hkbEventBase")) event_id_offset = find_member_offset(*base, "id");
  std::optional<BindingOffsets> binding;
  if (const HkxClassLayout* row = find_layout("hkbVariableBindingSetBinding")) {
    const auto index = find_member_offset(*row, "variableIndex");
    const auto type = find_member_offset(*row, "bindingType");
    if (index && type) binding = BindingOffsets{*index, *type};
  }
  const std::set<std::size_t> decoded(decoded_binding_sets.begin(), decoded_binding_sets.end());

  for (std::size_t at = 0; at < packfile.objects.size(); ++at) {
    const HkxObjectView& view = packfile.objects[at];
    if (view.class_name == "hkbStateMachine" || view.class_name == "hkbClipTriggerArray") continue;
    std::vector<EventSlot> found_events;
    std::vector<EventSlot> found_variables;
    SlotVisitor visitor(data, found_events, variables != nullptr ? &found_variables : nullptr, event_id_offset, binding,
                        decoded.contains(at));
    Walker<SlotVisitor>(data, visitor).object(view);
    const std::string prefix = std::string(view.class_name) + ".";
    for (const EventSlot& slot : found_events) {
      if (slot.id >= 0) events.push_back({at, prefix + slot.member, slot.id});
    }
    for (const EventSlot& slot : found_variables) {
      if (slot.id >= 0) variables->push_back({at, prefix + slot.member, slot.id});
    }
  }
}

void collect_object_event_sites(const HkxPackfile& packfile, HkxSymbolUsage& usage) {
  if (packfile.sections.empty() || packfile.data_section >= packfile.sections.size()) return;
  const HkxSection& data = packfile.sections[packfile.data_section];
  std::vector<HkxGraphObjectSlot> slots;
  collect_object_slots(packfile, {}, slots, nullptr);

  std::map<std::int32_t, std::size_t> rows;
  for (std::size_t at = 0; at < usage.events.size(); ++at) rows[usage.events[at].event_id] = at;

  for (const HkxGraphObjectSlot& slot : slots) {
    auto row = rows.find(slot.value);
    if (row == rows.end()) {
      row = rows.emplace(slot.value, usage.events.size()).first;
      usage.events.push_back({slot.value, {}, false, {}});
    }
    usage.events[row->second].sites.push_back({HkxEventSite::ObjectMember, -1, -1,
                                               object_label(data, packfile.objects[slot.object], slot.object),
                                               static_cast<std::int32_t>(slot.object), slot.member});
  }
}

}  // namespace fo4::hkx
