#!/usr/bin/env bash
set -euo pipefail

VERSION="${1:-1.0.0}"
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="$HERE/out"
NATIVE_BUILD="$OUT/native"
STAGE_ROOT="$(mktemp -d)"
STAGE="$STAGE_ROOT/fo4recordeditor_${VERSION}_amd64"
trap 'rm -rf "$STAGE_ROOT"' EXIT

echo ">> building the React bundle"
cd "$ROOT/web"
[ -d node_modules ] || npm ci
npm run build

echo ">> building the native C++ host"
rm -rf "$OUT"
cmake -S "$ROOT/native" -B "$NATIVE_BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DFO4IDE_BUILD_TESTS=OFF \
  -DFO4IDE_BUILD_HTTP=ON
cmake --build "$NATIVE_BUILD" --target fo4ide

test -x "$NATIVE_BUILD/bin/FO4RecordEditor"
test -d "$ROOT/web/dist"

echo ">> staging the package tree"
install -d "$STAGE/DEBIAN" \
           "$STAGE/opt/fo4recordeditor/web" \
           "$STAGE/usr/bin" \
           "$STAGE/usr/share/applications" \
           "$STAGE/usr/share/icons/hicolor/scalable/apps" \
           "$STAGE/usr/share/doc/fo4recordeditor"

install -m 0755 "$NATIVE_BUILD/bin/FO4RecordEditor" \
                "$STAGE/opt/fo4recordeditor/FO4RecordEditor"
cp -r "$ROOT/web/dist" "$STAGE/opt/fo4recordeditor/web/"

install -m 0755 "$HERE/fo4recordeditor.sh" "$STAGE/usr/bin/fo4recordeditor"
install -m 0644 "$HERE/fo4recordeditor.desktop" "$STAGE/usr/share/applications/fo4recordeditor.desktop"
install -m 0644 "$ROOT/web/public/favicon.svg" \
                "$STAGE/usr/share/icons/hicolor/scalable/apps/fo4recordeditor.svg"
install -m 0644 "$ROOT/LICENSE" "$STAGE/usr/share/doc/fo4recordeditor/copyright"
install -m 0644 "$ROOT/THIRD_PARTY_NOTICES.md" "$STAGE/usr/share/doc/fo4recordeditor/"
install -m 0644 "$ROOT/README.md" "$STAGE/usr/share/doc/fo4recordeditor/README.md"

INSTALLED_KB="$(du -ks "$STAGE" | cut -f1)"

cat > "$STAGE/DEBIAN/control" <<EOF
Package: fo4recordeditor
Version: ${VERSION}
Section: devel
Priority: optional
Architecture: amd64
Maintainer: PRISMA User Interface Framework <noreply@users.noreply.github.com>
Installed-Size: ${INSTALLED_KB}
Depends: libc6 (>= 2.34), libgcc-s1, libstdc++6, zlib1g
Recommends: zenity, wine
Homepage: https://github.com/PRISMA-USER-INTERFACE-FRAMEWORK/PluginEditTool
Description: Fallout 4 plugin editor and conflict resolver
 A native C++23 editor for Fallout 4 plugins: browse and edit records, resolve
 conflicts across a Mod Organizer 2 load order, inspect cells in 3D, read BA2
 archives, compile and decompile Papyrus, and expose the same tools over MCP.
 .
 Plugin parsing, mutation, load-order analysis, archive handling, and Papyrus
 services run in the native Radicle/FO4IDE backend. No .NET runtime is required.
 .
 The interface is served on a loopback port by the local native process and
 opened in the default browser; nothing is sent anywhere. A few optional helper
 programs are Windows-only, so Wine may still be useful for panels that invoke
 those external tools.
EOF

cat > "$STAGE/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
if [ "$1" = "configure" ]; then
    command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database -q || true
    command -v gtk-update-icon-cache >/dev/null 2>&1 && \
        gtk-update-icon-cache -q /usr/share/icons/hicolor 2>/dev/null || true
fi
EOF
chmod 0755 "$STAGE/DEBIAN/postinst"

echo ">> building the .deb"
DEB="$OUT/fo4recordeditor_${VERSION}_amd64.deb"
fakeroot dpkg-deb --build --root-owner-group "$STAGE" "$DEB" > /dev/null

echo
echo "Built: $DEB"
ls -lh "$DEB" | awk '{print "Size:  " $5}'
echo
echo "Install with:  sudo apt install \"$DEB\""