import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
import threading


def main(executable):
    with tempfile.TemporaryDirectory(prefix="fo4ide-mcp-") as temporary:
        root = Path(temporary)
        profile = root / "profiles" / "Smoke"
        for folder in (profile, root / "mods", root / "overwrite", root / "Stock Folder" / "Data"):
            folder.mkdir(parents=True)
        (root / "ModOrganizer.ini").write_text("selected_profile=Smoke\n", encoding="utf-8")
        (profile / "plugins.txt").write_text("", encoding="utf-8")
        (profile / "modlist.txt").write_text("", encoding="utf-8")
        environment = dict(os.environ, APPDATA=str(root / "config"), XDG_CONFIG_HOME=str(root / "config"))
        with (root / "stderr.log").open("w+", encoding="utf-8") as errors:
            process = subprocess.Popen(
                [executable, "--mcp", "--mo2", str(root)],
                stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=errors,
                text=True, encoding="utf-8", env=environment, cwd=root,
            )
            deadline = threading.Timer(60, process.kill)
            deadline.start()
            request_id = 0

            def rpc(method, params=None):
                nonlocal request_id
                request_id += 1
                process.stdin.write(json.dumps({"jsonrpc": "2.0", "id": request_id, "method": method, "params": params or {}}) + "\n")
                process.stdin.flush()
                line = process.stdout.readline()
                assert line, "Native MCP exited or timed out before responding"
                response = json.loads(line)
                assert response["id"] == request_id, response
                assert "error" not in response, response
                return response["result"]

            def call(name, arguments=None, failed=False):
                result = rpc("tools/call", {"name": name, "arguments": arguments or {}})
                assert result.get("isError", False) == failed, result
                return result["content"][0]["text"]

            try:
                assert rpc("initialize")["serverInfo"]["name"] == "fo4editor"
                names = {tool["name"] for tool in rpc("tools/list")["tools"]}
                required = {"list_plugins", "open_plugin", "create_plugin", "create_record", "get_record", "set_field", "save_plugin", "discard_plugin", "list_record_types", "list_records"}
                assert required <= names, f"Missing native plugin tools: {sorted(required - names)}"
                status = json.loads(call("get_mo2_status"))
                assert status["loaded"] and status["profile"] == "Smoke", status
                plugin = "NativeMcpSmoke.esp"
                def listed():
                    rows = json.loads(call("list_plugins"))
                    return next(row for row in rows if row.get("Key") == plugin or row.get("Name") == plugin)
                call("create_plugin", {"plugin": plugin})
                path = root / "overwrite" / plugin
                baseline = path.read_bytes()
                assert listed()["Pending"] is False
                created = call("create_record", {"plugin": plugin, "type": "EYES", "payload": {"editorId": "McpSmokeEyes", "fullText": "Original Eyes", "icon": "Textures\\Eyes\\smoke.dds"}})
                form_key = json.loads(created)["formKey"]
                assert path.read_bytes() == baseline
                assert "EYES" in call("list_record_types", {"plugin": plugin})
                assert "McpSmokeEyes" in call("list_records", {"type": "EYES"})
                record = {"form_key": form_key}
                def read_record():
                    matrix = json.loads(call("get_record", record))
                    full = next(row for row in matrix["Rows"] if row["Field"] == "Subrecords\\FULL[0]")["Values"][-1]
                    return bytes.fromhex(full.split(": ", 1)[1]).rstrip(b"\x00").decode("utf-8")

                assert read_record() == "Original Eyes"
                assert "Original Eyes" in call("list_records_summary", {"plugin": plugin, "type": "EYES"})
                worklist = json.loads(call("list_plugin_worklist", {"plugin": plugin, "filter": "added"}))
                assert worklist["Total"] == 1 and worklist["Rows"][0]["FormKey"] == form_key, worklist
                assert worklist["Rows"][0]["Kind"] == "Added" and worklist["Rows"][0]["OverrideDepth"] == 0, worklist
                call("list_plugin_worklist", {"plugin": plugin, "filter": "everything"}, failed=True)
                edit = {"plugin": plugin, "form_key": form_key, "path": "EYES\\Full", "value": "Saved Eyes"}
                call("set_field", edit)
                pending = read_record()
                assert "Saved Eyes" in pending and path.read_bytes() == baseline
                assert listed()["Pending"] is True
                dirty = json.loads(call("get_mo2_status"))
                assert plugin in dirty.get("pendingPlugins", []), dirty
                call("set_field", dict(edit, value=123), failed=True)
                assert read_record() == pending and path.read_bytes() == baseline
                call("set_field", dict(edit, path="EYES\\MissingField"), failed=True)
                assert read_record() == pending and path.read_bytes() == baseline
                call("save_plugin", {"plugin": plugin})
                saved = path.read_bytes()
                assert saved != baseline
                assert listed()["Pending"] is False
                clean = json.loads(call("get_mo2_status"))
                assert plugin not in clean.get("pendingPlugins", []), clean
                before = json.loads(call("get_mo2_status"))
                plugins_txt = profile / "plugins.txt"
                plugins_txt.write_text("*" + plugin + "\n", encoding="utf-8")
                later = plugins_txt.stat().st_mtime + 2
                os.utime(plugins_txt, (later, later))
                stale = json.loads(call("get_mo2_status"))
                assert stale["stale"] is True, stale
                assert stale["staleLoadOrder"] is True, stale
                assert stale.get("staleConfig") is not True, stale
                assert stale["pluginCount"] == before["pluginCount"], stale
                call("reload_mo2_profile")
                after = json.loads(call("get_mo2_status"))
                assert after["stale"] is False, after
                assert after.get("staleLoadOrder") is not True, after
                assert after["pluginCount"] == before["pluginCount"] + 1, after
                assert plugin in call("list_plugins")
                assert read_record() == "Saved Eyes"
                call("set_field", dict(edit, value="Open Guard Eyes"))
                assert read_record() == "Open Guard Eyes"
                assert path.read_bytes() == saved
                call("open_plugin", {"path": str(path)}, failed=True)
                assert read_record() == "Open Guard Eyes"
                assert path.read_bytes() == saved
                call("reload_mo2_profile", failed=True)
                assert read_record() == "Open Guard Eyes"
                assert path.read_bytes() == saved
                blocked = json.loads(call("get_mo2_status"))
                assert blocked["pluginCount"] == after["pluginCount"], blocked
                assert blocked["stale"] is False, blocked
                assert plugin in blocked.get("pendingPlugins", []), blocked
                call("reload_mo2_profile", {"discard_pending": True})
                assert read_record() == "Saved Eyes"
                call("set_field", dict(edit, value="Discard These Eyes"))
                assert read_record() == "Discard These Eyes"
                assert path.read_bytes() == saved
                assert listed()["Pending"] is True
                call("discard_plugin", {"plugin": plugin})
                assert read_record() == "Saved Eyes"
                assert path.read_bytes() == saved
                assert listed()["Pending"] is False
            finally:
                deadline.cancel()
                process.stdin.close()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
                process.stdout.close()
        for arguments in (["--mo2"], ["--mo2", str(root / "missing")]):
            result = subprocess.run([executable, "--mcp", *arguments], input="", capture_output=True, text=True, env=environment, cwd=root, timeout=30)
            assert result.returncode != 0, f"Invalid MO2 startup succeeded: {arguments}"
        print("PASS: native MCP startup, tools, memory-only edits, dirty-open guard, rejected edits, save, reload and discard")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("executable")
    parser.add_argument("--build-dir")
    parser.add_argument("--cmake", default="cmake")
    arguments = parser.parse_args()
    if arguments.build_dir:
        subprocess.run([arguments.cmake, "--build", arguments.build_dir, "--config", "Release", "--target", "fo4ide", "--parallel", "8"], check=True)
    main(str(Path(arguments.executable).resolve()))
