#include "object_ops.hpp"

#include "text_ops.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace fo4::avm1 {

namespace {

using text_ops::to_text;

constexpr const char* kPrimitiveMembers =
    "a member of a number, string or boolean is not in this slice";

std::optional<std::uint32_t> array_index(const std::string& key) {
  if (key.empty() || key.size() > 10 || (key.size() > 1 && key.front() == '0')) {
    return std::nullopt;
  }
  std::uint64_t value = 0;
  for (const char digit : key) {
    if (digit < '0' || digit > '9') {
      return std::nullopt;
    }
    value = value * 10 + static_cast<std::uint64_t>(digit - '0');
  }
  if (value > 4294967294u) {
    return std::nullopt;
  }
  return static_cast<std::uint32_t>(value);
}

void put(Avm1Object& object, std::string key, Avm1Data value) {
  if (object.is_array) {
    if (const std::optional<std::uint32_t> index = array_index(key)) {
      Avm1Data& length = object.properties.at("length");
      length.number = std::max(length.number, static_cast<double>(*index) + 1.0);
    }
  }
  if (!object.properties.contains(key)) {
    object.order.push_back(key);
  }
  object.properties[key] = std::move(value);
}

bool erase_property(Avm1Object& object, const std::string& key) {
  if (object.is_array && key == "length") {
    return false;
  }
  if (object.properties.erase(key) == 0) {
    return false;
  }
  object.order.erase(std::find(object.order.begin(), object.order.end(), key));
  return true;
}

Avm1Data make_object(bool is_array) {
  Avm1Data data{.kind = Avm1DataKind::Object, .object = std::make_shared<Avm1Object>()};
  data.object->is_array = is_array;
  if (is_array) {
    data.object->properties.emplace("length",
                                    Avm1Data{.kind = Avm1DataKind::Number, .number = 0.0});
  }
  return data;
}

Avm1RunStatus pop_count(Avm1State& state, std::size_t slots, std::size_t& count,
                        std::string& message) {
  Avm1Data operand;
  if (const Avm1RunStatus status = pop(state, operand, message); status != Avm1RunStatus::Ok) {
    return status;
  }
  const double limit = static_cast<double>(state.stack.size() / slots);
  if (operand.kind != Avm1DataKind::Number || !(operand.number >= 0.0 && operand.number <= limit) ||
      std::floor(operand.number) != operand.number) {
    message = "the element count is not a whole number the operand stack holds";
    return Avm1RunStatus::StackUnderflow;
  }
  count = static_cast<std::size_t>(operand.number);
  return Avm1RunStatus::Ok;
}

}  // namespace

Avm1Object::~Avm1Object() {
  std::vector<std::shared_ptr<Avm1Object>> pending;
  const auto take_children = [&pending](Avm1Object& object) {
    for (auto& entry : object.properties) {
      std::shared_ptr<Avm1Object>& child = entry.second.object;
      if (child && child.use_count() == 1) {
        pending.push_back(std::move(child));
      }
    }
  };
  take_children(*this);
  while (!pending.empty()) {
    std::shared_ptr<Avm1Object> node = std::move(pending.back());
    pending.pop_back();
    take_children(*node);
  }
}

