using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using BehaviourStudio.App;
using OpenCommonwealth.Services.Hkx;
using OpenCommonwealth.Services.Nif;
using Xunit;

namespace BehaviourStudio.Tests;

public sealed class Viewport3DTests
{
    private static readonly (double Yaw, double Pitch, double Zoom, double PanX, double PanY)[] Cameras =
    {
        (Math.PI / 2, 0, 1, 0, 0),
        (0, 0, 1, 0, 0),
        (0.7, -0.8, 2.4, 37, -19),
        (-2.1, 1.3, 0.35, -120, 88),
    };

    private static readonly Vector3[] Samples =
    {
        new(0, 0, 0),
        new(10, -4, 3),
        new(-25, 18, 70),
        new(0.5f, 0.25f, -0.75f),
        new(120, 0, -60),
    };

    [Fact]
    public void GlMatrixAgreesWithTheTwoDimensionalProjection()
    {
        const double width = 733, height = 512;
        foreach (var (yaw, pitch, zoom, panX, panY) in Cameras)
        {
            var camera = new ViewportCamera(yaw, pitch, zoom, panX, panY, new Vector3(3, -2, 9), 42);
            foreach (var sample in Samples)
            {
                var flat = camera.Project(sample, width, height);
                var matrix = camera.ProjectThroughMatrix(sample, width, height);
                Assert.True(Math.Abs(flat.X - matrix.X) < 0.01, $"x {flat.X} vs {matrix.X}");
                Assert.True(Math.Abs(flat.Y - matrix.Y) < 0.01, $"y {flat.Y} vs {matrix.Y}");
            }
        }
    }

    [Fact]
    public void TheDefaultCameraKeepsTheOldTwoDimensionalMapping()
    {
        var camera = new ViewportCamera(Math.PI / 2, 0, 1, 0, 0, Vector3.Zero, 10);
        const double width = 400, height = 400;

        var (x, y) = camera.Project(new Vector3(0, 5, 0), width, height);

        double scale = Math.Min(width, height) / (10 * ViewportCamera.FitSpan);
        Assert.Equal(width / 2 - 5 * scale, x, 6);
        Assert.Equal(height / 2, y, 6);
    }

    [Fact]
    public void ProjectionTracksZoomAndPanInPixels()
    {
        var plain = new ViewportCamera(0, 0, 1, 0, 0, Vector3.Zero, 10);
        var panned = new ViewportCamera(0, 0, 1, 30, -14, Vector3.Zero, 10);
        var zoomed = new ViewportCamera(0, 0, 2, 0, 0, Vector3.Zero, 10);
        var sample = new Vector3(4, 6, 0);

        var first = plain.Project(sample, 500, 500);
        var moved = panned.Project(sample, 500, 500);
        Assert.Equal(first.X + 30, moved.X, 3);
        Assert.Equal(first.Y - 14, moved.Y, 3);

        var doubled = zoomed.Project(sample, 500, 500);
        Assert.Equal(250 + (first.X - 250) * 2, doubled.X, 3);
        Assert.Equal(250 + (first.Y - 250) * 2, doubled.Y, 3);

        var centred = new ViewportCamera(0, 0, 1, 0, 0, sample, 10).Project(sample, 500, 500);
        Assert.Equal(250, centred.X, 3);
        Assert.Equal(250, centred.Y, 3);
    }

    [Fact]
    public void ComputedNormalsAreUnitLengthAndFaceOutward()
    {
        var positions = new[]
        {
            new Vector3(-1, -1, -1), new Vector3(1, -1, -1), new Vector3(1, 1, -1), new Vector3(-1, 1, -1),
            new Vector3(-1, -1, 1), new Vector3(1, -1, 1), new Vector3(1, 1, 1), new Vector3(-1, 1, 1),
        };
        var indices = new[]
        {
            0, 2, 1, 0, 3, 2,
            4, 5, 6, 4, 6, 7,
            0, 1, 5, 0, 5, 4,
            2, 3, 7, 2, 7, 6,
            1, 2, 6, 1, 6, 5,
            0, 4, 7, 0, 7, 3,
        };

        var normals = MeshGeometry.ComputeNormals(positions, indices);

        Assert.Equal(positions.Length, normals.Length);
        for (int i = 0; i < normals.Length; i++)
        {
            Assert.Equal(1f, normals[i].Length(), 4);
            Assert.True(Vector3.Dot(normals[i], Vector3.Normalize(positions[i])) > 0.7f);
        }
    }

