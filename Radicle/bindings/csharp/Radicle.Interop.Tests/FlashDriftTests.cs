using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using Flash;
using Xunit;

namespace Radicle.Interop.Tests;

public class FlashDriftTests
{
    private static readonly string[] Kinds = { "type", "global", "helper", "member" };
    private static readonly string[] Exempt = { "FromCharCode" };

    private static readonly Assembly Shim = typeof(Ops).Assembly;

    private static List<(string Kind, string Name, string Line)> Entries()
    {
        using Stream? stream = typeof(FlashDriftTests).Assembly.GetManifestResourceStream("emitted_names.txt");
        Assert.NotNull(stream);
        using StreamReader reader = new StreamReader(stream!);
        List<(string, string, string)> result = new List<(string, string, string)>();
        string? line;
        while ((line = reader.ReadLine()) != null)
        {
            line = line.TrimEnd('\r');
            if (line.Length == 0)
            {
                continue;
            }
            int space = line.IndexOf(' ');
            Assert.True(space > 0, "malformed line: " + line);
            result.Add((line.Substring(0, space), line.Substring(space + 1), line));
        }
        return result;
    }

    private static bool HasMember(string name)
    {
        BindingFlags flags = BindingFlags.Public | BindingFlags.Instance | BindingFlags.Static | BindingFlags.DeclaredOnly;
        foreach (Type type in Shim.GetExportedTypes())
        {
            foreach (MemberInfo member in type.GetMembers(flags))
            {
                bool usable = member.MemberType == MemberTypes.Method || member.MemberType == MemberTypes.Property ||
                              member.MemberType == MemberTypes.Field || member.MemberType == MemberTypes.Event;
                if (usable && member.Name == name)
                {
                    return true;
                }
            }
        }
        return false;
    }

    [Fact]
    public void TheNameListIsWellFormedSortedAndUnique()
    {
        List<(string Kind, string Name, string Line)> entries = Entries();
        Assert.True(entries.Count > 200, "the embedded list looks empty or truncated");
        Assert.Empty(entries.Where(e => !Kinds.Contains(e.Kind)).Select(e => e.Line));
        List<string> lines = entries.Select(e => e.Line).ToList();
        List<string> sorted = lines.OrderBy(l => l, StringComparer.Ordinal).ToList();
        Assert.Equal(sorted, lines);
        Assert.Equal(lines.Count, lines.Distinct().Count());
        Assert.Contains("type Flash.Display.Sprite", lines);
        Assert.Contains("global Flash.Utils.Globals.SetTimeout", lines);
        Assert.Contains("helper Flash.Ops.ToNumber", lines);
        Assert.Contains("member AddEventListener", lines);
    }

    [Fact]
    public void EveryEmittableTypeIsAPublicShimType()
    {
        List<string> missing = new List<string>();
        foreach ((string kind, string name, string _) in Entries())
        {
            if (kind != "type")
            {
                continue;
            }
            Type? type = Shim.GetType(name);
            if (type == null || !(type.IsPublic || type.IsNestedPublic))
            {
                missing.Add(name);
            }
        }
        Assert.Empty(missing);
    }

    [Fact]
    public void EveryEmittableGlobalAndHelperIsAPublicStaticMethod()
    {
        List<string> missing = new List<string>();
        foreach ((string kind, string name, string _) in Entries())
        {
            if (kind != "global" && kind != "helper")
            {
                continue;
            }
            int dot = name.LastIndexOf('.');
            Type? owner = Shim.GetType(name.Substring(0, dot));
            string method = name.Substring(dot + 1);
            bool found = owner != null && owner.IsPublic && owner.GetMethods(BindingFlags.Public | BindingFlags.Static)
                                                                 .Any(m => m.Name == method);
            if (!found)
            {
                missing.Add(name);
            }
        }
        Assert.Empty(missing);
    }

    [Fact]
    public void EveryEmittableMemberNameExistsOnSomePublicShimType()
    {
        List<string> missing = new List<string>();
        foreach ((string kind, string name, string _) in Entries())
        {
            if (kind == "member" && !Exempt.Contains(name) && !HasMember(name))
            {
                missing.Add(name);
            }
        }
        Assert.Empty(missing);
    }

    [Fact]
    public void EachExemptionIsStillNeeded()
    {
        List<string> names = Entries().Select(e => e.Name).ToList();
        foreach (string exempt in Exempt)
        {
            Assert.Contains(exempt, names);
            Assert.False(HasMember(exempt), exempt + " now exists in the shim; remove it from the exemption list");
        }
    }
}
