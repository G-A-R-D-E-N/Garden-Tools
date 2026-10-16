#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/f4se/CrossCheck.hpp>
#include <fo4ide/f4se/CppScanner.hpp>
#include <fo4ide/f4se/NativeOracle.hpp>
#include <fo4ide/f4se/RegistrationExtractor.hpp>
#include <fo4ide/f4se/TypeMap.hpp>
#include <fo4ide/f4se/VersionDiff.hpp>

// Mirrors F4SECorpusTests.cs / F4SEOracleCorpusTests.cs: a real F4SE source
// tree is scanned with the registration extractor and cross checked against
// the merged Papyrus corpus it ships. Gated on FO4RE_F4SE_SRC exactly like
// the C# TestRoots.F4SESourceVariable, so the sweep skips when unset.

namespace {

std::string trim(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    return value;
}

std::vector<std::filesystem::path> rootsFromEnv(const char* variable) {
    std::vector<std::filesystem::path> roots;
    const char* raw = std::getenv(variable);
    if (!raw || !*raw) return roots;
    std::istringstream stream(raw);
    std::string part;
    while (std::getline(stream, part, ':')) {
        const auto cleaned = trim(part);
        if (!cleaned.empty() && std::filesystem::is_directory(cleaned)) {
            roots.emplace_back(cleaned);
        }
    }
    return roots;
}

bool isDirectory(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::is_directory(path, ec);
}

bool isRegularFile(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
}

std::vector<std::filesystem::path> directoriesUnder(const std::filesystem::path& root) {
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(root, ec), end; !ec && it != end;
         it.increment(ec)) {
        if (it->is_directory()) out.push_back(it->path());
    }
    return out;
}

std::string readText(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

// Port of the C# Trees(): every F4SE checkout under the roots, recognised by
// its layout.
struct Tree {
    std::string label;
    std::filesystem::path cppDirectory;
    std::filesystem::path mergedScripts;
    std::optional<std::filesystem::path> vanillaScripts;
};

std::vector<Tree> trees() {
    std::vector<Tree> out;
    for (const auto& root : rootsFromEnv("FO4RE_F4SE_SRC")) {
        for (const auto& directory : directoriesUnder(root)) {
            const auto cpp = directory / "f4se";
            const auto modified = directory / "scripts" / "modified";
            const auto vanilla = directory / "scripts" / "vanilla";
            if (isDirectory(cpp) && isDirectory(modified)) {
                out.push_back(Tree{directory.filename().string(), cpp, modified,
                                   isDirectory(vanilla)
                                       ? std::optional<std::filesystem::path>(vanilla)
                                       : std::nullopt});
                continue;
            }

            const auto cpp2 = directory / "src" / "f4se" / "f4se";
            const auto merged = directory / "Data" / "Scripts" / "Source";
            if (isDirectory(cpp2) && isDirectory(merged)) {
                std::optional<std::filesystem::path> sibling;
                for (const auto& other : directoriesUnder(root)) {
                    const auto candidate = other / "scripts" / "vanilla";
                    if (isDirectory(candidate)) {
                        sibling = candidate;
                        break;
                    }
                }
                out.push_back(Tree{directory.filename().string(), cpp2, merged, sibling});
            }
        }
    }
    std::sort(out.begin(), out.end(),
              [](const Tree& a, const Tree& b) { return a.label < b.label; });
    return out;
}

std::string shorten(const std::filesystem::path& directory) {
    std::string text = directory.string();
    const auto pos = text.find("/f4se");
    return pos == std::string::npos ? text : text.substr(pos + 1);
}

// Port of ModuleDirectories(): every directory holding a PapyrusVM.h next to
// Papyrus*.cpp files.
std::vector<std::filesystem::path> moduleDirectories() {
    std::vector<std::filesystem::path> found;
    for (const auto& root : rootsFromEnv("FO4RE_F4SE_SRC")) {
        std::vector<std::filesystem::path> stack{root};
        std::error_code ec;
        while (!stack.empty()) {
            const auto dir = stack.back();
            stack.pop_back();
            for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end;
                 it.increment(ec)) {
                if (it->is_directory()) stack.push_back(it->path());
            }
            if (!isRegularFile(dir / "PapyrusVM.h")) continue;
            bool hasModules = false;
            for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                if (entry.is_regular_file() &&
                    entry.path().filename().string().rfind("Papyrus", 0) == 0 &&
                    entry.path().extension() == ".cpp") {
                    hasModules = true;
                    break;
                }
            }
            if (hasModules) found.push_back(dir);
        }
    }
    std::sort(found.begin(), found.end());
    found.erase(std::unique(found.begin(), found.end()), found.end());
    return found;
}

}  // namespace

