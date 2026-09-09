#!/usr/bin/env bash
set -euo pipefail

base=203f27e7

git cat-file -e "${base}^{commit}"
rm -rf /tmp/ponytail-174b-final-green
git clone --depth=1 https://github.com/DietrichGebert/ponytail.git /tmp/ponytail-174b-final-green
test -f /tmp/ponytail-174b-final-green/AGENTS.md

python3 - <<'PY'
from pathlib import Path

# masters.Reorder
path = Path("PluginEditTool/native/src/plugin/RadiclePluginMasters.cpp")
text = path.read_text()
old = '''    try {
        auto remapped = fo4::remap_plugin_masters(loaded->plugin, requested);
        fo4::write_plugin_file(remapped, loaded->path);
        loaded->plugin = std::move(remapped);
        {
            std::error_code ec;
            loaded->loadedTime = std::filesystem::last_write_time(loaded->path, ec);
        }
        rebuildGraphLocked();
        return nlohmann::json("Wrote " + name + " with reordered masters.");
    } catch (const std::exception& error) {
'''
new = '''    try {
        auto remapped = fo4::remap_plugin_masters(loaded->plugin, requested);
        stagePluginMutationLocked(*loaded, std::move(remapped));
        return nlohmann::json(
            "Reordered masters in " + name + ". SavePlugin to persist.");
    } catch (const std::exception& error) {
'''
if text.count(old) != 1:
    raise SystemExit(f"masters reorder tail count {text.count(old)}")
path.write_text(text.replace(old, new, 1))

# ChangeReferencingRecords
path = Path("PluginEditTool/native/src/plugin/RadiclePluginReferenceRemap.cpp")
text = path.read_text()
old = '''        fo4::write_plugin_file(mutated, patch->path);
        patch->plugin = std::move(mutated);
        {
            std::error_code ec;
            patch->loadedTime = std::filesystem::last_write_time(patch->path, ec);
        }
        rebuildGraphLocked();

        return nlohmann::json(
            "Changed " + std::to_string(changed) + " referencing record(s) from " +
            fromText + " to " + toText + " in " + patchName + ". Ensure " +
            patchName + " loads after " + fromKey->plugin.filename() + " and " +
            toKey->plugin.filename() + ", then save it.");
'''
new = '''        stagePluginMutationLocked(*patch, std::move(mutated));

        return nlohmann::json(
            "Changed " + std::to_string(changed) + " referencing record(s) from " +
            fromText + " to " + toText + " in " + patchName + ". Ensure " +
            patchName + " loads after " + fromKey->plugin.filename() + " and " +
            toKey->plugin.filename() + ". SavePlugin to persist.");
'''
if text.count(old) != 1:
    raise SystemExit(f"reference remap tail count {text.count(old)}")
path.write_text(text.replace(old, new, 1))

# RenumberPluginFormIds
path = Path("PluginEditTool/native/src/plugin/RadiclePluginRenumber.cpp")
text = path.read_text()
old = '''        fo4::write_plugin_file(mutated, loaded->path);
        loaded->plugin = std::move(mutated);
        {
            std::error_code ec;
            loaded->loadedTime = std::filesystem::last_write_time(loaded->path, ec);
        }
        rebuildGraphLocked();

        std::string message =
            "Renumbered " + std::to_string(remappedCount) + " record(s) in " + pluginName;
        if (externalCount != 0) {
            message += ". Warning: " + std::to_string(externalCount) +
                       " external reference(s) may now be dangling";
        }
        return nlohmann::json(std::move(message));
'''
new = '''        stagePluginMutationLocked(*loaded, std::move(mutated));

        std::string message =
            "Renumbered " + std::to_string(remappedCount) + " record(s) in " + pluginName;
        if (externalCount != 0) {
            message += ". Warning: " + std::to_string(externalCount) +
                       " external reference(s) may now be dangling";
        }
        message += ". SavePlugin to persist.";
        return nlohmann::json(std::move(message));
'''
if text.count(old) != 1:
    raise SystemExit(f"renumber tail count {text.count(old)}")
path.write_text(text.replace(old, new, 1))

# Components wording
path = Path("PluginEditTool/native/src/plugin/RadiclePluginComponents.cpp")
text = path.read_text()
old = '        message += " Written to disk.";\n'
new = '        message += " SavePlugin to persist.";\n'
if text.count(old) != 1:
    raise SystemExit(f"components wording count {text.count(old)}")
