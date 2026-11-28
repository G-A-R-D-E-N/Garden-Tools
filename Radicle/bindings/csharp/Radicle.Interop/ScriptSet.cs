using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public enum ArchiveFailurePolicy
{
    Fail = 0,
    Skip = 1,
}

// Invariant: ArchiveIndex counts the archives the caller passed to Open, including any that were skipped.
public sealed record ScriptArchiveFailure(int ArchiveIndex, string Path, string Message);

public sealed record ScriptSetEntry(string Name, string Path, AssetOrigin Origin, string Container, int LoadIndex,
                                  ulong Size, int Replaced);

public sealed record NativeCallSite(string Script, string Function, int Instruction, bool IsStatic, string Target,
                                    string CalledFunction);

public sealed record NativeSurfaceRow(string Script, string DeclaringClass, string Native, int CallCount, bool BoundInHost);

public sealed record NativeSurface(IReadOnlyList<NativeSurfaceRow> Rows, IReadOnlyList<string> Unresolved);

public sealed record ScriptCensus(int Objects, IReadOnlyList<string> Unreadable, IReadOnlyList<NativeCallSite> Sites,
                                  int ScriptCalls, IReadOnlyList<string> Unresolved);

internal sealed class ScriptSetHandle : SafeHandleZeroOrMinusOneIsInvalid
{
    public ScriptSetHandle(IntPtr handle) : base(ownsHandle: true) => SetHandle(handle);

    protected override bool ReleaseHandle()
    {
        RadicleNative.ScriptSetDestroy(handle);
        return true;
    }
}

internal sealed class NativeSurfaceHandle : SafeHandleZeroOrMinusOneIsInvalid
{
    public NativeSurfaceHandle(IntPtr handle) : base(ownsHandle: true) => SetHandle(handle);

    protected override bool ReleaseHandle()
    {
        RadicleNative.NativeSurfaceDestroy(handle);
        return true;
    }
}

internal sealed class ScriptCensusHandle : SafeHandleZeroOrMinusOneIsInvalid
{
    public ScriptCensusHandle(IntPtr handle) : base(ownsHandle: true) => SetHandle(handle);

    protected override bool ReleaseHandle()
    {
        RadicleNative.ScriptCensusDestroy(handle);
        return true;
    }
}

public sealed unsafe class ScriptSet : IDisposable
{
    private readonly ScriptSetHandle _handle;
    private HandleUse? _assetsHold;

    static ScriptSet() => RadicleLibrary.Register();

    private ScriptSet(IntPtr handle, HandleUse? assetsHold = null)
    {
        _handle = new ScriptSetHandle(handle);
        _assetsHold = assetsHold;
        try
        {
            Failures = ReadFailures(_handle);
        }
        catch
        {
            Dispose();
            throw;
        }
    }

    // Invariant: roots run from high to low priority and archives from low to high, as AssetResolver.OpenRoots takes them.
    public static ScriptSet Open(IEnumerable<string> roots, IEnumerable<string> archives,
                                 ArchiveFailurePolicy policy = ArchiveFailurePolicy.Fail)
    {
        ArgumentNullException.ThrowIfNull(roots);
        ArgumentNullException.ThrowIfNull(archives);
        if (policy != ArchiveFailurePolicy.Fail && policy != ArchiveFailurePolicy.Skip)
            throw new ArgumentOutOfRangeException(nameof(policy));
        using var strings = new NativeStrings();
        var rootPaths = roots.Select(strings.Add).ToArray();
        var archivePaths = archives.Select(strings.Add).ToArray();
        IntPtr handle;
        fixed (IntPtr* r = rootPaths)
        fixed (IntPtr* a = archivePaths)
            Call.Check(RadicleNative.ScriptSetOpen(r, (nuint)rootPaths.Length, a, (nuint)archivePaths.Length,
                                                   (ArchivePolicy)(int)policy, &handle));
        return new ScriptSet(handle);
    }

    // Invariant: the set holds a reference on assets until Dispose, so the caller may dispose assets first.
    // Invariant: rootLoadIndexes holds one index per root of assets, or is null for the high-to-low root order.
    public static ScriptSet Resolve(AssetResolver assets, IReadOnlyList<int>? rootLoadIndexes = null)
    {
        ArgumentNullException.ThrowIfNull(assets);
        var indexes = rootLoadIndexes?.ToArray() ?? Array.Empty<int>();
        var hold = assets.Use();
        IntPtr handle = IntPtr.Zero;
        try
        {
            fixed (int* i = indexes)
                Call.Check(RadicleNative.ScriptSetResolve(hold.Pointer, indexes.Length == 0 ? null : i, (nuint)indexes.Length, &handle));
        }
        catch
        {
            hold.Dispose();
            throw;
        }
        return new ScriptSet(handle, hold);
    }

    public IReadOnlyList<ScriptArchiveFailure> Failures { get; }

    public int Count
    {
        get
        {
            using var use = Use();
            return checked((int)RadicleNative.ScriptSetCount(use.Pointer));
        }
    }

    public ScriptSetEntry Winner(int index)
    {
        using var use = Use();
        ScriptSetWinner winner;
        Call.Check(RadicleNative.ScriptSetGet(use.Pointer, (nuint)index, &winner));
        var candidate = winner.Winner;
        return new ScriptSetEntry(Utf8.Read(winner.Name) ?? string.Empty, Utf8.Read(candidate.Path) ?? string.Empty,
                                candidate.Origin, Utf8.Read(candidate.Container) ?? string.Empty,
                                candidate.LoadIndex, candidate.Size, checked((int)winner.Replaced));
    }

    public IEnumerable<ScriptSetEntry> Winners => Enumerable.Range(0, Count).Select(Winner);

