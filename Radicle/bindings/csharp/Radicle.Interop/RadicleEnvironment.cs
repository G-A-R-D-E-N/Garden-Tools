using System;
using System.Collections.Generic;
using System.Linq;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public sealed record PluginInfo(string Filename, string Path, Native.PluginKind Kind, bool MasterFlag, bool Localized,
                                IReadOnlyList<string> Masters, IReadOnlyList<string> UnresolvedMasters);

public readonly record struct Bounds(short MinX, short MinY, short MinZ, short MaxX, short MaxY, short MaxZ);

public sealed record RecordInfo(FormKey FormKey, string Signature, int PluginIndex, uint RecordFlags, bool Deleted,
                                string? EditorId, NameKind NameKind, string? NameText, uint NameStringId,
                                string? ModelPath, Bounds? Bounds);

public sealed record RecordContainer(int GroupType, FormKey? Worldspace, FormKey? Cell);

public readonly record struct Record(IntPtr Handle, RadicleEnvironment Owner);

public readonly record struct CellChild(Record Record, bool Persistent);

public sealed record WorldspaceInfo(FormKey? Parent, ushort? ParentFlags, FormKey? Climate, FormKey? Water,
                                    FormKey? LightingTemplate, float? DefaultLandHeight, float? DefaultWaterHeight,
                                    (float X, float Y)? BoundsMin, (float X, float Y)? BoundsMax, byte? Flags,
                                    FormKey? LodWater = null, float? LodWaterHeight = null);

public readonly record struct ByteColor(byte Red, byte Green, byte Blue);

public sealed record Lighting(ByteColor Ambient, ByteColor Directional, ByteColor FogNearColor, float FogNear,
                              float FogFar, int DirectionalRotationXy, int DirectionalRotationZ, float DirectionalFade,
                              float FogClipDistance, float FogPower, ByteColor? FogFarColor, float? FogMax,
                              (float Begin, float End)? LightFade, uint? Inherits,
                              (float Mid, float Range)? NearHeight = null,
                              (ByteColor Near, ByteColor Far)? HighFogColors = null, float? HighDensityScale = null,
                              (float Near, float Far, float HighNear, float HighFar)? FogScales = null,
                              (float Mid, float Range)? FarHeight = null);

public sealed record CellInfo(ushort? Flags, (int X, int Y)? Grid, byte? LandFlags, Lighting? Lighting,
                              FormKey? LightingTemplate, float? WaterHeight, FormKey? ImageSpace,
                              FormKey? EncounterZone, FormKey? Location, FormKey? Worldspace, FormKey? Water = null,
                              ushort? PrevisTimestamp = null, ushort? PrecombinedTimestamp = null,
                              FormKey? AcousticSpace = null, FormKey? MusicType = null,
                              FormKey? SkyWeatherRegion = null, FormKey? GodRays = null, FormKey? LockList = null,
                              FormKey? Owner = null, int? OwnerRank = null,
                              (FormKey? Worldspace, float X, float Y, float Z)? ExteriorLod = null,
                              uint? WaterCurrentCount = null, string? WaterEnvironmentMap = null);

public sealed record TextureSetInfo(IReadOnlyList<string?> Textures, string? Material, ushort? Flags,
                                    IReadOnlyList<float> Decal);

public sealed record LandLayer(bool IsBase, FormKey? Texture, byte Quadrant, ushort Layer, byte[] Alpha);

public sealed record Landscape(uint? Flags, float? HeightOffset, byte[]? Heights, byte[]? Normals, byte[]? Colors,
                               IReadOnlyList<LandLayer> Layers);

public sealed record LandHeightGrid(float Base, float[] Heights);

public sealed record LandBlendLayer(FormKey? Texture, float[] Weights);

public sealed record LandBlend(IReadOnlyList<FormKey?> Base, IReadOnlyList<LandBlendLayer> Layers);

public sealed record LightInfo(int Time, uint Radius, ByteColor Color, uint Flags, float FalloffExponent, float Fov,
                               float NearClip, float FlickerPeriod, float FlickerIntensityAmplitude,
                               float FlickerMovementAmplitude, float Constant, float Scalar, float? Exponent,
                               float? GodRaysNearClip, uint? Value, float? Weight);

