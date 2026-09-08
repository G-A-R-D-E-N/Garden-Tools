using System.Buffers.Binary;

namespace OpenCommonwealth.Services.Nif;

public sealed record NifEmbeddedPhysics(
    int BlockIndex, string BlockType,
    int HavokVersion, byte[] Bytes);

public static class NifPhysics
{
    private static readonly string[] Blocks =
    {
        "bhkRagdollSystem",
        "bhkPhysicsSystem",
    };

    public static IReadOnlyList<NifEmbeddedPhysics> Read(NifFile nif)
    {
        List<NifEmbeddedPhysics> answer = new();
        foreach (var block in nif.BlocksOfType(Blocks))
        {
            int at = nif.BlockOffset[block];
            int end = at + nif.BlockSize[block];
            if (at < 0 || end < at || end > nif.Bytes.Length || end - at < 4)
                throw new InvalidDataException($"{nif.Path}: physics block {block} does not fit in the NIF");

            int count = (int)BinaryPrimitives.ReadUInt32LittleEndian(nif.Bytes.AsSpan(at, 4));
            at += 4;
            if (count < 0 || count > nif.BlockSize[block] - 4)
                throw new InvalidDataException($"{nif.Path}: embedded Havok length {count} does not fit in block {block}");

            answer.Add(new NifEmbeddedPhysics(
                block, nif.BlockTypes[nif.BlockTypeIndex[block]],
                nif.Version, nif.Bytes.AsSpan(at, count).ToArray()));
        }
        return answer;
    }
}
