#include "McpPathPolicy.hpp"
#include <fo4ide/core/ProtectedWriteName.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace fo4ide::app {
namespace {

namespace fs = std::filesystem;

template <typename CharT>
std::basic_string_view<CharT> trimmed(std::basic_string_view<CharT> text) {
    const auto isJunk = [](CharT c) {
        return c == CharT(' ') || c == CharT('\t') || c == CharT('\r') || c == CharT('\n') || c == CharT('"');
    };
    while (!text.empty() && isJunk(text.front())) text.remove_prefix(1);
    while (!text.empty() && isJunk(text.back())) text.remove_suffix(1);
    return text;
}

fs::path utf8Path(std::string_view text) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

std::string utf8Text(const fs::path& path) {
    const auto text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

std::string lowerAscii(std::string text) {
    std::ranges::transform(text, text.begin(), [](char c) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    });
    return text;
}

fs::path withoutTrailingSeparator(fs::path path) {
    return path.has_filename() ? path : path.parent_path();
}

bool isNetworkOrDevice(const fs::path& path) {
    const auto name = utf8Text(path.root_name());
    return name.starts_with("\\\\") || name.starts_with("//");
}

std::optional<fs::path> resolvedPath(const fs::path& candidate) {
    if (candidate.empty()) return std::nullopt;
    std::error_code ec;
    const auto absolute = candidate.is_absolute() ? candidate : fs::current_path(ec) / candidate;
    if (ec) return std::nullopt;
    if (isNetworkOrDevice(absolute)) return withoutTrailingSeparator(absolute.lexically_normal());
    const auto canonical = fs::weakly_canonical(absolute, ec);
    if (ec) return std::nullopt;
    return withoutTrailingSeparator(canonical);
}

void addRoot(std::vector<fs::path>& roots, const fs::path& candidate) {
    const auto root = resolvedPath(candidate);
    if (!root || !root->has_relative_path()) return;
    const auto relative = root->relative_path();
    if (isNetworkOrDevice(*root) && std::distance(relative.begin(), relative.end()) < 2) return;
    roots.push_back(*root);
}

std::vector<fs::path> configuredEnvRoots() {
    std::vector<fs::path> roots;
#if defined(_WIN32)
    const wchar_t* configured = _wgetenv(L"FO4IDE_MCP_ALLOWED_ROOTS");
#else
    const char* configured = std::getenv("FO4IDE_MCP_ALLOWED_ROOTS");
#endif
    if (configured == nullptr) return roots;
    using Char = fs::path::value_type;
    const fs::path::string_type all(configured);
    for (const auto entry : std::basic_string_view<Char>(all) | std::views::split(Char(';'))) {
        const fs::path::string_type segment(entry.begin(), entry.end());
        if (const auto text = trimmed(std::basic_string_view<Char>(segment)); !text.empty()) roots.emplace_back(text);
    }
    return roots;
}

std::vector<fs::path> allowedRoots(const fo4ide::settings::Settings& settings, const AutoRootResolver& autoRoots) {
    std::vector<fs::path> roots;
    if (const auto data = resolvedPath(settings.dataFolder)) {
        addRoot(roots, data->parent_path());
        addRoot(roots, *data);
    }
    addRoot(roots, settings.fallout4Path);
    for (const auto& root : autoRoots()) addRoot(roots, root);
    addRoot(roots, settings.mo2InstancePath);
    for (const auto& root : configuredEnvRoots()) addRoot(roots, root);
    return roots;
}

bool insideAnyRoot(const fs::path& resolved, const std::vector<fs::path>& roots) {
    return std::ranges::any_of(roots, [&resolved](const fs::path& root) {
        const auto relative = resolved.lexically_relative(root);
        return !relative.empty() && *relative.begin() != "..";
    });
}

std::optional<std::string> pathProblem(std::string_view text, PathUse use, const std::vector<fs::path>& roots) {
    const auto path = utf8Path(text);
    for (const auto& part : path) {
        const auto name = utf8Text(part);
        if (!name.empty() && name != "." && name.find_first_not_of(". ") == std::string::npos) {
            return "'..' and dot-only path segments are not allowed";
        }
    }
    if (use == PathUse::PluginName) {
        if (text.find_first_of("\\/:") == std::string_view::npos) return std::nullopt;
        use = PathUse::Write;
    }
    if (use == PathUse::Compiler && !path.is_absolute()) return "must be a full path";
    if (use == PathUse::Compiler && lowerAscii(utf8Text(path.filename())) != "papyruscompiler.exe") {
        return "must name PapyrusCompiler.exe";
    }
    if ((path.has_root_name() || path.has_root_directory()) && !path.is_absolute()) {
        return "must be a full path, not drive- or root-relative";
    }
    std::optional<fs::path> resolved;
    if (isNetworkOrDevice(path)) {
        resolved = withoutTrailingSeparator(path.lexically_normal());
        if (!insideAnyRoot(*resolved, roots)) return "is a network or device path outside the allowed roots";
    } else if (use != PathUse::DataRelative || path.is_absolute()) {
        resolved = resolvedPath(path);
        if (!resolved || !insideAnyRoot(*resolved, roots)) return "is outside the allowed roots";
    }
    if (use == PathUse::Write) {
        if (const auto problem = core::protectedWriteName(utf8Text(path.filename()))) return problem;
        if (resolved) return core::protectedWriteName(utf8Text(resolved->filename()));
    }
    return std::nullopt;
}

bool isInlineObject(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos || text[first] != '{') return false;
    return nlohmann::json::parse(text, nullptr, false).is_object();
}

