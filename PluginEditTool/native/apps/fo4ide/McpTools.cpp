#include "McpTools.hpp"

#include "McpCatalog.hpp"

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
    const char* itemsSchema{nullptr};
};

constexpr const char* k_perk_effect_items =
    R"({"type":"object","properties":{"type":{"type":"string","enum":["quest_stage","ability","entry_point"],)"
    R"("description":"Effect kind; quest_stage uses quest and questStage, ability uses ability, entry_point uses entryPoint and function"},)"
    R"("rank":{"type":"integer","minimum":0,"maximum":255},"priority":{"type":"integer","minimum":0,"maximum":255},)"
    R"("quest":{"type":"string","description":"quest_stage only: a loaded QUST FormKey"},)"
    R"("questStage":{"type":"integer","minimum":0,"maximum":65535},)"
    R"("ability":{"type":"string","description":"ability only: a loaded SPEL FormKey"},)"
    R"("entryPoint":{"type":"integer","minimum":0,"maximum":255},"function":{"type":"integer","minimum":0,"maximum":255},)"
    R"("tabs":{"type":"array","description":"Condition tabs; an effect without tabs clears them","items":{"type":"object",)"
    R"("properties":{"runOn":{"type":"integer"},"conditions":{"type":"array","items":{"type":"object","properties":{)"
    R"("function":{"type":"string"},"operator":{"type":"string"},"value":{"type":"number"},"compareGlobal":{"type":"string"},)"
    R"("param1":{"type":["integer","string"]},"param2":{"type":["integer","string"]},"runOn":{"type":["string","integer"]},)"
    R"("reference":{"type":"string"},"flags":{"type":"string"},"cis1":{"type":"string"},"cis2":{"type":"string"}}}}}}},)"
    R"("parameters":{"type":"object","description":"Use data (hex) or the typed fields for parameters.type, not both. value: EPFT 1, 2 (float pair) and 8; value2: EPFT 2 float pair; form: EPFT 3, 4, 5; actorValue: EPFT 8 or EPFT 2 with an actor-value function; text: EPFT 6",)"
    R"("properties":{"type":{"type":"integer","minimum":0,"maximum":255},"entryId":{"type":"integer","minimum":0,"maximum":65535},)"
    R"("scriptFlags":{"type":"integer","minimum":0,"maximum":65535},"data":{"type":"string","description":"EPFD bytes as hex"},)"
    R"("buttonLabel":{"type":"string"},"buttonLabelId":{"type":"integer","minimum":0},)"
    R"("value":{"type":"number"},"value2":{"type":"number"},)"
    R"("form":{"type":"string","description":"FormKey or editor ID of a loaded LVLI for EPFT 3, or SPEL for EPFT 4 and 5"},)"
    R"("actorValue":{"type":"string","description":"FormKey or editor ID of a loaded AVIF"},"text":{"type":"string"}}}},)"
    R"("required":["type"]})";

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
            if (field.itemsSchema) properties[field.name]["items"] = nlohmann::json::parse(field.itemsSchema);
            if (!field.optional) required.push_back(field.name);
            order.emplace_back(field.name);
        }
        ToolDefinition definition{std::move(name), std::move(description),
                                  objectSchema(properties, required)};
        definition.inputSchema["additionalProperties"] = false;
        const auto schema = definition.inputSchema;
        auto bridge = std::make_shared<RpcTool>(definition, rpc, std::move(target),
                                                std::move(method), std::move(order));
        return registerMcpTool(registry, std::make_shared<FunctionTool>(
            std::move(definition), [bridge, schema](const nlohmann::json& args) {
                if (const auto error = argumentError(schema, args)) return ToolResult{*error, true};
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
    if (!add("create_record", "Create a typed record in memory. Supported types: ARMA, ARMO, CELL, EXPL, EYES, FACT, FLST, GLOB, HAZD, LVLI, LVLN, LVSP, MESG, MGEF, PACK, PERK, PROJ, QUST, RACE, SPEL. Payload fields are validated by the native backend. PACK takes an editorId, a location object (type reference, cell, near_package_start, near_editor_location or near_self; form, a FormKey required for reference and cell; radius, default 0), speed (walk, jog, run or fast_walk, default jog) and the booleans preferPreferredPath, useSoftRadius and ignoreMarkerOrientation, and builds one Travel procedure with its four data inputs. FLST, GLOB, MESG, MGEF, QUST and SPEL take an editorId and build every required subrecord from the schema defaults. QUST also takes optional name (inline text in a non-localized plugin; in a localized plugin it becomes a new string ID in the staged string tables, so call strings_save to persist it, and language picks the table language, default en), flags, priority and type. PERK takes an editorId and optional full, desc, trait, level, ranks, playable, effects (the set_perk_effects array), conditions (the array shape set_conditions takes), nextPerk (a PERK FormKey), sound (an SNDR FormKey), swf (text) and language (localized plugins: the table language, default en). In a localized plugin full and desc are written as new string IDs in the staged string tables; call strings_save to persist them", "backend", "CreateRecord",
             {{"plugin", "string"}, {"type", "string"}, {"payload", "object"}})) return false;
    if (!add("release_memory", "Page the process working set out to the OS after a large load, save or reload. Plugin data stays loaded; the next access pages it back in. drop_conflict_graph also drops the cached conflict/reference graph (the largest optional allocation; it rebuilds on the next conflict or reference query). Use when the editor is idle and memory use matters", "backend", "ReleaseMemory",
             {{"drop_conflict_graph", "boolean", true}})) return false;
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
    if (!add("get_record", "Get one record's compact details: FormKey, signature, EditorID, winning plugin and override count. Per-plugin versions come from get_record_plugin_matrix", "backend", "GetRecordDetails",
             {{"form_key", "string"}})) return false;
    if (!add("get_conflict_matrix", "Get the conflict matrix for one FormKey: every plugin version and its conflict rows. Large payloads are possible on big load orders; prefer get_record for a compact view", "backend", "GetConflictMatrix",
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
    if (!add("add_master",
             "Declare one master on a plugin, fresh or existing, in memory; call save_plugin to persist. Use it before authoring records whose links live in that master. master_file is a plugin file name, not a path. Masters are load-order significant: an already-declared master that loads after the new one is refused with MasterOrder. Declaring a master that is already declared is a no-op. A plugin cannot declare itself, an unloaded full or light-unknown master is refused, and protected or game-Data plugins are refused",
             "backend", "AddMasters",
             {{"plugin", "string"}, {"master_file", "string"}})) return false;
    if (!add("add_masters",
             "Declare several masters on a plugin in one call, in memory; call save_plugin to persist. master_file is a comma-separated list of plugin file names and the declaration order is the order given. Same refusals as add_master, and a refusal leaves the plugin unchanged",
             "backend", "AddMasters",
             {{"plugin", "string"}, {"master_file", "string"}})) return false;
    if (!add("get_magic_effects",
             "Read a record's magic effect list. The record must be SPEL, ALCH, ENCH or INGR. Returns count and effects with index, effect FormKey, magnitude, area and duration",
             "backend", "GetMagicEffects",
             {{"plugin", "string"}, {"form_key", "string"}})) return false;
    if (!add("add_magic_effect",
             "Append one effect to a record's magic effect list in memory and return its index. The record must be SPEL, ALCH, ENCH or INGR; effect must be an MGEF FormKey; magnitude is a float, area and duration are integers. Call save_plugin to persist",
             "backend", "AddMagicEffect",
             {{"plugin", "string"}, {"form_key", "string"}, {"effect", "string"},
              {"magnitude", "number"}, {"area", "integer"}, {"duration", "integer"}})) return false;
    if (!add("set_magic_effect",
             "Replace one effect of a record's magic effect list in memory by index. The record must be SPEL, ALCH, ENCH or INGR; effect must be an MGEF FormKey. Call save_plugin to persist",
             "backend", "SetMagicEffect",
             {{"plugin", "string"}, {"form_key", "string"}, {"index", "integer"},
              {"effect", "string"}, {"magnitude", "number"}, {"area", "integer"},
              {"duration", "integer"}})) return false;
    if (!add("remove_magic_effect",
             "Remove one effect from a record's magic effect list in memory by index. The record must be SPEL, ALCH, ENCH or INGR. Call save_plugin to persist",
             "backend", "RemoveMagicEffect",
             {{"plugin", "string"}, {"form_key", "string"}, {"index", "integer"}})) return false;
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
    if (!add("add_sound_file", "Add one file name to an SNDR sound descriptor in memory. The file is an ANAM entry inserted after the last one and before the descriptors; the result names the new index. Call save_plugin to persist", "backend", "AddSoundFile",
             {{"plugin", "string"}, {"form_key", "string"}, {"file", "string"}})) return false;
    if (!add("remove_sound_file", "Remove one SNDR file name by its ANAM index in memory. Call save_plugin to persist", "backend", "RemoveSoundFile",
             {{"plugin", "string"}, {"form_key", "string"}, {"file_index", "integer"}})) return false;
    if (!add("set_sound_base", "Set or remove the BNAM base-descriptor link of an SNDR in memory. Only an AutoWeapon descriptor carries that link; an empty target removes it. Call save_plugin to persist", "backend", "SetSoundBase",
             {{"plugin", "string"}, {"form_key", "string"}, {"target", "string", true}})) return false;
    if (!add("create_sound", "Create a SOUN sound marker or an FSTS footstep set with proved defaults in memory; signature must be SOUN or FSTS. Call save_plugin to persist", "backend", "CreateSound",
             {{"plugin", "string"}, {"signature", "string"}, {"editor_id", "string"}})) return false;
    if (!add("add_package_procedure", "Add a leaf procedure node under a PACK procedure-tree branch in memory. parent_index addresses the decoded tree node; the result names the new node index. Procedures that need data inputs get default Bool inputs created first. A templated package that needs them, and a removal that would orphan an input, are refused. Call save_plugin to persist", "backend", "AddPackageProcedure",
             {{"plugin", "string"}, {"form_key", "string"}, {"parent_index", "integer"}, {"procedure", "string"}})) return false;
    if (!add("remove_package_procedure", "Remove one leaf procedure node from a PACK procedure tree in memory, recomputing the branch counts. Call save_plugin to persist", "backend", "RemovePackageProcedure",
             {{"plugin", "string"}, {"form_key", "string"}, {"node_index", "integer"}})) return false;
    if (!add("add_script", "Add a script to a record in memory, creating an empty VMAD when the record has none. script is the Papyrus script name; flags is the optional script flags byte and index the optional script-list position; the result names the new index. Call save_plugin to persist", "backend", "AddScript",
             {{"plugin", "string"}, {"form_key", "string"}, {"script", "string"}, {"flags", "integer", true}, {"index", "integer", true}})) return false;
    if (!add("remove_script", "Remove one script and its properties from a record's VMAD in memory by script index. Call save_plugin to persist", "backend", "RemoveScript",
             {{"plugin", "string"}, {"form_key", "string"}, {"script_index", "integer"}})) return false;
    if (!add("add_script_property", "Add a property to a script in memory. type is the VMAD property type byte (1 object, 2 string, 3 int, 4 float, 5 bool, 7 struct, 11-15 arrays, 17 struct array); flags is the optional property flags byte and index the optional property-list position. Set its value afterwards with set_script_value. Call save_plugin to persist", "backend", "AddScriptProperty",
             {{"plugin", "string"}, {"form_key", "string"}, {"script_index", "integer"}, {"name", "string"}, {"type", "integer"}, {"flags", "integer", true}, {"index", "integer", true}})) return false;
    if (!add("set_script_value", "Set one VMAD script property value in memory. kind selects the typed setter: object (value is a FormKey, alias optional), string, int, float or bool (value is text for int/float/bool). element selects an element of an array property and defaults to 0. Call save_plugin to persist", "backend", "SetScriptValue",
             {{"plugin", "string"}, {"form_key", "string"}, {"script_index", "integer"},
              {"property_index", "integer"}, {"kind", "string"}, {"value", "string", true},
              {"alias", "integer", true}, {"element", "integer", true}})) return false;
    if (!add("remove_script_property", "Remove one property from a script in memory by property index. Call save_plugin to persist", "backend", "RemoveScriptProperty",
             {{"plugin", "string"}, {"form_key", "string"}, {"script_index", "integer"}, {"property_index", "integer"}})) return false;
    if (!add("set_info_fragment", "Bind or replace one INFO script fragment in memory. index is 0 OnBegin or 1 OnEnd; the VMAD and its fragment section are created when absent. Call save_plugin to persist", "backend", "SetInfoFragment",
             {{"plugin", "string"}, {"info_form_key", "string"}, {"index", "integer"}, {"script_name", "string"}, {"function_name", "string"}})) return false;
    if (!add("add_info_fragment", "Add one INFO script fragment at index in memory; refuses when that index is already bound and when the record has no fragment section yet (use set_info_fragment to create it). index is 0 OnBegin or 1 OnEnd. Call save_plugin to persist", "backend", "AddInfoFragment",
             {{"plugin", "string"}, {"info_form_key", "string"}, {"index", "integer"}, {"script_name", "string"}, {"function_name", "string"}})) return false;
    if (!add("remove_info_fragment", "Remove the INFO script fragment at index in memory and clear its flag bit. Call save_plugin to persist", "backend", "RemoveInfoFragment",
             {{"plugin", "string"}, {"info_form_key", "string"}, {"index", "integer"}})) return false;
    if (!add("set_scene_phase_fragment", "Bind or replace a scene phase script fragment in memory. phase_flag is 1 for start and 2 for completion. Call save_plugin to persist", "backend", "SetScenePhaseFragment",
             {{"plugin", "string"}, {"scene_form_key", "string"}, {"phase_index", "integer"}, {"phase_flag", "integer"}, {"script_name", "string"}, {"function_name", "string"}})) return false;
    if (!add("add_scene_phase_fragment", "Add a scene phase script fragment in memory at phase_index and phase_flag; refuses when that pair is already bound and when the scene has no fragment section yet (use set_scene_phase_fragment to create it). phase_flag is 1 for start and 2 for completion. Call save_plugin to persist", "backend", "AddScenePhaseFragment",
             {{"plugin", "string"}, {"scene_form_key", "string"}, {"phase_index", "integer"}, {"phase_flag", "integer"}, {"script_name", "string"}, {"function_name", "string"}})) return false;
    if (!add("remove_scene_phase_fragment", "Remove a scene phase script fragment in memory by phase_index and phase_flag (1 start, 2 completion). Call save_plugin to persist", "backend", "RemoveScenePhaseFragment",
             {{"plugin", "string"}, {"scene_form_key", "string"}, {"phase_index", "integer"}, {"phase_flag", "integer"}})) return false;
    if (!add("get_weapon_damage_types", "Get a WEAP record's damage types by FormKey from the winning version (read-only)", "backend", "GetWeaponDamageTypes",
             {{"form_key", "string"}})) return false;
    if (!add("get_perk_effects", "Get a PERK record's effects by FormKey from plugin's version (read-only). Each effect has type, rank, priority, quest and questStage, ability, or entryPoint and function, condition tabs and parameters (data is hex)", "backend", "GetPerkEffects",
             {{"plugin", "string"}, {"form_key", "string"}})) return false;
    if (!add("set_perk_effects", "Replace a PERK record's whole effect list in memory with the effects array (see get_perk_effects for the item shape; an effect without tabs clears its condition tabs). Call save_plugin to persist", "backend", "SetPerkEffects",
             {{"plugin", "string"}, {"form_key", "string"}, {"effects", "array", false, k_perk_effect_items}})) return false;
    if (!add("get_perk_links", "Get a PERK record's links from plugin's version (read-only): nextPerk (NNAM, a PERK FormKey or null), sound (SNAM, an SNDR FormKey or null), swf (FNAM, the SWF text or null) and conditions (the root condition list that gates the perk, in the get_conditions shape)", "backend", "GetPerkLinks",
             {{"plugin", "string"}, {"form_key", "string"}})) return false;
    if (!add("set_perk_next", "Set a PERK record's next-perk link (NNAM) in memory. next must be a PERK FormKey; a record without NNAM gets one inserted before its FNAM or effects. Call save_plugin to persist. Sound and swf are set with set_perk_sound and set_perk_swf", "backend", "SetPerkNext",
             {{"plugin", "string"}, {"form_key", "string"}, {"next", "string"}})) return false;
    if (!add("set_perk_sound", "Set a PERK record's sound (SNAM) in memory to an SNDR FormKey. The SNAM is inserted after DATA when absent and rewritten in place otherwise; an empty sound removes it. Call save_plugin to persist", "backend", "SetPerkSound",
             {{"plugin", "string"}, {"form_key", "string"}, {"sound", "string"}})) return false;
    if (!add("set_perk_swf", "Set a PERK record's SWF text (FNAM) in memory from UTF-8 text that must be representable in Windows-1252. The FNAM is inserted before the effects when absent and rewritten in place otherwise; an empty swf removes it. Call save_plugin to persist", "backend", "SetPerkSwf",
             {{"plugin", "string"}, {"form_key", "string"}, {"swf", "string"}})) return false;
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
    if (!add({"nif_inspect", "Inspect a NIF: niftool when configured, else read in-process", nifPathSchema},
             "nif", "Inspect", {"path"})) return false;
    if (!add({"nif_verify", "Verify a NIF: niftool when configured, else read in-process", nifPathSchema},
             "nif", "Verify", {"path"})) return false;
    if (!add({"nif_tree", "Return the NIF block tree: niftool when configured, else read in-process", nifPathSchema},
             "nif", "Tree", {"path"})) return false;
    if (!add({"nif_geometry", "Return geometry data: niftool when configured, else read in-process", nifPathSchema},
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

    return registerPluginTools(registry, rpc, log) &&
           fo4ide::app::registerCatalogTools(registry, rpc, log);
}
