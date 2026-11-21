using System;
using System.Collections.Generic;
using System.Linq;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public enum NpcVisualSource : byte { FaceGen = 0, Skin = 1, Outfit = 2, ObjectTemplate = 3 }

public sealed record NpcVisualPart(string MeshEntry, uint BipedSlots, NpcVisualSource Source);

public sealed record NpcVisual(FormKey Npc, FormKey? Race, string? RaceEditorId, bool Female, float[]? SkinTint,
    string? SkeletonHkxEntry, IReadOnlyList<NpcVisualPart> Parts, string? IdleClipEntry, IReadOnlyList<string> Stubs,
    bool SeedDependent = false);

public sealed unsafe partial class RadicleEnvironment
{
    [Obsolete("Tools only: pass the level the actor spawns at and a stable seed, or every placed reference draws level 1.")]
    public NpcVisual NpcVisual(Record npc) => NpcVisual(npc, null, 0);

    /// <param name="level">Must be the level the actor actually spawns at, 1..65535.</param>
    /// <param name="seed">Must be stable for one placed reference.</param>
    public NpcVisual NpcVisual(Record npc, ushort? level, ulong seed)
    {
        using var use = Use(npc);
        var options = new Native.NpcVisualOptions();
        options.HasLevel = (byte)(level.HasValue ? 1 : 0);
        options.Level = level ?? 0;
        options.Seed = seed;
        Native.NpcVisual* v;
        Call.Check(RadicleNative.NpcVisualDecodeEx(use.Pointer, npc.Handle, &options, &v));
        try
        {
            var parts = Call.Copy<Native.NpcVisualPart>(v->Parts, v->PartCount)
                .Select(p => new NpcVisualPart(Utf8.Read(p.MeshEntry)!, p.BipedSlots, (NpcVisualSource)p.Source)).ToArray();
            var stubs = Call.Copy<IntPtr>(v->Stubs, v->StubCount).Select(s => Utf8.Read(s) ?? string.Empty).ToArray();
            return new(FormKey.FromNative(v->Npc)!.Value, v->HasRace == 0 ? null : FormKey.FromNative(v->Race),
                Utf8.Read(v->RaceEditorId), v->Female != 0,
                v->HasSkinTint == 0 ? null : new[] { v->SkinTint[0], v->SkinTint[1], v->SkinTint[2], v->SkinTint[3] },
                Utf8.Read(v->SkeletonHkxEntry), parts, Utf8.Read(v->IdleClipEntry), stubs, v->SeedDependent != 0);
        }
        finally { RadicleNative.NpcVisualFree(v); }
    }
}