public sealed record LightRecord(LightInfo? Data, float? Fade, string? Gobo, FormKey? Lens, FormKey? GodRays);

public sealed record AddOnNodeRecord(uint? NodeIndex, FormKey? Light, string? Model, ushort? MasterParticleCap, ushort? Flags);

public sealed record BendableSpline(float? DefaultTiles, ushort? DefaultSlices, bool? TilesRelativeToLength,
                                    (float R, float G, float B)? DefaultColor, float? WindSensibility,
                                    float? WindFlexibility, FormKey? Texture);

public sealed record LightingTemplate(Lighting? Lighting, FormKey? GodRays);

public sealed record ClimateWeather(FormKey? Weather, int Chance, FormKey? Global);

public sealed record Climate(IReadOnlyList<ClimateWeather> Weathers, string? SunTexture, string? SunGlareTexture)
{
    public ClimateTiming? Timing { get; init; }
}

public sealed record Weather(int ColorGroups, int ColorTimes, byte[] Colors, IReadOnlyList<float> FogDistances)
{
    public WeatherParameters? Parameters { get; init; }
    public const int Day = 1;

    public ByteColor? Color(int group, int time)
    {
        if (group < 0 || group >= ColorGroups || time < 0 || time >= ColorTimes) return null;
        var offset = (group * ColorTimes + time) * 4;
        return new ByteColor(Colors[offset], Colors[offset + 1], Colors[offset + 2]);
    }
}

public readonly record struct StaticPlacement((float X, float Y, float Z) Position, (float X, float Y, float Z) Rotation,
                                              float Scale);

public sealed record StaticPart(FormKey? Static, IReadOnlyList<StaticPlacement> Placements);

public sealed record DistantLod(int LevelCount, IReadOnlyList<string?> Meshes);

public sealed record WaterNoiseLayer(float WindDirection, float WindSpeed, float AmplitudeScale, float UvScale, float NoiseFalloff,
                                     string? Texture);

public sealed record WaterVisual(
    float FogDepthAmount, ByteColor FogShallowColor, ByteColor FogDeepColor, float FogColorShallowRange,
    float FogColorDeepRange, float FogShallowAlpha, float FogDeepAlpha, float FogAlphaShallowRange, float FogAlphaDeepRange,
    ByteColor FogUnderwaterColor, float FogUnderwaterAmount, float FogUnderwaterNear, float FogUnderwaterFar,
    float NormalMagnitude, float ShallowNormalFalloff, float DeepNormalFalloff, float ReflectivityAmount, float FresnelAmount,
    float SurfaceEffectFalloff, float DisplacementForce, float DisplacementVelocity, float DisplacementFalloff,
    float DisplacementDampener, float DisplacementStartingSize, ByteColor ReflectionColor, float SunSpecularPower,
    float SunSpecularMagnitude, float SunSparklePower, float SunSparkleMagnitude, float InteriorSpecularRadius,
    float InteriorSpecularBrightness, float InteriorSpecularPower, float? SiltAmount, ByteColor? SiltLightColor,
    ByteColor? SiltDarkColor, bool? ScreenSpaceReflections);

public sealed record Water(byte? Flags, WaterVisual? Visual, IReadOnlyList<WaterNoiseLayer> Noise,
                           (float X, float Y, float Z)? LinearVelocity, (float X, float Y, float Z)? AngularVelocity);

public sealed unsafe partial class RadicleEnvironment : IDisposable
{
    private readonly EnvironmentHandle _handle;

    static RadicleEnvironment() => RadicleLibrary.Register();

    private RadicleEnvironment(IntPtr handle) => _handle = new EnvironmentHandle(handle);

    internal HandleUse Use() => new(_handle, nameof(RadicleEnvironment));

    internal HandleUse Use(Record record)
    {
        if (record.Owner != this) throw new ArgumentException("The record does not belong to this environment.", nameof(record));
        return Use();
    }

    public static RadicleEnvironment Load(IEnumerable<string> orderedPaths)
    {
        using var strings = new NativeStrings();
        var paths = orderedPaths.Select(strings.Add).ToArray();
        IntPtr env;
        fixed (IntPtr* p = paths) Call.Check(RadicleNative.EnvLoad(p, (nuint)paths.Length, &env));
        return new RadicleEnvironment(env);
    }

