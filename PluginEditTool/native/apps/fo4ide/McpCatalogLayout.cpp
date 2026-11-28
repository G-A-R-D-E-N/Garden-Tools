#include "McpCatalog.hpp"

namespace fo4ide::app {
namespace {

const CatalogTool kTools[] = {
    {"get_model_bounds", "layout", "GetModelBounds", "model_paths:dlist?,base_form_keys:array?", "Layout query: local bounds of model meshes with per-shape transforms applied. Give exactly one of model_paths or base_form_keys. Bounds are cached per model."},
    {"describe_kit", "layout", "DescribeKit", "name_prefix:string?,plugin:string?,model_dir:string?", "Layout query: describe a modular kit: pieces with category, bounds and the detected grid. Give at least one of name_prefix, plugin or model_dir. Read-only."},
    {"snap_transform", "layout", "SnapTransform", "base_form_key:string,near:object,grid:number?,neighbour_keys:array?", "Layout query: suggest a transform for a base object near a point, snapped to a grid and butted against neighbour references. Read-only."},
    {"build_room", "layout", "BuildRoom", "patch_plugin:string,cell_id:string,kit:object,size:object,origin:object,grid:number?,dry_run:boolean?", "Layout operation: build a room from a kit of floor, wall, corner, ceiling and door pieces into a cell of a patch plugin. kit pieces are {baseFormKey, min, max}; size is {cells_x, cells_y, height_cells}; origin is {x, y, z}. dry_run returns the placements without writing. At most 200 placements per call. Recorded for undo and redo."},
    {"check_overlaps", "layout", "CheckOverlaps", "cell_id:string,tolerance:number?", "Layout query: list placed references whose bounds overlap in a cell. Markers and references whose model cannot be read are skipped and listed. Read-only."},
    {"align_references", "layout", "AlignReferences", "patch_plugin:string,keys:array,mode:string,grid:number?", "Layout operation: align placed references in a patch plugin. mode is floor, grid, distribute_x or distribute_y; grid needs grid > 0; distribute needs at least three keys. At most 200 keys per call. Recorded for undo and redo."},
};

}

std::span<const CatalogTool> layoutCatalogTools() { return kTools; }

}
