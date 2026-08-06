using System.Linq;
using Mutagen.Bethesda.Fallout4;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Plugins.Records;
using JsonConvert = Newtonsoft.Json.JsonConvert;

namespace FO4RecordEditor.Services;

/// <summary>
/// Reads a CELL's placed references (REFR/ACHR/traps) for the 3D cell viewer -- position, rotation,
/// scale, and (for anything with a static model) the NIF path to render. No Mutagen call resolves
/// this in one shot: a placed reference only carries a FormLink to its base object, and only some
/// base object types (statics, activators, containers, doors, furniture, trees, flora, weapons, ...)
/// carry a Model subrecord at all -- actors and traps don't, so those come back with modelPath=null
/// and the viewer just draws a marker instead of a mesh for them.
///
/// This is read-only. It is NOT the write-side CreateCell/CreatePlacedObject path in
/// WriteService.Placed.cs, which walks a single mutable mod's own Cells tree instead (that's for
/// authoring a cell this plugin owns, not for viewing whichever cell is actually loaded).
///
/// A cell's REFR contents are UNIONED across the whole load order, not simple override-wins, unlike
/// almost every other record type in this app. Confirmed empirically, not assumed: the plain "winning
/// record" resolution (cache.TryResolve, same as get_record/list_records) returned 0 references for
/// a real busy interior cell whose individual per-plugin copies had thousands between them -- the
/// game engine loads every plugin's own Temporary/Persistent children for a given cell FormID
/// additively, so a plugin that overrides the cell's water height (say) without touching references
/// at all still "wins" the CELL record with EMPTY reference lists, silently hiding everyone else's
/// placed objects if you only look at the winning copy. So: scalar cell metadata (name, interior
/// flag) comes from the winning record, but the reference list comes from
/// MutagenLoader.GetRecordContexts walking every plugin's own copy in load order and merging
/// per-REFR-FormKey (later plugin wins that one reference; IsDeleted removes it), the same
/// last-plugin-wins semantics the game itself applies to an individual reference override.
/// </summary>
public static class CellService
{
    /// <summary>
    /// #64: resolve an EXTERIOR cell by worldspace + grid coordinate instead of by FormKey/EditorID.
    /// Verified against Mutagen's actual Worldspace layout before writing this, not assumed: a
    /// worldspace's exterior cells are NOT a flat list, they are nested Worldspace.SubCells
    /// (WorldspaceBlock) -> WorldspaceBlock.Items (WorldspaceSubBlock) -> WorldspaceSubBlock.Items
    /// (the actual Cell records), and each Cell's grid position is Cell.Grid.Point (a P2Int X/Y),
    /// not a FormLink back to its parent worldspace -- the relationship is purely structural
    /// (confirmed via Worldspace_Generated.cs / WorldspaceSubBlock_Generated.cs).
    ///
    /// Walks every plugin's own copy of the worldspace (GetRecordContexts, same reasoning as the
    /// interior-cell reference union below: a mod adding a new exterior cell authors its own copy of
    /// the block/subblock tree at that grid position) and returns the first cell FormKey found at
    /// (gridX, gridY) -- every plugin's copy of the SAME logical cell shares one FormKey, so the
    /// first hit is enough once resolved, this just widens the search past whichever plugin happens
    /// to be resolved as the worldspace's own "winning" copy.
    /// </summary>
    public static string? ResolveExteriorCellFormKey(object? env, string worldspaceId, int gridX, int gridY, out string? error)
    {
        var wsFk = MutagenLoader.ResolveId(env, worldspaceId.Trim());
        if (wsFk.IsNull) { error = $"Could not resolve worldspace '{worldspaceId}' to a loaded WRLD record."; return null; }

        foreach (var (_, rec) in MutagenLoader.GetRecordContexts(env, wsFk))
        {
            if (rec is not IWorldspaceGetter ws) continue;
            foreach (var block in ws.SubCells)
                foreach (var subBlock in block.Items)
                    foreach (var cell in subBlock.Items)
                        if (cell.Grid != null && cell.Grid.Point.X == gridX && cell.Grid.Point.Y == gridY)
                        {
                            error = null;
                            return cell.FormKey.ToString();
                        }
        }
        error = $"No exterior cell found at grid ({gridX},{gridY}) in worldspace '{worldspaceId}'.";
        return null;
    }

