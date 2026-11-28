using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using Xunit;

namespace Radicle.Interop.Tests;

public class VanillaEnumTests
{
    private static string? Master()
    {
        var dataDir = Environment.GetEnvironmentVariable("FO4_DATA_DIR");
        if (string.IsNullOrEmpty(dataDir)) return null;
        var path = Path.Combine(dataDir, "Fallout4.esm");
        return File.Exists(path) ? path : null;
    }

    private static Record Form(RadicleEnvironment env, uint id) => env.Winner(new FormKey("Fallout4.esm", id))!.Value;

    [Fact]
    public void EnumValuesEqualTheConstantsTheEngineHardCodes()
    {
        Assert.Equal(new[] { 1, 2, 4 }, new[] { FurnitureAnimType.Sit, FurnitureAnimType.Lay, FurnitureAnimType.Lean }.Select(a => (int)a));
        Assert.Equal(0x8000_0000u, (uint)FurnitureMarkerFlag.IsSleepFurniture);
        Assert.Equal(0x003F_FFFFu, (uint)FurnitureMarkerFlag.InteractionPointMask);
        Assert.Equal(0x0200_0000u, (uint)FurnitureRecordFlag.PowerArmor);
        Assert.Equal(new[] { 1, 2, 4, 8, 16 },
            new[] { FurnitureMarkerSides.Front, FurnitureMarkerSides.Rear, FurnitureMarkerSides.Right, FurnitureMarkerSides.Left, FurnitureMarkerSides.Other }.Select(e => (int)e));
        Assert.Equal(new[] { 1, 2, 4, 8, 16 },
            new[] { FurnitureEntryTypes.Front, FurnitureEntryTypes.Behind, FurnitureEntryTypes.Right, FurnitureEntryTypes.Left, FurnitureEntryTypes.Up }.Select(e => (int)e));
        Assert.Equal(new[] { 2, 7, 8, 9 },
            new[] { FurnitureBenchType.Weapons, FurnitureBenchType.Armor, FurnitureBenchType.PowerArmor, FurnitureBenchType.RobotMod }.Select(b => (int)b));
        Assert.Equal(new[] { 0, 2, 3, 12 },
            new[] { PackageLocationType.NearReference, PackageLocationType.NearPackageStart, PackageLocationType.NearEditorLocation, PackageLocationType.NearSelf }.Select(t => (int)t));
        Assert.Equal(0, (int)PackageTargetType.SpecificReference);
        Assert.Equal(Enumerable.Range(0, 15), Enum.GetValues<PackageLocationType>().Select(t => (int)t));
        Assert.Equal(Enumerable.Range(0, 9), Enum.GetValues<PackageTargetType>().Select(t => (int)t));
    }

