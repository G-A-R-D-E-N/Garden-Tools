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
    {"get_quest_objectives", "backend", "GetQuestObjectives", "plugin:string,form_key:string,context:object?", "Get quest objectives with alias targets and flags. Supply a language context to resolve localized display text."},
    {"get_quest_stages", "backend", "GetQuestStages", "plugin:string,form_key:string,context:object?", "Get quest stages, logs, notes, raw flags and startUp. Supply a language context to resolve localized log text. startUp does not establish automatic quest startup."},
    {"get_quest_aliases", "backend", "GetQuestAliases", "plugin:string,form_key:string", "Editor query: get quest aliases with kind, ID, name, flags and collection entries. Same RPC the desktop editor uses."},
    {"get_quest_fragments", "backend", "GetQuestFragments", "plugin:string,form_key:string", "Editor query: list a quest's fragment bindings from plugin's in-memory version: stage, logEntryIndex, script, function and fragmentScript (the fragment script header name). Read-only; use it before remove_quest_fragment."},
    {"get_quest_script", "backend", "GetQuestScript", "script_name:string,imports_directory:string?", "Editor query: read a quest script's Papyrus source before edit_quest_script. Source order: a loose <script>.psc in imports_directory, else a loose <script>.pex there (decompiled), else the vanilla PEX in Fallout4 - Misc.ba2 under the configured Data folder (decompiled; only that archive is searched). Returns {script, origin, source, extends, parses}. Read-only; an unknown or non-identifier name is refused."},
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
};

}

std::span<const CatalogTool> readCatalogTools() { return kTools; }

}
