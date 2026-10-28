#include "McpTools.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

bool registerMcpTool(fo4ide::mcp::Registry& registry,
                     std::shared_ptr<fo4ide::mcp::Tool> tool,
                     const std::shared_ptr<spdlog::logger>& log) {
    auto registered = registry.registerTool(std::move(tool));
    if (registered) return true;
    log->error("MCP setup failed: {}", registered.error().message);
    return false;
}

nlohmann::json objectSchema(nlohmann::json properties,
                            nlohmann::json required = nlohmann::json::array()) {
    nlohmann::json schema{{"type", "object"}, {"properties", std::move(properties)}};
    if (!required.empty()) schema["required"] = std::move(required);
    schema["additionalProperties"] = false;
    return schema;
}

fo4ide::mcp::ToolResult rpcToolText(const fo4ide::rpc::Response& response) {
    if (!response.ok) {
        const auto prefix = response.type.empty() ? std::string{} : response.type + ": ";
        return {prefix + response.error, true};
    }
    if (response.value.is_string()) return {response.value.get<std::string>(), false};
    if (response.value.is_null()) return {"", false};
    return {response.value.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace), false};
}

struct PluginArgument {
    const char* name;
    const char* type;
    bool optional{false};
};

bool registerPluginTools(fo4ide::mcp::Registry& registry,
                         const fo4ide::rpc::Dispatcher& rpc,
                         const std::shared_ptr<spdlog::logger>& log) {
    using namespace fo4ide::mcp;
    const auto add = [&](std::string name, std::string description,
                         std::string target, std::string method,
                         std::vector<PluginArgument> fields) {
        nlohmann::json properties = nlohmann::json::object();
        nlohmann::json required = nlohmann::json::array();
        std::vector<std::string> order;
        for (const auto& field : fields) {
            properties[field.name] = {{"type", field.type}};
            if (!field.optional) required.push_back(field.name);
            order.emplace_back(field.name);
        }
        ToolDefinition definition{std::move(name), std::move(description),
                                  objectSchema(properties, required)};
        definition.inputSchema["additionalProperties"] = false;
        auto bridge = std::make_shared<RpcTool>(definition, rpc, std::move(target),
                                                std::move(method), std::move(order));
        return registerMcpTool(registry, std::make_shared<FunctionTool>(
            std::move(definition), [bridge, fields, properties](const nlohmann::json& args) {
                if (!args.is_object()) return ToolResult{"Arguments must be an object", true};
                for (const auto& [name, value] : args.items()) {
                    static_cast<void>(value);
                    if (!properties.contains(name)) return ToolResult{"Unknown argument: " + name, true};
                }
                for (const auto& field : fields) {
                    const auto value = args.find(field.name);
                    if (value == args.end()) {
                        if (field.optional) continue;
                        return ToolResult{std::string("Missing argument: ") + field.name, true};
                    }
                    const std::string_view type(field.type);
                    const bool valid = (type == "string" && value->is_string()) ||
                                       (type == "object" && value->is_object()) ||
                                       (type == "integer" && value->is_number_integer()) ||
                                       (type == "number" && value->is_number()) ||
                                       (type == "boolean" && value->is_boolean());
                    if (!valid) return ToolResult{std::string(field.name) + " must be " + field.type, true};
                }
                return bridge->call(args);
            }), log);
    };
    if (!add("list_plugins",
             "List loaded plugins. Pending=true means unsaved in-memory edits; check this before save_plugin, discard_plugin, or reload_mo2_profile",
             "appInterop", "GetPlugins", {})) return false;
    if (!add("open_plugin", "Open an existing plugin by absolute path", "backend", "OpenPlugin",
             {{"path", "string"}})) return false;
    if (!add("create_plugin", "Create an empty ESP in the trusted MO2 overwrite folder; later edits require save_plugin", "backend", "CreatePlugin",
             {{"plugin", "string"}})) return false;
    if (!add("create_record", "Create a typed record in memory. Supported types: ARMA, ARMO, CELL, EXPL, EYES, FACT, FLST, GLOB, HAZD, LVLI, LVLN, LVSP, MESG, MGEF, PROJ, QUST, RACE, SPEL. Payload fields are validated by the native backend. FLST, GLOB, MESG, MGEF, QUST and SPEL take an editorId and build every required subrecord from the schema defaults", "backend", "CreateRecord",
             {{"plugin", "string"}, {"type", "string"}, {"payload", "object"}})) return false;
    if (!add("create_placed_object",
             "Create a placed reference (REFR) over a base object in a CELL this plugin owns and return its formKey. cell and baseObject are FormKeys. The CELL must be owned by this plugin. In an exterior CELL the reference must be non-persistent and positioned inside the CELL's grid. persistent defaults to true (persistent list), false puts it in the temporary list. initially_disabled defaults to false. Rotations are in radians and default to 0",
             "backend", "CreatePlacedObject",
             {{"plugin", "string"}, {"cell", "string"}, {"baseObject", "string"},
              {"x", "number"}, {"y", "number"}, {"z", "number"},
              {"rotX", "number", true}, {"rotY", "number", true}, {"rotZ", "number", true},
              {"persistent", "boolean", true}, {"initially_disabled", "boolean", true}})) return false;
    if (!add("create_interior_cell",
             "Create an interior CELL in this plugin with the given editor ID and return its formKey. Refused when the editor ID is empty or matches a CELL in this plugin, ignoring ASCII case",
             "backend", "CreateInteriorCell",
             {{"plugin", "string"}, {"editor_id", "string"}})) return false;
    if (!add("get_record", "Get record fields and plugin versions by FormKey, including pending edits", "backend", "GetConflictMatrix",
             {{"form_key", "string"}})) return false;
    if (!add("get_record_fields", "Get the field paths and values for one plugin's record version", "backend", "GetRecordTree",
             {{"plugin", "string"}, {"form_key", "string"}})) return false;
    if (!add("list_record_types", "List the record signatures and counts in a plugin", "backend", "GetPluginRecordTypes",
             {{"plugin", "string"}})) return false;
    if (!add("list_records", "List winning records of a signature across the loaded order; use filter, limit and offset to narrow results", "backend", "GetRecordsOfType",
             {{"type", "string"}, {"filter", "string", true}, {"limit", "integer", true}, {"offset", "integer", true}})) return false;
    if (!add("list_records_summary", "Read a plugin's record grid with decoded values and editable column paths", "backend", "GetRecordsGrid",
             {{"plugin", "string"}, {"type", "string"}, {"limit", "integer", true}, {"offset", "integer", true}})) return false;
    if (!add("list_plugin_worklist",
             "List every record a plugin adds or overrides in one call, with override depth, winner and later overriding plugins. filter is all, added, overrides, winning, losing or conflicted; type narrows to one record signature",
             "backend", "GetPluginWorklist",
             {{"plugin", "string"}, {"filter", "string", true}, {"type", "string", true},
              {"limit", "integer", true}, {"offset", "integer", true}})) return false;
    if (!add("search_records", "Search the loaded order by text and optional record signature", "backend", "SearchRecords",
             {{"query", "string"}, {"type", "string", true}})) return false;
    if (!add("set_field", "Set a field in memory using an editable column path from list_records_summary; value is text. Call save_plugin to persist", "backend", "SetField",
             {{"plugin", "string"}, {"form_key", "string"}, {"path", "string"}, {"value", "string"}})) return false;
    if (!add("save_plugin", "Save pending changes to the plugin's existing disk path. Use list_plugins Pending to see which plugins are dirty first", "backend", "SavePatch",
             {{"plugin", "string"}})) return false;
    if (!add("discard_plugin", "Discard pending in-memory edits for one plugin without writing disk. Use list_plugins Pending to see which plugins are dirty first", "backend", "DiscardPlugin",
             {{"plugin", "string"}})) return false;
    if (!add("list_masters", "List a plugin's declared masters", "masters", "List",
             {{"plugin", "string"}})) return false;
    if (!add("get_leveled_list", "Get a leveled list (LVLI, LVLN or LVSP) by FormKey: chance none, max count, flags, use global and entries from the winning version, or from plugin's version when plugin is given. edit_leveled_list_entry indexes the named plugin's version", "backend", "GetLeveledList",
             {{"form_key", "string"}, {"plugin", "string", true}})) return false;
    if (!add("set_leveled_list_settings", "Set a leveled list's chance none, max count and flags bytes (0-255) in memory. Call save_plugin to persist", "backend", "SetLeveledListSettings",
             {{"plugin", "string"}, {"form_key", "string"}, {"chance_none", "integer"}, {"max_count", "integer"}, {"flags", "integer"}})) return false;
    if (!add("edit_leveled_list_entry", "Add (insert at index), set or remove one leveled list entry in memory. value is {level, item, count, chanceNone, extra}; required for add and set. index is into the named plugin's version of the list. Call save_plugin to persist", "backend", "EditLeveledListEntry",
             {{"plugin", "string"}, {"form_key", "string"}, {"op", "string"}, {"index", "integer"}, {"value", "object", true}})) return false;
    if (!add("get_npc", "Get an NPC_ record's factions, perks and inventory by FormKey from the winning version, or from plugin's version when plugin is given. edit_npc_entries indexes the named plugin's version", "backend", "GetNpc",
             {{"form_key", "string"}, {"plugin", "string", true}})) return false;
    if (!add("edit_npc_entries", "Add, set or remove one NPC_ entry in memory. list is factions, perks or inventory; index is required for set and remove and indexes the named plugin's version; value is required for add and set. Call save_plugin to persist", "backend", "EditNpcEntries",
             {{"plugin", "string"}, {"form_key", "string"}, {"list", "string"}, {"op", "string"}, {"index", "integer", true}, {"value", "object", true}})) return false;
    if (!add("get_faction", "Get a FACT record's relations and ranks by FormKey from the winning version, or from plugin's version when plugin is given. edit_faction_entries indexes the named plugin's version", "backend", "GetFaction",
             {{"form_key", "string"}, {"plugin", "string", true}})) return false;
    if (!add("edit_faction_entries", "Add, set or remove one FACT relation or rank in memory. list is relations or ranks; index is required for set and remove and indexes the named plugin's version; value is required for add and set. Call save_plugin to persist", "backend", "EditFactionEntries",
             {{"plugin", "string"}, {"form_key", "string"}, {"list", "string"}, {"op", "string"}, {"index", "integer", true}, {"value", "object", true}})) return false;
    if (!add("get_weapon_damage_types", "Get a WEAP record's damage types by FormKey from the winning version (read-only)", "backend", "GetWeaponDamageTypes",
             {{"form_key", "string"}})) return false;
    return true;
}

}

