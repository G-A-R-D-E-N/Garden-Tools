using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using BehaviourStudio.Mcp;
using OpenCommonwealth.Services.Archive;
using OpenCommonwealth.Services.Hkx;
using Xunit;

namespace BehaviourStudio.Tests;

public sealed class ProjectChainPolicyTests
{
    [Fact]
    public void NamesInsideTheFilesCannotLeadOutsideTheAllowedFolder()
    {
        using var scope = new Scope();
        string rig = Path.Combine(scope.Outside, "skeleton.hkx");
        File.WriteAllBytes(rig, new byte[] { 0 });
        var reads = new List<string>();
        var chain = ProjectChain.Resolve(
            scope.Entry, scope.Reader(@"..\outside\secret.hkx", rig, @"..\outside\anim.hkx", reads),
            mayRead: scope.Allowed);

        Assert.DoesNotContain(reads, path => path.StartsWith(scope.Outside, StringComparison.OrdinalIgnoreCase));
        var behaviour = Assert.Single(chain.Links, l => l.Role == "behaviour");
        Assert.False(behaviour.Exists);
        Assert.Equal("", behaviour.Resolved);
        var skeleton = Assert.Single(chain.Links, l => l.Role == "skeleton");
        Assert.False(skeleton.Exists);
        Assert.Equal("", skeleton.Resolved);
        Assert.Equal("", chain.AnimationSources[@"..\outside\anim.hkx"]);
        Assert.Equal(3, chain.Problems.Count(p => p.Contains("outside the allowed folders")));
        Assert.False(chain.Allows(@"..\outside\anim.hkx"));
        Assert.True(chain.Allows(@"Animations\walk.hkx"));
    }

    [Fact]
    public void WithoutAPolicyTheSameNamesStillResolve()
    {
        using var scope = new Scope();
        var chain = ProjectChain.Resolve(
            scope.Entry, scope.Reader(@"..\outside\secret.hkx", "", @"..\outside\anim.hkx", new List<string>()));

        var behaviour = Assert.Single(chain.Links, l => l.Role == "behaviour");
        Assert.True(behaviour.Exists);
        Assert.Equal("loose", chain.AnimationSources[@"..\outside\anim.hkx"]);
    }

    [Fact]
    public void AnAbsoluteCharacterPathIsRefused()
    {
        using var scope = new Scope();
        string character = Path.Combine(scope.Outside, "character.hkx");
        File.WriteAllBytes(character, new byte[] { 0 });
        var reads = new List<string>();
        var chain = ProjectChain.Resolve(scope.Entry, scope.Reader("", "", "", reads, character),
                                         mayRead: scope.Allowed);

        Assert.DoesNotContain(reads, path => path.Equals(character, StringComparison.OrdinalIgnoreCase));
        Assert.Single(chain.Links, l => l.Role == "character" && !l.Exists && l.Resolved == "");
    }

    [Fact]
    public void RootIsNotWidenedToAParentTheFolderPolicyRefuses()
    {
        using var scope = new Scope();
        string behaviors = Path.Combine(scope.Project, "Behaviors");
        Directory.CreateDirectory(behaviors);
        string entry = Path.Combine(behaviors, "Entry.hkx");
        File.WriteAllBytes(entry, new byte[] { 0 });
        var reads = new List<string>();
        var chain = ProjectChain.Resolve(
            entry, path => { reads.Add(path); return null; },
            mayRead: path => path.StartsWith(behaviors, StringComparison.OrdinalIgnoreCase));

        Assert.Equal(behaviors, chain.Root);
        Assert.All(reads, path => Assert.StartsWith(behaviors, path, StringComparison.OrdinalIgnoreCase));
    }

    [Fact]
    public void McpPolicyAllowsOnlyPathsUnderItsRoots()
    {
        using var scope = new Scope();
        Assert.True(McpPathPolicy.TryCreate(new[] { scope.Project }, out var policy, out _));
        string inside = Path.Combine(scope.Project, "Characters", "a.hkx");

        Assert.True(policy!.MayRead(inside));
        Assert.True(policy.MayRead(scope.Entry));
        Assert.False(policy.MayRead(Path.Combine(scope.Outside, "secret.hkx")));
        Assert.False(policy.MayRead(Path.Combine(scope.Project, "..", "outside", "secret.hkx")));
        Assert.False(policy.MayRead(scope.Project + "-copy"));
    }

    [Fact]
    public void PluginGroupsNestedPastTheBoundAreRefused()
    {
        Assert.Empty(Walk(16));
        Assert.Throws<InvalidDataException>(() => Walk(17));
    }

    private static List<EsPlugin.AnimSet> Walk(int depth)
    {
        using var stream = new MemoryStream();
        using var writer = new BinaryWriter(stream);
        for (int level = 0; level < depth; level++)
        {
            writer.Write(new[] { (byte)'G', (byte)'R', (byte)'U', (byte)'P' });
            writer.Write((uint)(24 * (depth - level)));
            writer.Write(new byte[16]);
        }

        stream.Position = 0;
        return EsPlugin.RaceAnimationSets(new BinaryReader(stream), stream.Length);
    }

    private sealed class Scope : IDisposable
    {
        private readonly string _base = Directory.CreateTempSubdirectory("bgs-chain-policy").FullName;

        public Scope()
        {
            Project = Path.Combine(_base, "proj");
            Outside = Path.Combine(_base, "outside");
            Directory.CreateDirectory(Path.Combine(Project, "Characters"));
            Directory.CreateDirectory(Outside);
            Entry = Path.Combine(Project, "Project.hkx");
            File.WriteAllBytes(Entry, new byte[] { 0 });
            File.WriteAllBytes(Path.Combine(Project, "Characters", "a.hkx"), new byte[] { 0 });
            File.WriteAllBytes(Path.Combine(Outside, "secret.hkx"), new byte[] { 0 });
            File.WriteAllBytes(Path.Combine(Outside, "anim.hkx"), new byte[] { 0 });
        }

        public string Project { get; }
        public string Outside { get; }
        public string Entry { get; }

        public bool Allowed(string path) => path.StartsWith(Project, StringComparison.OrdinalIgnoreCase);

        public Func<string, BehaviourGraphModel?> Reader(
            string behaviour, string rig, string animation, List<string> reads, string character = "")
        {
            return path =>
            {
                reads.Add(path);
                var model = new BehaviourGraphModel();
                string name = Path.GetFileName(path);
                if (name == "Project.hkx")
                {
                    var project = Add(model, "1", "hkbProjectStringData");
                    project.Lists["characterFilenames"] =
                        new List<string> { character.Length > 0 ? character : @"Characters\a.hkx" };
                }
                else if (name == "a.hkx")
                {
                    var strings = Add(model, "1", "hkbCharacterStringData");
                    if (behaviour.Length > 0) strings.Scalars["behaviorFilename"] = behaviour;
                    if (rig.Length > 0) strings.Scalars["rigName"] = rig;
                    if (animation.Length > 0) strings.Lists["animationNames"] = new List<string> { animation };
                }
                return model;
            };
        }

        public void Dispose()
        {
            try { Directory.Delete(_base, recursive: true); }
            catch (Exception error) when (error is IOException or UnauthorizedAccessException) { }
        }

        private static HkObject Add(BehaviourGraphModel model, string id, string className)
        {
            var obj = new HkObject { Id = id, Class = className };
            model.ById[id] = obj;
            model.Objects.Add(obj);
            return obj;
        }
    }
}