    public static string GetPlacedReferencesJson(string cellId, object? env, string? worldspace = null, int? gridX = null, int? gridY = null)
    {
        if (string.IsNullOrWhiteSpace(cellId) && !string.IsNullOrWhiteSpace(worldspace) && gridX.HasValue && gridY.HasValue)
        {
            var resolved = ResolveExteriorCellFormKey(env, worldspace, gridX.Value, gridY.Value, out var gridErr);
            if (resolved == null) return JsonConvert.SerializeObject(new { error = gridErr });
            cellId = resolved;
        }

        if (string.IsNullOrWhiteSpace(cellId))
            return JsonConvert.SerializeObject(new { error = "Provide 'cell_id' (a FormKey like '001234:Fallout4.esm', or an EditorID), or 'worldspace' + 'grid_x' + 'grid_y' for an exterior cell." });

        var cache = MutagenLoader.LinkCache;
        if (cache == null)
            return JsonConvert.SerializeObject(new { error = "No environment loaded. Load Env or Open MO2 first." });

        var fk = MutagenLoader.ResolveId(env, cellId.Trim());
        if (fk.IsNull || !cache.TryResolve<ICellGetter>(fk, out var winningCell))
            return JsonConvert.SerializeObject(new { error = $"Could not resolve '{cellId}' to a loaded CELL record." });

        var interior = winningCell.Flags.HasFlag(Cell.Flag.IsInteriorCell);

        // Union every plugin's own copy of this cell's reference lists -- see class doc for why the
        // winning-record-only approach silently drops references.
        var merged = new Dictionary<FormKey, IPlacedGetter>();
        foreach (var (_, rec) in MutagenLoader.GetRecordContexts(env, fk))
        {
            if (rec is not ICellGetter cellCopy) continue;
            foreach (var placed in cellCopy.Persistent.Concat(cellCopy.Temporary))
            {
                if (placed.IsDeleted) { merged.Remove(placed.FormKey); continue; }
                merged[placed.FormKey] = placed;
            }
        }

        var refs = new List<object>();
        var withModel = 0;
        foreach (var placed in merged.Values)
        {
            // IPlacedGetter itself carries no spatial data -- Position/Rotation live on the concrete
            // types (PlacedObject/PlacedNpc/APlacedTrap) via IPositionRotationGetter, which every
            // real-world placed type implements, so this only skips something genuinely degenerate.
            if (placed is not IPositionRotationGetter posRot) continue;

            string? modelPath = null;
            string? baseEditorId = null;
            string baseFormKey = "";
            // The base object's record type (Static/Light/Furniture/MoveableStatic/...) -- the useful
            // grouping for the viewer's layer list. `recordType` below is the PLACED record's own kind
            // (PlacedObject/PlacedNpc/PlacedTrap), NOT the base -- a PlacedObject is "PlacedObject"
            // regardless of what it's placed as, so grouping on baseType is what actually matters.
            string? baseType = null;
            float scale = 1f;
            // A shape with no texture of its own can render via a ground decal instead: the base is a
            // TextureSet (TXST), not anything with a Model at all -- confirmed via get_record on a real
            // decal reference ("Type: PlacedObject", Base resolves to a genuine TXST with its own
            // Decal subrecord: Diffuse texture + Width/Height/Depth/Flags). Emitted alongside modelPath
            // (which stays null for these -- TXST has no Model field) so the viewer can draw a real
            // textured decal plane instead of a generic "no model" marker.
            string? decalDiffuse = null;
            float? decalWidth = null;
            float? decalHeight = null;
            // Static Collections (SCOL) bake a precombined Model.File, but a LOT of mods ship the
            // SCOL record without ever running the CK's "generate precombine" step -- confirmed on a
            // real case (SwitchBoard_Reborn.esp's SCOL\SwitchBoard_Reborn.esp\CM000000EA.NIF): the
            // mod's own BA2 has zero SCOL entries at all, so that path can NEVER resolve, loose or
            // archived. The game engine's own fallback in that case is to render each of the SCOL's
            // member statics individually at their own local placement -- this is that fallback,
            // exposed so the viewer can do the same instead of showing a "mesh unavailable" marker
            // for what's often a large chunk of a cell's own structure (floors/walls built from SCOL).
            List<object>? scolParts = null;

            if (placed is IPlacedObjectGetter po)
            {
                scale = po.Scale ?? 1f;
                if (!po.Base.FormKey.IsNull)
                {
                    baseFormKey = po.Base.FormKey.ToString();
                    if (cache.TryResolve<IMajorRecordGetter>(po.Base.FormKey, out var baseRec))
                    {
                        baseEditorId = baseRec.EditorID;
                        // Mutagen's lazy binary-overlay wrapper class is named "<RealType>BinaryOverlay"
                        // (confirmed: a real TextureSet base's runtime type reported "TextureSetBinaryOverlay",
                        // caught by CellServiceTests against a known real record, not assumed) -- strip it
                        // so the layer list groups by the real record kind ("Static", "TextureSet", ...)
                        // instead of every layer label carrying this internal loading-detail suffix.
                        baseType = baseRec.GetType().Name;
                        if (baseType.EndsWith("BinaryOverlay", StringComparison.Ordinal))
                            baseType = baseType[..^"BinaryOverlay".Length];
                        if (baseRec is IModeledGetter modeled) modelPath = modeled.Model?.File;
                        if (baseRec is IStaticCollectionGetter scol)
                        {
                            var parts = new List<object>();
                            foreach (var part in scol.Parts)
                            {
                                if (part.Static.IsNull) continue;
                                if (!cache.TryResolve<IMajorRecordGetter>(part.Static.FormKey, out var partRec)) continue;
                                if (partRec is not IModeledGetter partModeled || string.IsNullOrWhiteSpace(partModeled.Model?.File)) continue;
                                var placements = part.Placements;
                                if (placements == null || placements.Count == 0) continue;
                                parts.Add(new
                                {
                                    modelPath = partModeled.Model!.File,
                                    placements = placements.Select(p => new
                                    {
                                        x = p.Position.X, y = p.Position.Y, z = p.Position.Z,
                                        rx = p.Rotation.X, ry = p.Rotation.Y, rz = p.Rotation.Z,
                                        scale = p.Scale,
                                    }).ToList(),
                                });
                            }
                            if (parts.Count > 0) scolParts = parts;
                        }
                        if (baseRec is ITextureSetGetter txst && !string.IsNullOrWhiteSpace(txst.Diffuse))
                        {
                            decalDiffuse = txst.Diffuse;
                            // ObjectBounds is in the same world-unit convention as everything else this
                            // viewer already renders (unlike the Decal subrecord's own MinWidth/MaxWidth,
                            // whose unit convention wasn't confirmed against a known-good real-world
                            // reference) -- X/Y extent of the bounding box is a reasonable stand-in for
                            // the decal's real footprint. Falls back to the Decal subrecord's own
                            // Width/Height only if ObjectBounds is degenerate (both zero).
                            var b = baseRec is IObjectBoundedGetter ob ? ob.ObjectBounds : null;
                            var bw = b != null ? Math.Abs(b.Second.X - b.First.X) : 0f;
                            var bh = b != null ? Math.Abs(b.Second.Y - b.First.Y) : 0f;
                            if (bw > 0.01f && bh > 0.01f) { decalWidth = bw; decalHeight = bh; }
                            else if (txst.Decal != null) { decalWidth = txst.Decal.MinWidth; decalHeight = txst.Decal.MinHeight; }
                        }
                    }
                }
            }

            if (modelPath != null) withModel++;

            string recordType = placed switch
            {
                IPlacedObjectGetter => "PlacedObject",
                IPlacedNpcGetter => "PlacedNpc",
                IAPlacedTrapGetter => "PlacedTrap",
                _ => placed.GetType().Name,
            };

            refs.Add(new
            {
                formKey = placed.FormKey.ToString(),
                editorId = placed.EditorID,
                recordType,
                baseType,
                baseFormKey,
                baseEditorId,
                modelPath,
                decalDiffuse,
                decalWidth,
                decalHeight,
                scolParts,
                position = new { x = posRot.Position.X, y = posRot.Position.Y, z = posRot.Position.Z },
                rotation = new { x = posRot.Rotation.X, y = posRot.Rotation.Y, z = posRot.Rotation.Z },
                scale,
            });
        }

        return JsonConvert.SerializeObject(new
        {
            cellFormKey = winningCell.FormKey.ToString(),
            cellEditorId = winningCell.EditorID,
            cellName = winningCell.Name?.String,
            interior,
            referenceCount = refs.Count,
            withModelCount = withModel,
            references = refs,
        });
    }
}