    public void Dispose() => _handle.Dispose();

    public IReadOnlyList<PluginInfo> Plugins
    {
        get
        {
            using var use = Use();
            var count = (int)RadicleNative.EnvPluginCount(use.Pointer);
            var result = new List<PluginInfo>(count);
            for (var i = 0; i < count; ++i)
            {
                EnvPluginInfo info;
                Call.Check(RadicleNative.EnvGetPluginInfo(use.Pointer, (nuint)i, &info));
                var masters = new List<string>();
                var unresolved = new List<string>();
                for (var m = 0; m < (int)info.MasterCount; ++m)
                {
                    IntPtr name;
                    int resolved;
                    Call.Check(RadicleNative.EnvPluginMaster(use.Pointer, (nuint)i, (nuint)m, &name, &resolved));
                    masters.Add(Utf8.Read(name)!);
                    if (resolved == 0) unresolved.Add(Utf8.Read(name)!);
                }
                result.Add(new PluginInfo(Utf8.Read(info.Filename)!, Utf8.Read(info.Path)!, info.Kind, info.MasterFlag != 0,
                                          info.Localized != 0, masters, unresolved));
            }
            return result;
        }
    }

    public int? FindPlugin(string filename)
    {
        using var use = Use();
        nuint index;
        return Call.Found(RadicleNative.EnvFindPlugin(use.Pointer, filename, &index)) ? (int)index : null;
    }

    public Record? Winner(FormKey key)
    {
        using var use = Use();
        using var s = new NativeStrings();
        IntPtr record;
        return Call.Found(RadicleNative.EnvWinner(use.Pointer, s.Key(key), &record)) ? new Record(record, this) : null;
    }

    public IReadOnlyList<Record> Versions(FormKey key)
    {
        using var use = Use();
        using var s = new NativeStrings();
        IntPtr* items;
        nuint count;
        Call.Check(RadicleNative.EnvVersions(use.Pointer, s.Key(key), &items, &count));
        return Records(items, count);
    }

    public Record? FindEditorId(string editorId, string? signature = null)
    {
        using var use = Use();
        IntPtr record;
        return Call.Found(RadicleNative.EnvFindEditorId(use.Pointer, editorId, signature, &record)) ? new Record(record, this) : null;
    }

    public IReadOnlyList<Record> WinnersOf(string signature)
    {
        using var use = Use();
        IntPtr* items;
        nuint count;
        Call.Check(RadicleNative.EnvWinnersOf(use.Pointer, signature, &items, &count));
        return Records(items, count);
    }

    public IReadOnlyList<Record> PluginRecords(int pluginIndex)
    {
        using var use = Use();
        IntPtr* items;
        nuint count;
        Call.Check(RadicleNative.EnvPluginRecords(use.Pointer, (nuint)pluginIndex, &items, &count));
        return Records(items, count);
    }

    public RecordInfo Info(Record record)
    {
        using var use = Use(record);
        Native.RecordInfo info;
        Call.Check(RadicleNative.RecordGetInfo(use.Pointer, record.Handle, &info));
        Bounds? bounds = info.HasBounds != 0
            ? new Bounds(info.BoundsMin[0], info.BoundsMin[1], info.BoundsMin[2], info.BoundsMax[0], info.BoundsMax[1], info.BoundsMax[2])
            : null;
        return new RecordInfo(new FormKey(Utf8.Read(info.OwnerPlugin)!, info.LocalId), Signature(info.Signature),
                              (int)info.PluginIndex, info.RecordFlags, info.Deleted != 0, Utf8.Read(info.EditorId),
                              info.NameKind, Utf8.Read(info.NameText), info.NameStringId, Utf8.Read(info.ModelPath), bounds);
    }

    public string? PrimaryModelPath(Record record)
    {
        using var use = Use(record);
        IntPtr path;
        return Call.Found(RadicleNative.RecordPrimaryModelPath(use.Pointer, record.Handle, &path)) ? Utf8.Read(path) : null;
    }

    public RecordContainer Container(Record record)
    {
        using var use = Use(record);
        int group;
        Native.FormKey world, cell;
        Call.Check(RadicleNative.RecordGetContainer(use.Pointer, record.Handle, &group, &world, &cell));
        return new RecordContainer(group, FormKey.FromNative(world), FormKey.FromNative(cell));
    }

