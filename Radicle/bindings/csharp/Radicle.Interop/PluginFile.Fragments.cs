using System;
using System.Collections.Generic;
using Radicle.Interop.Native;

namespace Radicle.Interop;

// Constraint: PACK, INFO and SCEN store fragment bindings under a flag byte, so the key a host passes is the bit index the writer shifts: OnBegin 0, OnEnd 1, OnChange 2. Only PACK carries the third bit; the writer refuses a key past a record's own bit count.
public enum VmadFragmentFlag : byte
{
    OnBegin = 0,
    OnEnd = 1,
    OnChange = 2,
}

// Constraint: each record family keys its VMAD fragment bindings differently, so a host must address a fragment through the shape that matches the record it holds: QUST by (stage, stage index), PERK and TERM by fragment index, PACK/INFO/SCEN by flag bit. Every shape delegates to the existing typed writer entry points, so a refusal leaves the plugin bytes untouched and a repeated edit reports unchanged.
// Constraint: a scene phase fragment is keyed by (phase index, phase flag), so a caller must pass both and the flag must name one phase event.
public enum VmadPhaseFlag : byte
{
    OnStart = 1,
    OnCompletion = 2,
}

// Invariant: Unknown is the entry's own byte, which a write preserves and a new binding sets to zero.
public sealed record ScenePhaseFragmentInfo(uint PhaseIndex, VmadPhaseFlag Flag, byte Unknown, string Script, string Function);

public sealed unsafe partial class PluginFile
{
    // Constraint: a quest may bind one stage twice, so the pair form is required; the writer refuses an ambiguous stage-only address and a pair it does not hold.
    public bool SetQuestFragment(FormKey record, ushort stage, int stageIndex, string script, string function)
    {
        FragmentName(script, "VMAD fragment script name");
        FragmentName(function, "VMAD fragment function name");
        return SetQuestVmadFragment(record, stage, stageIndex, script, function);
    }

    // Constraint: the caller must supply stageIndex, the log entry slot the fragment list is keyed by; false means the plugin has no such record, the record has no VMAD, or its VMAD has no fragment section with a named fragment script, and nothing is written; a pair that is already bound or a non-QUST record throws RadicleException with the bytes untouched.
    public bool AddQuestFragment(FormKey record, ushort stage, int stageIndex, string script, string function)
    {
        FragmentName(script, "VMAD fragment script name");
        FragmentName(function, "VMAD fragment function name");
        return Edit(record, (plugin, s, key) => RadicleNative.PluginAddQuestVmadFragment(plugin, key, stage, stageIndex, script, function));
    }

    // Constraint: false means the pair is absent; a non-QUST record is refused by the writer instead of reporting absence.
    public bool RemoveQuestFragment(FormKey record, ushort stage, int stageIndex) =>
        RemoveQuestVmadFragment(record, stage, stageIndex);

    // Constraint: PERK fragments are keyed by the fragment index the record's own fragment list uses, not by position.
    public void AddPerkFragment(FormKey record, ushort index, string script, string function) =>
        AddIndexedFragment(record, index, script, function);

    public void RemovePerkFragment(FormKey record, ushort index) => RemoveVmadFragment(record, index);

    public void AddTerminalFragment(FormKey record, ushort index, string script, string function) =>
        AddIndexedFragment(record, index, script, function);

    public void RemoveTerminalFragment(FormKey record, ushort index) => RemoveVmadFragment(record, index);

    // Constraint: a package carries three flag bits, so OnChange is addressable only on PACK.
    public void AddPackageFragment(FormKey record, VmadFragmentFlag flag, string script, string function)
    {
        FragmentFlag(flag, VmadFragmentFlag.OnChange, "PACK");
        AddKeyedFragment(record, (uint)flag, script, function);
    }

    public void RemovePackageFragment(FormKey record, VmadFragmentFlag flag)
    {
        FragmentFlag(flag, VmadFragmentFlag.OnChange, "PACK");
        RemoveVmadFragment(record, (uint)flag);
    }

    // Constraint: INFO carries only the OnBegin and OnEnd bits the writer exposes, so OnChange is refused before the ABI call.
    public void AddInfoFragment(FormKey record, VmadFragmentFlag flag, string script, string function)
    {
        FragmentFlag(flag, VmadFragmentFlag.OnEnd, "INFO");
        AddKeyedFragment(record, (uint)flag, script, function);
    }

    public void RemoveInfoFragment(FormKey record, VmadFragmentFlag flag)
    {
        FragmentFlag(flag, VmadFragmentFlag.OnEnd, "INFO");
        RemoveVmadFragment(record, (uint)flag);
    }

    // Constraint: SCEN carries two fragment lists, so this pair addresses only the two record-level flag bits; a phase fragment is keyed by (phase index, phase flag) and goes through SetScenePhaseFragment and RemoveScenePhaseFragment.
    public void AddSceneFragment(FormKey record, VmadFragmentFlag flag, string script, string function)
    {
        FragmentFlag(flag, VmadFragmentFlag.OnEnd, "SCEN");
        AddKeyedFragment(record, (uint)flag, script, function);
    }

    public void RemoveSceneFragment(FormKey record, VmadFragmentFlag flag)
    {
        FragmentFlag(flag, VmadFragmentFlag.OnEnd, "SCEN");
        RemoveVmadFragment(record, (uint)flag);
    }

