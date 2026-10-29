#include <fo4/avm1/avm1.hpp>

#include <algorithm>
#include <cstddef>
#include <utility>
#include <variant>
#include <vector>

namespace fo4::avm1 {

namespace {

std::size_t body_count(const Avm1Action& action) {
  if (std::holds_alternative<Avm1Function>(action.payload) ||
      std::holds_alternative<Avm1With>(action.payload)) {
    return 1;
  }
  const auto* try_block = std::get_if<Avm1Try>(&action.payload);
  if (try_block == nullptr) {
    return 0;
  }
  std::size_t count = 1;
  if ((try_block->flags & 1) != 0) {
    ++count;
  }
  if ((try_block->flags & 2) != 0) {
    ++count;
  }
  return count;
}

std::size_t record_span(const Avm1Action& action) {
  std::size_t total = 1;
  for (std::size_t slot = 0; slot < body_count(action); ++slot) {
    total += action.body_records[slot];
  }
  return total;
}

void resize_enclosing(std::vector<Avm1Action>& actions, std::size_t index, std::size_t records,
                      bool grow) {
  for (std::size_t owner = 0; owner < index; ++owner) {
    std::size_t first = owner + 1;
    for (std::size_t slot = 0; slot < body_count(actions[owner]); ++slot) {
      const std::size_t count = actions[owner].body_records[slot];
      if (first <= index && index < first + count) {
        actions[owner].body_records[slot] =
            grow ? count + records : count - std::min(count, records);
      }
      first += count;
    }
  }
}

}  // namespace

bool insert_action(std::vector<Avm1Action>& actions, std::size_t index, Avm1Action action) {
  if (index > actions.size()) {
    return false;
  }
  resize_enclosing(actions, index, 1, true);
  actions.insert(actions.begin() + static_cast<std::ptrdiff_t>(index), std::move(action));
  return true;
}

bool erase_action(std::vector<Avm1Action>& actions, std::size_t index) {
  if (index >= actions.size()) {
    return false;
  }
  const std::size_t count = std::min(record_span(actions[index]), actions.size() - index);
  resize_enclosing(actions, index, count, false);
  const auto first = actions.begin() + static_cast<std::ptrdiff_t>(index);
  actions.erase(first, first + static_cast<std::ptrdiff_t>(count));
  return true;
}

}  // namespace fo4::avm1
