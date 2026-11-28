using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public sealed record PapyrusSaveCapture(string Json, string Digest);

public sealed record PapyrusSaveRestoreReport(string Report, int Resumed, int Rejected);

public sealed record PapyrusInstanceStateCapture(string Blob, string Digest, IReadOnlyList<ulong> EngineHandles);

public sealed record PapyrusInstanceStateReport(int Instances, int Variables, int SkippedInstances, int SkippedVariables, int UnboundReferences, string Report);

public sealed record PapyrusInstanceIdentity(PapyrusOwner Owner, string Script);

public static unsafe class PapyrusSave
{
    static PapyrusSave() => RadicleLibrary.Register();

    public static string? ScriptFingerprint(PexFile pex, string script)
    {
        ArgumentNullException.ThrowIfNull(pex);
        ArgumentNullException.ThrowIfNull(script);
        using var use = pex.Use();
        IntPtr fingerprint;
        if (!Call.Found(RadicleNative.PapyrusScriptFingerprint(use.Pointer, script, &fingerprint))) return null;
        return TakeString(fingerprint);
    }

    public static PapyrusSaveRestoreReport Restore(string json, IReadOnlyDictionary<string, string> fingerprints)
    {
        ArgumentNullException.ThrowIfNull(json);
        ArgumentNullException.ThrowIfNull(fingerprints);
        using var strings = new NativeStrings();
        var pairs = fingerprints.ToArray();
        var scripts = strings.AddAll(pairs.Select(pair => pair.Key));
        var prints = strings.AddAll(pairs.Select(pair => pair.Value));
        IntPtr report;
        int resumed, rejected;
        fixed (IntPtr* scriptPointer = scripts)
        fixed (IntPtr* printPointer = prints)
            Call.Check(RadicleNative.PapyrusSaveRestore(json, scriptPointer, printPointer, (nuint)scripts.Length, &report, &resumed, &rejected));
        return new(TakeString(report), resumed, rejected);
    }

    internal static string TakeString(IntPtr text)
    {
        try { return Utf8.Read(text) ?? string.Empty; }
        finally { RadicleNative.PapyrusStringFree(text); }
    }
}

public sealed unsafe partial class PapyrusRuntime
{
    public PapyrusSaveCapture CaptureSave()
    {
        using var runtime = UseRuntime();
        IntPtr json, digest;
        Call.Check(RadicleNative.PapyrusSaveCapture(runtime.Pointer, &json, &digest));
        return new(PapyrusSave.TakeString(json), PapyrusSave.TakeString(digest));
    }

    public PapyrusInstanceStateCapture CaptureState()
    {
        using var runtime = UseRuntime();
        var capture = new Native.PapyrusStateCapture { StructSize = (nuint)sizeof(Native.PapyrusStateCapture) };
        Call.Check(RadicleNative.PapyrusRuntimeStateCapture(runtime.Pointer, &capture));
        try
        {
            var handles = new ulong[checked((int)capture.EngineHandleCount)];
            for (var i = 0; i < handles.Length; i++) handles[i] = ((ulong*)capture.EngineHandles)[i];
            return new(Utf8.Read(capture.Blob) ?? string.Empty, Utf8.Read(capture.Digest) ?? string.Empty, handles);
        }
        finally { RadicleNative.PapyrusStateCaptureDestroy(&capture); }
    }

    public PapyrusInstanceStateReport RestoreState(string blob, bool skipUnknown = false, IReadOnlyDictionary<ulong, ulong>? engineHandles = null) =>
        RestoreCore(blob, skipUnknown, engineHandles, null);

    // Invariant: only the named identities are restored; every other instance keeps its state and is not counted, and an identity the blob does not hold restores nothing.
    public PapyrusInstanceStateReport RestoreInstances(string blob, IReadOnlyCollection<PapyrusInstanceIdentity> identities, bool skipUnknown = false, IReadOnlyDictionary<ulong, ulong>? engineHandles = null)
    {
        ArgumentNullException.ThrowIfNull(identities);
        foreach (var identity in identities)
            if (identity?.Owner is null) throw new ArgumentNullException(nameof(identities), "an instance identity and its owner are required");
        return RestoreCore(blob, skipUnknown, engineHandles, identities);
    }

    private PapyrusInstanceStateReport RestoreCore(string blob, bool skipUnknown, IReadOnlyDictionary<ulong, ulong>? engineHandles, IReadOnlyCollection<PapyrusInstanceIdentity>? identities)
    {
        ArgumentNullException.ThrowIfNull(blob);
        using var runtime = UseRuntime();
        var map = (engineHandles ?? new Dictionary<ulong, ulong>()).Select(pair => new Native.PapyrusEngineHandleMap { Saved = pair.Key, Live = pair.Value }).ToArray();
        var report = new Native.PapyrusStateReport { StructSize = (nuint)sizeof(Native.PapyrusStateReport) };
        var size = (nuint)Encoding.UTF8.GetByteCount(blob);
        var flags = skipUnknown ? 1u : 0u;
        fixed (Native.PapyrusEngineHandleMap* mapPointer = map)
        {
            Native.PapyrusEngineHandleMap* maps = map.Length == 0 ? null : mapPointer;
            if (identities is null)
                Call.Check(RadicleNative.PapyrusRuntimeStateRestore(runtime.Pointer, blob, size, flags, maps, (nuint)map.Length, &report));
            else
            {
                using var strings = new NativeStrings();
                var kinds = identities.Select(identity => identity.Owner.Kind).ToArray();
                var bases = strings.AddAll(identities.Select(identity => identity.Owner.BaseFormKey));
                var discriminators = strings.AddAll(identities.Select(identity => identity.Owner.Discriminator));
                var scripts = strings.AddAll(identities.Select(identity => identity.Script));
                fixed (Native.PapyrusOwnerKind* kindPointer = kinds)
                fixed (IntPtr* basePointer = bases)
                fixed (IntPtr* discriminatorPointer = discriminators)
                fixed (IntPtr* scriptPointer = scripts)
                    Call.Check(RadicleNative.PapyrusRuntimeStateRestoreInstances(runtime.Pointer, blob, size, flags, maps, (nuint)map.Length,
                        kinds.Length == 0 ? null : kindPointer, bases.Length == 0 ? null : basePointer, discriminators.Length == 0 ? null : discriminatorPointer,
                        scripts.Length == 0 ? null : scriptPointer, (nuint)kinds.Length, &report));
            }
        }
        try { return new(report.Instances, report.Variables, report.SkippedInstances, report.SkippedVariables, report.UnboundReferences, Utf8.Read(report.Report) ?? string.Empty); }
        finally { RadicleNative.PapyrusStateReportDestroy(&report); }
    }
}