bool registerNativeMcpTools(fo4ide::mcp::Registry& registry,
                            const fo4ide::rpc::Dispatcher& rpc,
                            const std::shared_ptr<spdlog::logger>& log) {
    using fo4ide::mcp::FunctionTool;
    using fo4ide::mcp::RpcTool;
    using fo4ide::mcp::ToolDefinition;

    const auto add = [&](ToolDefinition definition,
                         std::string target,
                         std::string method,
                         std::vector<std::string> argumentOrder = {}) {
        return registerMcpTool(
            registry,
            std::make_shared<RpcTool>(
                std::move(definition), rpc, std::move(target), std::move(method), std::move(argumentOrder)),
            log);
    };

    const auto emptySchema = objectSchema(nlohmann::json::object());
    if (!add({"native_status", "Report the native FO4IDE host and backend capability status", emptySchema},
             "app", "GetHealth")) return false;
    if (!add({"get_mo2_status",
              "Report the loaded MO2 instance/profile. pendingPlugins lists dirty plugins. staleConfig=true if ModOrganizer.ini changed; staleLoadOrder=true if plugins.txt, modlist.txt, or loadorder.txt changed; stale is either. Check pendingPlugins before reload_mo2_profile",
              emptySchema},
             "appInterop", "GetMo2Status")) return false;
    if (!add({"reload_mo2_profile",
              "Reload enabled plugins from the currently configured MO2 profile. Check list_plugins or get_mo2_status pendingPlugins first. Fails if any plugin has pending edits unless discard_pending is true",
              objectSchema({
                  {"discard_pending",
                   {{"type", "boolean"},
                    {"description",
                     "Discard all pending in-memory plugin edits before reloading. Defaults to false."}}}
              })},
             "appInterop", "ReloadMo2Profile",
             {"discard_pending"})) return false;
    if (!add({"get_settings", "Return the current FO4IDE settings", emptySchema},
             "settings", "GetSettings")) return false;

    const auto papyrusCompileSchema = objectSchema(
        {
            {"source", {{"type", "string"},
                        {"description", "A .psc file path, or a folder of scripts (with all=true)."}}},
            {"output", {{"type", "string"}, {"description", "Output dir for .pex (default: the source folder)."}}},
            {"imports", {{"type", "string"}, {"description", "Extra import roots, semicolon-separated."}}},
            {"flags", {{"type", "string"}, {"description", "Flags file (default Institute_Papyrus_Flags.flg)."}}},
            {"all", {{"type", "boolean"}, {"description", "Compile every .psc in the source folder."}}},
            {"optimize", {{"type", "boolean"}, {"description", "Optimize (-op). Creation Kit engine only."}}},
            {"release", {{"type", "boolean"}, {"description", "Release build: strips DebugOnly/BetaOnly calls."}}},
            {"engine", {{"type", "string"},
                        {"description", "'auto' (default), 'builtin', or 'creationkit'."}}},
            {"debug_info", {{"type", "boolean"}, {"description", "Emit line numbers and property groups (default true)."}}},
            {"compiler_path", {{"type", "string"}, {"description", "Override path to PapyrusCompiler.exe."}}}
        },
        nlohmann::json::array({"source"}));
    if (!add({
            "compile_papyrus",
            "Compile Papyrus source .psc -> .pex with the built-in engine (auto-detects a single file "
            "or a folder; 'creationkit' shells out to PapyrusCompiler.exe). Returns a 'RESULT: X "
            "succeeded, Y failed' summary.",
            papyrusCompileSchema},
            "papyrus", "Compile",
            {"source", "output", "imports", "flags", "all", "optimize", "release",
             "compiler_path", "engine", "debug_info"})) return false;
    if (!add({
            "decompile_papyrus",
            "Decompile compiled Papyrus .pex -> source .psc with the built-in decompiler (single file "
            "or folder; assembly=true emits a bytecode listing). Namespaced scripts are written into "
            "namespace subfolders so they recompile as-is.",
            objectSchema({
                {"source", {{"type", "string"}, {"description", "A .pex file, or a folder of .pex files."}}},
                {"output", {{"type", "string"}, {"description", "Output dir when writing."}}},
                {"assembly", {{"type", "boolean"}, {"description", "Emit bytecode disassembly instead of source."}}},
                {"write", {{"type", "boolean"}, {"description", "Write .psc/.pas files instead of returning text inline."}}}
            }, nlohmann::json::array({"source"}))},
            "papyrus", "Decompile", {"source", "output", "assembly", "write"})) return false;
    if (!add({
            "papyrus_check",
            "Check Papyrus SOURCE (.psc) with the built-in front end (no Creation Kit): syntax, then "
            "name resolution and type checking. Reports 'file(line,col): error CODE: message' per "
            "problem; files whose parent/import/type is not on the roots are counted separately.",
            objectSchema({
                {"source", {{"type", "string"}, {"description", "A .psc file path, or a folder of scripts."}}},
                {"semantic", {{"type", "boolean"}, {"description", "Resolve names and check types too (default true)."}}},
                {"imports", {{"type", "string"}, {"description", "Extra source roots, semicolon-separated."}}}
            }, nlohmann::json::array({"source"}))},
            "papyrus", "Check", {"source", "semantic", "imports"})) return false;
    if (!add({
            "papyrus_function_lookup",
            "Look up a Papyrus function in the offline Creation Kit Wiki mirror.",
            objectSchema({
                {"function", {{"type", "string"}, {"description", "Function name, e.g. 'GetBaseObject'."}}},
                {"script", {{"type", "string"}, {"description", "Owning script (optional but disambiguates)."}}}
            }, nlohmann::json::array({"function"}))},
            "papyrus", "LookupFunction", {"script", "function"})) return false;
    if (!add({
            "papyrus_script_info",
            "Get a Papyrus script's overview (Extends, Global/Member Functions, Events) from the "
            "offline Creation Kit Wiki mirror.",
            objectSchema({{"script", {{"type", "string"},
                                         {"description", "Script name, e.g. 'ObjectReference' or 'Actor'."}}}},
                         nlohmann::json::array({"script"}))},
            "papyrus", "LookupScriptInfo", {"script"})) return false;

    const auto nifPathSchema = objectSchema(
        {{"path", {{"type", "string"}, {"description", "Path to the NIF file"}}}},
        nlohmann::json::array({"path"}));
    if (!add({"nif_inspect", "Inspect a NIF, read in-process (no niftool needed)", nifPathSchema},
             "nif", "Inspect", {"path"})) return false;
    if (!add({"nif_verify", "Verify a NIF through the native niftool wrapper", nifPathSchema},
             "nif", "Verify", {"path"})) return false;
    if (!add({"nif_tree", "Return the NIF block tree through the native niftool wrapper", nifPathSchema},
             "nif", "Tree", {"path"})) return false;
    if (!add({"nif_geometry", "Return geometry data, read in-process (no niftool needed)", nifPathSchema},
             "nif", "Geo", {"path"})) return false;

    if (!add({
            "audio_convert_to_xwm",
            "Convert one audio/video file or a folder to Fallout 4 XWM",
            objectSchema({
                {"source", {{"type", "string"}}},
                {"output", {{"type", "string"}}},
                {"bitrateBps", {{"type", "integer"}, {"minimum", 1}}}
            }, nlohmann::json::array({"source"}))},
            "audio", "ConvertToXwm", {"source", "output", "bitrateBps"})) return false;
    if (!add({
            "audio_convert_from_xwm",
            "Decode an XWM file to WAV or another ffmpeg-supported format",
            objectSchema({
                {"source", {{"type", "string"}}},
                {"output", {{"type", "string"}}},
                {"targetExt", {{"type", "string"}, {"default", "wav"}}}
            }, nlohmann::json::array({"source"}))},
            "audio", "ConvertFromXwm", {"source", "output", "targetExt"})) return false;
    if (!add({
            "audio_make_fuz",
            "Create a Fallout 4 FUZ package from audio/XWM and an optional LIP file",
            objectSchema({
                {"audioSource", {{"type", "string"}}},
                {"lipPath", {{"type", "string"}}},
                {"fuzOutput", {{"type", "string"}}},
                {"noLip", {{"type", "boolean"}, {"default", false}}}
            }, nlohmann::json::array({"audioSource", "fuzOutput"}))},
            "audio", "MakeFuz", {"audioSource", "lipPath", "fuzOutput", "noLip"})) return false;
    if (!add({
            "audio_extract_fuz",
            "Extract XWM/LIP data from a Fallout 4 FUZ package",
            objectSchema({
                {"fuzPath", {{"type", "string"}}},
                {"xwmOutput", {{"type", "string"}}},
                {"lipOutput", {{"type", "string"}}},
                {"alsoWav", {{"type", "boolean"}, {"default", false}}}
            }, nlohmann::json::array({"fuzPath"}))},
            "audio", "ExtractFuz", {"fuzPath", "xwmOutput", "lipOutput", "alsoWav"})) return false;

    if (!add({
            "material_inspect",
            "Inspect a Fallout 4 BGSM or BGEM material through the native codec",
            objectSchema({{"path", {{"type", "string"}}}}, nlohmann::json::array({"path"}))},
            "material", "Inspect", {"path"})) return false;

    ToolDefinition materialSet{
        .name = "material_set_fields",
        .description = "Set one or more named BGSM/BGEM material fields through the native codec",
        .inputSchema = objectSchema({
            {"path", {{"type", "string"}}},
            {"fields", {{"type", "object"}, {"additionalProperties", {{"type", "string"}}}}},
            {"output", {{"type", "string"}}}
        }, nlohmann::json::array({"path", "fields"}))
    };
    if (!registerMcpTool(
            registry,
            std::make_shared<FunctionTool>(std::move(materialSet), [&rpc](const nlohmann::json& args) {
                if (!args.is_object() || !args.contains("path") || !args.at("path").is_string() ||
                    !args.contains("fields") || !args.at("fields").is_object()) {
                    return fo4ide::mcp::ToolResult{"path and fields are required", true};
                }
                const auto output = args.value("output", std::string{});
                const auto response = rpc.invoke({
                    "material",
                    "SetFields",
                    nlohmann::json::array({args.at("path"), args.at("fields").dump(), output})
                });
                return rpcToolText(response);
            }),
            log)) return false;

    ToolDefinition archivePack{
        .name = "archive_pack",
        .description = "Pack folders into a Fallout 4 BA2 through Archive2.exe",
        .inputSchema = objectSchema({
            {"sources", {{"type", "array"}, {"items", {{"type", "string"}}}, {"minItems", 1}}},
            {"output", {{"type", "string"}}},
            {"format", {{"type", "string"}, {"enum", nlohmann::json::array({"General", "DDS"})}}},
            {"rootDir", {{"type", "string"}}},
            {"compress", {{"type", "boolean"}, {"default", true}}}
        }, nlohmann::json::array({"sources", "output", "rootDir"}))
    };
    if (!registerMcpTool(
            registry,
            std::make_shared<FunctionTool>(std::move(archivePack), [&rpc](const nlohmann::json& args) {
                if (!args.is_object() || !args.contains("sources") || !args.at("sources").is_array()) {
                    return fo4ide::mcp::ToolResult{"sources is required", true};
                }
                const auto response = rpc.invoke({
                    "archive",
                    "Pack",
                    nlohmann::json::array({
                        args.at("sources").dump(),
                        args.value("output", std::string{}),
                        args.value("format", std::string("General")),
                        args.value("rootDir", std::string{}),
                        args.value("compress", true)
                    })
                });
                return rpcToolText(response);
            }),
            log)) return false;

    return registerPluginTools(registry, rpc, log);
}
