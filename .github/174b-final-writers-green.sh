#!/usr/bin/env bash
set -euo pipefail

base=203f27e7

git cat-file -e "${base}^{commit}"
rm -rf /tmp/ponytail-174b-masters-test-align
git clone --depth=1 https://github.com/DietrichGebert/ponytail.git /tmp/ponytail-174b-masters-test-align
test -f /tmp/ponytail-174b-masters-test-align/AGENTS.md

python3 - <<'PY'
from pathlib import Path
path = Path("PluginEditTool/native/tests/unit/RadiclePluginMastersTests.cpp")
text = path.read_text()
old = '    CHECK(reorder.rfind("Wrote ", 0) == 0);\n'
new = '    CHECK_FALSE(reorder.empty());\n'
if text.count(old) != 1:
    raise SystemExit(f"masters legacy response assertion count {text.count(old)}")
path.write_text(text.replace(old, new, 1))
PY

git diff --check
actual="$(git diff --name-only | sort)"
expected="PluginEditTool/native/tests/unit/RadiclePluginMastersTests.cpp"
test "$actual" = "$expected"

cmake -S PluginEditTool/native -B build/fo4ide-174b-masters-test-align -G Ninja -DCMAKE_BUILD_TYPE=Debug -DFO4IDE_BUILD_TESTS=ON -DFO4IDE_BUILD_HTTP=OFF
cmake --build build/fo4ide-174b-masters-test-align --target fo4ide_test_plugin_backend
binary=build/fo4ide-174b-masters-test-align/bin/fo4ide_test_plugin_backend
set +e
"$binary" "Masters reorder writes immediately and preserves logical FormLinks" > /tmp/masters-align-red.txt 2>&1
status=$?
set -e
cat /tmp/masters-align-red.txt
test "$status" -ne 0
grep -F 'CHECK( fo4::test::original_bytes(fo4::read_plugin_file(patch)) == beforeSave )' /tmp/masters-align-red.txt
grep -F 'CHECK( reorder.find("SavePlugin") != std::string::npos )' /tmp/masters-align-red.txt
if grep -F 'CHECK_FALSE( reorder.empty() )' /tmp/masters-align-red.txt; then
  exit 1
fi

git rm .github/workflows/174b-core-writers.yml .github/174b-final-writers-green.sh
git config user.name "NomadsReach"
git config user.email "144523850+NomadsReach@users.noreply.github.com"
git add PluginEditTool/native/tests/unit/RadiclePluginMastersTests.cpp
git commit -m "test: align deferred master reorder response"
git push origin HEAD:work/174b-deferred-mutations
