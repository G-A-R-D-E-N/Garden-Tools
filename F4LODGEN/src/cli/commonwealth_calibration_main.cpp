#include "f4lod/commonwealth_calibration.hpp"

#include <charconv>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <string_view>

namespace {

bool parse_size(std::string_view text, std::size_t& value) {
    if (text.empty()) return false;
    std::size_t parsed = 0U;
    const auto* first = text.data();
    const auto* last = text.data() + text.size();
    const auto result = std::from_chars(first, last, parsed);
    if (result.ec != std::errc{} || result.ptr != last || parsed == 0U) return false;
    value = parsed;
    return true;
}

void print_usage() {
    std::cerr
        << "Usage: f4lod-calibrate-commonwealth <Fallout 4 Data directory> [options]\n"
        << "Options:\n"
        << "  --json             Emit versioned machine-readable JSON.\n"
        << "  --samples <count>  Deterministic samples per LOD level; default 3.\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage();
        return 2;
    }

    const std::filesystem::path data_root{argv[1]};
    bool json = false;
    std::size_t samples_per_level = 3U;

    for (int i = 2; i < argc; ++i) {
        const std::string_view arg{argv[i]};
        if (arg == "--json") {
            json = true;
        } else if (arg == "--samples") {
            if (i + 1 >= argc || !parse_size(argv[++i], samples_per_level)) {
                std::cerr << "--samples requires a positive integer.\n";
                return 2;
            }
        } else if (arg == "--help" || arg == "-h") {
            print_usage();
            return 0;
        } else {
            std::cerr << "Unknown option: " << arg << '\n';
            print_usage();
            return 2;
        }
    }

    const auto calibration = f4lod::calibrate_installed_commonwealth_btrs(
        data_root,
        samples_per_level);

    if (json) {
        std::cout << f4lod::format_commonwealth_calibration_json(calibration);
    } else {
        std::cout << f4lod::format_commonwealth_calibration_text(calibration);
    }

    if (!calibration.valid) return 1;
    return calibration.writer_compatible ? 0 : 1;
}