    [Fact]
    public void FurnitureEnumsMatchTheVanillaMaster()
    {
        var path = Master();
        if (path == null) return;
        using var env = RadicleEnvironment.Load(new[] { path });

        var sit = env.Furniture(Form(env, 0x4CD2));
        Assert.Equal(0x4000_0001u, sit.MarkerFlags);
        Assert.Equal(FurnitureMarkerFlag.HasModel | (FurnitureMarkerFlag)1, sit.MarkerFlagSet);
        Assert.False(sit.IsSleepFurniture);
        Assert.False(sit.IsPowerArmor);
        Assert.All(sit.EntryPoints, e => Assert.Equal(FurnitureAnimType.Sit, e.Animation));
        Assert.All(sit.EntryPoints, e => Assert.Equal(FurnitureEntryTypes.Front | FurnitureEntryTypes.Right | FurnitureEntryTypes.Left, e.Entries));

        var bed = env.Furniture(Form(env, 0x20C40));
        Assert.Equal(0x8800_0001u, bed.MarkerFlags);
        Assert.Equal(FurnitureMarkerFlag.IsSleepFurniture | FurnitureMarkerFlag.MustExitToTalk | (FurnitureMarkerFlag)1, bed.MarkerFlagSet);
        Assert.True(bed.IsSleepFurniture);
        Assert.False(bed.IsPowerArmor);

        var frame = env.Furniture(Form(env, 0x34309));
        Assert.True(frame.IsPowerArmor);
        Assert.True(frame.RecordFlagSet.HasFlag(FurnitureRecordFlag.PowerArmor | FurnitureRecordFlag.HasContainer));
        Assert.Equal(FurnitureBenchType.PowerArmor, frame.BenchKind);

        var perch = env.Furniture(Form(env, 0x126301));
        Assert.Equal(FurnitureMarkerFlag.IsPerch | FurnitureMarkerFlag.HasModel | (FurnitureMarkerFlag)1, perch.MarkerFlagSet);
        Assert.True(perch.RecordFlagSet.HasFlag(FurnitureRecordFlag.IsPerch));
        Assert.False(perch.IsPowerArmor);

        var all = new List<FurnitureRecord>();
        foreach (var record in env.WinnersOf("FURN"))
        {
            try { all.Add(env.Furniture(record)); }
            catch (RadicleException) { }
        }
        Assert.True(all.Count > 400);
        var entries = all.SelectMany(f => f.EntryPoints).ToList();
        Assert.All(entries, e => Assert.True(Enum.IsDefined(e.Animation), $"anim type {e.AnimType}"));
        Assert.All(entries, e => Assert.Equal(0, e.EntryPoints & ~0x1F));
        Assert.Contains(entries, e => e.Animation == FurnitureAnimType.Sit);
        Assert.Contains(all, f => f.IsSleepFurniture);
        Assert.Contains(all, f => f.IsPowerArmor);
        Assert.All(all, f => Assert.Equal(f.MarkerFlags is { } m && (m & 0x8000_0000u) != 0, f.IsSleepFurniture));
        Assert.All(all.Where(f => f.BenchType is { }), f => Assert.True(Enum.IsDefined(f.BenchKind!.Value), $"bench {f.BenchType}"));
    }

    [Fact]
    public void PackageEnumsMatchTheVanillaMaster()
    {
        var path = Master();
        if (path == null) return;
        using var env = RadicleEnvironment.Load(new[] { path });

        bool HasLocation(uint id, PackageLocationType kind) => env.Package(Form(env, id)).Locations.Any(l => l.Kind == kind);
        bool HasTarget(uint id, PackageTargetType kind) => env.Package(Form(env, id)).Targets.Any(t => t.Kind == kind);
        Assert.True(HasLocation(0x2CBC, PackageLocationType.NearReference));
        Assert.True(HasLocation(0x2CC8, PackageLocationType.InCell));
        Assert.True(HasLocation(0x2A838, PackageLocationType.NearPackageStart));
        Assert.True(HasLocation(0x2CB1, PackageLocationType.NearEditorLocation));
        Assert.True(HasLocation(0x2CB5, PackageLocationType.EditorLocationCell));
        Assert.True(HasTarget(0x2CB6, PackageTargetType.SpecificReference));
        Assert.True(HasTarget(0x20C3F, PackageTargetType.ObjectId));
        Assert.True(HasTarget(0x2CE0, PackageTargetType.LinkedReference));

        var locations = new List<PackageLocation>();
        var targets = new List<PackageTarget>();
        foreach (var record in env.WinnersOf("PACK"))
        {
            var package = env.Package(record);
            locations.AddRange(package.Locations);
            targets.AddRange(package.Targets);
        }
        Assert.All(locations, l => Assert.True(l.IsKnownKind, $"location type {l.Type}"));
        Assert.All(targets, t => Assert.True(t.IsKnownKind, $"target type {t.Type}"));
        var locationKinds = locations.Select(l => l.Kind).ToHashSet();
        var expectedLocations = new[]
        {
            PackageLocationType.NearReference, PackageLocationType.InCell, PackageLocationType.NearPackageStart,
            PackageLocationType.NearEditorLocation, PackageLocationType.NearLinkedReference, PackageLocationType.QuestAlias,
            PackageLocationType.LocationHandle, PackageLocationType.NamedSpecialReference, PackageLocationType.DataInputIdMatch,
            PackageLocationType.NearSelf, PackageLocationType.EditorLocationCell, PackageLocationType.AliasCollectionItem,
        };
        Assert.All(expectedLocations, kind => Assert.Contains(kind, locationKinds));
        Assert.Equal(Enum.GetValues<PackageTargetType>().ToHashSet(), targets.Select(t => t.Kind).ToHashSet());
    }
}
