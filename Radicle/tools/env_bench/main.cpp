#include "env_bench/env_bench.hpp"

#include <exception>
#include <iostream>
#include <string>

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
#else
int main(int argc, char** argv) {
#endif
  if (argc < 2) {
    std::cerr << "usage: radicle_env_bench <Data dir> [plugin ...]\n";
    return 2;
  }
  const std::filesystem::path data_dir = argv[1];
  try {
    std::vector<std::filesystem::path> plugins;
    for (int i = 2; i < argc; ++i) plugins.push_back(data_dir / argv[i]);
    if (plugins.empty()) plugins = radicle_bench::default_masters(data_dir);
    if (plugins.empty()) {
      const auto name = data_dir.u8string();
      std::cerr << "no official masters found under " << std::string(name.begin(), name.end()) << "\n";
      return 1;
    }
    radicle_bench::print(std::cout, radicle_bench::run_env_bench(data_dir, plugins));
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }
  return 0;
}
