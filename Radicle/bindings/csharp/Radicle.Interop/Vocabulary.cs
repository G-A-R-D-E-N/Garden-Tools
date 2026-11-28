using System;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public readonly record struct SignatureTraits(bool Named, bool TrapLike);

// Constraint: these are Radicle's fixed identifier tables (not xEdit labels); a value with no row answers null so the caller keeps its numeric or signature fallback.
public static unsafe class Vocabulary
{
    public static string? Name(VocabTable table, uint value)
    {
        byte* text = null;
        nuint size = 0;
        return Call.Found(RadicleNative.VocabName((uint)table, value, &text, &size)) ? Call.TakeLatin1(text, size) : null;
    }

    // Invariant: a signature is exactly four ASCII bytes, packed little-endian, the key RECORD_CLASS and the traits export use.
    public static uint Pack(string signature)
    {
        ArgumentNullException.ThrowIfNull(signature);
        if (signature.Length != 4) throw new ArgumentException("a signature is four characters", nameof(signature));
        uint packed = 0;
        for (var i = 0; i < 4; i++)
        {
            if (signature[i] > 0x7F) throw new ArgumentException("a signature is ASCII", nameof(signature));
            packed |= (uint)signature[i] << (8 * i);
        }
        return packed;
    }

    public static string? ClassName(string signature) => Name(VocabTable.RecordClass, Pack(signature));

    public static SignatureTraits Traits(string signature)
    {
        uint bits;
        Call.Check(RadicleNative.VocabSignatureTraits(Pack(signature), &bits));
        return new SignatureTraits((bits & (uint)VocabTrait.Named) != 0, (bits & (uint)VocabTrait.TrapLike) != 0);
    }
}
