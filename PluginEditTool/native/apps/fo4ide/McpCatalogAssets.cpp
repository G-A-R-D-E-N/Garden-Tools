#include "McpCatalog.hpp"

namespace fo4ide::app {
namespace {

const CatalogTool kTools[] = {
    {"browse_assets_categories", "assets", "BrowseCategories", "plugin:string?", "Asset query: browse categories. Lists placeable base-object groups with counts and the plugins that define them."},
    {"browse_assets_folder", "assets", "BrowseFolder", "path:string,group:string?,depth:integer?", "Asset query: browse folder. Walks the model folder tree of placeable assets under path, with counts and sample rows per folder."},
    {"search_assets", "assets", "SearchAssets", "text:string?,group:string?,folder:string?,plugin:string?,match:string?,field:string?,has_model:boolean?,sort:string?,limit:integer?,offset:integer?", "Asset query: search assets. Filters placeable base objects by text, group, model folder, plugin and model presence; paged."},
    {"get_asset_details", "assets", "GetAssetDetails", "base_form_key:string?,editor_id:string?", "Asset query: get asset details. One placeable base object by form key or unique editor ID, with model path and bounds."},
    {"place_asset", "assets", "PlaceAsset", "patch_plugin:string,cell_id:string,asset:string,position:object?,near_reference_key:string?,offset:object?,rotation:object?,scale:number?,count:integer?,spacing:object?,snap_grid:number?,persistent:boolean?,dry_run:boolean?", "Editor operation: place asset. Places an asset by form key or unique editor ID at position x,y,z, or near an existing reference with offset. count places copies (max 200) stepped by spacing x,y,z and optionally snapped to snap_grid, as one undo step. dry_run returns the plan without writing. Recorded for undo_cell_edit and redo_cell_edit; in memory until save_plugin."},
};

}

std::span<const CatalogTool> assetCatalogTools() { return kTools; }

}
