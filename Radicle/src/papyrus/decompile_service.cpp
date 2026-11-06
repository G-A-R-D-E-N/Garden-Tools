#include <fo4/papyrus/decompile_service.hpp>
#include <fo4/papyrus/decompiler.hpp>
#include <fo4/papyrus/script_names.hpp>
#include <fo4/io/atomic_write.hpp>
#include <fo4/pex/pex.hpp>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace fo4::papyrus {
namespace {

bool endsWithIgnoreCase(std::string_view value, std::string_view suffix) {
    if (value.size() < suffix.size()) return false;
    value.remove_prefix(value.size() - suffix.size());
    return std::equal(value.begin(), value.end(), suffix.begin(), [](unsigned char a, unsigned char b) {
        return std::tolower(a) == std::tolower(b);
    });
}

void writeFileText(const std::filesystem::path& path, std::string_view text) {
    std::error_code ec;
    const auto status = std::filesystem::symlink_status(path, ec);
    if (ec && ec != std::errc::no_such_file_or_directory) {
        throw std::runtime_error("Cannot inspect output '" + path.string() + "': " + ec.message());
    }
    if (std::filesystem::is_symlink(status)) {
        throw std::runtime_error("Output is a symlink: " + path.string());
    }
#ifdef _WIN32
    const auto attributes = GetFileAttributesW(path.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        throw std::runtime_error("Output is a reparse point: " + path.string());
    }
#endif
    fo4::io::write_file_atomic(path, std::as_bytes(std::span(text.data(), text.size())));
}

std::vector<std::uint8_t> readFileBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Could not read '" + path.string() + "'.");
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(stream)),
                                     std::istreambuf_iterator<char>());
}

std::vector<std::filesystem::path> enumeratePexFiles(const std::filesystem::path& root) {
    std::vector<std::filesystem::path> files;
    std::vector<std::filesystem::path> stack{root};
    while (!stack.empty()) {
        const auto dir = stack.back();
        stack.pop_back();
        std::error_code ec;
        std::filesystem::directory_iterator it(dir, ec), end;
        if (ec) throw std::runtime_error("Cannot enumerate '" + dir.string() + "': " + ec.message());
        for (; it != end; it.increment(ec)) {
            if (ec) throw std::runtime_error("Cannot enumerate '" + dir.string() + "': " + ec.message());
            std::error_code statusEc;
            const auto& entry = *it;
            const auto status = entry.symlink_status(statusEc);
            if (statusEc) throw std::runtime_error("Cannot inspect '" + entry.path().string() + "': " + statusEc.message());
            if (std::filesystem::is_symlink(status)) continue;
#ifdef _WIN32
            const auto attributes = GetFileAttributesW(entry.path().c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES) {
                throw std::runtime_error("Cannot inspect '" + entry.path().string() + "': Windows error " +
                                         std::to_string(GetLastError()));
            }
            if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
#endif
            if (std::filesystem::is_directory(status)) {
                stack.push_back(entry.path());
            } else if (std::filesystem::is_regular_file(status) &&
                       endsWithIgnoreCase(entry.path().extension().string(), ".pex")) {
                files.push_back(entry.path());
            }
        }
        if (ec) throw std::runtime_error("Cannot enumerate '" + dir.string() + "': " + ec.message());
    }
    return files;
}

