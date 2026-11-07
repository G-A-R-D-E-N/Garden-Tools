using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using System.Reflection;
using System.Threading;
using BehaviourStudio.App;
using Xunit;

namespace BehaviourGraph.Tests;

public sealed class ViewportMeshThreadingTests
{
    private const int Vertices = 4000;

    private static List<MeshPart> Frame(int frame, int[] indices)
    {
        var positions = new Vector3[Vertices];
        for (int i = 0; i < positions.Length; i++)
            positions[i] = new Vector3(frame, frame + i * 0.001f, frame * 2f);
        return new List<MeshPart> { new() { Positions = positions, Indices = indices } };
    }

    [Fact]
    public void ARenderThreadNeverPacksAMixOfTwoFrames()
    {
        var indices = Enumerable.Range(0, Vertices - 2).SelectMany(i => new[] { i, i + 1, i + 2 }).ToArray();
        var frames = Enumerable.Range(1, 8).Select(f => Frame(f, indices)).ToArray();
        var geometry = MeshGeometry.FromParts(frames[0]);
        int[] firstIndices = geometry.Indices;

        using var stop = new CancellationTokenSource();
        int refreshes = 0;
        var producer = new Thread(() =>
        {
            int at = 0;
            while (!stop.IsCancellationRequested)
            {
                geometry.Refresh(frames[at++ % frames.Length]);
                Interlocked.Increment(ref refreshes);
            }
        }) { IsBackground = true };
        producer.Start();

        float[] packed = Array.Empty<float>();
        int torn = 0, checkedFrames = 0, distinctFrames = 0;
        float lastFrame = -1;
        try
        {
            SpinWait.SpinUntil(() => Volatile.Read(ref refreshes) > 0, 5000);
            for (int pass = 0; pass < 3000; pass++)
            {
                var snapshotIndices = geometry.PackSnapshot(ref packed);
                Assert.Same(firstIndices, snapshotIndices);
                float frame = packed[0];
                if (frame != lastFrame) { distinctFrames++; lastFrame = frame; }
                for (int i = 0; i < Vertices; i++)
                {
                    int at = i * MeshGeometry.FloatsPerVertex;
                    if (packed[at] != frame || packed[at + 1] != frame + i * 0.001f || packed[at + 2] != frame * 2f)
                    {
                        torn++;
                        break;
                    }
                }
                checkedFrames++;
            }
        }
        finally
        {
            stop.Cancel();
            producer.Join();
        }

        Assert.Equal(3000, checkedFrames);
        Assert.True(Volatile.Read(ref refreshes) > 1, "the producer never overlapped the reader");
        Assert.True(distinctFrames > 1, "the reader only ever saw one frame");
        Assert.Equal(0, torn);
    }

    [Fact]
    public void ChangedTopologyGetsANewIndexArrayInstead()
    {
        var first = new[] { 0, 1, 2 };
        var geometry = MeshGeometry.FromParts(new List<MeshPart>
        {
            new() { Positions = new Vector3[3], Indices = first },
        });
        var published = geometry.Indices;

        geometry.Refresh(new List<MeshPart> { new() { Positions = new Vector3[3], Indices = new[] { 0, 2, 1 } } });

        Assert.NotSame(published, geometry.Indices);
        Assert.Equal(new[] { 0, 1, 2 }, published);
        Assert.Equal(new[] { 0, 2, 1 }, geometry.Indices);
    }

    [Fact]
    public void SameTopologyKeepsTheIndexArrayInstance()
    {
        var geometry = MeshGeometry.FromParts(new List<MeshPart>
        {
            new() { Positions = new Vector3[3], Indices = new[] { 0, 1, 2 } },
        });
        var published = geometry.Indices;

        geometry.Refresh(new List<MeshPart> { new() { Positions = new Vector3[3], Indices = new[] { 0, 1, 2 } } });

        Assert.Same(published, geometry.Indices);
    }

    [Fact]
    public void PackSnapshotResizesTheBufferToTheMeshItPacks()
    {
        var geometry = MeshGeometry.FromParts(new List<MeshPart>
        {
            new() { Positions = new Vector3[5], Indices = new[] { 0, 1, 2 } },
        });
        float[] packed = new float[1];

        geometry.PackSnapshot(ref packed);

        Assert.Equal(5 * MeshGeometry.FloatsPerVertex, packed.Length);
    }

    [Fact]
    public void EveryShaderSourceStartsWithItsVersionDirective()
    {
        var shaders = typeof(Viewport3D)
            .GetFields(BindingFlags.NonPublic | BindingFlags.Public | BindingFlags.Static)
            .Where(field => field.IsLiteral && field.FieldType == typeof(string) && field.Name.Contains("Shader"))
            .Select(field => (field.Name, Source: (string)field.GetRawConstantValue()!))
            .ToList();

        Assert.Equal(8, shaders.Count);
        foreach (var (name, source) in shaders)
            Assert.True(source.StartsWith("#version ", StringComparison.Ordinal), name + " does not start with #version");
    }
}
