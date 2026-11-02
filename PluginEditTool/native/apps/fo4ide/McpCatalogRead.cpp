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
    {"previs_scan", "previs", "Scan", "data_dir:string?", "Read-only previs report for the saved load order: broken, outdated and skipped cells with capped rows. The scan reads the saved files, so unsaved edits are not included; pendingPlugins names the loaded plugins that have them. Same RPC the FO4IDE previs panel uses."},
};

}

std::span<const CatalogTool> readCatalogTools() { return kTools; }

}