std::string namespacedOutPath(const std::string& outDir, const std::string& text,
                              const std::string& fallbackBaseName, const std::string& ext,
                              bool assembly) {
    std::filesystem::path rel = fallbackBaseName + ext;
    if (!assembly) {
        const auto obj = extractScriptName(text);
        if (!obj.empty()) {
            rel = std::filesystem::path{};
            std::istringstream stream(obj);
            std::string part;
            while (std::getline(stream, part, ':')) {
                if (!part.empty()) rel /= part;
            }
            rel += ext;
        }
    }
#ifdef _WIN32
    for (const auto& component : rel) {
        auto name = component.string();
        if (name.empty() || name.back() == '.' || name.back() == ' ') {
            throw std::runtime_error("invalid Windows output component: " + name);
        }
        const auto dot = name.find('.');
        if (dot != std::string::npos) name.resize(dot);
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        if (name == "con" || name == "prn" || name == "aux" || name == "nul" ||
            name == "conin$" || name == "conout$" ||
            (name.size() == 4 && (name.substr(0, 3) == "com" || name.substr(0, 3) == "lpt") &&
             name[3] >= '1' && name[3] <= '9')) {
            throw std::runtime_error("invalid Windows output component: " + component.string());
        }
    }
#endif
    const auto root = std::filesystem::path(outDir);
    std::error_code rootEc;
    std::filesystem::create_directories(root, rootEc);
    if (rootEc) throw std::runtime_error("Cannot create directory '" + root.string() + "': " + rootEc.message());
    auto parent = root;
    for (const auto& component : rel.parent_path()) {
        parent /= component;
        std::error_code ec;
        std::filesystem::create_directory(parent, ec);
        if (ec) throw std::runtime_error("Cannot create directory '" + parent.string() + "': " + ec.message());
        const auto status = std::filesystem::symlink_status(parent, ec);
        if (ec || !std::filesystem::is_directory(status)) {
            throw std::runtime_error("Output directory is not a real directory: " + parent.string());
        }
#ifdef _WIN32
        const auto attributes = GetFileAttributesW(parent.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            throw std::runtime_error("Output directory is a reparse point: " + parent.string());
        }
#endif
    }
    return (root / rel).string();
}

#ifdef _WIN32
struct OutputKeyLess {
    bool operator()(const std::wstring& left, const std::wstring& right) const {
        return CompareStringOrdinal(left.c_str(), static_cast<int>(left.size()), right.c_str(),
                                    static_cast<int>(right.size()), TRUE) == CSTR_LESS_THAN;
    }
};
using OutputKey = std::wstring;

OutputKey outputKey(const std::filesystem::path& path) {
    auto resolvedPath = path;
    auto handle = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    bool parentOnly = false;
    if (handle == INVALID_HANDLE_VALUE) {
        const auto error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) {
            throw std::runtime_error("Cannot inspect output: " + path.string());
        }
        parentOnly = true;
        resolvedPath = path.parent_path();
        handle = CreateFileW(resolvedPath.c_str(), FILE_READ_ATTRIBUTES,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    }
    if (handle == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot inspect output directory: " + resolvedPath.string());
    const auto length = GetFinalPathNameByHandleW(handle, nullptr, 0, FILE_NAME_NORMALIZED);
    std::wstring result(length, L'\0');
    const auto written = length ? GetFinalPathNameByHandleW(handle, result.data(), length, FILE_NAME_NORMALIZED) : 0;
    CloseHandle(handle);
    if (!written || written >= length) throw std::runtime_error("Cannot resolve output: " + resolvedPath.string());
    result.resize(written);
    if (parentOnly) {
        result += L"\\";
        result += path.filename().native();
    }
    return result;
}

#else
using OutputKey = std::string;
using OutputKeyLess = std::less<OutputKey>;
using FileIdentity = std::pair<dev_t, ino_t>;

std::optional<FileIdentity> fileIdentity(const std::filesystem::path& path) {
    struct stat status {};
    if (::stat(path.c_str(), &status) == 0) return FileIdentity{status.st_dev, status.st_ino};
    const int error = errno;
    if (error == ENOENT) return std::nullopt;
    throw std::runtime_error("Cannot inspect output '" + path.string() + "': " +
                             std::error_code(error, std::generic_category()).message());
}

OutputKey outputKey(const std::filesystem::path& path) {
    return (std::filesystem::canonical(path.parent_path()) / path.filename()).string();
}
#endif

std::string firstLines(std::string text, std::size_t limit) {
    std::istringstream stream(text);
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(stream, line) && lines.size() < limit) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(std::move(line));
    }
    std::ostringstream joined;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i) joined << '\n';
        joined << lines[i];
    }
    return joined.str();
}

}

