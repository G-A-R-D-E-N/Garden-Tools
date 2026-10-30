# FO4RecordEditor on Linux

The Linux build is the same native C++23 program as the Windows one: no .NET, and no Wine for the
editor itself. The one difference is the window. Windows hosts the UI in an embedded WebView2 window.
Linux has no embedded window, so the program serves the UI on a loopback port and opens it in your
default browser.

Status: CI builds the native host on Linux and runs its tests (job `native-linux` in
`.github/workflows/native-ci.yml`). This page was written from the source and the packaging script.
The `.deb` and the desktop launch described here were not run on a Linux desktop for it, so report
anything that does not match.

## Install

```bash
sudo apt install ./packaging/linux/out/fo4recordeditor_1.0.0_amd64.deb
fo4recordeditor
```

The package puts the program and its `web/dist` UI bundle in `/opt/fo4recordeditor`, a launcher at
`/usr/bin/fo4recordeditor` and a menu entry under Development. It depends only on the C and C++
runtime libraries and zlib. It recommends `zenity` (file and folder pickers) and `wine` (for the
Windows-only helper programs, see below).

## Starting it

With no arguments the program listens on a free port of `127.0.0.1` and opens the page with
`xdg-open` (or `gio open`). It also prints the address:

```text
FO4IDE native server: http://127.0.0.1:41823/#/main?t=0123456789abcdef0123456789abcdef
```

If no browser appeared, open that exact address. The `t=` value is an access token generated on every
start. The server refuses backend calls that do not carry it, and the page keeps it for that browser
tab only. Closing the tab does not stop the program: press Ctrl+C in the terminal that started it, or
run `pkill FO4RecordEditor` (a launch from the menu has no terminal).

| Flag | Meaning |
|---|---|
| `--browser` | Serve the UI and open it in the default browser. This is the default on Linux. |
| `--headless`, `--serve` | Serve the UI but open nothing. Use the printed address. |
| `--port N` | Listen on port N (0 to 65535) instead of a free port. 0 means any free port. |
| `--web-root <folder>` | Serve the UI from this folder. The default is `web/dist` beside the program, then the source tree's `web/dist`. |
| `--mo2 <instance>` | Load an MO2 instance before starting: the folder that holds `ModOrganizer.ini` and `profiles/`. |
| `--mcp` | Run as a stdio MCP server with no UI. See "MCP mode". |
| `--health` | Print a JSON status and exit. |
| `--window` | Windows only. On Linux it fails with `NativeWindowUnsupported` and exit code 5. |

The server always binds `127.0.0.1`; there is no option to change that. Exit codes: 2 means `web/dist`
was not found (give `--web-root`), 3 means the server could not start (a port that is taken, for
example), 5 is `--window`, and 6 means a bad `--port` or `--mo2` argument or an MO2 instance that
failed to load.

Settings live in `$XDG_CONFIG_HOME/FO4RecordEditor/settings.json` (`~/.config/FO4RecordEditor/` when
that variable is unset). The log is `logs/FO4IDE.log` in the same folder, 5 MiB per file with the
current file plus three rotated ones.

## Talking to it directly

| Endpoint | What it does |
|---|---|
| `POST /rpc` | `{"target":"app","method":"GetHealth","args":[]}` returns `{"ok":true,"value":...}`, or `{"ok":false,"error":"...","type":"..."}`. Needs the `X-FO4IDE-Token` header and `Content-Type: application/json`. |
| `GET /events` | Server-sent events, the stream the page listens to. Needs the token header. |
| `GET /api/health` | Liveness and the registered target names. No token. |

```bash
curl -s -H 'X-FO4IDE-Token: <the t= value>' -H 'Content-Type: application/json' \
  -d '{"target":"app","method":"GetHealth","args":[]}' http://127.0.0.1:<port>/rpc
```

## MCP mode

```bash
fo4recordeditor --mcp --mo2 "/home/you/Modlists/MyList"
```

This is the same contract as on Windows; [../MCP_SETUP.md](../MCP_SETUP.md) has the tool list, the
write-safety rules and the client configuration. Paths are ordinary Linux paths. The process exits
when stdin closes, so a one-shot run needs every request ready first:

```bash
python3 - <<'PY' > /tmp/in.jsonl
import json
for m in [
 {"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05",
   "capabilities":{},"clientInfo":{"name":"probe","version":"1"}}},
 {"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"list_plugins","arguments":{}}},
]: print(json.dumps(m))
PY
fo4recordeditor --mcp --mo2 "/home/you/Modlists/MyList" < /tmp/in.jsonl > /tmp/out.jsonl
```

## MO2 instances made under Wine or Proton

MO2 running under Wine writes Windows-style paths into `ModOrganizer.ini`. The profile reader turns
them into Linux paths for the game path, the base directory and the overwrite directory: backslashes
become slashes, `Z:\...` becomes `/...`, and any other drive letter is looked up in
`$WINEPREFIX/drive_<letter>` (`~/.wine` when `WINEPREFIX` is unset). For a Proton prefix, set
`WINEPREFIX` to the prefix folder before starting. The mods folder is always `mods/` inside the
instance.