TEST_CASE("F4SE corpus: every registration in a real tree is recovered") {
    const auto directories = moduleDirectories();
    if (directories.empty()) SKIP("FO4RE_F4SE_SRC is not set to a directory holding an F4SE source tree; nothing to scan.");

    const fo4ide::f4se::RegistrationExtractor extractor;
    int totalPresent = 0;
    int totalRecovered = 0;

    for (const auto& directory : directories) {
        int present = 0;
        for (const auto& file : std::filesystem::directory_iterator(directory)) {
            if (!file.is_regular_file() || file.path().extension() != ".cpp") continue;
            if (file.path().filename().string().rfind("Papyrus", 0) != 0) continue;
            const auto blanked = fo4ide::f4se::CppScanner::blankComments(readText(file.path()));
            std::size_t pos = 0;
            while ((pos = blanked.find("RegisterFunction(", pos)) != std::string::npos) {
                present++;
                pos += 17;
            }
        }

        const auto schemas = extractor.extractDirectory(directory);
        int natives = 0;
        int problems = 0;
        int unmapped = 0;
        std::vector<std::string> problemTexts;
        for (const auto& schema : schemas) {
            natives += static_cast<int>(schema.natives.size());
            problems += static_cast<int>(schema.problems.size());
            for (const auto& problem : schema.problems) problemTexts.push_back(problem);
            for (const auto& native : schema.natives) {
                if (native.returnType.name == fo4ide::f4se::RegistrationExtractor::kUnknownTypeName) {
                    unmapped++;
                }
                for (const auto& parameter : native.parameters) {
                    if (parameter.type.name ==
                        fo4ide::f4se::RegistrationExtractor::kUnknownTypeName) {
                        unmapped++;
                    }
                }
            }
        }

        INFO("directory: " + shorten(directory));
        CHECK(natives == present);
        CHECK(problems == 0);
        CHECK(unmapped == 0);

        totalPresent += present;
        totalRecovered += natives;
    }

    CHECK(totalPresent > 0);
    CHECK(totalRecovered == totalPresent);
}

TEST_CASE("F4SE corpus: recovered bindings are internally consistent") {
    const auto directories = moduleDirectories();
    if (directories.empty()) SKIP("FO4RE_F4SE_SRC is not set; nothing to scan.");

    const fo4ide::f4se::RegistrationExtractor extractor;
    int checked_ = 0;
    for (const auto& directory : directories) {
        for (const auto& schema : extractor.extractDirectory(directory)) {
            for (const auto& native : schema.natives) {
                checked_++;
                CHECK_FALSE(native.functionName.empty());
                CHECK_FALSE(native.papyrusClass.empty());
                CHECK_FALSE(native.cppFunctionName.empty());
                CHECK(native.arity() == static_cast<int>(native.parameters.size()));
                CHECK(native.isGlobal() ==
                      (native.cppBaseType == fo4ide::f4se::F4SETypeMap::kStaticFunctionTag));
                CHECK(native.sourceLine > 0);
            }
        }
    }
    CHECK(checked_ > 0);
}

TEST_CASE("F4SE corpus: recovered registrations agree with the papyrus declarations") {
    const auto found = trees();
    if (found.empty()) SKIP("FO4RE_F4SE_SRC is not set to an F4SE tree; nothing to cross check.");

    const fo4ide::f4se::RegistrationExtractor extractor;
    for (const auto& tree : found) {
        std::vector<fo4ide::f4se::NativeBinding> recovered;
        for (const auto& schema : extractor.extractDirectory(tree.cppDirectory)) {
            for (const auto& native : schema.natives) recovered.push_back(native);
        }

        const auto oracle = fo4ide::f4se::NativeOracle::build(
            tree.mergedScripts, tree.vanillaScripts ? &*tree.vanillaScripts : nullptr);
        const auto result = fo4ide::f4se::CrossCheck::compare(recovered, oracle);

        INFO("tree: " + tree.label);
        CHECK(result.recovered > 0);
        CHECK(result.mismatches.empty());
    }
}

TEST_CASE("F4SE corpus: the two shipped versions differ only in ways the diff can name") {
    const auto found = trees();
    if (found.size() < 2) SKIP("FO4RE_F4SE_SRC does not hold two F4SE trees; nothing to diff.");

    const fo4ide::f4se::RegistrationExtractor extractor;
    struct Surface {
        std::string label;
        int count;
        std::vector<fo4ide::f4se::NativeBinding> natives;
    };
    std::vector<Surface> surfaces;
    for (const auto& tree : found) {
        std::vector<fo4ide::f4se::NativeBinding> natives;
        for (const auto& schema : extractor.extractDirectory(tree.cppDirectory)) {
            for (const auto& native : schema.natives) natives.push_back(native);
        }
        surfaces.push_back(
            Surface{tree.label, static_cast<int>(natives.size()), std::move(natives)});
    }
    std::sort(surfaces.begin(), surfaces.end(),
              [](const Surface& a, const Surface& b) { return a.count < b.count; });

    const auto& older = surfaces.front();
    const auto& newer = surfaces.back();
    const auto delta = fo4ide::f4se::VersionDiff::compare(older.natives, newer.natives);

    INFO(older.label + " (" + std::to_string(older.count) + ") -> " + newer.label + " (" +
         std::to_string(newer.count) + ")");
    CHECK((newer.count - older.count) ==
          static_cast<int>(delta.added.size()) - static_cast<int>(delta.removed.size()));
}