    public WorldspaceInfo Worldspace(Record record)
    {
        using var use = Use(record);
        Native.WorldspaceInfo w;
        Call.Check(RadicleNative.WorldspaceGetInfo(use.Pointer, record.Handle, &w));
        return new WorldspaceInfo(FormKey.FromNative(w.Parent), w.HasParentFlags != 0 ? w.ParentFlags : null,
                                  FormKey.FromNative(w.Climate), FormKey.FromNative(w.Water),
                                  FormKey.FromNative(w.LightingTemplate),
                                  w.HasLandDefaults != 0 ? w.DefaultLandHeight : null,
                                  w.HasLandDefaults != 0 ? w.DefaultWaterHeight : null,
                                  w.HasBoundsMin != 0 ? (w.BoundsMin[0], w.BoundsMin[1]) : null,
                                  w.HasBoundsMax != 0 ? (w.BoundsMax[0], w.BoundsMax[1]) : null,
                                  w.HasFlags != 0 ? w.Flags : null, FormKey.FromNative(w.LodWater),
                                  w.HasLodWaterHeight != 0 ? w.LodWaterHeight : null);
    }

    public CellInfo Cell(Record record)
    {
        using var use = Use(record);
        Native.CellInfo c;
        Call.Check(RadicleNative.CellGetInfo(use.Pointer, record.Handle, &c));
        return new CellInfo(c.HasFlags != 0 ? c.Flags : null, c.HasGrid != 0 ? (c.GridX, c.GridY) : null,
                            c.HasLandFlags != 0 ? c.LandFlags : null,
                            c.HasLighting != 0 ? ToLighting(c.Lighting, c.LightingTail) : null,
                            FormKey.FromNative(c.LightingTemplate), c.HasWaterHeight != 0 ? c.WaterHeight : null,
                            FormKey.FromNative(c.ImageSpace), FormKey.FromNative(c.EncounterZone),
                            FormKey.FromNative(c.Location), FormKey.FromNative(c.Worldspace),
                            FormKey.FromNative(c.Water),
                            c.HasPrevisTimestamp != 0 ? c.PrevisTimestamp : null,
                            c.HasPrecombinedTimestamp != 0 ? c.PrecombinedTimestamp : null,
                            FormKey.FromNative(c.AcousticSpace), FormKey.FromNative(c.MusicType),
                            FormKey.FromNative(c.SkyWeatherRegion), FormKey.FromNative(c.GodRays),
                            FormKey.FromNative(c.LockList), FormKey.FromNative(c.Owner),
                            c.HasOwnerRank != 0 ? c.OwnerRank : null,
                            c.HasExteriorLod != 0
                                ? (FormKey.FromNative(c.ExteriorLodWorldspace), c.ExteriorLodOffset[0],
                                   c.ExteriorLodOffset[1], c.ExteriorLodOffset[2])
                                : null,
                            c.HasWaterCurrentCount != 0 ? c.WaterCurrentCount : null,
                            Utf8.Read(c.WaterEnvironmentMap));
    }

    public IReadOnlyList<FormKey?> CellRegions(Record record)
    {
        using var use = Use(record);
        Native.FormKey* items;
        nuint count;
        Call.Check(RadicleNative.CellGetRegions(use.Pointer, record.Handle, &items, &count));
        try { return Call.Copy<Native.FormKey>((IntPtr)items, count).Select(FormKey.FromNative).ToArray(); }
        finally { RadicleNative.FreeFormKeys(items); }
    }

    public IReadOnlyList<FormKey?> CellPrevisReferences(Record record)
    {
        using var use = Use(record);
        Native.FormKey* items;
        nuint count;
        Call.Check(RadicleNative.CellGetPrevisReferences(use.Pointer, record.Handle, &items, &count));
        try { return Call.Copy<Native.FormKey>((IntPtr)items, count).Select(FormKey.FromNative).ToArray(); }
        finally { RadicleNative.FreeFormKeys(items); }
    }

