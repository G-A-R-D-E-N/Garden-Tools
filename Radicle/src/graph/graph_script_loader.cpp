#include <fo4/graph/graph_script_loader.hpp>

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <fo4/papyrus/decompiler.hpp>
#include <fo4/papyrus/parser.hpp>
#include <fo4/pex/pex.hpp>

namespace fo4::graph {

namespace {

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

bool pathEquals(const std::filesystem::path& a, const std::filesystem::path& b) {
    return iequals(a.string(), b.string());
}

std::string lowerExtension(const std::filesystem::path& path) {
    const auto extension = path.extension().string();
    std::string out;
    for (const char ch : extension) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    return out;
}

std::vector<std::uint8_t> readBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("could not read " + path.string());
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(stream)),
                                     std::istreambuf_iterator<char>());
}

std::string readText(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("could not read " + path.string());
    return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

}

bool GraphScriptLoader::isScript(const std::filesystem::path& path) {
    const auto extension = lowerExtension(path);
    return extension == ".psc" || extension == ".pex";
}

GraphScriptLoadResult GraphScriptLoader::load(
    const std::filesystem::path& path, const std::vector<std::filesystem::path>& roots) {
    std::error_code errorCode;
    if (!std::filesystem::is_regular_file(path, errorCode)) {
        return failed("There is no file at " + path.string() + ".");
    }

    const auto extension = lowerExtension(path);
    std::string text;
    if (extension == ".pex") {
        try {
            const auto pex = fo4::pex::PexFile::fromBytes(readBytes(path));
            text = papyrus::Decompiler::toSource(pex);
        } catch (const std::exception& e) {
            return failed(path.filename().string() + " could not be decompiled: " + e.what());
        }

        if (text.find(".code") != std::string::npos ||
            text.find("[decompile failed") != std::string::npos) {
            return failed(path.filename().string() +
                          " has a function the decompiler could not turn back into Papyrus, so it "
                          "came back as an assembly listing. Read it in the Papyrus panel instead.");
        }
    } else if (extension == ".psc") {
        try {
            text = readText(path);
        } catch (const std::exception& e) {
            return failed(path.filename().string() + " could not be read: " + e.what());
        }
    } else {
        return failed(path.filename().string() + " is not a .psc or a .pex.");
    }

    const auto fileName = path.filename().string();
    const auto parsed = papyrus::Parser::parse(text, fileName);
    papyrus::ScriptIndex index;
    for (const auto& root : rootsFor(path, roots)) index.addRoot(root);

    GraphLifter lifter(index);
    auto lifted = lifter.lift(parsed);

    GraphScriptLoadResult result;
    if (lifted.success()) result.document = std::move(lifted.document);
    result.diagnostics = std::move(lifted.diagnostics);
    return result;
}

std::vector<std::filesystem::path> GraphScriptLoader::rootsFor(
    const std::filesystem::path& path, const std::vector<std::filesystem::path>& roots) {
    std::vector<std::filesystem::path> list;
    std::error_code errorCode;
    for (const auto& root : roots) {
        if (std::filesystem::is_directory(root, errorCode)) list.push_back(root);
    }

    const auto folder = std::filesystem::absolute(path).parent_path();
    bool seen = false;
    for (const auto& existing : list) {
        if (pathEquals(existing, folder)) {
            seen = true;
            break;
        }
    }
    if (!seen) list.insert(list.begin(), folder);
    return list;
}

GraphScriptLoadResult GraphScriptLoader::failed(std::string message) {
    GraphScriptLoadResult result;
    result.failure = std::move(message);
    return result;
}

}
