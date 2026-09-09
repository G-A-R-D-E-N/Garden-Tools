#!/usr/bin/env bash
set -euo pipefail

base=19948098

git cat-file -e "${base}^{commit}"
rm -rf /tmp/ponytail-174b-quest-green
git clone --depth=1 https://github.com/DietrichGebert/ponytail.git /tmp/ponytail-174b-quest-green
test -f /tmp/ponytail-174b-quest-green/AGENTS.md

python3 - <<'PY'
from pathlib import Path

path = Path("PluginEditTool/native/src/plugin/RadiclePluginQuest.cpp")
text = path.read_text()

alias_old = '''        fo4::forms::write_quest_aliases(mutated, questKey, creations);
        fo4::write_plugin_file(mutated, loaded->path);
        loaded->plugin = std::move(mutated);
        {
            std::error_code ec;
            loaded->loadedTime = std::filesystem::last_write_time(loaded->path, ec);
        }
        rebuildGraphLocked();
        return nlohmann::json(
            "Set " + std::to_string(creations.size()) + " quest alias(es) on " +
            recordText + " in " + pluginName + ". Written to disk.");
'''
alias_new = '''        fo4::forms::write_quest_aliases(mutated, questKey, creations);
        stagePluginMutationLocked(*loaded, std::move(mutated));
        return nlohmann::json(
            "Set " + std::to_string(creations.size()) + " quest alias(es) on " +
            recordText + " in " + pluginName + ". SavePlugin to persist.");
'''
if text.count(alias_old) != 1:
    raise SystemExit(f"alias production tail count {text.count(alias_old)}")
text = text.replace(alias_old, alias_new, 1)

stage_old = '''        const auto pluginBytes = fo4::write_plugin(mutated);
        if (localizedTableChanged) {
#ifdef FO4IDE_TEST_HOOKS
            if (localizedQuestBeforeCommitHook != nullptr) localizedQuestBeforeCommitHook();
#endif
            try {
                if (readLocalizedFileBytes(localizedTables->stringsPath) !=
                        localizedTables->originalStringsBytes ||
                    readLocalizedFileBytes(localizedTables->dlstringsPath) !=
                        localizedTables->originalDlstringsBytes ||
                    readLocalizedFileBytes(localizedTables->ilstringsPath) !=
                        localizedTables->originalIlstringsBytes) {
                    return std::unexpected(Error{
                        "LocalizedSidecarChangedOnDisk",
                        "localized sibling table changed after it was read; reload before writing"
                    });
                }
            } catch (const std::exception& error) {
                return std::unexpected(Error{
                    "LocalizedLogTableInvalid",
                    "could not re-read localized DLSTRINGS before writing: " +
                        std::string(error.what())
                });
            }
            const auto tableBytes = fo4::localization::write_string_table(
                localizedTables->dlstrings);
            fo4::io::write_file_pair_dependency_first(
                localizedTables->dlstringsPath,
                tableBytes,
                loaded->path,
                pluginBytes);
        } else {
            fo4::write_plugin_file(mutated, loaded->path);
        }
        loaded->plugin = std::move(mutated);
        {
            std::error_code ec;
            loaded->loadedTime = std::filesystem::last_write_time(loaded->path, ec);
        }
        rebuildGraphLocked();
        return nlohmann::json(
            "Set " + std::to_string(stages.stages.size()) + " quest stage(s) on " +
            recordText + " in " + pluginName + ". Written to disk.");
'''
stage_new = '''        if (localizedTableChanged) {
            const auto pluginBytes = fo4::write_plugin(mutated);
#ifdef FO4IDE_TEST_HOOKS
            if (localizedQuestBeforeCommitHook != nullptr) localizedQuestBeforeCommitHook();
#endif
            try {
                if (readLocalizedFileBytes(localizedTables->stringsPath) !=
                        localizedTables->originalStringsBytes ||
                    readLocalizedFileBytes(localizedTables->dlstringsPath) !=
                        localizedTables->originalDlstringsBytes ||
                    readLocalizedFileBytes(localizedTables->ilstringsPath) !=
                        localizedTables->originalIlstringsBytes) {
                    return std::unexpected(Error{
                        "LocalizedSidecarChangedOnDisk",
                        "localized sibling table changed after it was read; reload before writing"
                    });
                }
            } catch (const std::exception& error) {
                return std::unexpected(Error{
                    "LocalizedLogTableInvalid",
                    "could not re-read localized DLSTRINGS before writing: " +
                        std::string(error.what())
                });
            }
            const auto tableBytes = fo4::localization::write_string_table(
                localizedTables->dlstrings);
            fo4::io::write_file_pair_dependency_first(
                localizedTables->dlstringsPath,
                tableBytes,
                loaded->path,
                pluginBytes);
            loaded->plugin = std::move(mutated);
            {
                std::error_code ec;
                loaded->loadedTime = std::filesystem::last_write_time(loaded->path, ec);
            }
            rebuildGraphLocked();
            return nlohmann::json(
                "Set " + std::to_string(stages.stages.size()) + " quest stage(s) on " +
                recordText + " in " + pluginName + ". Written to disk.");
        }
        stagePluginMutationLocked(*loaded, std::move(mutated));
        return nlohmann::json(
            "Set " + std::to_string(stages.stages.size()) + " quest stage(s) on " +
            recordText + " in " + pluginName + ". SavePlugin to persist.");
'''
if text.count(stage_old) != 1:
    raise SystemExit(f"stage production tail count {text.count(stage_old)}")
text = text.replace(stage_old, stage_new, 1)
path.write_text(text)
PY

git diff --check
test "$(git diff --name-only | tr -d '\r')" = "PluginEditTool/native/src/plugin/RadiclePluginQuest.cpp"

cmake -S PluginEditTool/native -B build/fo4ide-174b-quest-green -G Ninja -DCMAKE_BUILD_TYPE=Debug -DFO4IDE_BUILD_TESTS=ON -DFO4IDE_BUILD_HTTP=OFF
cmake --build build/fo4ide-174b-quest-green --target fo4ide_test_quest_authoring fo4ide_test_plugin_backend
quest=build/fo4ide-174b-quest-green/bin/fo4ide_test_quest_authoring
backend=build/fo4ide-174b-quest-green/bin/fo4ide_test_plugin_backend
"$quest" "Native SetQuestAliases replaces reference location and collection aliases"
"$quest" "Native SetQuestStages writes multiple log entries with note and log text"
"$quest" "Native SetQuestAliases adds a loaded uniqueActor master and remaps the fill"
"$quest"
"$backend"

printf '\n=== persistence inventory ===\n'
grep -RInE 'write_plugin_file|write_plugin\(|write_file_pair|atomic_write|std::ofstream|last_write_time|Written to disk|Saved |save it' PluginEditTool/native/src/plugin || true

git rm .github/workflows/174b-core-writers.yml .github/174b-quest-green.sh
git config user.name "NomadsReach"
git config user.email "144523850+NomadsReach@users.noreply.github.com"
git add PluginEditTool/native/src/plugin/RadiclePluginQuest.cpp
git commit -m "feat: defer plugin-only quest persistence"
git push origin HEAD:work/174b-deferred-mutations