    public IReadOnlyList<(float X, float Y, float Z, float Unnamed)> CellWaterCurrents(Record record)
    {
        using var use = Use(record);
        Native.WaterCurrent* items;
        nuint count;
        Call.Check(RadicleNative.CellGetWaterCurrents(use.Pointer, record.Handle, &items, &count));
        try
        {
            return Call.Copy<Native.WaterCurrent>((IntPtr)items, count)
                .Select(current => (current.Velocity[0], current.Velocity[1], current.Velocity[2], current.Unnamed))
                .ToArray();
        }
        finally { RadicleNative.FreeWaterCurrents(items); }
    }

    public Record? WorldspaceTopCell(FormKey worldspace)
    {
        using var use = Use();
        using var s = new NativeStrings();
        IntPtr record;
        return Call.Found(RadicleNative.EnvWorldspaceTopCell(use.Pointer, s.Key(worldspace), &record)) ? new Record(record, this) : null;
    }

    public IReadOnlyList<Record> WorldspaceCells(FormKey worldspace)
    {
        using var use = Use();
        using var s = new NativeStrings();
        IntPtr* items;
        nuint count;
        Call.Check(RadicleNative.EnvWorldspaceCells(use.Pointer, s.Key(worldspace), &items, &count));
        return Records(items, count);
    }

    public IReadOnlyList<Record> InteriorCells()
    {
        using var use = Use();
        IntPtr* items;
        nuint count;
        Call.Check(RadicleNative.EnvInteriorCells(use.Pointer, &items, &count));
        return Records(items, count);
    }

    public IReadOnlyList<CellChild> CellChildren(FormKey cell)
    {
        using var use = Use();
        using var s = new NativeStrings();
        Native.CellChild* items;
        nuint count;
        Call.Check(RadicleNative.EnvCellChildren(use.Pointer, s.Key(cell), &items, &count));
        var result = new CellChild[(int)count];
        for (var i = 0; i < result.Length; ++i) result[i] = new CellChild(new Record(items[i].Record, this), items[i].Persistent != 0);
        if (items != null) RadicleNative.FreeCellChildren(items);
        return result;
    }

    public Record? CellLandscape(FormKey cell)
    {
        using var use = Use();
        using var s = new NativeStrings();
        IntPtr record;
        return Call.Found(RadicleNative.EnvCellLandscape(use.Pointer, s.Key(cell), &record)) ? new Record(record, this) : null;
    }

    public Landscape Landscape(Record record)
    {
        using var use = Use(record);
        Native.Landscape* land;
        Call.Check(RadicleNative.LandscapeDecode(use.Pointer, record.Handle, &land));
        try
        {
            const int vertices = 33 * 33;
            var layers = new List<LandLayer>();
            var native = (Native.LandLayer*)land->Layers;
            for (var i = 0; i < (int)land->LayerCount; ++i)
            {
                var layer = native[i];
                layers.Add(new LandLayer(layer.IsBase != 0, FormKey.FromNative(layer.Texture), layer.Quadrant, layer.Layer,
                                         Utf8.Raw(layer.Alpha, layer.AlphaSize)));
            }
            return new Landscape(land->HasFlags != 0 ? land->Flags : null, land->HasHeights != 0 ? land->HeightOffset : null,
                                 land->Heights == IntPtr.Zero ? null : Utf8.Raw(land->Heights, vertices),
                                 land->Normals == IntPtr.Zero ? null : Utf8.Raw(land->Normals, vertices * 3),
                                 land->Colors == IntPtr.Zero ? null : Utf8.Raw(land->Colors, vertices * 3), layers);
        }
        finally
        {
            RadicleNative.LandscapeFree(land);
        }
    }

    public LandHeightGrid? LandscapeHeights(Record record)
    {
        using var use = Use(record);
        Native.LandHeights heights;
        Call.Check(RadicleNative.LandscapeHeights(use.Pointer, record.Handle, &heights));
        return heights.HasHeights == 0 ? null : new LandHeightGrid(heights.Base, new ReadOnlySpan<float>(heights.Heights, 1089).ToArray());
    }