## What needs Wine, and what does not

Reading, editing, conflict resolution, saving, cells, BA2 reading, material files and the built-in
Papyrus engine are all native. These helper programs are Windows executables:

- `PapyrusCompiler.exe` (the Creation Kit compiler; the built-in engine does not need it)
- `Archive2.exe` (`archive_pack`)
- `xWMAEncode.exe`, `BmlFuzEncode.exe`, `BmlFuzDecode.exe` (audio and FUZ tools)
- `Texconvx64.exe`, if you only have the Windows build

The program runs any helper whose file name ends in `.exe` through `wine` (the call fails if wine is missing). Set
`FO4RE_WINE` to name a different wine binary, or to an empty value to turn that off. `ffmpeg` is used
natively from `PATH`, and `texconv` is also looked up on `PATH` without the `.exe`.
Where each helper is looked for is listed in [../MCP_SETUP.md](../MCP_SETUP.md). How the Windows
tools cope with Linux paths under Wine has not been checked.

## Gotchas

- **A launch without a default browser handler shows nothing.** The program logs `Could not open
  browser` and keeps running. Start it from a terminal, open the printed address yourself, and stop it
  with Ctrl+C.
- **The pickers need `zenity` or `kdialog`.** Without either, the Open MO2 button does nothing, as if
  you had canceled the dialog. A folder without a `profiles/` subfolder is treated the same way.
  Start with `--mo2 <instance>` instead.
- **"Load Env" cannot detect a game install.** It reloads the MO2 instance saved in `settings.json`
  and fails with `EnvironmentUnavailable` when there is none. Open an MO2 instance once (the picker
  or `--mo2`) and it is remembered.
- **Open the printed address, not a bare `http://127.0.0.1:<port>/`.** A tab without the access token
  loads the page but every backend call is refused with HTTP 401.
- **The `PAPYRUS_BASE_IMPORTS` list is separated by `;`, also on Linux.** A `:` separated list is read
  as one path that does not exist, so it is ignored.

## Building the .deb

```bash
packaging/linux/build-deb.sh [version]     # default 1.0.0
```

Needs a C++23 compiler, CMake, Ninja, Python 3, Node with `npm`, `dpkg-deb` and `fakeroot`, and
network access on the first configure (the C++ dependencies are fetched at pinned versions). It
builds the React bundle (`npm ci` when `web/node_modules` is missing), builds the `fo4ide` target in
Release without tests, stages the tree and writes the package to `packaging/linux/out/`. It empties
that `out/` folder first, so every run is a full C++ build.

Things that will bite:

- **Staging happens in a temporary directory (`mktemp -d`), deliberately.** The repo often lives on an
  exfat/ntfs mount, which reports every file as mode 777, and `dpkg-deb` refuses a `DEBIAN/`
  directory with those permissions.
- **`node_modules` is platform-specific.** Vite 8 uses rolldown, whose native binding is per-OS. A
  `node_modules` installed on Windows has only `@rolldown/binding-win32-x64-msvc` and the build dies
  with `Cannot find module '../rolldown-binding.linux-x64-gnu.node'`. Run `npm ci` on the machine
  you are building on. Installing the Linux binding prunes the Windows one, so a Windows build
  afterwards needs its own `npm ci`.
- **`npm install` needs symlinks, which the ntfs3 kernel driver cannot create.** If the repo sits
  on a mount reported as `type ntfs3` (check `mount`; `ntfs-3g` userspace supports symlinks, the
  kernel `ntfs3` driver does not), npm dies on the `node_modules/.bin` links with
  `ENOENT: no such file or directory, symlink '../@babel/parser/bin/babel-parser.js' -> ...`. Work
  around it with `--no-bin-links` and call the tools by their real paths:
  `npm ci --no-bin-links`, then `node node_modules/typescript/bin/tsc -b`,
  `node node_modules/vite/bin/vite.js build` (dev: `node node_modules/vite/bin/vite.js`),
  `node node_modules/vitest/vitest.mjs run` for the web tests. On an ext4 mount, or on Windows,
  where npm writes `.bin` shims instead of symlinks, the plain `npm ci && npm run build` works as
  written. The native CMake build already avoids the same symlink problem for its shared library, so
  only npm needs the flag.

## Building without the package

```bash
cd web && npm ci && npm run build          # the UI bundle
cd ..
cmake -S native -B build/fo4ide -G Ninja -DCMAKE_BUILD_TYPE=Release -DFO4IDE_BUILD_TESTS=OFF
cmake --build build/fo4ide --target fo4ide
build/fo4ide/bin/FO4RecordEditor
```

Run from the build tree, the program finds the checkout's `web/dist` on its own. To build and run
the tests instead, see "Testing" in [ARCHITECTURE.md](ARCHITECTURE.md). Tests that read a real Fallout
4 `Data` folder look at `FO4_DATA_DIR` and skip when it is unset (for example in
`native/tests/unit/RadiclePluginSoundSemanticRealTests.cpp`), so a pass without it does not mean they ran.