path.write_text(text.replace(old, new, 1))

# SetConditions wording
path = Path("PluginEditTool/native/src/plugin/RadiclePluginMutation.cpp")
text = path.read_text()
old = '        message += " Written to disk.";\n'
new = '        message += " SavePlugin to persist.";\n'
if text.count(old) != 1:
    raise SystemExit(f"conditions wording count {text.count(old)}")
path.write_text(text.replace(old, new, 1))

# RemoveListItem wording
path = Path("PluginEditTool/native/src/plugin/RadiclePluginFormLinkList.cpp")
text = path.read_text()
old = '''        return nlohmann::json(
            "Removed " + std::to_string(removed) + " entr(y/ies) matching " + valueText +
            " from " + fieldName + " (now " + std::to_string(newSize) + ") on " +
            recordText + ". Written to disk.");
'''
new = '''        return nlohmann::json(
            "Removed " + std::to_string(removed) + " entr(y/ies) matching " + valueText +
            " from " + fieldName + " (now " + std::to_string(newSize) + ") on " +
            recordText + ". SavePlugin to persist.");
'''
if text.count(old) != 1:
    raise SystemExit(f"list removal wording count {text.count(old)}")
path.write_text(text.replace(old, new, 1))
PY

git diff --check
actual="$(git diff --name-only | sort)"
expected="$(printf '%s\n' \
  PluginEditTool/native/src/plugin/RadiclePluginComponents.cpp \
  PluginEditTool/native/src/plugin/RadiclePluginFormLinkList.cpp \
  PluginEditTool/native/src/plugin/RadiclePluginMasters.cpp \
  PluginEditTool/native/src/plugin/RadiclePluginMutation.cpp \
  PluginEditTool/native/src/plugin/RadiclePluginReferenceRemap.cpp \
  PluginEditTool/native/src/plugin/RadiclePluginRenumber.cpp | sort)"
test "$actual" = "$expected"

cmake -S PluginEditTool/native -B build/fo4ide-174b-final-green -G Ninja -DCMAKE_BUILD_TYPE=Debug -DFO4IDE_BUILD_TESTS=ON -DFO4IDE_BUILD_HTTP=OFF
cmake --build build/fo4ide-174b-final-green --target fo4ide_test_plugin_backend fo4ide_test_quest_authoring
backend=build/fo4ide-174b-final-green/bin/fo4ide_test_plugin_backend
quest=build/fo4ide-174b-final-green/bin/fo4ide_test_quest_authoring

"$backend" "Masters reorder writes immediately and preserves logical FormLinks"
"$backend" "Native ChangeReferencingRecords copies winners adds target master and remaps links"
"$backend" "Native RenumberPluginFormIds vacates overlapping targets before rekeying dependents"
"$backend" "PluginEditTool set_components writes FVPA and no CDIX on a COBJ"
"$backend" "condition backend: SetConditions replaces the list and round-trips"
"$backend" "PluginEditTool remove_list_item matches the field name case-insensitively"
"$backend"
"$quest"

printf '\n=== final persistence inventory ===\n'
grep -RInE 'write_plugin_file|write_plugin\(|write_file_pair|atomic_write|std::ofstream|last_write_time|Written to disk|Saved |save it|SavePlugin' PluginEditTool/native/src/plugin || true

git rm .github/workflows/174b-core-writers.yml .github/174b-final-writers-green.sh
git config user.name "NomadsReach"
git config user.email "144523850+NomadsReach@users.noreply.github.com"
git add \
  PluginEditTool/native/src/plugin/RadiclePluginComponents.cpp \
  PluginEditTool/native/src/plugin/RadiclePluginFormLinkList.cpp \
  PluginEditTool/native/src/plugin/RadiclePluginMasters.cpp \
  PluginEditTool/native/src/plugin/RadiclePluginMutation.cpp \
  PluginEditTool/native/src/plugin/RadiclePluginReferenceRemap.cpp \
  PluginEditTool/native/src/plugin/RadiclePluginRenumber.cpp
git commit -m "feat: defer remaining plugin writers"
git push origin HEAD:work/174b-deferred-mutations
