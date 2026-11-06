#include "McpCatalog.hpp"

namespace fo4ide::app {
namespace {

const CatalogTool kTools[] = {
    {"get_conflicts", "backend", "GetConflicts", "", "Editor query: get conflicts. Same RPC the desktop editor uses."},
    {"get_referenced_by", "backend", "GetReferencedBy", "form_key:string", "Editor query: get referenced by. Same RPC the desktop editor uses."},
    {"get_problems", "backend", "GetProblems", "form_key:string", "Editor query: get problems. Same RPC the desktop editor uses."},
    {"compare_plugin_to", "backend", "ComparePluginTo", "plugin:string,other_path:string,form_key:string?", "Editor query: compare plugin to. Same RPC the desktop editor uses."},
    {"describe_element", "backend", "DescribeElement", "plugin:string,form_key:string,path:string", "Editor query: describe element. Same RPC the desktop editor uses."},
    {"get_conditions", "backend", "GetConditions", "plugin:string,form_key:string", "Editor query: get conditions. Same RPC the desktop editor uses."},
    {"get_quest_objectives", "backend", "GetQuestObjectives", "plugin:string,form_key:string,context:object?", "Get quest objectives with alias targets and flags. Each objective also returns flagNames (named FNAM bits set) and unknownFlagBits (remaining raw bits). Supply a language context to resolve localized display text."},
    {"get_quest_stages", "backend", "GetQuestStages", "plugin:string,form_key:string,context:object?", "Get quest stages, logs, notes, raw flags and startUp. Each stage also returns flagNames (named INDX bits set: RunOnStart, RunOnStop, KeepInstanceDataFromHereOn) and unknownFlagBits (remaining raw bits). Supply a language context to resolve localized log text. startUp does not establish automatic quest startup."},
    {"get_quest_aliases", "backend", "GetQuestAliases", "plugin:string,form_key:string", "Editor query: get quest aliases with kind, ID, name, flags and collection entries. Same RPC the desktop editor uses."},
    {"get_dialogue", "backend", "GetDialogue", "plugin:string,quest_form_key:string", "Get a quest's dialogue branches, topics and INFO responses with conditions. INFO resetHours is in hours; resetTimeEncoded is the raw stored value. Reads the in-memory plugin."},
    {"get_quest_fragments", "backend", "GetQuestFragments", "plugin:string,form_key:string", "Editor query: list a quest's fragment bindings from plugin's in-memory version: stage, logEntryIndex, script, function and fragmentScript (the fragment script header name). Read-only; use it before remove_quest_fragment."},
    {"get_quest_script", "backend", "GetQuestScript", "script_name:string,imports_directory:string?", "Editor query: read a quest script's Papyrus source before edit_quest_script. script_name may be namespaced with ':', '/' or '\\' separators (for example Fragments:Quests:QF_X or DLC01:Fragments:Quests:QF_X). Source order: a loose <namespace folders>/<script>.psc in imports_directory, then a loose .pex there (decompiled), then a loose .pex under the Data Scripts folder (archive 'loose'), then the vanilla PEX under scripts/<namespace>/<script>.pex in the BA2 archives under Data (decompiled): Fallout4 - Misc.ba2 is read first, the other archives follow in name order, archives named for textures, meshes, sounds, voices, materials, shaders, interface or animations are skipped, and the last archive holding the script wins. Returns {script, origin, archive, source, extends, parses, notes}; notes lists unreadable non-Misc archives that were skipped. The vanilla source is a decompilation: comments and doc-strings are lost, and the PEX was compiled without debug info or debug-only/beta-only code, so its header compile metadata differs. Read-only; an unknown or malformed name is refused."},
    {"get_conditions_at", "backend", "GetConditionsAt", "plugin:string,form_key:string,path:string", "Editor query: get conditions at. Same RPC the desktop editor uses."},
    {"get_condition_functions", "backend", "GetConditionFunctions", "", "Editor query: get condition functions. Same RPC the desktop editor uses."},
    {"get_condition_run_on_types", "backend", "GetConditionRunOnTypes", "", "Editor query: get condition run on types. Same RPC the desktop editor uses."},
    {"get_condition_function_params", "backend", "GetConditionFunctionParams", "", "Editor query: get condition function params. Same RPC the desktop editor uses."},
    {"get_record_type_index", "backend", "GetRecordTypeIndex", "", "Editor query: get record type index. Same RPC the desktop editor uses."},
    {"get_containment_path", "backend", "GetContainmentPath", "form_key:string", "Editor query: get containment path. Same RPC the desktop editor uses."},
    {"get_record_details", "backend", "GetRecordDetails", "form_key:string", "Editor query: get record details. Same RPC the desktop editor uses."},
    {"get_record_plugin_matrix", "backend", "GetRecordPluginMatrix", "form_key:string", "Editor query: get record plugin matrix. Same RPC the desktop editor uses."},
    {"get_dependencies", "backend", "GetDependencies", "form_key:string", "Editor query: get dependencies. Same RPC the desktop editor uses."},
    {"get_history", "backend", "GetHistory", "form_key:string", "Editor query: get history. Same RPC the desktop editor uses."},
    {"get_load_order_summary", "backend", "GetLoadOrderSummary", "", "Editor query: get load order summary. Same RPC the desktop editor uses."},
    {"get_texture", "cell", "GetTexture", "rel_model_path:string,rel_tex_path:string", "Editor query: get texture. Same RPC the desktop editor uses."},
    {"get_placed_references", "cell", "GetPlacedReferences", "cell_id:string", "Editor query: get placed references. Placed-reference or cell operation."},
    {"get_placed_references_at_grid", "cell", "GetPlacedReferencesAtGrid", "worldspace:string,grid_x:integer,grid_y:integer", "Editor query: get placed references at grid. Placed-reference or cell operation."},
    {"search_worldspaces", "cell", "SearchWorldspaces", "query:string,limit:integer?", "Editor query: search worldspaces. Placed-reference or cell operation."},
    {"get_geometry_batch", "cell", "GetGeometryBatch", "rel_model_paths_json:string", "Editor query: get geometry batch. Same RPC the desktop editor uses."},
    {"search_cells", "cell", "SearchCells", "query:string,limit:integer?", "Editor query: search cells. Placed-reference or cell operation."},
    {"get_geometry_batch_progress", "cell", "GetGeometryBatchProgress", "", "Editor query: get geometry batch progress. Same RPC the desktop editor uses."},
    {"get_scene", "backend", "GetScene", "plugin:string,scene_form_key:string", "Get a scene: flags, parent quest, template scene, index, phases with start and completion conditions, actors, actions with payload links, and the INFO response texts of dialogue topics the actions reference. Reads the in-memory plugin."},
};

}

std::span<const CatalogTool> readCatalogTools() { return kTools; }

}
