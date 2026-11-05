#include "McpCatalog.hpp"

namespace fo4ide::app {
namespace {

const CatalogTool kTools[] = {
    {"script_analyze", "papyrus", "Analyze", "text:string,path:string?", "Editor query: analyze. Papyrus source operation."},
    {"script_symbol_at", "papyrus", "SymbolAt", "text:string,path:string?,offset:integer?,imports:string?", "Editor operation: symbol at. Papyrus source operation."},
    {"script_complete", "papyrus", "Complete", "text:string,path:string?,offset:integer?,imports:string?", "Editor query: complete. Papyrus source operation."},
    {"script_signature_help", "papyrus", "SignatureHelp", "text:string,path:string?,offset:integer?,imports:string?", "Editor query: signature help. Papyrus source operation."},
    {"script_check_text", "papyrus", "CheckText", "text:string,path:string?,revision:integer?,imports:string?", "Editor query: check text. Papyrus source operation."},
    {"script_references", "papyrus", "References", "text:string,path:string?,offset:integer?,imports:string?", "Editor query: references. Papyrus source operation; locals, parameters and script names only."},
    {"script_rename", "papyrus", "Rename", "text:string,path:string?,offset:integer?,new_name:string,imports:string?", "Editor query: rename, dry run. Papyrus source operation; returns the edit list and writes no files, the caller applies the edits."},
    {"script_workspace_symbols", "papyrus", "WorkspaceSymbols", "query:string,limit:integer?,imports:string?", "Editor query: workspace symbols. Papyrus source operation."},
    {"script_read_script", "papyrus", "ReadScript", "path:string", "Editor query: read script. Papyrus source operation."},
    {"script_write_script", "papyrus", "WriteScript", "path:string,text:string", "Editor operation: write script. Papyrus source operation."},
    {"strings_list_tables", "strings", "ListStringTables", "plugin:string,language:string?", "Editor query: list string tables. String-table operation."},
    {"strings_get", "strings", "GetStrings", "plugin:string,language:string?,table:string,filter:string?,offset:integer?,limit:integer?", "Editor query: get strings. String-table operation."},
    {"strings_lookup", "strings", "LookupString", "plugin:string,language:string?,form_key:integer", "Editor query: lookup string. String-table operation."},
    {"strings_set", "strings", "SetString", "plugin:string,language:string?,table:string,form_key:integer,text:string", "Editor operation: set string. String-table operation; persist with strings_save or drop with strings_discard."},
    {"strings_add", "strings", "AddString", "plugin:string,language:string?,table:string,text:string", "Editor operation: add string. String-table operation; persist with strings_save or drop with strings_discard."},
    {"strings_remove", "strings", "RemoveString", "plugin:string,language:string?,table:string,form_key:integer", "Editor operation: remove string. String-table operation; persist with strings_save or drop with strings_discard."},
    {"strings_export", "strings", "ExportStrings", "plugin:string,language:string?,table:string", "Editor operation: export strings. String-table operation."},
    {"strings_save", "strings", "SaveStringTables", "plugin:string,language:string?", "Editor operation: save string tables. String-table operation; persist with strings_save or drop with strings_discard."},
    {"strings_discard", "strings", "DiscardStringEdits", "plugin:string,language:string?", "Editor operation: discard string edits. String-table operation; persist with strings_save or drop with strings_discard."},
    {"archive_extract_file", "archive", "ExtractFile", "archive_path:string,inner_path:string,out_path:string", "Editor operation: extract file. Archive operation; paths are validated by the editor backend."},
    {"archive_extract_selected", "archive", "ExtractSelected", "archive_path:string,inner_paths_json:string,out_dir:string", "Editor operation: extract selected. Archive operation; paths are validated by the editor backend."},
    {"archive_extract_all", "archive", "ExtractAll", "archive_path:string,out_dir:string,filter:string?,limit:integer?,filter_mode:string?", "Editor operation: extract all. Archive operation; paths are validated by the editor backend."},
    {"archive_compare", "archive", "Compare", "archive_path_a:string,archive_path_b:string", "Editor query: compare. Archive operation; paths are validated by the editor backend."},
    {"graph_core_palette", "graph", "GetCorePalette", "", "Editor query: get core palette. Blueprint behaviour-graph operation."},
    {"graph_search_palette", "graph", "SearchPalette", "kind:string?,query:string,script_filter:string?,limit:integer?", "Editor query: search palette. Blueprint behaviour-graph operation."},
    {"graph_node_signature", "graph", "GetNodeSignature", "node_type:string", "Editor query: get node signature. Blueprint behaviour-graph operation."},
    {"graph_validate", "graph", "ValidateGraph", "document_json:string", "Editor query: validate graph. Blueprint behaviour-graph operation."},
    {"graph_compile_to_source", "graph", "CompileToSource", "document_json:string", "Editor operation: compile to source. Blueprint behaviour-graph operation."},
    {"graph_compile_to_pex", "graph", "CompileToPex", "document_json:string,output_directory:string", "Editor operation: compile to pex. Blueprint behaviour-graph operation."},
    {"graph_load", "graph", "LoadGraph", "path:string", "Editor operation: load graph. Blueprint behaviour-graph operation."},
    {"load_script", "graph", "LoadScript", "path:string", "Editor operation: load script. Blueprint behaviour-graph operation."},
    {"graph_save", "graph", "SaveGraph", "path:string,document_json:string", "Editor operation: save graph. Blueprint behaviour-graph operation."},
};

}

std::span<const CatalogTool> serviceCatalogTools() { return kTools; }

}