    public LandBlend LandscapeBlend(Record record)
    {
        using var use = Use(record);
        Native.LandBlend* blend;
        Call.Check(RadicleNative.LandscapeBlend(use.Pointer, record.Handle, &blend));
        try
        {
            var layers = new List<LandBlendLayer>();
            var native = (Native.LandBlendLayer*)blend->Layers;
            for (var i = 0; i < (int)blend->LayerCount; ++i)
            {
                layers.Add(new LandBlendLayer(FormKey.FromNative(native[i].Texture),
                                              new ReadOnlySpan<float>((float*)native[i].Weights, 1089).ToArray()));
            }
            return new LandBlend(new[] { FormKey.FromNative(blend->Base0), FormKey.FromNative(blend->Base1),
                                         FormKey.FromNative(blend->Base2), FormKey.FromNative(blend->Base3) }, layers);
        }
        finally
        {
            RadicleNative.LandscapeBlendFree(blend);
        }
    }

    public FormKey? LandscapeTextureSet(Record record)
    {
        using var use = Use(record);
        Native.FormKey key;
        Call.Check(RadicleNative.LandscapeTextureGetSet(use.Pointer, record.Handle, &key));
        return FormKey.FromNative(key);
    }

    public TextureSetInfo TextureSet(Record record)
    {
        using var use = Use(record);
        Native.TextureSetInfo t;
        Call.Check(RadicleNative.TextureSetGetInfo(use.Pointer, record.Handle, &t));
        var textures = new[] { t.Textures0, t.Textures1, t.Textures2, t.Textures3, t.Textures4, t.Textures5, t.Textures6, t.Textures7 }
            .Select(Utf8.Read).ToArray();
        var decal = new float[(int)t.DecalCount];
        for (var i = 0; i < decal.Length; ++i) decal[i] = t.Decal[i];
        return new TextureSetInfo(textures, Utf8.Read(t.Material), t.HasFlags != 0 ? t.Flags : null, decal);
    }

    public string? LandscapeTextureDiffuse(FormKey landscapeTexture)
    {
        using var use = Use();
        using var s = new NativeStrings();
        IntPtr path;
        return Call.Found(RadicleNative.EnvLandscapeTextureDiffuse(use.Pointer, s.Key(landscapeTexture), &path)) ? Utf8.Read(path) : null;
    }

    public LightRecord Light(Record record)
    {
        using var use = Use(record);
        LightInfo? data = null;
        Native.LightInfo l;
        Call.Check(RadicleNative.LightGetInfo(use.Pointer, record.Handle, &l));
        if (l.HasData != 0)
        {
            data = new LightInfo(l.Time, l.Radius, ToColor(l.Color), l.Flags, l.FalloffExponent, l.Fov, l.NearClip,
                                 l.FlickerPeriod, l.FlickerIntensityAmplitude, l.FlickerMovementAmplitude, l.Constant,
                                 l.Scalar, l.HasExponent != 0 ? l.Exponent : null,
                                 l.HasGodRaysNearClip != 0 ? l.GodRaysNearClip : null, l.HasValue != 0 ? l.Value : null,
                                 l.HasWeight != 0 ? l.Weight : null);
        }
        return new LightRecord(data, l.HasFade != 0 ? l.Fade : null, Utf8.Read(l.Gobo), FormKey.FromNative(l.Lens),
                               FormKey.FromNative(l.GodRays));
    }

    public AddOnNodeRecord AddOnNode(Record record)
    {
        using var use = Use(record);
        Native.AddOnNodeInfo a;
        Call.Check(RadicleNative.AddOnNodeGetInfo(use.Pointer, record.Handle, &a));
        return new AddOnNodeRecord(a.HasNodeIndex != 0 ? a.NodeIndex : null, FormKey.FromNative(a.Light),
                                   Utf8.Read(a.Model), a.HasDnam != 0 ? a.MasterParticleCap : null,
                                   a.HasDnam != 0 ? a.Flags : null);
    }

    public BendableSpline BendableSpline(Record record)
    {
        using var use = Use(record);
        BendableSplineInfo b;
        Call.Check(RadicleNative.BendableSplineGetInfo(use.Pointer, record.Handle, &b));
        return new BendableSpline(b.HasDefaultTiles != 0 ? b.DefaultTiles : null,
                                  b.HasDefaultSlices != 0 ? b.DefaultSlices : null,
                                  b.HasTilesRelativeToLength != 0 ? b.TilesRelativeToLength != 0 : null,
                                  b.HasDefaultColor != 0 ? (b.DefaultColor[0], b.DefaultColor[1], b.DefaultColor[2]) : null,
                                  b.HasWindSensibility != 0 ? b.WindSensibility : null,
                                  b.HasWindFlexibility != 0 ? b.WindFlexibility : null, FormKey.FromNative(b.Texture));
    }

