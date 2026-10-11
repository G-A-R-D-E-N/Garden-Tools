using System.Collections.Generic;
using System.Linq;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public sealed record MaterialSubstitution(string OriginalMaterial, string? ReplacementMaterial, float? ColorRemappingIndex);

public sealed unsafe partial class RadicleEnvironment
{
    public IReadOnlyList<MaterialSubstitution> MaterialSwap(Record record)
    {
        using var use = Use(record);
        Native.MaterialSwap* swap;
        Call.Check(RadicleNative.MaterialSwapDecode(use.Pointer, record.Handle, &swap));
        try
        {
            return Call.Copy<Native.MaterialSubstitution>(swap->Substitutions, swap->SubstitutionCount)
                .Select(s => new MaterialSubstitution(Utf8.Read(s.OriginalMaterial) ?? string.Empty,
                                                      Utf8.Read(s.ReplacementMaterial),
                                                      s.HasColorRemappingIndex != 0 ? s.ColorRemappingIndex : null))
                .ToArray();
        }
        finally
        {
            RadicleNative.MaterialSwapFree(swap);
        }
    }

    public FormKey? PlacedMaterialSwap(Record record)
    {
        using var use = Use(record);
        Native.FormKey key;
        Call.Check(RadicleNative.PlacedReferenceMaterialSwap(use.Pointer, record.Handle, &key));
        return FormKey.FromNative(key);
    }

    public FormKey? PrimaryModelMaterialSwap(Record record)
    {
        using var use = Use(record);
        Native.FormKey key;
        Call.Check(RadicleNative.RecordPrimaryModelMaterialSwap(use.Pointer, record.Handle, &key));
        return FormKey.FromNative(key);
    }

    public float? PrimaryModelColorRemappingIndex(Record record)
    {
        using var use = Use(record);
        float index;
        return Call.Found(RadicleNative.RecordPrimaryModelColorRemappingIndex(use.Pointer, record.Handle, &index)) ? index : null;
    }
}
