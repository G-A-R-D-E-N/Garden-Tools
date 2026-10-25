using System;
using OpenCommonwealth.Services.Hkx;

namespace BehaviourStudio.Tests;

internal sealed record CollisionSample(byte[] Bytes, int System, int BodyHeader);

internal static class NifCollisionFixture
{
    public const float SphereRadius = 0.75f;

    public const uint MaterialHash0 = 0x11223344u;
    public const uint MaterialHash1 = 0x55667788u;

    public static CollisionSample Build()
    {
        var file = new CollisionPackfile();

        int system = file.AddObject("hknpPhysicsSystemData");
        int bsProperties = file.AddObject("hknpBSMaterialProperties");

        int sphere = file.AddObject("hknpSphereShape");
        file.F32(sphere + CollisionPackfile.Member("hknpSphereShape", "convexRadius"), SphereRadius);

        int polytope = file.AddObject("hknpConvexPolytopeShape");
        file.RelArray(polytope + CollisionPackfile.Member("hknpConvexPolytopeShape", "vertices"),
                      CollisionPackfile.Floats(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0), 3);
        file.RelArray(polytope + CollisionPackfile.Member("hknpConvexPolytopeShape", "faces"),
                      new byte[] { 0, 0, 3, 0 }, 1);
        file.RelArray(polytope + CollisionPackfile.Member("hknpConvexPolytopeShape", "indices"),
                      new byte[] { 0, 1, 2 }, 3);

        int compound = file.AddObject("hknpCompoundShape");
        int instanceSize = CollisionPackfile.Size("hknpShapeInstance");
        int instances = file.Array(compound + CollisionPackfile.Member("hknpCompoundShape", "instances"),
                                   new byte[2 * instanceSize], 2);

        Identify(file, instances, 0);
        int transformAt = CollisionPackfile.Member("hknpShapeInstance", "transform");
        int scaleAt = CollisionPackfile.Member("hknpShapeInstance", "scale");
        file.F32(instances + transformAt + 48, 0.0f);
        file.F32(instances + scaleAt, 1.0f);
        file.F32(instances + scaleAt + 4, 1.0f);
        file.F32(instances + scaleAt + 8, 1.0f);
        file.F32(instances + scaleAt + 12, 1.0f);
        file.Point(instances + CollisionPackfile.Member("hknpShapeInstance", "shape"), sphere);

        int second = instances + instanceSize;
        Identify(file, second, 1.0f);
        file.F32(second + transformAt + 48, 1.0f);
        file.F32(second + scaleAt, 1.0f);
        file.F32(second + scaleAt + 4, 1.0f);
        file.F32(second + scaleAt + 8, 1.0f);
        file.F32(second + scaleAt + 12, 1.0f);
        file.Point(second + CollisionPackfile.Member("hknpShapeInstance", "shape"), polytope);

        int mesh = file.AddObject("hknpCompressedMeshShape");
        int data = file.AddObject("hknpCompressedMeshShapeData");
        file.Point(mesh + CollisionPackfile.Member("hknpCompressedMeshShape", "data"), data);
        Mesh(file, data);

        int materials = file.Array(system + CollisionPackfile.Member("hknpPhysicsSystemData", "materials"),
                                   new byte[2 * CollisionPackfile.Size("hknpMaterial")], 2);
        for (int i = 0; i < 2; i++)
        {
            int at = materials + i * CollisionPackfile.Size("hknpMaterial");
            file.Half(at + CollisionPackfile.Member("hknpMaterial", "dynamicFriction"), i == 0 ? 0.5f : 0.75f);
            file.Half(at + CollisionPackfile.Member("hknpMaterial", "staticFriction"), i == 0 ? 0.25f : 0.5f);
            file.Half(at + CollisionPackfile.Member("hknpMaterial", "restitution"), i == 0 ? 0.125f : 0.25f);
            file.U8(at + CollisionPackfile.Member("hknpMaterial", "frictionCombinePolicy"), (byte)(i == 0 ? 1 : 2));
            file.U8(at + CollisionPackfile.Member("hknpMaterial", "restitutionCombinePolicy"), (byte)(i == 0 ? 2 : 1));
        }

        int bs = file.Array(bsProperties + CollisionPackfile.Member("hknpBSMaterialProperties", "MaterialA"),
                            new byte[2 * CollisionPackfile.Size("hknpBSMaterial")], 2);
        int crc = CollisionPackfile.Member("hknpBSMaterial", "uiMaterialCRC");
        file.U32(bs + crc, MaterialHash0);
        file.U32(bs + CollisionPackfile.Size("hknpBSMaterial") + crc, MaterialHash1);

        int bodyLayout = CollisionPackfile.Size("hknpBodyCinfo");
        int bodiesHeader = system + CollisionPackfile.Member("hknpPhysicsSystemData", "bodyCinfos");
        int bodies = file.Array(bodiesHeader, new byte[3 * bodyLayout], 3);

        Body(file, bodies + 0 * bodyLayout, sphere, materialId: 0, flags: 1);
        Body(file, bodies + 1 * bodyLayout, compound, materialId: 1, flags: 2);
        Body(file, bodies + 2 * bodyLayout, mesh, materialId: 0, flags: 4);

        int positionAt = CollisionPackfile.Member("hknpBodyCinfo", "position");
        file.F32(bodies + 0 * bodyLayout + positionAt, 1.0f);
        file.F32(bodies + 0 * bodyLayout + positionAt + 4, 2.0f);
        file.F32(bodies + 0 * bodyLayout + positionAt + 8, 3.0f);
        file.F32(bodies + 0 * bodyLayout + positionAt + 12, 1.0f);
        file.F32(bodies + 0 * bodyLayout + CollisionPackfile.Member("hknpBodyCinfo", "orientation") + 12, 1.0f);

        int referenced = file.Array(system + CollisionPackfile.Member("hknpPhysicsSystemData", "referencedObjects"),
                                    new byte[8], 1);
        file.Point(referenced, bsProperties);

        return new CollisionSample(file.Finish(), system, bodiesHeader);
    }