    public FormKey? LayerParent(Record record)
    {
        using var use = Use(record);
        Native.FormKey key;
        Call.Check(RadicleNative.LayerGetParent(use.Pointer, record.Handle, &key));
        return FormKey.FromNative(key);
    }

    public LightingTemplate LightingTemplate(Record record)
    {
        using var use = Use(record);
        LightingTemplateInfo t;
        Call.Check(RadicleNative.LightingTemplateGetInfo(use.Pointer, record.Handle, &t));
        return new LightingTemplate(t.HasLighting != 0 ? ToLighting(t.Lighting) : null, FormKey.FromNative(t.GodRays));
    }

    public Climate Climate(Record record)
    {
        using var use = Use(record);
        Native.Climate* c;
        Call.Check(RadicleNative.ClimateDecode(use.Pointer, record.Handle, &c));
        try
        {
            var weathers = Call.Copy<Native.ClimateWeather>(c->Weathers, c->WeatherCount)
                .Select(w => new ClimateWeather(FormKey.FromNative(w.Weather), w.Chance, FormKey.FromNative(w.Global)))
                .ToArray();
            return new Climate(weathers, Utf8.Read(c->SunTexture), Utf8.Read(c->SunGlareTexture))
                { Timing = c->HasTiming != 0 ? ToTiming(c->Timing) : null };
        }
        finally
        {
            RadicleNative.ClimateFree(c);
        }
    }

    public Weather Weather(Record record)
    {
        using var use = Use(record);
        WeatherInfo* w;
        Call.Check(RadicleNative.WeatherDecode(use.Pointer, record.Handle, &w));
        try
        {
            var colors = Utf8.Raw(w->Colors, w->ColorGroups * w->ColorTimes * 4);
            return new Weather((int)w->ColorGroups, (int)w->ColorTimes, colors,
                               Call.Copy<float>(w->FogDistances, w->FogDistanceCount))
                { Parameters = w->HasParameters != 0 ? ToParameters(w->Parameters) : null };
        }
        finally
        {
            RadicleNative.WeatherFree(w);
        }
    }

    public IReadOnlyList<StaticPart> StaticCollection(Record record)
    {
        using var use = Use(record);
        Native.StaticCollection* c;
        Call.Check(RadicleNative.StaticCollectionDecode(use.Pointer, record.Handle, &c));
        try
        {
            var parts = new List<StaticPart>();
            foreach (var part in Call.Copy<Native.StaticPart>(c->Parts, c->PartCount))
            {
                var placements = Call.Copy<Native.StaticPlacement>(part.Placements, part.PlacementCount)
                    .Select(p => new StaticPlacement((p.Position[0], p.Position[1], p.Position[2]),
                                                     (p.Rotation[0], p.Rotation[1], p.Rotation[2]), p.Scale))
                    .ToArray();
                parts.Add(new StaticPart(FormKey.FromNative(part.StaticRecord), placements));
            }
            return parts;
        }
        finally
        {
            RadicleNative.StaticCollectionFree(c);
        }
    }

    public DistantLod StaticDistantLod(Record record)
    {
        using var use = Use(record);
        Native.StaticDistantLod d;
        Call.Check(RadicleNative.StaticGetDistantLod(use.Pointer, record.Handle, &d));
        return new DistantLod((int)d.LevelCount, new[] { Utf8.Read(d.Meshes0), Utf8.Read(d.Meshes1), Utf8.Read(d.Meshes2), Utf8.Read(d.Meshes3) });
    }

    public FormKey? ActivatorWaterType(Record record)
    {
        using var use = Use(record);
        Native.FormKey key;
        Call.Check(RadicleNative.ActivatorGetWaterType(use.Pointer, record.Handle, &key));
        return FormKey.FromNative(key);
    }

