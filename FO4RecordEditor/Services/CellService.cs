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
    public static string GetPlacedReferencesJson(string cellId, object? env)
    {
        if (string.IsNullOrWhiteSpace(cellId))
            return JsonConvert.SerializeObject(new { error = "Provide 'cell_id' (a FormKey like '001234:Fallout4.esm', or an EditorID)." });

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
            // grouping for the viewer's layer list. `recordType` below is the PLACED record type, which
            // is "PlacedObject" for nearly every reference, so grouping on it collapses to ~2 groups.
            string? baseType = null;
            float scale = 1f;

            if (placed is IPlacedObjectGetter po)
            {
                scale = po.Scale ?? 1f;
                if (!po.Base.FormKey.IsNull)
                {
                    baseFormKey = po.Base.FormKey.ToString();
                    if (cache.TryResolve<IMajorRecordGetter>(po.Base.FormKey, out var baseRec))
                    {
                        baseEditorId = baseRec.EditorID;
                        baseType = baseRec.GetType().Name;
                        if (baseRec is IModeledGetter modeled) modelPath = modeled.Model?.File;
                    }
                }
            }

            if (modelPath != null) withModel++;

            refs.Add(new
            {
                formKey = placed.FormKey.ToString(),
                editorId = placed.EditorID,
                recordType = placed.GetType().Name,
                baseType,
                baseFormKey,
                baseEditorId,
                modelPath,
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