    [Fact]
    public void NormalsFollowTheSkinnedTrianglePositions()
    {
        var rest = new[] { new Vector3(0, 0, 0), new Vector3(1, 0, 0), new Vector3(0, 1, 0) };
        var indices = new[] { 0, 1, 2 };
        var rotation = Matrix4x4.CreateRotationX(0.4f);
        var posed = rest.Select(v => Vector3.Transform(v, rotation)).ToArray();

        var restNormals = MeshGeometry.ComputeNormals(rest, indices);
        var posedNormals = MeshGeometry.ComputeNormals(posed, indices);
        var expected = Vector3.Transform(restNormals[0], rotation);

        Assert.Equal(expected.X, posedNormals[0].X, 4);
        Assert.Equal(expected.Y, posedNormals[0].Y, 4);
        Assert.Equal(expected.Z, posedNormals[0].Z, 4);
    }

    [Fact]
    public void DegenerateTrianglesKeepAUsableNormal()
    {
        var positions = new[] { new Vector3(1, 1, 1), new Vector3(1, 1, 1), new Vector3(1, 1, 1) };
        var normals = MeshGeometry.ComputeNormals(positions, new[] { 0, 1, 2 });

        Assert.All(normals, normal => Assert.Equal(new Vector3(0, 0, 1), normal));
    }

    [Fact]
    public void PackingInterleavesPositionsAndNormals()
    {
        var positions = new[] { new Vector3(1, 2, 3), new Vector3(4, 5, 6) };
        var normals = new[] { new Vector3(0, 1, 0), new Vector3(0, 0, -1) };

        var packed = MeshGeometry.PackVertices(positions, normals);

        Assert.Equal(positions.Length * MeshGeometry.FloatsPerVertex, packed.Length);
        Assert.Equal(new[] { 1f, 2f, 3f, 0f, 1f, 0f, 4f, 5f, 6f, 0f, 0f, -1f }, packed);
    }

    [Fact]
    public void GeometryConcatenatesPartsAndOffsetsTheirIndices()
    {
        var parts = new List<MeshPart>
        {
            new()
            {
                Positions = new[] { new Vector3(0, 0, 0), new Vector3(1, 0, 0), new Vector3(0, 1, 0) },
                Indices = new[] { 0, 1, 2 },
            },
            new()
            {
                Positions = new[] { new Vector3(0, 0, 5), new Vector3(1, 0, 5), new Vector3(0, 1, 5) },
                Indices = new[] { 2, 1, 0 },
            },
        };

        var geometry = MeshGeometry.FromParts(parts);

        Assert.Equal(6, geometry.VertexCount);
        Assert.Equal(2, geometry.TriangleCount);
        Assert.Equal(new[] { 0, 1, 2, 5, 4, 3 }, geometry.Indices);
        Assert.All(geometry.Normals, normal => Assert.Equal(1f, normal.Length(), 4));
        Assert.Null(geometry.Uvs);
    }

    [Fact]
    public void SuppliedNormalsAndUvsSurvivePacking()
    {
        var supplied = new[] { new Vector3(0, 0, 1), new Vector3(0, 1, 0), new Vector3(1, 0, 0) };
        var uvs = new[] { new Vector2(0, 0), new Vector2(1, 0), new Vector2(0, 1) };
        var part = new MeshPart
        {
            Positions = new[] { new Vector3(0, 0, 0), new Vector3(1, 0, 0), new Vector3(0, 1, 0) },
            Indices = new[] { 0, 1, 2 },
            Normals = supplied,
            Uvs = uvs,
        };

        var geometry = MeshGeometry.FromParts(new List<MeshPart> { part });

        Assert.NotNull(geometry.Uvs);
        Assert.Equal(uvs[2], geometry.Uvs![2]);
        Assert.Equal(supplied[0], geometry.Normals[0]);
        Assert.Equal(supplied[0], Vector3.Normalize(new Vector3(geometry.Pack()[3], geometry.Pack()[4],
                                                                 geometry.Pack()[5])));
    }
    [Fact]
    public void SkinBoneNamesResolveAgainstTheSkeleton()
    {
        var rig = new HkxSkeleton { Name = "Rig", BoneNames = { "Root", "Middle", "Tip" } };
        var shape = new NifShape { Name = "Body" };
        shape.BoneNames.Add("Middle_skin");
        shape.BoneNames.Add("TipSpur");

        MainWindow.ResolveSkinBoneNames(shape, rig);

        Assert.Equal("Middle", shape.BoneNames[0]);
        Assert.Equal("TipSpur", shape.BoneNames[1]);
        var binding = SkinnedMesh.Bind(shape, rig);
        Assert.Single(binding.Unmatched);
        Assert.Equal(1, binding.ToSkeleton[0]);
    }
}