    public Water Water(Record record)
    {
        using var use = Use(record);
        WaterInfo w;
        Call.Check(RadicleNative.WaterGetInfo(use.Pointer, record.Handle, &w));
        var textures = new[] { Utf8.Read(w.NoiseTextures0), Utf8.Read(w.NoiseTextures1), Utf8.Read(w.NoiseTextures2) };
        var noise = new WaterNoiseLayer[3];
        for (var i = 0; i < 3; ++i)
            noise[i] = new WaterNoiseLayer(w.NoiseWindDirection[i], w.NoiseWindSpeed[i], w.NoiseAmplitudeScale[i], w.NoiseUvScale[i],
                                           w.NoiseFalloff[i], textures[i]);
        WaterVisual? visual = w.HasVisual == 0 ? null : new WaterVisual(
            w.FogDepthAmount, ToColor(w.FogShallowColor), ToColor(w.FogDeepColor), w.FogColorShallowRange, w.FogColorDeepRange,
            w.FogShallowAlpha, w.FogDeepAlpha, w.FogAlphaShallowRange, w.FogAlphaDeepRange, ToColor(w.FogUnderwaterColor),
            w.FogUnderwaterAmount, w.FogUnderwaterNear, w.FogUnderwaterFar, w.NormalMagnitude, w.ShallowNormalFalloff,
            w.DeepNormalFalloff, w.ReflectivityAmount, w.FresnelAmount, w.SurfaceEffectFalloff, w.DisplacementForce,
            w.DisplacementVelocity, w.DisplacementFalloff, w.DisplacementDampener, w.DisplacementStartingSize,
            ToColor(w.ReflectionColor), w.SunSpecularPower, w.SunSpecularMagnitude, w.SunSparklePower, w.SunSparkleMagnitude,
            w.InteriorSpecularRadius, w.InteriorSpecularBrightness, w.InteriorSpecularPower,
            w.HasSiltAmount != 0 ? w.SiltAmount : null, w.HasSiltLightColor != 0 ? ToColor(w.SiltLightColor) : null,
            w.HasSiltDarkColor != 0 ? ToColor(w.SiltDarkColor) : null,
            w.HasScreenSpaceReflections != 0 ? w.ScreenSpaceReflections != 0 : null);
        return new Water(w.HasFlags != 0 ? w.Flags : null, visual, noise,
                         w.HasLinearVelocity != 0 ? (w.LinearVelocity[0], w.LinearVelocity[1], w.LinearVelocity[2]) : null,
                         w.HasAngularVelocity != 0 ? (w.AngularVelocity[0], w.AngularVelocity[1], w.AngularVelocity[2]) : null);
    }

    internal static ByteColor ToColor(Native.ByteColor color) => new(color.Red, color.Green, color.Blue);

    internal static Lighting ToLighting(CellLighting l, CellLightingTail? tail = null)
    {
        var t = tail.GetValueOrDefault();
        return new(ToColor(l.Ambient), ToColor(l.Directional), ToColor(l.FogNearColor), l.FogNear, l.FogFar,
                   l.DirectionalRotationXy, l.DirectionalRotationZ, l.DirectionalFade, l.FogClipDistance, l.FogPower,
                   l.HasFogFarColor != 0 ? ToColor(l.FogFarColor) : null, l.HasFogMax != 0 ? l.FogMax : null,
                   l.HasLightFade != 0 ? (l.LightFadeBegin, l.LightFadeEnd) : null,
                   l.HasInherits != 0 ? l.Inherits : null,
                   tail.HasValue && t.HasNearHeight != 0 ? (t.NearHeightMid, t.NearHeightRange) : null,
                   tail.HasValue && t.HasHighFogColors != 0
                       ? (ToColor(t.FogColorHighNear), ToColor(t.FogColorHighFar))
                       : null,
                   tail.HasValue && t.HasHighDensityScale != 0 ? t.HighDensityScale : null,
                   tail.HasValue && t.HasFogScales != 0
                       ? (t.FogNearScale, t.FogFarScale, t.FogHighNearScale, t.FogHighFarScale)
                       : null,
                   tail.HasValue && t.HasFarHeight != 0 ? (t.FarHeightMid, t.FarHeightRange) : null);
    }

    internal static string Signature(byte* text) => System.Text.Encoding.ASCII.GetString(text, 4);

    private Record[] Records(IntPtr* items, nuint count) =>
        Call.TakeRecords(items, count).Select(handle => new Record(handle, this)).ToArray();
}