    public static byte[] Prefix(byte[] bytes)
    {
        var prefixed = new byte[bytes.Length + 4];
        bytes.CopyTo(prefixed, 4);
        return prefixed;
    }

    private static void Body(CollisionPackfile file, int at, int shape, int materialId, int flags)
    {
        file.Point(at + CollisionPackfile.Member("hknpBodyCinfo", "shape"), shape);
        file.U16(at + CollisionPackfile.Member("hknpBodyCinfo", "materialId"), (ushort)materialId);
        file.I32(at + CollisionPackfile.Member("hknpBodyCinfo", "flags"), flags);
    }

    private static void Identify(CollisionPackfile file, int at, float translateX)
    {
        file.F32(at, 1.0f);
        file.F32(at + 4, 0.0f);
        file.F32(at + 8, 0.0f);
        file.F32(at + 12, 0.0f);
        file.F32(at + 16, 0.0f);
        file.F32(at + 20, 1.0f);
        file.F32(at + 24, 0.0f);
        file.F32(at + 28, 0.0f);
        file.F32(at + 32, 0.0f);
        file.F32(at + 36, 0.0f);
        file.F32(at + 40, 1.0f);
        file.F32(at + 44, 0.0f);
        file.F32(at + 48, translateX);
    }

    private static void Mesh(CollisionPackfile file, int data)
    {
        const string treeClass = "hknpCompressedMeshShapeTree";
        int tree = data + CollisionPackfile.Member("hknpCompressedMeshShapeData", "meshTree");

        int domain = tree + CollisionPackfile.Member(treeClass, "domain");
        file.F32(domain + 12, 0.0f);
        file.F32(domain + 16, 1.0f);
        file.F32(domain + 20, 1.0f);
        file.F32(domain + 24, 1.0f);

        int sections = file.Array(tree + CollisionPackfile.Member(treeClass, "sections"),
                                  new byte[CollisionPackfile.Size("hkcdStaticMeshTreeBaseSection")], 1);
        file.Array(tree + CollisionPackfile.Member(treeClass, "primitives"),
                   new byte[] { 0, 1, 2, 2, 1, 2, 3, 3 }, 2);
        file.Array(tree + CollisionPackfile.Member(treeClass, "sharedVerticesIndex"),
                   new byte[] { 0, 0 }, 1);

        int packed = file.Array(tree + CollisionPackfile.Member(treeClass, "packedVertices"), new byte[12], 3);
        file.U32(packed + 0, Packed32(1, 2, 3));
        file.U32(packed + 4, Packed32(4, 5, 6));
        file.U32(packed + 8, Packed32(7, 8, 9));

        file.Array(tree + CollisionPackfile.Member(treeClass, "sharedVertices"), new byte[8], 1);

        const string sectionClass = "hkcdStaticMeshTreeBaseSection";
        int parms = sections + CollisionPackfile.Member(sectionClass, "codecParms");
        file.F32(parms + 12, 1.0f);
        file.F32(parms + 16, 1.0f);
        file.F32(parms + 20, 1.0f);

        file.U32(sections + CollisionPackfile.Member(sectionClass, "firstPackedVertex"), 0);
        file.U32(sections + CollisionPackfile.Member(sectionClass, "sharedVertices"), 0);
        file.U32(sections + CollisionPackfile.Member(sectionClass, "primitives"), 0);
        file.U8(sections + CollisionPackfile.Member(sectionClass, "numPackedVertices"), 3);
        file.U8(sections + CollisionPackfile.Member(sectionClass, "numSharedIndices"), 1);
    }

    public static uint Packed32(int x, int y, int z) =>
        (uint)(z << 22 | y << 11 | x);
}