namespace object_ops {

Avm1RunStatus new_object(Avm1State& state, std::string& message) {
  Avm1Data name;
  Avm1Data count;
  if (const Avm1RunStatus status = pop(state, name, message); status != Avm1RunStatus::Ok) {
    return status;
  }
  if (const Avm1RunStatus status = pop(state, count, message); status != Avm1RunStatus::Ok) {
    return status;
  }
  if (count.kind != Avm1DataKind::Number || count.number != 0.0 ||
      to_text(name, state.swf_version) != "Object") {
    message = "only new Object with no arguments is in this slice";
    return Avm1RunStatus::Unsupported;
  }
  state.stack.push_back(make_object(false));
  return Avm1RunStatus::Ok;
}

Avm1RunStatus init_array(Avm1State& state, std::string& message) {
  std::size_t count = 0;
  if (const Avm1RunStatus status = pop_count(state, 1, count, message);
      status != Avm1RunStatus::Ok) {
    return status;
  }
  Avm1Data array = make_object(true);
  const std::size_t top = state.stack.size();
  const std::size_t first = top - count;
  for (std::size_t index = 0; index < count; ++index) {
    put(*array.object, std::to_string(index), std::move(state.stack[top - 1 - index]));
  }
  state.stack.resize(first);
  state.stack.push_back(std::move(array));
  return Avm1RunStatus::Ok;
}

Avm1RunStatus init_object(Avm1State& state, std::string& message) {
  std::size_t count = 0;
  if (const Avm1RunStatus status = pop_count(state, 2, count, message);
      status != Avm1RunStatus::Ok) {
    return status;
  }
  Avm1Data object = make_object(false);
  const std::size_t first = state.stack.size() - 2 * count;
  for (std::size_t index = 0; index < count; ++index) {
    const std::size_t name = first + 2 * index;
    put(*object.object, to_text(state.stack[name], state.swf_version),
        std::move(state.stack[name + 1]));
  }
  state.stack.resize(first);
  state.stack.push_back(std::move(object));
  return Avm1RunStatus::Ok;
}

Avm1RunStatus get_member(Avm1State& state, std::string& message) {
  Avm1Data name;
  Avm1Data base;
  if (const Avm1RunStatus status = pop(state, name, message); status != Avm1RunStatus::Ok) {
    return status;
  }
  if (const Avm1RunStatus status = pop(state, base, message); status != Avm1RunStatus::Ok) {
    return status;
  }
  if (base.kind == Avm1DataKind::Object) {
    const auto& properties = base.object->properties;
    const auto found = properties.find(to_text(name, state.swf_version));
    state.stack.push_back(found == properties.end() ? Avm1Data{} : found->second);
    return Avm1RunStatus::Ok;
  }
  if (base.kind == Avm1DataKind::Undefined || base.kind == Avm1DataKind::Null) {
    state.stack.push_back(Avm1Data{});
    return Avm1RunStatus::Ok;
  }
  message = kPrimitiveMembers;
  return Avm1RunStatus::Unsupported;
}

Avm1RunStatus set_member(Avm1State& state, std::string& message) {
  Avm1Data value;
  Avm1Data name;
  Avm1Data base;
  if (const Avm1RunStatus status = pop(state, value, message); status != Avm1RunStatus::Ok) {
    return status;
  }
  if (const Avm1RunStatus status = pop(state, name, message); status != Avm1RunStatus::Ok) {
    return status;
  }
  if (const Avm1RunStatus status = pop(state, base, message); status != Avm1RunStatus::Ok) {
    return status;
  }
  if (base.kind == Avm1DataKind::Object) {
    std::string key = to_text(name, state.swf_version);
    if (base.object->is_array && key == "length") {
      message = "setting the length of an array is not in this slice";
      return Avm1RunStatus::Unsupported;
    }
    put(*base.object, std::move(key), std::move(value));
    return Avm1RunStatus::Ok;
  }
  if (base.kind == Avm1DataKind::Undefined || base.kind == Avm1DataKind::Null) {
    return Avm1RunStatus::Ok;
  }
  message = kPrimitiveMembers;
  return Avm1RunStatus::Unsupported;
}

Avm1RunStatus delete_member(Avm1State& state, std::string& message) {
  Avm1Data name;
  Avm1Data base;
  if (const Avm1RunStatus status = pop(state, name, message); status != Avm1RunStatus::Ok) {
    return status;
  }
  if (const Avm1RunStatus status = pop(state, base, message); status != Avm1RunStatus::Ok) {
    return status;
  }
  const bool removed = base.kind == Avm1DataKind::Object &&
                       erase_property(*base.object, to_text(name, state.swf_version));
  state.stack.push_back(Avm1Data{.kind = Avm1DataKind::Boolean, .boolean = removed});
  return Avm1RunStatus::Ok;
}

Avm1RunStatus delete_variable(Avm1State& state, std::string& message) {
  Avm1Data name;
  if (const Avm1RunStatus status = pop(state, name, message); status != Avm1RunStatus::Ok) {
    return status;
  }
  const bool removed = state.variables.erase(to_text(name, state.swf_version)) > 0;
  state.stack.push_back(Avm1Data{.kind = Avm1DataKind::Boolean, .boolean = removed});
  return Avm1RunStatus::Ok;
}

}  // namespace object_ops

}  // namespace fo4::avm1
