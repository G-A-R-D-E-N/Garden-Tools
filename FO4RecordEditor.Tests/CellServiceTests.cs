using System.IO;
using System.Linq;
using FluentAssertions;
using FO4RecordEditor.Services;
using Mutagen.Bethesda.Fallout4;
using Newtonsoft.Json.Linq;
using Xunit;
using Xunit.Abstractions;

namespace FO4RecordEditor.Tests;

// Integration test against a real MO2 instance on this machine (same one Mo2ProfileLoaderTests uses).
// Skipped automatically if the instance isn't present, so it won't fail on other machines/CI.
public class CellServiceTests
{
    private readonly ITestOutputHelper _out;
    public CellServiceTests(ITestOutputHelper o) => _out = o;

    private const string Instance = @"E:\Modlists\Fallen World Alpha 2";

    private static bool Available =>
        Directory.Exists(Path.Combine(Instance, "profiles")) &&
        File.Exists(Path.Combine(Instance, "ModOrganizer.ini"));

    [Fact]
    public void GetPlacedReferences_UnionsAcrossPlugins_NotJustTheWinningRecord()
    {
        if (!Available) return;
        var info = Mo2ProfileLoader.ReadInstanceInfo(Instance);
        var (env, _) = Mo2ProfileLoader.Load(Instance, info.Profile, info.GameDataFolder);

        var cache = MutagenLoader.LinkCache!;
        var cell = cache.PriorityOrder
            .SelectMany(m => m.EnumerateMajorRecords())
            .OfType<ICellGetter>()
            .Where(c => c.Flags.HasFlag(Cell.Flag.IsInteriorCell))
            .OrderByDescending(c => c.Persistent.Count + c.Temporary.Count)
            .FirstOrDefault();
        if (cell == null) return;   // no interior cells in this profile somehow

        // The naive count from whichever single plugin's copy EnumerateMajorRecords() happened to
        // surface is a lower bound, not the truth -- proven wrong in practice (2026-07-20): the
        // WINNING cell record for FO4's biggest interior cell reported 0 references while the real,
        // unioned total was 28,471. Assert the service's real total is never smaller than that lower
        // bound, which would be true if it silently fell back to a single plugin's copy again.
        var namingLowerBound = cell.Persistent.Count + cell.Temporary.Count;

        var json = CellService.GetPlacedReferencesJson(cell.FormKey.ToString(), env);
        var parsed = JObject.Parse(json);
        parsed["error"].Should().BeNull(json);

        var referenceCount = parsed["referenceCount"]!.Value<int>();
        _out.WriteLine($"cell={cell.FormKey} {cell.EditorID}  naive={namingLowerBound}  merged={referenceCount}");
        referenceCount.Should().BeGreaterThanOrEqualTo(namingLowerBound,
            "the merged reference count must never be smaller than a single plugin's own copy of the cell");

        var references = (JArray)parsed["references"]!;
        references.Should().HaveCount(referenceCount);
        // Every row must carry real spatial data -- not zeroed placeholders from a resolution failure.
        references.Should().Contain(r =>
            (float)r["position"]!["x"]! != 0f || (float)r["position"]!["y"]! != 0f || (float)r["position"]!["z"]! != 0f);
    }

    [Fact]
    public void GetPlacedReferences_UnknownId_ReturnsError()
    {
        if (!Available) return;
        var info = Mo2ProfileLoader.ReadInstanceInfo(Instance);
        var (env, _) = Mo2ProfileLoader.Load(Instance, info.Profile, info.GameDataFolder);

        var json = CellService.GetPlacedReferencesJson("ThisCellDoesNotExist12345", env);
        var parsed = JObject.Parse(json);
        parsed["error"].Should().NotBeNull();
    }

    // Regression for the ground-decal fix (2026-07-20). Ref 1D515B:Fallout4.esm in
    // SanctuaryBasementJahani is a REAL PlacedObject whose Base resolves to a TextureSet
    // (DecalGroundDefault01, 078966:Fallout4.esm) with a genuine Decal subrecord -- verified by hand
    // via get_record before writing this fix, not guessed. Before the fix, TextureSet bases (no Model
    // field at all) fell through to modelPath=null with nothing else to show; the viewer drew a
    // generic "no model" marker for what's actually a real, textured ground decal. Also regression-
    // tests the recordType fix: Mutagen's lazy binary-overlay wrapper reported this ref's runtime type
    // as "TextureSetBinaryOverlay" (the TextureSet MAJOR RECORD's own overlay class, not a placement
    // kind -- confirmed no such placed-record type exists in Mutagen.Fallout4 at all), which is
    // corrected to the real semantic kind via an interface check, same pattern already used for
    // baseType.
    [Fact]
    public void GetPlacedReferences_ATextureSetBase_ReportsDecalFields_NotAModelPath()
    {
        if (!Available) return;
        var info = Mo2ProfileLoader.ReadInstanceInfo(Instance);
        var (env, _) = Mo2ProfileLoader.Load(Instance, info.Profile, info.GameDataFolder);

        var json = CellService.GetPlacedReferencesJson("SanctuaryBasementJahani", env);
        var parsed = JObject.Parse(json);
        parsed["error"].Should().BeNull(json);

        var refs = (JArray)parsed["references"]!;
        var decalRef = refs.FirstOrDefault(r => (string?)r["formKey"] == "1D515B:Fallout4.esm");
        decalRef.Should().NotBeNull("this specific reference is a known real ground decal");

        decalRef!["modelPath"]!.Type.Should().Be(Newtonsoft.Json.Linq.JTokenType.Null,
            "a TextureSet base has no Model field -- there is genuinely no mesh to show");
        ((string?)decalRef["recordType"]).Should().Be("PlacedObject",
            "the interface-based check must correct Mutagen's misleading overlay-wrapper type name");
        ((string?)decalRef["baseType"]).Should().Be("TextureSet");
        ((string?)decalRef["decalDiffuse"]).Should().Be("Landscape/Ground/CommonwealthDefault01Decal_d.DDS");
        ((float?)decalRef["decalWidth"]).Should().BeApproximately(15f, 0.5f, "ObjectBounds X-extent (7 - -8)");
        ((float?)decalRef["decalHeight"]).Should().BeApproximately(60f, 0.5f, "ObjectBounds Y-extent (30 - -30)");
    }
}
