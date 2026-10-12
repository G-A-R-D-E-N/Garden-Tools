#pragma once

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>

enum class GameRelease { OG, NG, AE, Unknown };

struct GameBuild {
  std::string file_version;
  GameRelease release;
};

std::string parse_file_version(std::span<const std::byte> bytes);
GameBuild game_build_from_version(const std::string& file_version);
GameBuild detect_game_build(const std::filesystem::path& data_dir);
bool exact_counts_known(const GameBuild& build);
void record_game_build(const GameBuild& build);
