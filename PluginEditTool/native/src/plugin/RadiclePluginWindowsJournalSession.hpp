#pragma once

#if defined(_WIN32) && defined(FO4IDE_TEST_HOOKS)
#include "RadiclePluginJournalDestination.hpp"
#include "RadiclePluginJournalManifest.hpp"
#include "RadiclePluginJournalSession.hpp"
#include "RadiclePluginWindowsJournalIntent.hpp"

namespace fo4ide::plugin::detail {
class WindowsJournalSessionState {
public:
    using Targets = std::vector<std::unique_ptr<JournalDestination>>;
    static std::filesystem::path pluginIdentity(const JournalRoots& roots);
    static bool samePath(const std::filesystem::path& first, const std::filesystem::path& second);
    static std::string completionDocument(const WindowsJournalDirectory& journal, std::string_view name);
    static std::string authorize(const WindowsJournalDirectory& journal, std::string_view document,
        const JournalManifest& manifest, const std::vector<std::filesystem::path>& paths, const Targets& targets);
    static void inspect(const WindowsJournalDirectory& journal, std::string_view document,
        const JournalManifest& manifest, const std::vector<std::filesystem::path>& paths, const Targets& targets);
    static void prepare(const WindowsJournalDirectory& journal, std::string_view document,
        const JournalManifest& manifest, const std::vector<std::filesystem::path>& paths, const Targets& targets,
        WindowsJournalSessionHook hook, const std::function<void()>& revalidate);
    static void apply(const WindowsJournalDirectory& journal, const JournalManifest& manifest,
        const std::vector<std::filesystem::path>& paths, const Targets& targets,
        const std::function<void()>& revalidate, const std::function<void(std::size_t)>& committed);
    static bool cleanup(const WindowsJournalDirectory& journal, std::string_view document, const JournalManifest& manifest,
        const std::vector<std::filesystem::path>& paths, const Targets& targets, WindowsJournalSessionHook hook,
        const std::function<void()>& revalidate, bool inspectOnly = false);
};
}
#endif