bool isPluginNameArgument(std::string_view name, const nlohmann::json& schema) {
    return (name == "plugin" || name.ends_with("_plugin")) && schema.value("type", std::string{}) == "string";
}

std::optional<fo4ide::mcp::ToolResult> refusePathArguments(const nlohmann::json& arguments,
                                                           const std::vector<PathArgument>& paths,
                                                           const fo4ide::settings::SharedSettings& settings,
                                                           const AutoRootResolver& autoRoots) {
    if (paths.empty()) return std::nullopt;
    if (!arguments.is_object() && !arguments.is_null()) {
        return fo4ide::mcp::ToolResult{"MCP tool arguments must be an object", true};
    }
    const auto roots = allowedRoots(settings.snapshot(), autoRoots);
    const auto refuse = [](std::string_view name, std::string_view reason) {
        return fo4ide::mcp::ToolResult{"Path refused for '" + std::string(name) + "': " + std::string(reason), true};
    };
    for (const auto& argument : paths) {
        const auto found = arguments.find(argument.name);
        if (found == arguments.end() || found->is_null()) continue;
        std::vector<std::string> values;
        if (argument.form == PathForm::JsonList) {
            const auto list = found->is_string()
                ? nlohmann::json::parse(found->get_ref<const std::string&>(), nullptr, false)
                : *found;
            if (!list.is_array()) return refuse(argument.name, "must be a list of paths");
            for (const auto& item : list) {
                if (!item.is_string()) return refuse(argument.name, "must be a list of path strings");
                values.push_back(item.get<std::string>());
            }
        } else {
            if (argument.form == PathForm::SingleOrObject && found->is_object()) continue;
            if (!found->is_string()) return refuse(argument.name, "must be a string");
            if (argument.form == PathForm::SingleOrObject && isInlineObject(found->get<std::string>())) continue;
            values.push_back(found->get<std::string>());
        }
        // Invariant: only a value the tool itself splits is split here, or 'root;x/file' would pass as two inside paths and open as one outside path.
        const bool list = argument.form == PathForm::SemicolonList || argument.form == PathForm::LineList;
        const char separator = argument.form == PathForm::LineList ? '\n' : ';';
        for (const auto& value : values) {
            std::vector<std::string_view> parts;
            if (list) {
                for (const auto part : std::string_view(value) | std::views::split(separator)) {
                    parts.emplace_back(part.begin(), part.end());
                }
            } else {
                parts.emplace_back(value);
            }
            for (const auto part : parts) {
                const auto text = trimmed(part);
                if (text.empty()) continue;
                if (std::ranges::any_of(text, [](char c) { return static_cast<unsigned char>(c) < 0x20; })) {
                    return refuse(argument.name, "control characters are not allowed in paths");
                }
                if (const auto problem = pathProblem(text, argument.use, roots)) {
                    return refuse(argument.name, *problem);
                }
            }
        }
    }
    return std::nullopt;
}

}

std::shared_ptr<fo4ide::mcp::Tool> withPathPolicy(std::shared_ptr<fo4ide::mcp::Tool> tool,
                                                  std::vector<PathArgument> paths,
                                                  const fo4ide::settings::SharedSettings& settings,
                                                  AutoRootResolver autoRoots) {
    auto definition = tool->definition();
    const auto properties = definition.inputSchema.value("properties", nlohmann::json::object());
    for (const auto& [name, schema] : properties.items()) {
        if (isPluginNameArgument(name, schema)) paths.push_back({name, PathUse::PluginName, PathForm::Single});
    }
    // Constraint: settings is captured by reference, so the SharedSettings and the resolver's captures must outlive the registry.
    return std::make_shared<fo4ide::mcp::FunctionTool>(
        std::move(definition),
        [tool = std::move(tool), paths = std::move(paths), &settings, autoRoots = std::move(autoRoots)](const nlohmann::json& arguments) {
            if (const auto refused = refusePathArguments(arguments, paths, settings, autoRoots)) return *refused;
            return tool->call(arguments);
        });
}

}
