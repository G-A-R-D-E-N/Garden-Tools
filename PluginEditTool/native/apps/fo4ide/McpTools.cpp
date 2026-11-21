#include "McpTools.hpp"

#include "McpCatalog.hpp"
#include "McpPathPolicy.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using fo4ide::app::AutoRootResolver;
using fo4ide::app::PathArgument;
using fo4ide::app::PathForm;
using fo4ide::app::PathUse;
using fo4ide::app::withPathPolicy;

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

struct PluginArgument {
    const char* name;
    const char* type;
    bool optional{false};
    const char* itemsSchema{nullptr};
};

constexpr const char* k_furniture_marker_items =
    R"({"type":"object","properties":{"enabled":{"type":"boolean","description":"Interaction point on; default true"},)"
    R"("offsetX":{"type":"number"},"offsetY":{"type":"number"},"offsetZ":{"type":"number"},"rotationZ":{"type":"number"},)"
    R"("entryTypes":{"type":"integer","minimum":0,"maximum":255,"description":"Entry type bit mask; default 255"}}})";

constexpr const char* k_message_button_items =
    R"({"oneOf":[{"type":"string","minLength":1},{"type":"object","properties":{"text":{"type":"string","minLength":1}},"required":["text"]}]})";

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
                         const std::shared_ptr<spdlog::logger>& log,
                         const fo4ide::settings::SharedSettings& settings,
                         const AutoRootResolver& autoRoots) {
    using namespace fo4ide::mcp;
    const auto add = [&](std::string name, std::string description,
                         std::string target, std::string method,
                         std::vector<PluginArgument> fields,
                         std::vector<PathArgument> paths = {}) {
        nlohmann::json properties = nlohmann::json::object();
        nlohmann::json required = nlohmann::json::array();
        std::vector<std::string> order;
        for (const auto& field : fields) {
            properties[field.name] = {{"type", field.type}};
            if (std::string_view(field.type) == "string|object") {
                properties[field.name]["type"] = nlohmann::json::array({"string", "object"});
            }
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
        return registerMcpTool(registry, withPathPolicy(std::make_shared<FunctionTool>(
            std::move(definition), [bridge, schema](const nlohmann::json& args) {
                if (const auto error = argumentError(schema, args)) return ToolResult{*error, true};
                return bridge->call(args);
            }), std::move(paths), settings, autoRoots), log);
    };
    if (!add("list_plugins",
             "List loaded plugins. Pending=true means unsaved in-memory edits; check this before save_plugin, discard_plugin, or reload_mo2_profile",
             "appInterop", "GetPlugins", {})) return false;
    if (!add("open_plugin", "Open an existing plugin by absolute path", "backend", "OpenPlugin",
             {{"path", "string"}}, {{"path", PathUse::Read, PathForm::Single}})) return false;
    if (!add("create_plugin", "Create an empty ESP in the new-plugins folder: <MO2 mods>/FO4IDE New Plugins by default, or the NewPluginFolder setting, which is applied when the MO2 profile loads. The result names the full path and, for a folder in MO2's mods folder, the MO2 steps; later edits require save_plugin", "backend", "CreatePlugin",
             {{"plugin", "string"}})) return false;
    if (!add("create_record", "Create a typed record in memory. Supported types: ALCH, ARMA, ARMO, CELL, EXPL, EYES, FACT, FLST, GLOB, HAZD, KYWD, LVLI, LVLN, LVSP, MESG, MGEF, PACK, PERK, PROJ, QUST, RACE, SPEL. Payload fields are validated by the native backend. PACK takes an editorId, a location object (type reference, cell, near_package_start, near_editor_location or near_self; form, a FormKey required for reference and cell; radius, default 0), speed (walk, jog, run or fast_walk, default jog) and the booleans preferPreferredPath, useSoftRadius and ignoreMarkerOrientation, and builds one Travel procedure with its four data inputs. ALCH, FLST, GLOB, KYWD, MESG, MGEF, QUST and SPEL take an editorId and build every required subrecord from the schema defaults. KYWD also takes type (an enum name such as Sound, or 0-18) and color (RRGGBB, RRGGBBAA or [r, g, b, a]); FLST takes entries (FormKeys or EditorIDs); ALCH and SPEL take effects [{effect, magnitude, area, duration}]. QUST also takes optional name (inline text in a non-localized plugin; in a localized plugin it becomes a new string ID in the staged string tables, so call strings_save to persist it, and language picks the table language, default en), flags, priority and type. PERK takes an editorId and optional full, desc, trait, level, ranks, playable, effects (the set_perk_effects array), conditions (the array shape set_conditions takes), nextPerk (a PERK FormKey), sound (an SNDR FormKey), swf (text) and language (localized plugins: the table language, default en). In a localized plugin full and desc are written as new string IDs in the staged string tables; call strings_save to persist them", "backend", "CreateRecord",
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
    if (!add("get_conflict_matrix", "Get the conflict matrix for one FormKey: every plugin version and its conflict rows, plus the record's conflict category. conflict_categories is an optional comma-separated filter (unknown, only_one, no_conflict, benign, override, conflict, critical); a record outside the filter returns no rows. Large payloads are possible on big load orders; prefer get_record for a compact view", "backend", "GetConflictMatrix",
             {{"form_key", "string"}, {"conflict_categories", "string", true}})) return false;
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
    if (!add("search_records", "Search the loaded order by text (matches FormKey, EditorID, signature and display name), signature, form ID, EditorID, display name, field value, conflict categories and limit (max 500). editor_id_pattern is a whole-EditorID glob (* any run, ? one character, case-insensitive, max 128 characters). Set regex to true to treat query as a case-insensitive ECMAScript regular expression instead of a substring", "backend", "SearchRecords",
             {{"query", "string"}, {"type", "string", true}, {"form_id", "string", true}, {"editor_id", "string", true}, {"editor_id_pattern", "string", true}, {"display_name", "string", true}, {"field_value", "string", true}, {"conflict_categories", "string", true}, {"limit", "integer", true}, {"language", "string", true}, {"regex", "boolean", true}})) return false;
    if (!add("set_field", "Set a field in memory using an editable column path from list_records_summary; value is text. <SIG>\\Full is the record name on every record type that has one; set_name does the same from just a form key. Call save_plugin to persist", "backend", "SetField",
             {{"plugin", "string"}, {"form_key", "string"}, {"path", "string"}, {"value", "string"}})) return false;
    if (!add("save_plugin", "Save pending changes to the plugin's existing disk path. Use list_plugins Pending to see which plugins are dirty first", "backend", "SavePatch",
             {{"plugin", "string"}})) return false;
    if (!add("discard_plugin", "Discard pending in-memory edits for one plugin without writing disk. Use list_plugins Pending to see which plugins are dirty first", "backend", "DiscardPlugin",
             {{"plugin", "string"}})) return false;
    if (!add("backup_plugin", "Copy the plugin's on-disk file to a timestamped .bak beside it (MyMod.esp.20250623_142031.bak) before the first edit of an existing plugin; never overwrites an existing backup and leaves pending in-memory edits untouched", "backend", "BackupPlugin",
             {{"plugin", "string"}})) return false;
    if (!add("list_masters", "List a plugin's declared masters", "masters", "List",
             {{"plugin", "string"}})) return false;
    if (!add("validate_plugin",
             "Validate a loaded plugin and return findings with stable codes. Checks duplicate FormIDs, FormIDs past the declared masters, HEDR numRecords and nextObjectId, missing or misordered masters, required subrecords per record type, empty effect lists, a QUST that carries a VMAD script without Start Game Enabled, VMAD scripts against compiled .pex, and an optional project contract. severity is info, warning or error (default info); contract is a JSON file path or an inline contract object. errors is the count that makes a check fail",
             "backend", "ValidatePlugin",
             {{"plugin", "string"}, {"contract", "string|object", true}, {"severity", "string", true}},
             {{"contract", PathUse::Read, PathForm::SingleOrObject}})) return false;
    if (!add("add_master",
             "Declare one master on a plugin, fresh or existing, in memory; call save_plugin to persist. Optional for the record writers, which declare a missing master themselves in load order. master_file is a plugin file name, not a path. Masters are load-order significant: an already-declared master that loads after the new one is refused with MasterOrder. Declaring a master that is already declared is a no-op. A plugin cannot declare itself, an unloaded full or light-unknown master is refused, and protected or game-Data plugins are refused",
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
    if (!add("set_furniture_markers",
             "Replace a FURN record's whole marker list in memory with the markers array (at most 22; omitted or empty-string markers means one default marker). Each marker takes enabled, offsetX, offsetY, offsetZ, rotationZ and entryTypes. Call save_plugin to persist",
             "backend", "SetFurnitureMarkers",
             {{"plugin", "string"}, {"form_key", "string"}, {"markers", "array", true, k_furniture_marker_items}})) return false;
    if (!add("set_message_buttons",
             "Replace a MESG record's menu buttons in memory with the buttons array (strings or objects with text; zero-based Show() index order; at most 65535) and set its MessageBox flag bit, keeping the other flag bits. The old buttons and any conditions on them are dropped. Omitted buttons clears the list. Call save_plugin to persist",
             "backend", "SetMessageButtons",
             {{"plugin", "string"}, {"form_key", "string"}, {"buttons", "array", true, k_message_button_items}})) return false;
    if (!add("get_leveled_list", "Get a leveled list (LVLI, LVLN or LVSP) by FormKey: chance none, max count, flags, use global and entries from the winning version, or from plugin's version when plugin is given. edit_leveled_list_entry indexes the named plugin's version", "backend", "GetLeveledList",
             {{"form_key", "string"}, {"plugin", "string", true}})) return false;
    if (!add("set_leveled_list_settings", "Set a leveled list's chance none, max count and flags bytes (0-255) in memory. Call save_plugin to persist", "backend", "SetLeveledListSettings",
             {{"plugin", "string"}, {"form_key", "string"}, {"chance_none", "integer"}, {"max_count", "integer"}, {"flags", "integer"}})) return false;
    if (!add("edit_leveled_list_entry", "Add (insert at index), set or remove one leveled list entry in memory. value is {level, item, count, chanceNone, extra}; required for add and set. index is into the named plugin's version of the list. Call save_plugin to persist", "backend", "EditLeveledListEntry",
             {{"plugin", "string"}, {"form_key", "string"}, {"op", "string"}, {"index", "integer"}, {"value", "object", true}})) return false;
    if (!add("get_form_list_entries", "Get an FLST record's entries in order by FormKey from the named plugin's version. add_form_list_entry, set_form_list_entry and remove_form_list_entry index this list", "backend", "GetFormListEntries",
             {{"plugin", "string"}, {"form_key", "string"}})) return false;
    if (!add("add_form_list_entry", "Insert one FLST entry in memory at index, or append at the end when index is omitted. entry is a FormKey or a loaded EditorID and must resolve in the loaded order; a refused entry names its index. Call save_plugin to persist", "backend", "AddFormListEntry",
             {{"plugin", "string"}, {"form_key", "string"}, {"entry", "string"}, {"index", "integer", true}})) return false;
    if (!add("set_form_list_entry", "Replace one FLST entry at index in memory with entry, a FormKey or a loaded EditorID; the index must already exist. Call save_plugin to persist", "backend", "SetFormListEntry",
             {{"plugin", "string"}, {"form_key", "string"}, {"index", "integer"}, {"entry", "string"}})) return false;
    if (!add("remove_form_list_entry", "Remove one FLST entry by index in memory; the index must already exist. Call save_plugin to persist", "backend", "RemoveFormListEntry",
             {{"plugin", "string"}, {"form_key", "string"}, {"index", "integer"}})) return false;
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
    if (!add("remove_vmad_fragment", "Remove one existing VMAD fragment binding from a PACK, PERK, TERM or SCEN record in memory. fragment_key names the binding: for PACK 0 is OnBegin, 1 OnEnd and 2 OnChange; for SCEN 0 is OnBegin and 1 OnEnd (phase fragments use remove_scene_phase_fragment); for PERK and TERM it is the fragment index stored in the entry, not its list position. Attached scripts, the other bindings and the rest of the record are kept. INFO and QUST are refused with UnsupportedFragmentRecord (use remove_info_fragment or remove_quest_fragment); a key with no binding is refused with FragmentMissing. Call save_plugin to persist", "backend", "RemoveVmadFragment",
             {{"plugin", "string"}, {"form_key", "string"}, {"fragment_key", "integer"}})) return false;
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
    if (!add("add_alias_script", "Add a script to a QUST quest alias in memory, creating the alias entry (at its ascending alias_id position, with one script) when the quest has none for alias_id. script is the Papyrus script name; flags is the optional script flags byte and index the optional script-list position within the alias. Call save_plugin to persist", "backend", "AddAliasScript",
             {{"plugin", "string"}, {"form_key", "string"}, {"alias_id", "integer"}, {"script", "string"}, {"flags", "integer", true}, {"index", "integer", true}})) return false;
    if (!add("remove_alias_script", "Remove one script from a QUST quest alias in memory by script index. Removing the alias's last script removes the whole alias entry. Call save_plugin to persist", "backend", "RemoveAliasScript",
             {{"plugin", "string"}, {"form_key", "string"}, {"alias_id", "integer"}, {"script_index", "integer"}})) return false;
    if (!add("add_alias_script_property", "Add a property to a script of a QUST quest alias in memory. type is the VMAD property type byte (1 object, 2 string, 3 int, 4 float, 5 bool, 7 struct, 11-15 arrays, 17 struct array); flags is the optional property flags byte and index the optional property-list position. Set its value afterwards with set_alias_script_value. Call save_plugin to persist", "backend", "AddAliasScriptProperty",
             {{"plugin", "string"}, {"form_key", "string"}, {"alias_id", "integer"}, {"script_index", "integer"}, {"name", "string"}, {"type", "integer"}, {"flags", "integer", true}, {"index", "integer", true}})) return false;
    if (!add("remove_alias_script_property", "Remove one property from a script of a QUST quest alias in memory by property index. Call save_plugin to persist", "backend", "RemoveAliasScriptProperty",
             {{"plugin", "string"}, {"form_key", "string"}, {"alias_id", "integer"}, {"script_index", "integer"}, {"property_index", "integer"}})) return false;
    if (!add("set_alias_script_value", "Set one VMAD script property value of a QUST quest alias in memory. kind selects the typed setter: object (value is a FormKey, alias optional), string, int, float or bool (value is text for int/float/bool). element selects an element of an array property and defaults to 0. Call save_plugin to persist", "backend", "SetAliasScriptValue",
             {{"plugin", "string"}, {"form_key", "string"}, {"alias_id", "integer"}, {"script_index", "integer"},
              {"property_index", "integer"}, {"kind", "string"}, {"value", "string", true},
              {"alias", "integer", true}, {"element", "integer", true}})) return false;
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
                            const std::shared_ptr<spdlog::logger>& log,
                            const fo4ide::settings::SharedSettings& settings,
                            const AutoRootResolver& autoRoots) {
    using fo4ide::mcp::FunctionTool;
    using fo4ide::mcp::RpcTool;
    using fo4ide::mcp::ToolDefinition;

    const auto add = [&](ToolDefinition definition,
                         std::string target,
                         std::string method,
                         std::vector<std::string> argumentOrder = {},
                         std::vector<PathArgument> paths = {}) {
        return registerMcpTool(
            registry,
            withPathPolicy(
                std::make_shared<RpcTool>(
                    std::move(definition), rpc, std::move(target), std::move(method), std::move(argumentOrder)),
                std::move(paths), settings, autoRoots),
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
             "compiler_path", "engine", "debug_info"},
            {{"source", PathUse::Read, PathForm::Single}, {"output", PathUse::Write, PathForm::Single},
             {"imports", PathUse::Read, PathForm::SemicolonList}, {"flags", PathUse::DataRelative, PathForm::Single},
             {"compiler_path", PathUse::Compiler, PathForm::Single}})) return false;
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
            "papyrus", "Decompile", {"source", "output", "assembly", "write"},
            {{"source", PathUse::Read, PathForm::Single}, {"output", PathUse::Write, PathForm::Single}})) return false;
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
            "papyrus", "Check", {"source", "semantic", "imports"},
            {{"source", PathUse::Read, PathForm::Single}, {"imports", PathUse::Read, PathForm::SemicolonList}})) return false;
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
    const std::vector<PathArgument> pathRead{{"path", PathUse::Read, PathForm::Single}};
    if (!add({"nif_inspect", "Inspect a NIF, read in-process", nifPathSchema},
             "nif", "Inspect", {"path"}, pathRead)) return false;
    if (!add({
            "nif_segments",
            "Return one BSSubIndexTriShape's armor segment table, its sub-segment counts and its SSF sidecar, "
            "read in-process. block is the NIF block index. Game acceptance of a rewritten segment table is unproven.",
            objectSchema({
                {"path", {{"type", "string"}, {"description", "Path to the NIF file"}}},
                {"block", {{"type", "integer"}, {"description", "Block index of a BSSubIndexTriShape"}}}
            }, nlohmann::json::array({"path", "block"}))},
            "nif", "Segments", {"path", "block"}, pathRead)) return false;
    if (!add({"nif_verify", "Verify a NIF, read in-process", nifPathSchema},
             "nif", "Verify", {"path"}, pathRead)) return false;
    if (!add({"nif_tree", "Return the NIF block tree, read in-process", nifPathSchema},
             "nif", "Tree", {"path"}, pathRead)) return false;
    if (!add({"nif_geometry", "Return geometry data, read in-process", nifPathSchema},
             "nif", "Geo", {"path"}, pathRead)) return false;
    if (!add({
            "nif_import",
            "Build a NIF from an OBJ in process. collision adds a collision body around the mesh: box, hull "
            "or mesh (mesh is static only); true means box, false or none means no collision. mass_kg gives a "
            "box or hull body its mass in kilograms; 0 or omitted keeps it static. An existing out_nif is refused "
            "with an EXISTS: error unless overwrite is true. Authored collision has not "
            "been tested in the game until the owner confirms it.",
            objectSchema({
                {"obj_path", {{"type", "string"}, {"description", "Path to the OBJ file"}}},
                {"out_nif", {{"type", "string"}, {"description", "Path to write the NIF"}}},
                {"material", {{"type", "string"}, {"description", "Optional BGSM or BGEM material path"}}},
                {"tex_diffuse", {{"type", "string"}, {"description", "Optional diffuse texture path"}}},
                {"tex_normal", {{"type", "string"}, {"description", "Optional normal texture path"}}},
                {"collision", {{"anyOf", nlohmann::json::array({{{"type", "boolean"}},
                                                                 {{"type", "string"}, {"enum", nlohmann::json::array({"none", "box", "hull", "mesh"})}}})},
                               {"description", "none, box, hull or mesh; true means box, false means none. Default none."}}},
                {"from_blender", {{"type", "boolean"}, {"description", "The OBJ was exported from Blender with Y up. Default false."}}},
                {"mass_kg", {{"type", "number"}, {"minimum", 0}, {"description", "Body mass in kilograms for a box or hull. 0 or omitted keeps the body static."}}},
                {"overwrite", {{"type", "boolean"}, {"description", "Replace an existing NIF at out_nif. Default false."}}}
            }, nlohmann::json::array({"obj_path", "out_nif"}))},
            "nif", "Import",
            {"obj_path", "out_nif", "material", "tex_diffuse", "tex_normal", "collision", "from_blender", "mass_kg", "overwrite"}, {{"obj_path", PathUse::Read, PathForm::Single}, {"out_nif", PathUse::Write, PathForm::Single}})) return false;
    if (!add({
            "nif_create_texture_set",
            "Give a shape whose BSLightingShaderProperty has no texture set a new BSShaderTextureSet holding the given slot paths, and write the NIF in process. block is the shape's block index. Refused when the shader already has a texture set or is not a lighting shader.",
            objectSchema({
                {"path", {{"type", "string"}, {"description", "Path to the NIF file to read"}}},
                {"block", {{"type", "integer"}, {"minimum", 0}, {"description", "Block index of the shape"}}},
                {"textures", {{"type", "array"}, {"items", {{"type", "string"}}}, {"minItems", 1}, {"description", "Texture paths in slot order"}}},
                {"out_nif", {{"type", "string"}, {"description", "Path to write the NIF"}}},
                {"overwrite", {{"type", "boolean"}, {"description", "Replace an existing NIF at out_nif. Default false."}}}
            }, nlohmann::json::array({"path", "block", "textures", "out_nif"}))},
            "nif", "CreateTextureSet", {"path", "block", "textures", "out_nif", "expected_source", "overwrite"}, {{"path", PathUse::Read, PathForm::Single}, {"out_nif", PathUse::Write, PathForm::Single}})) return false;
    if (!add({
            "nif_attach_alpha",
            "Give a BSTriShape or BSMeshLODTriShape with no alpha property a new NiAlphaProperty and write the NIF in process. block is the shape's block index, flags is the alpha flags word (236 is the engine default), threshold is the alpha test threshold. Refused when the shape already has one or is not a supported tri-shape.",
            objectSchema({
                {"path", {{"type", "string"}, {"description", "Path to the NIF file to read"}}},
                {"block", {{"type", "integer"}, {"minimum", 0}, {"description", "Block index of the shape"}}},
                {"flags", {{"type", "integer"}, {"minimum", 0}, {"maximum", 65535}, {"description", "Alpha flags word. Default 236."}}},
                {"threshold", {{"type", "integer"}, {"minimum", 0}, {"maximum", 255}, {"description", "Alpha test threshold. Default 128."}}},
                {"out_nif", {{"type", "string"}, {"description", "Path to write the NIF"}}},
                {"overwrite", {{"type", "boolean"}, {"description", "Replace an existing NIF at out_nif. Default false."}}}
            }, nlohmann::json::array({"path", "block", "out_nif"}))},
            "nif", "AttachAlpha", {"path", "block", "flags", "threshold", "out_nif", "expected_source", "overwrite"}, {{"path", PathUse::Read, PathForm::Single}, {"out_nif", PathUse::Write, PathForm::Single}})) return false;
    if (!add({
            "nif_remove_block",
            "Remove a childless block with no outside references from a NIF and write the result in process, renumbering every reference past it. Blocks it alone owned go with it. Refused when the block has children or is referenced from outside a child list.",
            objectSchema({
                {"path", {{"type", "string"}, {"description", "Path to the NIF file to read"}}},
                {"block", {{"type", "integer"}, {"minimum", 0}, {"description", "Block index to remove"}}},
                {"out_nif", {{"type", "string"}, {"description", "Path to write the NIF"}}},
                {"overwrite", {{"type", "boolean"}, {"description", "Replace an existing NIF at out_nif. Default false."}}}
            }, nlohmann::json::array({"path", "block", "out_nif"}))},
            "nif", "RemoveBlock", {"path", "block", "out_nif", "expected_source", "overwrite"}, {{"path", PathUse::Read, PathForm::Single}, {"out_nif", PathUse::Write, PathForm::Single}})) return false;
    if (!add({
            "nif_block_op",
            "Run one block-structure op on a NIF in process and write the result to out_nif. "
            "duplicate_block copies a childless node or a shape (block) as a new last block with no parent, so attach it with append_child; a shape copy shares its skin, shader and texture references, and a block with a collision object is refused. "
            "append_child makes the unparented block child the last child of the node block, and refuses a child that has a parent or is an ancestor of the node. "
            "sort_children_by_name orders the child list of the node block by block name and keeps its length. "
            "merge_nif_into appends every block of the donor NIF at donor_path and parents the donor roots under the node block; a donor with Havok collision is refused. "
            "bake_shape_transforms applies each listed shape's own transform into its vertices and resets it to identity, so the vertices end up in the parent's space (blocks is required); skinned, half precision and mirrored shapes are skipped and reported, and the op is refused when nothing was baked. "
            "join_shapes merges the BSTriShape blocks in blocks into blocks[0], which keeps its node, material, shader and skin, and removes the others; the shapes need byte-identical vertex layouts, bitwise-identical global transforms (bake first), the same material and textures, and all skinned or all unskinned. "
            "separate_shape splits the BSTriShape block into one shape per triangle list in groups (every triangle in exactly one group, at least two groups); the copies are appended under the same plain parent and share the skin, shader and texture blocks. "
            "separate_by_bone does the same for a skinned shape, one shape per dominant bone. The reply lists the surviving and appended shape blocks. "
            "apply_pose writes the bone-local transforms of the pose named pose in the pose library file pose_path (see nif_pose_list) into the bone data of the skin instance block (BSSkin::Instance or BSDismemberSkinInstance); a pose bone the skin lacks and a skin bone the pose lacks are reported, and the op is refused when no bone was posed. "
            "set_keys replaces one key channel of the key data block (NiTransformData, NiKeyframeData, NiPosData or NiFloatData) with keys: translation, scale, rotation_quat, rotation_x, rotation_y or rotation_z for the transform blocks, data for the others. "
            "Each key is {time, value}; value holds interpolation-dependent numbers: 1 number per component for linear (1) and constant (5) keys, 3 per component for quadratic (2) keys (value, forward and backward tangent), 3 more for TBC (3) keys (tension, continuity, bias); components are 1 for scale, rotation_x/y/z and float data, 3 for translation and position data, 4 (w, x, y, z) for rotation_quat, whose quadratic keys carry no tangents. "
            "Times must be finite and non-decreasing; the channel is replaced whole, so send every key; NiBoolData and text keys are refused. "
            "Blocks are numbered as in nif_tree. An existing NIF at out_nif is refused unless overwrite is true.",
            objectSchema({
                {"path", {{"type", "string"}, {"description", "Path to the NIF file to read"}}},
                {"op", {{"type", "string"}, {"enum", nlohmann::json::array({"duplicate_block", "append_child", "sort_children_by_name", "merge_nif_into", "bake_shape_transforms", "join_shapes", "separate_shape", "separate_by_bone", "apply_pose", "set_keys"})}, {"description", "The block-structure op to run"}}},
                {"block", {{"type", "integer"}, {"minimum", 0}, {"description", "Block index: the block to copy (duplicate_block), the node that gains or sorts children (append_child, sort_children_by_name), the node that parents the donor roots (merge_nif_into), the shape to split (separate_shape, separate_by_bone), the skin instance to pose (apply_pose) or the key data block (set_keys). Not used by bake_shape_transforms or join_shapes."}}},
                {"child", {{"type", "integer"}, {"minimum", 0}, {"description", "append_child only: the block to append"}}},
                {"donor_path", {{"type", "string"}, {"description", "merge_nif_into only: the donor NIF to merge in"}}},
                {"blocks", {{"type", "array"}, {"items", {{"type", "integer"}, {"minimum", 0}}}, {"description", "bake_shape_transforms: the shape block ids to bake. join_shapes: the shape block ids to join, the first one survives (at least two)."}}},
                {"groups", {{"type", "array"}, {"items", {{"type", "array"}, {"items", {{"type", "integer"}, {"minimum", 0}}}}}, {"description", "separate_shape only: triangle index lists, one per resulting shape; together they must cover every triangle of the shape exactly once"}}},
                {"pose_path", {{"type", "string"}, {"description", "apply_pose only: the pose library file (JSON with a poses list)"}}},
                {"pose", {{"type", "string"}, {"description", "apply_pose only: the name of the pose in the library"}}},
                {"channel", {{"type", "string"}, {"enum", nlohmann::json::array({"translation", "scale", "rotation_quat", "rotation_x", "rotation_y", "rotation_z", "data"})}, {"description", "set_keys only: the key channel to replace"}}},
                {"interpolation", {{"type", "integer"}, {"enum", nlohmann::json::array({1, 2, 3, 5})}, {"description", "set_keys only: the key type, 1 linear, 2 quadratic, 3 TBC or 5 constant. Optional when the channel already has keys (they keep their type); required for an empty channel."}}},
                {"keys", {{"type", "array"}, {"items", {{"type", "object"}, {"properties", {{"time", {{"type", "number"}}}, {"value", {{"type", "array"}, {"items", {{"type", "number"}}}}}}}, {"required", nlohmann::json::array({"time", "value"})}}}, {"description", "set_keys only: the whole new channel as {time, value} entries, times non-decreasing (at least one key)"}}},
                {"out_nif", {{"type", "string"}, {"description", "Path to write the NIF"}}},
                {"overwrite", {{"type", "boolean"}, {"description", "Replace an existing NIF at out_nif. Default false."}}}
            }, nlohmann::json::array({"path", "op", "out_nif"}))},
            "nif", "BlockOp", {"path", "op", "block", "child", "donor_path", "blocks", "out_nif", "expected_source", "overwrite", "groups", "pose_path", "pose", "channel", "interpolation", "keys"}, {{"path", PathUse::Read, PathForm::Single}, {"donor_path", PathUse::Read, PathForm::Single}, {"pose_path", PathUse::Read, PathForm::Single}, {"out_nif", PathUse::Write, PathForm::Single}})) return false;
    if (!add({
            "nif_pose_list",
            "List the poses of a pose library file (JSON: a poses list of named bone-local transforms) with their bone counts. "
            "Give path and block (a BSSkin::Instance or BSDismemberSkinInstance block, numbered as in nif_tree) as well to dry-run every pose against that skin: "
            "each pose then reports how many bones it would write, the pose bones the skin lacks and the skin bones the pose lacks, or why it would be refused. Nothing is written; apply a pose with nif_block_op apply_pose.",
            objectSchema({
                {"pose_path", {{"type", "string"}, {"description", "Path to the pose library file"}}},
                {"path", {{"type", "string"}, {"description", "Path to the NIF whose skin the poses are checked against. Give it together with block."}}},
                {"block", {{"type", "integer"}, {"minimum", 0}, {"description", "The skin instance block of that NIF. Give it together with path."}}}
            }, nlohmann::json::array({"pose_path"}))},
            "nif", "PoseList", {"pose_path", "path", "block"}, {{"pose_path", PathUse::Read, PathForm::Single}, {"path", PathUse::Read, PathForm::Single}})) return false;
    if (!add({
            "nif_export_gltf",
            "Export a NIF to a binary glTF 2.0 (.glb) file in process: the node tree, shapes with positions, normals, "
            "UVs, tangents, vertex colours, skin and one material per shape. Materials come from the shader texture set, "
            "or from the BGSM/BGEM the shape names, with alpha mode and double-sided. Positions are in metres "
            "(0.0142875 per NIF unit) and Z-up becomes Y-up. The NIF is not changed; Havok animation clips are not exported. "
            "An existing .glb at out, or a texture file it would write beside it, is refused unless overwrite is true.",
            objectSchema({
                {"path", {{"type", "string"}, {"description", "Path to the NIF file"}}},
                {"out", {{"type", "string"}, {"description", "Path to write the .glb file"}}},
                {"overwrite", {{"type", "boolean"}, {"description", "Replace an existing .glb at out, and texture files beside it. Default false."}}},
                {"materials", {{"type", "boolean"}, {"description", "Write materials, textures and alpha. Default true."}}},
                {"textures", {{"type", "string"}, {"enum", nlohmann::json::array({"png", "copy", "embed", "uri"})},
                               {"description", "png writes decoded PNG files beside the .glb under textures/, copy writes the DDS files beside it, embed stores the DDS inside the .glb, uri writes relative paths only. Default png."}}},
                {"tangents", {{"type", "boolean"}, {"description", "Write TANGENT when the shape has tangents. Default true."}}},
                {"vertex_colours", {{"type", "boolean"}, {"description", "Write COLOR_0 when the shape has vertex colours. Default true."}}}
            }, nlohmann::json::array({"path", "out"}))},
            "nif", "ExportGltf", {"path", "out", "overwrite", "materials", "textures", "tangents", "vertex_colours"}, {{"path", PathUse::Read, PathForm::Single}, {"out", PathUse::Write, PathForm::Single}})) return false;
    if (!add({
            "nif_import_gltf",
            "Build a NIF from a glTF 2.0 file (.glb or .gltf) in process. Nodes, primitives, positions, normals, UVs, "
            "tangents, vertex colours and skins are kept; Y-up metres become Z-up NIF units. A skinned mesh needs "
            "skeleton, a NIF whose bones its joints name. A material's base colour and normal textures become texture "
            "paths under textures\\, and a material named by a .bgsm or by extras.bgsm becomes the shader's BGSM. "
            "An existing NIF at out_nif is refused unless overwrite is true.",
            objectSchema({
                {"gltf_path", {{"type", "string"}, {"description", "Path to the .glb or .gltf file"}}},
                {"out_nif", {{"type", "string"}, {"description", "Path to write the NIF"}}},
                {"material", {{"type", "string"}, {"description", "Optional BGSM material path, used where a glTF material names none"}}},
                {"tex_diffuse", {{"type", "string"}, {"description", "Optional diffuse texture path, used where a material has no base colour texture"}}},
                {"tex_normal", {{"type", "string"}, {"description", "Optional normal texture path, used where a material has no normal texture"}}},
                {"collision", {{"type", "boolean"}, {"description", "Add a static box collision body around the mesh. Default false."}}},
                {"skeleton", {{"type", "string"}, {"description", "Skeleton NIF whose bones the skinned joints bind to. Required when the glTF has skins."}}},
                {"overwrite", {{"type", "boolean"}, {"description", "Replace an existing NIF at out_nif. Default false."}}}
            }, nlohmann::json::array({"gltf_path", "out_nif"}))},
            "nif", "ImportGltf", {"gltf_path", "out_nif", "material", "tex_diffuse", "tex_normal", "collision", "skeleton", "overwrite"}, {{"gltf_path", PathUse::Read, PathForm::Single}, {"out_nif", PathUse::Write, PathForm::Single}, {"skeleton", PathUse::Read, PathForm::Single}})) return false;
    if (!add({"nif_collision_info",
              "Read a NIF's collision in process: each body's node, motion, mass, layer and layer name (empty when no COLL "
              "record names it), material CRCs, shape kind and vertex and triangle counts, constraints, unsupported shapes "
              "and lint findings. Each system's owned flag marks the donor bodies that nif_collision_set can copy. Read-only",
              nifPathSchema},
             "nif", "CollisionInfo", {"path"}, pathRead)) return false;
    if (!add({
            "nif_collision_set",
            "Add or replace the collision of one node and write a new NIF. source donor_nif copies one collision body of "
            "donor_path, the first unless donor_body names another, carrying its full system bytes: compound children, constraints, ragdoll bones, and dynamic motion and mass are preserved; source kind builds box, hull or mesh from the shapes under the node, "
            "and mass_kg applies to box or hull. "
            "out_nif must differ from path; an existing out_nif is refused with an EXISTS: error unless overwrite is true, "
            "and when overwritten it is copied to out_nif.bak once before it is replaced. "
            "The node name must be unique in the NIF. "
            "Authored collision has not been tested in the game until the owner confirms it. Mesh is static only.",
            objectSchema({
                {"path", {{"type", "string"}, {"description", "Path to the NIF whose node gets the collision"}}},
                {"node", {{"type", "string"}, {"description", "Name of the node that owns the collision"}}},
                {"out_nif", {{"type", "string"}, {"description", "Save As path for the new NIF"}}},
                {"source", {{"type", "string"}, {"enum", nlohmann::json::array({"donor_nif", "kind"})}, {"description", "donor_nif or kind"}}},
                {"donor_path", {{"type", "string"}, {"description", "Donor NIF whose collision is copied, when source is donor_nif"}}},
                {"kind", {{"type", "string"}, {"enum", nlohmann::json::array({"box", "hull", "mesh"})}, {"description", "Shape built from the node's geometry, when source is kind"}}},
                {"mass_kg", {{"type", "number"}, {"minimum", 0}, {"description", "Mass in kilograms for a box or hull. 0 or omitted keeps the body static."}}},
                {"donor_body", {{"type", "integer"}, {"minimum", 0}, {"description", "Index of the donor body to copy, when source is donor_nif. Counts the donor's entries with owned true in nif_collision_info, in order; a system shared by two owners has two entries. Default 0; out of range is refused."}}},
                {"overwrite", {{"type", "boolean"}, {"description", "Replace an existing out_nif. Default false, which refuses with an EXISTS: error."}}}
            }, nlohmann::json::array({"path", "node", "out_nif", "source"}))},
            "nif", "CollisionSet",
            {"path", "node", "out_nif", "source", "donor_path", "kind", "mass_kg", "donor_body", "overwrite"}, {{"path", PathUse::Read, PathForm::Single}, {"out_nif", PathUse::Write, PathForm::Single}, {"donor_path", PathUse::Read, PathForm::Single}})) return false;
    if (!add({"nif_collision_remove",
              "Remove the collision of one node and write a new NIF. out_nif must differ from path; an existing out_nif is refused "
              "with an EXISTS: error unless overwrite is true, and when overwritten it is copied to out_nif.bak once. "
              "The node name must be unique in the NIF.",
              objectSchema({
                  {"path", {{"type", "string"}, {"description", "Path to the NIF whose node loses its collision"}}},
                  {"node", {{"type", "string"}, {"description", "Name of the node that owns the collision"}}},
                  {"out_nif", {{"type", "string"}, {"description", "Save As path for the new NIF"}}},
                  {"overwrite", {{"type", "boolean"}, {"description", "Replace an existing out_nif. Default false, which refuses with an EXISTS: error."}}}
              }, nlohmann::json::array({"path", "node", "out_nif"}))},
             "nif", "CollisionRemove", {"path", "node", "out_nif", "overwrite"}, {{"path", PathUse::Read, PathForm::Single}, {"out_nif", PathUse::Write, PathForm::Single}})) return false;
    if (!add({"nif_connect_points_get", "List a NIF's connect point blocks (CPA parents, CPT children), read in-process",
              nifPathSchema},
             "nif", "ConnectPoints", {"path"}, pathRead)) return false;
    if (!add({
            "nif_connect_points_set",
            "Apply connect point ops to a NIF in process and write it to out_nif. ops is a JSON array text of "
            "add, remove or set operations on BSConnectPoint blocks.",
            objectSchema({
                {"path", {{"type", "string"}, {"description", "Path to the NIF file"}}},
                {"ops", {{"type", "string"}, {"description", "JSON array text of connect point ops"}}},
                {"out_nif", {{"type", "string"}, {"description", "Path to write the NIF"}}},
                {"overwrite", {{"type", "boolean"}, {"description", "Replace an existing NIF at out_nif. Default false."}}}
            }, nlohmann::json::array({"path", "ops", "out_nif"}))},
            "nif", "ApplyConnectPoints", {"path", "ops", "out_nif", "overwrite"}, {{"path", PathUse::Read, PathForm::Single}, {"out_nif", PathUse::Write, PathForm::Single}})) return false;
    if (!add({"nif_uv_get",
              "Return every shape's UV sets: vertex count, whether UV0 and UV2 are present, and a per-island vertex count and UV-area summary",
              nifPathSchema},
             "nif", "UvGet", {"path"}, pathRead)) return false;
    if (!add({
            "nif_uv_transform",
            "Transform one shape's UV channel and save it. Order matches the editor: sx/sy scale about (0.5,0.5), then rotation_degrees rotates about (0.5,0.5), then du/dv translate. Writes through the existing uvs/uvs2 edit keys and refuses non-finite or out-of-half-range results",
            objectSchema({
                {"path", {{"type", "string"}, {"description", "Path to the NIF file"}}},
                {"block", {{"type", "integer"}, {"description", "Shape block id"}}},
                {"channel", {{"type", "integer"}, {"description", "0 for UV0, 2 for UV2 (default 0)"}}},
                {"du", {{"type", "number"}, {"description", "UV translation along U"}}},
                {"dv", {{"type", "number"}, {"description", "UV translation along V"}}},
                {"sx", {{"type", "number"}, {"description", "UV scale along U"}}},
                {"sy", {{"type", "number"}, {"description", "UV scale along V"}}},
                {"rotation_degrees", {{"type", "number"}, {"description", "Rotation about (0.5,0.5)"}}},
                {"out_nif", {{"type", "string"}, {"description", "Path to write the NIF"}}},
                {"overwrite", {{"type", "boolean"}, {"description", "Replace an existing NIF at out_nif. Default false."}}}
            }, nlohmann::json::array({"path", "block", "out_nif"}))},
            "nif", "UvTransform",
            {"path", "block", "channel", "du", "dv", "sx", "sy", "rotation_degrees", "out_nif", "overwrite"}, {{"path", PathUse::Read, PathForm::Single}, {"out_nif", PathUse::Write, PathForm::Single}})) return false;
    if (!add({"nif_vertex_color_get",
              "Return every shape's vertex colour summary: vertex count, whether colours are present, and per-channel min, max and mean",
              nifPathSchema},
             "nif", "VertexColorGet", {"path"}, pathRead)) return false;
    if (!add({
            "nif_vertex_color_set",
            "Paint one shape's vertex colours and save it. kind: fill (color to the target vertices), channel (one of red, green, blue, alpha set to value), gradient (color to end_color along axis across the whole shape's extent), enable (add white colours to a shape without them; takes no vertex list). vertices lists target vertex indices; omitted means all vertices, and an empty list is refused. Values are 0..1; refuses a shape without vertex colours for every kind except enable",
            objectSchema({
                {"path", {{"type", "string"}, {"description", "Path to the NIF file"}}},
                {"block", {{"type", "integer"}, {"description", "Shape block id"}}},
                {"kind", {{"type", "string"}, {"description", "fill, channel, gradient or enable"}}},
                {"channel", {{"type", "string"}, {"description", "red, green, blue or alpha when kind is channel"}}},
                {"axis", {{"type", "string"}, {"description", "x, y or z when kind is gradient"}}},
                {"value", {{"type", "number"}, {"description", "Scalar for channel kind"}}},
                {"color", {{"type", "array"}, {"items", {{"type", "number"}}}, {"description", "RGBA 0..1 for fill or gradient start"}}},
                {"end_color", {{"type", "array"}, {"items", {{"type", "number"}}}, {"description", "RGBA 0..1 for gradient end"}}},
                {"vertices", {{"type", "array"}, {"items", {{"type", "integer"}}}, {"description", "Target vertex indices; omitted means all vertices, an empty list is refused"}}},
                {"out_nif", {{"type", "string"}, {"description", "Path to write the NIF"}}},
                {"overwrite", {{"type", "boolean"}, {"description", "Replace an existing NIF at out_nif. Default false."}}}
            }, nlohmann::json::array({"path", "block", "kind", "out_nif"}))},
            "nif", "VertexColorSet",
            {"path", "block", "kind", "channel", "axis", "value", "color", "end_color", "vertices", "out_nif", "expected_source", "overwrite"}, {{"path", PathUse::Read, PathForm::Single}, {"out_nif", PathUse::Write, PathForm::Single}})) return false;
    if (!add({
            "nif_shape_op",
            "Weld, simplify or sanitise mesh geometry, drop degenerate or unused geometry, or recompute an attribute "
            "on a NIF, and write the result to out_nif. The NIF at nif_path is not changed. "
            "op is one of recalc_normals, recalc_tangents, flip_normals, flip_winding, apply_transform, refresh_bounds, "
            "weld_vertices, remove_degenerate_triangles, remove_unused_vertices, simplify or sanitize. "
            "blocks lists the block ids to change (default: every shape); a block that is not a tri-shape is reported as skipped. "
            "crease_degrees applies to recalc_normals: edges sharper than that angle stay hard (default 180 smooths everything). "
            "normalize_weights, limit_influences, prune_weights and copy_weights edit skin weights on skinned shapes and keep "
            "both counts; max_influences applies to limit_influences, prune_threshold to prune_weights and source_block to "
            "copy_weights, which takes each vertex's weights from the nearest vertex of that tri-shape. "
            "extrude_faces, bridge_loops, knife_plane, cut_loop and bevel_edges edit the geometry of one plain BSTriShape "
            "named by a single id in blocks and keep its block index, name, references, transform and vertex layout: "
            "extrude_faces lifts the triangles in tris along their vertex normals by distance and walls the boundary; "
            "bridge_loops bands the vertex loops loop_a and loop_b of equal length; knife_plane cuts triangles against the "
            "plane through offset along normal; cut_loop splits the ring of vertex pairs in edges and re-triangulates the "
            "crossed triangles; bevel_edges chamfers the vertex pairs in edges by width, a fraction above 0 and below 0.5. They refuse a skinned shape with "
            "a BSDismemberSkinInstance, a second UV set, other tri-shape types and any shape they would change on rewrite. "
            "An existing NIF at out_nif is refused unless overwrite is true. weld_vertices, remove_degenerate_triangles, "
            "remove_unused_vertices and simplify may change one of the two counts on plain BSTriShape blocks; the other "
            "attribute ops never do.",
            objectSchema({
                {"nif_path", {{"type", "string"}, {"description", "Path to the NIF to read"}}},
                {"out_nif", {{"type", "string"}, {"description", "Path to write the changed NIF"}}},
                {"op", {{"type", "string"}, {"description", "recalc_normals, recalc_tangents, flip_normals, flip_winding, apply_transform, refresh_bounds, weld_vertices, remove_degenerate_triangles, remove_unused_vertices, simplify, sanitize, normalize_weights, limit_influences, prune_weights, copy_weights, extrude_faces, bridge_loops, knife_plane, cut_loop or bevel_edges"}}},
                {"blocks", {{"type", "array"}, {"items", {{"type", "integer"}, {"minimum", 0}}},
                            {"description", "Block ids to change. Default: every shape. extrude_faces, bridge_loops, knife_plane, cut_loop and bevel_edges need exactly one."}}},
                {"crease_degrees", {{"type", "number"}, {"description", "recalc_normals only: edges sharper than this angle stay hard. Default 180."}}},
                {"ratio", {{"type", "number"}, {"description", "simplify only: target fraction of the triangles, above 0 and below 1. Default 0.5."}}},
                {"max_influences", {{"type", "integer"}, {"minimum", 1}, {"maximum", 4}, {"description", "limit_influences only: most bones a vertex keeps, 1 to 4. Default 4."}}},
                {"prune_threshold", {{"type", "number"}, {"minimum", 0}, {"maximum", 0.999}, {"description", "prune_weights only: weights below this are dropped, at least 0 and below 1. Default 0."}}},
                {"source_block", {{"type", "integer"}, {"minimum", 0}, {"description", "copy_weights only: the tri-shape block to copy skin weights from. Required for copy_weights."}}},
                {"overwrite", {{"type", "boolean"}, {"description", "Replace an existing NIF at out_nif. Default false."}}},
                {"tris", {{"type", "array"}, {"items", {{"type", "integer"}, {"minimum", 0}}}, {"description", "extrude_faces only: triangle indices of the shape to extrude. Required for extrude_faces."}}},
                {"distance", {{"type", "number"}, {"description", "extrude_faces only: how far the triangles move along their vertex normals, above 0. Required for extrude_faces."}}},
                {"loop_a", {{"type", "array"}, {"items", {{"type", "integer"}, {"minimum", 0}}}, {"description", "bridge_loops only: vertex indices of the first loop, at least two. Required for bridge_loops."}}},
                {"loop_b", {{"type", "array"}, {"items", {{"type", "integer"}, {"minimum", 0}}}, {"description", "bridge_loops only: vertex indices of the second loop, as many as loop_a, sharing no vertex with it. Required for bridge_loops."}}},
                {"normal", {{"type", "array"}, {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "knife_plane only: the plane normal [x, y, z], not zero. Required for knife_plane."}}},
                {"offset", {{"type", "number"}, {"description", "knife_plane only: the plane sits where normal . position = offset with the normal as given, so normal [2, 0, 0] and offset 1 cut at x = 0.5. May be negative. A plane that crosses no triangle is refused. Required for knife_plane."}}},
                {"edges", {{"type", "array"}, {"items", {{"type", "array"}, {"items", {{"type", "integer"}, {"minimum", 0}}}, {"minItems", 2}, {"maxItems", 2}}}, {"description", "cut_loop: the ring of mesh edges as [vertex, vertex] pairs in order, each sharing a vertex with the next and the last with the first. bevel_edges: the edges to chamfer as [vertex, vertex] pairs, sharing no endpoint. Required for both."}}},
                {"width", {{"type", "number"}, {"description", "bevel_edges only: how far each chamfer corner sits from the edge, as a fraction of the way to the triangle's third vertex, above 0 and below 0.5. Required for bevel_edges."}}}
            }, nlohmann::json::array({"nif_path", "out_nif", "op"}))},
            "nif", "ShapeOp",
            {"nif_path", "out_nif", "op", "blocks", "crease_degrees", "overwrite", "ratio", "max_influences",
             "prune_threshold", "source_block", "tris", "distance", "loop_a", "loop_b", "normal", "offset", "edges",
             "width"}, {{"nif_path", PathUse::Read, PathForm::Single}, {"out_nif", PathUse::Write, PathForm::Single}})) return false;
    if (!add({
            "nif_skin_influences",
            "Read the skin influences of one vertex of a NIF tri-shape: the bones that move it and their weights, "
            "heaviest first, with the shape's bone names so the caller can name a bone by index.",
            objectSchema({
                {"nif_path", {{"type", "string"}, {"description", "Path to the NIF to read"}}},
                {"block", {{"type", "integer"}, {"minimum", 0}, {"description", "Tri-shape block id"}}},
                {"vertex", {{"type", "integer"}, {"minimum", 0}, {"description", "Vertex index inside that shape"}}}
            }, nlohmann::json::array({"nif_path", "block", "vertex"}))},
            "nif", "SkinInfluences", {"nif_path", "block", "vertex"},
            {{"nif_path", PathUse::Read, PathForm::Single}})) return false;
    if (!add({
            "nif_skin_influenced_by",
            "List the vertices one bone moves in a NIF tri-shape, with their count, weight sum and local bounding box. "
            "minimum drops vertices the bone moves less than that weight (default 0.01).",
            objectSchema({
                {"nif_path", {{"type", "string"}, {"description", "Path to the NIF to read"}}},
                {"block", {{"type", "integer"}, {"minimum", 0}, {"description", "Tri-shape block id"}}},
                {"bone", {{"type", "integer"}, {"minimum", 0}, {"description", "Bone index inside that shape"}}},
                {"minimum", {{"type", "number"}, {"minimum", 0}, {"description", "Smallest weight the bone must carry. Default 0.01."}}}
            }, nlohmann::json::array({"nif_path", "block", "bone"}))},
            "nif", "SkinInfluencedBy", {"nif_path", "block", "bone", "minimum"},
            {{"nif_path", PathUse::Read, PathForm::Single}})) return false;
    if (!add({
            "nif_skin_validate",
            "Check a NIF tri-shape's skin the way BGS SkinValidator does: bind transforms against bones, finite and "
            "non-negative weights, a sum of one, one bone in two slots of a vertex, a vertex that moves nothing and a "
            "bone no vertex uses. Errors make the skin unusable; warnings are worth a look.",
            objectSchema({
                {"nif_path", {{"type", "string"}, {"description", "Path to the NIF to read"}}},
                {"block", {{"type", "integer"}, {"minimum", 0}, {"description", "Tri-shape block id"}}}
            }, nlohmann::json::array({"nif_path", "block"}))},
            "nif", "SkinValidate", {"nif_path", "block"},
            {{"nif_path", PathUse::Read, PathForm::Single}})) return false;
    if (!add({
            "nif_skin_edit",
            "Edit the skin weights of one NIF tri-shape and write the result to out_nif. kind is set_influences "
            "(replace vertex's bones and weights with influences), copy_bone (move from_bone's weight onto to_bone, "
            "keeping it as well when keep_source is true) or mirror_weights (copy the from_positive side's weights "
            "onto the other side, matching vertices by reflected position and bones by their left/right names). "
            "The NIF at nif_path is not changed, and an existing NIF at out_nif is refused unless overwrite is true.",
            objectSchema({
                {"nif_path", {{"type", "string"}, {"description", "Path to the NIF to read"}}},
                {"out_nif", {{"type", "string"}, {"description", "Path to write the changed NIF"}}},
                {"kind", {{"type", "string"}, {"description", "set_influences, copy_bone or mirror_weights"}}},
                {"block", {{"type", "integer"}, {"minimum", 0}, {"description", "Tri-shape block id"}}},
                {"vertex", {{"type", "integer"}, {"minimum", 0}, {"description", "set_influences only: the vertex to rewrite"}}},
                {"influences", {{"type", "array"},
                                {"items", {{"type", "object"}}},
                                {"description", "set_influences only: [{bone, weight}], at most four, strongest first after sorting"}}},
                {"from_bone", {{"type", "integer"}, {"minimum", 0}, {"description", "copy_bone only: the bone whose weight moves"}}},
                {"to_bone", {{"type", "integer"}, {"minimum", 0}, {"description", "copy_bone only: the bone that receives the weight"}}},
                {"keep_source", {{"type", "boolean"}, {"description", "copy_bone only: keep from_bone's weight as well. Default false."}}},
                {"lane", {{"type", "integer"}, {"minimum", 0}, {"maximum", 2}, {"description", "mirror_weights only: 0 for X, 1 for Y, 2 for Z. Default 0."}}},
                {"from_positive_side", {{"type", "boolean"}, {"description", "mirror_weights only: copy the positive side. Default true."}}},
                {"overwrite", {{"type", "boolean"}, {"description", "Replace an existing NIF at out_nif. Default false."}}},
                {"expected_source", {{"type", "string"}, {"description", "sha256 of the NIF as it was loaded; a mismatch refuses the save"}}}
            }, nlohmann::json::array({"nif_path", "out_nif", "kind", "block"}))},
            "nif", "SkinEdit",
            {"nif_path", "out_nif", "kind", "block", "vertex", "influences", "from_bone", "to_bone",
             "keep_source", "lane", "from_positive_side", "overwrite", "expected_source"},
            {{"nif_path", PathUse::Read, PathForm::Single}, {"out_nif", PathUse::Write, PathForm::Single}})) return false;
    if (!add({
            "hkx_skeleton",
            "Read a skeleton .hkx: the bone hierarchy with each bone's translation lock, the reference floats, the float slots, "
            "the local frames and the named partitions",
            objectSchema({
                {"skeleton_path", {{"type", "string"}, {"description", "Path to the skeleton .hkx"}}}
            }, nlohmann::json::array({"skeleton_path"}))},
            "hkx", "Skeleton", {"skeleton_path"}, {{"skeleton_path", PathUse::Read, PathForm::Single}})) return false;
    if (!add({
            "hkx_pose",
            "Sample one animation pose from skeleton and animation .hkx files at a time in seconds",
            objectSchema({
                {"skeleton_path", {{"type", "string"}, {"description", "Path to the skeleton .hkx"}}},
                {"animation_path", {{"type", "string"}, {"description", "Path to the animation .hkx"}}},
                {"time", {{"type", "number"}, {"minimum", 0}, {"description", "Time in seconds"}}},
                {"space", {{"type", "string"}, {"enum", nlohmann::json::array({"local", "model"})}, {"description", "Pose space"}}}
            }, nlohmann::json::array({"skeleton_path", "animation_path", "time"}))},
            "hkx", "Pose", {"skeleton_path", "animation_path", "time", "space"}, {{"skeleton_path", PathUse::Read, PathForm::Single}, {"animation_path", PathUse::Read, PathForm::Single}})) return false;
    if (!add({
            "hkx_objects",
            "List the typed objects of a .hkx packfile with their fields and resolved references",
            objectSchema({
                {"path", {{"type", "string"}, {"description", "Path to the .hkx file"}}},
                {"class_prefix", {{"type", "string"}, {"description", "Keep objects whose class name starts with this prefix. Default: every class."}}},
                {"known_layout_only", {{"type", "boolean"}, {"description", "Skip objects whose class has no generated layout. Default false."}}},
                {"limit", {{"type", "integer"}, {"minimum", 1}, {"description", "Most objects to return. Default 64."}}}
            }, nlohmann::json::array({"path"}))},
            "hkx", "InspectObjects", {"path", "class_prefix", "known_layout_only", "limit"},
            {{"path", PathUse::Read, PathForm::Single}})) return false;
    if (!add({
            "hkx_root_motion",
            "Report one bone's travelled distance, heading change and loop closure for a clip",
            objectSchema({
                {"skeleton_path", {{"type", "string"}, {"description", "Path to the skeleton .hkx"}}},
                {"animation_path", {{"type", "string"}, {"description", "Path to the animation .hkx"}}},
                {"bone", {{"type", "integer"}, {"minimum", 0}, {"description", "Bone index the clip moves"}}},
                {"limit", {{"type", "integer"}, {"minimum", 1}, {"description", "Most samples to return. Default 64."}}}
            }, nlohmann::json::array({"skeleton_path", "animation_path", "bone"}))},
            "hkx", "RootMotion", {"skeleton_path", "animation_path", "bone", "limit"},
            {{"skeleton_path", PathUse::Read, PathForm::Single}, {"animation_path", PathUse::Read, PathForm::Single}})) return false;
    if (!add({
            "hkx_skeleton_document",
            "Read a skeleton .hkx as a bone document with depths, children and left/right partners",
            objectSchema({
                {"skeleton_path", {{"type", "string"}, {"description", "Path to the skeleton .hkx"}}}
            }, nlohmann::json::array({"skeleton_path"}))},
            "hkx", "SkeletonDocument", {"skeleton_path"},
            {{"skeleton_path", PathUse::Read, PathForm::Single}})) return false;
    if (!add({
            "hkx_behavior_graph",
            "List a .hkx file's hkb behavior objects and the links between them",
            objectSchema({
                {"path", {{"type", "string"}, {"description", "Path to the .hkx file"}}},
                {"limit", {{"type", "integer"}, {"minimum", 1}, {"description", "Most behavior objects to return. Default 64."}}}
            }, nlohmann::json::array({"path"}))},
            "hkx", "InspectBehavior", {"path", "limit"},
            {{"path", PathUse::Read, PathForm::Single}})) return false;
    if (!add({
            "hkx_behavior_model",
            "Read a behavior .hkx through the typed reader: graph name, root node, events, variables, typed nodes with child edges and state generators",
            objectSchema({
                {"path", {{"type", "string"}, {"description", "Path to the behavior .hkx file"}}},
                {"limit", {{"type", "integer"}, {"minimum", 1}, {"description", "Most rows to return from each list. Default 2000."}}}
            }, nlohmann::json::array({"path"}))},
            "hkx", "ReadBehaviorModel", {"path", "limit"},
            {{"path", PathUse::Read, PathForm::Single}})) return false;
    if (!add({
            "hkx_subgraph_id",
            "Name a subgraph and read its manifest without a RACE record: give id (decimal, as a crash log prints it) or a behavior_path "
            "with its path_prefixes, which are hashed to the id the game uses. With data_folder the AnimationFileData manifest for that id is "
            "read (loose Meshes first, then the .ba2 archives) and each animation it lists is reported present or in unresolved",
            objectSchema({
                {"id", {{"type", "string"}, {"description", "Subgraph id, a decimal unsigned 64-bit number. Give this or behavior_path."}}},
                {"behavior_path", {{"type", "string"}, {"description", "Behaviour graph path, for example Actors/Character/Behaviors/WeaponBehavior.hkx. Give this or id."}}},
                {"path_prefixes", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Animation path prefixes in the order the record lists them. Required with behavior_path."}}},
                {"data_folder", {{"type", "string"}, {"description", "Optional Data folder holding Meshes and the .ba2 archives"}}}
            }, nlohmann::json::array())},
            "hkx", "SubgraphManifest", {"id", "behavior_path", "path_prefixes", "data_folder"},
            {{"data_folder", PathUse::Read, PathForm::Single}})) return false;
    if (!add({
            "hkx_ragdoll",
            "Read a standalone ragdoll .hkx: bodies, shapes, constraints with their limits, bone bindings, the physics and animation skeletons "
            "and the validator findings. A file with no usable ragdoll replies with loaded false and the classifier verdict (skeleton only, "
            "legacy physics, unsupported shapes, wrong container) instead of an error",
            objectSchema({
                {"path", {{"type", "string"}, {"description", "Path to the ragdoll .hkx file"}}},
                {"limit", {{"type", "integer"}, {"minimum", 1}, {"description", "Most rows to return from each list. Default 2000."}}}
            }, nlohmann::json::array({"path"}))},
            "hkx", "ReadRagdoll", {"path", "limit"},
            {{"path", PathUse::Read, PathForm::Single}})) return false;
    if (!add({
            "hkx_mapper",
            "Read the skeleton mappers of an .hkx: both skeletons, mapping type, unmapped bones, the extracted motion, and every simple row and "
            "chain row with its bone names. With recompute true each simple row also reports the transform the two reference poses produce and "
            "its gap to the stored one, so a stale row is visible. Nothing is written",
            objectSchema({
                {"path", {{"type", "string"}, {"description", "Path to the .hkx file holding the mapper"}}},
                {"recompute", {{"type", "boolean"}, {"description", "Also report each simple row's recomputed transform and gap. Default false."}}},
                {"limit", {{"type", "integer"}, {"minimum", 1}, {"description", "Most rows to return from each list. Default 2000."}}}
            }, nlohmann::json::array({"path"}))},
            "hkx", "ReadMappers", {"path", "recompute", "limit"},
            {{"path", PathUse::Read, PathForm::Single}})) return false;
    if (!add({
            "hkx_project_index",
            "Index the .hkx files under a directory, search them and group identical behavior graphs",
            objectSchema({
                {"root", {{"type", "string"}, {"description", "Directory to scan"}}},
                {"query", {{"type", "string"}, {"description", "Match file names or class names, ignoring case. Default: every file."}}},
                {"limit", {{"type", "integer"}, {"minimum", 1}, {"description", "Most files to scan. Default 400."}}}
            }, nlohmann::json::array({"root"}))},
            "hkx", "ProjectIndex", {"root", "query", "limit"},
            {{"root", PathUse::Read, PathForm::Single}})) return false;
    if (!add({
            "hkx_skeleton_edit",
            "Apply one validated bone edit and return the resulting skeleton, without writing a file",
            objectSchema({
                {"skeleton_path", {{"type", "string"}, {"description", "Path to the skeleton .hkx"}}},
                {"kind", {{"type", "string"}, {"enum", nlohmann::json::array({"rename", "reparent", "remove"})},
                          {"description", "Edit to apply"}}},
                {"bone", {{"type", "integer"}, {"minimum", 0}, {"description", "Bone index to edit"}}},
                {"name", {{"type", "string"}, {"description", "rename only: the new bone name"}}},
                {"parent", {{"type", "integer"}, {"minimum", -1}, {"description", "reparent only: the new parent index, or -1 for a root"}}}
            }, nlohmann::json::array({"skeleton_path", "kind", "bone"}))},
            "hkx", "SkeletonEdit", {"skeleton_path", "kind", "bone", "name", "parent"},
            {{"skeleton_path", PathUse::Read, PathForm::Single}})) return false;
    if (!add({
            "hkx_summary",
            "Read an HKX overview in process: object count, the first skeleton's name and bone count, every decoded clip's class, "
            "kind, duration, frame, track and annotation counts, the skeleton names the bindings name, and one undecoded entry per "
            "animation object that did not decode, so clips is never silently empty. hkx_animation_inspect reports a class this "
            "build cannot decode as kind view.",
            objectSchema({
                {"path", {{"type", "string"}, {"description", "Path to the .hkx file"}}}
            }, nlohmann::json::array({"path"}))},
            "hkx", "Summary", {"path"}, pathRead)) return false;
    if (!add({
            "hkx_animation_inspect",
            "Inspect one animation .hkx in process: the clip class and kind, duration, frame, track, annotation and object counts, "
            "each track's animation flags and sample count, and every annotation track's entries. With track, that transform track's "
            "translations, rotations (x, y, z, w) and scales are included for at most limit frames, and truncated says whether the "
            "clip has more. A clip class this build does not decode reports kind view with no tracks rather than failing.",
            objectSchema({
                {"animation_path", {{"type", "string"}, {"description", "Path to the animation .hkx"}}},
                {"track", {{"type", "integer"}, {"minimum", 0}, {"description", "Optional transform-track index whose samples are returned"}}},
                {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 100000},
                           {"description", "Frames the optional track dump returns, 1 to 100000. Default 2000"}}}
            }, nlohmann::json::array({"animation_path"}))},
            "hkx", "InspectAnimation", {"animation_path", "track", "limit"},
            {{"animation_path", PathUse::Read, PathForm::Single}})) return false;

    if (!add({
            "hkx_subgraph",
            "Resolve a race's AnimTextData subgraph in process: the behaviour graphs (SGNM) and animation path "
            "prefixes (SAPT) the record names, the subgraph id those hash to, and the game's AnimationFileData "
            "manifest for that id read from data_folder (loose Meshes first, then its .ba2 archives). Each animation "
            "path the manifest lists is reported present or in unresolved. A record that names several behaviour "
            "graphs reports the lists with no id, because the record does not group prefixes per graph.",
            objectSchema({
                {"plugin_path", {{"type", "string"}, {"description", "Path to the plugin holding the RACE record"}}},
                {"form_id", {{"type", "string"}, {"description", "Local form ID of the RACE, decimal or 0x hex"}}},
                {"data_folder", {{"type", "string"}, {"description", "Optional Data folder holding Meshes and the .ba2 archives"}}}
            }, nlohmann::json::array({"plugin_path", "form_id"}))},
            "hkx", "Subgraph", {"plugin_path", "form_id", "data_folder"},
            {{"plugin_path", PathUse::Read, PathForm::Single},
             {"data_folder", PathUse::Read, PathForm::Single}})) return false;
    if (!add({
            "audio_convert_to_xwm",
            "Convert one audio/video file or a folder to Fallout 4 XWM",
            objectSchema({
                {"source", {{"type", "string"}}},
                {"output", {{"type", "string"}}},
                {"bitrateBps", {{"type", "integer"}, {"minimum", 1}}}
            }, nlohmann::json::array({"source"}))},
            "audio", "ConvertToXwm", {"source", "output", "bitrateBps"},
            {{"source", PathUse::Read, PathForm::Single}, {"output", PathUse::Write, PathForm::Single}})) return false;
    if (!add({
            "audio_convert_from_xwm",
            "Decode an XWM file to WAV or another ffmpeg-supported format",
            objectSchema({
                {"source", {{"type", "string"}}},
                {"output", {{"type", "string"}}},
                {"targetExt", {{"type", "string"}, {"default", "wav"}}}
            }, nlohmann::json::array({"source"}))},
            "audio", "ConvertFromXwm", {"source", "output", "targetExt"},
            {{"source", PathUse::Read, PathForm::Single}, {"output", PathUse::Write, PathForm::Single}})) return false;
    if (!add({
            "audio_make_fuz",
            "Create a Fallout 4 FUZ package from audio/XWM and an optional LIP file",
            objectSchema({
                {"audioSource", {{"type", "string"}}},
                {"lipPath", {{"type", "string"}}},
                {"fuzOutput", {{"type", "string"}}},
                {"noLip", {{"type", "boolean"}, {"default", false}}}
            }, nlohmann::json::array({"audioSource", "fuzOutput"}))},
            "audio", "MakeFuz", {"audioSource", "lipPath", "fuzOutput", "noLip"},
            {{"audioSource", PathUse::Read, PathForm::Single}, {"lipPath", PathUse::Read, PathForm::Single},
             {"fuzOutput", PathUse::Write, PathForm::Single}})) return false;
    if (!add({
            "audio_extract_fuz",
            "Extract XWM/LIP data from a Fallout 4 FUZ package",
            objectSchema({
                {"fuzPath", {{"type", "string"}}},
                {"xwmOutput", {{"type", "string"}}},
                {"lipOutput", {{"type", "string"}}},
                {"alsoWav", {{"type", "boolean"}, {"default", false}}}
            }, nlohmann::json::array({"fuzPath"}))},
            "audio", "ExtractFuz", {"fuzPath", "xwmOutput", "lipOutput", "alsoWav"},
            {{"fuzPath", PathUse::Read, PathForm::Single}, {"xwmOutput", PathUse::Write, PathForm::Single},
             {"lipOutput", PathUse::Write, PathForm::Single}})) return false;

    if (!add({
            "material_inspect",
            "Inspect a Fallout 4 BGSM or BGEM material through the native codec",
            objectSchema({{"path", {{"type", "string"}}}}, nlohmann::json::array({"path"}))},
            "material", "Inspect", {"path"}, pathRead)) return false;

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
            withPathPolicy(std::make_shared<FunctionTool>(std::move(materialSet), [&rpc](const nlohmann::json& args) {
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
                return fo4ide::mcp::rpcToolResult(response);
            }), {{"path", PathUse::Write, PathForm::Single}, {"output", PathUse::Write, PathForm::Single}}, settings, autoRoots),
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
            withPathPolicy(std::make_shared<FunctionTool>(std::move(archivePack), [&rpc](const nlohmann::json& args) {
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
                return fo4ide::mcp::rpcToolResult(response);
            }), {{"sources", PathUse::Read, PathForm::JsonList}, {"output", PathUse::Write, PathForm::Single},
                 {"rootDir", PathUse::Read, PathForm::Single}}, settings, autoRoots),
            log)) return false;

    return registerPluginTools(registry, rpc, log, settings, autoRoots) &&
           fo4ide::app::registerCatalogTools(registry, rpc, log, settings, autoRoots) &&
           registerPreviewMcpTools(registry, rpc, log, settings, autoRoots);
}