    // Constraint: a set creates an unbound (phase index, phase flag) pair and renames a bound one, so a fresh scene gains its first phase fragment.
    public void SetScenePhaseFragment(FormKey record, uint phaseIndex, VmadPhaseFlag flag, string script, string function)
    {
        PhaseFragmentAddress(flag);
        FragmentName(script, "VMAD fragment script name");
        FragmentName(function, "VMAD fragment function name");
        using var use = Use();
        using var strings = new NativeStrings();
        Call.Check(RadicleNative.PluginSetSceneVmadPhaseFragment(use.Pointer, strings.Key(record), phaseIndex, (byte)flag, script, function));
    }

    // Constraint: false means the plugin has no such record, the record has no fragment section, or the (phase index, phase flag) pair is not bound; nothing is written in any of those cases.
    public bool RemoveScenePhaseFragment(FormKey record, uint phaseIndex, VmadPhaseFlag flag)
    {
        PhaseFragmentAddress(flag);
        return Edit(record, (plugin, s, key) => RadicleNative.PluginRemoveSceneVmadPhaseFragment(plugin, key, phaseIndex, (byte)flag));
    }

    // Constraint: a scene has no fragment section until one is bootstrapped, so this must run before the first phase binding; a second bootstrap of the same record is refused, so a caller binds first and bootstraps only when the bind reports NotFound.
    public void BootstrapFragmentSection(FormKey record, string script)
    {
        FragmentName(script, "VMAD fragment script name");
        using var use = Use();
        using var strings = new NativeStrings();
        Call.Check(RadicleNative.PluginBootstrapVmadFragment(use.Pointer, strings.Key(record), script));
    }

    // Constraint: the reads stay on the plugin being edited, so a caller sees unsaved phase edits and a record without a fragment section reports an empty list.
    public IReadOnlyList<ScenePhaseFragmentInfo> ScenePhaseFragments(FormKey record)
    {
        using var use = Use();
        using var strings = new NativeStrings();
        IntPtr handle;
        var status = RadicleNative.PluginVmadPhaseFragments(use.Pointer, strings.Key(record), &handle);
        if (status == Status.NotFound) return Array.Empty<ScenePhaseFragmentInfo>();
        Call.Check(status);
        try
        {
            var count = RadicleNative.VmadPhaseFragmentCount(handle);
            var result = new ScenePhaseFragmentInfo[checked((int)count)];
            for (var index = 0; index < result.Length; ++index)
            {
                Native.VmadPhaseFragment row;
                Call.Check(RadicleNative.VmadPhaseFragmentsAt(handle, (nuint)index, &row));
                result[index] = new ScenePhaseFragmentInfo(row.PhaseIndex, (VmadPhaseFlag)row.PhaseFlag, row.Unknown,
                    Utf8.Read(row.Script) ?? string.Empty, Utf8.Read(row.Function) ?? string.Empty);
            }
            return result;
        }
        finally
        {
            RadicleNative.FreeVmadPhaseFragments(handle);
        }
    }

    // Constraint: a flag outside OnStart and OnCompletion names no phase event, so it is refused before the ABI call.
    private static void PhaseFragmentAddress(VmadPhaseFlag flag)
    {
        if (flag != VmadPhaseFlag.OnStart && flag != VmadPhaseFlag.OnCompletion)
            throw new ArgumentOutOfRangeException(nameof(flag), flag, "A scene phase fragment flag must be OnStart or OnCompletion.");
    }

    // Constraint: the keyed writer takes the raw fragment index, and both TERM and PERK declare that index as an unsigned 16-bit field.
    private void AddIndexedFragment(FormKey record, ushort index, string script, string function)
    {
        FragmentName(script, "VMAD fragment script name");
        FragmentName(function, "VMAD fragment function name");
        AddVmadFragment(record, index, script, function);
    }

    private void AddKeyedFragment(FormKey record, uint key, string script, string function)
    {
        FragmentName(script, "VMAD fragment script name");
        FragmentName(function, "VMAD fragment function name");
        AddVmadFragment(record, key, script, function);
    }

    // Constraint: the writer refuses an empty fragment script name and requires valid UTF-8, so both names are checked before the record is touched.
    private static void FragmentName(string value, string what)
    {
        ArgumentNullException.ThrowIfNull(value, what);
        if (value.Length == 0) throw new ArgumentException($"{what} is required.", what);
        if (value.Contains('\0')) throw new ArgumentException($"{what} cannot hold an embedded NUL through this API.", what);
    }

    // Constraint: only the flag bits the record type carries are addressable, so a wider bit is refused here instead of becoming a key the writer rejects as out of range.
    private static void FragmentFlag(VmadFragmentFlag flag, VmadFragmentFlag highest, string shape)
    {
        if (!Enum.IsDefined(flag)) throw new ArgumentOutOfRangeException(nameof(flag), flag, "Unknown VMAD fragment flag.");
        if ((byte)flag > (byte)highest)
            throw new ArgumentOutOfRangeException(nameof(flag), flag,
                $"{shape} carries only OnBegin and OnEnd; OnChange is a PACK-only flag bit.");
    }
}