    public byte[] Read(int index)
    {
        using var use = Use();
        byte* bytes;
        nuint size;
        Call.Check(RadicleNative.ScriptSetRead(use.Pointer, (nuint)index, &bytes, &size));
        return Call.TakeBytes(bytes, size);
    }

    // Invariant: a missing parent, an unreadable payload and a cycle are invalid; an object without a parent is valid.
    public bool ParentChainValid(string objectName)
    {
        ArgumentNullException.ThrowIfNull(objectName);
        using var use = Use();
        int valid;
        Call.Check(RadicleNative.ScriptSetParentChainValid(use.Pointer, objectName, &valid));
        return valid != 0;
    }

    public ScriptCensus Census()
    {
        using var use = Use();
        IntPtr handle;
        Call.Check(RadicleNative.ScriptSetCensus(use.Pointer, &handle));
        using var census = new ScriptCensusHandle(handle);
        using var censusUse = new HandleUse(census, nameof(ScriptCensus));
        ScriptCensusTotals totals;
        Call.Check(RadicleNative.ScriptCensusTotalsGet(censusUse.Pointer, &totals));
        var unreadable = new string[checked((int)totals.UnreadableCount)];
        for (var i = 0; i < unreadable.Length; i++)
        {
            IntPtr text;
            Call.Check(RadicleNative.ScriptCensusUnreadable(censusUse.Pointer, (nuint)i, &text));
            unreadable[i] = Utf8.Read(text) ?? string.Empty;
        }
        var sites = new NativeCallSite[checked((int)totals.SiteCount)];
        for (var i = 0; i < sites.Length; i++)
        {
            ScriptCensusSite site;
            Call.Check(RadicleNative.ScriptCensusSiteGet(censusUse.Pointer, (nuint)i, &site));
            sites[i] = new NativeCallSite(Utf8.Read(site.Script) ?? string.Empty, Utf8.Read(site.Function) ?? string.Empty,
                                          checked((int)site.Instruction), site.IsStatic != 0,
                                          Utf8.Read(site.Target) ?? string.Empty,
                                          Utf8.Read(site.CalledFunction) ?? string.Empty);
        }
        var unresolved = new string[checked((int)totals.UnresolvedCount)];
        for (var i = 0; i < unresolved.Length; i++)
        {
            IntPtr text;
            Call.Check(RadicleNative.ScriptCensusUnresolved(censusUse.Pointer, (nuint)i, &text));
            unresolved[i] = Utf8.Read(text) ?? string.Empty;
        }
        return new ScriptCensus(checked((int)totals.ObjectCount), unreadable, sites,
                                checked((int)totals.ScriptCallCount), unresolved);
    }

    // Invariant: scripts and bound natives compare without case; bound natives are "Class.Function" names; a script the set lacks adds no row.
    public NativeSurface BuildNativeSurface(IEnumerable<string> scripts, IEnumerable<string> boundNatives)
    {
        ArgumentNullException.ThrowIfNull(scripts);
        ArgumentNullException.ThrowIfNull(boundNatives);
        using var use = Use();
        using var strings = new NativeStrings();
        var scriptNames = scripts.Select(strings.Add).ToArray();
        var bound = boundNatives.Select(strings.Add).ToArray();
        IntPtr handle;
        fixed (IntPtr* s = scriptNames)
        fixed (IntPtr* b = bound)
            Call.Check(RadicleNative.ScriptNativeSurfaceBuild(use.Pointer, s, (nuint)scriptNames.Length, b, (nuint)bound.Length, &handle));
        using var surface = new NativeSurfaceHandle(handle);
        using var surfaceUse = new HandleUse(surface, nameof(NativeSurface));
        var rows = new NativeSurfaceRow[checked((int)RadicleNative.NativeSurfaceCount(surfaceUse.Pointer))];
        for (var i = 0; i < rows.Length; i++)
        {
            Native.NativeSurfaceRow row;
            Call.Check(RadicleNative.NativeSurfaceRowGet(surfaceUse.Pointer, (nuint)i, &row));
            rows[i] = new NativeSurfaceRow(Utf8.Read(row.Script) ?? string.Empty, Utf8.Read(row.DeclaringClass) ?? string.Empty,
                                           Utf8.Read(row.Native) ?? string.Empty, checked((int)row.CallCount), row.BoundInHost != 0);
        }
        var unresolved = new string[checked((int)RadicleNative.NativeSurfaceUnresolvedCount(surfaceUse.Pointer))];
        for (var i = 0; i < unresolved.Length; i++)
        {
            IntPtr text;
            Call.Check(RadicleNative.NativeSurfaceUnresolved(surfaceUse.Pointer, (nuint)i, &text));
            unresolved[i] = Utf8.Read(text) ?? string.Empty;
        }
        return new NativeSurface(rows, unresolved);
    }

    public void Dispose()
    {
        _handle.Dispose();
        if (_assetsHold is { } hold)
        {
            _assetsHold = null;
            hold.Dispose();
        }
    }

    private HandleUse Use() => new(_handle, nameof(ScriptSet));

    private static ScriptArchiveFailure[] ReadFailures(ScriptSetHandle handle)
    {
        using var use = new HandleUse(handle, nameof(ScriptSet));
        var failures = new ScriptArchiveFailure[checked((int)RadicleNative.ScriptSetFailureCount(use.Pointer))];
        for (var i = 0; i < failures.Length; i++)
        {
            ScriptSetFailureInfo failure;
            Call.Check(RadicleNative.ScriptSetFailure(use.Pointer, (nuint)i, &failure));
            failures[i] = new ScriptArchiveFailure(checked((int)failure.ArchiveIndex), Utf8.Read(failure.Path) ?? string.Empty,
                                                   Utf8.Read(failure.Message) ?? string.Empty);
        }
        return failures;
    }
}
