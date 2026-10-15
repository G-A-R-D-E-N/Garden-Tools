using System.IO;
using System.Linq;
using Radicle.Interop.Native;
using Xunit;

namespace Radicle.Interop.Tests;

public class WritebackTests : IClassFixture<WorldFixture>
{
    private readonly WorldFixture _world;

    public WritebackTests(WorldFixture world) => _world = world;

    private static FormKey M(uint id) => new("Master.esm", id);

    [Fact]
    public void OverridesEditsSavesAndReloads()
    {
        using var temp = new TempDirectory();
        var output = Path.Combine(temp.Path, "Edits.esp");
        using (var env = RadicleEnvironment.Load(new[] { _world.Master }))
        using (var plugin = PluginFile.Create("Edits.esp"))
        {
            var refr = env.Winner(M(0x803))!.Value;
            Assert.Equal(PlacedCopyStatus.Inserted, plugin.CopyPlacedOverride(env, refr));
            Assert.Equal(PlacedCopyStatus.AlreadyExists, plugin.CopyPlacedOverride(env, refr));
            Assert.Equal(PlacedCopyStatus.Replaced, plugin.CopyPlacedOverride(env, refr, overwrite: true));
            var created = plugin.CopyPlacedNew(env, refr);
            Assert.Equal(new FormKey("Edits.esp", 0x800), created);

            Assert.True(plugin.SetTransform(M(0x803), new Vector3(1, 2, 3), new Vector3(0, 0, 0.25f)));
            Assert.True(plugin.SetScale(M(0x803), null));
            Assert.True(plugin.SetEditorId(M(0x803), "Moved"));
            Assert.True(plugin.SetRecordFlags(M(0x803), 0x800));
            Assert.True(plugin.SetComments(M(0x803), null));
            Assert.True(plugin.SetLocationReference(env, M(0x803), M(0x831)));
            Assert.True(plugin.SetOwnership(env, M(0x803), M(0x830), noCrime: true, factionRank: 2));
            Assert.True(plugin.SetEnableParent(env, M(0x803), null, 0));
            Assert.True(plugin.SetLinkedReferences(env, M(0x803), new[] { new LinkedReference(null, M(0x804)), new LinkedReference(M(0x830), M(0x821)) }));
            Assert.True(plugin.SetActivateParentDelay(M(0x803), 0, 4.0f));
            Assert.False(plugin.SetActivateParentDelay(M(0x803), 3, 4.0f));
            Assert.False(plugin.SetTeleport(env, M(0x803), M(0x804), new Vector3(0, 0, 0), new Vector3(0, 0, 0)));
            Assert.False(plugin.SetPrimitive(M(0x803), new Vector3(1, 1, 1), 1));
            Assert.False(plugin.SetComments(M(0x999), "x"));
            var error = Assert.Throws<RadicleException>(() => plugin.SetOwnership(env, M(0x803), new FormKey("Nope.esm", 1), false, null));
            Assert.Equal(Status.InvalidArgument, error.Status);
            plugin.SortMasters(env);
            plugin.Write(output);
            Assert.Equal(File.ReadAllBytes(output), plugin.ToBytes());
        }

        using var reloaded = RadicleEnvironment.Load(new[] { _world.Master, output });
        var winner = reloaded.Winner(M(0x803))!.Value;
        var info = reloaded.Info(winner);
        Assert.Equal(1, info.PluginIndex);
        Assert.Equal("Moved", info.EditorId);
        Assert.Equal(0x800u, info.RecordFlags);
        var placed = reloaded.PlacedReference(winner);
        Assert.Equal(new Vector3(1, 2, 3), placed.Position);
        Assert.Null(placed.Scale);
        Assert.Null(placed.Comments);
        Assert.Equal(M(0x831), placed.LocationReference);
        Assert.Equal(M(0x830), placed.Owner);
        Assert.True(placed.NoCrime);
        Assert.Equal(2, placed.FactionRank);
        Assert.Null(placed.EnableParent);
        Assert.Equal(new[] { M(0x804), M(0x821) }, placed.LinkedReferences.Select(l => l.Reference!.Value));
        Assert.Equal(4.0f, placed.ActivateParents.Single().Delay);
        Assert.Contains(reloaded.CellChildren(M(0x802)), c => reloaded.Info(c.Record).FormKey == new FormKey("Edits.esp", 0x800));

        using var reread = PluginFile.Read(output);
        Assert.Equal(File.ReadAllBytes(output), reread.ToBytes());
    }

    [Fact]
    public void MovesAPlacedReferenceWithinItsCellAndRefusesAMissingTargetCell()
    {
        using var env = RadicleEnvironment.Load(new[] { _world.Master });
        using var plugin = PluginFile.Create("Moves.esp");
        Assert.Equal(PlacedCopyStatus.Inserted, plugin.CopyPlacedOverride(env, env.Winner(M(0x803))!.Value));

        Assert.Equal(PlacedMoveStatus.Moved, plugin.MoveTransform(M(0x803), new Vector3(5000, 9000, 30), new Vector3(0, 0, 0)));
        var error = Assert.Throws<RadicleException>(() => plugin.MoveTransform(M(0x803), new Vector3(0, 0, 0), new Vector3(0, 0, 0)));
        Assert.Equal(Status.NotFound, error.Status);
    }
}
