#include <fo4/papyrus/file_walk.hpp>

#include <algorithm>
#include <cctype>
#include <stack>
#include <string>
#include <string_view>
#include <system_error>

namespace fo4::papyrus {

namespace {

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto x = static_cast<unsigned char>(a[i]);
        const auto y = static_cast<unsigned char>(b[i]);
        if (std::tolower(x) != std::tolower(y)) return false;
    }
    return true;
}

std::string lower(std::string_view text) {
    std::string out(text);
    for (auto& ch : out) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return out;
}

}

std::vector<std::filesystem::path> enumeratePapyrusFiles(const std::filesystem::path& root) {
    std::vector<std::filesystem::path> files;
    if (root.empty() || !std::filesystem::is_directory(root)) return files;

    std::stack<std::filesystem::path> pending;
    pending.push(root);
    while (!pending.empty()) {
        const auto dir = pending.top();
        pending.pop();

        std::error_code error;
        for (auto it = std::filesystem::directory_iterator(dir, error);
             !error && it != std::filesystem::directory_iterator(); it.increment(error)) {
            std::error_code entryError;
            if (it->is_regular_file(entryError)) {
                if (iequals(it->path().extension().string(), ".psc")) {
                    files.push_back(it->path());
                }
            } else if (it->is_directory(entryError) && !it->is_symlink(entryError)) {
                pending.push(it->path());
            }
        }
    }

    std::sort(files.begin(), files.end(),
              [](const auto& a, const auto& b) { return lower(a.string()) < lower(b.string()); });
    return files;
}

}
