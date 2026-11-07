#include "fo4/archive/ba2_reader.hpp"
#include "fo4/io/atomic_write.hpp"
#include "fo4/io/sha256.hpp"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {

bool is_oc_entry(std::string_view path) {
  if (path.size() < 7) return false;
  const auto lower = [&path](std::size_t i) {
    const char ch = path[i];
    return static_cast<char>(ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch);
  };
  const bool oc_suffix =
      (lower(path.size() - 7) == '_' && lower(path.size() - 6) == 'o' && lower(path.size() - 5) == 'c' &&
       lower(path.size() - 4) == '.' && lower(path.size() - 3) == 'n' && lower(path.size() - 2) == 'i' &&
       lower(path.size() - 1) == 'f');
  if (!oc_suffix) return false;
  return path.find("PreCombined") != std::string_view::npos || path.find("precombined") != std::string_view::npos;
}

std::string manifest_path(std::string_view archive_path) {
  std::string out(archive_path);
  for (auto& ch : out) {
    if (ch == '\\') ch = '/';
  }
  return out;
}

fs::path flat_name(std::string_view archive_path) {
  for (auto ch : archive_path) {
    if (ch == '/' || ch == '\\') continue;
    if (ch == ':') return fs::path("flat_invalid_path");
  }
  return fs::path(archive_path).filename();
}

void usage() {
  std::cerr << "usage: previs_oracle_extract <DataDir> <OutDir> [--limit N]\n";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    usage();
    return 1;
  }
  const fs::path data_dir = argv[1];
  const fs::path out_dir = argv[2];
  std::size_t limit = 5;
  for (int i = 3; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--limit" && i + 1 < argc) {
      limit = static_cast<std::size_t>(std::strtoul(argv[++i], nullptr, 10));
    }
  }

  nlohmann::json manifest;
  manifest["files"] = nlohmann::json::array();
  const auto exe = data_dir.parent_path() / "Fallout4.exe";
  if (fs::exists(exe)) {
    manifest["build"] = "fo4-exe-present";
  }

  std::size_t captured = 0;
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(data_dir, ec)) {
    if (ec || !entry.is_regular_file()) continue;
    if (entry.path().extension() != ".ba2") continue;
    fo4::archive::Ba2Reader reader;
    try {
      reader = fo4::archive::Ba2Reader::open(entry.path());
    } catch (const std::exception& error) {
      std::cerr << "skip " << entry.path().filename() << ": " << error.what() << '\n';
      continue;
    }
    if (reader.kind() != fo4::archive::Ba2Kind::general) continue;

    for (std::size_t index = 0; index < reader.entries().size() && captured < limit; ++index) {
      const auto& info = reader.entries()[index];
      if (!is_oc_entry(info.path)) continue;
      std::vector<std::byte> bytes;
      try {
        bytes = reader.extract(index);
      } catch (const std::exception& error) {
        std::cerr << "extract failed " << info.path << ": " << error.what() << '\n';
        continue;
      }
      if (bytes.empty()) continue;

      const auto rel = manifest_path(info.path);
      const auto dest = out_dir / flat_name(info.path);
      fs::create_directories(out_dir, ec);
      const auto hash = fo4::io::sha256_hex(bytes);
      try {
        fo4::io::write_file_atomic(dest, bytes);
      } catch (const std::exception& error) {
        std::cerr << "write failed " << dest << ": " << error.what() << '\n';
        continue;
      }

      manifest["files"].push_back({{"path", rel},
                                   {"sha256", hash},
                                   {"bytes", bytes.size()},
                                   {"archive", entry.path().filename().string()}});
      ++captured;
      std::cout << "captured " << rel << '\n';
    }
    if (captured >= limit) break;
  }

  const auto manifest_path = out_dir / "manifest.json";
  const auto manifest_text = manifest.dump(2);
  const std::span<const std::byte> manifest_bytes{
      reinterpret_cast<const std::byte*>(manifest_text.data()), manifest_text.size()};
  fo4::io::write_file_atomic(manifest_path, manifest_bytes);
  std::cout << "wrote " << manifest_path << " (" << captured << " files)\n";
  return captured == 0 ? 2 : 0;
}
