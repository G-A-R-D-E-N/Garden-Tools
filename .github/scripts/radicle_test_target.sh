#!/usr/bin/env bash
set -u
case "${1:-}" in
  ci/pr-*) ;;
  *) echo all; exit 0 ;;
esac
git fetch --quiet --depth=1 origin refs/heads/ci/nomad/dev 2>/dev/null || { echo all; exit 0; }
changed=$(git diff --no-renames --name-only FETCH_HEAD HEAD) || { echo all; exit 0; }
[ -n "$changed" ] || { echo all; exit 0; }
target=
while IFS= read -r file; do
  case "$file" in
    Radicle/tests/*.cpp)
      lists=$(grep -lxF "  ${file#Radicle/}" Radicle/cmake/tests_core.cmake Radicle/cmake/tests_papyrus.cmake Radicle/cmake/tests_flash.cmake Radicle/cmake/tests_assets.cmake) || lists=
      [ "$(printf '%s\n' "$lists" | grep -c .)" = 1 ] || { echo all; exit 0; }
      found=${lists##*/tests_}
      found=fo4_tests_${found%.cmake} ;;
    *) echo all; exit 0 ;;
  esac
  if [ -z "$target" ]; then
    target=$found
  elif [ "$target" != "$found" ]; then
    echo all; exit 0
  fi
done <<< "$changed"
echo "$target"
