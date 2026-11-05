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
                required = {"list_plugins", "open_plugin", "create_plugin", "create_record", "get_record", "set_field", "save_plugin", "discard_plugin", "list_record_types", "list_records", "set_quest_fragment", "remove_quest_fragment", "get_quest_fragments", "get_quest_script", "edit_quest_script"}
                assert required <= names, f"Missing native plugin tools: {sorted(required - names)}"
                status = json.loads(call("get_mo2_status"))
                assert status["loaded"] and status["profile"] == "Smoke", status
                conditions = json.loads(call("get_condition_functions"))
                assert "GetIsID" in conditions and conditions["GetIsID"], conditions
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
                    matrix = json.loads(call("get_conflict_matrix", record))
                    full = next(row for row in matrix["Rows"] if row["Field"] == "Subrecords\\FULL[0]")["Values"][-1]
                    return bytes.fromhex(full.split(": ", 1)[1]).rstrip(b"\x00").decode("utf-8")

                assert read_record() == "Original Eyes"
                assert "Original Eyes" in call("list_records_summary", {"plugin": plugin, "type": "EYES"})
                worklist = json.loads(call("list_plugin_worklist", {"plugin": plugin, "filter": "added"}))
                assert worklist["Total"] == 1 and worklist["Rows"][0]["FormKey"] == form_key, worklist
                assert worklist["Rows"][0]["Kind"] == "Added" and worklist["Rows"][0]["OverrideDepth"] == 0, worklist
                page = json.loads(call("list_plugin_worklist", {"plugin": plugin, "type": "EYES", "limit": 1, "offset": 1}))
                assert page["Total"] == 1 and page["Offset"] == 1 and page["Rows"] == [], page
                call("list_plugin_worklist", {"plugin": plugin, "filter": "everything"}, failed=True)
                call("list_plugin_worklist", {"plugin": plugin, "type": "INVALID"}, failed=True)
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
                gap = "NativeMcpGap.esp"
                call("create_plugin", {"plugin": gap})
                gap_path = root / "overwrite" / gap
                gap_base = gap_path.read_bytes()
                tools = {tool["name"]: tool for tool in rpc("tools/list")["tools"]}
                assert len(tools) >= 160, f"Expected at least 160 MCP tools, got {len(tools)}"
                for name in ("add_master", "add_masters", "set_script_value", "get_condition_functions", "create_placed_object","create_interior_cell", "get_leveled_list", "set_leveled_list_settings", "edit_leveled_list_entry", "get_npc", "edit_npc_entries", "get_faction", "edit_faction_entries", "get_weapon_damage_types", "get_perk_effects", "set_perk_effects", "get_perk_links", "set_perk_next", "set_perk_sound", "set_perk_swf", "add_sound_file", "remove_sound_file", "add_package_procedure", "remove_package_procedure", "set_sound_base", "create_sound", "add_script", "remove_script", "add_script_property", "remove_script_property", "add_alias_script", "remove_alias_script", "add_alias_script_property", "remove_alias_script_property", "set_alias_script_value", "get_magic_effects", "add_magic_effect", "set_magic_effect", "remove_magic_effect", "get_conflicts", "get_record_details", "get_conflict_matrix", "delete_record", "copy_as_override", "get_conditions", "set_conditions", "set_components", "describe_element", "add_element", "strings_list_tables", "graph_core_palette", "script_read_script", "archive_extract_file", "get_placed_references"):
                    assert name in tools, f"Missing MCP tool: {name}"
                placed_schema = tools["create_placed_object"]["inputSchema"]
                assert placed_schema["properties"]["x"]["type"] == "number", placed_schema
                assert placed_schema["properties"]["persistent"]["type"] == "boolean", placed_schema
                assert set(placed_schema["required"]) == {"plugin", "cell", "baseObject", "x", "y", "z"}, placed_schema
                assert placed_schema["additionalProperties"] is False, placed_schema
                leveled_schema = tools["set_leveled_list_settings"]["inputSchema"]
                assert leveled_schema["properties"]["flags"]["type"] == "integer", leveled_schema
                assert set(leveled_schema["required"]) == {"plugin", "form_key", "chance_none", "max_count", "flags"}, leveled_schema
                perk_item = tools["set_perk_effects"]["inputSchema"]["properties"]["effects"]["items"]
                assert perk_item["required"] == ["type"], perk_item
                assert {"value", "value2", "form", "actorValue", "text", "data"} <= set(perk_item["properties"]["parameters"]["properties"]), perk_item
                assert perk_item["properties"]["tabs"]["items"]["properties"]["conditions"]["items"]["properties"]["function"]["type"] == "string", perk_item
                placed_args = {"plugin": gap, "cell": "000800:NativeMcpGap.esp", "baseObject": "000801:NativeMcpGap.esp", "x": 1, "y": 2, "z": 3}
                assert call("create_placed_object", {"plugin": gap, "cell": "000800:NativeMcpGap.esp", "baseObject": "000801:NativeMcpGap.esp", "x": 1, "y": 2}, failed=True) == "Missing argument: z"
                assert call("create_placed_object", dict(placed_args, x="1"), failed=True) == "x must be number"
                assert call("create_placed_object", dict(placed_args, persistent=1), failed=True) == "persistent must be boolean"
                assert call("create_placed_object", dict(placed_args, foo=1), failed=True) == "Unknown argument: foo"
                leveled_args = {"plugin": gap, "form_key": "000800:NativeMcpGap.esp", "chance_none": 1, "max_count": 1}
                assert call("set_leveled_list_settings", dict(leveled_args, flags=1.5), failed=True) == "flags must be integer"
                assert call("set_leveled_list_settings", dict(leveled_args, flags=256), failed=True).startswith("InvalidLeveledListSettings: ")
                assert call("create_interior_cell", {"plugin": gap, "editor_id": ""}, failed=True).startswith("InteriorCellArgumentsMissing: ")
                assert call("edit_leveled_list_entry", {"plugin": gap, "form_key": "000800:NativeMcpGap.esp", "op": "bogus", "index": 0}, failed=True).startswith("InvalidLeveledListOperation: ")
                assert call("clean_unused_masters", {"plugin": gap, "Apply": True}, failed=True) == "Unknown argument: Apply"
                assert call("clean_unused_masters", {"plugin": gap}, failed=True) == "Missing argument: apply"
                assert call("clean_unused_masters", {"plugin": gap, "apply": "yes"}, failed=True) == "apply must be boolean"
                assert "Missing argument" not in json.dumps(rpc("tools/call", {"name": "search_cells", "arguments": {"query": "NativeMcpGap"}})), "search_cells must not require limit"
                assert gap_path.read_bytes() == gap_base
                outer = json.loads(call("create_record", {"plugin": gap, "type": "LVLI", "payload": {"editorId": "McpGapOuter", "chanceNone": 5, "maxCount": 2, "flags": 1}}))["formKey"]
                inner = json.loads(call("create_record", {"plugin": gap, "type": "LVLI", "payload": {"editorId": "McpGapInner", "chanceNone": 0, "maxCount": 0, "flags": 0}}))["formKey"]
                eyes = json.loads(call("create_record", {"plugin": gap, "type": "EYES", "payload": {"editorId": "McpGapEyes", "fullText": "Eyes", "icon": "Textures\\Eyes\\gap.dds"}}))["formKey"]
                cell = json.loads(call("create_interior_cell", {"plugin": gap, "editor_id": "McpGapRoom"}))["formKey"]
                assert call("create_interior_cell", {"plugin": gap, "editor_id": "mcpgaproom"}, failed=True).startswith("InteriorCellCreateFailed: ")
                placed = json.loads(call("create_placed_object", {"plugin": gap, "cell": cell, "baseObject": eyes, "x": 1.5, "y": 2, "z": 3}))
                assert placed["ok"] is True and placed["formKey"], placed
                listed_outer = json.loads(call("get_leveled_list", {"form_key": outer}))
                assert listed_outer["chanceNone"] == 5 and listed_outer["maxCount"] == 2 and listed_outer["flags"] == 1 and listed_outer["entries"] == [], listed_outer
                call("set_leveled_list_settings", {"plugin": gap, "form_key": outer, "chance_none": 9, "max_count": 3, "flags": 2})
                call("edit_leveled_list_entry", {"plugin": gap, "form_key": outer, "op": "add", "index": 0, "value": {"level": 1, "item": inner, "count": 1, "chanceNone": 0}})
                edited = json.loads(call("get_leveled_list", {"form_key": outer}))
                assert edited["chanceNone"] == 9 and edited["maxCount"] == 3 and edited["flags"] == 2, edited
                assert edited["entries"][0]["item"] == inner, edited
                assert gap_path.read_bytes() == gap_base
                call("save_plugin", {"plugin": gap})
                assert gap_path.read_bytes() != gap_base
                assert json.loads(call("get_leveled_list", {"form_key": outer})) == edited
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
        empty = root / "empty-instance"
        empty.mkdir()
        requests = [
            {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}},
            {"jsonrpc": "2.0", "id": 2, "method": "tools/call", "params": {"name": "get_mo2_status", "arguments": {}}},
        ]
        result = subprocess.run([executable, "--mcp", "--mo2", str(empty)], input="".join(json.dumps(request) + "\n" for request in requests), capture_output=True, text=True, env=environment, cwd=root, timeout=30)
        assert result.returncode == 0, result.stderr
        responses = {response["id"]: response for response in map(json.loads, result.stdout.splitlines())}
        failed = responses[2]["result"]
        assert failed["isError"] is True and "MO2 load failed" in failed["content"][0]["text"], failed
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
