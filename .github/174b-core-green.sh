#!/usr/bin/env bash
set -euo pipefail

base=f4aa389b3037e710b1695258c2fc5eaa4c87ef7d

git cat-file -e "${base}^{commit}"
changed="$(git diff --name-only "${base}..HEAD" | sort)"
expected="$(printf '%s\n' '.github/174b-core-green.sh' '.github/workflows/174b-core-writers.yml' | sort)"
test "$changed" = "$expected"

rm -rf /tmp/ponytail-174b-core-green
git clone --depth=1 https://github.com/DietrichGebert/ponytail.git /tmp/ponytail-174b-core-green
test -f /tmp/ponytail-174b-core-green/AGENTS.md

python3 - <<'PY'
from pathlib import Path


def replace_once(path, old, new, label):
    file = Path(path)
    text = file.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"expected one {label} tail in {path}, found {count}")
    file.write_text(text.replace(old, new, 1))

replace_once(
    "PluginEditTool/native/src/plugin/RadiclePluginMasterMutation.cpp",
    '''        fo4::write_plugin_file(mutated, target->path);
        target->plugin = std::move(mutated);
        {
            std::error_code ec;
            target->loadedTime = std::filesystem::last_write_time(target->path, ec);
        }
        rebuildGraphLocked();
''',
    '''        stagePluginMutationLocked(*target, std::move(mutated));
''',
    "AddMasters",
)

replace_once(
    "PluginEditTool/native/src/plugin/RadiclePluginCompact.cpp",
    '''        fo4::write_plugin_file(mutated, loaded->path);
        loaded->plugin = std::move(mutated);
        std::error_code ec;
        loaded->loadedTime = std::filesystem::last_write_time(loaded->path, ec);
        rebuildGraphLocked();
''',
    '''        stagePluginMutationLocked(*loaded, std::move(mutated));
''',
    "CompactToEsl",
)

replace_once(
    "PluginEditTool/native/src/plugin/RadiclePluginCell.cpp",
    '''        fo4::write_plugin_file(mutated, patch->path);
        patch->plugin = std::move(mutated);
        {
            std::error_code ec;
            patch->loadedTime = std::filesystem::last_write_time(patch->path, ec);
        }
        rebuildGraphLocked();
''',
    '''        stagePluginMutationLocked(*patch, std::move(mutated));
''',
    "SetPlacedReferenceTransform",
)

maintenance = Path("PluginEditTool/native/src/plugin/RadiclePluginMaintenance.cpp")
text = maintenance.read_text()

nested = '''                fo4::write_plugin_file(mutated, target->path);
                target->plugin = std::move(mutated);
                std::error_code ec;
                target->loadedTime = std::filesystem::last_write_time(target->path, ec);
                rebuildGraphLocked();
'''
if text.count(nested) != 1:
    raise SystemExit(f"expected one nested CopyAsNew tail, found {text.count(nested)}")
text = text.replace(nested, '''                stagePluginMutationLocked(*target, std::move(mutated));
''', 1)

top = '''            fo4::write_plugin_file(mutated, target->path);
            target->plugin = std::move(mutated);
            std::error_code ec;
            target->loadedTime = std::filesystem::last_write_time(target->path, ec);
            rebuildGraphLocked();
'''
if text.count(top) != 1:
    raise SystemExit(f"expected one top-level CopyAsNew tail, found {text.count(top)}")
text = text.replace(top, '''            stagePluginMutationLocked(*target, std::move(mutated));
''', 1)

clean = '''            mutated.original_range.clear();
            fo4::write_plugin_file(mutated, loaded->path);
            loaded->plugin = std::move(mutated);
            std::error_code ec;
            loaded->loadedTime = std::filesystem::last_write_time(loaded->path, ec);
            rebuildGraphLocked();
'''
if text.count(clean) != 1:
    raise SystemExit(f"expected one CleanPlugin tail, found {text.count(clean)}")
text = text.replace(clean, '''            mutated.original_range.clear();
            stagePluginMutationLocked(*loaded, std::move(mutated));
''', 1)

itm = '''            if (removed != 0) {
                fo4::write_plugin_file(mutated, loaded->path);
                loaded->plugin = std::move(mutated);
                std::error_code ec;
                loaded->loadedTime = std::filesystem::last_write_time(loaded->path, ec);
                rebuildGraphLocked();
            }
'''
if text.count(itm) != 1:
    raise SystemExit(f"expected one RemoveIdenticalToMaster tail, found {text.count(itm)}")
text = text.replace(itm, '''            if (removed != 0) {
                stagePluginMutationLocked(*loaded, std::move(mutated));
            }
''', 1)
maintenance.write_text(text)
PY

git diff --check
production_changed="$(git diff --name-only | sort)"
production_expected="$(printf '%s\n' \
  'PluginEditTool/native/src/plugin/RadiclePluginCell.cpp' \
  'PluginEditTool/native/src/plugin/RadiclePluginCompact.cpp' \
  'PluginEditTool/native/src/plugin/RadiclePluginMaintenance.cpp' \
  'PluginEditTool/native/src/plugin/RadiclePluginMasterMutation.cpp' | sort)"
test "$production_changed" = "$production_expected"

cmake -S PluginEditTool/native -B build/fo4ide-174b-core-green -G Ninja -DCMAKE_BUILD_TYPE=Debug -DFO4IDE_BUILD_TESTS=ON -DFO4IDE_BUILD_HTTP=OFF
cmake --build build/fo4ide-174b-core-green --target fo4ide_test_plugin_backend

binary=build/fo4ide-174b-core-green/bin/fo4ide_test_plugin_backend
"$binary" "Native AddMasters*"
"$binary" "Native CompactToEsl*"
"$binary" "Native cell transform*"
"$binary" "Native CleanPlugin*"
"$binary" "Native RemoveIdenticalToMaster*"
"$binary" "Native CopyAsNewRecord*"
"$binary" "Native CopyAsNewRecord dispatches a contained INFO to the dialogue primitive"

git rm .github/workflows/174b-core-writers.yml .github/174b-core-green.sh
git config user.name "NomadsReach"
git config user.email "144523850+NomadsReach@users.noreply.github.com"
git add \
  PluginEditTool/native/src/plugin/RadiclePluginCell.cpp \
  PluginEditTool/native/src/plugin/RadiclePluginCompact.cpp \
  PluginEditTool/native/src/plugin/RadiclePluginMaintenance.cpp \
  PluginEditTool/native/src/plugin/RadiclePluginMasterMutation.cpp
git commit -m "feat: defer remaining core writer persistence"
git push origin HEAD:work/174b-deferred-mutations
