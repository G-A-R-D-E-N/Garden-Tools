using System.IO;
using FluentAssertions;
using FO4RecordEditor.Services;
using Mutagen.Bethesda;
using Mutagen.Bethesda.Fallout4;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Plugins.Order;
using Noggog;
using Xunit;

namespace FO4RecordEditor.Tests;

/// <summary>
/// Regression guard for the process-global leak fix: a class that loads an environment into
/// MutagenLoader.LinkCache without restoring it leaves ITS records answering every later FormLink
/// resolution in the run (the leak class behind the order-dependent CellCompaction spin). This
/// proves GlobalStateIsolation's dispose really restores resolution, not just the pointer: after
/// a simulated leak scope, the leaked environment's record stops resolving and the pre-existing
/// environment's record resolves again.
/// </summary>
public class GlobalStateIsolationTests
{
    private static Mo2ProfileLoader.Mo2GameEnvironment BuildEnv(string modName, string editorId, out FormKey fk)
    {
        var mod = new Fallout4Mod(ModKey.FromNameAndExtension(modName), Fallout4Release.Fallout4);
        fk = new FormKey(mod.ModKey, 0x800);
        mod.Weapons.Add(new Weapon(fk, Fallout4Release.Fallout4) { EditorID = editorId });

        var listings = new List<IModListingGetter<IFallout4ModGetter>>
        {
            new ModListing<IFallout4ModGetter>(mod.ModKey, mod, enabled: true, ghostSuffix: string.Empty),
        };
        var lo = new LoadOrder<IModListingGetter<IFallout4ModGetter>>(listings, disposeItems: false);
        return new Mo2ProfileLoader.Mo2GameEnvironment
        {
            LoadOrder = lo,
            LinkCache = lo.ToImmutableLinkCache<IFallout4Mod, IFallout4ModGetter>(),
            DataFolderPath = new DirectoryPath(Path.GetTempPath()),
            PluginPaths = new Dictionary<string, string>(),
        };
    }

    [Fact]
    public void AfterARestore_ResolutionReturnsToThePreExistingEnvironment()
    {
        // A legitimate environment that the NEXT test class would resolve against.
        var previous = BuildEnv("Prev.esp", editorId: "PrevWeapon", out var prevFk);
        MutagenLoader.LinkCache = previous.LinkCache;

        try
        {
            FormKey leakFk;
            using (var isolation = new GlobalStateIsolation())
            {
                // Simulate a leaky prior class: it loads a DIFFERENT environment into the process
                // global while the isolation scope is open.
                var leaked = BuildEnv("Leak.esp", editorId: "LeakWeapon", out leakFk);
                MutagenLoader.LinkCache = leaked.LinkCache;

                // The leak is genuinely live while the scope is open -- without this assertion the
                // test could pass vacuously if the swap never happened.
                MutagenLoader.DescribeFormKey(null, leakFk.ToString()).Should().Contain("LeakWeapon");
            }   // dispose restores the pre-existing cache

            // After the restore, resolution consults the legitimate environment again: the
            // previous record resolves, and the leaked one no longer does (the raw FormKey string
            // comes back untouched instead of its EditorID).
            MutagenLoader.DescribeFormKey(null, prevFk.ToString()).Should().Contain("PrevWeapon");
            MutagenLoader.DescribeFormKey(null, leakFk.ToString()).Should().NotContain("LeakWeapon");
        }
        finally
        {
            MutagenLoader.LinkCache = null;
        }
    }
}
