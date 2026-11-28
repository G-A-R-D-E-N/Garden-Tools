#include "McpCatalog.hpp"

namespace fo4ide::app {
namespace {

const CatalogTool kTools[] = {
    {"script_analyze", "papyrus", "Analyze", "text:string,path:rpath?", "Editor query: analyze. Papyrus source operation."},
    {"script_symbol_at", "papyrus", "SymbolAt", "text:string,path:rpath?,offset:integer?,imports:rlist?", "Editor operation: symbol at. Papyrus source operation."},
    {"script_complete", "papyrus", "Complete", "text:string,path:rpath?,offset:integer?,imports:rlist?", "Editor query: complete. Papyrus source operation."},
    {"script_signature_help", "papyrus", "SignatureHelp", "text:string,path:rpath?,offset:integer?,imports:rlist?", "Editor query: signature help. Papyrus source operation."},
    {"script_check_text", "papyrus", "CheckText", "text:string,path:rpath?,revision:integer?,imports:rlist?", "Editor query: check text. Papyrus source operation."},
    {"script_references", "papyrus", "References", "text:string,path:rpath?,offset:integer?,imports:rlist?", "Editor query: references. Papyrus source operation; locals, parameters and script names only."},
    {"script_rename", "papyrus", "Rename", "text:string,path:rpath?,offset:integer?,new_name:string,imports:rlist?", "Editor query: rename, dry run. Papyrus source operation; returns the edit list and writes no files, the caller applies the edits."},
    {"script_workspace_symbols", "papyrus", "WorkspaceSymbols", "query:string,limit:integer?,imports:rlist?", "Editor query: workspace symbols. Papyrus source operation."},
    {"script_read_script", "papyrus", "ReadScript", "path:rpath", "Editor query: read script. Papyrus source operation."},
    {"script_write_script", "papyrus", "WriteScript", "path:wpath,text:string", "Editor operation: write script. Papyrus source operation."},
    {"strings_list_tables", "strings", "ListStringTables", "plugin:string,language:string?", "Editor query: list string tables. String-table operation."},
    {"strings_get", "strings", "GetStrings", "plugin:string,language:string?,table:string,filter:string?,offset:integer?,limit:integer?", "Editor query: get strings. String-table operation."},
    {"strings_lookup", "strings", "LookupString", "plugin:string,language:string?,form_key:integer", "Editor query: lookup string. String-table operation."},
    {"strings_set", "strings", "SetString", "plugin:string,language:string?,table:string,form_key:integer,text:string", "Editor operation: set string. String-table operation; persist with strings_save or drop with strings_discard."},
    {"strings_add", "strings", "AddString", "plugin:string,language:string?,table:string,text:string", "Editor operation: add string. String-table operation; persist with strings_save or drop with strings_discard."},
    {"strings_remove", "strings", "RemoveString", "plugin:string,language:string?,table:string,form_key:integer", "Editor operation: remove string. String-table operation; persist with strings_save or drop with strings_discard."},
    {"strings_export", "strings", "ExportStrings", "plugin:string,language:string?,table:string", "Editor operation: export strings. String-table operation."},
    {"strings_save", "strings", "SaveStringTables", "plugin:string,language:string?", "Editor operation: save string tables. String-table operation; persist with strings_save or drop with strings_discard."},
    {"strings_discard", "strings", "DiscardStringEdits", "plugin:string,language:string?", "Editor operation: discard string edits. String-table operation; persist with strings_save or drop with strings_discard."},
    {"strings_localize", "strings", "LocalizePlugin", "plugin:string,language:string?", "Editor operation: localize plugin. Converts the plugin's inline localized text into string IDs and stages the string tables; persist the tables with strings_save and the plugin with save. Refused while a localized quest edit is pending."},
    {"strings_delocalize", "strings", "DelocalizePlugin", "plugin:string,language:string?", "Editor operation: delocalize plugin. Writes each string ID back as inline text from the staged or on-disk string tables and clears the localized flag; the plugin is saved with save. Refused while a localized quest edit is pending."},
    {"archive_extract_file", "archive", "ExtractFile", "archive_path:rpath,inner_path:string,out_path:wpath,overwrite:boolean?", "Editor operation: extract file. Archive operation; paths are validated by the editor backend."},
    {"archive_extract_selected", "archive", "ExtractSelectedForMcp", "archive_path:rpath,inner_paths_json:string,out_dir:wpath,overwrite:boolean?", "Editor operation: extract selected. Archive operation; paths are validated by the editor backend."},
    {"archive_extract_all", "archive", "ExtractAllForMcp", "archive_path:rpath,out_dir:wpath,filter:string?,limit:integer?,filter_mode:string?,overwrite:boolean?", "Editor operation: extract all. Archive operation; paths are validated by the editor backend."},
    {"archive_compare", "archive", "Compare", "archive_path_a:rpath,archive_path_b:rpath", "Editor query: compare. Archive operation; paths are validated by the editor backend."},
    {"archive_list", "archive", "List", "archive_path:rpath,filter:string?,limit:integer?,filter_mode:string?", "Editor query: list the entries of a BA2 archive, optionally narrowed by filter and filter_mode, up to limit (default 2000). Archive operation; paths are validated by the editor backend."},
    {"resolve_asset", "nif", "ResolveAsset", "path:dpath,extract:boolean?,limit:integer?", "Editor query: find which loose file or BA2 serves a game-relative asset path (for example Meshes\\Clutter\\Rock01.nif) across the loaded data roots, highest priority first, with every lower-priority provider. extract:true also writes the winning copy to the asset cache and returns its path. limit caps the providers listed (default 25)."},
    {"graph_core_palette", "graph", "GetCorePalette", "", "Editor query: get core palette. Blueprint behaviour-graph operation."},
    {"graph_search_palette", "graph", "SearchPalette", "kind:string?,query:string,script_filter:string?,limit:integer?", "Editor query: search palette. Blueprint behaviour-graph operation."},
    {"graph_node_signature", "graph", "GetNodeSignature", "node_type:string", "Editor query: get node signature. Blueprint behaviour-graph operation."},
    {"graph_validate", "graph", "ValidateGraph", "document_json:string", "Editor query: validate graph. Blueprint behaviour-graph operation."},
    {"graph_compile_to_source", "graph", "CompileToSource", "document_json:string", "Editor operation: compile to source. Blueprint behaviour-graph operation."},
    {"graph_compile_to_pex", "graph", "CompileToPex", "document_json:string,output_directory:wpath", "Editor operation: compile to pex. Blueprint behaviour-graph operation."},
    {"graph_load", "graph", "LoadGraph", "path:rpath", "Editor operation: load graph. Blueprint behaviour-graph operation."},
    {"load_script", "graph", "LoadScript", "path:rpath", "Editor operation: load script. Blueprint behaviour-graph operation."},
    {"graph_save", "graph", "SaveGraph", "path:wpath,document_json:string", "Editor operation: save graph. Blueprint behaviour-graph operation."},
    {"lod_worlds", "lod", "Worlds", "plugin_paths:rlines,data_dir:rpath,archive_paths:rlines",
     "LOD worldspaces that carry tile settings."},
    {"lod_plan", "lod", "Plan", "plugin_paths:rlines,data_dir:rpath,archive_paths:rlines,ledger:string?",
     "Read-only LOD rebake plan; also returns the ledger it would keep after a bake."},
    {"lod_preview", "lod", "Preview",
     "plugin_paths:rlines,data_dir:rpath,archive_paths:rlines,world:string,level:integer,x:integer,y:integer,object:boolean?",
     "Write one LOD tile to a temp NIF and return its path; object:true previews the .bto, terrain otherwise. "
     "Previews are temporary: only the 8 newest are kept."},
    {"lod_candidates", "lod", "Candidates",
     "plugin_paths:rlines,data_dir:rpath,archive_paths:rlines,world:string,level:integer",
     "Read-only object LOD candidates for a world and level, with a count per skip reason."},
    {"lod_generate", "lod", "Generate",
     "plugin_paths:rlines,data_dir:wpath,archive_paths:rlines,ledger:string?,apply:boolean",
     "Bakes the planned terrain and object tiles into data_dir and writes the atlas maps they name; "
     "apply:true is the write gate and a current tile is never rebuilt."},
    {"lod_verify", "lod", "Verify",
     "plugin_paths:rlines,data_dir:rpath,archive_paths:rlines,ledger:string?",
     "Read-only per-tile on-disk state, so a ledger entry is never reported as a present tile."},
    {"lod_generate_world", "lod", "GenerateScoped",
     "plugin_paths:rlines,data_dir:wpath,archive_paths:rlines,world:string,ledger:string?,apply:boolean,"
     "level_min:integer?,level_max:integer?",
     "Bakes the planned tiles of one worldspace into data_dir; apply:true is the write gate. level_min and level_max "
     "scope the bake to a level range, and 0 or no level keeps the world's own range."},
    {"lod_verify_world", "lod", "VerifyWorld",
     "plugin_paths:rlines,data_dir:rpath,archive_paths:rlines,world:string,ledger:string?",
     "Read-only per-tile on-disk state for one worldspace."},
};

}

std::span<const CatalogTool> serviceCatalogTools() { return kTools; }

}
