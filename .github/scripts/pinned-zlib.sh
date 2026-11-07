#!/usr/bin/env bash
set -euo pipefail

cmake_file=PluginEditTool/native/CMakeLists.txt
zlib_rev=$(awk '{ sub(/\r$/, "") } armed && $1 == "GIT_TAG" { print $2 } { armed = ($0 ~ /GIT_REPOSITORY[ \t]+https:\/\/github\.com\/madler\/zlib\.git/) }' "$cmake_file")
if [[ ! "$zlib_rev" =~ ^[0-9a-f]{40}$ ]]; then
  echo "::error::expected exactly one 40-hex GIT_TAG after the zlib GIT_REPOSITORY in $cmake_file"
  exit 1
fi

cache="${RUNNER_TOOL_CACHE:-$HOME/.cache}/garden-src/zlib/$zlib_rev"
if [ -f "$cache/.installed" ]; then
  echo "Reusing cached zlib $zlib_rev"
else
  echo "Fetching zlib $zlib_rev into $cache"
  mkdir -p "$(dirname "$cache")"
  tmp="$cache.tmp.$$"
  rm -rf "$tmp"
  git init -q "$tmp"
  for attempt in 1 2 3 4 5; do
    if git -C "$tmp" -c http.lowSpeedLimit=1000 -c http.lowSpeedTime=30 fetch --depth 1 https://github.com/madler/zlib.git "$zlib_rev"; then
      break
    fi
    if [ "$attempt" -eq 5 ]; then
      rm -rf "$tmp"
      echo "::error::could not fetch zlib $zlib_rev from GitHub after 5 attempts"
      exit 1
    fi
    echo "::warning::zlib fetch attempt $attempt failed, retrying"
    sleep $((attempt * 15))
  done
  git -C "$tmp" checkout -q --detach FETCH_HEAD
  if [ "$(git -C "$tmp" rev-parse HEAD)" != "$zlib_rev" ]; then
    rm -rf "$tmp"
    echo "::error::zlib checkout is not $zlib_rev"
    exit 1
  fi
  touch "$tmp/.installed"
  if ! mv -T "$tmp" "$cache" 2>/dev/null; then
    rm -rf "$tmp"
    if [ ! -f "$cache/.installed" ]; then
      echo "::error::zlib cache $cache is incomplete"
      exit 1
    fi
  fi
fi

mkdir -p build
cp -a "$cache" build/zlib-src
rm -rf build/zlib-src/.git
echo "ZLIB_SRC=$PWD/build/zlib-src" >> "$GITHUB_ENV"
