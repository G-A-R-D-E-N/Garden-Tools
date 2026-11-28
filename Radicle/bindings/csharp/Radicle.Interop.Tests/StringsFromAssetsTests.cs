using System;
using System.Text;
using Radicle.Interop.Native;
using Xunit;
using static Radicle.Interop.Tests.Bytes;
using static Radicle.Interop.Tests.PluginWriter;

namespace Radicle.Interop.Tests;

public class StringsFromAssetsTests
{
    private sealed class Fixture : IDisposable
    {
        public TempDirectory Temp { get; } = new();
        public string Data => System.IO.Path.Combine(Temp.Path, "Data");
        public string Mod => System.IO.Path.Combine(Temp.Path, "ModB");
        public string[] Plugins { get; }
        public string Archive { get; }

        public Fixture()
        {
            var baseEsm = Temp.Write("Data/Base.esm", Build(0x81, Array.Empty<string>(),
                Top("WRLD", new Rec("WRLD", 0x800, new[] { ("FULL", U32(0x10)) }))));
            var patch = Temp.Write("Data/Patch.esp", Build(0x80, new[] { "Base.esm" },
                Top("WRLD", new Rec("WRLD", 0x800, new[] { ("FULL", U32(0x11)) }))));
            Plugins = new[] { baseEsm, patch };
            Temp.Write("Data/Strings/Base_en.STRINGS", StringsWriter.Table((0x10, "Vanilla world")));
            Temp.Write("Data/Strings/Base_en.DLSTRINGS", StringsWriter.LengthPrefixedTable(0x20, "Vanilla description"));
            Temp.Write("Data/Strings/Base_de.STRINGS", StringsWriter.Table((0x10, "Welt")));
            Temp.Write("ModB/Strings/base_en.strings", StringsWriter.Table((0x10, "Mod world")));
            Temp.Write("ModB/Strings/Patch_en.STRINGS", StringsWriter.Table((0x11, "Mod patch")));
            Archive = Temp.Write("Data/Patch - Main.ba2", Ba2Writer.General(
                ("Strings\\Patch_fr.STRINGS", StringsWriter.Table((0x11, "Patch fr"))),
                ("Strings\\Base_en.ILSTRINGS", StringsWriter.LengthPrefixedTable(0x30, "Archived il")),
                ("Strings\\Base_en.STRINGS", StringsWriter.Table((0x10, "Archived world")))));
        }

        public AssetResolver Open(bool modFirst = true) =>
            AssetResolver.OpenRoots(modFirst ? new[] { Mod, Data } : new[] { Data, Mod }, new[] { Archive });

        public void Dispose() => Temp.Dispose();
    }

    private static string? Text(StringTables strings, int plugin, StringSource source, uint id) =>
        strings.Lookup(plugin, source, id) is { } bytes ? Encoding.Latin1.GetString(bytes) : null;

    [Fact]
    public void LoadsEachTableFromTheHighestRootThenArchives()
    {
        using var f = new Fixture();
        using var env = RadicleEnvironment.Load(f.Plugins);
        using var assets = f.Open();
        using var strings = StringTables.Load(env, assets);

        Assert.Equal("Mod world", Text(strings, 0, StringSource.Normal, 0x10));
        Assert.StartsWith(f.Mod, strings.TablePath(0, StringSource.Normal), StringComparison.OrdinalIgnoreCase);
        Assert.Equal("Vanilla description", Text(strings, 0, StringSource.Dl, 0x20));
        Assert.StartsWith(f.Data, strings.TablePath(0, StringSource.Dl), StringComparison.OrdinalIgnoreCase);
        Assert.Equal("Archived il", Text(strings, 0, StringSource.Il, 0x30));
        Assert.EndsWith("Strings/Base_en.ILSTRINGS", strings.TablePath(0, StringSource.Il)!.Replace('\\', '/'));

        Assert.Equal("Mod patch", Text(strings, 1, StringSource.Normal, 0x11));
        Assert.Null(strings.TablePath(1, StringSource.Dl));
        Assert.Null(strings.Lookup(1, StringSource.Dl, 0x20));
        Assert.Equal("Mod patch", Encoding.Latin1.GetString(strings.ResolveName(env.Winner(new FormKey("Base.esm", 0x800))!.Value)!));
    }

    [Fact]
    public void RootOrderDecidesTheWinner()
    {
        using var f = new Fixture();
        using var env = RadicleEnvironment.Load(f.Plugins);
        using var assets = f.Open(modFirst: false);
        using var strings = StringTables.Load(env, assets);

        Assert.Equal("Vanilla world", Text(strings, 0, StringSource.Normal, 0x10));
        Assert.Equal("Mod patch", Text(strings, 1, StringSource.Normal, 0x11));
    }

    [Fact]
    public void LanguageSelectsTheTableAndAMissingLanguageHasNoTables()
    {
        using var f = new Fixture();
        using var env = RadicleEnvironment.Load(f.Plugins);
        using var assets = f.Open();

        using (var german = StringTables.Load(env, assets, "de"))
        {
            Assert.Equal("Welt", Text(german, 0, StringSource.Normal, 0x10));
            Assert.Null(german.TablePath(0, StringSource.Dl));
            Assert.Null(german.TablePath(1, StringSource.Normal));
        }

        using (var french = StringTables.Load(env, assets, "fr"))
        {
            Assert.Equal("Patch fr", Text(french, 1, StringSource.Normal, 0x11));
            Assert.EndsWith("Strings/Patch_fr.STRINGS", french.TablePath(1, StringSource.Normal)!.Replace('\\', '/'));
            Assert.Null(french.TablePath(0, StringSource.Normal));
        }

        using var none = StringTables.Load(env, assets, "ja");
        Assert.Null(none.TablePath(0, StringSource.Normal));
        Assert.Null(none.Lookup(1, StringSource.Normal, 0x11));
    }

    [Fact]
    public void RootsWithoutStringsYieldNoTablesAndBadInputIsRefused()
    {
        using var f = new Fixture();
        using var env = RadicleEnvironment.Load(f.Plugins);
        using (var empty = AssetResolver.OpenRoots(new[] { System.IO.Path.Combine(f.Temp.Path, "Nope") }, Array.Empty<string>()))
        using (var none = StringTables.Load(env, empty))
            Assert.Null(none.TablePath(0, StringSource.Normal));

        using var assets = f.Open();
        Assert.Throws<ArgumentNullException>(() => StringTables.Load(env, (AssetResolver)null!));
        Assert.Equal(Status.ParseError, Assert.Throws<RadicleException>(() => StringTables.Load(env, assets, "e/n")).Status);
        f.Temp.Write("ModB/Strings/Base_en.ILSTRINGS", new byte[] { 1, 0 });
        Assert.Equal(Status.ParseError, Assert.Throws<RadicleException>(() => StringTables.Load(env, assets)).Status);
    }

    [Fact]
    public void DirectoryLoadStillReadsOnlyThatDirectory()
    {
        using var f = new Fixture();
        using var env = RadicleEnvironment.Load(f.Plugins);
        using var assets = f.Open();
        using var strings = StringTables.Load(env, System.IO.Path.Combine(f.Data, "Strings"), "en", assets);

        Assert.Equal("Vanilla world", Text(strings, 0, StringSource.Normal, 0x10));
        Assert.Null(strings.TablePath(1, StringSource.Normal));
        Assert.Equal("Archived il", Text(strings, 0, StringSource.Il, 0x30));
    }
}
