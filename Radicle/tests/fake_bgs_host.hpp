#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace fake_bgs {

struct Call {
  std::string method;
  std::vector<std::string> arguments;
};

class FakeBgsHost {
 public:
  std::optional<std::string> host_call(std::string_view method,
                                       std::vector<std::string> arguments) {
    calls_.push_back(Call{std::string{method}, arguments});
    if (method.substr(0, 4) != "bgs.") return std::nullopt;
    const std::string_view name = method.substr(4);
    if (name == "getHighscore") {
      const auto found = highscores_.find(argument(arguments, 0));
      if (found == highscores_.end()) return std::nullopt;
      return found->second;
    }
    if (name == "setHighscore") {
      store_if_greater(argument(arguments, 0), argument(arguments, 1));
      return std::nullopt;
    }
    if (name == "getSaveData") {
      const auto found = save_data_.find(argument(arguments, 0));
      if (found == save_data_.end()) return std::nullopt;
      return found->second;
    }
    if (name == "setSaveData") {
      save_data_[argument(arguments, 0)] = argument(arguments, 1);
      return std::nullopt;
    }
    if (name == "registerSound") sounds_.insert(argument(arguments, 0));
    return std::nullopt;
  }

  const std::vector<Call>& calls() const noexcept { return calls_; }
  const std::map<std::string, std::string>& highscores() const noexcept { return highscores_; }
  const std::map<std::string, std::string>& save_data() const noexcept { return save_data_; }
  const std::set<std::string>& sounds() const noexcept { return sounds_; }

  std::uint64_t log_hash() const {
    std::uint64_t hash = 14695981039346656037ULL;
    const auto mix = [&hash](std::uint8_t byte) {
      hash ^= byte;
      hash *= 1099511628211ULL;
    };
    const auto mix_text = [&mix](std::string_view text) {
      for (const char character : text) mix(static_cast<std::uint8_t>(character));
      mix(0xFF);
    };
    for (const Call& call : calls_) {
      mix_text(call.method);
      for (const std::string& value : call.arguments) mix_text(value);
      mix(0xFE);
    }
    return hash;
  }

 private:
  static std::string argument(const std::vector<std::string>& arguments, std::size_t index) {
    return index < arguments.size() ? arguments[index] : std::string{};
  }

  void store_if_greater(const std::string& key, const std::string& score) {
    const auto found = highscores_.find(key);
    if (found == highscores_.end() ||
        std::strtod(score.c_str(), nullptr) > std::strtod(found->second.c_str(), nullptr)) {
      highscores_[key] = score;
    }
  }

  std::vector<Call> calls_;
  std::map<std::string, std::string> highscores_;
  std::map<std::string, std::string> save_data_;
  std::set<std::string> sounds_;
};

}  // namespace fake_bgs
