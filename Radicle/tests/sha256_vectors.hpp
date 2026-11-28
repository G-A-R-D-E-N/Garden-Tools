#pragma once

#include <array>
#include <string>
#include <string_view>

namespace fo4::test::sha256 {

struct Vector {
  std::string_view label;
  std::string input;
  std::string_view digest;
};

// Invariant: 55, 56 and 64 bytes straddle the one-block, two-block and exact-block padding edges.
inline std::array<Vector, 5> known_answers() {
  return {{
      {"empty", "", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
      {"abc", "abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
      {"55 bytes", std::string(55, 'a'), "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"},
      {"56 bytes", std::string(56, 'a'), "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a"},
      {"64 bytes", std::string(64, 'a'), "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"},
  }};
}

}  // namespace fo4::test::sha256