std::string DecompileService::decompile(const std::string& source, const std::string& output, bool assembly, bool write) {
    if (source.empty()) {
        return "Provide 'source' (a .pex file or a folder of .pex).";
    }
    const auto ext = assembly ? ".pas" : ".psc";

    if (std::filesystem::is_regular_file(source)) {
        std::string text;
        try {
            const auto bytes = readFileBytes(source);
            const auto pex = fo4::pex::PexFile::fromBytes(bytes);
            text = assembly ? fo4::papyrus::Decompiler::disassemble(pex)
                            : fo4::papyrus::Decompiler::toSource(pex);
        } catch (const std::exception& ex) {
            return ("Decompile failed for " +
                                  std::filesystem::path(source).filename().string() + ": " +
                                  ex.what());
        }

        const bool doWrite = write || !output.empty();
        if (!doWrite) {
            return (text +
                                  "\n\n; ---------------------------------------------------------------\n"
                                  "; NOT SAVED - shown in the OUTPUT pane only. Tick 'Write files to disk'\n"
                                  "; (or set an Output folder) to save this as a .psc.");
        }
        const auto outDir = output.empty()
                                ? std::filesystem::absolute(source).parent_path().string()
                                : output;
        std::string outPath;
        try {
            outPath = namespacedOutPath(outDir, text,
                                        std::filesystem::path(source).stem().string(), ext,
                                        assembly);
            writeFileText(outPath, text);
        } catch (const std::exception& ex) {
            return ("Cannot write '" + outPath + "': " + ex.what());
        }
        return ("SAVED -> " + outPath + "\n\n--- preview (first 40 lines) ---\n" +
                              firstLines(text, 40));
    }

    if (std::filesystem::is_directory(source)) {
        std::vector<std::filesystem::path> files;
        try {
            files = enumeratePexFiles(source);
        } catch (const std::exception& ex) {
            return ex.what();
        }
        if (files.empty()) {
            return ("RESULT: 0 decompiled. No .pex files found under " + source +
                                  ".");
        }
        const auto outDir = output.empty() ? source : output;
        std::error_code ec;
        std::filesystem::create_directories(outDir, ec);
        if (ec) return ("Cannot create output dir '" + outDir + "': " + ec.message());

        int ok = 0;
        std::vector<std::string> fails;
        std::set<OutputKey, OutputKeyLess> destinations;
#ifndef _WIN32
        std::set<FileIdentity> identities;
#endif
        for (const auto& file : files) {
            try {
                const auto bytes = readFileBytes(file);
                const auto pex = fo4::pex::PexFile::fromBytes(bytes);
                const auto text = assembly ? fo4::papyrus::Decompiler::disassemble(pex)
                                           : fo4::papyrus::Decompiler::toSource(pex);
                const auto outPath = namespacedOutPath(outDir, text, file.stem().string(), ext,
                                                       assembly);
                const auto key = outputKey(outPath);
                bool duplicate = destinations.contains(key);
#ifndef _WIN32
                const auto existingIdentity = fileIdentity(outPath);
                if (existingIdentity && identities.contains(*existingIdentity)) duplicate = true;
#endif
                if (duplicate) {
                    throw std::runtime_error("duplicate output path: " + outPath);
                }
                writeFileText(outPath, text);
                destinations.insert(key);
#ifndef _WIN32
                const auto writtenIdentity = fileIdentity(outPath);
                if (!writtenIdentity) throw std::runtime_error("Output disappeared: " + outPath);
                identities.insert(*writtenIdentity);
#endif
                ++ok;
            } catch (const std::exception& ex) {
                fails.push_back(file.filename().string() + ": " + ex.what());
            }
        }

        std::ostringstream msg;
        msg << "RESULT: " << ok << "/" << files.size() << " decompiled -> " << ext << "\n"
            << "OUTPUT: " << outDir << "\n"
            << "Namespaced scripts were written into namespace subfolders so they recompile as-is "
               "(point Compile at this folder with 'Compile all').";
        if (!fails.empty()) {
            msg << "\nFailures (" << fails.size() << "):\n  ";
            const auto shown = std::min<std::size_t>(20, fails.size());
            for (std::size_t i = 0; i < shown; ++i) {
                if (i) msg << "\n  ";
                msg << fails[i];
            }
        }
        return msg.str();
    }

    return "Source not found: " + source;
}

}
