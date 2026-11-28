using System;
using System.Collections.Generic;
using System.Linq;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public enum ResolvedMaterialSource
{
    NifOnly = 0,
    Bgsm = 1,
    Bgem = 2,
}

public enum ResolvedAlphaMode
{
    Opaque = 0,
    Test = 1,
    Blend = 2,
}

/// <summary>Invariant: Textures always holds 10 lowercase Data-relative paths in the lighting texture-set slot order (0 diffuse, 1 normal, 2 glow, 3 greyscale, 4 envmap, 5 envmask, 7 smooth-spec), empty for an unused slot; BlendSrc and BlendDst are the NiAlphaProperty blend enum.</summary>
public sealed record ResolvedMaterial(
    ResolvedMaterialSource Source,
    string NifMaterialPath,
    string MaterialPath,
    IReadOnlyList<string> Textures,
    ResolvedAlphaMode AlphaMode,
    byte AlphaThreshold,
    byte BlendSrc,
    byte BlendDst,
    float Alpha,
    bool TwoSided,
    bool HasGlow,
    bool HasWet,
    (float U, float V) UvOffset,
    (float U, float V) UvScale,
    MaterialColor Emissive,
    float EmissiveMult,
    bool Swapped,
    float? ColorRemappingIndex,
    IReadOnlyList<string> Warnings)
{
    internal static unsafe ResolvedMaterial Resolve(IntPtr nif, int shapeIndex, AssetResolver assets,
                                                    IReadOnlyList<MaterialSubstitution>? swap)
    {
        if (shapeIndex < 0) throw new ArgumentOutOfRangeException(nameof(shapeIndex));
        using var use = assets.Use();
        Native.ResolvedMaterial* material;
        Call.Check(RadicleNative.MaterialResolve(nif, (nuint)shapeIndex, use.Pointer, &material));
        try
        {
            if (swap is { Count: > 0 })
            {
                using var strings = new NativeStrings();
                var items = swap.Select(s => new Native.MaterialSubstitution
                {
                    OriginalMaterial = strings.Add(s.OriginalMaterial),
                    ReplacementMaterial = strings.Add(s.ReplacementMaterial),
                    HasColorRemappingIndex = s.ColorRemappingIndex.HasValue ? 1 : 0,
                    ColorRemappingIndex = s.ColorRemappingIndex ?? 0f,
                }).ToArray();
                fixed (Native.MaterialSubstitution* pointer = items)
                {
                    var view = new Native.MaterialSwap { Substitutions = (IntPtr)pointer, SubstitutionCount = (nuint)items.Length };
                    int applied;
                    Call.Check(RadicleNative.ResolvedMaterialApplySwap(material, nif, (nuint)shapeIndex, use.Pointer, &view, &applied));
                }
            }
            return Copy(*material);
        }
        finally
        {
            RadicleNative.ResolvedMaterialFree(material);
        }
    }

    private static unsafe string[] Strings(IntPtr items, nuint count)
    {
        var result = new string[checked((int)count)];
        for (var i = 0; i < result.Length; ++i) result[i] = Utf8.Read(((IntPtr*)items)[i]) ?? string.Empty;
        return result;
    }

    private static unsafe ResolvedMaterial Copy(Native.ResolvedMaterial m) => new(
        (ResolvedMaterialSource)m.Source,
        Utf8.Read(m.NifMaterialPath) ?? string.Empty,
        Utf8.Read(m.MaterialPath) ?? string.Empty,
        Strings(m.Textures, m.TextureCount),
        (ResolvedAlphaMode)m.AlphaMode,
        m.AlphaThreshold,
        m.BlendSrc,
        m.BlendDst,
        m.Alpha,
        m.TwoSided != 0,
        m.HasGlow != 0,
        m.HasWet != 0,
        (m.UvOffset[0], m.UvOffset[1]),
        (m.UvScale[0], m.UvScale[1]),
        new MaterialColor(m.Emissive.R, m.Emissive.G, m.Emissive.B),
        m.EmissiveMult,
        m.Swapped != 0,
        m.HasColorRemappingIndex != 0 ? m.ColorRemappingIndex : null,
        Strings(m.Warnings, m.WarningCount));
}

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
